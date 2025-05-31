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
	std::string name;

	dsp::RingBuffer<std::tuple<std::function<void()>, Context*>, 32> workQueue;

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
				auto t = workQueue.shift();
				std::function<void()> task = std::get<0>(t);
				Context* context = std::get<1>(t);
				contextSet(context);
				task();
			}
			workerDoProcess = false;
		}
	}

	void work(std::function<void()> task) {
		work(task, workerContext);
	}

	void work(std::function<void()> task, Context* context) {
		workQueue.push(std::make_tuple(task, context));
		workerDoProcess = true;
		workerCondVar.notify_one();
	}
}; // struct TaskWorker

} // namespace StoermelderPackOne