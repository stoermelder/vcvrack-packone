#pragma once
#include <rack.hpp>
#include "../../utils/TaskWorker.hpp"


namespace StoermelderPackOne {
namespace Siren {

// Encapsulates all drag-drop state and logic shared between SirenBrowserPane and
// SirenPreviewPane. Owned by SirenWidget; both panes hold a raw pointer to it.
//
// Thread safety: startDrag / endDrag / step all run on the UI thread.
// The task lambda returned by prepareForDropCallback runs on the worker thread —
// it writes pendingDropPath then signals dropPending (release), and step() reads
// dropPending (acquire) before reading pendingDropPath.
struct SirenDropHandler {
	// drag in progress
	bool active = false;
	std::string dragPath;
	std::string dragDisplayName;

	// async conversion state
	std::atomic<bool> converting{false};

	// pending drop (worker → UI thread handoff)
	std::atomic<bool> dropPending{false};
	std::string pendingDropPath;
	Vec pendingDropPos;

	// module widget reference (set by SirenWidget)
	// Used to suppress the label and conversion while the cursor is still over
	// the Siren module itself — drops are only meaningful to external targets.
	widget::Widget* moduleWidget = nullptr;

	// conversion callback wired by SirenWidget
	// Returns a task lambda for the given path. The lambda is dispatched to the
	// worker and its return value is the final drop path.
	std::function<std::function<std::string()>(const std::string&)> prepareForDropCallback;

	// Returns true when the cursor is over the Siren module widget.
	// moduleWidget->box is in rack-local coordinates; getMousePos() matches.
	bool mouseIsInsideModule() const {
		if (!moduleWidget) return false;
		return moduleWidget->box.contains(APP->scene->rack->getMousePos());
	}

	// Returns true when the cursor is over any module widget other than our own.
	bool mouseIsOverOtherModule() const {
		Vec mp = APP->scene->rack->getMousePos();
		for (ModuleWidget* mw : APP->scene->rack->getModules()) {
			if (mw->box.contains(mp)) return true;
		}
		return false;
	}

	// Called on the UI thread when a drag begins.
	void startDrag(const std::string& path, const std::string& displayName) {
		active = true;
		dragPath = path;
		dragDisplayName = displayName;
	}

	void cancelDrag() {
		active = false;
		dragPath.clear();
		dragDisplayName.clear();
	}

	// Called on the UI thread when a drag ends. The drop is only fired when the
	// cursor is over another module widget — empty rack space is ignored.
	void endDrag(Vec dropPos, TaskWorker* worker) {
		if (!active) return;
		active = false;
		std::string path = std::move(dragPath);
		dragPath.clear();

		if (!mouseIsOverOtherModule()) return;
		if (mouseIsInsideModule()) return;

		auto task = prepareForDropCallback
			? prepareForDropCallback(path)
			: [path]() { return path; };

		converting.store(true, std::memory_order_relaxed);
		worker->work([this, task, dropPos]() {
			std::string readyPath = task();
			converting.store(false, std::memory_order_relaxed);
			pendingDropPath = readyPath;
			pendingDropPos = dropPos;
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
};

} // namespace Siren
} // namespace StoermelderPackOne
