#pragma once
#include <rack.hpp>
#include <map>
#include <set>

namespace StoermelderPackOne {

/**
 * @brief Centralized control of a widget container's visibility (typically the
 * rack's cable container) shared between many owners.
 *
 * Widgets with overlay/viz workflows hide the cable container while their
 * overlay is active. Writing `container->visible` directly per widget makes
 * instances fight each other: turning one off (or deleting it) re-shows the
 * cables even though another widget still needs them hidden.
 *
 * This tracks the set of owners wanting the container hidden. The container is
 * hidden on the FIRST request and restored exactly when the LAST owner
 * releases. The visibility that existed before the first request is
 * snapshotted and restored, so a manual "cables hidden" preference isn't
 * clobbered. Both calls are idempotent, so a destructor can always `release()`.
 *
 * Thread-safety: GUI-thread only (the thread owning APP->scene->rack). The
 * container pointer must outlive every owner — in Rack the cable container is
 * a child of the RackWidget and module widgets are removed while the rack is
 * alive.
 *
 * Usage:
 *   VisibilityTracker::hide(container, this);    // on entering viz mode
 *   VisibilityTracker::release(container, this); // on leaving viz mode / in dtor
 */
struct VisibilityTracker {
	/** The entire mechanism, encapsulated so it can also be unit-tested with a
	 *  fresh instance per test case. */
	struct State {
		struct Entry {
			std::set<void*> owners;          // owners currently requesting hidden
			bool previousVisible = true;     // container visibility before the first request
		};
		std::map<rack::widget::Widget*, Entry> entries;

		void hide(rack::widget::Widget* container, void* owner) {
			if (!container || !owner) return;
			Entry& e = entries[container];
			bool first = e.owners.empty();
			if (first) {
				e.previousVisible = container->visible;
				container->hide();
			}
			e.owners.insert(owner);
		}

		void release(rack::widget::Widget* container, void* owner) {
			if (!container || !owner) return;
			auto it = entries.find(container);
			if (it == entries.end()) return; // never requested — safe no-op
			Entry& e = it->second;
			e.owners.erase(owner);
			if (e.owners.empty()) {
				if (e.previousVisible) container->show();
				// else: leave hidden — it was already hidden before we got involved
				entries.erase(it);
			}
		}

		bool isHiddenBy(rack::widget::Widget* container, void* owner) const {
			auto it = entries.find(container);
			return it != entries.end() && it->second.owners.find(owner) != it->second.owners.end();
		}

		size_t ownerCount(rack::widget::Widget* container) const {
			auto it = entries.find(container);
			return it == entries.end() ? 0 : it->second.owners.size();
		}
	};

	/** Single shared instance — a function-local static in an inline function,
	 *  so all module code in the dylib coalesces onto one State. */
	static State& state() {
		static State state;
		return state;
	}

	static void hide(rack::widget::Widget* container, void* owner) { state().hide(container, owner); }
	static void release(rack::widget::Widget* container, void* owner) { state().release(container, owner); }
	static bool isHiddenBy(rack::widget::Widget* container, void* owner) { return state().isHiddenBy(container, owner); }
	static size_t ownerCount(rack::widget::Widget* container) { return state().ownerCount(container); }

	/** RAII helper: requests hide on construction, releases on destruction. */
	struct Guard {
		rack::widget::Widget* container;
		void* owner;
		Guard(rack::widget::Widget* c, void* o) : container(c), owner(o) { hide(c, o); }
		~Guard() { release(container, owner); }
		Guard(const Guard&) = delete;
		Guard& operator=(const Guard&) = delete;
	};
};

} // namespace StoermelderPackOne
