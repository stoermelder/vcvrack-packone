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
#include "SirenTagClassifier.hpp"  // loads model into TagClassifier
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
		NUM_PARAMS
	};
	enum InputIds {
		NUM_INPUTS
	};
	enum LightIds {
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
			}

			// ── seek: reposition, configure resampler, prime ring ─────────
			int64_t sf = pendingSeekFrame.exchange(-1, std::memory_order_acq_rel);
			if (sf >= 0 && stream) {
				// SeekTo first so old audio stays in the ring during seek I/O — MP3 seek
				// can take ~100ms and an empty ring causes audible silence. The spurious
				// path-2 trigger that previously required pre-clearing no longer applies:
				// the autonomous fill loop never sets eofReached, so the danger state
				// (pendingSeekFrame=-1 + eofReached=true + empty ring) cannot occur.
				stream->seekTo(sf);
				outputFrameCount.store(0, std::memory_order_relaxed);
				eofReached.store(false, std::memory_order_relaxed);
				rbL.clear(); rbR.clear();

				// Channel count — clamped to the stereo SRC limit.
				fillCh = stream->channels();
				if (fillCh < 1) fillCh = 1;
				if (fillCh > 2) fillCh = 2;

				// Resampler setup — only when file and engine rates differ.
				int inRate  = stream->sampleRate();
				int outRate = engineSampleRate;
				fillResample = sirenSettings.resampleOnPlayback && inRate > 0 && outRate > 0 && inRate != outRate;
				if (fillResample) {
					fillRatio = (float)outRate / (float)inRate;
					src.setChannels(fillCh);
					src.setRates(inRate, outRate);
					sampleRateRatio.store(fillRatio, std::memory_order_relaxed);
				}
				else {
					fillRatio = 1.f;
					sampleRateRatio.store(1.f, std::memory_order_relaxed);
				}

				// ZC lookahead — scan ahead for the first zero crossing so playback
				// starts near silence, avoiding a click. Frames before the crossing
				// are discarded; the rest are pushed into the ring to prime it.
				static constexpr size_t ZC_WINDOW = 2048;
				float zcBuf[ZC_WINDOW * 2];
				int64_t zcRead = stream->readF32(zcBuf, ZC_WINDOW);
				int64_t zcOffset = 0;
				if (zcRead > 1) {
					float prev = zcBuf[0];
					for (int64_t i = 1; i < zcRead; i++) {
						float cur = zcBuf[i * fillCh];
						if (prev * cur <= 0.f) { zcOffset = i; break; }
						prev = cur;
					}
				}
				const float* ptr = zcBuf + zcOffset * fillCh;
				int64_t remaining = zcRead - zcOffset;
				while (remaining > 0) {
					int n = (int)std::min(remaining, (int64_t)CHUNK);
					pushFrames(ptr, n);
					ptr += n * fillCh;
					remaining -= n;
				}

				// Shift seekBaseFrame forward so the displayed playhead matches
				// the actual audio start (written before playing release-fence).
				seekBaseFrame += zcOffset;
				fillFilePos = sf + zcOffset;
				playing.store(true, std::memory_order_release);
			}

			// ── fill: keep ring topped up while playing ───────────────────
			if (playing.load(std::memory_order_relaxed) && stream && !eofReached.load(std::memory_order_relaxed)) {
				bool   isLooping    = sirenSettings.loopPlayback;
				int64_t totalFrames = streamTotalFrames;

				// Seamless autonomous loop: when the fill position reaches trimOut, seek the
				// stream back to trimIn WITHOUT clearing the ring. The ring then contains a
				// continuous, gapless stream that wraps the range; the DSP only needs to reset
				// its display counters when it crosses the boundary — no seek gap, no silence.
				if (isLooping && totalFrames > 0) {
					int64_t trimOutFrame = (int64_t)(trimOut.load(std::memory_order_relaxed) * totalFrames);
					if (fillFilePos >= trimOutFrame) {
						int64_t trimInFrame = (int64_t)(trimIn.load(std::memory_order_relaxed) * totalFrames);
						if (trimInFrame < trimOutFrame) {
							stream->seekTo(trimInFrame);
							fillFilePos = trimInFrame;
						}
					}
				}

				size_t space = rbL.capacity();
				if (space > 0) {
					float tmp[CHUNK * 2];
					// Limit input so the resampled output fits in the available ring space.
					size_t toRead = fillResample
					    ? std::min(CHUNK, (size_t)std::max(1.0, (double)space / fillRatio))
					    : std::min(CHUNK, space);

					// Cap at trimOut so the ring never contains frames from beyond the range.
					if (isLooping && totalFrames > 0) {
						int64_t trimOutFrame = (int64_t)(trimOut.load(std::memory_order_relaxed) * totalFrames);
						int64_t framesLeft   = trimOutFrame - fillFilePos;
						if (framesLeft <= 0) {
							toRead = 0;  // loop-seek will fire at top of next iteration
						} 
						else if (fillResample) {
							toRead = std::min(toRead, (size_t)std::max(1.0, (double)framesLeft / fillRatio));
						} 
						else {
							toRead = std::min(toRead, (size_t)framesLeft);
						}
					}

					if (toRead > 0) {
						int64_t nRead = stream->readF32(tmp, (int64_t)toRead);
						pushFrames(tmp, (int)nRead);
						fillFilePos += nRead;
						if (nRead == 0) eofReached.store(true, std::memory_order_release);
					}
				}
			}

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
		configOutput(OUTPUT_L, "Left audio out");
		configOutput(OUTPUT_R, "Right audio out");

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
					if (count >= stopAtOut
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
	void draw(const DrawArgs& args) override {
		float b     = std::max(0.2f, settings::rackBrightness);
		float b_inv = 1.f + std::max(b - settings::rackBrightness, 0.f) * 8.f;
		nvgGlobalAlpha(args.vg, b);

		math::Rect r = box.zeroPos();

		// Dark gradient background
		nvgBeginPath(args.vg);
		nvgRect(args.vg, RECT_ARGS(r));
		NVGcolor topColor    = color::mult(nvgRGB(0x22, 0x22, 0x22), b_inv);
		NVGcolor bottomColor = color::mult(nvgRGB(0x12, 0x12, 0x12), b_inv);
		nvgFillPaint(args.vg, nvgLinearGradient(args.vg, 0.f, 0.f, 0.f, 25.f, topColor, bottomColor));
		nvgFill(args.vg);

		// Children (browser, preview, topbar) drawn inside the dimmed context
		OpaqueWidget::draw(args);

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
	bool initiated = false;

	void drawLayer(const DrawArgs& args, int layer) override {
		if (layer != 1) return;
		if (!dropHandler || !dropHandler->active) return;
		if (dropHandler->mouseIsInsideModule()) return;

		std::string lbl = (previewPane
		                && dropHandler->dragPath == previewPane->currentId
		                && !previewPane->displayName.empty())
		                ? previewPane->displayName
		                : rack::system::getFilename(dropHandler->dragPath);

		Vec mp = APP->scene->rack->getMousePos();

		nvgBeginPath(args.vg);
		nvgRoundedRect(args.vg, mp.x + 10.f, mp.y, 150.f, 18.f, 3.f);
		nvgFillColor(args.vg, nvgRGBAf(0.f, 0.f, 0.f, 0.7f));
		nvgFill(args.vg);
		nvgFontSize(args.vg, 10.f);
		nvgFillColor(args.vg, nvgRGBf(1.f, 0.85f, 0.1f));
		nvgText(args.vg, mp.x + 14.f, mp.y + 12.f, lbl.c_str(), nullptr);
	}

	void onButton(const ButtonEvent& e) override {
		if (e.action == GLFW_PRESS && e.button == GLFW_MOUSE_BUTTON_LEFT && (e.mods & RACK_MOD_MASK) == (RACK_MOD_CTRL | RACK_MOD_SHIFT)) {
			dropHandler->startDrag(previewPane->currentId);
			initiated = true;
			e.consume(this);
		}
		if (e.action == GLFW_RELEASE && e.button == GLFW_MOUSE_BUTTON_LEFT && initiated) {
			dropHandler->cancelDrag();
			initiated = false;
			e.consume(this);
		}
		TransparentWidget::onButton(e);
	}
};


struct SirenOcWidget : TransparentWidget {
	SirenOcWidget() {
		box.size = Vec(26.f, 26.f);
	}
	void onButton(const ButtonEvent& e) override {
		if (e.action == GLFW_PRESS && e.button == GLFW_MOUSE_BUTTON_LEFT) {
			system::openBrowser("https://omricohen-music.com/");
			e.consume(this);
			return;
		}
		TransparentWidget::onButton(e);
	}
};

struct SirenWidget : ThemedModuleWidget<SirenModule> {
	TaskWorker taskWorker{"Siren"};
	SirenDropHandler dropHandler;

	SirenBrowserPane*  browserPane  = nullptr;
	SirenPreviewPane*  previewPane  = nullptr;
	SirenDragOverlay*  dragOverlay  = nullptr;

	SirenWidget(SirenModule* module)
		: ThemedModuleWidget<SirenModule>(module, "Siren") {
		setModule(module);

		//addChild(createWidget<StoermelderBlackScrew>(Vec(RACK_GRID_WIDTH, 0)));
		addChild(createWidget<StoermelderBlackScrew>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, 0)));
		addChild(createWidget<StoermelderBlackScrew>(Vec(RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));
		addChild(createWidget<StoermelderBlackScrew>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));

		addChild(createParamCentered<StoermelderSmallKnob>(math::Vec(532.5f, 138.4f), module, SirenModule::PARAM_VOLUME));
		
		addOutput(createOutputCentered<StoermelderPort>(Vec(532.5f, 63.4f), module, SirenModule::OUTPUT_L));
		addOutput(createOutputCentered<StoermelderPort>(Vec(532.5f, 92.5f), module, SirenModule::OUTPUT_R));


		// ── Layout constants ──────────────────────────────────────────────────
		const float zoom     = 0.60f;
		const float contentX = 8.f;
		const float contentY = 8.f;
		const float browserW = 172.3f;
		const float previewW = 307.8f;
		const float totalW   = browserW + previewW;
		const float topBarH  = 30.f * zoom;
		const float contentH = 336.6f;
		const float paneH    = contentH - topBarH;
		const float gapW     = 8.f;   // gap between browser and preview

		// ── Display widget (single container for browser + topbar + preview) ──
		SirenDisplayWidget* display = new SirenDisplayWidget;
		display->box.pos  = Vec(8.3f, 10.2f);
		display->box.size = Vec(501.7f, 354.0f);
		addChild(display);

		// ── Browser pane (inside display, local coords) ───────────────────────
		{
			const Vec displaySize = Vec(browserW, paneH);
			const Vec logicalSize = displaySize.div(zoom);

			widget::ZoomWidget* zw = new widget::ZoomWidget;
			zw->box.pos  = Vec(contentX, contentY + topBarH);   // relative to display
			zw->box.size = displaySize;
			zw->setZoom(zoom);

			browserPane = new SirenBrowserPane;
			browserPane->box.pos = Vec(0.f, 0.f);
			browserPane->dropHandler = &dropHandler;
			browserPane->worker = &taskWorker;
			browserPane->init(&taskWorker);
			browserPane->setSize(logicalSize);
			browserPane->onFileSelected = [this](const std::string& path, bool startPlay) {
				onFileSelected(path, startPlay);
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
				browserPane->onRemoveRoot = [this, m](int idx) {
					if (idx < 0 || idx >= (int)sirenSettings.rootContainers.size()) return;
					sirenSettings.rootContainers.erase(sirenSettings.rootContainers.begin() + idx);
					sirenSettings.activeRootIdx = sirenSettings.rootContainers.empty() ? -1 : 0;
					if (m) m->activeRootIdx = sirenSettings.activeRootIdx;
					browserPane->setRoots(sirenSettings.rootContainers, sirenSettings.activeRootIdx);
				};
				browserPane->onSelectRoot = [this, m](int idx) {
					sirenSettings.activeRootIdx = idx;
					if (m) m->activeRootIdx = idx;
					browserPane->setRoots(sirenSettings.rootContainers, idx);
				};
			}
			zw->addChild(browserPane);
			display->addChild(zw);
		}

		// ── Top bar (inside display, local coords) ────────────────────────────
		{
			const float logW  = totalW / zoom;
			const float logH  = topBarH / zoom;
			const float btnH  = 22.f;
			const float btnW  = 150.f / zoom;
			const float mrgX  = 5.f;
			const float mrgY  = (logH - btnH) * 0.5f;

			SirenSourceButton* srcBtn = new SirenSourceButton;
			srcBtn->box.size = Vec(btnW, btnH);
			srcBtn->pane = browserPane;

			SirenSearchField* searchField = new SirenSearchField;
			searchField->box.size = Vec(300.f, btnH);
			searchField->pane = browserPane;

			ui::SequentialLayout* layout = new ui::SequentialLayout;
			layout->box.pos  = Vec(0.f, 0.f);
			layout->box.size = Vec(logW, logH);
			layout->margin   = Vec(mrgX, mrgY);
			layout->spacing  = Vec(mrgX, 0.f);
			layout->addChild(srcBtn);
			layout->addChild(searchField);

			widget::ZoomWidget* topBarZw = new widget::ZoomWidget;
			topBarZw->box.pos  = Vec(contentX, contentY);   // relative to display
			topBarZw->box.size = Vec(totalW, topBarH);
			topBarZw->setZoom(zoom);
			topBarZw->addChild(layout);
			display->addChild(topBarZw);
		}

		// ── Preview pane (inside display, local coords) ───────────────────────
		previewPane = new SirenPreviewPane;
		previewPane->box.pos  = Vec(contentX + browserW + gapW, contentY + topBarH);  // relative to display
		previewPane->box.size = Vec(previewW, paneH);
		previewPane->init(&taskWorker, &dropHandler);
		previewPane->cacheDir = sirenCacheDirPath();
		display->addChild(previewPane);

		// Wire audio callbacks: pane → module
		if (module) {
			SirenModule* m = module;
			previewPane->openStreamCallback    = [m](const std::string& id, DataSource* src) {
				m->openStream(id, src);
			};
			previewPane->startPlaybackCallback = [m](float pos) {
				m->startPlayback(pos);
			};
			previewPane->stopPlaybackCallback  = [m]() {
				m->stopPlayback();
			};
			// Atomic pointers for low-overhead display reads
			previewPane->modulePlayheadPos = &module->playheadPos;
			previewPane->modulePlaying     = &module->playing;
			previewPane->moduleInPoint     = &module->trimIn;
			previewPane->moduleOutPoint    = &module->trimOut;
		}

		// Refresh browser when preview pane modifies metadata
		previewPane->onMetadataChanged = [this]() {
			browserPane->requestRebuild();
		};

		dropHandler.moduleWidget = this;

		if (module) {
			// Top-level drag label overlay — drawn above all other rack elements
			dragOverlay = new SirenDragOverlay;
			dragOverlay->box.pos  = Vec(0.f, 0.f);
			dragOverlay->box.size = Vec(1e6f, 1e6f);
			dragOverlay->dropHandler = &dropHandler;
			dragOverlay->previewPane = previewPane;
			APP->scene->rack->addChild(dragOverlay);
		}

		// Obtain the conversion task from the active source; dispatched by the drop handler.
		dropHandler.prepareForDropCallback = [this](const std::string& id) -> std::function<std::string()> {
			DataSource* src = browserPane->activeDataSource;
			if (!src) return [id]() { return id; };

			int targetRate = sirenSettings.resampleOnDrop ? this->module->engineSampleRate : 0;
			float trimIn  = previewPane->inPoint;
			float trimOut = previewPane->outPoint;
			return src->prepareForDrop(id, sirenSettings.convertToWavOnDrop,
			                           targetRate, trimIn, trimOut, sirenSettings.resampleQuality);
		};

		// Load global settings and restore state
		sirenSettings.load();
		if (module) {
			// Patch state takes priority over global settings if the patch was saved
			if (!module->lastFilePath.empty()) {
				sirenSettings.activeRootIdx = module->activeRootIdx;
			}
		}
		browserPane->setRoots(sirenSettings.rootContainers, sirenSettings.activeRootIdx);
		std::string restoreFile = module ? module->lastFilePath : sirenSettings.lastFile;
		float restorePos = module ? module->lastPlayheadPos : sirenSettings.lastPlayheadPos;
		if (!restoreFile.empty()) {
			DataSource* src = browserPane->activeDataSource;
			previewPane->loadItem(restoreFile, src, src ? src->getMetadata() : nullptr);
			if (module) module->playheadPos.store(restorePos, std::memory_order_relaxed);
		}

		// VU meter: two vertical LED bars in the right margin, same top as the panes
		static constexpr float vuW = 2.f * SirenVuMeter::BAR_W + SirenVuMeter::BAR_GAP + 4.f;
		static constexpr float vuH = SirenVuMeter::NUM_SEGS * (SirenVuMeter::SEG_H + SirenVuMeter::SEG_GAP);
		SirenVuMeter* vu = new SirenVuMeter;
		vu->levelL   = module ? &module->levelL : nullptr;
		vu->levelR   = module ? &module->levelR : nullptr;
		vu->box.pos  = Vec(522.5f, 235.3f);
		vu->box.size = Vec(vuW, vuH);
		addChild(vu);

		addChild(createWidgetCentered<SirenOcWidget>(Vec(532.5f, 329.f)));
	}

	~SirenWidget() override {
		if (dragOverlay) {
			APP->scene->rack->removeChild(dragOverlay);
			delete dragOverlay;
			dragOverlay = nullptr;
		}

		// Sync preview state back into module fields (for patch save) and global settings
		sirenSettings.lastFile = previewPane->currentId;
		sirenSettings.lastPlayheadPos = module ? module->playheadPos.load() : 0.f;
		if (module) {
			module->lastFilePath = previewPane->currentId;
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

	void onFileSelected(const std::string& path, bool startPlay) {
		sirenSettings.lastFile = path;
		if (module) module->lastFilePath = path;  // keep dataToJson() in sync
		DataSource* src = browserPane->activeDataSource;
		previewPane->loadItem(path, src, src ? src->getMetadata() : nullptr, startPlay);
	}

	void onSelectKey(const SelectKeyEvent& e) override {
		if (e.action == GLFW_PRESS || e.action == GLFW_REPEAT) {
			if (e.key == GLFW_KEY_SPACE) {
				if (!previewPane->currentId.empty()) {
					if (module->playing.load(std::memory_order_relaxed))
						previewPane->stopPlaybackCallback();
					else
						previewPane->startPlaybackFrom(previewPane->inPoint);
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
				int idx = sirenSettings.activeRootIdx;
				if (idx < 0 || idx >= (int)sirenSettings.rootContainers.size()) return;
				sirenSettings.rootContainers.erase(sirenSettings.rootContainers.begin() + idx);
				sirenSettings.activeRootIdx = sirenSettings.rootContainers.empty() ? -1 : 0;
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
	}
};

} // namespace Siren
} // namespace StoermelderPackOne

Model* modelSiren = createModel<StoermelderPackOne::Siren::SirenModule, StoermelderPackOne::Siren::SirenWidget>("Siren");
