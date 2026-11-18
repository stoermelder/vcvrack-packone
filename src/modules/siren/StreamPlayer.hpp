#pragma once
#include "../../plugin.hpp"
#include "Settings.hpp"
#include "AudioStream.hpp"
#include "DataSource.hpp"
#include <atomic>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <thread>

namespace StoermelderPackOne {
namespace Siren {

// StreamPlayer
// Streaming sample player: a fill thread decodes (and resamples) into a lock-free
// ring, the DSP thread consumes it frame by frame.
//
// Threads and what each may call:
//   UI   — openStream / adoptStream / startPlayback / stopPlayback
//   fill — fillOnce (driven by the internal thread)
//   DSP  — nextFrame, setEngineSampleRate
//
// Every counter has exactly one writing thread:
//   streamTotalFrames        UI
//   pending* commands        UI (set) / fill (consume), plus the DSP's loop restart
//   seekBase + seekGen       fill (after a prime, so the zero-crossing offset is included)
//   dspBase / dspCount       DSP (adopts seekBase when seekGen changes, restarts on loop wrap)
//   declick state            DSP (starts a ramp whenever it sees a new seekGen)
//   playheadPos              DSP (the UI only primes it as a display hint while idle)
//   sampleRateRatio          fill
class StreamPlayer {
public:
	static constexpr float DECLICK_TIME = 0.015f;  // ~15 ms
	static constexpr size_t RB_SIZE = 1 << 13;     // 8192 frames ≈ 186 ms at 44.1 kHz

	StreamPlayer(const std::atomic<float>* trimIn, const std::atomic<float>* trimOut)
			: trimInRef(trimIn), trimOutRef(trimOut) {
		fillThread = std::thread(&StreamPlayer::fillThreadFunc, this);
	}

	~StreamPlayer() {
		stopFillThread();
		// Delete any pending stream the fill thread never consumed
		delete pendingStream.exchange(nullptr, std::memory_order_acq_rel);
	}

	// Stops and joins the fill thread (idempotent). The destructor calls it; tests use
	// it to keep pending commands from being consumed.
	void stopFillThread() {
		fillThreadStop = true;
		fillCv.notify_all();
		if (fillThread.joinable()) fillThread.join();
	}

	// ─── Observers (any thread) ───────────────────────────────────────────────

	bool isPlaying() const {
		return playing.load(std::memory_order_acquire);
	}
	float playhead() const {
		return playheadPos.load(std::memory_order_relaxed);
	}
	int engineSampleRate() const {
		return engineSampleRateAtomic.load(std::memory_order_relaxed);
	}
	bool isPreviewActive() const {
		return previewActive.load(std::memory_order_relaxed);
	}
	void setPreviewActive(bool v) {
		previewActive.store(v, std::memory_order_relaxed);
	}

	// UI: display hint for the playhead while idle (e.g. restoring the saved position).
	void setPlayhead(float pos) {
		playheadPos.store(pos, std::memory_order_relaxed);
	}

	// ─── UI thread ────────────────────────────────────────────────────────────

	// Open a streaming decoder for the given item. Ownership of the new AudioStream
	// goes to the fill thread via the atomic.
	void openStream(const std::string& id, DataSource* src) {
		AudioStream* ns = (src && !id.empty()) ? src->openAudioStream(id).release() : nullptr;
		// Delete any previous pending stream that the fill thread hasn't consumed yet
		delete pendingStream.exchange(ns, std::memory_order_acq_rel);

		int64_t total = 0;
		if (src && !id.empty()) {
			AudioInfo inf;
			src->loadAudioInfo(id, inf);
			total = inf.frameCount;
		}
		streamTotalFrames.store(total, std::memory_order_relaxed);
		fillCv.notify_one();
	}

	// Adopt an in-memory AudioStream (e.g. loop preview). totalFrames must be given by
	// the caller since loadAudioInfo is unavailable.
	void adoptStream(std::unique_ptr<AudioStream> stream, int64_t totalFrames) {
		AudioStream* ns = stream.release();
		delete pendingStream.exchange(ns, std::memory_order_acq_rel);
		streamTotalFrames.store(totalFrames, std::memory_order_relaxed);
		fillCv.notify_one();
	}

