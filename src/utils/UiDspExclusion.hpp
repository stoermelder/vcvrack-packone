#pragma once
#include <atomic>
#include <thread>

namespace StoermelderPackOne {

// Mutual exclusion between one realtime thread and one UI thread in which the realtime side
// never waits. Dekker-style: each side stores its flag, then loads the other's, both seq_cst,
// so at least one of them sees the other and the two sections can never overlap.
struct UiDspExclusion {
	std::atomic<bool> dspInside{false};
	std::atomic<bool> uiInside{false};

	// Audio thread. Returns false if the UI is inside: skip the work, retry next process().
	bool tryEnterDsp() {
		dspInside.store(true, std::memory_order_seq_cst);
		if (uiInside.load(std::memory_order_seq_cst)) {
			dspInside.store(false, std::memory_order_release);
			return false;
		}
		return true;
	}
	void leaveDsp() { dspInside.store(false, std::memory_order_release); }

	// UI thread. Keeps uiInside raised while waiting, so the DSP cannot start a new section;
	// the wait is bounded by the one DSP section already running. Nesting is allowed: only the
	// outermost enter/leave touches the flag (uiDepth is UI-thread only, so a plain int).
	void enterUi() {
		if (uiDepth++ > 0) return;
		uiInside.store(true, std::memory_order_seq_cst);
		while (dspInside.load(std::memory_order_seq_cst)) std::this_thread::yield();
	}
	void leaveUi() {
		if (--uiDepth > 0) return;
		uiInside.store(false, std::memory_order_release);
	}

private:
	int uiDepth = 0;
};

}
