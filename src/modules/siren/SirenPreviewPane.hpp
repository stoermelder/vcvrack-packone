#pragma once
#include <rack.hpp>
#include "SirenAudio.hpp"
#include "SirenAudioStream.hpp"
#include "SirenMetadata.hpp"
#include "SirenDropHandler.hpp"
#include "SirenBpmDetector.hpp"
#include "SirenWaveformCanvas.hpp"
#include "../../utils/TaskWorker.hpp"


namespace StoermelderPackOne {
namespace Siren {

// ─── SirenPreviewPane ─────────────────────────────────────────────────────────
// Orchestrates the preview region: top bar (file info, play button) + the
// SirenWaveformCanvas child widget.  Owns all file-state, waveform caches, and
// loop-preview generation logic.
//
// Loop-preview workflow:
//   1. User right-clicks → "Generate loop preview"
//   2. Worker decodes the trimmed region, applies rotation+crossfade, builds a
//      waveform cache for the result.
//   3. step() adopts the MemoryAudioStream into the module and activates the
//      canvas's loopPreviewMode (gold waveform, no trim handles).
//   4. Dropping while in loop-preview mode regenerates the loop on disk.
//   5. "Cancel loop preview" returns to the normal file stream.

struct SirenPreviewPane : widget::OpaqueWidget {
	static constexpr float TB_H = 50.f;

	// ── file state ───────────────────────────────────────────────────────────
	DataSourceNode currentNode;
	DataSource*  source      = nullptr;
	std::string  displayName;
	std::string  relPath;
	AudioInfo    info;

	// ── normal waveform cache ─────────────────────────────────────────────────
	WaveformCache     cache;
	std::atomic<bool> cacheReady{false};
	std::atomic<bool> cacheBuilding{false};

	struct PendingCache { WaveformCache cache; int gen = -1; bool valid = false; };
	std::atomic<int> cacheGeneration{0};
	PendingCache     pendingCache;
	std::atomic<bool> pendingCacheReady{false};

	float loopCrossfadeDuration = 6.f;
	float repitchSemitones      = 0.f;
	float repitchCents          = 0.f;

	// Total pitch shift in semitones, combining the semitone and cent sliders.
	float repitchTotalSemitones() const { return repitchSemitones + repitchCents / 100.f; }

	// ── loop-preview state ────────────────────────────────────────────────────
	bool          loopPreviewActive   = false;
	bool          loopPreviewBuilding = false;  // worker running, not yet active
	bool          previewIsRepitch    = false;  // true if the active preview was generated via repitch
	WaveformCache loopCache;
	bool          loopCacheReady     = false;
	float         loopDurationSeconds = 0.f;

	struct PendingLoopPreview {
		std::vector<float> samples;
		int   channels        = 0;
		int   sampleRate      = 0;
		float durationSeconds = 0.f;
		WaveformCache cache;
		bool  valid           = false;
	};
	PendingLoopPreview pendingLoop;
	std::atomic<bool>  pendingLoopReady{false};

	// ── child widget ──────────────────────────────────────────────────────────
	SirenWaveformCanvas* canvas = nullptr;

	// ── module interface ──────────────────────────────────────────────────────
	std::function<void(const std::string&, DataSource*)>      openStreamCallback;
	std::function<void(std::unique_ptr<AudioStream>, int64_t)> adoptStreamCallback;
	std::function<void(float)>                                 startPlaybackCallback;
	std::function<void()>                                      stopPlaybackCallback;

	std::atomic<float>* modulePlayheadPos = nullptr;
	std::atomic<bool>*  modulePlaying     = nullptr;
	std::atomic<float>* moduleInPoint     = nullptr;
	std::atomic<float>* moduleOutPoint    = nullptr;

	SirenDropHandler* dropHandler = nullptr;
	TaskWorker*       worker      = nullptr;
	std::string       cacheDir;

	// ── BPM ───────────────────────────────────────────────────────────────────
	std::atomic<float> bpm{0.f};