	// Seek to pos [0,1] and start playback.
	void startPlayback(float pos) {
		int64_t total = streamTotalFrames.load(std::memory_order_relaxed);
		int64_t sf = (total > 0) ? (int64_t)(pos * (float)total) : 0;
		playheadPos.store(pos, std::memory_order_relaxed);
		pendingSeekFrame.store(sf, std::memory_order_release);
		fillCv.notify_one();
	}

	void stopPlayback() {
		pendingStop.store(true, std::memory_order_release);
		fillCv.notify_one();
	}

	// ─── DSP thread ───────────────────────────────────────────────────────────

	void setEngineSampleRate(int sr) {
		engineSampleRateAtomic.store(sr, std::memory_order_relaxed);
	}

	// Next output frame (pre-volume), including the post-seek declick ramp.
	dsp::Frame<2> nextFrame(float trimIn, float trimOut, bool loop) {
		dsp::Frame<2> out;
		out.samples[0] = out.samples[1] = 0.f;

		// A new seek was primed: adopt its base and ramp from the last output sample
		// into the new audio instead of stepping to it.
		uint32_t gen = seekGen.load(std::memory_order_acquire);
		if (gen != dspGen) {
			dspGen = gen;
			dspBase = seekBase.load(std::memory_order_relaxed);
			dspCount = 0;
			int frames = std::max(1, (int)((float)engineSampleRateAtomic.load(std::memory_order_relaxed) * DECLICK_TIME));
			declickTotal = frames;
			declickRemaining = frames;
			declickFromL = lastOutL;
			declickFromR = lastOutR;
		}

		if (playing.load(std::memory_order_acquire)) {
			if (!rb.empty()) {
				out = rb.shift();

				int64_t count = ++dspCount;
				int64_t total = streamTotalFrames.load(std::memory_order_relaxed);
				if (total > 0) {
					// sampleRateRatio = engineRate/fileRate; convert the output frame count to
					// file-frame units so playhead and trimOut stay in file-frame space.
					float ratio = sampleRateRatio.load(std::memory_order_relaxed);
					float inputCount = (float)count / ratio;
					float ph = ((float)dspBase + inputCount) / (float)total;
					playheadPos.store(ph, std::memory_order_relaxed);

					bool preview = previewActive.load(std::memory_order_relaxed);
					int64_t stopAt = preview ? total : (int64_t)(trimOut * (float)total);
					// stopAt (file frames) as an output-frame count from the seek base
					int64_t stopAtOut = (int64_t)(((float)stopAt - (float)dspBase) * ratio);
					if (stopAtOut > 0 && count >= stopAtOut
							&& pendingSeekFrame.load(std::memory_order_relaxed) < 0) {
						if (loop) {
							// The fill thread has already wrapped at trimOut → trimIn seamlessly,
							// so the ring holds the next iteration. Only the counters restart —
							// no seek, no ring clear.
							dspBase = preview ? 0 : (int64_t)(trimIn * (float)total);
							dspCount = 0;
						}
						else {
							playing.store(false, std::memory_order_release);
						}
					}
				}
			}
			else if (eofReached.load(std::memory_order_acquire)) {
				// Ring drained and fill thread hit EOF
				if (loop && pendingSeekFrame.load(std::memory_order_relaxed) < 0) {
					int64_t total = streamTotalFrames.load(std::memory_order_relaxed);
					bool preview = previewActive.load(std::memory_order_relaxed);
					int64_t loopStart = preview ? 0 : (int64_t)(trimIn * (float)total);
					dspBase = loopStart;
					dspCount = 0;
					pendingSeekFrame.store(loopStart, std::memory_order_release);
					fillCv.notify_one();
				}
				else {
					playing.store(false, std::memory_order_release);
				}
			}
		}

		if (declickRemaining > 0) {
			float t = 1.f - (float)declickRemaining / (float)declickTotal;
			out.samples[0] = declickFromL + (out.samples[0] - declickFromL) * t;
			out.samples[1] = declickFromR + (out.samples[1] - declickFromR) * t;
			declickRemaining--;
		}
		lastOutL = out.samples[0];
		lastOutR = out.samples[1];
		return out;
	}

