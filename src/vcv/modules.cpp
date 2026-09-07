#include "modules.hpp"

namespace StoermelderPackOne {
namespace vcv {

// The production module implementation, backed by the live Rack widget tree. Holds today's
// bodies of vcv_files.hpp's moduleFromJson/moduleToRack (and the duplicate copies in
// Strip.hpp): plugin::getModel + model->createModule() + APP->engine->addModule +
// createModuleWidget + APP->scene->rack->addModule + setModulePosForce, and mw->fromJson
// for applyPreset. LEFT/RIGHT placement is geometry — the caller computes the absolute
// `pos` (eventually via the layer-1 layout functions) and calls addModule with it.
ModuleWidget* RackModuleAccess::getModuleWidget(int64_t moduleId) const {
	return APP->scene->rack->getModule(moduleId);
}

std::vector<ModuleWidget*> RackModuleAccess::getModuleWidgets() const {
	return APP->scene->rack->getModules();
}

int64_t RackModuleAccess::addModule(const ModuleRef& ref, Vec pos) {
	plugin::Model* model = plugin::getModel(ref.pluginSlug, ref.modelSlug);
	if (!model) return -1;

	// Create Module
	engine::Module* addedModule = model->createModule();
	APP->engine->addModule(addedModule);

	// Create ModuleWidget
	ModuleWidget* mw = model->createModuleWidget(addedModule);
	assert(mw);
	mw->box.pos = pos;
	APP->scene->rack->addModule(mw);
	APP->scene->rack->setModulePosForce(mw, pos);
	return mw->module->id;
}

void RackModuleAccess::removeModule(int64_t moduleId) {
	ModuleWidget* mw = APP->scene->rack->getModule(moduleId);
	if (!mw) return;
	// Transfers ownership to the caller, who must delete it.
	APP->scene->rack->removeModule(mw);
	delete mw;
}

void RackModuleAccess::setModuleWidgetPos(int64_t moduleId, Vec pos) {
	ModuleWidget* mw = APP->scene->rack->getModule(moduleId);
	if (!mw) return;
	APP->scene->rack->setModulePosForce(mw, pos);
}

void RackModuleAccess::applyPreset(int64_t moduleId, json_t* moduleJ) {
	ModuleWidget* mw = APP->scene->rack->getModule(moduleId);
	if (!mw) return;
	mw->fromJson(moduleJ);
}

json_t* RackModuleAccess::toJson(int64_t moduleId) const {
	ModuleWidget* mw = APP->scene->rack->getModule(moduleId);
	return mw ? mw->toJson() : nullptr;
}

// The shared production instance; namespace-scope so no __cxa_guard is tested on access.
// In a release build this is what the moduleAccessFor() macro names directly.
RackModuleAccess rackModuleAccess;


#ifdef DEBUGPLUGIN
// One definition, external linkage: a mock installed in a test TU must be seen by
// code compiled into the dylib. See the declaration in the header.
ModuleAccess* moduleAccess = nullptr;
ModuleAccess& moduleAccessFor() {
	return moduleAccess ? *moduleAccess : rackModuleAccess;
}
#endif

} // namespace vcv
} // namespace StoermelderPackOne
