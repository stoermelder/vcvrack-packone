#include "../../plugin.hpp"
#include "../../pluginsettings.hpp"
#include "../../components/Knobs.hpp"
#include "SirenDataSource.hpp"
#include "SirenFileSystem.hpp"
#include "SirenAudio.hpp"
#include "SirenBrowserPane.hpp"
#include "SirenPreviewPane.hpp"
#include "SirenVuMeter.hpp"
#include <widget/ZoomWidget.hpp>

#include <osdialog.h>
#include <ghc/filesystem.hpp>

namespace StoermelderPackOne {
namespace Siren {

// ─── helper: settings file paths ─────────────────────────────────────────────

static std::string settingsDirPath() {
	return rack::asset::user("Stoermelder-P1");
}

static std::string sirenFilePath() {
	return settingsDirPath() + "/siren.json";
}

static std::string sirenCacheDirPath() {
	return settingsDirPath() + "/siren-cache";
}

// ─── global siren settings ───────────────────────────────────────────────────

struct SirenSettings {
	std::vector<std::string> rootContainers;
	int activeRootIdx = -1;
	std::string lastFile;
	float lastPlayheadPos = 0.f;

	void save() const {
		if (isTesting()) return;
		json_t* j = toJson();
		rack::system::createDirectories(settingsDirPath());
		FILE* f = fopen(sirenFilePath().c_str(), "w");
		if (f) { json_dumpf(j, f, JSON_INDENT(2) | JSON_REAL_PRECISION(9)); fclose(f); }
		json_decref(j);
	}

	void load() {
		if (isTesting()) return;
		FILE* f = fopen(sirenFilePath().c_str(), "r");
		if (!f) return;
		json_error_t err;
		json_t* j = json_loadf(f, 0, &err);
		fclose(f);
		if (!j) return;
		fromJson(j);
		json_decref(j);
	}

	json_t* toJson() const {
		json_t* j = json_object();
		json_t* rootsJ = json_array();
		for (const std::string& r : rootContainers)
			json_array_append_new(rootsJ, json_string(r.c_str()));
		json_object_set_new(j, "rootContainers", rootsJ);
		json_object_set_new(j, "activeRootIdx", json_integer(activeRootIdx));
		json_object_set_new(j, "lastFile", json_string(lastFile.c_str()));
		json_object_set_new(j, "lastPlayheadPos", json_real(lastPlayheadPos));
		return j;
	}

	void fromJson(json_t* j) {
		rootContainers.clear();
		json_t* rootsJ = json_object_get(j, "rootContainers");
		if (rootsJ && json_is_array(rootsJ)) {
			size_t i; json_t* v;
			json_array_foreach(rootsJ, i, v) {
				if (json_is_string(v)) rootContainers.push_back(json_string_value(v));
			}
		}
		json_t* v;
		v = json_object_get(j, "activeRootIdx"); if (v) activeRootIdx = (int)json_integer_value(v);
		v = json_object_get(j, "lastFile");      if (v) lastFile = json_string_value(v);
		v = json_object_get(j, "lastPlayheadPos"); if (v) lastPlayheadPos = (float)json_real_value(v);
	}
} sirenSettings;

// ─── module ───────────────────────────────────────────────────────────────────

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

	// Position counters — written before playing=true (release), read after playing (acquire)
	int64_t              seekBaseFrame     = 0;  // file frame at which this play session began
	int64_t              streamTotalFrames = 0;  // total frames in the open item; set in openStream()
	std::atomic<int64_t> outputFrameCount{0};    // frames output since last seek; DSP increments

	// VU meter — written by DSP thread, read by UI thread
	std::atomic<float> levelL{-100.f};  // dBFS; -100 = silence
	std::atomic<float> levelR{-100.f};
	float peakL = -100.f;  // peak-hold state, DSP thread only
	float peakR = -100.f;