	// ─── Fill thread ──────────────────────────────────────────────────────────

	// One pass of the fill thread's work: apply pending commands, then top up the
	// ring while playing. Returns when there is nothing more to do right now.
	void fillOnce() {
		// stop: halt playback and drain ring
		if (pendingStop.exchange(false, std::memory_order_acq_rel)) {
			playing.store(false, std::memory_order_release);
			eofReached.store(false, std::memory_order_relaxed);
			rb.clear();
		}

		// stream swap: adopt new decoder from UI thread
		AudioStream* ns = pendingStream.exchange(nullptr, std::memory_order_acq_rel);
		if (ns) {
			delete stream;
			stream = ns;
			eofReached.store(false, std::memory_order_relaxed);
			rb.clear();
			fillFilePos = 0;
			loopHeadValid = false;
		}

		// seek: reposition, configure resampler, prime ring
		int64_t sf = pendingSeekFrame.exchange(-1, std::memory_order_acq_rel);
		if (sf >= 0 && stream) primeFromSeek(sf);

		// fill: keep ring topped up while playing
		if (playing.load(std::memory_order_relaxed) && stream && !eofReached.load(std::memory_order_relaxed)) {
			fillRing();
		}
	}

	// Shared state. Code outside StreamPlayer goes through the functions above;
	// tests may touch these directly.
	// Command channel (UI → fill thread). No mutex on any audio path.
	std::atomic<AudioStream*> pendingStream{nullptr};  // raw ptr: UI releases ownership, fill thread adopts
	std::atomic<int64_t> pendingSeekFrame{-1};         // ≥0: seek to this frame and start playing
	std::atomic<bool> pendingStop{false};              // true: stop and drain the ring

	// Single interleaved stereo ring so L+R are pushed/consumed as one unit; separate
	// L/R rings could be read between the two pushes and permanently desync.
	dsp::RingBuffer<dsp::Frame<2>, RB_SIZE> rb;

	std::atomic<bool> playing{false};
	std::atomic<bool> eofReached{false};   // fill thread sets at decoder EOF; DSP drains the ring first
	std::atomic<float> playheadPos{0.f};   // normalised; the UI reads it for display
	// True while the active stream is an in-memory processed preview spanning the full
	// [0, 1] range: trimIn/trimOut are then treated as 0/1 so the module's own trim
	// values are never overwritten by the preview.
	std::atomic<bool> previewActive{false};

	std::atomic<int64_t> streamTotalFrames{0};  // frames in the open item; set by openStream/adoptStream
	std::atomic<int> engineSampleRateAtomic{0};

	// Fill thread → DSP: file frame at which audio of the current play session starts
	// (seek target plus the zero-crossing offset). seekGen is bumped after it is stored.
	std::atomic<int64_t> seekBase{0};
	std::atomic<uint32_t> seekGen{0};
	std::atomic<float> sampleRateRatio{1.f};    // outRate/inRate

private:
	const std::atomic<float>* trimInRef;
	const std::atomic<float>* trimOutRef;

	// DSP-thread state
	uint32_t dspGen = 0;
	int64_t dspBase = 0;    // file frame at which the current play session began
	int64_t dspCount = 0;   // frames output since dspBase
	int declickRemaining = 0;
	int declickTotal = 1;
	float declickFromL = 0.f, declickFromR = 0.f;
	float lastOutL = 0.f, lastOutR = 0.f;   // most recent raw (pre-volume) output

	// Fill thread management
	std::thread fillThread;
	std::condition_variable fillCv;  // woken by UI commands or when the ring needs data
	std::mutex fillCvMutex;          // guards the condition variable only
	std::atomic<bool> fillThreadStop{false};

	// Fill-thread state — touched only by the fill thread
	AudioStream* stream = nullptr;  // owned by the fill thread
	dsp::SampleRateConverter<2> src;
	int fillCh = 1;      // channels actually pushed to the ring, clamped to 2
	int streamCh = 1;    // real channel count of the decoder — readF32 always interleaves this many
	bool fillResample = false;
	float fillRatio = 1.f;  // outRate / inRate

