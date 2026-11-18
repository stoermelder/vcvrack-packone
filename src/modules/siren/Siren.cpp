#include "../../plugin.hpp"
#include "../../pluginsettings.hpp"
#include "../../components/Knobs.hpp"
#include "../../utils/MpmcTaskWorker.hpp"
#include "../../ui/DisplayTheme.hpp"
#include "Settings.hpp"
#include "DataSource.hpp"
#include "FileSystem.hpp"
#include "Audio.hpp"
#include "StreamPlayer.hpp"
#include "BrowserPane.hpp"
#include "PreviewPane.hpp"
#include "TopBar.hpp"
#include "VuMeter.hpp"
#include <mutex>
#include <condition_variable>
#include <thread>


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

	// Trim range [0,1] — patch state, written by the UI, read by the player.
	std::atomic<float> trimIn{0.f};
	std::atomic<float> trimOut{1.f};

	// Streaming playback (fill thread + ring buffer); see StreamPlayer.hpp
	StreamPlayer player{&trimIn, &trimOut};

	// VU meter — written by DSP thread, read by UI thread
	std::atomic<float> levelL{-100.f};  // dBFS; -100 = silence
	std::atomic<float> levelR{-100.f};
	float peakL = -100.f;  // peak-hold state, DSP thread only
	float peakR = -100.f;

	ClockDividerEx lightDivider;

	SirenModule() {
		config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);
		configParam(PARAM_VOLUME, 0.f, 2.f, 1.f, "Volume", " dB", -10.f, 20.f);
		configSwitch(PARAM_AUTOPLAY, 0.f, 1.f, 0.f, "Autoplay", {"Off", "On"});
		paramQuantities[PARAM_AUTOPLAY]->description = "When On, the player starts automatically as soon as a new sample is loaded.";
		configOutput(OUTPUT_L, "Left audio");
		configOutput(OUTPUT_R, "Right audio");

		lightDivider.setDivision(512);
	}

	void onSampleRateChange(const SampleRateChangeEvent& e) override {
		player.setEngineSampleRate(e.sampleRate);
	}
	
	void process(const ProcessArgs& args) override {
		dsp::Frame<2> fr = player.nextFrame(trimIn.load(std::memory_order_relaxed),
			trimOut.load(std::memory_order_relaxed), sirenSettings.loopPlayback);
		float l = fr.samples[0], r = fr.samples[1];

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
		json_object_set_new(rootJ, "trimIn", json_real(trimIn.load(std::memory_order_relaxed)));
		json_object_set_new(rootJ, "trimOut", json_real(trimOut.load(std::memory_order_relaxed)));
		return rootJ;
	}

	void dataFromJson(json_t* rootJ) override {
		json_t* v;
		v = json_object_get(rootJ, "lastFile");
		if (v && json_is_string(v)) lastFilePath = json_string_value(v);
		v = json_object_get(rootJ, "lastPlayheadPos");
		if (v && json_is_real(v)) lastPlayheadPos = (float)json_real_value(v);
		v = json_object_get(rootJ, "activeRootIdx");
		if (v && json_is_integer(v)) activeRootIdx = (int)json_integer_value(v);
		v = json_object_get(rootJ, "trimIn");
		if (v && json_is_real(v)) {
			float t = (float)json_real_value(v);
			// Clamp to [0,1] so a malformed patch can never produce an
			// out-of-range trim that breaks loop wrapping at trimOut → trimIn.
			if (t < 0.f) t = 0.f;
			if (t > 1.f) t = 1.f;
			trimIn.store(t, std::memory_order_relaxed);
		}
		v = json_object_get(rootJ, "trimOut");
		if (v && json_is_real(v)) {
			float t = (float)json_real_value(v);
			if (t < 0.f) t = 0.f;
			if (t > 1.f) t = 1.f;
			trimOut.store(t, std::memory_order_relaxed);
		}
	}
};