	MetadataStore* metadata() const { return source ? source->getMetadata() : nullptr; }
	std::function<void()> onMetadataChanged;

	// ── public accessors for SirenWidget ─────────────────────────────────────
	bool isLoopPreviewActive() const { return loopPreviewActive && !previewIsRepitch; }
	bool isRepitchPreviewActive() const { return loopPreviewActive && previewIsRepitch; }

	// ── init ─────────────────────────────────────────────────────────────────

	void init(TaskWorker* tw, SirenDropHandler* dh) {
		worker      = tw;
		dropHandler = dh;

		canvas = new SirenWaveformCanvas;
		canvas->box.pos   = Vec(0.f, TB_H);
		canvas->dropHandler = dh;
		canvas->worker      = tw;

		canvas->onInPointChanged = [this](float v) {
			if (moduleInPoint) moduleInPoint->store(v, std::memory_order_relaxed);
		};
		canvas->onOutPointChanged = [this](float v) {
			if (moduleOutPoint) moduleOutPoint->store(v, std::memory_order_relaxed);
		};
		canvas->onScrubTo = [this](float pos) {
			if (startPlaybackCallback) startPlaybackCallback(pos);
		};
		canvas->onCancelLoopPreview = [this]() {
			cancelLoopPreview();
		};

		addChild(canvas);
	}

	// ── file loading ──────────────────────────────────────────────────────────

	void loadItem(const DataSourceNode& node, DataSource* src,
	              bool startPlay = false, bool forceRebuild = false) {
		const std::string& id = node.relativePath;

		if (stopPlaybackCallback) stopPlaybackCallback();
		if (openStreamCallback)   openStreamCallback(id, src);

		currentNode   = node;
		source        = src;
		displayName   = !node.name.empty() ? node.name : (src && !id.empty() ? src->getDisplayName(id) : "");
		relPath       = id;
		cacheReady    = false;
		cacheBuilding = false;
		pendingCacheReady.store(false, std::memory_order_relaxed);
		int gen = ++cacheGeneration;

		// Cancel any active loop preview when a new file is loaded
		loopPreviewActive   = false;
		loopPreviewBuilding = false;
		loopCacheReady      = false;
		loopDurationSeconds = 0.f;
		loopCache           = WaveformCache{};

		if (canvas) {
			canvas->inPoint  = 0.f;
			canvas->outPoint = 1.f;
			canvas->scrubPos = 0.f;
			canvas->zoomLevel = 1.0f;
			canvas->scrollPos = 0.0f;
			canvas->onInPointChanged(0.f);
			canvas->onOutPointChanged(1.f);
		}
		bpm.store(0.f);

		if (id.empty() || !src) return;

		src->loadAudioInfo(id, info);

		if (MetadataStore* meta = metadata()) {
			if (!relPath.empty()) {
				auto it = meta->samples.find(relPath);
				if (it != meta->samples.end() && it->second.bpm > 0.f) {
					bpm.store(it->second.bpm);
				}
				else {
					float conf = 0.f;
					float nameBpm = BpmDetector::detectFromName(id, conf);
					if (nameBpm > 0.f) {
						bpm.store(nameBpm);
						meta->setBpm(relPath, nameBpm, conf);
					}
				}
				meta->markSeen(relPath);
			}
		}

		int64_t ts        = src->getTimestamp(id);
		std::string cacheFile = cachePathFor(id);
		if (!forceRebuild) {
			WaveformCache loaded;
			if (loadWaveformCacheFile(cacheFile, ts, loaded) && loaded.sampleCount > 0) {
				cache      = std::move(loaded);
				cacheReady = true;
				if (startPlay) startPlaybackFrom(0.f);
				return;
			}
		}

		if (!worker) return;
		cacheBuilding = true;
		int pw = canvas ? (int)canvas->box.size.x - (int)SirenWaveformCanvas::WAVE_X - 8 : 512;
		if (pw < 64) pw = 512;

		std::string cacheCopy    = cacheFile;
		std::string cacheDirCopy = cacheDir;
		worker->work([this, id, ts, src, cacheCopy, cacheDirCopy, pw, gen]() {
			WaveformCache built;
			bool ok = src->buildWaveformCache(id, ts, pw, built);
			if (ok && !cacheDirCopy.empty()) {
				rack::system::createDirectories(cacheDirCopy);
				saveWaveformCacheFile(cacheCopy, built);
			}
			pendingCache.cache = std::move(built);
			pendingCache.gen   = gen;
			pendingCache.valid = ok;
			pendingCacheReady.store(true, std::memory_order_release);
		});

		if (startPlay) startPlaybackFrom(0.f);
	}