	// Scratch for raw decoder reads when streamCh > 2 (e.g. 5.1): readF32() interleaves
	// streamCh channels per frame, only the first fillCh (<=2) are kept.
	std::vector<float> chScratch;

	// File-frame position of the next frame pushed into the ring; lets the fill thread
	// wrap at trimOut → trimIn without clearing the ring, so the DSP never hears a gap.
	int64_t fillFilePos = 0;

	static constexpr size_t CHUNK = 1024;
	static constexpr size_t ZC_WINDOW = 2048;    // zero-crossing lookahead window (frames)
	static constexpr size_t OUT_MAX = CHUNK * 8;  // headroom for up to 8× upsampling
	float outBuf[OUT_MAX * 2];  // resampler output

	// Trim-loop declick crossfade. Looping a sub-range joins two arbitrary-amplitude
	// samples (trimOut → trimIn), which clicks. The tail of the range is equal-power
	// crossfaded into its head in file-frame (pre-resample) space so the stream the
	// resampler sees is continuous. A full-file loop needs none of this — its boundaries
	// are near silence — so it keeps the cheaper hard seek.
	static constexpr int64_t LOOP_XFADE = 512;  // ~11 ms @44.1 kHz
	float loopHead[LOOP_XFADE * 2];   // first `xfade` frames of the range, cached once
	float loopTail[LOOP_XFADE * 2];   // last `xfade` frames of the range, read each period
	bool loopHeadValid = false;       // loopHead matches the current range
	int64_t loopCacheIn = -1;         // trimIn frame loopHead was built for
	int64_t loopCacheOut = -1;        // trimOut frame loopHead was built for

	void fillThreadFunc() {
		while (!fillThreadStop.load(std::memory_order_relaxed)) {
			fillOnce();

			// sleep when idle
			std::unique_lock<std::mutex> cvLock(fillCvMutex);
			fillCv.wait_for(cvLock, std::chrono::milliseconds(5), [this] {
				return fillThreadStop.load()
					|| pendingStop.load()
					|| pendingStream.load() != nullptr
					|| pendingSeekFrame.load() >= 0
					|| (playing.load() && rb.capacity() > 256);
			});
		}
		delete stream;  // clean up on thread exit
		stream = nullptr;
	}

	// Push n frames from buf into the ring, resampling if active.
	void pushFrames(const float* buf, int n) {
		if (fillResample) {
			int inF = n, outF = (int)OUT_MAX;
			src.process(buf, fillCh, &inF, outBuf, fillCh, &outF);
			for (int f = 0; f < outF; f++) {
				if (rb.full()) break;  // SRC output can slightly exceed the capacity estimate
				dsp::Frame<2> fr;
				fr.samples[0] = outBuf[f * fillCh];
				fr.samples[1] = fillCh >= 2 ? outBuf[f * fillCh + 1] : outBuf[f * fillCh];
				rb.push(fr);
			}
		}
		else {
			for (int f = 0; f < n; f++) {
				dsp::Frame<2> fr;
				fr.samples[0] = buf[f * fillCh];
				fr.samples[1] = fillCh >= 2 ? buf[f * fillCh + 1] : buf[f * fillCh];
				rb.push(fr);
			}
		}
	}

	// Read exactly n frames into buf, zero-padding any shortfall so the fixed-size
	// crossfade buffers are always fully populated even near EOF.
	void readPadded(float* buf, int64_t n) {
		int64_t got = stream->readF32(chScratch.data(), n);
		for (int64_t f = 0; f < got; f++) {
			for (int c = 0; c < fillCh; c++) buf[f * fillCh + c] = chScratch[f * streamCh + c];
		}
		for (int64_t i = got * fillCh; i < n * fillCh; i++) buf[i] = 0.f;
	}

