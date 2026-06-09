#include "../../plugin.hpp"
#include "../../pluginsettings.hpp"
#include "../../components/Knobs.hpp"
#include "Siren.hpp"
#include "SirenDataSource.hpp"
#include "SirenFileSystem.hpp"
#include "SirenAudio.hpp"
#include "SirenBrowserPane.hpp"
#include "SirenPreviewPane.hpp"
#include "SirenTopBar.hpp"
#include "SirenVuMeter.hpp"
#include <osdialog.h>


namespace StoermelderPackOne {
namespace Siren {

struct SirenModule : Module {
	enum OutputIds { 
		OUTPUT_L,
		OUTPUT_R,
		NUM_OUTPUTS
	};
	enum ParamIds {
		PARAM_VOLUME,
		PARAM_AUTOPLAY,
		NUM_PARAMS
	};
	enum InputIds {
		NUM_INPUTS
	};
	enum LightIds {
		LIGHT_AUTOPLAY,
		NUM_LIGHTS
	};

	int panelTheme = -1;  // required by ThemedModuleWidget

	// Patch-local state (persisted in the Rack patch, not in siren.json)
	std::string lastFilePath;
	float lastPlayheadPos = 0.f;
	int activeRootIdx = -1;

	// ── audio streaming ────────────────────────────────────────────────────────
	// The module owns all audio state; the widget only drives the UI.

	// ── lock-free command channel (UI → fill thread) ──────────────────────────
	// UI sets; fill thread exchange()s to nullptr/-1/false to consume.
	// No mutex on any audio path — only atomics and ring buffers.
	std::atomic<AudioStream*> pendingStream{nullptr};  // raw ptr: UI releases ownership, fill thread adopts
	std::atomic<int64_t>      pendingSeekFrame{-1};    // ≥0 means seek to this frame and start playing
	std::atomic<bool>         pendingStop{false};      // true means stop and drain ring

	// Audio ring buffers — single-producer (fill thread) / single-consumer (process())
	static constexpr size_t RB_SIZE = 1 << 13;         // 8192 frames ≈ 186 ms at 44.1 kHz
	dsp::RingBuffer<float, RB_SIZE> rbL, rbR;

	// Fill thread management
	std::thread             fillThread;
	std::condition_variable fillCv;      // woken by UI commands or when ring needs data
	std::mutex              fillCvMutex; // guards the condition variable only
	std::atomic<bool>       fillThreadStop{false};

	// Playback state
	std::atomic<bool>  playing{false};       // DSP reads; fill thread and UI write
	std::atomic<bool>  eofReached{false};    // fill thread sets at decoder EOF; DSP drains ring before stopping
	std::atomic<float> playheadPos{0.f};     // DSP writes; UI reads for display
	std::atomic<float> trimIn{0.f};          // UI writes; DSP reads for loop restart position
	std::atomic<float> trimOut{1.f};         // UI writes; DSP reads to compute stop frame

	// Position counters — written before playing=true (release), read after playing (acquire)
	int64_t              seekBaseFrame     = 0;  // file frame at which this play session began
	int64_t              streamTotalFrames = 0;  // total frames in the open item; set in openStream()
	std::atomic<int64_t> outputFrameCount{0};    // frames output since last seek; DSP increments

	// Resampling — fill thread reads, process() writes engineSampleRate
	int engineSampleRate;   // set in process(), read by fill thread
	std::atomic<float> sampleRateRatio{1.f};      // outRate/inRate; set by fill thread, read in process()

	// VU meter — written by DSP thread, read by UI thread
	std::atomic<float> levelL{-100.f};  // dBFS; -100 = silence
	std::atomic<float> levelR{-100.f};
	float peakL = -100.f;  // peak-hold state, DSP thread only
	float peakR = -100.f;

	ClockDividerEx lightDivider;

