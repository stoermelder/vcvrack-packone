#pragma once
#include "../plugin.hpp"

// Expands to the compiler's unused-attribute, or nothing where it isn't available.
#if defined(__GNUC__) || defined(__clang__)
#define P1_UNUSED __attribute__((unused))
#else
#define P1_UNUSED
#endif

namespace StoermelderPackOne {
namespace vcv {

// ---- Swappable scene layer ----
// Every rack-scene operation in this namespace routes through SceneAccess, so the whole
// layer is replaceable: production uses RackSceneAccess (defined in scene.cpp, backed by
// the live RackWidget); the unit-test harness installs a mock (a std::set<int64_t>) via
// `sceneAccess` to assert on selection effects without a RackWidget tree.
//
// Selection is expressed in module ids, not ModuleWidget*, for the same reason
// ModuleAccess::addModule returns an id: it keeps widgets out of every layer above and
// makes the mock a plain set of ids.
//
// getMousePos is deliberately NOT here — it is transient pointer input read inside drag
// handlers, not patch state (see the plan §4.3). The one caller that matters reads it
// directly at the call site and passes it into layoutSelection as `origin`, so the layout
// math stays testable without putting input state behind an interface.
// Must not be called from the engine thread (the scene API is GUI-thread only).
struct SceneAccess {
	virtual ~SceneAccess() {}

	// Selection
	virtual void select(int64_t moduleId) {}
	virtual void deselect(int64_t moduleId) {}
	virtual void deselectAll() {}
	virtual bool isSelected(int64_t moduleId) const { return false; }
	virtual std::vector<int64_t> getSelectedModuleIds() const { return {}; }
};


// The production implementation; bodies in the .cpp. Declared here — and `final` — so a
// release build's call sites see the concrete type and devirtualize. See cables.hpp.
struct RackSceneAccess final : SceneAccess {
	void select(int64_t moduleId) override;
	void deselect(int64_t moduleId) override;
	void deselectAll() override;
	bool isSelected(int64_t moduleId) const override;
	std::vector<int64_t> getSelectedModuleIds() const override;
};
// The shared production instance, defined in the .cpp.
extern RackSceneAccess rackSceneAccess;


// Debug builds keep the mockable seam; release resolves the access statically. See
// cables.hpp for why, and for the DEBUGPLUGIN contract.
#ifdef DEBUGPLUGIN
// Null by default -> the shared instance above is used. Tests point this at a mock.
extern SceneAccess* sceneAccess;
SceneAccess& sceneAccessFor();
#else
#define sceneAccessFor() ::StoermelderPackOne::vcv::rackSceneAccess
#endif


namespace scene {

P1_UNUSED
static void select(int64_t moduleId) {
	sceneAccessFor().select(moduleId);
}

P1_UNUSED
static void deselect(int64_t moduleId) {
	sceneAccessFor().deselect(moduleId);
}

P1_UNUSED
static void deselectAll() {
	sceneAccessFor().deselectAll();
}

P1_UNUSED
static bool isSelected(int64_t moduleId) {
	return sceneAccessFor().isSelected(moduleId);
}

P1_UNUSED
static std::vector<int64_t> getSelectedModuleIds() {
	return sceneAccessFor().getSelectedModuleIds();
}

} // namespace scene

} // namespace vcv
} // namespace StoermelderPackOne

#undef P1_UNUSED
