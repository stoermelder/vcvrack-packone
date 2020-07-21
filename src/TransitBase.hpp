#pragma once
#include "plugin.hpp"
#include "digital.hpp"

namespace StoermelderPackOne {
namespace Transit {

template <int NUM_PRESETS>
struct TransitBase : Module {
	/** [Stored to JSON] */
	bool presetSlotUsed[NUM_PRESETS] = {false};
	/** [Stored to JSON] */
	std::vector<float> presetSlot[NUM_PRESETS];

	LongPressButton presetButton[NUM_PRESETS];

	int ctrlModuleId = -1;

	virtual Param* transitParam(int i) { return NULL; }
	virtual Light* transitLight(int i) { return NULL; }
};

} // namespace Transit
} // namespace StoermelderPackOne