	// Splice the loop range's end back to its start with an equal-power crossfade,
	// hiding the click a hard trimOut→trimIn seek would produce. Returns false when
	// the ring can't yet hold the burst, so the caller retries next cycle.
	bool crossfadeWrap(int64_t inFrame, int64_t outFrame, int64_t xf) {
		size_t need = fillResample ? (size_t)std::ceil((double)xf * fillRatio) + 2 : (size_t)xf;
		if (rb.capacity() < need) return false;
		// The head is constant for the range — read it once.
		if (!loopHeadValid) {
			stream->seekTo(inFrame);
			readPadded(loopHead, xf);
			loopHeadValid = true;
		}
		stream->seekTo(outFrame - xf);
		readPadded(loopTail, xf);
		float blend[LOOP_XFADE * 2];
		for (int64_t i = 0; i < xf; i++) {
			float angle = ((xf > 1) ? (float)i / (float)(xf - 1) : 1.f) * float(M_PI) * 0.5f;
			float fo = std::cos(angle), fi = std::sin(angle);
			for (int c = 0; c < fillCh; c++) {
				blend[i * fillCh + c] = loopTail[i * fillCh + c] * fo + loopHead[i * fillCh + c] * fi;
			}
		}
		pushFrames(blend, (int)xf);
		// Resume after the head frames already consumed by the fade-in.
		stream->seekTo(inFrame + xf);
		fillFilePos = inFrame + xf;
		return true;
	}

	// Reposition the decoder, (re)configure the resampler, then prime the ring with
	// audio beginning on a zero crossing so playback starts click-free.
	void primeFromSeek(int64_t sf) {
		// SeekTo first so old audio stays in the ring during seek I/O — MP3 seek can
		// take ~100ms and an empty ring would be audible silence.
		stream->seekTo(sf);
		eofReached.store(false, std::memory_order_relaxed);
		rb.clear();
		loopHeadValid = false;

		// streamCh is the real decoder channel count; fillCh is clamped to the stereo
		// SRC limit, so files with more than 2 channels play only their first two.
		streamCh = stream->channels();
		if (streamCh < 1) streamCh = 1;
		fillCh = std::min(streamCh, 2);
		chScratch.resize((size_t)ZC_WINDOW * streamCh);

		// Resampler setup — only when file and engine rates differ.
		int inRate = stream->sampleRate(), outRate = engineSampleRateAtomic.load(std::memory_order_relaxed);
		fillResample = sirenSettings.resampleOnPlayback && inRate > 0 && outRate > 0 && inRate != outRate;
		fillRatio = fillResample ? (float)outRate / (float)inRate : 1.f;
		if (fillResample) { src.setChannels(fillCh); src.setRates(inRate, outRate); }
		sampleRateRatio.store(fillRatio, std::memory_order_relaxed);

		// ZC lookahead — discard frames up to the first zero crossing on channel 0,
		// then push the rest to prime the ring.
		float zcBuf[ZC_WINDOW * 2];
		int64_t zcRead = stream->readF32(chScratch.data(), ZC_WINDOW);
		for (int64_t f = 0; f < zcRead; f++) {
			for (int c = 0; c < fillCh; c++) zcBuf[f * fillCh + c] = chScratch[f * streamCh + c];
		}
		int64_t zcOffset = 0;
		for (int64_t i = 1; i < zcRead; i++) {
			if (zcBuf[(i - 1) * fillCh] * zcBuf[i * fillCh] <= 0.f) { zcOffset = i; break; }
		}
		for (int64_t r = zcOffset; r < zcRead; r += (int64_t)CHUNK) {
			pushFrames(zcBuf + r * fillCh, (int)std::min((int64_t)CHUNK, zcRead - r));
		}

		// Publish the audio start (seek target + zero-crossing offset) so the displayed
		// playhead matches what is actually heard; the DSP adopts it on seeing the new gen.
		seekBase.store(sf + zcOffset, std::memory_order_relaxed);
		seekGen.fetch_add(1, std::memory_order_release);
		fillFilePos = sf + zcRead;
		playing.store(true, std::memory_order_release);
	}

