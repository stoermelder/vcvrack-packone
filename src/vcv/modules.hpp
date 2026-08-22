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

// ---- Swappable module layer ----
// Every module operation in this namespace routes through ModuleAccess, so the whole layer
// is replaceable: production uses RackModuleAccess (the live Rack widget tree); the
// unit-test harness installs a mock registry via `moduleAccess` to assert on module
// effects without a RackWidget tree.
//
// The interface is deliberately split from the other two Rack accesses so each stays
// orthogonal and independently mockable: cables go through CableAccess (vcv_cables.hpp),
// the patch as a whole (selection/history/mouse) goes through PatchAccess
// (vcv_patch.hpp), and modules go through this one. A test only installs the access it
// actually asserts on and leaves the others at their production implementations.
//
// The design rule that keeps layer-1 JSON logic pure: addModule() takes a ModuleRef
// (plugin/model slugs) and an absolute pixel position, and returns the NEW module id
// (or -1), never a ModuleWidget*. Old→new id mapping is the caller's job.
// getModuleWidget() still returns a widget because the existing call sites genuinely
// need one.
// Must not be called from the engine thread (the module API is GUI-thread only).

struct ModuleRef {
	std::string pluginSlug;
	std::string modelSlug;
};

struct ModuleAccess {
	virtual ~ModuleAccess() {}

	// Lookup — the dominant operation across the codebase.
	virtual ModuleWidget* getModuleWidget(int64_t moduleId) const { return nullptr; }
	virtual std::vector<ModuleWidget*> getModuleWidgets() const { return {}; }
	virtual engine::Module* getModule(int64_t moduleId) const {
		ModuleWidget* mw = getModuleWidget(moduleId);
		return mw ? mw->module : nullptr;
	}

	// Lifecycle — was moduleFromJson + moduleToRack.
	virtual int64_t addModule(const ModuleRef& ref, Vec pos) { return -1; }
	virtual void removeModule(int64_t moduleId) {}
	virtual void setModuleWidgetPos(int64_t moduleId, Vec pos) {}

	// Preset application — wraps ModuleWidget::fromJson.
	virtual void applyPreset(int64_t moduleId, json_t* moduleJ) {}
	virtual json_t* toJson(int64_t moduleId) const { return nullptr; }
};

// The production implementation; bodies in the .cpp. Declared here — and `final` — so a
// release build's call sites see the concrete type and devirtualize. See cables.hpp.
struct RackModuleAccess final : ModuleAccess {
	ModuleWidget* getModuleWidget(int64_t moduleId) const override;
	std::vector<ModuleWidget*> getModuleWidgets() const override;
	int64_t addModule(const ModuleRef& ref, Vec pos) override;
	void removeModule(int64_t moduleId) override;
	void setModuleWidgetPos(int64_t moduleId, Vec pos) override;
	void applyPreset(int64_t moduleId, json_t* moduleJ) override;
	json_t* toJson(int64_t moduleId) const override;
};
// The shared production instance, defined in the .cpp.
extern RackModuleAccess rackModuleAccess;


// Debug builds keep the mockable seam; release resolves the access statically. See
// cables.hpp for why, and for the DEBUGPLUGIN contract.
#ifdef DEBUGPLUGIN
// Null by default -> the shared instance above is used. Tests point this at a mock.
extern ModuleAccess* moduleAccess;
ModuleAccess& moduleAccessFor();
#else
#define moduleAccessFor() ::StoermelderPackOne::vcv::rackModuleAccess
#endif


P1_UNUSED
static ModuleWidget* getModuleWidget(int64_t moduleId) {
	return moduleAccessFor().getModuleWidget(moduleId);
}

P1_UNUSED
static std::vector<ModuleWidget*> getModuleWidgets() {
	return moduleAccessFor().getModuleWidgets();
}

P1_UNUSED
static engine::Module* getModule(int64_t moduleId) {
	return moduleAccessFor().getModule(moduleId);
}

P1_UNUSED
static int64_t addModule(const ModuleRef& ref, Vec pos) {
	return moduleAccessFor().addModule(ref, pos);
}

P1_UNUSED
static void removeModule(int64_t moduleId) {
	moduleAccessFor().removeModule(moduleId);
}

P1_UNUSED
static void setModuleWidgetPos(int64_t moduleId, Vec pos) {
	moduleAccessFor().setModuleWidgetPos(moduleId, pos);
}

P1_UNUSED
static void applyPreset(int64_t moduleId, json_t* moduleJ) {
	moduleAccessFor().applyPreset(moduleId, moduleJ);
}

P1_UNUSED
static json_t* toJson(int64_t moduleId) {
	return moduleAccessFor().toJson(moduleId);
}


} // namespace vcv
} // namespace StoermelderPackOne

#undef P1_UNUSED
