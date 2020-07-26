#pragma once

struct StripIdFixModule {
	const std::map<int, ModuleWidget*>* idFixMap = NULL;

	void idFixDataFromJson(const std::map<int, ModuleWidget*>& moduleIdMapping) {
		this->idFixMap = &moduleIdMapping;
	}

	int idFix(int moduleId) {
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