	void fillThreadFunc() {
		AudioStream* stream = nullptr;  // owned by this thread

		// Per-stream playback config — computed once per seek, reused every fill cycle.
		dsp::SampleRateConverter<2> src;
		int   fillCh       = 1;
		bool  fillResample = false;
		float fillRatio    = 1.f;  // outRate / inRate

		// Tracks the file-frame position of the next frame to be pushed into the ring.
		// The fill thread uses this to wrap seamlessly at trimOut → trimIn during looping
		// without clearing the ring, so the DSP never hears a gap.
		int64_t fillFilePos = 0;

		static constexpr size_t CHUNK   = 1024;
		static constexpr size_t OUT_MAX = CHUNK * 8;  // headroom for up to 8× upsampling
		float outBuf[OUT_MAX * 2];  // resampler output — allocated once for the thread lifetime

		// Trim-loop declick crossfade. When looping a sub-range the splice trimOut→trimIn
		// joins two arbitrary-amplitude samples, producing a click. To hide it the tail of
		// the range is equal-power crossfaded into its head, in file-frame (pre-resample)
		// space, so the reconstructed stream the resampler sees is continuous. A full-file
		// loop needs none of this — its boundaries are already near silence — so it keeps
		// the cheaper hard seek.
		static constexpr int64_t LOOP_XFADE = 512;  // ~11 ms @44.1 kHz
		float   loopHead[LOOP_XFADE * 2];   // first `xfade` frames of the range, cached once
		float   loopTail[LOOP_XFADE * 2];   // last `xfade` frames of the range, read each period
		bool    loopHeadValid = false;      // loopHead matches the current range
		int64_t loopCacheIn   = -1;         // trimIn frame loopHead was built for
		int64_t loopCacheOut  = -1;         // trimOut frame loopHead was built for

		// Push n frames from buf into the ring, resampling if active.
		auto pushFrames = [&](const float* buf, int n) {
			if (fillResample) {
				int inF = n, outF = (int)OUT_MAX;
				src.process(buf, fillCh, &inF, outBuf, fillCh, &outF);
				for (int f = 0; f < outF; f++) {
					rbL.push(outBuf[f * fillCh]);
					rbR.push(fillCh >= 2 ? outBuf[f * fillCh + 1] : outBuf[f * fillCh]);
				}
			}
			else {
				for (int f = 0; f < n; f++) {
					rbL.push(buf[f * fillCh]);
					rbR.push(fillCh >= 2 ? buf[f * fillCh + 1] : buf[f * fillCh]);
				}
			}
		};

		// Read exactly n frames into buf, zero-padding any shortfall so the fixed-size
		// crossfade buffers are always fully populated even near EOF.
		auto readPadded = [&](float* buf, int64_t n) {
			int64_t got = stream->readF32(buf, n);
			for (int64_t i = got * fillCh; i < n * fillCh; i++) buf[i] = 0.f;
		};

		// Splice the loop range's end back to its start with an equal-power crossfade,
		// hiding the click a hard trimOut→trimIn seek would produce. Works in file-frame
		// (pre-resample) space so the stream the resampler sees stays continuous. Returns
		// false when the ring can't yet hold the burst, so the caller retries next cycle.
		auto crossfadeWrap = [&](int64_t inFrame, int64_t outFrame, int64_t xf) -> bool {
			size_t need = fillResample ? (size_t)std::ceil((double)xf * fillRatio) + 2 : (size_t)xf;
			if (rbL.capacity() < need) return false;
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
				for (int c = 0; c < fillCh; c++)
					blend[i * fillCh + c] = loopTail[i * fillCh + c] * fo + loopHead[i * fillCh + c] * fi;
			}
			pushFrames(blend, (int)xf);
			// Resume after the head frames already consumed by the fade-in.
			stream->seekTo(inFrame + xf);
			fillFilePos = inFrame + xf;
			return true;
		};

		// Reposition the decoder, (re)configure the resampler, then prime the ring with
		// audio beginning on a zero crossing so playback starts click-free.
		auto primeFromSeek = [&](int64_t sf) {
			// SeekTo first so old audio stays in the ring during seek I/O — MP3 seek can
			// take ~100ms and an empty ring would be audible silence.
			stream->seekTo(sf);
			outputFrameCount.store(0, std::memory_order_relaxed);
			eofReached.store(false, std::memory_order_relaxed);
			rbL.clear(); rbR.clear();
			loopHeadValid = false;

			// Channel count — clamped to the stereo SRC limit.
			fillCh = stream->channels();
			if (fillCh < 1) fillCh = 1;
			if (fillCh > 2) fillCh = 2;

			// Resampler setup — only when file and engine rates differ.
			int inRate = stream->sampleRate(), outRate = engineSampleRate;
			fillResample = sirenSettings.resampleOnPlayback && inRate > 0 && outRate > 0 && inRate != outRate;
			fillRatio = fillResample ? (float)outRate / (float)inRate : 1.f;
			if (fillResample) { src.setChannels(fillCh); src.setRates(inRate, outRate); }
			sampleRateRatio.store(fillRatio, std::memory_order_relaxed);

			// ZC lookahead — discard frames up to the first zero crossing on channel 0,
			// then push the rest to prime the ring.
			static constexpr size_t ZC_WINDOW = 2048;
			float zcBuf[ZC_WINDOW * 2];
			int64_t zcRead = stream->readF32(zcBuf, ZC_WINDOW);
			int64_t zcOffset = 0;
			for (int64_t i = 1; i < zcRead; i++)
				if (zcBuf[(i - 1) * fillCh] * zcBuf[i * fillCh] <= 0.f) { zcOffset = i; break; }
			for (int64_t r = zcOffset; r < zcRead; r += (int64_t)CHUNK)
				pushFrames(zcBuf + r * fillCh, (int)std::min((int64_t)CHUNK, zcRead - r));

			// Shift seekBaseFrame so the displayed playhead matches the actual audio start
			// (written before the playing release-fence).
			seekBaseFrame += zcOffset;
			fillFilePos = sf + zcRead;
			playing.store(true, std::memory_order_release);
		};

