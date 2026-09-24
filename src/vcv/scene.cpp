#include "scene.hpp"
#include "modules.hpp"

namespace StoermelderPackOne {
namespace vcv {

// The production scene implementation, backed by the live Rack widget tree. Module-id →
// widget lookups go through ModuleAccess (modules.hpp), never directly to the Rack widget
// tree, so a mock `moduleAccess` is usable underneath the *real* RackSceneAccess.
void RackSceneAccess::select(int64_t moduleId) {
	ModuleWidget* mw = moduleAccessFor().getModuleWidget(moduleId);
	if (mw) APP->scene->rack->select(mw, true);
}

void RackSceneAccess::deselect(int64_t moduleId) {
	ModuleWidget* mw = moduleAccessFor().getModuleWidget(moduleId);
	if (mw) APP->scene->rack->select(mw, false);
}

void RackSceneAccess::deselectAll() {
	APP->scene->rack->deselectAll();
}

bool RackSceneAccess::isSelected(int64_t moduleId) const {
	for (ModuleWidget* mw : APP->scene->rack->getSelected()) {
		if (mw->module->id == moduleId) return true;
	}
	return false;
}

std::vector<int64_t> RackSceneAccess::getSelectedModuleIds() const {
	std::vector<int64_t> ids;
	for (ModuleWidget* mw : APP->scene->rack->getSelected()) {
		ids.push_back(mw->module->id);
	}
	return ids;
}

// The shared production instance; namespace-scope so no __cxa_guard is tested on access.
// In a release build this is what the sceneAccessFor() macro names directly.
RackSceneAccess rackSceneAccess;


#ifdef DEBUGPLUGIN
// One definition, external linkage: a mock installed in a test TU must be seen by
// code compiled into the dylib. See the declaration in the header.
SceneAccess* sceneAccess = nullptr;
SceneAccess& sceneAccessFor() {
	return sceneAccess ? *sceneAccess : rackSceneAccess;
}
#endif

} // namespace vcv
} // namespace StoermelderPackOne
