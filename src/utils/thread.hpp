#pragma once
#include <cassert>
#include <functional>
#include <thread>

namespace StoermelderPackOne {
namespace thread {

/** Rack's UI (main) thread id, captured by captureUiThreadId() during plugin init.
 *
 * Identity is captured as std::thread::id (the portable C++ handle) because Rack 2.6
 * no longer exposes a public thread API — rack::thread::mainThread / Thread were
 * removed after Rack 2.5, so the UI thread can only be pinned down by snapshotting its
 * id and comparing it later. This part is a genuine process-wide fact — Rack has exactly
 * one UI thread for the whole process — unlike worker-thread identity, which is now
 * owned per module instance (see ThreadVerifier). */
std::thread::id getUiThreadId();

/** Captures the calling thread's id as Rack's UI thread. Call once, from plugin
 * init() — Rack loads plugins on its main (UI) thread, in both the interactive and
 * headless (CLI) builds. */
void captureUiThreadId();

/** Set by Test::TestContext before any module code runs (see test_context.hpp). Not for
 * direct use outside the test harness — read by makeVerifier() to decide which lambdas
 * to hand out. */
extern bool verifyEnabled;

/**
 * Runtime thread verification for debug builds.
 *
 * Each module instance owns its own ThreadVerifier (typically as a member alongside its
 * GuiTaskProcessor), built via makeVerifier() with that instance's own worker-thread
 * check injected — no shared/global worker registry, so instances never need to
 * distinguish "my worker" from another module's.
 */
struct ThreadVerifier {
	/** True when the caller is on Rack's UI (main) thread. */
	std::function<bool()> isUiThread;
	/** True when the caller is on this instance's own worker thread. */
	std::function<bool()> isWorkerThread;
	/** True when the caller is on the UI thread or this instance's worker thread — the
	 * set of threads allowed to touch the patch (cables, widget tree). */
	std::function<bool()> isUiOrWorker;
	/** True when the caller is not this instance's worker thread. The only thread-identity
	 * statement engine-thread methods can rely on: engine thread ids are unknown, and
	 * headless Rack runs the engine on the UI thread, so "engine" cannot be asserted
	 * positively. */
	std::function<bool()> isEngine;
};

// Call sites use assert(verifier->isUiOrWorker()) / assert(verifier->isEngine()) directly
// (or assert(verifier.is...()) for a by-value instance) rather than a wrapping
// assertUiOrWorker()/assertEngineThread() method — assert(EXPR) is a macro that expands to
// nothing under NDEBUG, including EXPR itself, so writing the check inline at the call
// site guarantees it evaluates to zero cost in release builds. A wrapping method's body
// would still be a real (if empty) function call unless the compiler chooses to inline it.

/** Builds a ThreadVerifier for one module instance to own.
 *
 * isMyWorkerThread is supplied by the caller — typically GuiTaskProcessor::isWorkerThread
 * bound to that instance's own processor (see GuiTaskProcessor.hpp) — so each verifier
 * checks only its own module's worker, with no shared registry or mutex involved.
 *
 * In test binaries (Test::TestContext sets testingModeEnabled before any module code
 * runs) every check always passes, so the same asserts are inert without the test
 * harness having to know about any specific module's verifier or worker processor. */
ThreadVerifier makeVerifier(std::function<bool()> isMyWorkerThread);

} // namespace thread
} // namespace StoermelderPackOne
