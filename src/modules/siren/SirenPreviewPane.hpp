#pragma once
#include <rack.hpp>
#include <mutex>
#include <memory>
#include <thread>
#include <condition_variable>
#include "SirenAudio.hpp"
#include "SirenMetadata.hpp"
#include "SirenDropHandler.hpp"
#include "../../utils/TaskWorker.hpp"

namespace StoermelderPackOne {
namespace Siren {

struct SirenPreviewPane : widget::OpaqueWidget {
	// ── layout ───────────────────────────────────────────────────────────────
	static constexpr float TB_H       = 34.f;  // top bar
	static constexpr float READOUT_H  = 26.f;  // bottom readout bar
	static constexpr float WAVE_X     = 8.f;   // left margin (for L/R labels)

	// ── state ────────────────────────────────────────────────────────────────
	std::string  currentId;          // opaque item identifier (supplied by the data source)
	DataSource*  source = nullptr;   // used only during loadItem; must not be stored for deferred use
	std::string  displayName;        // cached from source->getDisplayName() at load time
	std::string  relPath;            // cached from source->getRelativePath() at load time
	AudioInfo    info;
	WaveformCache cache;
	std::atomic<bool> cacheReady{false};
	std::atomic<bool> cacheBuilding{false};

	float inPoint          = 0.f;   // trim IN handle position [0, 1]
	float outPoint         = 1.f;   // trim OUT handle position [0, 1]
	float scrubPos         = 0.f;   // drag-only position; never touched by audio thread
	float dragStartRackX   = 0.f;   // rack-space X when drag began
	float dragStartScrub   = 0.f;   // scrubPos / handle pos when drag began
	bool  draggingPlayhead = false;
	bool  trimmingIn       = false;  // Shift+drag on IN handle
	bool  trimmingOut      = false;  // Shift+drag on OUT handle

	RootMetadata*     metadata    = nullptr;
	SirenDropHandler* dropHandler = nullptr;
	TaskWorker*       worker      = nullptr;

	// Called after any metadata change so the browser pane can refresh
	std::function<void()> onMetadataChanged;

	// ── module interface (audio state lives in SirenModule) ──────────────────
	// Callbacks set by SirenWidget after both module and pane are constructed.
	std::function<void(const std::string& id, DataSource* src)> openStreamCallback;
	std::function<void(float pos)> startPlaybackCallback;
	std::function<void()>          stopPlaybackCallback;

	// Direct atomic pointers into the module for low-overhead display reads/writes.
	std::atomic<float>* modulePlayheadPos = nullptr;
	std::atomic<bool>*  modulePlaying     = nullptr;
	std::atomic<float>* moduleInPoint     = nullptr;  // UI writes, DSP reads for loop start
	std::atomic<float>* moduleOutPoint    = nullptr;  // UI writes, DSP reads for stop frame
	std::atomic<bool>*  moduleLooping     = nullptr;  // UI writes, DSP reads for loop toggle

	// Pending waveform cache from worker
	std::atomic<int> cacheGeneration{0};
	struct PendingCache { WaveformCache cache; int gen = -1; bool valid = false; };
	PendingCache      pendingCache;
	std::atomic<bool> pendingCacheReady{false};

	// Cache dir path (set by module widget)
	std::string cacheDir;

	void init(TaskWorker* tw, SirenDropHandler* dh) {
		worker      = tw;
		dropHandler = dh;
	}

	void syncInPoint() {
		if (moduleInPoint)
			moduleInPoint->store(inPoint, std::memory_order_relaxed);
	}

	void syncOutPoint() {
		if (moduleOutPoint)
			moduleOutPoint->store(outPoint, std::memory_order_relaxed);
	}

