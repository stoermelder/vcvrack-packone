#pragma once
#include <rack.hpp>
#include "SirenAudio.hpp"
#include "SirenDropHandler.hpp"
#include "../../utils/TaskWorker.hpp"
#include <vector>


namespace StoermelderPackOne {
namespace Siren {

// Cosmetic info for the canvas's current display mode — the normal view, or
// a special view (e.g. loop/repitch preview). The parent points the canvas's
// viewMode at one of these each step(). Lets the canvas stay agnostic of how
// many special views exist — adding one is just a new named instance, not a
// new flag.
struct CanvasViewMode {
	NVGcolor accentColor;
	NVGcolor filenameColor;
	std::string label;
	std::string generatingMessage;

	// Accent color shared by the waveform and the header texts (badges, zoom,
	// BPM) when no special view is active.
	static const CanvasViewMode& normal() {
		static const CanvasViewMode info{
			nvgRGBf(0.70f, 0.70f, 0.63f), nvgRGBf(0.92f, 0.92f, 0.88f), "", ""};
		return info;
	}
	static const CanvasViewMode& loopCrossfade() {
		static const CanvasViewMode info{
			nvgRGBf(0.35f, 0.80f, 0.85f), nvgRGBf(0.75f, 0.95f, 0.97f), "LOOP PREVIEW", "Generating loop preview\xe2\x80\xa6"};
		return info;
	}
	static const CanvasViewMode& repitch() {
		static const CanvasViewMode info{
			nvgRGBf(0.95f, 0.65f, 0.30f), nvgRGBf(1.00f, 0.85f, 0.55f), "REPITCH PREVIEW", "Generating repitch preview\xe2\x80\xa6"};
		return info;
	}
};

// SirenWaveformCanvas
// Child widget of SirenPreviewPane that owns the waveform region: drawing,
// trim handles, playhead, scrollbar, readout, and all mouse/keyboard interaction.
//
// The parent feeds display inputs each step(); the canvas owns inPoint/outPoint,
// zoom, and scroll state, and fires callbacks when they change.
//
// In loopPreviewMode:
//   - Waveform is tinted gold to signal loop preview
//   - Trim handles are hidden and interaction is suppressed
//   - A "Generating loop preview…" overlay is shown while building
struct SirenWaveformCanvas : widget::OpaqueWidget {
	static constexpr float WAVE_X = 8.f;
	static constexpr float READOUT_H = 14.f;
	static constexpr float SCROLLBAR_H = 12.f;
	// Minimum time between onScrubTo callbacks while dragging the playhead —
	// each call triggers a seek/ring-buffer refill on the audio thread, which
	// can glitch if fired on every UI frame.
	static constexpr double SCRUB_INTERVAL = 0.1;

	// display inputs (set by parent each step())
	AudioWaveformCache* cache = nullptr;  // non-owning pointer to parent's active cache
	bool cacheReady = false;
	bool loopPreviewMode = false;    // tint + suppress trim handles
	const CanvasViewMode* viewMode = &CanvasViewMode::normal(); // active mode's accent color, shared by waveform + header texts
	bool hasFile = false;
	float durationSeconds = 0.f;
	std::atomic<float>* modulePlayheadPos = nullptr;

	// Single overlay for any background task ("Building waveform…", "Converting…",
	// "Generating loop preview…", "Indexing… N / M", ...). Empty = no overlay.
	std::string statusMessage;
	NVGcolor statusColor = nvgRGBf(1.f, 0.85f, 0.1f);

	// drag source data — updated by parent each step()
	std::string dragPath;
	std::string dragDisplayName;

	SirenDropHandler* dropHandler = nullptr;
	TaskWorker* worker = nullptr;

	// owned state
	float inPoint = 0.f;
	float outPoint = 1.f;
	float zoomLevel = 1.0f;
	float scrollPos = 0.0f;

