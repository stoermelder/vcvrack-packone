#pragma once
#include "TaskProcessor.hpp"
#include <context.hpp>
#include <functional>
#include <thread>
#include <atomic>
#if defined ARCH_MAC
	// POSIX unnamed semaphores (sem_init) are not implemented on macOS;
	// dispatch semaphores are the supported lightweight equivalent.
	#include <dispatch/dispatch.h>
#else
	#include <semaphore.h>
#endif

namespace StoermelderPackOne {

// GUI-thread task queue that keeps working when Rack is not stepping widgets.
//
// Tasks queued here mutate the patch (cables, module state) and must run on a thread
// that does NOT hold the engine lock — so they can be drained either by the UI thread
// (the normal case, via the widget's step()) or by a private worker thread, but never
// inline on the engine thread that queued them.
//
// SINGLE PRODUCER. The backing store is a dsp::RingBuffer, which supports exactly one
// producer and one consumer. Only ONE thread may ever call enqueue() on a given
// instance — in practice the engine thread, since that is the thread that cannot run
// these tasks itself. Two threads pushing concurrently compute the same slot index and
// interleave their std::function assignments: lost tasks at best, UB at worst. Code
// that is already on the GUI thread must call its target function directly instead of
// enqueueing — that is both correct and a frame faster. (The consumer side, by
// contrast, IS safe for two threads: step() and the worker are mutually excluded by the
// `draining` flag below.)
//
// Rack does not always step widgets: in the plugin build the editor window can be
// closed, and headless/CLI Rack has no window at all. Both conditions can appear and
// disappear repeatedly during a module's lifetime, so absence is checked on every call
// rather than once at construction: process() (called once per engine-thread divider
// tick) takes the current window pointer — APP->window in production, defaulted so
// call sites don't need to pass it — and starts a worker the instant it is null. This
// is the same signal EightFace uses (see EightFaceMk2.cpp's settings::isPlugin &&
// !APP->window branch) for "nobody is going to call step() on this widget", just
// without the settings::isPlugin gate, since the caller decides what counts as absent
// by passing whatever pointer is appropriate — a real GuiTaskProcessor::process() call
// site should pass APP->window; unit tests can pass a dummy non-null pointer to
// exercise the "UI present" path without a real rack::window::Window. Once started,
// the worker is not repeatedly created and destroyed — it parks on a counting
// semaphore (see TaskSignal, same mechanism and lost-wakeup rationale as
// MpmcTaskWorker) between drains instead of polling on a timer, so it does zero work
// while idle and wakes exactly when there is something to do. Every synchronization
// point here is lock-free apart from the kernel-level semaphore wait/post: no mutex,
// no condition variable.
//
//   window present -> tasks drain from step(), worker stays parked/absent
//   window absent  -> a worker thread is started on first need and drains the queue
template <size_t SIZE = 8>
struct GuiTaskProcessor {
	// Minimal counting semaphore (C++14 — std::counting_semaphore is C++20). Signals are
	// counted rather than edge-triggered, so a post() that lands in the window between
	// the worker deciding to sleep and actually sleeping is not lost — the next wait()
	// consumes it immediately instead of blocking. A condition variable notified without
	// its mutex held has exactly that lost-wakeup window; see MpmcTaskWorker's TaskSignal
	// for the fuller rationale (that gap was observed in practice there).
	struct TaskSignal {
#if defined ARCH_MAC
		dispatch_semaphore_t sem;
		TaskSignal() { sem = dispatch_semaphore_create(0); }
		~TaskSignal() { dispatch_release(sem); }
		void post() { dispatch_semaphore_signal(sem); }
		void wait() { dispatch_semaphore_wait(sem, DISPATCH_TIME_FOREVER); }
#else
		sem_t sem;
		TaskSignal() { sem_init(&sem, 0, 0); }
		~TaskSignal() { sem_destroy(&sem); }
		void post() { sem_post(&sem); }
		// sem_wait can be interrupted by a signal (EINTR); retry.
		void wait() { while (sem_wait(&sem) != 0) {} }
#endif
	};

	TaskProcessor<SIZE> internalQueue;

