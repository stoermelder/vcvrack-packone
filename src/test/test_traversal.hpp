#pragma once
#include "test_plugin.hpp"
#include <rack.hpp>
#include <widget/Widget.hpp>
#include <app/Scene.hpp>
#include <vector>
#include <functional>

// Test::traversal — the spatial walk of a widget tree, shared by every driver that needs one.
//
// This is deliberately its own header and its own namespace, and it deliberately knows nothing
// about events. Two things will want to walk a laid-out widget tree: EventDriver
// and a future DrawDriver. They need the *same* notion of
// which children get visited, in what order, and with what accumulated transform — a point
// translated down into local space for hit-testing is the same arithmetic as an origin
// translated down for drawing. Writing that twice is how the two end up disagreeing about, say,
// whether an invisible child is skipped.
//
// The rule mirrors rack::widget::Widget::recursePositionEvent (Widget.hpp) exactly, because that
// is the code EventDriver actually dispatches through — if this diverged from Rack's own
// recursion, a traversal-based assertion would describe a tree Rack never walks:
//
//   - children are visited in REVERSE insertion order (last added is topmost),
//   - an invisible child is skipped, along with its whole subtree,
//   - for position-based work, a child whose box does not contain the point is skipped,
//   - a child's local coordinates are the parent's minus the child's box origin.
//
// Nothing here dispatches, consumes, or mutates anything. It observes. That keeps it usable by
// a DrawDriver whose whole cheapest tier is "which widgets, in what order, with
// what transform" and which must not send events to find out.

namespace Test {
namespace traversal {

// One visited widget, with the state the walk accumulated to reach it.
struct Visit {
	rack::widget::Widget* widget = nullptr;
	// The widget's origin in the coordinate space of the widget the walk started from. Add a
	// widget-local point to this to get the same point in the root's space; subtract it from a
	// root-space point to get that point in the widget's local space.
	rack::math::Vec offset;
	// How many levels below the starting widget this one is. The root itself is depth 0.
	int depth = 0;
};

// Whether a child would be descended into at all, ignoring position. Visibility is the only
// filter recursePositionEvent applies before the box test, and it applies to the whole subtree:
// hiding a widget hides its children too. SceneLayout relies on exactly this to neutralise the
// scene's own full-size overlays.
inline bool isTraversable(const rack::widget::Widget* w) {
	return w != nullptr && w->visible;
}

// Walks the tree under `root` in Rack's own position-event order, calling `visit` for every
// widget reached, `root` included.
//
// `visit` returns false to prune — the walk then skips that widget's children but continues with
// its siblings. That is enough to express both drivers' needs without a second walk function: a
// hit-test prunes a child whose box does not contain the point, and a draw walk prunes a clipped
// subtree.
inline void walk(rack::widget::Widget* root,
                 const std::function<bool(const Visit&)>& visit,
                 rack::math::Vec offset = rack::math::Vec(), int depth = 0) {
	if (!isTraversable(root)) return;

	Visit v;
	v.widget = root;
	v.offset = offset;
	v.depth = depth;
	if (!visit(v)) return;

	// Reverse insertion order: topmost first, matching recursePositionEvent. A driver that
	// walked forward would report the *bottom* widget as the one an event reaches.
	for (auto it = root->children.rbegin(); it != root->children.rend(); it++) {
		walk(*it, visit, offset.plus((*it)->box.pos), depth + 1);
	}
}

// Every widget under `root`, in traversal order. The straightforward form of walk() for a test
// that wants to assert on visit order or on what is reachable at all.
inline std::vector<Visit> visitAll(rack::widget::Widget* root) {
	std::vector<Visit> visits;
	walk(root, [&](const Visit& v) {
		visits.push_back(v);
		return true;
	});
	return visits;
}

// Descends the single topmost path under `pos`, calling `visit` for each widget on the way down.
//
// Unlike walk(), this does not visit siblings: at each level it takes the *first* traversable
// child (in reverse insertion order) whose box contains the point, exactly as an unconsumed
// position event would be delivered topmost-first. `pos` is in `root`'s own coordinate space.
inline void walkHit(rack::widget::Widget* root, rack::math::Vec pos,
                    const std::function<void(const Visit&)>& visit) {
	if (!isTraversable(root)) return;

	rack::widget::Widget* current = root;
	rack::math::Vec local = pos;
	rack::math::Vec offset;
	int depth = 0;

	while (true) {
		Visit v;
		v.widget = current;
		v.offset = offset;
		v.depth = depth;
		visit(v);

		rack::widget::Widget* next = nullptr;
		for (auto it = current->children.rbegin(); it != current->children.rend(); it++) {
			rack::widget::Widget* child = *it;
			if (!isTraversable(child)) continue;
			if (!child->box.contains(local)) continue;
			next = child;
			break;
		}
		if (!next) return;

		local = local.minus(next->box.pos);
		offset = offset.plus(next->box.pos);
		current = next;
		depth++;
	}
}

// The chain of widgets a position event at `pos` (in `root`'s coordinate space) would descend
// through, outermost first — root, then the topmost child containing the point, and so on.
//
// This is the hit-test path *as geometry*, and it is not the same question as "who consumed the
// event": consumption depends on what each widget's handler does, which only real dispatch can
// answer. EventDriver answers that one, through Rack's actual recursion. This answers "what is
// under this point", which is what a test wants when the interesting failure is that a widget
// is not where it thought it was.
inline std::vector<Visit> hitPath(rack::widget::Widget* root, rack::math::Vec pos) {
	std::vector<Visit> path;
	walkHit(root, pos, [&](const Visit& v) { path.push_back(v); });
	return path;
}

// The deepest widget under `pos`, or nullptr if the point misses `root` entirely. The widget an
// unconsumed position event would reach last.
inline rack::widget::Widget* hitTest(rack::widget::Widget* root, rack::math::Vec pos) {
	std::vector<Visit> path = hitPath(root, pos);
	return path.empty() ? nullptr : path.back().widget;
}

// The first widget of type T under `root`, in traversal order — the inner widget a test actually
// wants to click, found by what it *is* rather than by where the panel happens to put it.
//
// This is the alternative to re-deriving a module's own layout arithmetic at the call site. A
// ModuleWidget's interesting children (a grid, an edge lane, a screen) are private locals of its
// constructor, so a test that wants one either reaches it by type or reconstructs its position
// from the panel's layout constants — and the reconstruction is a second copy of the layout, free
// to drift silently until the click lands on the wrong widget and the test asserts nothing. Type
// lookup follows a layout change for free.
//
// Ignores visibility deliberately: `walk` skips invisible subtrees, and a widget hidden at rest
// (a tooltip, a mode-dependent overlay) is still a legitimate lookup target for a test that means
// to show it first.
template <typename T>
inline T* findDescendant(rack::widget::Widget* root) {
	if (!root) return nullptr;
	if (T* hit = dynamic_cast<T*>(root)) return hit;
	for (auto it = root->children.rbegin(); it != root->children.rend(); it++) {
		if (T* hit = findDescendant<T>(*it)) return hit;
	}
	return nullptr;
}

// Every widget of type T under `root`, for a panel with several of a kind — Tilt's four edge
// lanes, a mixer's channel strips. Order matches findDescendant's (reverse insertion, topmost
// first), so index 0 is the same widget findDescendant returns.
template <typename T>
inline std::vector<T*> findDescendants(rack::widget::Widget* root) {
	std::vector<T*> found;
	walk(root, [&](const Visit& v) {
		if (T* hit = dynamic_cast<T*>(v.widget)) found.push_back(hit);
		return true;
	});
	return found;
}

} // namespace traversal
} // namespace Test