struct SirenDisplayWidget : OpaqueWidget {
	void draw(const DrawArgs& args) override {
		// Intentionally empty
	}
	void drawLayer(const DrawArgs& args, int layer) override {
		if (layer != 1) return;
		float b = std::max(0.2f, settings::rackBrightness);
		float b_inv = 1.f + std::max(b - settings::rackBrightness, 0.f) * 8.f;
		nvgGlobalAlpha(args.vg, b);

		math::Rect r = box.zeroPos();

		// Outer glow — screen light bleeding onto the panel surface
		float spread = 22.f;
		NVGpaint glow = nvgBoxGradient(args.vg,
			r.pos.x, r.pos.y, r.size.x, r.size.y,
			3.f, spread,
			nvgRGBAf(0.45f, 0.70f, 1.0f, 0.12f * b),
			nvgRGBAf(0.0f, 0.0f, 0.0f, 0.0f));
		nvgBeginPath(args.vg);
		nvgRect(args.vg, r.pos.x - spread, r.pos.y - spread,
			r.size.x + 2.f * spread, r.size.y + 2.f * spread);
		nvgFillPaint(args.vg, glow);
		nvgFill(args.vg);

		// Dark gradient background
		nvgBeginPath(args.vg);
		nvgRect(args.vg, RECT_ARGS(r));
		NVGcolor topColor = color::mult(DisplayTheme::backgroundTop(), b_inv);
		NVGcolor bottomColor = color::mult(DisplayTheme::backgroundBottom(), b_inv);
		nvgFillPaint(args.vg, nvgLinearGradient(args.vg, 0.f, 0.f, 0.f, 25.f, topColor, bottomColor));
		nvgFill(args.vg);

		// Children (browser, preview, topbar) drawn inside the dimmed context
		OpaqueWidget::draw(args);

		// Corner vignette — subtle darkening toward edges for screen depth
		NVGpaint vignette = nvgRadialGradient(args.vg,
			r.size.x * 0.5f, r.size.y * 0.5f,
			r.size.x * 0.35f, r.size.x * 0.75f,
			DisplayTheme::vignette(0.0f),
			DisplayTheme::vignette(0.45f));
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
		nvgStrokeColor(args.vg, DisplayTheme::ink(0.25f));
		nvgStrokeWidth(args.vg, 1.f);
		nvgStroke(args.vg);

		// Inner top stroke
		nvgBeginPath(args.vg);
		nvgMoveTo(args.vg, 0.f, 2.5f);
		nvgLineTo(args.vg, box.size.x, 2.5f);
		nvgStrokeColor(args.vg, DisplayTheme::ink(0.20f));
		nvgStrokeWidth(args.vg, 1.f);
		nvgStroke(args.vg);

		// Inner bottom stroke
		nvgBeginPath(args.vg);
		nvgMoveTo(args.vg, 0.f, box.size.y - 2.5f);
		nvgLineTo(args.vg, box.size.x, box.size.y - 2.5f);
		nvgStrokeColor(args.vg, DisplayTheme::ink(0.20f));
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
	ITaskWorker* worker = nullptr;
	bool initiated = false;

	void drawLayer(const DrawArgs& args, int layer) override {
		if (layer != 2) return;
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
		const float w = bounds[2] - bounds[0] + pad * 2.f;
		const float h = 18.f;

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
		if (dropHandler && dropHandler->active && worker) {
			dropHandler->endDrag(APP->scene->mousePos, worker);
		}
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
			vcv::ui::openBrowser("https://omricohen-music.com/");
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

struct SirenWidget : ThemedModuleWidget<SirenModule>, ModuleChangeListener {
	MpmcTaskWorker taskWorker{"Siren"};
	SirenDropHandler dropHandler;

	SirenBrowserPane* browserPane = nullptr;
	SirenPreviewPane* previewPane = nullptr;
	SirenDragOverlay* dragOverlay = nullptr;

	SirenWidget(SirenModule* module)
		: ThemedModuleWidget<SirenModule>(module, "Siren"), ModuleChangeListener{false} {
		setModule(module);

		if (module) {
			// Watch the global sirenSettings so that changes made in another
			// Siren instance (adding a root, selecting one, opening a file,
			// toggling loop/resample/quality/convert-target, etc.) are reflected
			// in this instance's browser pane and preview pane without requiring
			// a reload. notifyModuleListeners("Siren") fires from the mutating
			// callbacks below; this widget's step() consumes the flag.
			registerModuleListener("Siren", this);
		}

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
		vu->levelL = module ? &module->levelL : nullptr;
		vu->levelR = module ? &module->levelR : nullptr;
		vu->box.pos = Vec(12.9f, 235.3f);
		vu->box.size = Vec(vuW, vuH);
		addChild(vu);

		addChild(createWidgetCentered<SirenOcWidget>(Vec(22.9f, 329.f)));

		// Layout constants
		const float contentX = 8.f;
		const float contentY = 8.f;
		const float browserW = 188.0f;
		const float previewW = 297.8f;
		const float totalW = browserW + previewW;
		const float topBarH = 18.f;
		const float contentH = 336.6f;
		const float paneH = contentH - topBarH;
		const float gapW = 4.f;   // gap between browser and preview

		// Display widget (single container for browser + topbar + preview)
		SirenDisplayWidget* display = new SirenDisplayWidget;
		display->box.pos = Vec(45.5f, 10.2f);
		display->box.size = Vec(501.7f, 354.0f);
		addChild(display);

		// Browser pane (inside display, local coords)
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
					RootContainer rc;
					if (!filesystem::createNewRootContainer(rc)) return;
					if (std::find(sirenSettings.rootContainers.begin(), sirenSettings.rootContainers.end(), rc)
							== sirenSettings.rootContainers.end()) {
						sirenSettings.rootContainers.push_back(rc);
						std::sort(sirenSettings.rootContainers.begin(), sirenSettings.rootContainers.end(),
							[](const RootContainer& a, const RootContainer& b) {
								return rack::string::lowercase(a.name) < rack::string::lowercase(b.name);
							});
						auto it = std::find(sirenSettings.rootContainers.begin(), sirenSettings.rootContainers.end(), rc);
						// The newly added root becomes active in THIS instance only —
						// other instances keep their own selected root, they just need
						// to refresh their root list (notified below).
						int newIdx = (int)(it - sirenSettings.rootContainers.begin());
						if (m) m->activeRootIdx = newIdx;
						browserPane->setRoots(sirenSettings.rootContainers, newIdx);
						browserPane->startIndexing();
						// Persist the shared root list immediately — don't rely on the
						// widget destructor, which may not run (crash/force-quit) or may
						// save a stale set if another instance reloaded meanwhile.
						sirenSettings.save();
						// Other Siren instances must refresh their root list now
						// that the global settings have a new entry.
						notifyModuleListeners("Siren");
						// Skip self: the originating widget already applied the change
						// above, and re-running setRoots() in step() would trigger a
						// redundant async reload of the DataSource.
						moduleChangedFlag = false;
					}
				};
				browserPane->onSelectRoot = [this, m](int idx) {
					// Both sirenSettings.rootContainers and browserPane->rootContainers
					// are kept in sorted order, so `idx` is valid for both directly.
					if (idx < 0 || idx >= (int)sirenSettings.rootContainers.size()) return;
					// Per-instance: only this module's selected root changes. Other
					// instances keep their own selection.
					if (m) m->activeRootIdx = idx;
					browserPane->setRoots(sirenSettings.rootContainers, idx);
					// Skip self: no broadcast — selecting a root is a local action
					// in the originating instance and should not affect peers.
					moduleChangedFlag = false;
				};
				browserPane->onRemoveRoot = [this, m]() {
					// Per-instance: remove the root this instance currently has
					// selected. Each instance owns its own activeRootIdx, so other
					// instances simply shift their index in their step() refresh
					// if the removed root was before theirs.
					int idx = m ? m->activeRootIdx : -1;
					if (idx < 0) return;
					if (!sirenSettings.removeRootAt(idx, browserPane->activeDs.get())) return;
					if (m) {
						// After erasing `idx`, the next root (or none) becomes active
						// for this instance. The removed root was at idx, so the new
						// active is the entry that slid into its slot — or -1 when the
						// list is now empty.
						m->activeRootIdx = (int)sirenSettings.rootContainers.size() > idx
							? idx
							: (int)sirenSettings.rootContainers.size() - 1;
					}
					browserPane->setRoots(sirenSettings.rootContainers, m ? m->activeRootIdx : -1);
					// Persist the shared root list immediately (see onAddRoot).
					sirenSettings.save();
					// Other Siren instances must drop the removed root from their list.
					notifyModuleListeners("Siren");
					// Skip self: see onAddRoot above.
					moduleChangedFlag = false;
				};
			}

			display->addChild(browserPane);
		}

		// Top bar (inside display, local coords)
		SirenTopBar* topBar = new SirenTopBar;
		topBar->box.pos = Vec(contentX, contentY);   // relative to display
		topBar->box.size = Vec(totalW, topBarH);
		topBar->pane = browserPane;
		topBar->init();
		display->addChild(topBar);

		// Preview pane (inside display, local coords)
		previewPane = new SirenPreviewPane;
		previewPane->box.pos = Vec(contentX + browserW + gapW, contentY + topBarH);  // relative to display
		previewPane->box.size = Vec(previewW, paneH);
		previewPane->init(&taskWorker, &dropHandler);
		SirenBrowserPane* bp = browserPane;
		previewPane->externalStatusMessage = [bp]() { return bp->statusMessage(); };
		previewPane->onSetSearchQuery = [bp](const std::string& q) { bp->setFilter(q); };
		display->addChild(previewPane);

		// The active DataSource (and its MetadataStore) is about to be destroyed —
		// drop the preview pane's references to it before they dangle.
		browserPane->onActiveSourceChanging = [this]() {
			previewPane->loadItem(DataSourceNode{}, nullptr, false, false, /*resetTrim=*/false);
		};

		if (!module) {
			// Module browser thumbnail — show static placeholder content instead
			// of empty panes.
			browserPane->dummyPreview = true;
			previewPane->setupDummyPreview();
			return;
		}

		// Wire audio callbacks: pane → module
		previewPane->openStreamCallback = [module](const std::string& id, DataSource* src) {
			module->player.openStream(id, src);
		};
		previewPane->adoptStreamCallback = [module](std::unique_ptr<AudioStream> s, int64_t tf) {
			module->player.adoptStream(std::move(s), tf);
		};
		previewPane->startPlaybackCallback = [module](float pos) {
			module->player.startPlayback(pos);
		};
		previewPane->stopPlaybackCallback = [module]() {
			module->player.stopPlayback();
		};
		previewPane->onMetadataChanged = [this]() {
			browserPane->requestRebuild();
		};
		previewPane->player = &module->player;
		previewPane->canvas->moduleInPoint = &module->trimIn;
		previewPane->canvas->moduleOutPoint = &module->trimOut;

		// Top-level drag label overlay — drawn above all other rack elements
		dragOverlay = new SirenDragOverlay;
		dragOverlay->box.pos = Vec(0.f, 0.f);
		dragOverlay->box.size = Vec(1e6f, 1e6f);
		dragOverlay->dropHandler = &dropHandler;
		dragOverlay->previewPane = previewPane;
		dragOverlay->worker = &taskWorker;
		APP->scene->rack->addChildBottom(dragOverlay);

		// Load the global sirenSettings (the SHARED root list and per-user
		// conversion/playback preferences). Per-instance state — selected root,
		// last opened file, playhead position — lives on the module and is
		// restored from the patch via SirenModule::dataFromJson before this
		// constructor runs.
		sirenSettings.load();

		// Obtain the conversion task from the active source; dispatched by the drop handler.
		dropHandler.prepareForDropCallback = [this](const std::string& id) -> std::function<std::string()> {
			DataSource* src = browserPane->activeDs.get();
			if (!src) return [id]() { return id; };

			int targetRate = sirenSettings.resampleOnDrop ? this->module->player.engineSampleRate() : 0;
			bool loopOnDrop = previewPane->isLoopPreviewActive();
			float repitchOnDrop = previewPane->isRepitchPreviewActive() ? previewPane->repitchTotalSemitones() : 0.f;
			float trimIn = previewPane->canvas ? previewPane->canvas->getInPoint() : 0.f;
			float trimOut = previewPane->canvas ? previewPane->canvas->getOutPoint() : 1.f;
			// Resolve the output directory from the chosen target at drop time.
			// Patch storage can only be created once the module is added to the
			// engine, so we look it up here (UI thread) rather than capturing the
			// module pointer and dereferencing it on the worker thread later.
			std::string outputDir;
			switch (sirenSettings.convertTarget) {
				case SirenSettings::CT_CUSTOM:
					outputDir = sirenSettings.customConvertDir;
					break;
				case SirenSettings::CT_PATCH:
					outputDir = this->module ? this->module->createPatchStorageDirectory() : "";
					break;
				case SirenSettings::CT_SOURCE:
				default:
					outputDir = "";
					break;
			}
			return src->prepareForDrop(id, sirenSettings.convertToWavOnDrop,
				targetRate, trimIn, trimOut, sirenSettings.resampleQuality, outputDir,
				loopOnDrop, previewPane->loopCrossfadeDuration, repitchOnDrop,
				sirenSettings.alwaysCopy);
		};
		dropHandler.moduleWidget = this;

		// Clamp to the
		// currently-known root list so a stale idx from an older patch
		// (when the list was different) doesn't index out of range.
		int activeIdx = module->activeRootIdx;
		if (activeIdx < 0 || activeIdx >= (int)sirenSettings.rootContainers.size()) {
			activeIdx = sirenSettings.rootContainers.empty() ? -1 : 0;
		}
		browserPane->setRoots(sirenSettings.rootContainers, activeIdx);
		std::string restoreFile = module->lastFilePath;
		float restorePos = module->lastPlayheadPos;
		if (!restoreFile.empty()) {
			std::shared_ptr<DataSource> src = browserPane->activeDs;
			DataSourceNode restoreNode = src ? src->resolveNode(restoreFile) : DataSourceNode{};
			previewPane->loadItem(restoreNode, src, false, false, /*resetTrim=*/false);
			module->player.setPlayhead(restorePos);
			browserPane->revealPath(restoreFile);
		}
	}

