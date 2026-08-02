#include "../test/test_plugin.hpp"
#include "../test/test_context.hpp"
#include "MpmcTaskWorker.hpp"
#include <future>
#include <thread>
#include <chrono>

using namespace StoermelderPackOne;

static std::future<void> makePromise(std::shared_ptr<std::promise<void>>& out) {
	out = std::make_shared<std::promise<void>>();
	return out->get_future();
}

TEST_CASE("Task executes exactly once", "[MpmcTaskWorker]") {
	Test::TestContext<> ctx;
	MpmcTaskWorker worker;

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

TEST_CASE("Multiple tasks execute serially in submission order", "[MpmcTaskWorker]") {
	Test::TestContext<> ctx;
	MpmcTaskWorker worker;

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

// work() uses try_push(): under a concurrent burst that outruns the worker,
// the fixed-capacity queue can be momentarily full and a submission is
// dropped (returns false) rather than blocking the producer or overwriting a
// pending task. This test doesn't assume every submission lands -- it counts
// how many were accepted and asserts exactly that many run, exactly once.
TEST_CASE("Concurrent producers from multiple threads: every accepted task runs exactly once", "[MpmcTaskWorker]") {
	Test::TestContext<> ctx;
	MpmcTaskWorker worker;

	constexpr int PRODUCERS = 8;
	constexpr int PER_PRODUCER = 50;
	std::atomic<int> execCount{0};
	std::atomic<int> acceptedCount{0};

	std::vector<std::thread> threads;
	for (int p = 0; p < PRODUCERS; p++) {
		threads.emplace_back([&worker, &execCount, &acceptedCount]() {
			for (int i = 0; i < PER_PRODUCER; i++) {
				bool ok = worker.work([&execCount]() {
					execCount.fetch_add(1);
				});
				if (ok) acceptedCount.fetch_add(1);
			}
		});
	}
	for (auto& t : threads) t.join();

	int expected = acceptedCount.load();
	auto start = std::chrono::steady_clock::now();
	while (execCount.load() < expected) {
		if (std::chrono::steady_clock::now() - start > std::chrono::seconds(5)) break;
		std::this_thread::yield();
	}

	REQUIRE(expected > 0);
	REQUIRE(execCount == expected);
}

TEST_CASE("Queuing from the task itself does not deadlock", "[MpmcTaskWorker]") {
	Test::TestContext<> ctx;
	MpmcTaskWorker worker;

	std::atomic<int> execCount{0};
	std::shared_ptr<std::promise<void>> prom;
	auto fut = makePromise(prom);

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
