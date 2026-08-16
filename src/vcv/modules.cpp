#include "modules.hpp"

namespace StoermelderPackOne {
namespace vcv {

// The production module implementation, backed by the live Rack widget tree. Holds today's
// bodies of vcv_files.hpp's moduleFromJson/moduleToRack (and the duplicate copies in
// Strip.hpp): plugin::getModel + model->createModule() + APP->engine->addModule +
// createModuleWidget + APP->scene->rack->addModule + setModulePosForce, and mw->fromJson
// for applyPreset. LEFT/RIGHT placement is geometry — the caller computes the absolute
// `pos` (eventually via the layer-1 layout functions) and calls addModule with it.
struct RackModuleAccess : ModuleAccess {
	ModuleWidget* getModuleWidget(int64_t moduleId) const override {
		return APP->scene->rack->getModule(moduleId);
	}

	std::vector<ModuleWidget*> getModuleWidgets() const override {
		return APP->scene->rack->getModules();
	}

	int64_t addModule(const ModuleRef& ref, Vec pos) override {
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

	void removeModule(int64_t moduleId) override {
		ModuleWidget* mw = APP->scene->rack->getModule(moduleId);
		if (!mw) return;
		// Transfers ownership to the caller, who must delete it.
		APP->scene->rack->removeModule(mw);
		delete mw;
	}

	void setModuleWidgetPos(int64_t moduleId, Vec pos) override {
		ModuleWidget* mw = APP->scene->rack->getModule(moduleId);
		if (!mw) return;
		APP->scene->rack->setModulePosForce(mw, pos);
	}

	void applyPreset(int64_t moduleId, json_t* moduleJ) override {
		ModuleWidget* mw = APP->scene->rack->getModule(moduleId);
		if (!mw) return;
		mw->fromJson(moduleJ);
	}

	json_t* toJson(int64_t moduleId) const override {
		ModuleWidget* mw = APP->scene->rack->getModule(moduleId);
		return mw ? mw->toJson() : nullptr;
	}
};

// The single definition of the swappable module layer's active access — same reasoning as
// cableAccess above: external linkage, exactly one definition in this TU. A test that
// installs a mock `moduleAccess` in its own TU will not be seen by code compiled into the
// plugin dylib if this were `static` in the header instead.
ModuleAccess* moduleAccess = nullptr;

ModuleAccess& moduleAccessFor() {
	static RackModuleAccess rackAccess;
	return moduleAccess ? *moduleAccess : rackAccess;
}

} // namespace vcv
} // namespace StoermelderPackOne