	void loadItem(const std::string& id, DataSource* src, RootMetadata* meta,
	              bool startPlay = false, bool forceRebuild = false) {
		// Delegate audio open to the module via callback
		if (stopPlaybackCallback)  stopPlaybackCallback();
		if (openStreamCallback)    openStreamCallback(id, src);

		currentId     = id;
		source        = src;
		metadata      = meta;
		displayName   = (src && !id.empty()) ? src->getDisplayName(id)  : "";
		relPath       = (src && !id.empty()) ? src->getRelativePath(id) : "";
		cacheReady    = false;
		cacheBuilding = false;
		pendingCacheReady.store(false, std::memory_order_relaxed);
		int gen = ++cacheGeneration;
		inPoint      = 0.f;
		outPoint     = 1.f;
		scrubPos     = 0.f;
		syncInPoint();
		syncOutPoint();

		if (id.empty() || !src) return;

		src->loadAudioInfo(id, info);

		int64_t ts        = src->getTimestamp(id);
		std::string cacheFile = cachePathFor(id);
		if (!forceRebuild) {
			WaveformCache loaded;
			if (loadWaveformCacheFile(cacheFile, ts, loaded)) {
				cache      = std::move(loaded);
				cacheReady = true;
				if (startPlay) { inPoint = 0.f; scrubPos = 0.f; startPlaybackFrom(0.f); }
				return;
			}
		}

		if (!worker) return;
		cacheBuilding = true;
		int pw = (int)box.size.x - (int)WAVE_X - 8;
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

		if (startPlay) { inPoint = 0.f; scrubPos = 0.f; startPlaybackFrom(0.f); }
	}

	void step() override {
		// Consume pending waveform cache
		if (pendingCacheReady.load(std::memory_order_acquire)) {
			pendingCacheReady.store(false, std::memory_order_relaxed);
			if (pendingCache.valid && pendingCache.gen == cacheGeneration.load(std::memory_order_relaxed)) {
				cache         = std::move(pendingCache.cache);
				cacheReady    = true;
				cacheBuilding = false;
			}
		}

		if (dropHandler) dropHandler->step();

		widget::OpaqueWidget::step();
	}