		// Top up the ring while playing, wrapping seamlessly at the loop boundary.
		auto fillRing = [&]() {
			bool    isLooping   = sirenSettings.loopPlayback;
			int64_t totalFrames = streamTotalFrames;

			// Resolve the loop range and crossfade length. A crossfade is used only for a
			// sub-range (trim points active); a full-file loop wraps at near-silent
			// boundaries and keeps the cheaper hard seek.
			int64_t inFrame = 0, outFrame = 0, xfade = 0;
			bool    trimLoop = false;
			if (isLooping && totalFrames > 0) {
				inFrame  = (int64_t)(trimIn.load(std::memory_order_relaxed)  * totalFrames);
				outFrame = (int64_t)(trimOut.load(std::memory_order_relaxed) * totalFrames);
				if ((inFrame > 0 || outFrame < totalFrames) && inFrame < outFrame) {
					xfade    = std::min(LOOP_XFADE, (outFrame - inFrame) / 2);
					trimLoop = xfade > 0;
					// Rebuild the cached head whenever the loop range changes.
					if (inFrame != loopCacheIn || outFrame != loopCacheOut) {
						loopCacheIn = inFrame; loopCacheOut = outFrame; loopHeadValid = false;
					}
				}
			}

			// A trim loop only governs playback while the position is inside the range.
			// Seeking past trimOut disengages it: playback runs forward to EOF, where the
			// process() EOF handler restarts it from trimIn. This way a click after the
			// range is honoured instead of being yanked straight back into the loop.
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

			size_t space = rbL.capacity();
			if (space == 0) return;
			float tmp[CHUNK * 2];
			// Limit input so the resampled output fits in the available ring space.
			size_t toRead = fillResample
			    ? std::min(CHUNK, (size_t)std::max(1.0, (double)space / fillRatio))
			    : std::min(CHUNK, space);
			// Never read past the loop end: an engaged trim loop stops `xfade` early, a
			// full-file loop stops at the file end, otherwise read freely to EOF.
			int64_t fillEnd = loopEngaged ? outFrame - xfade
			                : (isLooping && !trimLoop && totalFrames > 0) ? outFrame : -1;
			if (fillEnd >= 0) {
				int64_t framesLeft = fillEnd - fillFilePos;
				if (framesLeft <= 0)   toRead = 0;  // wrap fires next iteration
				else if (fillResample) toRead = std::min(toRead, (size_t)std::max(1.0, (double)framesLeft / fillRatio));
				else                   toRead = std::min(toRead, (size_t)framesLeft);
			}
			if (toRead > 0) {
				int64_t nRead = stream->readF32(tmp, (int64_t)toRead);
				pushFrames(tmp, (int)nRead);
				fillFilePos += nRead;
				if (nRead == 0) eofReached.store(true, std::memory_order_release);
			}
		};

		while (!fillThreadStop.load(std::memory_order_relaxed)) {
			// ── stop: halt playback and drain ring ────────────────────────
			if (pendingStop.exchange(false, std::memory_order_acq_rel)) {
				playing.store(false, std::memory_order_release);
				eofReached.store(false, std::memory_order_relaxed);
				rbL.clear(); rbR.clear();
			}

			// ── stream swap: adopt new decoder from UI thread ─────────────
			AudioStream* ns = pendingStream.exchange(nullptr, std::memory_order_acq_rel);
			if (ns) {
				delete stream;
				stream = ns;
				eofReached.store(false, std::memory_order_relaxed);
				rbL.clear(); rbR.clear();
				fillFilePos = 0;
				loopHeadValid = false;
			}

			// ── seek: reposition, configure resampler, prime ring ─────────
			int64_t sf = pendingSeekFrame.exchange(-1, std::memory_order_acq_rel);
			if (sf >= 0 && stream) primeFromSeek(sf);

			// ── fill: keep ring topped up while playing ───────────────────
			if (playing.load(std::memory_order_relaxed) && stream && !eofReached.load(std::memory_order_relaxed))
				fillRing();

			// ── sleep when idle ───────────────────────────────────────────
			{
				std::unique_lock<std::mutex> cvLock(fillCvMutex);
				fillCv.wait_for(cvLock, std::chrono::milliseconds(5), [this] {
					return fillThreadStop.load()
					    || pendingStop.load()
					    || pendingStream.load() != nullptr
					    || pendingSeekFrame.load() >= 0
					    || (playing.load() && rbL.capacity() > 256);
				});
			}
		}

		delete stream;  // clean up on thread exit
	}

	SirenModule() {
		config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);
		configParam(PARAM_VOLUME, 0.f, 2.f, 1.f, "Volume", " dB", -10.f, 20.f);
		configSwitch(PARAM_AUTOPLAY, 0.f, 1.f, 0.f, "Autoplay", {"Off", "On"});
		paramQuantities[PARAM_AUTOPLAY]->description = "When On, the player starts automatically as soon as a new sample is loaded.";
		configOutput(OUTPUT_L, "Left audio");
		configOutput(OUTPUT_R, "Right audio");

		lightDivider.setDivision(512);

		// Dedicated fill thread — no mutexes, only atomics and ring buffer
		fillThread = std::thread(&SirenModule::fillThreadFunc, this);
	}

	~SirenModule() override {
		fillThreadStop = true;
		fillCv.notify_all();
		if (fillThread.joinable()) fillThread.join();
		// Delete any pending stream the fill thread never consumed
		delete pendingStream.exchange(nullptr, std::memory_order_acq_rel);
	}

	// Open a streaming decoder for the given item (called from UI thread).
	// Transfers ownership of the new AudioStream to the fill thread via atomic.
	void openStream(const std::string& id, DataSource* src) {
		AudioStream* ns = (src && !id.empty()) ? src->openAudioStream(id).release() : nullptr;
		// Delete any previous pending stream that the fill thread hasn't consumed yet
		delete pendingStream.exchange(ns, std::memory_order_acq_rel);

		streamTotalFrames = 0;
		if (src && !id.empty()) {
			AudioInfo inf;
			src->loadAudioInfo(id, inf);
			streamTotalFrames = inf.frameCount;
		}
		fillCv.notify_one();
	}

	// Adopt an in-memory AudioStream (e.g. loop preview) on the UI thread.
	// totalFrames must be set by the caller since loadAudioInfo is unavailable.
	void adoptStream(std::unique_ptr<AudioStream> stream, int64_t totalFrames) {
		AudioStream* ns = stream.release();
		delete pendingStream.exchange(ns, std::memory_order_acq_rel);
		streamTotalFrames = totalFrames;
		fillCv.notify_one();
	}

	// Seek to pos [0,1] and start playback (called from UI thread).
	void startPlayback(float pos) {
		int64_t total = streamTotalFrames;
		seekBaseFrame = (total > 0) ? (int64_t)(pos * (float)total) : 0;
		outputFrameCount.store(0, std::memory_order_relaxed);
		playheadPos.store(pos, std::memory_order_relaxed);
		// Send seek command to fill thread (release: seekBaseFrame visible after acquire)
		pendingSeekFrame.store(seekBaseFrame, std::memory_order_release);
		fillCv.notify_one();
	}