	~SirenWidget() override {
		// Discard queued-but-unstarted tasks before anything below is torn down
		taskWorker.drain();

		if (module) {
			unregisterModuleListener("Siren", this);
		}

		if (dragOverlay) {
			APP->scene->rack->removeChild(dragOverlay);
			delete dragOverlay;
			dragOverlay = nullptr;
		}

		// Sync preview state back into module fields (for patch save). Per-instance
		// state — file, playhead, active root — lives only on the module.
		if (module) {
			module->lastFilePath = previewPane->currentNode.relativePath;
			module->lastPlayheadPos = module->player.playhead();
		}

		sirenSettings.save();
		// Metadata is saved by FileSystemDataSource destructor via ~SirenBrowserPane
	}

	void onFileSelected(const DataSourceNode& node, bool startPlay) {
		// Per-instance: only this module remembers the selected file.
		if (module) module->lastFilePath = node.relativePath;
		std::shared_ptr<DataSource> src = browserPane->activeDs;
		bool autoplay = module && module->params[SirenModule::PARAM_AUTOPLAY].getValue() > 0.5f;
		previewPane->loadItem(node, src, startPlay || autoplay);
		// Skip self: opening a file is a local action — it should not switch
		// the loaded item in other Siren instances.
		moduleChangedFlag = false;
	}

