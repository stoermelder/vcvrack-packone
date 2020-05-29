#pragma once

struct StripIdFixModule {
	const std::map<int, ModuleWidget*>* moduleIdMapping = NULL;

	virtual void dataFromJsonIdFix(const std::map<int, ModuleWidget*>& moduleIdMapping) {
		this->moduleIdMapping = &moduleIdMapping;
	}
};