	void stopPlayback() {
		pendingStop.store(true, std::memory_order_release);
		fillCv.notify_one();
	}

	void onSampleRateChange(const SampleRateChangeEvent& e) override {
		engineSampleRate = e.sampleRate;
	}
	
	void process(const ProcessArgs& args) override {
		float l = 0.f, r = 0.f;

		// DSP reads ring buffer — no mutex, no file I/O
		if (playing.load(std::memory_order_acquire)) {
			if (!rbL.empty()) {
				l = rbL.shift();
				r = rbR.shift();

				int64_t count = outputFrameCount.fetch_add(1, std::memory_order_relaxed) + 1;
				int64_t total = streamTotalFrames;
				if (total > 0) {
					// sampleRateRatio = engineRate/fileRate; convert output frame count to
					// file-frame units so playhead and trimOut stay in file-frame space.
					float ratio = sampleRateRatio.load(std::memory_order_relaxed);
					float inputCount = (float)count / ratio;
					float ph = ((float)seekBaseFrame + inputCount) / (float)total;
					playheadPos.store(ph, std::memory_order_relaxed);

					int64_t stopAt = (int64_t)(trimOut.load(std::memory_order_relaxed) * (float)total);
					// Convert stopAt (file frames) to output-frame count from the seek base
					int64_t stopAtOut = (int64_t)(((float)stopAt - (float)seekBaseFrame) * ratio);
					if (stopAtOut > 0
					        && count >= stopAtOut
					        && pendingSeekFrame.load(std::memory_order_relaxed) < 0) {
						if (sirenSettings.loopPlayback) {
							// The fill thread has already wrapped at trimOut → trimIn seamlessly,
							// so the ring already contains the next iteration's audio.
							// Only the display counters need resetting — no seek, no ring clear.
							int64_t loopStart = (int64_t)(trimIn.load(std::memory_order_relaxed) * (float)total);
							seekBaseFrame = loopStart;
							outputFrameCount.store(0, std::memory_order_relaxed);
						}
						else {
							playing.store(false, std::memory_order_release);
						}
					}
				}
			}
			else if (eofReached.load(std::memory_order_acquire)) {
				// Ring drained and fill thread hit EOF
				if (sirenSettings.loopPlayback
				        && pendingSeekFrame.load(std::memory_order_relaxed) < 0) {
					int64_t total = streamTotalFrames;
					int64_t loopStart = (int64_t)(trimIn.load(std::memory_order_relaxed) * (float)total);
					seekBaseFrame = loopStart;
					outputFrameCount.store(0, std::memory_order_relaxed);
					pendingSeekFrame.store(loopStart, std::memory_order_release);
					fillCv.notify_one();
				}
				else {
					playing.store(false, std::memory_order_release);
				}
			}
		}

		float vol = params[PARAM_VOLUME].getValue();
		l *= vol * 5.f;
		r *= vol * 5.f;

		outputs[OUTPUT_L].setVoltage(l);
		outputs[OUTPUT_R].setVoltage(r);

		if (lightDivider.process()) {
			lights[LIGHT_AUTOPLAY].setBrightness(params[PARAM_AUTOPLAY].getValue() > 0.5f ? 1.f : 0.f);
			// Peak-hold with 30 dB/s decay
			auto trackPeak = [&](float& peak, std::atomic<float>& out, float sig) {
				float db = (fabsf(sig) > 1e-6f) ? 20.f * log10f(fabsf(sig) / 5.f) : -100.f;
				if (db > peak) peak = db;
				peak -= 30.f * args.sampleTime * lightDivider.division;
				if (peak < -100.f) peak = -100.f;
				out.store(peak, std::memory_order_relaxed);
			};
			trackPeak(peakL, levelL, l);
			trackPeak(peakR, levelR, r);
		}
	}

	json_t* dataToJson() override {
		json_t* rootJ = json_object();
		json_object_set_new(rootJ, "lastFile", json_string(lastFilePath.c_str()));
		json_object_set_new(rootJ, "lastPlayheadPos", json_real(lastPlayheadPos));
		json_object_set_new(rootJ, "activeRootIdx", json_integer(activeRootIdx));
		return rootJ;
	}

	void dataFromJson(json_t* rootJ) override {
		json_t* v;
		v = json_object_get(rootJ, "lastFile");        if (v) lastFilePath = json_string_value(v);
		v = json_object_get(rootJ, "lastPlayheadPos"); if (v) lastPlayheadPos = (float)json_real_value(v);
		v = json_object_get(rootJ, "activeRootIdx");   if (v) activeRootIdx = (int)json_integer_value(v);
	}
};

// ─── display widget ───────────────────────────────────────────────────────────

