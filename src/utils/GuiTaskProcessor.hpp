#pragma once
#include "TaskProcessor.hpp"
#include "TaskWorker.hpp"
#include "../vcv/ui.hpp"
#include <context.hpp>
#include <functional>
#include <thread>
#include <atomic>

namespace StoermelderPackOne {

// GUI-thread task queue that keeps working when Rack is not stepping widgets.
//
// Tasks queued here mutate the patch (cables, module state) and must run off the engine
// thread — so they're drained either by the UI thread (via the widget's step()) or by a
// private worker thread, never inline on the engine thread that queued them.
//
// SINGLE PRODUCER: the backing dsp::RingBuffer supports exactly one producer. Only the
// engine thread may call enqueue() — two threads pushing concurrently would interleave
// std::function assignments (lost tasks at best, UB at worst). Code already on the GUI
// thread should call its target directly instead. (The consumer side, in contrast, is
// safe for two threads: step() and the worker are mutually excluded by `draining` below.)
//
// Rack doesn't always step widgets (plugin build with the editor closed, or headless
// Rack), and that can change repeatedly during a module's lifetime, so process() (once
// per engine-thread divider tick) checks vcv::ui::hasWindow() every call and starts a
// worker the instant it's false — the same signal EightFace uses for "nobody will call
// step()" (EightFaceMk2.cpp's settings::isPlugin && !APP->window). Routed through the
// swappable UiAccess seam rather than APP->window directly so tests can exercise the
// "window present" path, which APP->window can't fake headless. Once started, the worker
// parks on a semaphore (TaskSignal) between drains rather than polling, so it's idle at
// zero cost and wakes exactly when needed. No mutex or condvar anywhere in this class.
//
//   window present -> tasks drain from step(), worker stays parked/absent
//   window absent  -> a worker thread is started on first need and drains the queue
template <size_t SIZE = 8>
struct GuiTaskProcessor {
	// TaskSignal (the counting semaphore) lives in TaskWorker.hpp and is shared with
	// TaskWorker/MpmcTaskWorker, which use the identical mechanism.
	TaskProcessor<SIZE> internalQueue;

	// CAS-guarded drain: whichever of step()/the worker gets here first drains; the loser
	// just skips this round, since the skipped tasks get drained next call anyway. This
	// is what makes the exclusion lock-free instead of needing a mutex.
	std::atomic_flag draining = ATOMIC_FLAG_INIT;

	// Worker lifecycle, advanced only via compare_exchange so startWorker() (engine
	// thread) and stopWorker() (whichever thread destroys the module) never both believe
	// they own a transition.
	//
	// Retiring = "asked to exit, not yet joined", reached from Running when a window
	// reappears (retireWorker()) — lets the engine thread retire a worker without
	// blocking on join(); the join is deferred to whichever of startWorker()/stopWorker()
	// next runs on a thread allowed to block.
	enum class WorkerState : int { Absent, Starting, Running, Retiring, Stopping };
	std::atomic<WorkerState> workerState{WorkerState::Absent};
	std::atomic<bool> workerShouldRun{false};
	TaskSignal taskSignal;
	std::thread* worker = nullptr;  // only touched by whichever thread completes Starting/Stopping
	Context* workerContext = nullptr;

	// The worker thread's id, published in startWorker() (before runWorker() itself
	// starts) and cleared in reapWorker(). Atomic because isWorkerThread() reads it from
	// arbitrary threads. Backs this instance's own ThreadVerifier (thread.hpp) — no
	// cross-module registry, so each GuiTaskProcessor answers only for its own worker.
	std::atomic<std::thread::id> workerThreadId{std::thread::id()};

	// Optional hook run on the worker thread after each drain, for state step() would
	// normally maintain — while the worker is draining, no step() runs, so anything it
	// refreshes would otherwise go stale (e.g. MIDI feedback comparing a resolved LED
	// state id). Not called on the step() drain path, which already does that work.
	//
	// Runs only when the worker wakes for a queued task, not on a timer: state invalidated
	// by something that queued no task (a cable the user pulled) waits for the next task
	// or an explicit wake() — deliberate, so an idle worker stays genuinely idle.
	//
	// Must be installed before the first process() call and never mutated after: the
	// worker reads it without synchronization.
	std::function<void()> onWorkerDrained;

	// Set once stopWorker() starts tearing the worker down. Guards the final
	// drainWithCallback() in runWorker(): once shutdown has started, the owner may be
	// mid-destruction (stopWorker() is normally called from its own destructor), so
	// neither a queued task nor onWorkerDrained may run anymore. Plain bool, not atomic:
	// only written by stopWorker() before the shutdown post, so the worker's read after
	// waking is already ordered by that release/acquire pair.
	bool shuttingDown = false;

	// Test-only: when true, process() never starts a worker — tasks still queue via
	// enqueue() but only run when something explicitly calls step()/drain(). Keeps tests
	// that assert on the queue (enqueue, then drain and check the effect) meaningful
	// while removing what made them racy: a real background thread sharing static module
	// state with the test harness's single logical thread (see getInstances()/
	// crossPending() in SpliceKit.cpp). Set right after construction, before the first
	// process() call — not meant to be flipped mid-flight.
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

