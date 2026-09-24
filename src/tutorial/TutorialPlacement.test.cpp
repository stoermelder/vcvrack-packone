#include "../test/test_plugin.hpp"
#include "TutorialPlacement.hpp"

using namespace StoermelderPackOne::Tutorial;

namespace {

PlacementInput makeInput() {
	PlacementInput in;
	in.module = math::Rect(0.f, 0.f, 270.f, 200.f);
	in.bounds = in.module.grow(math::Vec(400.f, 400.f)); // plenty of open space by default
	in.bubble = math::Vec(180.f, 100.f);
	in.hasTarget = true;
	in.preferred = Side::AUTO;
	in.previous = Side::AUTO;
	return in;
}

bool onBubbleEdge(math::Rect box, math::Vec p, float eps = 1e-3f) {
	bool onVertical = math::isNear(p.x, box.getLeft(), eps) || math::isNear(p.x, box.getRight(), eps);
	bool onHorizontal = math::isNear(p.y, box.getTop(), eps) || math::isNear(p.y, box.getBottom(), eps);
	return onVertical || onHorizontal;
}

bool awayFromCorners(math::Rect box, math::Vec a, math::Vec b, float style_corner) {
	math::Vec corners[4] = {box.getTopLeft(), box.getTopRight(), box.getBottomLeft(), box.getBottomRight()};
	for (math::Vec c : corners) {
		if (a.minus(c).norm() < style_corner - 1e-3f) return false;
		if (b.minus(c).norm() < style_corner - 1e-3f) return false;
	}
	return true;
}

} // namespace

TEST_CASE("Each side: preferred side in open space is taken, no overlap, pointer geometry valid", "[TutorialPlacement]") {
	Side sides[4] = {Side::RIGHT, Side::LEFT, Side::BOTTOM, Side::TOP};
	for (Side side : sides) {
		PlacementInput in = makeInput();
		in.target = math::Rect(120.f, 90.f, 30.f, 20.f);
		in.preferred = side;

		Placement p = place(in);

		CATCH_INFO("side=" << (int) side);
		REQUIRE(p.side == side);
		REQUIRE(p.hasPointer);
		REQUIRE_FALSE(p.overlapsTarget);
		REQUIRE_FALSE(p.box.intersects(in.target));

		// Tip lies within `gap` of the target edge, on the corresponding axis.
		float gap = in.style.gap;
		if (side == Side::RIGHT) {
			REQUIRE(math::isNear(p.pointerTip.x, in.target.getRight() + gap, 1e-3f));
		}
		else if (side == Side::LEFT) {
			REQUIRE(math::isNear(p.pointerTip.x, in.target.getLeft() - gap, 1e-3f));
		}
		else if (side == Side::BOTTOM) {
			REQUIRE(math::isNear(p.pointerTip.y, in.target.getBottom() + gap, 1e-3f));
		}
		else if (side == Side::TOP) {
			REQUIRE(math::isNear(p.pointerTip.y, in.target.getTop() - gap, 1e-3f));
		}

		// Pointer base sits on the bubble edge, away from its corners.
		REQUIRE(onBubbleEdge(p.box, p.pointerBaseA));
		REQUIRE(onBubbleEdge(p.box, p.pointerBaseB));
		REQUIRE(awayFromCorners(p.box, p.pointerBaseA, p.pointerBaseB, in.style.cornerRadius));
	}
}

TEST_CASE("AUTO with room chooses RIGHT", "[TutorialPlacement]") {
	PlacementInput in = makeInput();
	in.target = math::Rect(120.f, 90.f, 30.f, 20.f);
	in.preferred = Side::AUTO;

	Placement p = place(in);
	REQUIRE(p.side == Side::RIGHT);
}

TEST_CASE("Module at the right edge of the visible region forces LEFT", "[TutorialPlacement]") {
	PlacementInput in = makeInput();
	// Bounds end right where the module ends: no room to the right.
	in.bounds = math::Rect(-400.f, -400.f, 400.f + in.module.size.x, 800.f);
	in.target = math::Rect(230.f, 90.f, 30.f, 20.f);
	in.preferred = Side::AUTO;

	Placement p = place(in);
	REQUIRE(p.side == Side::LEFT);
	REQUIRE_FALSE(p.box.intersects(in.target));
}

TEST_CASE("Target near the bottom-right resolves to LEFT or TOP, clamped, no overlap", "[TutorialPlacement]") {
	PlacementInput in = makeInput();
	// Bounds match the module exactly: no room to the right or below the target.
	in.bounds = in.module;
	in.target = math::Rect(250.f, 180.f, 15.f, 15.f);
	in.preferred = Side::AUTO;

	Placement p = place(in);
	REQUIRE((p.side == Side::LEFT || p.side == Side::TOP));
	REQUIRE_FALSE(p.overlapsTarget);
	REQUIRE_FALSE(p.box.intersects(in.target));
	REQUIRE(in.bounds.contains(p.box));
}

TEST_CASE("Narrow module with a wide bubble: box extends beyond module, accepted", "[TutorialPlacement]") {
	PlacementInput in = makeInput();
	in.module = math::Rect(0.f, 0.f, 45.f, 200.f); // 3 HP
	in.bounds = in.module.grow(math::Vec(400.f, 400.f));
	in.target = math::Rect(10.f, 90.f, 20.f, 20.f);
	in.bubble = math::Vec(180.f, 100.f); // 12 HP
	in.preferred = Side::RIGHT;

	Placement p = place(in);
	REQUIRE(p.side == Side::RIGHT);
	REQUIRE(p.box.getRight() > in.module.getRight());
	REQUIRE_FALSE(p.box.intersects(in.target));
}