struct SirenDisplayWidget : OpaqueWidget {
	void draw(const DrawArgs& args) override {}
	void drawLayer(const DrawArgs& args, int layer) override {
		if (layer != 1) return;
		float b     = std::max(0.2f, settings::rackBrightness);
		float b_inv = 1.f + std::max(b - settings::rackBrightness, 0.f) * 8.f;
		nvgGlobalAlpha(args.vg, b);

		math::Rect r = box.zeroPos();

		// Outer glow — screen light bleeding onto the panel surface
		float spread = 22.f;
		NVGpaint glow = nvgBoxGradient(args.vg,
			r.pos.x, r.pos.y, r.size.x, r.size.y,
			3.f, spread,
			nvgRGBAf(0.45f, 0.70f, 1.0f, 0.12f * b),
			nvgRGBAf(0.0f,  0.0f,  0.0f, 0.0f));
		nvgBeginPath(args.vg);
		nvgRect(args.vg, r.pos.x - spread, r.pos.y - spread,
			r.size.x + 2.f * spread, r.size.y + 2.f * spread);
		nvgFillPaint(args.vg, glow);
		nvgFill(args.vg);

		// Dark gradient background
		nvgBeginPath(args.vg);
		nvgRect(args.vg, RECT_ARGS(r));
		NVGcolor topColor    = color::mult(nvgRGB(0x22, 0x22, 0x22), b_inv);
		NVGcolor bottomColor = color::mult(nvgRGB(0x12, 0x12, 0x12), b_inv);
		nvgFillPaint(args.vg, nvgLinearGradient(args.vg, 0.f, 0.f, 0.f, 25.f, topColor, bottomColor));
		nvgFill(args.vg);

		// Children (browser, preview, topbar) drawn inside the dimmed context
		OpaqueWidget::draw(args);

		// Corner vignette — subtle darkening toward edges for screen depth
		NVGpaint vignette = nvgRadialGradient(args.vg,
			r.size.x * 0.5f, r.size.y * 0.5f,
			r.size.x * 0.35f, r.size.x * 0.75f,
			nvgRGBAf(0.f, 0.f, 0.f, 0.0f),
			nvgRGBAf(0.f, 0.f, 0.f, 0.45f));
		nvgBeginPath(args.vg);
		nvgRect(args.vg, RECT_ARGS(r));
		nvgFillPaint(args.vg, vignette);
		nvgFill(args.vg);

		// Outer top stroke (shadow)
		nvgBeginPath(args.vg);
		nvgMoveTo(args.vg, 0.f, -0.5f);
		nvgLineTo(args.vg, box.size.x, -0.5f);
		nvgStrokeColor(args.vg, nvgRGBAf(0.f, 0.f, 0.f, 0.24f));
		nvgStrokeWidth(args.vg, 1.f);
		nvgStroke(args.vg);

		// Outer bottom stroke (highlight)
		nvgBeginPath(args.vg);
		nvgMoveTo(args.vg, 0.f, box.size.y + 0.5f);
		nvgLineTo(args.vg, box.size.x, box.size.y + 0.5f);
		nvgStrokeColor(args.vg, nvgRGBAf(1.f, 1.f, 1.f, 0.25f));
		nvgStrokeWidth(args.vg, 1.f);
		nvgStroke(args.vg);

		// Inner top stroke
		nvgBeginPath(args.vg);
		nvgMoveTo(args.vg, 0.f, 2.5f);
		nvgLineTo(args.vg, box.size.x, 2.5f);
		nvgStrokeColor(args.vg, nvgRGBAf(1.f, 1.f, 1.f, 0.20f));
		nvgStrokeWidth(args.vg, 1.f);
		nvgStroke(args.vg);

		// Inner bottom stroke
		nvgBeginPath(args.vg);
		nvgMoveTo(args.vg, 0.f, box.size.y - 2.5f);
		nvgLineTo(args.vg, box.size.x, box.size.y - 2.5f);
		nvgStrokeColor(args.vg, nvgRGBAf(1.f, 1.f, 1.f, 0.20f));
		nvgStrokeWidth(args.vg, 1.f);
		nvgStroke(args.vg);

		// Black border (1 px inner shrink)
		math::Rect rBorder = r.shrink(math::Vec(1.f, 1.f));
		nvgBeginPath(args.vg);
		nvgRect(args.vg, RECT_ARGS(rBorder));
		nvgStrokeColor(args.vg, bottomColor);
		nvgStrokeWidth(args.vg, 2.f);
		nvgStroke(args.vg);
	}
};


// Top-level overlay drawn above all rack elements — used for the drag label so it
// is never occluded by cables, modules, or other widgets.
struct SirenDragOverlay : widget::TransparentWidget {
	SirenDropHandler* dropHandler = nullptr;
	SirenPreviewPane* previewPane = nullptr;
	TaskWorker*       worker      = nullptr;
	bool initiated = false;

	void drawLayer(const DrawArgs& args, int layer) override {
		if (layer != 1) return;
		if (!dropHandler || !dropHandler->active) return;
		if (dropHandler->mouseIsInsideModule()) return;

		std::string lbl = !dropHandler->dragDisplayName.empty()
		                ? dropHandler->dragDisplayName
		                : dropHandler->dragPath;

		Vec mp = APP->scene->rack->getMousePos();

		nvgFontSize(args.vg, 10.f);
		nvgFontFaceId(args.vg, APP->window->uiFont->handle);
		float bounds[4];
		nvgTextBounds(args.vg, 0.f, 0.f, lbl.c_str(), nullptr, bounds);
		const float pad = 4.f;
		const float w   = bounds[2] - bounds[0] + pad * 2.f;
		const float h   = 18.f;

		nvgBeginPath(args.vg);
		nvgRoundedRect(args.vg, mp.x + 10.f, mp.y, w, h, 3.f);
		nvgFillColor(args.vg, nvgRGBAf(0.f, 0.f, 0.f, 0.7f));
		nvgFill(args.vg);
		nvgFillColor(args.vg, nvgRGBf(1.f, 0.85f, 0.1f));
		nvgText(args.vg, mp.x + 10.f + pad, mp.y + 12.f, lbl.c_str(), nullptr);
	}