	float scrubPos = 0.f;
	float dragStartRackX = 0.f;
	float dragStartRackY = 0.f;
	float dragStartScrub = 0.f;
	bool draggingPlayhead = false;
	// Set on a plain left-click in the waveform, before the drag direction is
	// known. Resolved on the first onDragMove past a small threshold: a mostly
	// horizontal drag becomes a playhead drag, a mostly vertical one starts a
	// file drag. A click with no movement falls back to setting the playhead.
	bool dragDirectionPending = false;
	bool trimmingIn = false;
	bool trimmingOut = false;
	bool trimmingRange = false;
	float rangeAnchor = 0.f;
	bool draggingScrollbar = false;
	float dragStartScrollbarX = 0.f;
	double lastScrubTime = 0.0;

	// callbacks
	std::function<void(float)> onInPointChanged;
	std::function<void(float)> onOutPointChanged;
	std::function<void(float)> onScrubTo;
	std::function<void()> onCancelLoopPreview;

	// geometry helpers

	Rect waveformRect() const {
		float waveW = box.size.x - WAVE_X - 4.f;
		float waveH = box.size.y - 2.f - READOUT_H - SCROLLBAR_H - 2.f;
		if (waveH < 20.f) waveH = 20.f;
		return Rect(Vec(WAVE_X, 2.f), Vec(waveW, waveH));
	}

	static constexpr float EXIT_BTN_W = 36.f;
	static constexpr float EXIT_BTN_H = 14.f;

	// "Exit" button shown top-right of the waveform while in loop preview mode —
	// equivalent to the "Cancel loop preview" context menu item.
	Rect exitButtonRect() const {
		Rect r = waveformRect();
		return Rect(Vec(r.pos.x + r.size.x - EXIT_BTN_W - 2.f, r.pos.y + 2.f), Vec(EXIT_BTN_W, EXIT_BTN_H));
	}

	Rect scrollbarRect() const {
		float scrollbarY = box.size.y - SCROLLBAR_H - READOUT_H;
		return Rect(Vec(WAVE_X, scrollbarY), Vec(box.size.x - WAVE_X - 4.f, SCROLLBAR_H));
	}

	float getScrollbarThumbWidth() const {
		Rect sr = scrollbarRect();
		return (zoomLevel <= 1.f) ? sr.size.x : sr.size.x / zoomLevel;
	}

	float getScrollbarThumbX() const {
		Rect sr = scrollbarRect();
		float tw = getScrollbarThumbWidth();
		float maxS = 1.0f - (1.0f / zoomLevel);
		if (maxS <= 0.f) return sr.pos.x;
		return sr.pos.x + (scrollPos / maxS) * (sr.size.x - tw);
	}

	void clampScrollPos() {
		if (zoomLevel <= 1.f) {
			scrollPos = 0.f;
		}
		else {
			float maxS = 1.0f - (1.0f / zoomLevel);
			scrollPos = rack::math::clamp(scrollPos, 0.f, maxS);
		}
	}

	float posToNormalized(Vec pos) const {
		Rect r = waveformRect();
		float normInView = (pos.x - r.pos.x) / r.size.x;
		return rack::math::clamp(scrollPos + normInView / zoomLevel, 0.f, 1.f);
	}

	void resetTrimHandles() {
		inPoint = 0.f;
		outPoint = 1.f;
		if (onInPointChanged) onInPointChanged(inPoint);
		if (onOutPointChanged) onOutPointChanged(outPoint);
	}

