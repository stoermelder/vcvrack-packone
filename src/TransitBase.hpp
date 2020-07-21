#pragma once
#include "plugin.hpp"
#include "digital.hpp"

namespace StoermelderPackOne {
namespace Transit {

template <int NUM_PRESETS>
struct TransitBase : Module {
	/** [Stored to JSON] */
	int panelTheme = 0;
	
	/** [Stored to JSON] */
	bool presetSlotUsed[NUM_PRESETS] = {false};
	/** [Stored to JSON] */
	std::vector<float> presetSlot[NUM_PRESETS];

	LongPressButton presetButton[NUM_PRESETS];

	int ctrlModuleId = -1;
	int ctrlOffset = 0;

	virtual Param* transitParam(int i) { return NULL; }
	virtual Light* transitLight(int i) { return NULL; }
};

template <int NUM_PRESETS>
struct TransitParamQuantity : ParamQuantity {
	TransitBase<NUM_PRESETS>* module;
	int i;

	std::string getDisplayValueString() override {
		return module->presetSlotUsed[i] ? "Used" : "Empty";
	}
	std::string getLabel() override {
		return string::f("Set #%d", module->ctrlOffset * NUM_PRESETS + i + 1);
	}
};

} // namespace Transit
} // namespace StoermelderPackOne