	void onButton(const ButtonEvent& e) override {
		if (e.action == GLFW_PRESS && e.button == GLFW_MOUSE_BUTTON_LEFT && (e.mods & RACK_MOD_MASK) == (RACK_MOD_CTRL | RACK_MOD_SHIFT)) {
			dropHandler->startDrag(previewPane->currentNode.relativePath, previewPane->displayName);
			initiated = true;
			e.consume(this);
		}
		if (e.action == GLFW_RELEASE && e.button == GLFW_MOUSE_BUTTON_LEFT && initiated) {
			initiated = false;
			e.consume(this);
		}
		TransparentWidget::onButton(e);
	}

	void onDragEnd(const event::DragEnd& e) override {
		if (dropHandler && dropHandler->active && worker)
			dropHandler->endDrag(APP->scene->mousePos, worker);
		initiated = false;
	}
};


struct SirenOcWidget : OpaqueWidget {
	ui::Tooltip* tooltip = nullptr;

	SirenOcWidget() {
		box.size = Vec(26.f, 26.f);
	}

	void onButton(const ButtonEvent& e) override {
		if (e.action == GLFW_PRESS && e.button == GLFW_MOUSE_BUTTON_LEFT) {
			system::openBrowser("https://omricohen-music.com/");
			e.consume(this);
			return;
		}
		OpaqueWidget::onButton(e);
	}

	void onEnter(const EnterEvent& e) override {
		ui::Tooltip* tooltip = new ui::Tooltip;
		tooltip->text = "Omri Cohen";
		setTooltip(tooltip);
	}

	void onLeave(const LeaveEvent& e) override {
		setTooltip(nullptr);
	}

	void setTooltip(ui::Tooltip* tooltip) {
		if (this->tooltip) {
			this->tooltip->requestDelete();
			this->tooltip = nullptr;
		}

		if (tooltip) {
			APP->scene->addChild(tooltip);
			this->tooltip = tooltip;
		}
	}
};

struct SirenWidget : ThemedModuleWidget<SirenModule> {
	TaskWorker taskWorker{"Siren"};
	SirenDropHandler dropHandler;

	SirenBrowserPane* browserPane = nullptr;
	SirenPreviewPane* previewPane = nullptr;
	SirenDragOverlay* dragOverlay = nullptr;