	void onSelectKey(const SelectKeyEvent& e) override {
		if (e.action == GLFW_PRESS || e.action == GLFW_REPEAT) {
			if (e.key == GLFW_KEY_SPACE) {
				if (!previewPane->currentNode.relativePath.empty()) {
					if (module->player.isPlaying()) {
						previewPane->stopPlaybackCallback();
					}
					else {
						previewPane->startPlaybackFrom(module->trimIn.load(std::memory_order_relaxed));
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
		appendConversionMenuItems(menu, this->module != nullptr);
	}

	// Consume the cross-instance change flag set by notifyModuleListeners("Siren").
	// When another Siren instance added or removed a root from the SHARED root
	// list, refresh this instance's browser pane so its dropdown stays in sync.
	// The active root (module->activeRootIdx), loaded file and playhead
	// position are PER-INSTANCE — they are deliberately not touched here, so
	// selecting or loading in one Siren does not hijack another instance.
	void step() override {
		if (moduleChangedFlag) {
			moduleChangedFlag = false;
			if (browserPane && module) {
				// Clamp the per-instance idx to the (possibly shrunken) root list
				// — e.g. if instance A removed the root that was at idx 0 in
				// instance B, B's idx must shift to 0 (or -1) so it stays valid.
				int idx = module->activeRootIdx;
				if (idx < 0 || idx >= (int)sirenSettings.rootContainers.size()) {
					idx = sirenSettings.rootContainers.empty() ? -1 : 0;
				}
				browserPane->setRoots(sirenSettings.rootContainers, idx);
			}
		}
		ThemedModuleWidget<SirenModule>::step();
	}
};

} // namespace Siren
} // namespace StoermelderPackOne

Model* modelSiren = createModel<StoermelderPackOne::Siren::SirenModule, StoermelderPackOne::Siren::SirenWidget>("Siren");