	// Producer thread ONLY (see SINGLE PRODUCER above); never runs the task inline.
	// Returns whether t was actually queued: false when the ring buffer is full and the
	// task is dropped. Posts to the worker's semaphore whenever a worker is (or might be
	// about to be) running, so a task enqueued as the worker parks isn't stranded until
	// the next unrelated wake. Skipped only when Absent (no worker to wake; step() drains
	// instead) or on a dropped enqueue (nothing new to consume). Posting while Retiring is
	// harmless either way: that state only exists because a window came back, so step()
	// is already draining again.
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
	// the worker the instant no window is present; idempotent and cheap every tick.
	//
	// When a window comes back, the worker is asked to retire rather than joined here:
	// join() would block the engine thread on any task in flight, which this class exists
	// to avoid. The worker observes the request on its next wake, drains, and exits on
	// its own; the thread object is reaped by the next startWorker() or by stopWorker() at
	// destruction. Until it exits it stays a legal second drainer, since drain() tolerates
	// two of them.
	void process() {
		if (syncMode) return;
		if (!vcv::ui::hasWindow()) {
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

	// The worker's drain: queue, then the post-drain hook. Paired here so the hook can't
	// be forgotten at a new call site — it must run on every worker drain, including the
	// final shutdown one, which may apply the last queued change. Runs after drain() has
	// released `draining`, so it may call drain() again without deadlocking. Must NOT
	// enqueue(): the worker isn't the producer thread, and a second producer corrupts the
	// queue.
	void drainWithCallback() {
		drain();
		if (onWorkerDrained) onWorkerDrained();
	}

	// Engine thread — starts the worker on first need. Idempotent: the CAS on
	// workerState ensures only one caller ever wins Absent -> Starting, so thread
	// construction never races a concurrent start or stop.
	void startWorker() {
		WorkerState expected = WorkerState::Absent;
		if (!workerState.compare_exchange_strong(expected, WorkerState::Starting, std::memory_order_acq_rel)) {
			// A worker retired earlier (window came back) but never joined can be
			// reclaimed here, since the window has evidently gone away again. Claim
			// Retiring -> Starting and join the old thread first; it was already told to
			// exit, so this join is bounded by whatever task it had in flight, not new work.
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
		// Publish the id before runWorker() necessarily starts running, so
		// isWorkerThread() is correct for any assertion the instant it begins.
		workerThreadId.store(worker->get_id(), std::memory_order_release);
		workerState.store(WorkerState::Running, std::memory_order_release);
		// Tasks enqueued before any worker existed to post for are still sitting
		// there — make sure the new worker's first wait() doesn't block forever.
		taskSignal.post();
	}

	// Joins and deletes the worker thread object. Caller must own the lifecycle
	// transition (won the CAS into Starting or Stopping), so no other thread can be
	// touching `worker` concurrently. Blocks — never call from the engine thread while a
	// task could be in flight.
	void reapWorker() {
		if (!worker) return;
		worker->join();
		delete worker;
		worker = nullptr;
		workerThreadId.store(std::thread::id(), std::memory_order_release);
	}

	// Whichever thread destroys the module. Waits out an in-flight start (Starting),
	// then claims Stopping from Running or Retiring and joins — a retiring worker still
	// needs joining here, since retireWorker() deliberately left that to us. Nothing to
	// do if no worker was ever needed (Absent).
	void stopWorker() {
		WorkerState state = workerState.load(std::memory_order_acquire);
		while (true) {
			while (state == WorkerState::Starting) {
				// startWorker() is mid-construction; wait for it to publish Running
				// before claiming Stopping. It always advances out of Starting (even on
				// thread-creation failure), so this can't spin forever.
				std::this_thread::yield();
				state = workerState.load(std::memory_order_acquire);
			}
			if (state != WorkerState::Running && state != WorkerState::Retiring) return;
			if (workerState.compare_exchange_strong(state, WorkerState::Stopping, std::memory_order_acq_rel)) break;
			// Lost the race — `state` now holds what we actually observed; re-evaluate.
		}

		workerShouldRun.store(false, std::memory_order_release);
		// The owner may be mid-destruction once shutdown starts, so neither a queued
		// task nor onWorkerDrained may run anymore — drop whatever is left instead of
		// letting the worker's final drain execute it. drain() happens-before the post
		// below, so runWorker() is guaranteed to see the empty queue and
		// shuttingDown == true once it wakes for this post.
		shuttingDown = true;
		internalQueue.drain();
		// Extra signal with no matching task, same as MpmcTaskWorker's shutdown post:
		// wakes the worker to observe workerShouldRun == false and exit instead of
		// parking forever. Harmless if a retiring worker already exited.
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
				// Queue already discarded; skip onWorkerDrained (owner may be
				// half-destroyed). drain() just keeps `draining` consistent.
				drain();
				return;
			}
			drainWithCallback();
			if (!workerShouldRun.load(std::memory_order_acquire)) {
				// Final drain in case a task raced in concurrently with shutdown.
				drainWithCallback();
				return;
			}
		}
	}
}; // struct GuiTaskProcessor

} // namespace StoermelderPackOne
