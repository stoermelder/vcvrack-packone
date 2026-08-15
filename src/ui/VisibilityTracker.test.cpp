#include "../test/test_plugin.hpp"
#include "../test/test_context.hpp"
#include "VisibilityTracker.hpp"

using namespace StoermelderPackOne;

// Each test uses a FRESH local VisibilityTracker::State (not the process-wide
// singleton), so the global map is never touched here and Catch2 body-reruns
// cannot leak state between tests.

TEST_CASE("First owner hides, last owner shows", "[VisibilityTracker]") {
	Test::TestContext<> ctx;
	VisibilityTracker::State state;
	rack::widget::Widget container;
	container.visible = true;
	char a, b;

	state.hide(&container, &a);
	REQUIRE(container.visible == false);

	state.hide(&container, &b);
	REQUIRE(container.visible == false);

	state.release(&container, &a);
	REQUIRE(container.visible == false); // b still wants it hidden

	state.release(&container, &b);
	REQUIRE(container.visible == true);
}

TEST_CASE("Duplicate hide by same owner does not double-count", "[VisibilityTracker]") {
	Test::TestContext<> ctx;
	VisibilityTracker::State state;
	rack::widget::Widget container;
	container.visible = true;
	char a;

	state.hide(&container, &a);
	state.hide(&container, &a);
	REQUIRE(state.ownerCount(&container) == 1);

	state.release(&container, &a);
	REQUIRE(state.ownerCount(&container) == 0);
	REQUIRE(container.visible == true);
}

TEST_CASE("Release without hide is a no-op", "[VisibilityTracker]") {
	Test::TestContext<> ctx;
	VisibilityTracker::State state;
	rack::widget::Widget container;
	container.visible = true;
	char a;

	state.release(&container, &a);
	REQUIRE(container.visible == true);
	REQUIRE(state.ownerCount(&container) == 0);
}

TEST_CASE("Restores previous visibility when it was already hidden", "[VisibilityTracker]") {
	Test::TestContext<> ctx;
	VisibilityTracker::State state;
	rack::widget::Widget container;
	// Cables already hidden for an unrelated reason (e.g. a user preference).
	container.visible = false;
	char a;

	state.hide(&container, &a);
	REQUIRE(container.visible == false);

	state.release(&container, &a);
	// Must NOT force cables visible — they were hidden before we got involved.
	REQUIRE(container.visible == false);
}

TEST_CASE("Separate containers are independent", "[VisibilityTracker]") {
	Test::TestContext<> ctx;
	VisibilityTracker::State state;
	rack::widget::Widget container1, container2;
	container1.visible = true;
	container2.visible = true;
	char a, b;

	state.hide(&container1, &a);
	state.hide(&container2, &b);
	REQUIRE(container1.visible == false);
	REQUIRE(container2.visible == false);

	state.release(&container1, &a);
	REQUIRE(container1.visible == true);
	REQUIRE(container2.visible == false); // unaffected

	state.release(&container2, &b);
	REQUIRE(container2.visible == true);
}

TEST_CASE("isHiddenBy reports owner presence", "[VisibilityTracker]") {
	Test::TestContext<> ctx;
	VisibilityTracker::State state;
	rack::widget::Widget container;
	char a, b;

	REQUIRE(!state.isHiddenBy(&container, &a));
	state.hide(&container, &a);
	REQUIRE(state.isHiddenBy(&container, &a));
	REQUIRE(!state.isHiddenBy(&container, &b));
	state.release(&container, &a);
	REQUIRE(!state.isHiddenBy(&container, &a));
}

TEST_CASE("Guard hides on construction and releases on destruction", "[VisibilityTracker]") {
	Test::TestContext<> ctx;
	rack::widget::Widget container;
	container.visible = true;
	char a;

	{
		// Guard routes through the static wrappers, i.e. the shared singleton.
		VisibilityTracker::Guard guard(&container, &a);
		REQUIRE(container.visible == false);
		REQUIRE(VisibilityTracker::ownerCount(&container) == 1);
		REQUIRE(VisibilityTracker::isHiddenBy(&container, &a));
	}
	// Destruction of the guard releases the request; the shared state is clean.
	REQUIRE(container.visible == true);
	REQUIRE(VisibilityTracker::ownerCount(&container) == 0);
}

TEST_CASE("null container or owner is a safe no-op", "[VisibilityTracker]") {
	Test::TestContext<> ctx;
	VisibilityTracker::State state;
	rack::widget::Widget container;
	container.visible = true;
	char a;

	state.hide(nullptr, &a);
	state.hide(&container, nullptr);
	REQUIRE(container.visible == true);
	REQUIRE(state.ownerCount(&container) == 0);

	state.release(nullptr, &a);
	state.release(&container, nullptr);
	REQUIRE(container.visible == true);
}