	SirenWidget(SirenModule* module)
		: ThemedModuleWidget<SirenModule>(module, "Siren") {
		setModule(module);

		addChild(createWidget<StoermelderBlackScrew>(Vec(RACK_GRID_WIDTH, 0)));
		//addChild(createWidget<StoermelderBlackScrew>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, 0)));
		addChild(createWidget<StoermelderBlackScrew>(Vec(RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));
		addChild(createWidget<StoermelderBlackScrew>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));

		addChild(createParamCentered<StoermelderSmallKnob>(math::Vec(22.9f, 138.4f), module, SirenModule::PARAM_VOLUME));
		addParam(createLightParamCentered<VCVLightLatch<MediumSimpleLight<WhiteLight>>>(math::Vec(22.9f, 184.3f), module, SirenModule::PARAM_AUTOPLAY, SirenModule::LIGHT_AUTOPLAY));
		
		addOutput(createOutputCentered<StoermelderPort>(Vec(22.9f, 63.4f), module, SirenModule::OUTPUT_L));
		addOutput(createOutputCentered<StoermelderPort>(Vec(22.9f, 92.5f), module, SirenModule::OUTPUT_R));

		// VU meter: two vertical LED bars in the right margin, same top as the panes
		static constexpr float vuW = 2.f * SirenVuMeter::BAR_W + SirenVuMeter::BAR_GAP + 4.f;
		static constexpr float vuH = SirenVuMeter::NUM_SEGS * (SirenVuMeter::SEG_H + SirenVuMeter::SEG_GAP);
		SirenVuMeter* vu = new SirenVuMeter;
		vu->levelL   = module ? &module->levelL : nullptr;
		vu->levelR   = module ? &module->levelR : nullptr;
		vu->box.pos  = Vec(12.9f, 235.3f);
		vu->box.size = Vec(vuW, vuH);
		addChild(vu);

		addChild(createWidgetCentered<SirenOcWidget>(Vec(22.9f, 329.f)));

		// ── Layout constants ──────────────────────────────────────────────────
		const float contentX = 8.f;
		const float contentY = 8.f;
		const float browserW = 188.0f;
		const float previewW = 297.8f;
		const float totalW   = browserW + previewW;
		const float topBarH  = 18.f;
		const float contentH = 336.6f;
		const float paneH    = contentH - topBarH;
		const float gapW     = 4.f;   // gap between browser and preview

		// ── Display widget (single container for browser + topbar + preview) ──
		SirenDisplayWidget* display = new SirenDisplayWidget;
		display->box.pos = Vec(45.5f, 10.2f);
		display->box.size = Vec(501.7f, 354.0f);
		addChild(display);

		// ── Browser pane (inside display, local coords) ───────────────────────
		{
			const Vec displaySize = Vec(browserW, paneH);

			browserPane = new SirenBrowserPane;
			browserPane->box.pos = Vec(contentX, contentY + topBarH);   // relative to display
			browserPane->dropHandler = &dropHandler;
			browserPane->worker = &taskWorker;
			browserPane->init(&taskWorker);
			browserPane->setSize(displaySize);
			browserPane->onFileSelected = [this](const DataSourceNode& node, bool startPlay) {
				onFileSelected(node, startPlay);
			};

			{
				SirenModule* m = module;  // capture for lambdas (template base not visible by plain name)
				browserPane->onAddRoot = [this, m]() {
					char* path = osdialog_file(OSDIALOG_OPEN_DIR, nullptr, nullptr, nullptr);
					if (!path) return;
					std::string p(path);
					free(path);
					if (std::find(sirenSettings.rootContainers.begin(), sirenSettings.rootContainers.end(), p)
					    == sirenSettings.rootContainers.end()) {
						sirenSettings.rootContainers.push_back(p);
						sirenSettings.activeRootIdx = (int)sirenSettings.rootContainers.size() - 1;
						if (m) m->activeRootIdx = sirenSettings.activeRootIdx;
						browserPane->setRoots(sirenSettings.rootContainers, sirenSettings.activeRootIdx);
					}
				};
				browserPane->onSelectRoot = [this, m](int idx) {
					sirenSettings.activeRootIdx = idx;
					if (m) m->activeRootIdx = idx;
					browserPane->setRoots(sirenSettings.rootContainers, idx);
				};
			}

			display->addChild(browserPane);
		}

		// ── Top bar (inside display, local coords) ────────────────────────────
		SirenTopBar* topBar = new SirenTopBar;
		topBar->box.pos  = Vec(contentX, contentY);   // relative to display
		topBar->box.size = Vec(totalW, topBarH);
		topBar->pane = browserPane;
		topBar->init();
		display->addChild(topBar);

		// ── Preview pane (inside display, local coords) ───────────────────────
		previewPane = new SirenPreviewPane;
		previewPane->box.pos  = Vec(contentX + browserW + gapW, contentY + topBarH);  // relative to display
		previewPane->box.size = Vec(previewW, paneH);
		previewPane->init(&taskWorker, &dropHandler);
		previewPane->cacheDir = sirenCacheDirPath();
		display->addChild(previewPane);

		// The active DataSource (and its MetadataStore) is about to be destroyed —
		// drop the preview pane's references to it before they dangle.
		browserPane->onActiveSourceChanging = [this]() {
			previewPane->loadItem(DataSourceNode{}, nullptr);
		};

		if (!module) return;

		// Wire audio callbacks: pane → module
		previewPane->openStreamCallback    = [module](const std::string& id, DataSource* src) {
			module->openStream(id, src);
		};
		previewPane->adoptStreamCallback   = [module](std::unique_ptr<AudioStream> s, int64_t tf) {
			module->adoptStream(std::move(s), tf);
		};
		previewPane->startPlaybackCallback = [module](float pos) {
			module->startPlayback(pos);
		};
		previewPane->stopPlaybackCallback  = [module]() {
			module->stopPlayback();
		};
		previewPane->onMetadataChanged = [this]() {
			browserPane->requestRebuild();
		};
		previewPane->modulePlayheadPos = &module->playheadPos;
		previewPane->modulePlaying     = &module->playing;
		previewPane->moduleInPoint     = &module->trimIn;
		previewPane->moduleOutPoint    = &module->trimOut;

		// Top-level drag label overlay — drawn above all other rack elements
		dragOverlay = new SirenDragOverlay;
		dragOverlay->box.pos  = Vec(0.f, 0.f);
		dragOverlay->box.size = Vec(1e6f, 1e6f);
		dragOverlay->dropHandler = &dropHandler;
		dragOverlay->previewPane = previewPane;
		dragOverlay->worker      = &taskWorker;
		APP->scene->rack->addChild(dragOverlay);

		// Load global settings and restore state
		sirenSettings.load();
		// Patch state takes priority over global settings if the patch was saved
		if (!module->lastFilePath.empty()) {
			sirenSettings.activeRootIdx = module->activeRootIdx;
		}

		// Obtain the conversion task from the active source; dispatched by the drop handler.
		dropHandler.prepareForDropCallback = [this](const std::string& id) -> std::function<std::string()> {
			DataSource* src = browserPane->activeDataSource;
			if (!src) return [id]() { return id; };

			int targetRate   = sirenSettings.resampleOnDrop ? this->module->engineSampleRate : 0;
			bool loopOnDrop  = previewPane->isLoopPreviewActive();
			float trimIn  = previewPane->canvas ? previewPane->canvas->inPoint  : 0.f;
			float trimOut = previewPane->canvas ? previewPane->canvas->outPoint : 1.f;
			return src->prepareForDrop(id, sirenSettings.convertToWavOnDrop,
				targetRate, trimIn, trimOut, sirenSettings.resampleQuality, sirenSettings.customConvertDir,
				loopOnDrop, previewPane->loopCrossfadeDuration);
		};
		dropHandler.moduleWidget = this;

		browserPane->setRoots(sirenSettings.rootContainers, sirenSettings.activeRootIdx);
		std::string restoreFile = module ? module->lastFilePath : sirenSettings.lastFile;
		float restorePos = module ? module->lastPlayheadPos : sirenSettings.lastPlayheadPos;
		if (!restoreFile.empty()) {
			DataSource* src = browserPane->activeDataSource;
			DataSourceNode restoreNode = src ? src->resolveNode(restoreFile) : DataSourceNode{};
			previewPane->loadItem(restoreNode, src);
			module->playheadPos.store(restorePos, std::memory_order_relaxed);
			browserPane->revealPath(restoreFile);
		}
	}

	~SirenWidget() override {
		if (dragOverlay) {
			APP->scene->rack->removeChild(dragOverlay);
			delete dragOverlay;
			dragOverlay = nullptr;
		}

		// Sync preview state back into module fields (for patch save) and global settings
		sirenSettings.lastFile = previewPane->currentNode.relativePath;
		sirenSettings.lastPlayheadPos = module ? module->playheadPos.load() : 0.f;
		if (module) {
			module->lastFilePath = previewPane->currentNode.relativePath;
			module->lastPlayheadPos = module->playheadPos.load();
			module->activeRootIdx = sirenSettings.activeRootIdx;
		}

		sirenSettings.save();
		// Metadata is saved by FileSystemDataSource destructor via ~SirenBrowserPane
	}