	// CAS-guarded drain: whichever of process()/the worker gets here first drains: the
	// loser skips this round rather than blocking, which is safe because the skipped
	// tasks are simply drained on the next call (process() every UI frame, the worker
	// on its next wake) — there is no correctness requirement to drain on every call,
	// only that draining keeps happening. This is what makes the exclusion lock-free
	// instead of needing a mutex.
	std::atomic_flag draining = ATOMIC_FLAG_INIT;

	// Worker lifecycle state, advanced only via compare_exchange so a concurrent
	// startWorker() (engine thread, from process()) and stopWorker() (whichever
	// thread destroys the module) can never both believe they own the transition.
	//
	// Retiring means "asked to exit, not yet joined" — reached from Running when a window
	// reappears (see retireWorker()). It exists so the engine thread can retire a worker
	// without join()ing it; the join is deferred to whichever of startWorker() or
	// stopWorker() next runs on a thread that is allowed to block.
	enum class WorkerState : int { Absent, Starting, Running, Retiring, Stopping };
	std::atomic<WorkerState> workerState{WorkerState::Absent};
	std::atomic<bool> workerShouldRun{false};
	TaskSignal taskSignal;
	std::thread* worker = nullptr;  // only touched by whichever thread completes Starting/Stopping
	Context* workerContext = nullptr;

	// The worker thread's id, published the moment the thread object is constructed in
	// startWorker() (before runWorker() itself starts executing) and cleared in
	// reapWorker(). Atomic because it is read from arbitrary threads via isWorkerThread()
	// at thread-assertion sites. Backs this instance's own ThreadVerifier (see thread.hpp)
	// — there is no cross-module registry, so each GuiTaskProcessor answers only for its
	// own worker.
	std::atomic<std::thread::id> workerThreadId{std::thread::id()};

	// Optional hook run on the worker thread after each drain. Exists for state that the
	// widget's step() would normally maintain: while a worker is doing the draining there
	// is by definition no step() running, so anything step() refreshes would otherwise go
	// stale — and callers that edge-trigger off such state (e.g. MIDI feedback comparing a
	// resolved LED state id) would silently stop firing. Not called on the step() drain
	// path, where the widget already does that work.
	//
	// Runs when the worker wakes, i.e. after tasks queued through enqueue() — the changes
	// this class exists to carry. It is not a timer: state invalidated by something that
	// queued no task here (a cable the user pulled) is not picked up until the next task,
	// or until someone calls wake(). Keeping it task-driven is deliberate, so an idle
	// worker stays genuinely idle.
	//
	// Must be installed before the worker can start (i.e. before the first process() call)
	// and not mutated afterwards: the worker reads it without synchronization.
	std::function<void()> onWorkerDrained;

	// Set once stopWorker() begins tearing the worker down. Guards the final
	// drainWithCallback() in runWorker() (see its comment): once shutdown has started, the
	// owning object may itself be mid-destruction (this is normally called from the owner's
	// own destructor), so neither a queued task nor onWorkerDrained — both of which
	// typically capture/touch the owner — may run anymore. Plain bool, not atomic: only
	// ever written by stopWorker() before the shutdown taskSignal.post() that the worker
	// thread's wait() synchronizes with, so the worker's read of it after waking is
	// already ordered by that release/acquire pair.
	bool shuttingDown = false;

	// Test-only escape hatch: when true, process() never starts a worker — tasks still
	// queue normally on enqueue() and only ever run when something explicitly calls
	// step()/drain(), same as the real "window present" path. This keeps tests that
	// assert on the queue itself (a task is pending, then drain it and check the effect)
	// meaningful, while removing the one thing that made them racy: a real background
	// thread the test harness's single logical thread was never designed to share static
	// module state with (see the class comment's "SINGLE PRODUCER" note, and
	// getInstances()/crossPending() in SpliceKit.cpp for what that state looks like).
	// Must be set right after construction, before the first process() call — not meant
	// to be flipped mid-flight.
	bool syncMode = false;

	~GuiTaskProcessor() {
		stopWorker();
	}

	// True when called from this processor's own worker thread. A default-constructed
	// std::thread::id (the "no worker" state) never equals a real thread's id, so this is
	// false whenever no worker is running.
	bool isWorkerThread() const {
		return std::this_thread::get_id() == workerThreadId.load(std::memory_order_acquire);
	}

