#pragma once
#include <rack.hpp>
#include <string>
#include <functional>
#include <atomic>
#include "../../utils/TaskWorker.hpp"

namespace StoermelderPackOne {
namespace Siren {

// Encapsulates all drag-drop state and logic shared between SirenBrowserPane and
// SirenPreviewPane. Owned by SirenWidget; both panes hold a raw pointer to it.
//
// Thread safety: startDrag / endDrag / step / drawLabel all run on the UI thread.
// The task lambda returned by prepareForDropCallback runs on the worker thread —
// it writes pendingDropPath then signals dropPending (release), and step() reads
// dropPending (acquire) before reading pendingDropPath.
struct SirenDropHandler {
	// ── drag in progress ──────────────────────────────────────────────────────
	bool        active   = false;
	std::string dragPath;

	// ── async conversion state ────────────────────────────────────────────────
	std::atomic<bool> converting{false};

	// ── pending drop (worker → UI thread handoff) ─────────────────────────────
	std::atomic<bool> dropPending{false};
	std::string       pendingDropPath;
	Vec               pendingDropPos;

	// ── module widget reference (set by SirenWidget) ─────────────────────────
	// Used to suppress the label and conversion while the cursor is still over
	// the Siren module itself — drops are only meaningful to external targets.
	widget::Widget* moduleWidget = nullptr;

	// ── conversion callback wired by SirenWidget ──────────────────────────────
	// Returns a task lambda for the given path. The lambda is dispatched to the
	// worker and its return value is the final drop path.
	std::function<std::function<std::string()>(const std::string&)> prepareForDropCallback;

	// Returns true when the cursor is over the Siren module widget.
	// moduleWidget->box is in rack-local coordinates; getMousePos() matches.
	bool mouseIsInsideModule() const {
		if (!moduleWidget) return false;
		return moduleWidget->box.contains(APP->scene->rack->getMousePos());
	}

	// Called on the UI thread when a drag begins.
	void startDrag(const std::string& path) {
		active   = true;
		dragPath = path;
	}

	// Called on the UI thread when a drag ends. If the cursor is still over the
	// Siren module, the drag is cancelled silently. Otherwise a task lambda is
	// obtained from prepareForDropCallback and dispatched to the worker; the
	// PathDrop fires from step() once the task returns.
	void endDrag(Vec dropPos, TaskWorker* worker) {
		if (!active) return;
		active = false;
		std::string path = std::move(dragPath);
		dragPath.clear();

		if (mouseIsInsideModule()) return;

		auto task = prepareForDropCallback
		    ? prepareForDropCallback(path)
		    : [path]() { return path; };

		converting.store(true, std::memory_order_relaxed);
		worker->work([this, task, dropPos]() {
			std::string readyPath = task();
			converting.store(false, std::memory_order_relaxed);
			pendingDropPath = readyPath;
			pendingDropPos  = dropPos;
			dropPending.store(true, std::memory_order_release);
		});
	}

	// Call from widget::step() on the UI thread. Fires handleDrop once the
	// worker signals that conversion is complete.
	void step() {
		if (dropPending.load(std::memory_order_acquire)) {
			dropPending.store(false, std::memory_order_relaxed);
			APP->event->handleDrop(pendingDropPos, {pendingDropPath});
		}
	}

	// Draw a floating filename label at the cursor. Call from drawLayer (layer 1).
	// Suppressed while the cursor is over the Siren module itself.
	void drawLabel(const Widget::DrawArgs& args, widget::Widget* self, const std::string& label) const {
		if (!active || mouseIsInsideModule()) return;
		Vec lp = APP->scene->mousePos
		         .minus(self->getRelativeOffset(Vec(0, 0), APP->scene))
		         .div(self->getRelativeZoom(APP->scene));
		nvgBeginPath(args.vg);
		nvgRoundedRect(args.vg, lp.x + 10.f, lp.y, 150.f, 18.f, 3.f);
		nvgFillColor(args.vg, nvgRGBAf(0.f, 0.f, 0.f, 0.7f));
		nvgFill(args.vg);
		nvgFontSize(args.vg, 10.f);
		nvgFillColor(args.vg, nvgRGBf(1.f, 0.85f, 0.1f));
		nvgText(args.vg, lp.x + 14.f, lp.y + 12.f, label.c_str(), nullptr);
	}
};

} // namespace Siren
} // namespace StoermelderPackOne
