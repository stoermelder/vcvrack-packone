#pragma once
#include <math.hpp>


namespace StoermelderPackOne {
namespace Tutorial {

enum class Side {
	AUTO,
	RIGHT,
	LEFT,
	BOTTOM,
	TOP
};

// The edge of the BUBBLE the pointer touches, given which side of the TARGET the bubble was
// placed on (Placement::side). These are opposite: a bubble placed to the RIGHT of the target
// points back at it from the bubble's own LEFT edge, and so on.
inline Side oppositeSide(Side side) {
	switch (side) {
		case Side::RIGHT: return Side::LEFT;
		case Side::LEFT: return Side::RIGHT;
		case Side::BOTTOM: return Side::TOP;
		case Side::TOP: return Side::BOTTOM;
		default: return Side::AUTO;
	}
}

/** Sizes and spacing for tutorial bubbles, in module px (1 HP = 15 px). */
struct Style {
	float bubbleWidth = 140.f;
	float gap = 3.f;             // tip-to-ring gap
	float arrowLength = 6.f;
	float arrowBase = 10.f;
	float highlightPadding = 3.f;
	float boundsMargin = 4.f;
	float cornerRadius = 3.f;    // bubble corner radius; keeps the pointer base off the corners

	// TutorialBubble layout, not used by the placement algorithm itself but kept in this shared
	// Style struct. Visually close to Ahab's info dialog (InfoWindow.hpp), a fixed-size 337x187
	// screen-px modal — this bubble has no such zoom compensation (it scales with the rack), so
	// at 100% rack zoom these numbers make it read as a small aside, not a full-size Rack panel.
	float padding = 10.f;
	float captionFontSize = 7.f;
	float titleFontSize = 9.f;
	float bodyFontSize = 8.f;
	float buttonHeight = 14.f;
	float closeButtonSize = 12.f;
};

struct PlacementInput {
	math::Rect bounds;       // visible region, already inset by Style::boundsMargin
	math::Rect module;       // (0, 0, module size)
	bool hasTarget = false;
	math::Rect target;       // highlight rect, including highlight padding
	math::Vec bubble;        // bubble body size (without the pointer)
	Side preferred = Side::AUTO;
	Side previous = Side::AUTO;  // hysteresis; AUTO = none
	Style style;
};

struct Placement {
	math::Rect box;          // bubble body rect; may extend beyond `module`
	Side side = Side::AUTO;  // AUTO for centered
	bool hasPointer = false;
	math::Vec pointerTip;
	math::Vec pointerBaseA;
	math::Vec pointerBaseB;
	bool overlapsTarget = false;  // only set in the last-resort fallback
};

namespace detail {

inline bool intersects(math::Rect a, math::Rect b) {
	return a.intersects(b);
}

/** Builds the candidate bubble rect for one side, without clamping feasibility. */
inline math::Rect candidateBox(Side side, math::Rect target, math::Vec bubble, float d) {
	math::Rect box;
	box.size = bubble;
	switch (side) {
		case Side::RIGHT:
			box.pos.x = target.getRight() + d;
			box.pos.y = target.getCenter().y - bubble.y * 0.5f;
			break;
		case Side::LEFT:
			box.pos.x = target.getLeft() - d - bubble.x;
			box.pos.y = target.getCenter().y - bubble.y * 0.5f;
			break;
		case Side::BOTTOM:
			box.pos.y = target.getBottom() + d;
			box.pos.x = target.getCenter().x - bubble.x * 0.5f;
			break;
		case Side::TOP:
			box.pos.y = target.getTop() - d - bubble.y;
			box.pos.x = target.getCenter().x - bubble.x * 0.5f;
			break;
		default:
			break;
	}
	return box;
}

/** Clamps the cross axis into `bounds`, leaving the main axis untouched. */
inline math::Rect clampCrossAxis(Side side, math::Rect box, math::Rect bounds) {
	if (side == Side::RIGHT || side == Side::LEFT) {
		box.pos.y = math::clampSafe(box.pos.y, bounds.getTop(), bounds.getBottom() - box.size.y);
	}
	else if (side == Side::BOTTOM || side == Side::TOP) {
		box.pos.x = math::clampSafe(box.pos.x, bounds.getLeft(), bounds.getRight() - box.size.x);
	}
	return box;
}

/** Main-axis extent of the candidate box, before cross-axis clamping. */
inline bool isFeasible(Side side, math::Rect box, math::Rect bounds) {
	switch (side) {
		case Side::RIGHT:
			return box.getLeft() >= bounds.getLeft() && box.getRight() <= bounds.getRight();
		case Side::LEFT:
			return box.getLeft() >= bounds.getLeft() && box.getRight() <= bounds.getRight();
		case Side::BOTTOM:
			return box.getTop() >= bounds.getTop() && box.getBottom() <= bounds.getBottom();
		case Side::TOP:
			return box.getTop() >= bounds.getTop() && box.getBottom() <= bounds.getBottom();
		default:
			return false;
	}
}

/** Cross-axis shift introduced by clamping, used as a tiebreaker. */
inline float crossAxisShift(Side side, math::Rect unclamped, math::Rect clamped) {
	if (side == Side::RIGHT || side == Side::LEFT) {
		return std::fabs(clamped.pos.y - unclamped.pos.y);
	}
	return std::fabs(clamped.pos.x - unclamped.pos.x);
}

/** Order used to break ties among equally-clamped candidates. */
inline int sideOrder(Side side) {
	switch (side) {
		case Side::RIGHT: return 0;
		case Side::LEFT: return 1;
		case Side::BOTTOM: return 2;
		case Side::TOP: return 3;
		default: return 4;
	}
}

/** Pointer geometry for a feasible, cross-axis-clamped candidate. */
inline void computePointer(Side side, math::Rect box, math::Rect target, const Style& style, Placement& out) {
	float r = style.cornerRadius;
	float halfBase = style.arrowBase * 0.5f;

	if (side == Side::RIGHT || side == Side::LEFT) {
		float minY = box.getTop() + r + halfBase;
		float maxY = box.getBottom() - r - halfBase;
		float baseY = (minY <= maxY) ? math::clampSafe(target.getCenter().y, minY, maxY) : box.getCenter().y;
		float tipX = (side == Side::RIGHT) ? target.getRight() + style.gap : target.getLeft() - style.gap;
		float tipY = math::clampSafe(baseY, target.getTop(), target.getBottom());
		float baseX = (side == Side::RIGHT) ? box.getLeft() : box.getRight();

		out.pointerTip = math::Vec(tipX, tipY);
		out.pointerBaseA = math::Vec(baseX, baseY - halfBase);
		out.pointerBaseB = math::Vec(baseX, baseY + halfBase);
	}
	else {
		float minX = box.getLeft() + r + halfBase;
		float maxX = box.getRight() - r - halfBase;
		float baseX = (minX <= maxX) ? math::clampSafe(target.getCenter().x, minX, maxX) : box.getCenter().x;
		float tipY = (side == Side::BOTTOM) ? target.getBottom() + style.gap : target.getTop() - style.gap;
		float tipX = math::clampSafe(baseX, target.getLeft(), target.getRight());
		float baseY = (side == Side::BOTTOM) ? box.getTop() : box.getBottom();

		out.pointerTip = math::Vec(tipX, tipY);
		out.pointerBaseA = math::Vec(baseX - halfBase, baseY);
		out.pointerBaseB = math::Vec(baseX + halfBase, baseY);
	}
	out.hasPointer = true;
}

inline Placement placeCentered(const PlacementInput& in) {
	Placement out;
	out.side = Side::AUTO;
	out.hasPointer = false;

	math::Rect box;
	box.size = in.bubble;
	box.pos = in.module.getCenter().minus(in.bubble.mult(0.5f));

	if (in.bounds.intersects(in.module)) {
		box.pos.x = math::clampSafe(box.pos.x, in.bounds.getLeft(), in.bounds.getRight() - box.size.x);
		box.pos.y = math::clampSafe(box.pos.y, in.bounds.getTop(), in.bounds.getBottom() - box.size.y);
	}

	out.box = box;
	return out;
}

inline Placement placeLastResort(const PlacementInput& in) {
	Placement out;
	out.side = Side::AUTO;
	out.hasPointer = false;
	out.overlapsTarget = true;

	math::Vec targetCenter = in.target.getCenter();
	math::Vec boundsCenter = in.bounds.getCenter();

	// Corner of `bounds` farthest from the target center.
	math::Vec corners[4] = {
		in.bounds.getTopLeft(), in.bounds.getTopRight(),
		in.bounds.getBottomLeft(), in.bounds.getBottomRight()
	};
	math::Vec best = corners[0];
	float bestDist = -1.f;
	for (math::Vec c : corners) {
		float d = c.minus(targetCenter).square();
		if (d > bestDist) {
			bestDist = d;
			best = c;
		}
	}

	math::Rect box;
	box.size = in.bubble;
	// Anchor the corner of the box nearest `best` onto `best`, biasing away from bounds' center.
	box.pos.x = (best.x >= boundsCenter.x) ? best.x - box.size.x : best.x;
	box.pos.y = (best.y >= boundsCenter.y) ? best.y - box.size.y : best.y;
	box.pos.x = math::clampSafe(box.pos.x, in.bounds.getLeft(), in.bounds.getRight() - box.size.x);
	box.pos.y = math::clampSafe(box.pos.y, in.bounds.getTop(), in.bounds.getBottom() - box.size.y);

	out.box = box;
	return out;
}

} // namespace detail


/** Pure placement algorithm for a tutorial bubble relative to its target. */
inline Placement place(const PlacementInput& in) {
	if (!in.hasTarget) {
		return detail::placeCentered(in);
	}

	const float d = in.style.gap + in.style.arrowLength;
	const Side sides[4] = {Side::RIGHT, Side::LEFT, Side::BOTTOM, Side::TOP};

	struct Candidate {
		Side side;
		math::Rect unclamped;
		math::Rect clamped;
		bool feasible;
		float shift;
	};
	Candidate candidates[4];
	for (int i = 0; i < 4; i++) {
		Side side = sides[i];
		math::Rect box = detail::candidateBox(side, in.target, in.bubble, d);
		bool feasible = detail::isFeasible(side, box, in.bounds);
		math::Rect clamped = detail::clampCrossAxis(side, box, in.bounds);
		candidates[i] = Candidate{side, box, clamped, feasible, detail::crossAxisShift(side, box, clamped)};
	}

	auto findFeasible = [&](Side want) -> const Candidate* {
		for (int i = 0; i < 4; i++) {
			if (candidates[i].side == want && candidates[i].feasible) return &candidates[i];
		}
		return nullptr;
	};

	const Candidate* chosen = nullptr;
	// 1. Preferred side, if feasible.
	if (in.preferred != Side::AUTO) {
		chosen = findFeasible(in.preferred);
	}
	// 2. Previous side, if still feasible (hysteresis).
	if (!chosen && in.previous != Side::AUTO) {
		chosen = findFeasible(in.previous);
	}
	// 3. Lowest score = sideOrder * 1000 + crossAxisShift.
	if (!chosen) {
		float bestScore = 0.f;
		for (int i = 0; i < 4; i++) {
			if (!candidates[i].feasible) continue;
			float score = detail::sideOrder(candidates[i].side) * 1000.f + candidates[i].shift;
			if (!chosen || score < bestScore) {
				chosen = &candidates[i];
				bestScore = score;
			}
		}
	}
	// 4. Last resort: none feasible.
	if (!chosen) {
		return detail::placeLastResort(in);
	}

	Placement out;
	out.side = chosen->side;
	out.box = chosen->clamped;
	out.overlapsTarget = false;
	detail::computePointer(chosen->side, chosen->clamped, in.target, in.style, out);
	return out;
}

} // namespace Tutorial
} // namespace StoermelderPackOne