	void draw(const DrawArgs& args) override {
		float w = box.size.x;
		float h = box.size.y;

		// Derived layout values
		float waveY = TB_H + 2.f;
		float waveW = w - WAVE_X - 4.f;
		float waveH = h - waveY - READOUT_H - 4.f;
		if (waveH < 20.f) waveH = 20.f;

		bool isPlaying = modulePlaying ? modulePlaying->load() : false;

		// ── top bar ───────────────────────────────────────────────────────────
		if (!currentId.empty()) {
			// Play/stop button
			nvgFontSize(args.vg, 14.f);
			nvgFillColor(args.vg, isPlaying
				? nvgRGBf(1.f, 0.85f, 0.1f)
				: nvgRGBf(0.55f, 0.55f, 0.55f));
			nvgText(args.vg, 8.f, 12.f, isPlaying ? "■" : "▶", nullptr);

			// Filename — gold when playing, light when idle
			std::string fname = displayName.empty() ? currentId : displayName;
			nvgFontSize(args.vg, 12.f);
			nvgFillColor(args.vg, isPlaying
				? nvgRGBf(1.f, 0.85f, 0.1f)
				: nvgRGBf(0.92f, 0.92f, 0.88f));
			float maxFnW = w - 100.f;
			nvgScissor(args.vg, 28.f, 0.f, maxFnW, TB_H);
			nvgText(args.vg, 28.f, 12.f, fname.c_str(), nullptr);
			nvgResetScissor(args.vg);

			// Badges (right-aligned in top bar)
			float bx = w + 6.f;
			nvgFontSize(args.vg, 10.f);
			nvgFillColor(args.vg, nvgRGBf(0.50f, 0.50f, 0.50f));

			if (info.durationSeconds > 0.f) {
				int mm = (int)(info.durationSeconds / 60.f);
				float ss = info.durationSeconds - mm * 60.f;
				std::string dur = rack::string::f("%02d:%05.2f", mm, ss);
				// Right-justify duration
				float dw = dur.size() * 6.2f;
				nvgText(args.vg, bx - dw, 26.f, dur.c_str(), nullptr);
				bx -= dw + 4.f;
			}

			// ch · sr · bit
			std::string badges;
			if (info.bitDepth > 0)   badges = rack::string::f("%dbit", info.bitDepth);
			if (info.sampleRate > 0) badges = rack::string::f("%dk", info.sampleRate / 1000) + (badges.empty() ? "" : " · ") + badges;
			if (info.channels > 0)   badges = std::string(info.channels == 1 ? "MONO" : "STEREO") + (badges.empty() ? "" : " · ") + badges;
			if (!badges.empty()) {
				nvgText(args.vg, WAVE_X, 26.f, badges.c_str(), nullptr);
			}
		}

		int numCh = cacheReady ? (int)cache.peaks.size() : info.channels;
		if (numCh < 1) numCh = 1;

		if (cacheReady && !cache.empty()) {
			float chH = waveH / numCh;
			for (int ch = 0; ch < (int)cache.peaks.size(); ch++) {
				float chY  = waveY + ch * chH;
				float midY = chY + chH * 0.5f;
				const auto& chPeaks = cache.peaks[ch];
				int buckets = (int)chPeaks.size();
				if (buckets == 0) continue;

				float bw = waveW / (float)buckets;

				// Filled waveform contour
				nvgBeginPath(args.vg);
				for (int b = 0; b < buckets; b++) {
					float px = WAVE_X + b * bw;
					float py = midY - chPeaks[b].second * chH * 0.44f;
					if (b == 0) nvgMoveTo(args.vg, px, py);
					else        nvgLineTo(args.vg, px, py);
				}
				for (int b = buckets - 1; b >= 0; b--) {
					float px = WAVE_X + b * bw;
					float py = midY - chPeaks[b].first * chH * 0.44f;
					nvgLineTo(args.vg, px, py);
				}
				nvgClosePath(args.vg);
				nvgFillColor(args.vg, nvgRGBf(0.70f, 0.70f, 0.63f));
				nvgFill(args.vg);

				// Zero-line
				nvgBeginPath(args.vg);
				nvgMoveTo(args.vg, WAVE_X, midY);
				nvgLineTo(args.vg, WAVE_X + waveW, midY);
				nvgStrokeColor(args.vg, nvgRGBAf(1.f, 1.f, 1.f, 0.07f));
				nvgStrokeWidth(args.vg, 0.5f);
				nvgStroke(args.vg);

				// Channel label
				nvgFontSize(args.vg, 9.f);
				nvgFillColor(args.vg, nvgRGBAf(1.f, 1.f, 1.f, 0.30f));
				nvgText(args.vg, WAVE_X + 2.f, chY + 11.f, ch == 0 ? "L" : "R", nullptr);
			}
		}
		else if (cacheBuilding) {
			nvgFontSize(args.vg, 10.f);
			nvgFillColor(args.vg, nvgRGBAf(1.f, 1.f, 1.f, 0.28f));
			nvgText(args.vg, WAVE_X + 4.f, waveY + waveH * 0.5f + 4.f, "Building waveform…", nullptr);
		}

		// Tick marks along waveform bottom
		if (info.durationSeconds > 0.f && waveW > 0.f) {
			float dur = info.durationSeconds;
			static const float ivs[] = {0.5f, 1.f, 2.f, 5.f, 10.f, 30.f, 60.f, 300.f};
			float tickIv = 1.f;
			for (float iv : ivs) {
				if (dur / iv <= 14.f) { tickIv = iv; break; }
			}
			float tickY = waveY + waveH;
			for (float t = 0.f; t <= dur + 0.001f; t += tickIv) {
				float tx = WAVE_X + (t / dur) * waveW;
				nvgBeginPath(args.vg);
				nvgMoveTo(args.vg, tx, tickY - 4.f);
				nvgLineTo(args.vg, tx, tickY);
				nvgStrokeColor(args.vg, nvgRGBAf(1.f, 1.f, 1.f, 0.18f));
				nvgStrokeWidth(args.vg, 0.5f);
				nvgStroke(args.vg);
			}
		}

		// Trim region — only shown when at least one handle has been moved from its default
		if (!currentId.empty() && (inPoint > 0.f || outPoint < 1.f) && outPoint > inPoint) {
			float x1 = WAVE_X + inPoint  * waveW;
			float x2 = WAVE_X + outPoint * waveW;
			nvgBeginPath(args.vg);
			nvgRect(args.vg, x1, waveY, x2 - x1, waveH);
			nvgFillColor(args.vg, nvgRGBAf(1.f, 0.85f, 0.1f, 0.07f));
			nvgFill(args.vg);
		}

		// OUT handle — only shown when moved from its default (1.0)
		if (!currentId.empty() && outPoint < 1.f) {
			float opX = WAVE_X + outPoint * waveW;

			nvgBeginPath(args.vg);
			nvgMoveTo(args.vg, opX, waveY);
			nvgLineTo(args.vg, opX, waveY + waveH);
			nvgStrokeColor(args.vg, nvgRGBAf(1.f, 0.85f, 0.1f, 0.7f));
			nvgStrokeWidth(args.vg, 1.f);
			nvgStroke(args.vg);

			const float ts = 4.f;
			nvgBeginPath(args.vg);
			nvgMoveTo(args.vg, opX - ts, waveY + waveH);
			nvgLineTo(args.vg, opX + ts, waveY + waveH);
			nvgLineTo(args.vg, opX,      waveY + waveH - ts * 1.4f);
			nvgClosePath(args.vg);
			nvgFillColor(args.vg, nvgRGBAf(1.f, 0.85f, 0.1f, 0.85f));
			nvgFill(args.vg);
		}

		// IN handle — only shown when moved from its default (0.0)
		if (!currentId.empty() && inPoint > 0.f) {
			float ipX = WAVE_X + inPoint * waveW;

			nvgBeginPath(args.vg);
			nvgMoveTo(args.vg, ipX, waveY);
			nvgLineTo(args.vg, ipX, waveY + waveH);
			nvgStrokeColor(args.vg, nvgRGBAf(1.f, 0.85f, 0.1f, 0.7f));
			nvgStrokeWidth(args.vg, 1.f);
			nvgStroke(args.vg);

			const float ts = 4.f;
			nvgBeginPath(args.vg);
			nvgMoveTo(args.vg, ipX - ts, waveY);
			nvgLineTo(args.vg, ipX + ts, waveY);
			nvgLineTo(args.vg, ipX, waveY + ts * 1.4f);
			nvgClosePath(args.vg);
			nvgFillColor(args.vg, nvgRGBAf(1.f, 0.85f, 0.1f, 0.85f));
			nvgFill(args.vg);
		}

		// Playhead line + triangle pointer
		// During a drag we read scrubPos directly — the DSP thread continuously
		// overwrites modulePlayheadPos via process(), so it lags behind and would
		// not reflect the drag position until the fill thread finishes seeking.
		if (!currentId.empty()) {
			float ph = draggingPlayhead ? scrubPos
			         : (modulePlayheadPos ? modulePlayheadPos->load(std::memory_order_relaxed) : 0.f);
			float phX = WAVE_X + ph * waveW;

			nvgBeginPath(args.vg);
			nvgMoveTo(args.vg, phX, waveY);
			nvgLineTo(args.vg, phX, waveY + waveH);
			nvgStrokeColor(args.vg, nvgRGBf(1.f, 1.f, 1.f));
			nvgStrokeWidth(args.vg, 1.f);
			nvgStroke(args.vg);

			const float ts = 5.f;
			nvgBeginPath(args.vg);
			nvgMoveTo(args.vg, phX - ts, waveY);
			nvgLineTo(args.vg, phX + ts, waveY);
			nvgLineTo(args.vg, phX, waveY + ts * 1.6f);
			nvgClosePath(args.vg);
			nvgFillColor(args.vg, nvgRGBf(1.f, 1.f, 1.f));
			nvgFill(args.vg);
		}

		// ── bottom readout ────────────────────────────────────────────────────
		auto formatTime = [](float s) -> std::string {
			if (s < 0.f) s = 0.f;
			int mm = (int)(s / 60.f);
			float ss = s - mm * 60.f;
			return rack::string::f("%02d:%05.2f", mm, ss);
		};

		float pos = (draggingPlayhead ? scrubPos
		           : (modulePlayheadPos ? modulePlayheadPos->load(std::memory_order_relaxed) : 0.f))
		           * info.durationSeconds;
		float col = waveW / 4.f + 4.f;
		nvgFontSize(args.vg, 10.f);

		auto drawReadout = [&](float x, const char* lbl, float val) {
			nvgFillColor(args.vg, nvgRGBAf(1.f, 1.f, 1.f, 0.38f));
			nvgText(args.vg, x, box.size.y, lbl, nullptr);
			nvgFillColor(args.vg, nvgRGBf(0.88f, 0.88f, 0.83f));
			nvgText(args.vg, x + 21.f, box.size.y, formatTime(val).c_str(), nullptr);
		};

		drawReadout(WAVE_X,           "IN",  inPoint  * info.durationSeconds);
		drawReadout(WAVE_X + col,     "OUT", outPoint * info.durationSeconds);
		drawReadout(WAVE_X + col * 2, "LEN", (outPoint - inPoint) * info.durationSeconds);
		drawReadout(WAVE_X + col * 3, "POS", pos);

		// ── "Converting..." overlay ───────────────────────────────────────────
		if (dropHandler && dropHandler->converting.load(std::memory_order_relaxed)) {
			float oy = waveY + waveH * 0.5f - 9.f;
			nvgBeginPath(args.vg);
			nvgRoundedRect(args.vg, WAVE_X, oy, waveW, 18.f, 3.f);
			nvgFillColor(args.vg, nvgRGBAf(0.f, 0.f, 0.f, 0.65f));
			nvgFill(args.vg);
			nvgFontFaceId(args.vg, APP->window->uiFont->handle);
			nvgFontSize(args.vg, BND_LABEL_FONT_SIZE);
			nvgFillColor(args.vg, nvgRGBf(1.f, 0.85f, 0.1f));
			nvgTextAlign(args.vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
			nvgText(args.vg, WAVE_X + waveW * 0.5f, oy + 9.f, "Converting...", nullptr);
			nvgTextAlign(args.vg, NVG_ALIGN_LEFT | NVG_ALIGN_BASELINE);
		}
	}

	void drawLayer(const DrawArgs& args, int layer) override {
		if (layer != 1 || !dropHandler) return;
		// Use cached displayName when dragging the currently loaded item, otherwise
		// extract a name from the raw path (avoids calling through potentially-stale source ptr)
		std::string lbl = (dropHandler->dragPath == currentId && !displayName.empty())
		                ? displayName
		                : rack::system::getFilename(dropHandler->dragPath);
		dropHandler->drawLabel(args, this, lbl);
	}

	// ── waveform interaction ──────────────────────────────────────────────────

	Rect waveformRect() const {
		float waveY = TB_H + 2.f;
		float waveW = box.size.x - WAVE_X - 4.f;
		float waveH = box.size.y - waveY - READOUT_H - 4.f;
		if (waveH < 20.f) waveH = 20.f;
		return Rect(Vec(WAVE_X, waveY), Vec(waveW, waveH));
	}

	bool inWaveformArea(Vec pos) const {
		return waveformRect().contains(pos);
	}

	float posToPlayhead(Vec pos) const {
		Rect r = waveformRect();
		return rack::math::clamp((pos.x - r.pos.x) / r.size.x, 0.f, 1.f);
	}

	void startPlaybackFrom(float pos) {
		if (startPlaybackCallback) startPlaybackCallback(pos);
	}

	void createContextMenu() {
		if (currentId.empty() || !source || !metadata) return;

		std::string rel = relPath;

		ui::Menu* menu = createMenu();

		// File label
		menu->addChild(createMenuLabel(displayName.empty() ? currentId : displayName));

		// Reset trim handles
		menu->addChild(createMenuItem("Reset trim", "",
			[this]() {
				inPoint  = 0.f;
				outPoint = 1.f;
				syncInPoint();
				syncOutPoint();
			},
			inPoint == 0.f && outPoint == 1.f
		));

		// Loop playback toggle
		menu->addChild(createCheckMenuItem("Loop playback", "",
			[this]() { return moduleLooping && moduleLooping->load(std::memory_order_relaxed); },
			[this]() {
				if (moduleLooping)
					moduleLooping->store(!moduleLooping->load(std::memory_order_relaxed),
					                     std::memory_order_relaxed);
			}
		));
		
		menu->addChild(new ui::MenuSeparator);

		// Favorite toggle
		menu->addChild(createCheckMenuItem("Favorite", "",
			[this, rel]() { return metadata->isFavorite(rel); },
			[this, rel]() {
				metadata->setFavorite(rel, !metadata->isFavorite(rel));
				if (onMetadataChanged) onMetadataChanged();
			}
		));

		menu->addChild(new ui::MenuSeparator);
		menu->addChild(createMenuLabel("Tags"));

		// Text field for adding a new tag
		struct NewTagField : ui::TextField {
			RootMetadata* metadata;
			std::string rel;
			std::function<void()> onChanged;
			void onSelectKey(const event::SelectKey& e) override {
				if (e.action == GLFW_PRESS && e.key == GLFW_KEY_ENTER) {
					std::string tag = rack::string::trim(text);
					if (!tag.empty()) {
						metadata->addTag(rel, tag);
						if (onChanged) onChanged();
					}
					ui::MenuOverlay* overlay = getAncestorOfType<ui::MenuOverlay>();
					if (overlay) overlay->requestDelete();
					e.consume(this);
					return;
				}
				if (!e.getTarget()) ui::TextField::onSelectKey(e);
			}
		};
		NewTagField* ntf = new NewTagField;
		ntf->box.size.x = 150.f;
		ntf->placeholder = "New tag...";
		ntf->metadata = metadata;
		ntf->rel = rel;
		ntf->onChanged = onMetadataChanged;
		menu->addChild(ntf);
		APP->event->setSelectedWidget(ntf);

		// All known tags with checkmarks
		struct TagItem : ui::MenuItem {
			RootMetadata* metadata;
			std::string rel;
			std::string tag;
			std::function<void()> onChanged;
			void onAction(const event::Action& e) override {
				auto current = metadata->getTags(rel);
				bool has = std::find(current.begin(), current.end(), tag) != current.end();
				if (has) metadata->removeTag(rel, tag);
				else     metadata->addTag(rel, tag);
				if (onChanged) onChanged();
				e.unconsume();
			}
			void step() override {
				auto current = metadata->getTags(rel);
				rightText = CHECKMARK(std::find(current.begin(), current.end(), tag) != current.end());
				MenuItem::step();
			}
		};

		auto allTags = metadata->allTags();
		std::vector<std::string> sorted(allTags.begin(), allTags.end());
		std::sort(sorted.begin(), sorted.end());
		for (const std::string& tag : sorted) {
			TagItem* item = new TagItem;
			item->text     = toTitleCase(tag);
			item->metadata = metadata;
			item->rel      = rel;
			item->tag      = tag;
			item->onChanged = onMetadataChanged;
			menu->addChild(item);
		}
	}

	void onButton(const event::Button& e) override {
		if (e.button == GLFW_MOUSE_BUTTON_RIGHT && e.action == GLFW_PRESS) {
			createContextMenu();
			e.consume(this);
			return;
		}
		if (e.button == GLFW_MOUSE_BUTTON_LEFT && e.action == GLFW_PRESS) {
			if (!currentId.empty() && inWaveformArea(e.pos)) {
				bool shift = (e.mods & GLFW_MOD_SHIFT) != 0;
				if (shift) {
					Rect r = waveformRect();
					float pos = posToPlayhead(e.pos);
					dragStartRackX = APP->scene->rack->getMousePos().x;

					// At defaults: left half → IN, right half → OUT.
					// Otherwise: drag whichever handle is nearest to the click.
					bool pickIn = (inPoint == 0.f && outPoint == 1.f)
					    ? (e.pos.x < r.pos.x + r.size.x * 0.5f)
					    : (std::abs(pos - inPoint) <= std::abs(pos - outPoint));

					if (pickIn) {
						inPoint        = rack::math::clamp(pos, 0.f, outPoint);
						trimmingIn     = true;
						dragStartScrub = inPoint;
						syncInPoint();
					} 
					else {
						outPoint       = rack::math::clamp(pos, inPoint, 1.f);
						trimmingOut    = true;
						dragStartScrub = outPoint;
						syncOutPoint();
					}
				} 
				else {
					// Playhead scrubbing — moves only the playhead, not the trim handles
					scrubPos       = posToPlayhead(e.pos);
					dragStartRackX = APP->scene->rack->getMousePos().x;
					dragStartScrub = scrubPos;
					draggingPlayhead = true;
					//startPlaybackFrom(scrubPos);
				}

				e.consume(this);
				return;
			}
			// Play/stop button — always starts from the stored inPoint
			if (e.pos.x < 26.f && e.pos.y < TB_H) {
				if (modulePlaying && modulePlaying->load()) stopPlaybackCallback();
				else if (!currentId.empty()) startPlaybackFrom(inPoint);
				e.consume(this);
				return;
			}
		}
		widget::OpaqueWidget::onButton(e);
	}

	void onDragStart(const event::DragStart& e) override {
		if (!currentId.empty() && !draggingPlayhead && !trimmingIn && !trimmingOut && dropHandler)
			dropHandler->startDrag(currentId);
	}

	void onDragMove(const event::DragMove& e) override {
		Rect r = waveformRect();
		if (r.size.x <= 0.f) return;
		float dx = APP->scene->rack->getMousePos().x - dragStartRackX;
		float pos = dragStartScrub + dx / r.size.x;

		if (trimmingIn) {
			inPoint = rack::math::clamp(pos, 0.f, outPoint);
			syncInPoint();
		} else if (trimmingOut) {
			outPoint = rack::math::clamp(pos, inPoint, 1.f);
			syncOutPoint();
		} else if (draggingPlayhead && !currentId.empty()) {
			float newPos = rack::math::clamp(pos, 0.f, 1.f);
			if (newPos != scrubPos) {
				scrubPos = newPos;
				startPlaybackFrom(scrubPos);
			}
		}
	}

	void onDragEnd(const event::DragEnd& e) override {
		if (draggingPlayhead) {
			draggingPlayhead = false;
			startPlaybackFrom(scrubPos);
			return;
		}
		if (trimmingIn || trimmingOut) {
			trimmingIn  = false;
			trimmingOut = false;
			return;
		}
		if (dropHandler && dropHandler->active)
			dropHandler->endDrag(APP->scene->mousePos, worker);
	}

	std::string cachePathFor(const std::string& audioPath) const {
		return cacheDir + "/" + hashPath(audioPath) + ".json";
	}

	bool isPlaying() const { return modulePlaying && modulePlaying->load(); }
};

} // namespace Siren
} // namespace StoermelderPackOne
