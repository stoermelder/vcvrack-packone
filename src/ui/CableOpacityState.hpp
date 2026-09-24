#pragma once
#include <rack.hpp>

namespace StoermelderPackOne {
namespace Rack {

// Manages the process-global settings::cableOpacity while any SpliceKit instance's viz
// overlay is active. The setting is a single float shared by every module, so — exactly
// as the old VisibilityTracker coordinated multiple owners of the cable container —
// several instances can be in viz mode at once, and only the LAST one to leave restores
// the opacity that existed before the FIRST one entered. Restoring the saved value
// (rather than a hard-coded 1.f) also preserves a user's own cableOpacity preference
// (e.g. 0.4) that happened to be active when viz mode first engaged.
struct CableOpacityState {
	std::set<rack::Widget*> owners;
	float previousOpacity = 1.f;

	// First owner snapshots the current opacity and zeroes it (cables invisible);
	// later owners just add themselves. Idempotent per owner.
	void hide(rack::Widget* owner) {
		if (!owner) return;
		if (owners.empty()) {
			previousOpacity = rack::settings::cableOpacity;
			rack::settings::cableOpacity = 0.f;
		}
		owners.insert(owner);
	}

	// Removes one owner; the LAST release restores the snapshotted opacity. A release
	// without a matching hide is a safe no-op, so a widget destructor can always call it.
	void release(rack::Widget* owner) {
		if (!owner) return;
		auto it = owners.find(owner);
		if (it == owners.end()) return;
		owners.erase(it);
		if (owners.empty()) {
			rack::settings::cableOpacity = previousOpacity;
		}
	}

	size_t ownerCount() const {
		return owners.size();
	}
};

// Function-local static rather than a class static with an out-of-line definition:
// the test build both links the plugin binary and #includes this .cpp, so a class
// static would exist twice — see crossPending()/getInstances() for the same rationale.
inline CableOpacityState& cableOpacityState() {
	static CableOpacityState state;
	return state;
}

} // namespace Rack
} // namespace StoermelderPackOne
