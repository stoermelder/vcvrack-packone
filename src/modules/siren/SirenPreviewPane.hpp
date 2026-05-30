#pragma once
#include <rack.hpp>
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
	static constexpr float SCROLLBAR_H = 12.f; // scrollbar height
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
	bool  trimmingRange    = false;  // Shift+drag to define a new range from scratch
	float rangeAnchor      = 0.f;   // normalized start position for trimmingRange

	// ── zoom and scroll state ─────────────────────────────────────────────────
	float zoomLevel  = 1.0f;  // 1.0 = fit to width; up to ~10.0 max
	float scrollPos  = 0.0f;  // horizontal scroll position [0, 1]
	bool  draggingScrollbar = false;
	float dragStartScrollbarX = 0.f;

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

	// Pending waveform cache from worker
	std::atomic<int> cacheGeneration{0};
	struct PendingCache { WaveformCache cache; int gen = -1; bool valid = false; };
	PendingCache      pendingCache;
	std::atomic<bool> pendingCacheReady{false};

	// Cache dir path (set by module widget)
	std::string cacheDir;

	// ── Scrollbar and zoom helpers ────────────────────────────────────────────
	Rect scrollbarRect() const {
		float w = box.size.x;
		float h = box.size.y;
		float scrollbarY = h - SCROLLBAR_H - READOUT_H;  // Position above the readout
		return Rect(Vec(WAVE_X, scrollbarY), Vec(w - WAVE_X - 4.f, SCROLLBAR_H));
	}

	float getScrollbarThumbWidth() const {
		Rect sr = scrollbarRect();
		if (zoomLevel <= 1.0f) return sr.size.x;  // no scrollbar when not zoomed
		return sr.size.x / zoomLevel;
	}

	float getScrollbarThumbX() const {
		Rect sr = scrollbarRect();
		float thumbW = getScrollbarThumbWidth();
		float maxScroll = 1.0f - (1.0f / zoomLevel);
		if (maxScroll <= 0.f) return sr.pos.x;
		return sr.pos.x + (scrollPos / maxScroll) * (sr.size.x - thumbW);
	}

	void clampScrollPos() {
		if (zoomLevel <= 1.0f) {
			scrollPos = 0.0f;
		} else {
			float maxScroll = 1.0f - (1.0f / zoomLevel);
			scrollPos = rack::math::clamp(scrollPos, 0.0f, std::max(0.0f, maxScroll));
		}
	}

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
		zoomLevel    = 1.0f;
		scrollPos    = 0.0f;
		syncInPoint();
		syncOutPoint();

		if (id.empty() || !src) return;

		src->loadAudioInfo(id, info);

		int64_t ts        = src->getTimestamp(id);
		std::string cacheFile = cachePathFor(id);
		if (!forceRebuild) {
			WaveformCache loaded;
			if (loadWaveformCacheFile(cacheFile, ts, loaded) && loaded.sampleCount > 0) {
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
		float waveH = h - waveY - READOUT_H - SCROLLBAR_H - 4.f;
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

			nvgFontSize(args.vg, 10.f);
			nvgFillColor(args.vg, nvgRGBf(0.50f, 0.50f, 0.50f));

			// Zoom level — right-aligned in top bar
			std::string zoomText = rack::string::f("%.1fx", zoomLevel);
			nvgTextAlign(args.vg, NVG_ALIGN_RIGHT | NVG_ALIGN_BASELINE);
			nvgText(args.vg, w - 4.f, 26.f, zoomText.c_str(), nullptr);
			nvgTextAlign(args.vg, NVG_ALIGN_LEFT | NVG_ALIGN_BASELINE);

			// ch · sr · bit (left side of second row)
			std::string badges;
			if (info.bitDepth > 0)   badges = rack::string::f("%dbit", info.bitDepth);
			if (info.sampleRate > 0) badges = rack::string::f("%dk", info.sampleRate / 1000) + (badges.empty() ? "" : " · ") + badges;
			if (info.channels > 0)   badges = std::string(info.channels == 1 ? "MONO" : "STEREO") + (badges.empty() ? "" : " · ") + badges;
			if (!badges.empty()) {
				nvgText(args.vg, WAVE_X, 26.f, badges.c_str(), nullptr);
			}
		}

		int numCh = cacheReady ? (int)cache.samples.size() : info.channels;
		if (numCh < 1) numCh = 1;

		if (cacheReady && !cache.empty()) {
			float chH          = waveH / numCh;
			float visibleStart = scrollPos;
			float visibleEnd   = std::min(scrollPos + 1.0f / zoomLevel, 1.0f);

			nvgLineJoin(args.vg, NVG_ROUND);
			nvgStrokeColor(args.vg, nvgRGBf(0.70f, 0.70f, 0.63f));
			nvgStrokeWidth(args.vg, 1.f);

			for (int ch = 0; ch < (int)cache.samples.size(); ch++) {
				float chY  = waveY + ch * chH;
				float midY = chY + chH * 0.5f;
				const auto& chSamples = cache.samples[ch];
				int n = (int)chSamples.size();
				if (n == 0) continue;

				int startIdx = rack::math::clamp((int)(visibleStart * n),     0, n - 1);
				int endIdx   = rack::math::clamp((int)(visibleEnd   * n) + 1, 0, n);

				nvgBeginPath(args.vg);
				bool first = true;
				for (int i = startIdx; i < endIdx; i++) {
					float px = WAVE_X + ((i + 0.5f) / n - scrollPos) * zoomLevel * waveW;
					float py = midY - chSamples[i] * chH * 0.44f;
					if (first) { nvgMoveTo(args.vg, px, py); first = false; }
					else        nvgLineTo(args.vg, px, py);
				}
				nvgStroke(args.vg);

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

				// Restore waveform stroke state for next channel
				nvgStrokeColor(args.vg, nvgRGBf(0.70f, 0.70f, 0.63f));
				nvgStrokeWidth(args.vg, 1.f);
			}
		}
		else if (cacheBuilding) {
			float oy = waveY + waveH * 0.5f - 9.f;
			nvgBeginPath(args.vg);
			nvgRoundedRect(args.vg, WAVE_X, oy, waveW, 18.f, 3.f);
			nvgFillColor(args.vg, nvgRGBAf(0.f, 0.f, 0.f, 0.65f));
			nvgFill(args.vg);
			nvgFontFaceId(args.vg, APP->window->uiFont->handle);
			nvgFontSize(args.vg, BND_LABEL_FONT_SIZE);
			nvgFillColor(args.vg, nvgRGBf(1.f, 0.85f, 0.1f));
			nvgTextAlign(args.vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
			nvgText(args.vg, WAVE_X + waveW * 0.5f, oy + 9.f, "Building waveform…", nullptr);
			nvgTextAlign(args.vg, NVG_ALIGN_LEFT | NVG_ALIGN_BASELINE);
		}

		// Tick marks along waveform bottom
		if (info.durationSeconds > 0.f && waveW > 0.f) {
			float dur = info.durationSeconds;
			float visibleStart = scrollPos;
			float visibleEnd   = scrollPos + (1.0f / zoomLevel);
			if (visibleEnd > 1.0f) visibleEnd = 1.0f;

			static const float ivs[] = {0.5f, 1.f, 2.f, 5.f, 10.f, 30.f, 60.f, 300.f};
			float tickIv = 1.f;
			float zoomAdjustedDur = dur / zoomLevel;
			for (float iv : ivs) {
				if (zoomAdjustedDur / iv <= 14.f) { tickIv = iv; break; }
			}
			float tickY = waveY + waveH;
			float startTime = visibleStart * dur;
			float endTime = visibleEnd * dur;
			float firstTick = std::floor(startTime / tickIv) * tickIv;
			for (float t = firstTick; t <= endTime + 0.001f; t += tickIv) {
				if (t < startTime) continue;
				float tNorm = t / dur;
				float tx = WAVE_X + (tNorm - scrollPos) * zoomLevel * waveW;
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
			float x1 = WAVE_X + (inPoint - scrollPos) * zoomLevel * waveW;
			float x2 = WAVE_X + (outPoint - scrollPos) * zoomLevel * waveW;
			x1 = rack::math::clamp(x1, WAVE_X, WAVE_X + waveW);
			x2 = rack::math::clamp(x2, WAVE_X, WAVE_X + waveW);
			if (x2 > x1) {
				nvgBeginPath(args.vg);
				nvgRect(args.vg, x1, waveY, x2 - x1, waveH);
				nvgFillColor(args.vg, nvgRGBAf(1.f, 0.85f, 0.1f, 0.07f));
				nvgFill(args.vg);
			}
		}

		// OUT handle — only shown when moved from its default (1.0)
		if (!currentId.empty() && outPoint < 1.f) {
			float opX = WAVE_X + (outPoint - scrollPos) * zoomLevel * waveW;
			if (opX >= WAVE_X && opX <= WAVE_X + waveW) {
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
		}

		// IN handle — only shown when moved from its default (0.0)
		if (!currentId.empty() && inPoint > 0.f) {
			float ipX = WAVE_X + (inPoint - scrollPos) * zoomLevel * waveW;
			if (ipX >= WAVE_X && ipX <= WAVE_X + waveW) {
				nvgBeginPath(args.vg);
				nvgMoveTo(args.vg, ipX, waveY);
				nvgLineTo(args.vg, ipX, waveY + waveH);
				nvgStrokeColor(args.vg, nvgRGBAf(1.f, 0.85f, 0.1f, 0.7f));
				nvgStrokeWidth(args.vg, 1.f);
				nvgStroke(args.vg);

				const float ts = 4.f;
				nvgBeginPath(args.vg);
				nvgMoveTo(args.vg, ipX - ts, waveY + waveH);
				nvgLineTo(args.vg, ipX + ts, waveY + waveH);
				nvgLineTo(args.vg, ipX,      waveY + waveH - ts * 1.4f);
				nvgClosePath(args.vg);
				nvgFillColor(args.vg, nvgRGBAf(1.f, 0.85f, 0.1f, 0.85f));
				nvgFill(args.vg);
			}
		}

		// Playhead line + triangle pointer
		// During a drag we read scrubPos directly — the DSP thread continuously
		// overwrites modulePlayheadPos via process(), so it lags behind and would
		// not reflect the drag position until the fill thread finishes seeking.
		if (!currentId.empty()) {
			float ph = draggingPlayhead ? scrubPos
			         : (modulePlayheadPos ? modulePlayheadPos->load(std::memory_order_relaxed) : 0.f);
			float phX = WAVE_X + (ph - scrollPos) * zoomLevel * waveW;
			if (phX >= WAVE_X && phX <= WAVE_X + waveW) {
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

		// ── scrollbar and zoom display ────────────────────────────────────────
		Rect sr = scrollbarRect();
		if (sr.size.x > 0.f) {
			// Scrollbar background
			/*
			nvgBeginPath(args.vg);
			nvgRect(args.vg, sr.pos.x, sr.pos.y, sr.size.x, sr.size.y);
			nvgFillColor(args.vg, nvgRGBAf(0.f, 0.f, 0.f, 0.3f));
			nvgFill(args.vg);
			*/

			// Scrollbar thumb
			if (zoomLevel > 1.0f) {
				float thumbW = getScrollbarThumbWidth();
				float thumbX = getScrollbarThumbX();
				nvgBeginPath(args.vg);
				nvgRoundedRect(args.vg, thumbX, sr.pos.y + 1.f, thumbW, sr.size.y - 2.f, 2.f);
				nvgFillColor(args.vg, nvgRGBAf(0.55f, 0.55f, 0.55f, 0.7f));
				nvgFill(args.vg);
			}
		}

	}

	// ── waveform interaction ──────────────────────────────────────────────────

	Rect waveformRect() const {
		float waveY = TB_H + 2.f;
		float waveW = box.size.x - WAVE_X - 4.f;
		float waveH = box.size.y - waveY - READOUT_H - SCROLLBAR_H - 4.f;
		if (waveH < 20.f) waveH = 20.f;
		return Rect(Vec(WAVE_X, waveY), Vec(waveW, waveH));
	}

	bool inWaveformArea(Vec pos) const {
		return waveformRect().contains(pos);
	}

	float posToPlayhead(Vec pos) const {
		Rect r = waveformRect();
		float normInView = (pos.x - r.pos.x) / r.size.x;  // [0,1] within visible viewport
		return rack::math::clamp(scrollPos + normInView / zoomLevel, 0.f, 1.f);
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

		// Loop playback toggle — persisted globally via onLoopingChanged callback
		menu->addChild(createCheckMenuItem("Loop playback", "",
			[]() { return sirenSettings.loopPlayback; },
			[]() { sirenSettings.loopPlayback = !sirenSettings.loopPlayback; }
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

		// Scrollbar click/drag
		Rect sr = scrollbarRect();
		if (e.button == GLFW_MOUSE_BUTTON_LEFT && e.action == GLFW_PRESS && sr.contains(e.pos) && zoomLevel != 1.f) {
			float thumbW = getScrollbarThumbWidth();
			float thumbX = getScrollbarThumbX();
			if (e.pos.x >= thumbX && e.pos.x < thumbX + thumbW) {
				// Clicked on thumb — start drag (store in rack coords to match onDragMove)
				draggingScrollbar = true;
				dragStartScrollbarX = APP->scene->rack->getMousePos().x;
			}
			else if (zoomLevel > 1.0f) {
				// Clicked in track outside thumb — jump scroll so thumb centers on click
				float newThumbX = e.pos.x - thumbW * 0.5f;
				float maxThumbX = sr.pos.x + sr.size.x - thumbW;
				newThumbX = rack::math::clamp(newThumbX, sr.pos.x, maxThumbX);
				float maxScroll = 1.0f - (1.0f / zoomLevel);
				scrollPos = ((newThumbX - sr.pos.x) / (sr.size.x - thumbW)) * maxScroll;
				draggingScrollbar = true;
				dragStartScrollbarX = APP->scene->rack->getMousePos().x;
			}
			e.consume(this);
			return;
		}

		if (e.button == GLFW_MOUSE_BUTTON_LEFT && e.action == GLFW_PRESS) {
			if (!currentId.empty() && inWaveformArea(e.pos)) {
				bool shift = (e.mods & RACK_MOD_SHIFT) != 0;
				bool ctrl  = (e.mods & RACK_MOD_CTRL) != 0;
				if (shift) {
					Rect r = waveformRect();
					float pos = posToPlayhead(e.pos);
					dragStartRackX = APP->scene->rack->getMousePos().x;
					dragStartScrub = pos;

					bool hasRange = (inPoint > 0.f || outPoint < 1.f);
					float inScreenX  = r.pos.x + (inPoint  - scrollPos) * zoomLevel * r.size.x;
					float outScreenX = r.pos.x + (outPoint - scrollPos) * zoomLevel * r.size.x;
					const float handleThresh = 8.f;
					bool nearIn  = hasRange && std::abs(e.pos.x - inScreenX)  < handleThresh;
					bool nearOut = hasRange && std::abs(e.pos.x - outScreenX) < handleThresh;

					if (nearIn) {
						trimmingIn     = true;
						dragStartScrub = inPoint;
					}
					else if (nearOut) {
						trimmingOut    = true;
						dragStartScrub = outPoint;
					}
					else {
						// Not near a handle — Shift+drag always defines a new range from scratch
						rangeAnchor   = pos;
						inPoint       = pos;
						outPoint      = pos;
						trimmingRange = true;
						syncInPoint();
						syncOutPoint();
					}
				} 
				else if (!ctrl) {
					// Playhead scrubbing — moves only the playhead, not the trim handles
					scrubPos       = posToPlayhead(e.pos);
					dragStartRackX = APP->scene->rack->getMousePos().x;
					dragStartScrub = scrubPos;
					draggingPlayhead = true;
				}
				// Ctrl held: no drag mode set → onDragStart initiates a file drag

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
		if (!currentId.empty() && !draggingPlayhead && !trimmingIn && !trimmingOut && !trimmingRange && !draggingScrollbar && dropHandler) {
			dropHandler->startDrag(currentId);
		}
	}

	void onDragMove(const event::DragMove& e) override {
		if (draggingScrollbar) {
			Rect sr = scrollbarRect();
			if (sr.size.x > 0.f) {
				float thumbW = getScrollbarThumbWidth();
				float dx = APP->scene->rack->getMousePos().x - dragStartScrollbarX;
				float newThumbX = getScrollbarThumbX() + dx;
				float maxThumbX = sr.pos.x + sr.size.x - thumbW;
				newThumbX = rack::math::clamp(newThumbX, sr.pos.x, maxThumbX);
				float maxScroll = 1.0f - (1.0f / zoomLevel);
				scrollPos = ((newThumbX - sr.pos.x) / (sr.size.x - thumbW)) * maxScroll;
				dragStartScrollbarX = APP->scene->rack->getMousePos().x;
			}
			return;
		}

		Rect r = waveformRect();
		if (r.size.x <= 0.f) return;
		float dx = APP->scene->rack->getMousePos().x - dragStartRackX;
		float pos = dragStartScrub + dx / (r.size.x * zoomLevel);

		if (trimmingIn) {
			inPoint = rack::math::clamp(pos, 0.f, outPoint);
			syncInPoint();
		}
		else if (trimmingOut) {
			outPoint = rack::math::clamp(pos, inPoint, 1.f);
			syncOutPoint();
		}
		else if (trimmingRange) {
			float currentPos = rack::math::clamp(pos, 0.f, 1.f);
			inPoint  = std::min(rangeAnchor, currentPos);
			outPoint = std::max(rangeAnchor, currentPos);
			syncInPoint();
			syncOutPoint();
		}
		else if (draggingPlayhead && !currentId.empty()) {
			float newPos = rack::math::clamp(pos, 0.f, 1.f);
			if (newPos != scrubPos) {
				scrubPos = newPos;
				startPlaybackFrom(scrubPos);
			}
		}
	}

	void onDragEnd(const event::DragEnd& e) override {
		if (draggingScrollbar) {
			draggingScrollbar = false;
			return;
		}
		if (draggingPlayhead) {
			draggingPlayhead = false;
			startPlaybackFrom(scrubPos);
			return;
		}
		if (trimmingIn || trimmingOut || trimmingRange) {
			trimmingIn    = false;
			trimmingOut   = false;
			trimmingRange = false;
			return;
		}
		if (dropHandler && dropHandler->active) {
			dropHandler->endDrag(APP->scene->mousePos, worker);
		}
	}

	void onSelectKey(const event::SelectKey& e) override {
		if (e.action == GLFW_PRESS) {
			// Zoom in with + or ]
			if (e.key == GLFW_KEY_EQUAL || e.key == GLFW_KEY_RIGHT_BRACKET) {
				zoomLevel = rack::math::clamp(zoomLevel * 1.3f, 1.0f, 10.0f);
				clampScrollPos();
				e.consume(this);
				return;
			}
			// Zoom out with - or [
			if (e.key == GLFW_KEY_MINUS || e.key == GLFW_KEY_LEFT_BRACKET) {
				zoomLevel = rack::math::clamp(zoomLevel / 1.3f, 1.0f, 10.0f);
				clampScrollPos();
				e.consume(this);
				return;
			}
			// Reset zoom with 0
			if (e.key == GLFW_KEY_0) {
				zoomLevel = 1.0f;
				scrollPos = 0.0f;
				e.consume(this);
				return;
			}
		}
		widget::OpaqueWidget::onSelectKey(e);
	}

	void onHoverScroll(const HoverScrollEvent& e) override {
		Rect r = waveformRect();
		// Normalized position under the cursor before zoom change
		float cursorNorm = (r.size.x > 0.f)
			? scrollPos + (e.pos.x - r.pos.x) / (r.size.x * zoomLevel)
			: scrollPos;

		float factor = (e.scrollDelta.y > 0) ? 1.3f : (1.f / 1.3f);
		zoomLevel = rack::math::clamp(zoomLevel * factor, 1.0f, 10.0f);

		// Reposition scroll so the point under the cursor stays fixed
		if (r.size.x > 0.f) {
			scrollPos = cursorNorm - (e.pos.x - r.pos.x) / (r.size.x * zoomLevel);
		}
		clampScrollPos();
		e.consume(this);
	}

	std::string cachePathFor(const std::string& audioPath) const {
		return cacheDir + "/" + hashPath(audioPath) + ".json";
	}

	bool isPlaying() const {
		return modulePlaying && modulePlaying->load();
	}
};

} // namespace Siren
} // namespace StoermelderPackOne