	// ── loop preview ─────────────────────────────────────────────────────────

	void generateLoopPreview() {
		if (!source || currentNode.relativePath.empty() || !worker) return;

		loopPreviewBuilding = true;
		loopPreviewActive   = false;
		previewIsRepitch    = false;
		loopCacheReady      = false;
		loopCache           = WaveformCache{};

		DataSource* srcCopy = source;
		std::string idCopy  = currentNode.relativePath;
		float trimIn   = canvas ? canvas->inPoint  : 0.f;
		float trimOut  = canvas ? canvas->outPoint : 1.f;
		float duration = loopCrossfadeDuration;
		int pw = canvas ? (int)canvas->box.size.x - (int)SirenWaveformCanvas::WAVE_X - 8 : 512;
		if (pw < 64) pw = 512;

		worker->work([this, srcCopy, idCopy, trimIn, trimOut, duration, pw]() {
			LoopPreviewResult result = buildLoopPreview(*srcCopy, idCopy, trimIn, trimOut, duration);

			if (result.ok && !result.samples.empty()) {
				// Build waveform cache directly from the in-memory buffer
				MemoryAudioStream ms;
				ms.samples = result.samples;  // copy: cache builder reads it sequentially
				ms.ch = result.channels;
				ms.sr = result.sampleRate;
				WaveformCache wc;
				buildWaveformCache(0, ms, pw, wc);

				pendingLoop.samples       = std::move(result.samples);
				pendingLoop.channels      = result.channels;
				pendingLoop.sampleRate    = result.sampleRate;
				pendingLoop.durationSeconds = result.durationSeconds;
				pendingLoop.cache         = std::move(wc);
				pendingLoop.valid         = true;
			}
			else {
				pendingLoop.valid = false;
			}
			pendingLoopReady.store(true, std::memory_order_release);
		});
	}

	void generateRepitchPreview() {
		if (!source || currentNode.relativePath.empty() || !worker) return;

		loopPreviewBuilding = true;
		loopPreviewActive   = false;
		previewIsRepitch    = true;
		loopCacheReady      = false;
		loopCache           = WaveformCache{};

		DataSource* srcCopy = source;
		std::string idCopy  = currentNode.relativePath;
		float trimIn   = canvas ? canvas->inPoint  : 0.f;
		float trimOut  = canvas ? canvas->outPoint : 1.f;
		float semitones = repitchTotalSemitones();
		int pw = canvas ? (int)canvas->box.size.x - (int)SirenWaveformCanvas::WAVE_X - 8 : 512;
		if (pw < 64) pw = 512;

		worker->work([this, srcCopy, idCopy, trimIn, trimOut, semitones, pw]() {
			LoopPreviewResult result = buildRepitchPreview(*srcCopy, idCopy, trimIn, trimOut, semitones);

			if (result.ok && !result.samples.empty()) {
				// Build waveform cache directly from the in-memory buffer
				MemoryAudioStream ms;
				ms.samples = result.samples;  // copy: cache builder reads it sequentially
				ms.ch = result.channels;
				ms.sr = result.sampleRate;
				WaveformCache wc;
				buildWaveformCache(0, ms, pw, wc);

				pendingLoop.samples       = std::move(result.samples);
				pendingLoop.channels      = result.channels;
				pendingLoop.sampleRate    = result.sampleRate;
				pendingLoop.durationSeconds = result.durationSeconds;
				pendingLoop.cache         = std::move(wc);
				pendingLoop.valid         = true;
			}
			else {
				pendingLoop.valid = false;
			}
			pendingLoopReady.store(true, std::memory_order_release);
		});
	}