	// Producer thread ONLY — see the SINGLE PRODUCER note on the class comment; calling
	// this from a second thread corrupts the queue. Never runs the task inline — see the
	// class comment for why. Returns whether t was actually queued: false when the ring
	// buffer is full and the task is dropped (as before), so a caller that must not lose
	// a task can detect the drop and retry later. Posts to the worker's semaphore
	// whenever a worker is (or might be about to be) running, so a task enqueued right as
	// the worker is parking is never stranded until the next unrelated wake — see
	// TaskSignal's counted-signal rationale. The post is skipped only in the Absent
	// state, where there is no worker to wake and step() is the drainer, and on a dropped
	// enqueue, where there is nothing new for the worker to consume. Posting while
	// Retiring is likewise fine whether or not the worker is still alive to consume it:
	// that state is only ever entered because a window came back, so step() is draining
	// again.
	bool enqueue(std::function<void()> t) {
		if (!internalQueue.enqueue(std::move(t))) return false;
		if (workerState.load(std::memory_order_acquire) != WorkerState::Absent) {
			taskSignal.post();
		}
		return true;
	}

	// GUI thread — drains pending tasks. Called from the widget's step().
	void step() {
		drain();
	}

	// Engine thread — called once per divider tick from the module's process(). Starts
	// the worker the instant no window is present; idempotent and cheap to call every
	// tick regardless of whether a worker is already running. `window` defaults to
	// APP->window so real call sites need not pass anything; tests pass a dummy non-null
	// pointer to exercise the window-present path without a real rack::window::Window.
	//
	// When a window comes back (the plugin editor being reopened), the worker is asked to
	// retire rather than being joined here: join() would block the engine thread until a
	// task in flight finished, which is exactly what this class exists to avoid. The
	// worker observes the request on its next wake, drains, and exits on its own; the
	// thread object it leaves behind is reaped by the next startWorker() or by
	// stopWorker() at destruction. Until it exits it stays a legal second drainer, since
	// drain() tolerates two of them.
	void process(rack::window::Window* window = APP->window) {
		if (syncMode) return;
		if (!window) {
			startWorker();
		}
		else {
			retireWorker();
		}
	}

	// Engine thread — asks a running worker to exit without blocking on it. The state
	// goes Running -> Retiring, which startWorker() knows how to reclaim and stopWorker()
	// knows how to finish joining.
	void retireWorker() {
		WorkerState expected = WorkerState::Running;
		if (!workerState.compare_exchange_strong(expected, WorkerState::Retiring, std::memory_order_acq_rel)) return;
		workerShouldRun.store(false, std::memory_order_release);
		taskSignal.post();
	}

	// Drains the queue unless another thread is already draining it — see the
	// `draining` comment above. Runs on whichever thread calls it (UI or worker).
	void drain() {
		if (draining.test_and_set(std::memory_order_acquire)) return;
		internalQueue.process();
		draining.clear(std::memory_order_release);
	}

	// The worker's drain: the queue, then the post-drain hook. Paired here rather than at
	// each call site so the hook cannot be forgotten on a new one — it must run on every
	// worker drain, including the final one during shutdown, since that may be the pass
	// that applies the last queued change. The hook runs after drain() has released
	// `draining`, so it may call drain() again without deadlocking. It must NOT enqueue():
	// the worker is not the producer thread, and a second producer corrupts the queue.
	void drainWithCallback() {
		drain();
		if (onWorkerDrained) onWorkerDrained();
	}

