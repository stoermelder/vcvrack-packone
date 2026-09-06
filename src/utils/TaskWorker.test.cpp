#include "../test/framework.hpp"
#include "TaskWorker.hpp"
#include <future>
#include <thread>
#include <chrono>
#include <mutex>

using namespace StoermelderPackOne;

// ---- helpers ----------------------------------------------------------------

// Synchronises on a shared_ptr<promise> so std::function (which requires
// CopyConstructible captures) can own the synchronisation primitive.
static std::future<void> makePromise(std::shared_ptr<std::promise<void>>& out) {
	out = std::make_shared<std::promise<void>>();
	return out->get_future();
}

// Callable whose copy constructor increments a counter, letting tests assert
// that the work() path never copies the stored task.
struct CopyCounter {
	std::atomic<int>* copies;
	explicit CopyCounter(std::atomic<int>* c) : copies(c) {}
	CopyCounter(const CopyCounter& o) : copies(o.copies) { copies->fetch_add(1); }
	CopyCounter(CopyCounter&& o) noexcept : copies(o.copies) {}
	CopyCounter& operator=(const CopyCounter&) = delete;
	CopyCounter& operator=(CopyCounter&&) = delete;
};

// ---- tests ------------------------------------------------------------------

TEST_CASE("task executes exactly once", "[TaskWorker]") {
	Test::TestContext<> ctx;
	TaskWorker worker;

	std::atomic<int> execCount{0};
	std::shared_ptr<std::promise<void>> prom;
	auto fut = makePromise(prom);

	worker.work([&execCount, prom]() {
		execCount.fetch_add(1);
		prom->set_value();
	});

	REQUIRE(fut.wait_for(std::chrono::seconds(5)) == std::future_status::ready);
	REQUIRE(execCount == 1);
}

TEST_CASE("task callable is not copied through the work() chain", "[TaskWorker]") {
	Test::TestContext<> ctx;
	TaskWorker worker;

	std::atomic<int> copyCount{0};
	std::shared_ptr<std::promise<void>> prom;
	auto fut = makePromise(prom);

	CopyCounter tracker{&copyCount};
	worker.work([t = std::move(tracker), prom]() {
		prom->set_value();
	});

	REQUIRE(fut.wait_for(std::chrono::seconds(5)) == std::future_status::ready);
	REQUIRE(copyCount == 0);
}

TEST_CASE("multiple tasks execute serially in submission order", "[TaskWorker]") {
	Test::TestContext<> ctx;
	TaskWorker worker;

	constexpr int N = 8;
	std::vector<int> order;
	std::mutex orderMutex;
	std::atomic<int> doneCount{0};
	std::shared_ptr<std::promise<void>> lastProm;
	auto fut = makePromise(lastProm);

	for (int i = 0; i < N; i++) {
		auto prom = (i == N - 1) ? lastProm : std::make_shared<std::promise<void>>();
		worker.work([i, &order, &orderMutex, &doneCount, prom]() {
			{
				std::lock_guard<std::mutex> lk(orderMutex);
				order.push_back(i);
			}
			doneCount.fetch_add(1);
			if (i == N - 1) prom->set_value();
		});
	}

	REQUIRE(fut.wait_for(std::chrono::seconds(5)) == std::future_status::ready);
	REQUIRE(doneCount == N);

	std::vector<int> expected(N);
	std::iota(expected.begin(), expected.end(), 0);
	REQUIRE(order == expected);
}

TEST_CASE("a task pushed as the worker parks is never lost", "[TaskWorker]") {
	// Regression test for the lost-wakeup TaskSignal exists to prevent (see
	// its comment). Each iteration waits for the worker to go fully idle,
	// then pushes exactly one task, so the push races the worker's park. A
	// single lost signal stalls the iteration and trips the timeout.
	Test::TestContext<> ctx;
	TaskWorker worker;

	for (int i = 0; i < 2000; i++) {
		std::shared_ptr<std::promise<void>> prom;
		auto fut = makePromise(prom);

		worker.work([prom]() { prom->set_value(); });
		REQUIRE(fut.wait_for(std::chrono::seconds(5)) == std::future_status::ready);

		// The future is satisfied from inside the task, so the worker may not
		// have looped back to wait() yet. Give it that chance, so the NEXT
		// push races the park rather than arriving at a busy worker.
		std::this_thread::yield();
	}
}

TEST_CASE("queuing from the task itself does not deadlock", "[TaskWorker]") {
	Test::TestContext<> ctx;
	TaskWorker worker;

	std::atomic<int> execCount{0};
	std::shared_ptr<std::promise<void>> prom;
	auto fut = makePromise(prom);

	// First task queues a second task from inside the worker thread.
	worker.work([&worker, &execCount, prom]() {
		execCount.fetch_add(1);
		worker.work([&execCount, prom]() {
			execCount.fetch_add(1);
			prom->set_value();
		});
	});

	REQUIRE(fut.wait_for(std::chrono::seconds(5)) == std::future_status::ready);
	REQUIRE(execCount == 2);
}
