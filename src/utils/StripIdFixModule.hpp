#pragma once

namespace StoermelderPackOne {

// Old→new module id mapping, installed for the duration of a STRIP/selection load so a
// module's dataFromJson can rewrite the module ids it stored (mappings, restrictions, ...).
//
// The map is old id → new id, not old id → ModuleWidget*: idFix only ever read
// `mw->module->id` from it, and keeping widgets out of the interface is what lets the loader
// stay on ModuleAccess (which hands out ids, never widgets). See var/vcv_files_refactoring_plan.md §4.6.
struct StripIdFixModule {
	const std::map<int64_t, int64_t>* idFixMap = NULL;

	void idFixDataFromJson(const std::map<int64_t, int64_t>& moduleIdMapping) {
		this->idFixMap = &moduleIdMapping;
	}

	int64_t idFix(int64_t moduleId) {
		if (!this->idFixMap) return moduleId;
		auto it = this->idFixMap->find(moduleId);
		if (it == this->idFixMap->end()) return -1;
		return it->second;
	}

	bool idFixHasMap() {
		return this->idFixMap != NULL;
	}

	void idFixClearMap() {
		this->idFixMap = NULL;
	}
};

} // namespace StoermelderPackOne