	void fillThreadFunc() {
		AudioStream* stream = nullptr;  // owned by this thread

		while (!fillThreadStop.load(std::memory_order_relaxed)) {
			// ── process pending commands ──────────────────────────────────
			if (pendingStop.exchange(false, std::memory_order_acq_rel)) {
				playing.store(false, std::memory_order_release);
				eofReached.store(false, std::memory_order_relaxed);
				rbL.clear(); rbR.clear();
			}

			AudioStream* ns = pendingStream.exchange(nullptr, std::memory_order_acq_rel);
			if (ns) {
				delete stream;
				stream = ns;
				eofReached.store(false, std::memory_order_relaxed);
				rbL.clear(); rbR.clear();
			}

			int64_t sf = pendingSeekFrame.exchange(-1, std::memory_order_acq_rel);
			if (sf >= 0 && stream) {
				stream->seekTo(sf);
				outputFrameCount.store(0, std::memory_order_relaxed);
				eofReached.store(false, std::memory_order_relaxed);
				rbL.clear(); rbR.clear();
				playing.store(true, std::memory_order_release);
			}

			// ── fill ring buffer if playing ───────────────────────────────
			if (playing.load(std::memory_order_relaxed) && stream && !eofReached.load(std::memory_order_relaxed)) {
				size_t space = rbL.capacity();
				if (space > 0) {
					static constexpr size_t CHUNK = 1024;
					float tmp[CHUNK * 2];
					size_t toRead = std::min(space, CHUNK);
					int     ch    = stream->channels();
					int64_t nRead = stream->readF32(tmp, (int64_t)toRead);
					for (int64_t f = 0; f < nRead; f++) {
						rbL.push(tmp[f * ch]);
						rbR.push(ch >= 2 ? tmp[f * ch + 1] : tmp[f * ch]);
					}
					// Signal EOF without stopping — DSP drains the ring before stopping
					if (nRead == 0) eofReached.store(true, std::memory_order_release);
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
					float ph = (float)(seekBaseFrame + count) / (float)total;
					playheadPos.store(ph, std::memory_order_relaxed);
					if (seekBaseFrame + count >= total)
						playing.store(false, std::memory_order_release);
				}
			} else if (eofReached.load(std::memory_order_acquire)) {
				// Ring drained and fill thread hit EOF — end of file
				playing.store(false, std::memory_order_release);
			}
		}

		float vol = params[PARAM_VOLUME].getValue();
		l *= vol * 5.f;
		r *= vol * 5.f;

		outputs[OUTPUT_L].setVoltage(l);
		outputs[OUTPUT_R].setVoltage(r);

		// Peak-hold with 30 dB/s decay
		auto trackPeak = [&](float& peak, std::atomic<float>& out, float sig) {
			float db = (fabsf(sig) > 1e-6f) ? 20.f * log10f(fabsf(sig) / 5.f) : -100.f;
			if (db > peak) peak = db;
			peak -= 30.f * args.sampleTime;
			if (peak < -100.f) peak = -100.f;
			out.store(peak, std::memory_order_relaxed);
		};
		trackPeak(peakL, levelL, l);
		trackPeak(peakR, levelR, r);
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

// ─── top-bar search field ─────────────────────────────────────────────────────

struct SirenSearchField : ui::TextField {
	SirenBrowserPane* pane = nullptr;
	SirenSearchField() { placeholder = "Search..."; }
	void onChange(const event::Change& e) override {
		if (pane) {
			pane->searchQuery = rack::string::trim(text);
			pane->rebuildRowWidgets();
		}
		ui::TextField::onChange(e);
	}
	void onSelectKey(const event::SelectKey& e) override {
		if (e.action == GLFW_PRESS && e.key == GLFW_KEY_ESCAPE) {
			setText("");
			if (pane) { pane->searchQuery.clear(); pane->rebuildRowWidgets(); }
			e.consume(this);
			return;
		}
		ui::TextField::onSelectKey(e);
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

// ─── module widget ────────────────────────────────────────────────────────────

struct SirenWidget : ThemedModuleWidget<SirenModule> {
	TaskWorker        taskWorker{"Siren"};
	SirenDropHandler  dropHandler;

	SirenBrowserPane* browserPane = nullptr;
	SirenPreviewPane* previewPane = nullptr;

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
		const float previewW = 307.8f - 10.f;
		const float totalW   = browserW + previewW;
		const float topBarH  = 30.f * zoom;
		const float contentH = 338.6f;
		const float paneH    = contentH - topBarH;
		const float gapW     = 10.f;   // gap between browser and preview

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
			browserPane->onFileSelected = [this](const std::string& path) {
				onFileSelected(path);
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
			searchField->box.size = Vec(logW - btnW - mrgX * 3.f, btnH);
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
		}

		// Refresh browser when preview pane modifies metadata
		previewPane->onMetadataChanged = [this]() {
			browserPane->rebuildRowWidgets();
		};

		dropHandler.moduleWidget = this;

		// Obtain the conversion task from the active source; dispatched by the drop handler.
		dropHandler.prepareForDropCallback = [this](const std::string& id) -> std::function<std::string()> {
			DataSource* src = browserPane->activeDataSource;
			if (src) return src->prepareForDrop(id);
			return [id]() { return id; };
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
	}

	~SirenWidget() override {
		// Sync preview state back into module fields (for patch save) and global settings
		if (previewPane) {
			sirenSettings.lastFile = previewPane->currentId;
			sirenSettings.lastPlayheadPos = module ? module->playheadPos.load() : 0.f;
			if (module) {
				module->lastFilePath = previewPane->currentId;
				module->lastPlayheadPos = module->playheadPos.load();
				module->activeRootIdx = sirenSettings.activeRootIdx;
			}
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

	void onFileSelected(const std::string& path) {
		sirenSettings.lastFile = path;
		if (module) module->lastFilePath = path;  // keep dataToJson() in sync
		DataSource* src = browserPane->activeDataSource;
		previewPane->loadItem(path, src, src ? src->getMetadata() : nullptr, true);
	}

	void appendContextMenu(ui::Menu* menu) override {
		ThemedModuleWidget<SirenModule>::appendContextMenu(menu);
		menu->addChild(new ui::MenuSeparator);

		// Root container management
		menu->addChild(createMenuLabel("Sample Roots"));
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
	}
};

} // namespace Siren
} // namespace StoermelderPackOne

Model* modelSiren = createModel<StoermelderPackOne::Siren::SirenModule, StoermelderPackOne::Siren::SirenWidget>("Siren");