	// Top up the ring while playing, wrapping seamlessly at the loop boundary.
	void fillRing() {
		bool isLooping = sirenSettings.loopPlayback;
		int64_t totalFrames = streamTotalFrames.load(std::memory_order_relaxed);

		// Resolve the loop range and crossfade length. A crossfade is used only for a
		// sub-range (trim points active); a full-file loop wraps at near-silent
		// boundaries and keeps the cheaper hard seek.
		int64_t inFrame = 0;
		int64_t outFrame = 0;
		int64_t xfade = 0;
		bool trimLoop = false;
		if (isLooping && totalFrames > 0) {
			bool preview = previewActive.load(std::memory_order_relaxed);
			inFrame = preview ? 0 : (int64_t)(trimInRef->load(std::memory_order_relaxed) * totalFrames);
			outFrame = preview ? totalFrames : (int64_t)(trimOutRef->load(std::memory_order_relaxed) * totalFrames);
			if ((inFrame > 0 || outFrame < totalFrames) && inFrame < outFrame) {
				xfade = std::min((int64_t)LOOP_XFADE, (outFrame - inFrame) / 2);
				trimLoop = xfade > 0;
				// Rebuild the cached head whenever the loop range changes.
				if (inFrame != loopCacheIn || outFrame != loopCacheOut) {
					loopCacheIn = inFrame;
					loopCacheOut = outFrame;
					loopHeadValid = false;
				}
			}
		}

		// A trim loop only governs playback while the position is inside the range.
		// Seeking past trimOut disengages it: playback runs forward to EOF, where the
		// DSP's EOF handler restarts it from trimIn. This way a click after the range
		// is honoured instead of being yanked straight back into the loop.
		bool loopEngaged = trimLoop && fillFilePos < outFrame;

		// Seamless wrap WITHOUT clearing the ring, so the DSP never hears a gap. The
		// crossfaded loop stops `xfade` frames early — crossfadeWrap supplies them.
		if (loopEngaged && fillFilePos >= outFrame - xfade) {
			crossfadeWrap(inFrame, outFrame, xfade);  // no-op until the ring has room
			return;
		}

		// Full-file loop: hard seek back at the near-silent file boundary.
		if (isLooping && !trimLoop && totalFrames > 0
				&& fillFilePos >= outFrame && inFrame < outFrame) {
			stream->seekTo(inFrame);
			fillFilePos = inFrame;
		}

		size_t space = rb.capacity();
		if (space == 0) return;
		float tmp[CHUNK * 2];
		// Limit input so the resampled output fits in the available ring space. When
		// upsampling the SRC may produce up to ceil(fillRatio) output frames per input
		// frame; require that much room before reading, so one input frame never overflows.
		size_t minSpace = fillResample ? (size_t)std::ceil(fillRatio) + 1 : 1;
		if (space < minSpace) return;
		size_t toRead = fillResample
			? std::min((size_t)CHUNK, (size_t)((double)space / fillRatio))
			: std::min((size_t)CHUNK, space);
		// Never read past the loop end: an engaged trim loop stops `xfade` early, a
		// full-file loop stops at the file end, otherwise read freely to EOF.
		int64_t fillEnd = loopEngaged ? outFrame - xfade
			: (isLooping && !trimLoop && totalFrames > 0) ? outFrame : -1;
		if (fillEnd >= 0) {
			int64_t framesLeft = fillEnd - fillFilePos;
			if (framesLeft <= 0) toRead = 0;  // wrap fires next iteration
			else if (fillResample) toRead = std::min(toRead, (size_t)std::max(1.0, (double)framesLeft / fillRatio));
			else toRead = std::min(toRead, (size_t)framesLeft);
		}
		if (toRead > 0) {
			int64_t nRead = stream->readF32(chScratch.data(), (int64_t)toRead);
			for (int64_t f = 0; f < nRead; f++) {
				for (int c = 0; c < fillCh; c++) tmp[f * fillCh + c] = chScratch[f * streamCh + c];
			}
			pushFrames(tmp, (int)nRead);
			fillFilePos += nRead;
			if (nRead == 0) eofReached.store(true, std::memory_order_release);
		}
	}
};

} // namespace Siren
} // namespace StoermelderPackOne
