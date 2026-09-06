#pragma once
#include <functional>
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

// Minimal counting semaphore (C++14 -- std::counting_semaphore is C++20).
// Unlike a condvar notified without its mutex held, a post can't land in a
// window where the waiter is neither checking its predicate nor yet blocked
// and get lost -- signals are counted, not edge-triggered.
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

struct TaskWorker {
	// Single-producer, single-consumer only (dsp::RingBuffer supports no
	// more). For multiple producer threads, use MpmcTaskWorker instead.
	TaskSignal taskSignal;
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
	dsp::RingBuffer<WorkItem, 32> workQueue;

	TaskWorker(std::string name = "") {
		workerContext = contextGet();
		worker = new std::thread(&TaskWorker::processWorker, this);
		this->name = std::move(name);
	}

	TaskWorker(const TaskWorker&) = delete;
	TaskWorker& operator=(const TaskWorker&) = delete;

	~TaskWorker() {
		cancel.store(true, std::memory_order_relaxed);
		workerIsRunning.store(false, std::memory_order_release);
		// Extra wakeup with no matching item: drains whatever's still queued,
		// then finds the queue empty and workerIsRunning false, and exits.
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

		while (true) {
			taskSignal.wait();
			// True SPSC, so empty() is never transiently wrong (unlike
			// MpmcTaskWorker's MPMCQueue) -- no pendingTasks counter needed.
			while (!workQueue.empty()) {
				auto item = workQueue.shift();
				contextSet(item.context);
				(*item.task)(cancel);
			}
			if (!workerIsRunning.load(std::memory_order_acquire)) return;
		}
	}

	void work(std::function<void()> task) {
		work(std::move(task), workerContext);
	}

	void work(std::function<void()> task, Context* context) {
		work(std::bind([](std::function<void()>& task, std::atomic<bool>&) { task(); }, std::move(task), std::placeholders::_1), context);
	}

	// Cancel-aware variant: the task receives the worker's cancel flag and
	// should poll it periodically, returning early once it is set.
	void work(std::function<void(std::atomic<bool>&)> task) {
		work(std::move(task), workerContext);
	}

	void work(std::function<void(std::atomic<bool>&)> task, Context* context) {
		workQueue.push(WorkItem{std::make_shared<std::function<void(std::atomic<bool>&)>>(std::move(task)), context});
		taskSignal.post();
	}
}; // struct TaskWorker



struct ITaskWorker {
	virtual ~ITaskWorker() = default;
	// Returns false if the task was dropped instead of queued (e.g. a
	// fixed-capacity worker that was full). Implementations backed by an
	// unbounded/overwriting queue (TaskWorker) always return true.
	virtual bool work(std::function<void()> task) = 0;
	virtual bool work(std::function<void()> task, Context* context) = 0;

	// Cancel-aware: the task receives the worker's cancel flag and should
	// poll it periodically, returning early once it becomes true.
	virtual bool work(std::function<void(std::atomic<bool>&)> task) = 0;
	virtual bool work(std::function<void(std::atomic<bool>&)> task, Context* context) = 0;

	// True when the calling thread is the one this worker runs tasks on, so
	// callers can assert that state only touched from inside work() really is
	// being touched from there.
	virtual bool isWorkerThread() const = 0;
};

// Runs tasks synchronously on the calling thread — no background thread.
// Used in tests to make engine.process() calls deterministic.
struct SyncTaskWorker : ITaskWorker {
	// Never set. A member (not a per-call local) so a task that stashes the
	// reference (as the cancel-aware overload's contract allows) does not
	// end up holding a dangling reference once work() returns.
	std::atomic<bool> cancel{false};

	bool work(std::function<void()> task) override { task(); return true; }
	bool work(std::function<void()> task, Context* context) override {
		Context* prev = contextGet();
		contextSet(context);
		task();
		contextSet(prev);
		return true;
	}
	bool work(std::function<void(std::atomic<bool>&)> task) override {
		task(cancel);
		return true;
	}
	bool work(std::function<void(std::atomic<bool>&)> task, Context* context) override {
		Context* prev = contextGet();
		contextSet(context);
		task(cancel);
		contextSet(prev);
		return true;
	}
	// Every thread is "the worker thread": tasks run inline on the caller.
	bool isWorkerThread() const override { return true; }
}; // struct SyncTaskWorker



} // namespace StoermelderPackOne