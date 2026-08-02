#pragma once
#include "TaskWorker.hpp"
#include <rigtorp/MPMCQueue.h>
#include <functional>
#include <memory>
#include <mutex>
#include <condition_variable>
#include <thread>
#include <atomic>
#include <pthread.h>

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
struct MpmcTaskWorker : ITaskWorker {
	std::mutex workerMutex;
	std::condition_variable workerCondVar;
	std::thread* worker;
	Context* workerContext;
	std::atomic<bool> workerIsRunning{true};
	// Set by work() before notify_one(), cleared by the worker once it has
	// observed it. Closes the lost-wakeup window between try_pop() finding
	// the queue empty and the worker actually blocking on the condvar: the
	// wait predicate below doesn't consult workQueue directly (MPMCQueue has
	// no non-destructive empty() check), so without this flag a push+notify
	// landing in that window would be silently swallowed.
	std::atomic<bool> workerPending{false};
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

	~MpmcTaskWorker() {
		cancel.store(true, std::memory_order_relaxed);
		{
			std::unique_lock<std::mutex> lock(workerMutex);
			workerIsRunning.store(false, std::memory_order_relaxed);
		}
		workerCondVar.notify_one();
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
			if (workQueue.try_pop(item)) {
				contextSet(item.context);
				(*item.task)(cancel);
				continue;
			}
			std::unique_lock<std::mutex> lock(workerMutex);
			if (!workerIsRunning.load(std::memory_order_relaxed)) return;
			workerCondVar.wait(lock, [this]() {
				return !workerIsRunning.load(std::memory_order_relaxed) || workerPending.load(std::memory_order_relaxed);
			});
			workerPending.store(false, std::memory_order_relaxed);
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
	bool work(std::function<void(std::atomic<bool>&)> task) {
		return work(std::move(task), workerContext);
	}

	bool work(std::function<void(std::atomic<bool>&)> task, Context* context) {
		bool ok = workQueue.try_push(WorkItem{std::make_shared<std::function<void(std::atomic<bool>&)>>(std::move(task)), context});
		if (ok) {
			workerPending.store(true, std::memory_order_relaxed);
			workerCondVar.notify_one();
		}
		return ok;
	}

	void drain() {
		WorkItem item;
		while (workQueue.try_pop(item)) { }
	}
}; // struct MpmcTaskWorker

} // namespace StoermelderPackOne