	void cancelLoopPreview() {
		loopPreviewActive   = false;
		loopPreviewBuilding = false;
		previewIsRepitch    = false;
		loopCacheReady      = false;
		loopDurationSeconds = 0.f;
		loopCache           = WaveformCache{};

		// Restore original file stream and module trim points
		if (openStreamCallback && source && !currentNode.relativePath.empty())
			openStreamCallback(currentNode.relativePath, source);
		if (canvas) {
			if (moduleInPoint)  moduleInPoint->store(canvas->inPoint,  std::memory_order_relaxed);
			if (moduleOutPoint) moduleOutPoint->store(canvas->outPoint, std::memory_order_relaxed);
		}
	}

	// ── step ─────────────────────────────────────────────────────────────────

	void step() override {
		// Consume pending normal waveform cache
		if (pendingCacheReady.load(std::memory_order_acquire)) {
			pendingCacheReady.store(false, std::memory_order_relaxed);
			if (pendingCache.valid && pendingCache.gen == cacheGeneration.load(std::memory_order_relaxed)) {
				cache         = std::move(pendingCache.cache);
				cacheReady    = true;
				cacheBuilding = false;
			}
		}

		// Consume pending loop preview
		if (pendingLoopReady.load(std::memory_order_acquire)) {
			pendingLoopReady.store(false, std::memory_order_relaxed);
			if (pendingLoop.valid) {
				auto ms      = std::unique_ptr<MemoryAudioStream>(new MemoryAudioStream);
				ms->samples  = std::move(pendingLoop.samples);
				ms->ch       = pendingLoop.channels;
				ms->sr       = pendingLoop.sampleRate;
				int64_t tf   = ms->totalFrames();
				if (adoptStreamCallback) adoptStreamCallback(std::move(ms), tf);

				loopCache           = std::move(pendingLoop.cache);
				loopCacheReady      = true;
				loopDurationSeconds = pendingLoop.durationSeconds;
				loopPreviewActive   = true;
				loopPreviewBuilding = false;

				// The loop buffer spans [0, 1] — make the module loop it fully
				if (moduleInPoint)  moduleInPoint->store(0.f,  std::memory_order_relaxed);
				if (moduleOutPoint) moduleOutPoint->store(1.f, std::memory_order_relaxed);

				// Reset seek base so playhead is valid in the new stream's frame space
				if (startPlaybackCallback) startPlaybackCallback(0.f);
			}
			else {
				loopPreviewBuilding = false;
			}
		}

		// Update canvas display inputs
		if (canvas) {
			canvas->box.size       = Vec(box.size.x, box.size.y - TB_H);
			canvas->cache          = loopPreviewActive ? &loopCache : &cache;
			canvas->cacheReady     = loopPreviewActive ? loopCacheReady : (bool)cacheReady;
			canvas->cacheBuilding  = !loopPreviewActive && cacheBuilding;
			canvas->generatingLoop = loopPreviewBuilding && !loopPreviewActive;
			canvas->loopPreviewMode = loopPreviewActive;
			canvas->previewIsRepitch = previewIsRepitch;
			canvas->hasFile        = !currentNode.relativePath.empty();
			canvas->durationSeconds = loopPreviewActive ? loopDurationSeconds : info.durationSeconds;
			canvas->dragPath        = currentNode.relativePath;
			canvas->dragDisplayName = displayName;
			canvas->modulePlayheadPos = modulePlayheadPos;
			canvas->converting     = dropHandler ? &dropHandler->converting : nullptr;
		}

		if (dropHandler) dropHandler->step();
		widget::OpaqueWidget::step();
	}

