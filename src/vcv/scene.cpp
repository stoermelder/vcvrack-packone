#include "scene.hpp"
#include "modules.hpp"

namespace StoermelderPackOne {
namespace vcv {

// The production scene implementation, backed by the live Rack widget tree. Module-id →
// widget lookups go through ModuleAccess (modules.hpp), never directly to the Rack widget
// tree, so a mock `moduleAccess` is usable underneath the *real* RackSceneAccess.
struct RackSceneAccess : SceneAccess {
	void select(int64_t moduleId) override {
		ModuleWidget* mw = moduleAccessFor().getModuleWidget(moduleId);
		if (mw) APP->scene->rack->select(mw, true);
	}

	void deselect(int64_t moduleId) override {
		ModuleWidget* mw = moduleAccessFor().getModuleWidget(moduleId);
		if (mw) APP->scene->rack->select(mw, false);
	}

	void deselectAll() override {
		APP->scene->rack->deselectAll();
	}

	bool isSelected(int64_t moduleId) const override {
		for (ModuleWidget* mw : APP->scene->rack->getSelected()) {
			if (mw->module->id == moduleId) return true;
		}
		return false;
	}

	std::vector<int64_t> getSelectedModuleIds() const override {
		std::vector<int64_t> ids;
		for (ModuleWidget* mw : APP->scene->rack->getSelected()) {
			ids.push_back(mw->module->id);
		}
		return ids;
	}
};

// The single definition of the swappable scene layer's active access — external linkage,
// exactly one definition in this TU, so a mock installed in a test TU is seen by code
// compiled into the plugin dylib.
SceneAccess* sceneAccess = nullptr;

SceneAccess& sceneAccessFor() {
	static RackSceneAccess realAccess;
	return sceneAccess ? *sceneAccess : realAccess;
}

} // namespace vcv
} // namespace StoermelderPackOne