	// Engine thread — starts the worker on first need. Idempotent: the CAS on
	// workerState ensures only one caller ever wins the Absent -> Starting transition,
	// so construction of the thread itself never races a concurrent start or stop.
	void startWorker() {
		WorkerState expected = WorkerState::Absent;
		if (!workerState.compare_exchange_strong(expected, WorkerState::Starting, std::memory_order_acq_rel)) {
			// A worker that was retired (window came back) but never joined can be
			// reclaimed here — the window has evidently gone away again. Claim the
			// Retiring -> Starting transition and join the old thread before building a
			// new one; it was already told to exit, so this join is bounded by whatever
			// task it had in flight rather than waiting on new work.
			if (expected != WorkerState::Retiring) return;
			if (!workerState.compare_exchange_strong(expected, WorkerState::Starting, std::memory_order_acq_rel)) return;
			reapWorker();
		}

		workerContext = contextGet();
		workerShouldRun.store(true, std::memory_order_relaxed);
		try {
			worker = new std::thread(&GuiTaskProcessor::runWorker, this);
		}
		catch (const std::system_error&) {
			// Thread creation failed (resource exhaustion). Roll back to Absent rather
			// than leaving the state stuck at Starting: stopWorker() spins waiting for
			// Starting to advance, so a stuck state would hang the destructor forever.
			// A later process() tick simply retries the start.
			worker = nullptr;
			workerShouldRun.store(false, std::memory_order_relaxed);
			workerState.store(WorkerState::Absent, std::memory_order_release);
			return;
		}
		// The thread object exists now, so its id is stable — publish it before the
		// worker itself has necessarily started running, so isWorkerThread() is correct
		// for any assertion the instant runWorker() begins.
		workerThreadId.store(worker->get_id(), std::memory_order_release);
		workerState.store(WorkerState::Running, std::memory_order_release);
		// Tasks enqueued between the queue filling up and this moment (i.e. before any
		// worker existed to post for) are still sitting there — make sure the new
		// worker's first wait() doesn't block on them forever.
		taskSignal.post();
	}

	// Joins and deletes the worker thread object. Caller must own the lifecycle
	// transition (i.e. have won the CAS into Starting or Stopping), so no other thread
	// can be touching `worker` concurrently. Blocks — never call from the engine thread
	// while a task could be in flight.
	void reapWorker() {
		if (!worker) return;
		worker->join();
		delete worker;
		worker = nullptr;
		workerThreadId.store(std::thread::id(), std::memory_order_release);
	}

	// Whichever thread destroys the module. If a start is in flight (Starting) wait it
	// out, then claim Stopping from either Running or Retiring and join. A worker already
	// asked to retire still needs joining here — retireWorker() deliberately left that to
	// us. If no worker was ever needed (Absent), there is nothing to do.
	void stopWorker() {
		WorkerState state = workerState.load(std::memory_order_acquire);
		while (true) {
			while (state == WorkerState::Starting) {
				// startWorker() on the engine thread is mid-construction; wait for it to
				// publish Running before we can claim Stopping and join the real thread.
				// It always advances out of Starting, including on thread-creation
				// failure, so this cannot spin forever.
				std::this_thread::yield();
				state = workerState.load(std::memory_order_acquire);
			}
			if (state != WorkerState::Running && state != WorkerState::Retiring) return;
			if (workerState.compare_exchange_strong(state, WorkerState::Stopping, std::memory_order_acq_rel)) break;
			// Lost the race — `state` now holds what we actually observed; re-evaluate.
		}

		workerShouldRun.store(false, std::memory_order_release);
		// Once shutdown has started the owner may be mid-destruction: neither a task still
		// in the queue nor onWorkerDrained may run anymore (both typically capture/touch
		// the owner), so drop whatever is left rather than let the worker's final drain
		// execute it. drain() happens-before the post below, which is what the worker's
		// wait()/drainWithCallback() synchronizes on, so runWorker() is guaranteed to see
		// both the empty queue and shuttingDown == true once it wakes for this post.
		shuttingDown = true;
		internalQueue.drain();
		// Wakes the worker out of taskSignal.wait() the same way MpmcTaskWorker's
		// shutdown post does: an extra signal with no matching task, so the worker
		// drains anything left, observes workerShouldRun == false, and exits instead
		// of parking forever. (A retiring worker may already have exited; the extra
		// post is harmless, and reapWorker() handles the already-finished case.)
		taskSignal.post();
		reapWorker();
		workerState.store(WorkerState::Absent, std::memory_order_release);
		shuttingDown = false;
	}

	void runWorker() {
		contextSet(workerContext);
		while (true) {
			taskSignal.wait();
			if (shuttingDown) {
				// Shutdown discarded the queue and does not want onWorkerDrained run
				// against a possibly half-destroyed owner — just drain() (there is
				// nothing left to run, but this keeps `draining` consistent) and exit.
				drain();
				return;
			}
			drainWithCallback();
			if (!workerShouldRun.load(std::memory_order_acquire)) {
				// Final drain in case a task (and its post) arrived concurrently
				// with the shutdown request, after the check above would have missed it.
				drainWithCallback();
				return;
			}
		}
	}
}; // struct GuiTaskProcessor

} // namespace StoermelderPackOne
