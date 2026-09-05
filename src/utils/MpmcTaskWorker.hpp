#pragma once
#include "TaskWorker.hpp"
#include <rigtorp/MPMCQueue.h>
#include <functional>
#include <memory>
#include <thread>
#include <atomic>
#include <pthread.h>
#if defined ARCH_MAC
	// POSIX unnamed semaphores (sem_init) are not implemented on macOS;
	// dispatch semaphores are the supported lightweight equivalent.
	#include <dispatch/dispatch.h>
#else
	#include <semaphore.h>
#endif

namespace StoermelderPackOne {

// Same API and semantics as TaskWorker, but the work queue is a
// rigtorp::MPMCQueue instead of a mutex-guarded dsp::RingBuffer. Unlike
// TaskWorker::workQueue (an SPSC ring buffer that requires external
// synchronization when pushed from multiple threads), MPMCQueue supports
// concurrent producers natively without a lock. work() uses try_push(), so a
// momentarily full queue (default capacity 32) makes the call a no-op
// (returns false) instead of blocking the caller or overwriting a pending
// task -- callers on a real-time thread must never spin-wait on a full
// queue.
//
// The worker parks on a counting semaphore, NOT a condition variable, and the
// choice is load-bearing. work() may run on the audio thread, so it must not
// take a mutex the worker could be holding -- but a condvar notified without
// its mutex held can fire in the window between the waiter evaluating its
// predicate (under the lock) and actually blocking, and such a notify wakes
// nobody and is not remembered: the worker then sleeps indefinitely with a
// task queued. That lost wakeup was observed in practice (worker parked in
// wait() while producers spun on a queued sentinel). A semaphore has no such
// window because signals are counted, not edge-triggered: a post that
// arrives before the worker blocks is simply consumed by the next wait().
//
// The semaphore alone is NOT enough, though, because MPMCQueue::try_pop()
// can fail transiently on a NON-empty queue: producers reserve slots before
// filling them, and try_pop only inspects the head slot, so it returns
// false while the head slot's producer is mid-commit even if later slots
// are complete. Treating one wait() as one pop therefore strands work: a
// wake whose pop hits a mid-commit slot consumes a signal without removing
// an item, and once producers go quiet that item never runs (observed as a
// deterministic wedge under N-thread contention). pendingTasks is the
// ground truth that closes this: it counts committed-but-unpopped items,
// and the worker drains BY COUNTER on every wake — a failed try_pop with
// pendingTasks > 0 means "head slot mid-commit, retry", never "sleep".
// drain() keeps the counter honest; leftover posts after a drain() just
// cause a wake that finds the counter at zero and goes back to sleep.
struct MpmcTaskWorker : ITaskWorker {
	// Minimal counting semaphore (C++14 -- std::counting_semaphore is C++20).
	// post() is wait-free apart from the kernel wake and safe from a real-time
	// thread; wait() blocks indefinitely with zero idle wakeups.
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

	TaskSignal taskSignal;
	// Committed-but-unpopped items in workQueue. Incremented by work() after a
	// successful push (before the post), decremented after every successful
	// pop (worker and drain()). May be read transiently as one less than the
	// true value (a pop can land between a producer's push and its increment);
	// that only delays the worker into its next wake, never strands an item,
	// because the producer's post is still to come.
	std::atomic<long> pendingTasks{0};
	std::thread* worker;
	Context* workerContext;
	std::atomic<bool> workerIsRunning{true};
	std::string name;

	// Set just before the worker thread is torn down. Long-running tasks
	// should poll this and return early once it becomes true.
	std::atomic<bool> cancel{false};

	struct WorkItem {
		std::shared_ptr<std::function<void(std::atomic<bool>&)>> task;
		Context* context;
	};
	rigtorp::MPMCQueue<WorkItem> workQueue;

	MpmcTaskWorker(std::string name = "", size_t capacity = 32)
		: workQueue(capacity) {
		workerContext = contextGet();
		this->name = std::move(name);
		worker = new std::thread(&MpmcTaskWorker::processWorker, this);
	}

	MpmcTaskWorker(const MpmcTaskWorker&) = delete;
	MpmcTaskWorker& operator=(const MpmcTaskWorker&) = delete;

	~MpmcTaskWorker() {
		cancel.store(true, std::memory_order_relaxed);
		workerIsRunning.store(false, std::memory_order_release);
		// The shutdown post is one extra signal with no matching queue item:
		// the worker drains whatever is still queued (each of those items has
		// its own post), then this one wakes it to an empty queue and it
		// observes workerIsRunning == false and exits.
		taskSignal.post();
		worker->join();
		workerContext = NULL;
		delete worker;
	}

	void processWorker() {
#if defined ARCH_LIN
		if (name != "") {
			pthread_setname_np(pthread_self(), name.c_str());
		}
#elif defined ARCH_MAC
	// Not supported (yet) on Mac
#elif defined ARCH_WIN
		if (name != "") {
			pthread_setname_np(pthread_self(), name.c_str());
		}
#endif

		WorkItem item;
		while (true) {
			taskSignal.wait();
			// Drain everything the counter says is committed, regardless of
			// how many posts this wake represents. A failed try_pop with the
			// counter still positive is the head slot's producer mid-commit
			// (see the struct comment) -- yield and retry; sleeping here would
			// strand that item, since its signal may be the very one this
			// wake consumed. The retry window is the producer's push-commit,
			// i.e. nanoseconds unless the producer is descheduled.
			while (pendingTasks.load(std::memory_order_acquire) > 0) {
				if (workQueue.try_pop(item)) {
					pendingTasks.fetch_sub(1, std::memory_order_release);
					contextSet(item.context);
					(*item.task)(cancel);
				}
				else {
					std::this_thread::yield();
				}
			}
			// Leftover posts (multi-item wakes, drain()) land here with the
			// counter at zero and loop straight back into wait().
			if (!workerIsRunning.load(std::memory_order_acquire)) return;
		}
	}

	bool work(std::function<void()> task) override {
		return work(std::move(task), workerContext);
	}

	bool work(std::function<void()> task, Context* context) override {
		return work(std::bind([](std::function<void()>& task, std::atomic<bool>&) { task(); }, std::move(task), std::placeholders::_1), context);
	}

	// Cancel-aware variant: the task receives the worker's cancel flag and
	// should poll it periodically, returning early once it is set.
	// Returns false (task dropped) if the queue was full.
	bool work(std::function<void(std::atomic<bool>&)> task) override {
		return work(std::move(task), workerContext);
	}

	bool work(std::function<void(std::atomic<bool>&)> task, Context* context) override {
		bool ok = workQueue.try_push(WorkItem{std::make_shared<std::function<void(std::atomic<bool>&)>>(std::move(task)), context});
		if (ok) {
			// Lock-free by design: work() may be called from the audio thread,
			// which must never block on a mutex the worker could be holding.
			// Counter before post: the post is the wake-up, the counter is what
			// the woken worker trusts, so it must already include this item.
			// The post itself is counted, so it cannot be lost even if it lands
			// while the worker is between deciding to sleep and actually
			// sleeping -- see the semaphore rationale on the struct comment.
			pendingTasks.fetch_add(1, std::memory_order_release);
			taskSignal.post();
		}
		return ok;
	}

	bool isWorkerThread() const override {
		return worker && std::this_thread::get_id() == worker->get_id();
	}

	void drain() {
		WorkItem item;
		while (workQueue.try_pop(item)) {
			pendingTasks.fetch_sub(1, std::memory_order_release);
		}
	}
}; // struct MpmcTaskWorker

} // namespace StoermelderPackOne
