#pragma once

struct StripIdFixModule {
	const std::map<int64_t, ModuleWidget*>* idFixMap = NULL;

	void idFixDataFromJson(const std::map<int64_t, ModuleWidget*>& moduleIdMapping) {
		this->idFixMap = &moduleIdMapping;
	}

	int64_t idFix(int64_t moduleId) {
		if (!this->idFixMap) return moduleId;
		auto it = this->idFixMap->find(moduleId);
		if (it == this->idFixMap->end()) return -1;
		return it->second->module->id;
	}
	
	bool idFixHasMap() {
		return this->idFixMap != NULL;
	}

	void idFixClearMap() {
		this->idFixMap = NULL;
	}
};