	std::string activeRoot() const {
		int idx = sirenSettings.activeRootIdx;
		if (idx >= 0 && idx < (int)sirenSettings.rootContainers.size())
			return sirenSettings.rootContainers[idx];
		return "";
	}

	void onFileSelected(const DataSourceNode& node, bool startPlay) {
		sirenSettings.lastFile = node.relativePath;
		if (module) module->lastFilePath = node.relativePath;
		DataSource* src = browserPane->activeDataSource;
		bool autoplay = module && module->params[SirenModule::PARAM_AUTOPLAY].getValue() > 0.5f;
		previewPane->loadItem(node, src, startPlay || autoplay);
	}

	void onSelectKey(const SelectKeyEvent& e) override {
		if (e.action == GLFW_PRESS || e.action == GLFW_REPEAT) {
			if (e.key == GLFW_KEY_SPACE) {
				if (!previewPane->currentNode.relativePath.empty()) {
					if (module->playing.load(std::memory_order_relaxed)) {
						previewPane->stopPlaybackCallback();
					}
					else {
						previewPane->startPlaybackFrom(module->playheadPos.load(std::memory_order_relaxed));
					}
					e.consume(this);
					return;
				}
			}
			if (e.key == GLFW_KEY_UP || e.key == GLFW_KEY_DOWN || e.key == GLFW_KEY_LEFT || e.key == GLFW_KEY_RIGHT) {
				if (browserPane->navigateKey(e.key)) {
					e.consume(this);
					return;
				}
			}
		}
		ThemedModuleWidget<SirenModule>::onSelectKey(e);
	}

	void appendContextMenu(ui::Menu* menu) override {
		ThemedModuleWidget<SirenModule>::appendContextMenu(menu);
		menu->addChild(new ui::MenuSeparator);

		// Root container management
		for (int i = 0; i < (int)sirenSettings.rootContainers.size(); i++) {
			const std::string& root = sirenSettings.rootContainers[i];
			std::string label = root;
			bool active = (i == sirenSettings.activeRootIdx);
			menu->addChild(createCheckMenuItem(label, "", [=]() { return active; }, [this, i]() {
				sirenSettings.activeRootIdx = i;
				browserPane->setRoots(sirenSettings.rootContainers, i);
			}));
		}

		menu->addChild(createMenuItem("Add root...", "", [this]() {
			char* path = osdialog_file(OSDIALOG_OPEN_DIR, nullptr, nullptr, nullptr);
			if (!path) return;
			std::string p(path);
			free(path);
			if (std::find(sirenSettings.rootContainers.begin(), sirenSettings.rootContainers.end(), p)
			    == sirenSettings.rootContainers.end()) {
				sirenSettings.rootContainers.push_back(p);
				sirenSettings.activeRootIdx = (int)sirenSettings.rootContainers.size() - 1;
				browserPane->setRoots(sirenSettings.rootContainers, sirenSettings.activeRootIdx);
			}
		}));

		if (!sirenSettings.rootContainers.empty()) {
			menu->addChild(createMenuItem("Remove root", "", [this]() {
				if (!sirenSettings.removeActiveRoot(browserPane->activeDataSource)) return;
				browserPane->setRoots(sirenSettings.rootContainers, sirenSettings.activeRootIdx);
			}));
		}

		menu->addChild(new ui::MenuSeparator);
		menu->addChild(createBoolPtrMenuItem("Resample on playback", "", &sirenSettings.resampleOnPlayback));
		menu->addChild(createBoolPtrMenuItem("Resample on drop", "", &sirenSettings.resampleOnDrop));
		// Speex resampler quality used during "resample on drop".
		// Three presets cover the practical range: fast (1), default (6), best (10).
		menu->addChild(createSubmenuItem("Resample quality", "", [=](ui::Menu* qMenu) {
			struct QPreset { int value; std::string label; std::string desc; };
			QPreset presets[] = {
				{ 1,  "Fast",    "Lowest CPU" },
				{ 6,  "Default", "Balanced quality and CPU"      },
				{ 10, "Best",    "Highest CPU"  },
			};
			for (const QPreset& p : presets) {
				qMenu->addChild(createCheckMenuItem(p.label, p.desc,
					[=]() { return sirenSettings.resampleQuality == p.value; },
					[=]() { sirenSettings.resampleQuality = p.value; }
				));
			}
		}));
		menu->addChild(createBoolPtrMenuItem("Convert to WAV on drop", "", &sirenSettings.convertToWavOnDrop));
		menu->addChild(createSubmenuItem("Folder for converted/trimmed files", "", [=](ui::Menu* subMenu) {
			subMenu->addChild(createCheckMenuItem("Same folder as source file", "",
				[=]() { return sirenSettings.customConvertDir.empty(); },
				[=]() { sirenSettings.customConvertDir = ""; }
			));
			subMenu->addChild(createCheckMenuItem(
				sirenSettings.customConvertDir.empty() ? "Custom folder..." : sirenSettings.customConvertDir, "",
				[=]() { return !sirenSettings.customConvertDir.empty(); },
				[]() {
					char* path = osdialog_file(OSDIALOG_OPEN_DIR, nullptr, nullptr, nullptr);
					if (!path) return;
					sirenSettings.customConvertDir = path;
					free(path);
				}
			));
		}));
	}
};

} // namespace Siren
} // namespace StoermelderPackOne

Model* modelSiren = createModel<StoermelderPackOne::Siren::SirenModule, StoermelderPackOne::Siren::SirenWidget>("Siren");
