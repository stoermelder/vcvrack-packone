#include "../test/framework.hpp"
#include "UiDspExclusion.hpp"
#include <atomic>
#include <thread>

void testPluginInit(rack::Plugin* p) {
	pluginInstance = p;
}

using namespace StoermelderPackOne;

TEST_CASE("UiDspExclusion: DSP enters when the UI is outside", "[exclusion]") {
	UiDspExclusion x;
	REQUIRE(x.tryEnterDsp());
	x.leaveDsp();
	REQUIRE(x.tryEnterDsp()); // reusable
	x.leaveDsp();
}

TEST_CASE("UiDspExclusion: DSP is refused while the UI is inside and enters again after", "[exclusion]") {
	UiDspExclusion x;
	x.enterUi();
	REQUIRE_FALSE(x.tryEnterDsp());
	REQUIRE_FALSE(x.tryEnterDsp());
	REQUIRE_FALSE(x.dspInside.load()); // a refused attempt leaves no trace
	x.leaveUi();
	REQUIRE(x.tryEnterDsp());
	x.leaveDsp();
}

TEST_CASE("UiDspExclusion: the UI enters at once when the DSP is outside", "[exclusion]") {
	UiDspExclusion x;
	x.enterUi(); // would hang the test if it waited
	REQUIRE(x.uiInside.load());
	x.leaveUi();
	REQUIRE_FALSE(x.uiInside.load());

	REQUIRE(x.tryEnterDsp());
	x.leaveDsp();
	x.enterUi();
	x.leaveUi();
}

TEST_CASE("UiDspExclusion: UI sections nest, only the outermost leave releases the DSP", "[exclusion]") {
	UiDspExclusion x;
	x.enterUi();
	x.enterUi();
	x.leaveUi();
	REQUIRE_FALSE(x.tryEnterDsp());
	x.leaveUi();
	REQUIRE(x.tryEnterDsp());
	x.leaveDsp();
}

TEST_CASE("UiDspExclusion: the UI waits for a DSP section that is already running", "[exclusion]") {
	UiDspExclusion x;
	REQUIRE(x.tryEnterDsp());
	std::atomic<bool> uiEntered{false};
	std::thread ui([&]() {
		x.enterUi();
		uiEntered = true;
		x.leaveUi();
	});
	// While the DSP section is open the UI cannot be in, and it already blocks new DSP sections.
	while (!x.uiInside.load()) std::this_thread::yield();
	REQUIRE_FALSE(uiEntered.load());
	x.leaveDsp();
	ui.join();
	REQUIRE(uiEntered.load());
}

TEST_CASE("UiDspExclusion: two threads never overlap", "[exclusion]") {
	UiDspExclusion x;
	std::atomic<int> inside{0};
	std::atomic<int> overlaps{0};
	std::atomic<long> dspEntered{0}, uiEntered{0};
	constexpr int N = 200000;

	std::thread dsp([&]() {
		for (int i = 0; i < N; i++) {
			if (!x.tryEnterDsp()) continue;
			if (inside.fetch_add(1) != 0) overlaps++;
			inside.fetch_sub(1);
			x.leaveDsp();
			dspEntered++;
		}
	});
	for (int i = 0; i < N; i++) {
		x.enterUi();
		if (inside.fetch_add(1) != 0) overlaps++;
		inside.fetch_sub(1);
		x.leaveUi();
		uiEntered++;
	}
	dsp.join();

	REQUIRE(overlaps == 0);
	REQUIRE(uiEntered == N);
	REQUIRE(dspEntered > 0);
}
