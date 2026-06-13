#pragma once
#include <functional>
#include <mutex>
#include <condition_variable>
#include <thread>
#include <atomic>
#include <pthread.h>

namespace StoermelderPackOne {

struct TaskWorker {
	std::mutex workerMutex;
	std::condition_variable workerCondVar;
	std::thread* worker;
	Context* workerContext;
	bool workerIsRunning = true;
	bool workerDoProcess = false;
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

	~TaskWorker() {
		cancel.store(true, std::memory_order_relaxed);
		{
			std::unique_lock<std::mutex> lock(workerMutex);
			workerIsRunning = false;
			workerDoProcess = true;
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

		while (true) {
			std::unique_lock<std::mutex> lock(workerMutex);
			workerCondVar.wait(lock, std::bind(&TaskWorker::workerDoProcess, this));
			if (!workerIsRunning) return;
			while (!workQueue.empty()) {
				auto item = workQueue.shift();
				contextSet(item.context);
				(*item.task)(cancel);
			}
			workerDoProcess = false;
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
		workerDoProcess = true;
		workerCondVar.notify_one();
	}
}; // struct TaskWorker



struct ITaskWorker {
	virtual ~ITaskWorker() = default;
	virtual void work(std::function<void()> task) = 0;
	virtual void work(std::function<void()> task, Context* context) = 0;
};

// Runs tasks synchronously on the calling thread — no background thread.
// Used in tests to make engine.process() calls deterministic.
struct SyncTaskWorker : ITaskWorker {
	void work(std::function<void()> task) override { task(); }
	void work(std::function<void()> task, Context* context) override {
		Context* prev = contextGet();
		contextSet(context);
		task();
		contextSet(prev);
	}
}; // struct SyncTaskWorker


// Adapts a TaskWorker to the ITaskWorker interface without modifying TaskWorker.
struct TaskWorkerAdapter : ITaskWorker {
	std::shared_ptr<TaskWorker> inner;
	explicit TaskWorkerAdapter(std::shared_ptr<TaskWorker> tw) : inner(std::move(tw)) {}
	void work(std::function<void()> task) override { inner->work(std::move(task)); }
	void work(std::function<void()> task, Context* context) override { inner->work(std::move(task), context); }
}; // struct TaskWorkerAdapter



} // namespace StoermelderPackOne