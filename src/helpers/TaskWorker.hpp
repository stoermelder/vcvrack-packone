#pragma once
#include <functional>
#include <mutex>
#include <condition_variable>
#include <thread>
#include <pthread.h>

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
	std::string name;

	TaskWorker(std::string name = "") {
		workerContext = contextGet();
		worker = new std::thread(&TaskWorker::processWorker, this);
		this->name = name;
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