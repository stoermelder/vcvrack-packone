#include "utils/thread.hpp"

// Split out of plugin.cpp: these helpers carry no
// dependency on init()'s ~70 addModel() calls, so a test binary that wants
// StoermelderPackOne::thread without linking the whole plugin dylib needs them in their
// own TU.

namespace StoermelderPackOne {
namespace thread {

static std::thread::id uiThreadId;

void captureUiThreadId() {
	uiThreadId = std::this_thread::get_id();
}

std::thread::id getUiThreadId() {
	return uiThreadId;
}

bool verifyEnabled = true;

ThreadVerifier makeVerifier(std::function<bool()> isMyWorkerThread) {
#ifdef DEBUGPLUGIN
	if (verifyEnabled) {
		ThreadVerifier v;
		v.isUiThread = []() {
			return std::this_thread::get_id() == uiThreadId;
		};
		v.isWorkerThread = isMyWorkerThread;
		v.isUiOrWorker = [isMyWorkerThread]() {
			if (std::this_thread::get_id() == uiThreadId) return true;
			return isMyWorkerThread();
		};
		v.isEngine = [isMyWorkerThread]() {
			return !isMyWorkerThread();
		};
		return v;
	}
#endif
	ThreadVerifier v;
	v.isUiThread = []() { return true; };
	v.isWorkerThread = []() { return true; };
	v.isUiOrWorker = []() { return true; };
	v.isEngine = []() { return true; };
	return v;
}

} // namespace thread
} // namespace StoermelderPackOne
