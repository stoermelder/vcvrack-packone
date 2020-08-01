#pragma once
#include "plugin.hpp"
#include "digital.hpp"
#include "StripIdFixModule.hpp"

namespace StoermelderPackOne {
namespace Transit {

template <int NUM_PRESETS>
struct TransitBase : Module, StripIdFixModule {
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

	virtual void transitLoadSlot(int i) { }


	json_t* dataToJson() override {
		json_t* rootJ = json_object();
		json_object_set_new(rootJ, "panelTheme", json_integer(TransitBase<NUM_PRESETS>::panelTheme));

		json_t* presetsJ = json_array();
		for (int i = 0; i < NUM_PRESETS; i++) {
			json_t* presetJ = json_object();
			json_object_set_new(presetJ, "slotUsed", json_boolean(TransitBase<NUM_PRESETS>::presetSlotUsed[i]));
			if (TransitBase<NUM_PRESETS>::presetSlotUsed[i]) {
				json_t* slotJ = json_array();
				for (size_t j = 0; j < TransitBase<NUM_PRESETS>::presetSlot[i].size(); j++) {
					json_t* vJ = json_real(TransitBase<NUM_PRESETS>::presetSlot[i][j]);
					json_array_append_new(slotJ, vJ);
				}
				json_object_set(presetJ, "slot", slotJ);
			}
			json_array_append_new(presetsJ, presetJ);
		}
		json_object_set_new(rootJ, "presets", presetsJ);

		return rootJ;
	}

	void dataFromJson(json_t* rootJ) override {
		panelTheme = json_integer_value(json_object_get(rootJ, "panelTheme"));

		json_t* presetsJ = json_object_get(rootJ, "presets");
		json_t* presetJ;
		size_t presetIndex;
		json_array_foreach(presetsJ, presetIndex, presetJ) {
			presetSlotUsed[presetIndex] = json_boolean_value(json_object_get(presetJ, "slotUsed"));
			presetSlot[presetIndex].clear();
			if (presetSlotUsed[presetIndex]) {
				json_t* slotJ = json_object_get(presetJ, "slot");
				json_t* vJ;
				size_t j;
				json_array_foreach(slotJ, j, vJ) {
					float v = json_real_value(vJ);
					presetSlot[presetIndex].push_back(v);
				}
			}
		}
	}
};

template <int NUM_PRESETS>
struct TransitParamQuantity : ParamQuantity {
	TransitBase<NUM_PRESETS>* module;
	int i;

	std::string getDisplayValueString() override {
		return module->presetSlotUsed[i] ? "Used" : "Empty";
	}
	std::string getLabel() override {
		return string::f("Scene #%d", module->ctrlOffset * NUM_PRESETS + i + 1);
	}
};

template <int NUM_PRESETS>
struct TransitLedButton : LEDButton {
	TransitBase<NUM_PRESETS>* module;
	int id;
	bool eventConsumed = true;

	void onButton(const event::Button& e) override {
		if (e.action == GLFW_PRESS) {
			if (e.button == GLFW_MOUSE_BUTTON_LEFT && (e.mods & RACK_MOD_MASK) == GLFW_MOD_SHIFT) {
				module->transitLoadSlot(id);
				e.consume(this);
				eventConsumed = true;
			}
			else {
				LEDButton::onButton(e);
				eventConsumed = false;
			}
		}
	}

	void onDragStart(const event::DragStart& e) override {
		if (eventConsumed) {
			eventConsumed = false;
			return;
		}
		LEDButton::onDragStart(e);
	}
};

} // namespace Transit
} // namespace StoermelderPackOne