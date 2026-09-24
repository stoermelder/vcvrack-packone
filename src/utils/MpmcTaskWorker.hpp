#pragma once
#include "TaskWorker.hpp"
#include <rigtorp/MPMCQueue.h>
#include <functional>
#include <memory>
#include <thread>
#include <atomic>
#include <pthread.h>

namespace StoermelderPackOne {

// Same API and semantics as TaskWorker, but the queue is a rigtorp::MPMCQueue,
// which (unlike TaskWorker's SPSC dsp::RingBuffer) supports concurrent
// producers natively, lock-free. work() uses try_push(), so a momentarily
// full queue (default capacity 32) drops the task (returns false) instead of
// blocking or overwriting -- callers on a real-time thread must never
// spin-wait on a full queue.
//
// Parks on a semaphore (TaskSignal, TaskWorker.hpp), not a condvar, to avoid
// the lost-wakeup a condvar notified outside its mutex can suffer -- see
// TaskSignal's comment.
//
// The semaphore alone isn't enough: MPMCQueue::try_pop() can fail transiently
// on a non-empty queue, since it only inspects the head slot and that
// producer may be mid-commit even though later slots are done. Treating one
// wait() as one pop would strand such an item once producers go quiet.
// pendingTasks is the fix: it counts committed-but-unpopped items, and the
// worker drains BY COUNTER on every wake -- a failed try_pop with
// pendingTasks > 0 means "mid-commit, retry", never "sleep". drain() keeps
// the counter honest; leftover posts after it just find zero and re-sleep.
struct MpmcTaskWorker : ITaskWorker {
	TaskSignal taskSignal;
	// Committed-but-unpopped items in workQueue. Incremented by work() after a
	// successful push (before the post), decremented after every successful
	// pop. May be read transiently as one less than true (a pop can land
	// between a push and its increment) -- harmless, since the producer's
	// post is still to come.
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
		// Extra wakeup with no matching item: drains whatever's still queued,
		// then finds the counter at zero and workerIsRunning false, and exits.
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
			// Drain by counter, not by pop count: a failed try_pop while the
			// counter is still positive means the head producer is
			// mid-commit (see struct comment) -- yield and retry, don't
			// sleep, or this wake's item is stranded.
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
			// Counter before post: the woken worker must see this item
			// already counted.
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