	// ── draw (top bar only) ───────────────────────────────────────────────────

	void draw(const DrawArgs& args) override {
		widget::OpaqueWidget::draw(args);  // draw canvas child first
		if (currentNode.relativePath.empty()) return;

		float w = box.size.x;
		bool isPlaying = modulePlaying ? modulePlaying->load() : false;

		std::shared_ptr<Font> font = APP->window->loadFont(
			asset::system("res/fonts/ShareTechMono-Regular.ttf"));
		nvgFontFaceId(args.vg, font->handle);

		// Play/stop button
		nvgFontSize(args.vg, 14.f);
		nvgFillColor(args.vg, isPlaying
			? nvgRGBf(1.f, 0.85f, 0.1f)
			: nvgRGBf(0.55f, 0.55f, 0.55f));
		nvgText(args.vg, 8.f, 12.f, isPlaying ? "\xe2\x96\xa0" : "\xe2\x96\xb6", nullptr);

		// Preview accent color: cyan for loop preview, amber for repitch preview
		NVGcolor previewColor = previewIsRepitch
			? nvgRGBf(0.95f, 0.65f, 0.30f)
			: nvgRGBf(0.35f, 0.80f, 0.85f);

		// Filename — preview accent when in preview mode, gold when playing, light otherwise
		std::string fname = displayName.empty() ? currentNode.relativePath : displayName;
		nvgFontSize(args.vg, 12.f);
		NVGcolor fnColor = loopPreviewActive ? previewColor
		                 : isPlaying         ? nvgRGBf(1.f, 0.85f, 0.1f)
		                 :                     nvgRGBf(0.92f, 0.92f, 0.88f);
		nvgFillColor(args.vg, fnColor);
		float maxFnW = w - 30.f;
		nvgScissor(args.vg, 22.f, 0.f, maxFnW, TB_H);
		nvgText(args.vg, 22.f, 12.f, fname.c_str(), nullptr);
		nvgResetScissor(args.vg);

		nvgFontSize(args.vg, 10.f);
		nvgFillColor(args.vg, nvgRGBf(0.50f, 0.50f, 0.50f));

		// Preview label (second row, left side)
		if (loopPreviewActive) {
			nvgFillColor(args.vg, previewColor);
			nvgText(args.vg, SirenWaveformCanvas::WAVE_X, 26.f, previewIsRepitch ? "REPITCH PREVIEW" : "LOOP PREVIEW", nullptr);
			nvgFillColor(args.vg, nvgRGBf(0.50f, 0.50f, 0.50f));
		}
		else {
			// ch · sr · bit badges
			std::string badges;
			if (info.bitDepth > 0)   badges = rack::string::f("%dbit", info.bitDepth);
			if (info.sampleRate > 0) badges = rack::string::f("%dk", info.sampleRate / 1000) + (badges.empty() ? "" : " \xc2\xb7 ") + badges;
			if (info.channels > 0)   badges = std::string(info.channels == 1 ? "MONO" : "STEREO") + (badges.empty() ? "" : " \xc2\xb7 ") + badges;
			if (!badges.empty())
				nvgText(args.vg, SirenWaveformCanvas::WAVE_X, 26.f, badges.c_str(), nullptr);
		}

		// Zoom level — right-aligned
		if (canvas) {
			std::string zoomText = rack::string::f("%.1fx", canvas->zoomLevel);
			nvgFillColor(args.vg, nvgRGBf(0.50f, 0.50f, 0.50f));
			nvgTextAlign(args.vg, NVG_ALIGN_RIGHT | NVG_ALIGN_BASELINE);
			nvgText(args.vg, w - 4.f, 26.f, zoomText.c_str(), nullptr);
			nvgTextAlign(args.vg, NVG_ALIGN_LEFT | NVG_ALIGN_BASELINE);
		}

		// BPM
		float bpmVal = bpm.load();
		if (bpmVal > 0.f || bpmVal < 0.f) {
			std::string bpmText = (bpmVal < 0.f) ? "\xe2\x80\xa6 BPM" : rack::string::f("%.1f BPM", bpmVal);
			nvgFillColor(args.vg, nvgRGBf(0.50f, 0.50f, 0.50f));
			nvgTextAlign(args.vg, NVG_ALIGN_RIGHT | NVG_ALIGN_BASELINE);
			nvgText(args.vg, w - 50.f, 26.f, bpmText.c_str(), nullptr);
			nvgTextAlign(args.vg, NVG_ALIGN_LEFT | NVG_ALIGN_BASELINE);
		}

		// Tag chips (third row of top bar)
		if (MetadataStore* meta = metadata()) {
			auto tags = meta->getTags(relPath);
			if (!tags.empty()) {
				static constexpr float CHIP_H = 10.f;
				static constexpr float CHIP_Y = 34.f;
				float x    = SirenWaveformCanvas::WAVE_X;
				float maxX = w - 4.f;
				nvgFontFaceId(args.vg, APP->window->uiFont->handle);
				nvgFontSize(args.vg, 8.f);
				for (const std::string& tag : tags) {
					float bounds[4];
					nvgTextBounds(args.vg, 0.f, 0.f, tag.c_str(), nullptr, bounds);
					float chipW = bounds[2] - bounds[0] + 10.f;
					if (x + chipW > maxX) break;
					NVGcolor bgColor = bndGetTheme()->toolTheme.innerColor;
					bgColor.a *= 0.4f;
					nvgBeginPath(args.vg);
					nvgRoundedRect(args.vg, x, CHIP_Y, chipW, CHIP_H, 2.f);
					nvgFillColor(args.vg, bgColor);
					nvgFill(args.vg);
					nvgFillColor(args.vg, bndGetTheme()->toolTheme.textColor);
					nvgTextAlign(args.vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
					nvgText(args.vg, x + chipW * 0.5f, CHIP_Y + CHIP_H * 0.5f, tag.c_str(), nullptr);
					nvgTextAlign(args.vg, NVG_ALIGN_LEFT | NVG_ALIGN_BASELINE);
					x += chipW + 4.f;
				}
			}
		}
	}

	// ── interaction ───────────────────────────────────────────────────────────

	void onButton(const event::Button& e) override {
		if (e.button == GLFW_MOUSE_BUTTON_RIGHT && e.action == GLFW_PRESS) {
			createContextMenu();
			e.consume(this);
			return;
		}
		// Play/stop button (top bar area only — canvas handles clicks below TB_H)
		if (e.button == GLFW_MOUSE_BUTTON_LEFT && e.action == GLFW_PRESS) {
			if (e.pos.x < 26.f && e.pos.y < TB_H) {
				if (modulePlaying && modulePlaying->load()) stopPlaybackCallback();
				else if (!currentNode.relativePath.empty())
					startPlaybackFrom(canvas ? canvas->inPoint : 0.f);
				e.consume(this);
				return;
			}
		}
		widget::OpaqueWidget::onButton(e);
	}

	void onDragStart(const event::DragStart& e) override {
		if (!currentNode.relativePath.empty() && dropHandler)
			dropHandler->startDrag(currentNode.relativePath, displayName);
	}

	void onDragEnd(const event::DragEnd& e) override {
		if (dropHandler && dropHandler->active)
			dropHandler->endDrag(APP->scene->mousePos, worker);
	}

	void onSelectKey(const event::SelectKey& e) override {
		if (e.action == GLFW_PRESS && e.key == GLFW_KEY_ESCAPE && loopPreviewActive) {
			cancelLoopPreview();
			e.consume(this);
			return;
		}
		widget::OpaqueWidget::onSelectKey(e);
	}

	// ── context menu ──────────────────────────────────────────────────────────

	void createContextMenu() {
		if (currentNode.relativePath.empty() || !source) return;

		ui::Menu* menu = createMenu();
		menu->addChild(createMenuLabel(source->getDisplayName(currentNode.relativePath)));

		if (loopPreviewActive) {
			// ── preview mode ──────────────────────────────────────────────────
			menu->addChild(createMenuItem(previewIsRepitch ? "Exit repitch preview" : "Exit loop preview", "", [this]() {
				cancelLoopPreview();
			}));
		}
		else {
			// ── normal mode ──────────────────────────────────────────────────
			menu->addChild(new ui::MenuSeparator);
			menu->addChild(createMenuItem("Reset trim", "",
				[this]() { if (canvas) canvas->resetTrimHandles(); },
				canvas && canvas->inPoint == 0.f && canvas->outPoint == 1.f
			));
			menu->addChild(createCheckMenuItem("Loop playback", "",
				[]() { return sirenSettings.loopPlayback; },
				[]() { sirenSettings.loopPlayback = !sirenSettings.loopPlayback; }
			));

			menu->addChild(new ui::MenuSeparator);
			menu->addChild(createSubmenuItem("Crossfade loop", "", [=](Menu* menu) {
				menu->addChild(Rack::createPtrSlider(
					&loopCrossfadeDuration, 0.01f, 60.f, 6.f, "Crossfade", " s", 1.f, 150.f));
				menu->addChild(createMenuItem("Generate preview", "",
					[this]() { generateLoopPreview(); APP->event->setSelectedWidget(canvas); }
				));
			}));

			menu->addChild(createSubmenuItem("Repitch", "", [=](Menu* menu) {
				menu->addChild(Rack::createSteppedSlider<int>(
					[this]() { return (int)repitchSemitones; },
					[this](int v) { repitchSemitones = (float)v; },
					-24.f, 24.f, 0.f, "Repitch", " st", nullptr, 150.f));
				menu->addChild(Rack::createPtrSlider(
					&repitchCents, -100.f, 100.f, 0.f, "Repitch fine", " ct", 1.f, 150.f));
				menu->addChild(createMenuItem("Generate preview", "",
					[this]() { generateRepitchPreview(); APP->event->setSelectedWidget(canvas); }
				));
			}));

			if (metadata()) {
				menu->addChild(new ui::MenuSeparator);
				source->appendNodeMenuItems(menu, currentNode, [this]() {
					if (onMetadataChanged) onMetadataChanged();
				});
				menu->addChild(new ui::MenuSeparator);
				menu->addChild(createMenuItem("Clear tags", "", [this]() {
					source->getMetadata()->clearTags(currentNode.relativePath);
				}));
			}
		}
	}

	// ── BPM detection ─────────────────────────────────────────────────────────

	void startBpmDetection() {
		if (!source || currentNode.relativePath.empty() || !worker) return;
		if (bpm.load() < 0.f) return;
		bpm.store(-1.f);
		std::string idCopy      = currentNode.relativePath;
		std::string relPathCopy = relPath;
		DataSource* ds          = source;
		worker->work([this, idCopy, relPathCopy, ds]() {
			float confidence = 0.f;
			float result = BpmDetector::detectFromDsp(*ds, idCopy, confidence);
			MetadataStore* meta = ds ? ds->getMetadata() : nullptr;
			if (meta && result > 0.f && !relPathCopy.empty()) {
				meta->setBpm(relPathCopy, result, confidence);
				if (ds) ds->saveMetadata();
			}
			bpm.store(result);
		});
	}

	// ── helpers ───────────────────────────────────────────────────────────────

	void startPlaybackFrom(float pos) {
		if (startPlaybackCallback) startPlaybackCallback(pos);
	}

	bool isPlaying() const {
		return modulePlaying && modulePlaying->load();
	}

	std::string cachePathFor(const std::string& audioPath) const {
		return cacheDir + "/" + hashPath(audioPath) + ".json";
	}
};

} // namespace Siren
} // namespace StoermelderPackOne
