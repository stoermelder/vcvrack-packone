#pragma once
#include "../plugin.hpp"
#include <functional>
#include <mutex>
#include <condition_variable>

namespace StoermelderPackOne {

struct TaskWorker {
	std::mutex workerMutex;
	std::condition_variable workerCondVar;
	std::thread* worker;
	Context* workerContext;
	bool workerIsRunning = true;
	bool workerDoProcess = false;
	int workerPreset = -1;
	std::function<void()> workerTask;

	TaskWorker() {
		workerContext = contextGet();
		worker = new std::thread(&TaskWorker::processWorker, this);
	}

	~TaskWorker() {
		workerIsRunning = false;
		workerDoProcess = true;
		workerCondVar.notify_one();
		worker->join();
		workerContext = NULL;
		delete worker;
	}

	void processWorker() {
		contextSet(workerContext);
		while (true) {
			std::unique_lock<std::mutex> lock(workerMutex);
			workerCondVar.wait(lock, std::bind(&TaskWorker::workerDoProcess, this));
			if (!workerIsRunning) return;
			workerTask();
			workerDoProcess = false;
		}
	}

	void work(std::function<void()> task) {
		workerTask = task;
		workerDoProcess = true;
		workerCondVar.notify_one();
	}
}; // struct TaskWorker

} // namespace StoermelderPackOne