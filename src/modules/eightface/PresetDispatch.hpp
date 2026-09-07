#pragma once
#include "../../utils/TaskWorker.hpp"
#include "../../utils/GuiTaskProcessor.hpp"
#include "../../vcv/ui.hpp"
#include <functional>
#include <memory>

namespace StoermelderPackOne {
namespace EightFace {

enum class GUISAFEMODE {
	WORKER,
	GUI,
	GUI_WITH_LOCK
};

// Owns the two destinations a preset load can be dispatched to, and the mode -> hop decision
// between them, for one 8FACE module instance. The mode selects one hop on the engine thread in
// dispatch(), and every dispatched task is a leaf -- no task ever hands work to another.
//
//   Safe / Unsafe               -> guiTasks (GuiTaskProcessor)  -> apply
//   Unsafe fast                 -> taskWorker (ITaskWorker)     -> apply
//   Unsafe fast, mixed preset   -> both, side by side, over disjoint modules
//
// Re-chaining would break GuiTaskProcessor's single-producer contract (worker thread isn't the
// producer). PresetDispatch never touches a preset -- it routes "apply this pass's share of slot
// p" and the owning module supplies the apply as a callback, so differing preset storage never
// becomes this component's problem. It handles ModuleWidget*/json_t* only as opaque handles.
struct PresetDispatch {
	// Which bound modules of a preset a pass should touch. Unsafe fast splits a preset because
	// the allowlist is a per-module property. Private: only Loader (below) ever names a value.
	enum class Part { All, GuiOnly, WorkerOnly };

	// A dispatched pass's share of the apply, bound to its Part. Passed to ApplyFn instead of a
	// bare Part so applyPreset() never spells out or branches on which half it is.
	struct Loader {
		PresetDispatch* dispatch;
		Part part;

		Loader(PresetDispatch* dispatch, Part part) : dispatch(dispatch), part(part) { }

		// Whether this bound module belongs to this pass, for callers that need to act (the
		// auto-mode save) between the decision and the apply.
		bool shouldLoad(bool needsGuiThread) const {
			return dispatch->shouldLoadTarget(part, needsGuiThread);
		}

		// shouldLoad() plus the actual fromJson apply. Returns false if skipped.
		bool load(bool needsGuiThread, ModuleWidget* mw, json_t* moduleJ) const {
			return dispatch->loadTarget(part, needsGuiThread, mw, moduleJ);
		}
	};

	// Applies this pass's share of slot `p` via `loader`. Runs on the UI or worker thread; MUST
	// be a leaf. Passed to dispatch() per call rather than installed at construction, so a caller
	// with per-load data (mk1's moduleId) can capture it fresh each time instead of stashing it
	// in a mutable member a second presetLoad() could clobber before the first task runs.
	using ApplyFn = std::function<void(const Loader& loader, int p)>;

	GuiTaskProcessor<32> guiTasks;
	std::shared_ptr<ITaskWorker> taskWorker;

	/** [Stored to JSON by the owning module] */
	GUISAFEMODE guiSafeMode = GUISAFEMODE::GUI_WITH_LOCK;

	explicit PresetDispatch(std::shared_ptr<ITaskWorker> worker) : taskWorker(std::move(worker)) { }

	// A Loader for the whole preset, for callers that apply directly (tests, mainly) without
	// going through dispatch().
	Loader allLoader() {
		return Loader(this, Part::All);
	}

	// Engine thread ONLY -- never call from the GUI thread, since this enqueues into the
	// single-producer guiTasks. Chooses the single hop and dispatches leaf task(s) calling
	// `apply`. hasGui/hasWorker (computed by the caller; ignored outside WORKER mode) say which
	// halves of a split preset actually need a task, so nothing is dispatched to a destination
	// with nothing to do.
	void dispatch(int p, bool hasGui, bool hasWorker, ApplyFn apply) {
		if (guiSafeMode == GUISAFEMODE::WORKER) {
			if (!hasGui) {
				taskWorker->work([this, p, apply]() { apply(allLoader(), p); });
			}
			else if (!hasWorker) {
				guiTasks.enqueue([this, p, apply]() { apply(allLoader(), p); });
			}
			else {
				guiTasks.enqueue([this, p, apply]() { apply(Loader(this, Part::GuiOnly), p); });
				taskWorker->work([this, p, apply]() { apply(Loader(this, Part::WorkerOnly), p); });
			}
		}
		else {
			guiTasks.enqueue([this, p, apply]() { apply(allLoader(), p); });
		}
	}

	// GUI thread, from the widget's step().
	void step() {
		guiTasks.step();
	}

	// Engine thread, once per divider tick from the module's process().
	void process() {
		guiTasks.process();
	}

	// Test seam: drain the UI queue synchronously.
	void drain() {
		guiTasks.drain();
	}

	// Test seam: observe queue depth without reaching through GuiTaskProcessor's internals.
	size_t pendingGuiTasks() const {
		return guiTasks.internalQueue.queue.size();
	}

	// Unsafe-fast destination, for assertions.
	std::shared_ptr<ITaskWorker> worker() const {
		return taskWorker;
	}

	bool shouldLoadTarget(Part part, bool needsGuiThread) const {
		// Not this pass's half of a split preset -- the other task handles it.
		if (part == Part::GuiOnly && !needsGuiThread) return false;
		if (part == Part::WorkerOnly && needsGuiThread) return false;
		// An allowlisted module can't load off the UI thread, and with no window there is none
		// to load it on -- the crash the allowlist exists to prevent.
		if (needsGuiThread && !vcv::ui::hasWindow()) return false;
		return true;
	}

	bool loadTarget(Part part, bool needsGuiThread, ModuleWidget* mw, json_t* moduleJ) {
		if (!shouldLoadTarget(part, needsGuiThread)) return false;
		if (!mw) return false;
		if (guiSafeMode == GUISAFEMODE::GUI) {
			// Unlocked, deliberately: not perfectly thread-safe, as the engine thread would lock.
			mw->module->fromJson(moduleJ);
		}
		else {
			mw->fromJson(moduleJ);   // Safe, and Unsafe fast: full widget path
		}
		return true;
	}
};

} // namespace EightFace
} // namespace StoermelderPackOne