	void draw(const DrawArgs& args) override {
		Rect waveRect = waveformRect();
		float waveW = waveRect.size.x;
		float waveH = waveRect.size.y;
		float waveY = waveRect.pos.y;

		std::shared_ptr<Font> font = APP->window->loadFont(asset::system("res/fonts/ShareTechMono-Regular.ttf"));
		nvgFontFaceId(args.vg, font->handle);

		NVGcolor previewColor = viewMode->accentColor;

		// Waveform stroke color: shared with the header texts via viewMode
		NVGcolor waveColor = previewColor;

		// waveform
		if (cache && cacheReady && !cache->empty()) {
			int numCh = (int)cache->samples.size();
			float chH = waveH / numCh;
			float visibleStart = scrollPos;
			float visibleEnd = std::min(scrollPos + 1.0f / zoomLevel, 1.0f);

			// Clip the waveform line to the wave area — some peaks can exceed
			// their channel's height (e.g. hot/clipping signals).
			nvgSave(args.vg);
			nvgScissor(args.vg, WAVE_X, waveY, waveW, waveH);

			nvgLineJoin(args.vg, NVG_ROUND);
			nvgStrokeWidth(args.vg, 1.f);
			nvgStrokeColor(args.vg, waveColor);

			std::vector<float> wavePx;
			std::vector<float> wavePy;
			for (int ch = 0; ch < numCh; ch++) {
				float chY = waveY + ch * chH;
				float midY = chY + chH * 0.5f;
				const auto& chSamples = cache->samples[ch];
				int n = (int)chSamples.size();
				if (n == 0) continue;

				int startIdx = rack::math::clamp((int)(visibleStart * n), 0, n - 1);
				int endIdx = rack::math::clamp((int)(visibleEnd * n) + 1, 0, n);

				wavePx.clear();
				wavePy.clear();
				for (int i = startIdx; i < endIdx; i++) {
					wavePx.push_back(WAVE_X + ((i + 0.5f) / n - scrollPos) * zoomLevel * waveW);
					wavePy.push_back(midY - chSamples[i] * chH * 0.44f);
				}

				if (!wavePx.empty()) {
					nvgBeginPath(args.vg);
					nvgMoveTo(args.vg, wavePx[0], wavePy[0]);
					for (size_t i = 1; i < wavePx.size(); i++) nvgLineTo(args.vg, wavePx[i], wavePy[i]);
					nvgStrokeColor(args.vg, waveColor);
					nvgStrokeWidth(args.vg, 1.f);
					nvgStroke(args.vg);
				}

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

			// Filter overlay: fade the waveform toward the background color
			// away from each channel's zero-line, so high-amplitude peaks
			// look more faint than quiet passages.
			NVGcolor fadeColor = nvgRGB(0x12, 0x12, 0x12);
			for (int ch = 0; ch < numCh; ch++) {
				float chY = waveY + ch * chH;
				float midY = chY + chH * 0.5f;

				nvgBeginPath(args.vg);
				nvgRect(args.vg, WAVE_X, chY, waveW, chH * 0.5f);
				nvgFillPaint(args.vg, nvgLinearGradient(args.vg, 0.f, midY, 0.f, chY,
					nvgRGBAf(fadeColor.r, fadeColor.g, fadeColor.b, 0.f),
					nvgRGBAf(fadeColor.r, fadeColor.g, fadeColor.b, 0.5f)));
				nvgFill(args.vg);

				nvgBeginPath(args.vg);
				nvgRect(args.vg, WAVE_X, midY, waveW, chH * 0.5f);
				nvgFillPaint(args.vg, nvgLinearGradient(args.vg, 0.f, midY, 0.f, chY + chH,
					nvgRGBAf(fadeColor.r, fadeColor.g, fadeColor.b, 0.f),
					nvgRGBAf(fadeColor.r, fadeColor.g, fadeColor.b, 0.5f)));
				nvgFill(args.vg);
			}

			nvgRestore(args.vg);
		}

		// tick marks
		if (durationSeconds > 0.f && waveW > 0.f) {
			float dur = durationSeconds;
			float visibleStart = scrollPos;
			float visibleEnd = std::min(scrollPos + 1.0f / zoomLevel, 1.0f);

			static const float ivs[] = {0.5f, 1.f, 2.f, 5.f, 10.f, 30.f, 60.f, 300.f};
			float tickIv = 1.f;
			for (float iv : ivs) {
				if ((dur / zoomLevel) / iv <= 14.f) { tickIv = iv; break; }
			}
			float tickY = waveY + waveH;
			float startTime = visibleStart * dur;
			float endTime = visibleEnd * dur;
			float firstTick = std::floor(startTime / tickIv) * tickIv;
			for (float t = firstTick; t <= endTime + 0.001f; t += tickIv) {
				if (t < startTime) continue;
				float tx = WAVE_X + (t / dur - scrollPos) * zoomLevel * waveW;
				nvgBeginPath(args.vg);
				nvgMoveTo(args.vg, tx, tickY - 4.f);
				nvgLineTo(args.vg, tx, tickY);
				nvgStrokeColor(args.vg, nvgRGBAf(1.f, 1.f, 1.f, 0.18f));
				nvgStrokeWidth(args.vg, 0.5f);
				nvgStroke(args.vg);
			}
		}

		// trim region + handles (suppressed in loop preview mode)
		if (!loopPreviewMode && hasFile) {
			if ((inPoint > 0.f || outPoint < 1.f) && outPoint > inPoint) {
				float x1 = rack::math::clamp(WAVE_X + (inPoint - scrollPos) * zoomLevel * waveW, WAVE_X, WAVE_X + waveW);
				float x2 = rack::math::clamp(WAVE_X + (outPoint - scrollPos) * zoomLevel * waveW, WAVE_X, WAVE_X + waveW);
				if (x2 > x1) {
					nvgBeginPath(args.vg);
					nvgRect(args.vg, x1, waveY, x2 - x1, waveH);
					nvgFillColor(args.vg, nvgRGBAf(1.f, 0.85f, 0.1f, 0.07f));
					nvgFill(args.vg);
				}
			}
			drawHandle(args.vg, outPoint, false, waveY, waveW, waveH);
			drawHandle(args.vg, inPoint,  true,  waveY, waveW, waveH);
		}

		// playhead
		if (hasFile) {
			float ph = draggingPlayhead ? scrubPos
				: (modulePlayheadPos ? modulePlayheadPos->load(std::memory_order_relaxed) : 0.f);
			float phX = WAVE_X + (ph - scrollPos) * zoomLevel * waveW;
			if (phX >= WAVE_X && phX <= WAVE_X + waveW) {
				nvgBeginPath(args.vg);
				nvgMoveTo(args.vg, phX, waveY);
				nvgLineTo(args.vg, phX, waveY + waveH);
				nvgStrokeColor(args.vg, nvgRGBAf(1.f, 1.f, 1.f, 0.7f));
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

		// "Exit" button (loop preview mode)
		if (loopPreviewMode) {
			Rect br = exitButtonRect();
			nvgBeginPath(args.vg);
			nvgRoundedRect(args.vg, br.pos.x, br.pos.y, br.size.x, br.size.y, 2.f);
			nvgFillColor(args.vg, nvgRGBAf(0.f, 0.f, 0.f, 0.55f));
			nvgFill(args.vg);
			nvgStrokeColor(args.vg, nvgRGBAf(previewColor.r, previewColor.g, previewColor.b, 0.8f));
			nvgStrokeWidth(args.vg, 1.f);
			nvgStroke(args.vg);
			nvgFontSize(args.vg, 10.f);
			nvgFillColor(args.vg, previewColor);
			nvgTextAlign(args.vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
			nvgText(args.vg, br.pos.x + br.size.x * 0.5f, br.pos.y + br.size.y * 0.5f, "Exit", nullptr);
			nvgTextAlign(args.vg, NVG_ALIGN_LEFT | NVG_ALIGN_BASELINE);
		}

		// background-task overlay ("Building waveform…", "Converting…", etc.)
		if (!statusMessage.empty()) {
			drawStatusOverlay(args.vg, waveW, waveY, waveH, statusMessage.c_str(), statusColor);
		}

		// scrollbar
		if (zoomLevel > 1.0f) {
			Rect sr = scrollbarRect();
			if (sr.size.x > 0.f) {
				float thumbW = getScrollbarThumbWidth();
				float thumbX = getScrollbarThumbX();
				nvgBeginPath(args.vg);
				nvgRoundedRect(args.vg, thumbX, sr.pos.y + 1.f, thumbW, sr.size.y - 2.f, 2.f);
				nvgFillColor(args.vg, nvgRGBAf(0.55f, 0.55f, 0.55f, 0.7f));
				nvgFill(args.vg);
			}
		}

		// readout (IN / OUT / LEN / POS)
		auto formatTime = [](float s) -> std::string {
			if (s < 0.f) s = 0.f;
			int mm = (int)(s / 60.f);
			float ss = s - mm * 60.f;
			return rack::string::f("%02d:%05.2f", mm, ss);
		};
		float ph = draggingPlayhead ? scrubPos
			: (modulePlayheadPos ? modulePlayheadPos->load(std::memory_order_relaxed) : 0.f);
		float waveW2 = waveformRect().size.x;
		float col = waveW2 / 4.f + 4.f;
		nvgFontFaceId(args.vg, font->handle);
		nvgFontSize(args.vg, 10.f);
		NVGcolor accent = viewMode->accentColor;
		auto drawReadout = [&](float x, const char* lbl, float val) {
			nvgFillColor(args.vg, nvgRGBAf(accent.r, accent.g, accent.b, 0.5f));
			nvgText(args.vg, x, box.size.y, lbl, nullptr);
			nvgFillColor(args.vg, accent);
			nvgText(args.vg, x + 21.f, box.size.y, formatTime(val).c_str(), nullptr);
		};
		drawReadout(WAVE_X, "IN", inPoint * durationSeconds);
		drawReadout(WAVE_X + col, "OUT", outPoint * durationSeconds);
		drawReadout(WAVE_X + col * 2, "LEN", (outPoint - inPoint) * durationSeconds);
		drawReadout(WAVE_X + col * 3, "POS", ph * durationSeconds);
	}

	void onButton(const event::Button& e) override {
		// "Exit" button (loop preview mode)
		if (e.button == GLFW_MOUSE_BUTTON_LEFT && e.action == GLFW_PRESS && loopPreviewMode) {
			if (exitButtonRect().contains(e.pos)) {
				if (onCancelLoopPreview) onCancelLoopPreview();
				e.consume(this);
				return;
			}
		}

		// Scrollbar
		Rect sr = scrollbarRect();
		if (e.button == GLFW_MOUSE_BUTTON_LEFT && e.action == GLFW_PRESS
				&& sr.contains(e.pos) && zoomLevel != 1.f) {
			float tw = getScrollbarThumbWidth();
			float tx = getScrollbarThumbX();
			if (e.pos.x >= tx && e.pos.x < tx + tw) {
				draggingScrollbar = true;
				dragStartScrollbarX = APP->scene->rack->getMousePos().x;
			}
			else if (zoomLevel > 1.f) {
				float newTx = rack::math::clamp(e.pos.x - tw * 0.5f, sr.pos.x, sr.pos.x + sr.size.x - tw);
				float maxS = 1.0f - (1.0f / zoomLevel);
				scrollPos = ((newTx - sr.pos.x) / (sr.size.x - tw)) * maxS;
				draggingScrollbar = true;
				dragStartScrollbarX = APP->scene->rack->getMousePos().x;
			}
			e.consume(this);
			return;
		}

		if (e.button == GLFW_MOUSE_BUTTON_LEFT && e.action == GLFW_PRESS && hasFile) {
			Rect r = waveformRect();
			if (r.contains(e.pos)) {
				bool shift = (e.mods & RACK_MOD_SHIFT) != 0;
				bool ctrl = (e.mods & RACK_MOD_CTRL) != 0;

				if (shift && !loopPreviewMode) {
					float pos = posToNormalized(e.pos);
					dragStartRackX = APP->scene->rack->getMousePos().x;
					dragStartScrub = pos;

					bool hasRange = (inPoint > 0.f || outPoint < 1.f);
					float inScreenX = r.pos.x + (inPoint - scrollPos) * zoomLevel * r.size.x;
					float outScreenX = r.pos.x + (outPoint - scrollPos) * zoomLevel * r.size.x;
					const float thresh = 8.f;
					bool nearIn = hasRange && std::abs(e.pos.x - inScreenX) < thresh;
					bool nearOut = hasRange && std::abs(e.pos.x - outScreenX) < thresh;

					if (nearIn) {
						trimmingIn = true;
						dragStartScrub = inPoint;
					}
					else if (nearOut) {
						trimmingOut = true;
						dragStartScrub = outPoint;
					}
					else {
						rangeAnchor = pos;
						inPoint = pos;
						outPoint = pos;
						trimmingRange = true;
						if (onInPointChanged) onInPointChanged(inPoint);
						if (onOutPointChanged) onOutPointChanged(outPoint);
					}
				}
				else if (!ctrl) {
					Vec mp = APP->scene->rack->getMousePos();
					dragStartRackX = mp.x;
					dragStartRackY = mp.y;
					dragStartScrub = posToNormalized(e.pos);
					dragDirectionPending = true;
				}
				// Ctrl held (or loop preview) → onDragStart fires the file drop
				e.consume(this);
				return;
			}
		}
		widget::OpaqueWidget::onButton(e);
	}

	void onDragStart(const event::DragStart& e) override {
		if (hasFile && !draggingPlayhead && !trimmingIn && !trimmingOut
				&& !trimmingRange && !draggingScrollbar && !dragDirectionPending && dropHandler) {
			dropHandler->startDrag(dragPath, dragDisplayName);
		}
	}

	void onDragMove(const event::DragMove& e) override {
		if (draggingScrollbar) {
			Rect sr = scrollbarRect();
			if (sr.size.x > 0.f) {
				float tw = getScrollbarThumbWidth();
				float dx = APP->scene->rack->getMousePos().x - dragStartScrollbarX;
				float newTx = rack::math::clamp(getScrollbarThumbX() + dx,
					sr.pos.x, sr.pos.x + sr.size.x - tw);
				float maxS = 1.0f - (1.0f / zoomLevel);
				scrollPos = ((newTx - sr.pos.x) / (sr.size.x - tw)) * maxS;
				dragStartScrollbarX = APP->scene->rack->getMousePos().x;
			}
			return;
		}

		if (dragDirectionPending) {
			Vec mp = APP->scene->rack->getMousePos();
			float ddx = mp.x - dragStartRackX;
			float ddy = mp.y - dragStartRackY;
			const float threshold = 2.f;
			if (std::abs(ddx) < threshold && std::abs(ddy) < threshold) return;

			dragDirectionPending = false;
			// Require movement to be close to the horizontal axis for a playhead
			// drag — diagonal movement starts a file drag instead.
			if (std::abs(ddx) >= std::abs(ddy) * 2.f) {
				draggingPlayhead = true;
				scrubPos = dragStartScrub;
			}
			else if (dropHandler) {
				dropHandler->startDrag(dragPath, dragDisplayName);
			}
		}

		Rect r = waveformRect();
		if (r.size.x <= 0.f) return;
		float dx = APP->scene->rack->getMousePos().x - dragStartRackX;
		float pos = dragStartScrub + dx / (r.size.x * zoomLevel);

		if (trimmingIn) {
			inPoint = rack::math::clamp(pos, 0.f, outPoint);
			if (onInPointChanged) onInPointChanged(inPoint);
		}
		else if (trimmingOut) {
			outPoint = rack::math::clamp(pos, inPoint, 1.f);
			if (onOutPointChanged) onOutPointChanged(outPoint);
		}
		else if (trimmingRange) {
			float cp = rack::math::clamp(pos, 0.f, 1.f);
			inPoint = std::min(rangeAnchor, cp);
			outPoint = std::max(rangeAnchor, cp);
			if (onInPointChanged) onInPointChanged(inPoint);
			if (onOutPointChanged) onOutPointChanged(outPoint);
		}
		else if (draggingPlayhead && hasFile) {
			float np = rack::math::clamp(pos, 0.f, 1.f);
			if (np != scrubPos) {
				scrubPos = np;
				double now = rack::system::getTime();
				if (now - lastScrubTime >= SCRUB_INTERVAL) {
					lastScrubTime = now;
					if (onScrubTo) onScrubTo(scrubPos);
				}
			}
		}
	}

	void onDragEnd(const event::DragEnd& e) override {
		if (draggingScrollbar) {
			draggingScrollbar = false;
			return;
		}
		// Plain click without enough movement to pick a drag direction —
		// treat it as setting the playhead.
		if (dragDirectionPending) {
			dragDirectionPending = false;
			scrubPos = dragStartScrub;
			if (onScrubTo) onScrubTo(scrubPos);
			return;
		}
		if (draggingPlayhead)  {
			draggingPlayhead = false;
			if (onScrubTo) onScrubTo(scrubPos);
			return;
		}
		if (trimmingIn || trimmingOut || trimmingRange) {
			trimmingIn = trimmingOut = trimmingRange = false;
			return;
		}
		if (dropHandler && dropHandler->active) {
			dropHandler->endDrag(APP->scene->mousePos, worker);
		}
	}

	void onHoverScroll(const HoverScrollEvent& e) override {
		Rect r = waveformRect();
		float cursorNorm = (r.size.x > 0.f)
			? scrollPos + (e.pos.x - r.pos.x) / (r.size.x * zoomLevel)
			: scrollPos;
		float factor = (e.scrollDelta.y > 0) ? 1.3f : (1.f / 1.3f);
		zoomLevel = rack::math::clamp(zoomLevel * factor, 1.0f, 10.0f);
		if (r.size.x > 0.f) {
			scrollPos = cursorNorm - (e.pos.x - r.pos.x) / (r.size.x * zoomLevel);
		}
		clampScrollPos();
		e.consume(this);
	}

	void onSelectKey(const event::SelectKey& e) override {
		if (e.action == GLFW_PRESS) {
			if (e.key == GLFW_KEY_ESCAPE && loopPreviewMode) {
				if (onCancelLoopPreview) onCancelLoopPreview();
				e.consume(this); return;
			}
			if (e.key == GLFW_KEY_EQUAL || e.key == GLFW_KEY_RIGHT_BRACKET) {
				zoomLevel = rack::math::clamp(zoomLevel * 1.3f, 1.0f, 10.0f);
				clampScrollPos();
				e.consume(this); return;
			}
			if (e.key == GLFW_KEY_MINUS || e.key == GLFW_KEY_LEFT_BRACKET) {
				zoomLevel = rack::math::clamp(zoomLevel / 1.3f, 1.0f, 10.0f);
				clampScrollPos();
				e.consume(this); return;
			}
			if (e.key == GLFW_KEY_0) {
				zoomLevel = 1.0f;
				scrollPos = 0.0f;
				e.consume(this); return;
			}
			// Forward browser-navigation keys to the module widget
			if (e.key == GLFW_KEY_SPACE || e.key == GLFW_KEY_UP || e.key == GLFW_KEY_DOWN
					|| e.key == GLFW_KEY_LEFT || e.key == GLFW_KEY_RIGHT) {
				if (ModuleWidget* mw = getAncestorOfType<ModuleWidget>()) {
					mw->onSelectKey(e);
				}
				return;
			}
		}
		widget::OpaqueWidget::onSelectKey(e);
	}

	void drawHandle(NVGcontext* vg, float norm, bool isIn,
			float waveY, float waveW, float waveH) {
		if (isIn  && norm <= 0.f) return;
		if (!isIn && norm >= 1.f) return;
		float hx = WAVE_X + (norm - scrollPos) * zoomLevel * waveW;
		if (hx < WAVE_X || hx > WAVE_X + waveW) return;
		nvgBeginPath(vg);
		nvgMoveTo(vg, hx, waveY);
		nvgLineTo(vg, hx, waveY + waveH);
		nvgStrokeColor(vg, nvgRGBAf(1.f, 0.85f, 0.1f, 0.7f));
		nvgStrokeWidth(vg, 1.f);
		nvgStroke(vg);
		const float ts = 4.f;
		nvgBeginPath(vg);
		nvgMoveTo(vg, hx - ts, waveY + waveH);
		nvgLineTo(vg, hx + ts, waveY + waveH);
		nvgLineTo(vg, hx,      waveY + waveH - ts * 1.4f);
		nvgClosePath(vg);
		nvgFillColor(vg, nvgRGBAf(1.f, 0.85f, 0.1f, 0.85f));
		nvgFill(vg);
	}

	void drawStatusOverlay(NVGcontext* vg, float waveW, float waveY, float waveH,
			const char* msg, NVGcolor color) {
		float oy = waveY + waveH * 0.5f - 9.f;
		nvgBeginPath(vg);
		nvgRoundedRect(vg, WAVE_X, oy, waveW, 18.f, 3.f);
		nvgFillColor(vg, nvgRGBAf(0.f, 0.f, 0.f, 0.65f));
		nvgFill(vg);
		nvgFontFaceId(vg, APP->window->uiFont->handle);
		nvgFontSize(vg, BND_LABEL_FONT_SIZE);
		nvgFillColor(vg, color);
		nvgTextAlign(vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
		nvgText(vg, WAVE_X + waveW * 0.5f, oy + 9.f, msg, nullptr);
		nvgTextAlign(vg, NVG_ALIGN_LEFT | NVG_ALIGN_BASELINE);
	}
};

} // namespace Siren
} // namespace StoermelderPackOne