TEST_CASE("Hysteresis: previous side stays if still feasible", "[TutorialPlacement]") {
	PlacementInput in = makeInput();
	in.target = math::Rect(120.f, 90.f, 30.f, 20.f);
	in.preferred = Side::AUTO; // no preference this step
	in.previous = Side::LEFT;

	Placement p = place(in);
	REQUIRE(p.side == Side::LEFT);
}

TEST_CASE("Cross-axis clamp near the top edge keeps the tip within the target's y-range", "[TutorialPlacement]") {
	PlacementInput in = makeInput();
	// Bounds barely extend above the module; target sits high, forcing a cross-axis clamp on RIGHT.
	in.bounds = math::Rect(0.f, -10.f, 700.f, 700.f);
	in.target = math::Rect(120.f, -5.f, 30.f, 20.f);
	in.preferred = Side::RIGHT;

	Placement p = place(in);
	REQUIRE(p.side == Side::RIGHT);
	REQUIRE(p.hasPointer);
	REQUIRE(p.pointerTip.y >= in.target.getTop() - 1e-3f);
	REQUIRE(p.pointerTip.y <= in.target.getBottom() + 1e-3f);
}

TEST_CASE("Tiny target in a corner produces a slanted pointer", "[TutorialPlacement]") {
	PlacementInput in = makeInput();
	in.bounds = math::Rect(0.f, -10.f, 700.f, 700.f);
	in.target = math::Rect(120.f, -8.f, 4.f, 4.f);
	in.preferred = Side::RIGHT;

	Placement p = place(in);
	REQUIRE(p.hasPointer);
	// The tip is not on the same horizontal line as the (unclamped) base center -> slanted.
	float baseY = (p.pointerBaseA.y + p.pointerBaseB.y) * 0.5f;
	REQUIRE_FALSE(math::isNear(p.pointerTip.y, baseY, 1e-3f));
}

TEST_CASE("Target about equal to bounds triggers the last resort", "[TutorialPlacement]") {
	PlacementInput in = makeInput();
	in.bounds = math::Rect(0.f, 0.f, 270.f, 200.f);
	in.target = in.bounds; // fills the visible region (zoomed in)
	in.preferred = Side::AUTO;

	Placement p = place(in);
	REQUIRE(p.overlapsTarget);
	REQUIRE_FALSE(p.hasPointer);
	REQUIRE(in.bounds.contains(p.box));
}

TEST_CASE("Centered step is centered on the module and clamped when partly visible", "[TutorialPlacement]") {
	PlacementInput in = makeInput();
	in.hasTarget = false;

	Placement full = place(in);
	REQUIRE(full.side == Side::AUTO);
	REQUIRE_FALSE(full.hasPointer);
	math::Vec expectedCenter = in.module.getCenter();
	REQUIRE(math::isNear(full.box.getCenter().x, expectedCenter.x, 1e-3f));
	REQUIRE(math::isNear(full.box.getCenter().y, expectedCenter.y, 1e-3f));

	// Module only partly visible: bounds overlap the module but the unclamped centered box
	// would spill past their right edge, so it must be clamped inside.
	PlacementInput clampedIn = makeInput();
	clampedIn.hasTarget = false;
	clampedIn.bounds = math::Rect(0.f, 0.f, 200.f, 200.f); // overlaps the module's left part only
	Placement clamped = place(clampedIn);
	REQUIRE(clampedIn.bounds.contains(clamped.box));
	REQUIRE_FALSE(math::isNear(clamped.box.getCenter().x, expectedCenter.x, 1e-3f));
}

TEST_CASE("Centered step is not clamped when the module is off-screen", "[TutorialPlacement]") {
	PlacementInput in = makeInput();
	in.hasTarget = false;
	// Bounds don't overlap the module at all.
	in.bounds = math::Rect(1000.f, 1000.f, 200.f, 200.f);

	Placement p = place(in);
	math::Vec expectedCenter = in.module.getCenter();
	REQUIRE(math::isNear(p.box.getCenter().x, expectedCenter.x, 1e-3f));
	REQUIRE(math::isNear(p.box.getCenter().y, expectedCenter.y, 1e-3f));
}

TEST_CASE("Property sweep: feasible candidates never overlap the target and stay within bounds", "[TutorialPlacement]") {
	PlacementInput in = makeInput();
	in.bounds = math::Rect(-50.f, -50.f, 370.f, 300.f);

	int checked = 0;
	for (float tx = -20.f; tx <= 280.f; tx += 20.f) {
		for (float ty = -20.f; ty <= 210.f; ty += 20.f) {
			for (float tw : {5.f, 15.f, 40.f}) {
				for (float th : {5.f, 15.f, 40.f}) {
					in.target = math::Rect(tx, ty, tw, th);
					in.preferred = Side::AUTO;
					in.previous = Side::AUTO;

					Placement p = place(in);
					checked++;
					if (!p.overlapsTarget) {
						CATCH_INFO("target=(" << tx << "," << ty << "," << tw << "," << th << ") side=" << (int) p.side);
						REQUIRE_FALSE(p.box.intersects(in.target));
						REQUIRE(in.bounds.contains(p.box));
					}
				}
			}
		}
	}
	CATCH_INFO("checked=" << checked);
	REQUIRE(checked > 0);
}
