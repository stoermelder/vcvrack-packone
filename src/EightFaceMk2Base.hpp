#pragma once
#include "plugin.hpp"
#include "digital.hpp"
#include "StripIdFixModule.hpp"

namespace StoermelderPackOne {
namespace EightFaceMk2 {

enum class SLOT_CMD {
	LOAD,
	CLEAR,
	RANDOMIZE,
	COPY,
	PASTE
};

struct EightFaceMk2Slot {
	Param* param;
	Light* lights;
	bool* presetSlotUsed;
	std::vector<json_t*>* preset;
	LongPressButton* presetButton;
};

template <int NUM_PRESETS>
struct EightFaceMk2Base : Module, StripIdFixModule {
	/** [Stored to JSON] */
	int panelTheme = 0;
	
	/** [Stored to JSON] */
	bool presetSlotUsed[NUM_PRESETS] = {false};
	/** [Stored to JSON] */
	std::vector<json_t*> preset[NUM_PRESETS];

	LongPressButton presetButton[NUM_PRESETS];

	int ctrlModuleId = -1;
	int ctrlOffset = 0;
	bool ctrlWrite = false;

	EightFaceMk2Slot slot[NUM_PRESETS];

	virtual EightFaceMk2Slot* faceSlot(int i) { return NULL; }

	virtual void faceSlotCmd(SLOT_CMD cmd, int i) { }


	json_t* dataToJson() override {
		json_t* rootJ = json_object();
		json_object_set_new(rootJ, "panelTheme", json_integer(EightFaceMk2Base<NUM_PRESETS>::panelTheme));

		json_t* presetsJ = json_array();
		for (int i = 0; i < NUM_PRESETS; i++) {
			json_t* presetJ = json_object();
			json_object_set_new(presetJ, "slotUsed", json_boolean(EightFaceMk2Base<NUM_PRESETS>::presetSlotUsed[i]));
			if (EightFaceMk2Base<NUM_PRESETS>::presetSlotUsed[i]) {
				json_t* slotJ = json_array();
				for (size_t j = 0; j < EightFaceMk2Base<NUM_PRESETS>::preset[i].size(); j++) {
					json_array_append(slotJ, EightFaceMk2Base<NUM_PRESETS>::preset[i][j]);
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
			preset[presetIndex].clear();
			if (presetSlotUsed[presetIndex]) {
				json_t* slotJ = json_object_get(presetJ, "slot");
				json_t* vJ;
				size_t j;
				json_array_foreach(slotJ, j, vJ) {
					preset[presetIndex].push_back(json_deep_copy(vJ));
				}
			}
		}
	}
};

template <int NUM_PRESETS>
struct EightFaceMk2ParamQuantity : ParamQuantity {
	EightFaceMk2Base<NUM_PRESETS>* module;
	int id;

	std::string getDisplayValueString() override {
		return module->presetSlotUsed[id] ? "Used" : "Empty";
	}
	std::string getLabel() override {
		return string::f("Snapshot #%d", module->ctrlOffset * NUM_PRESETS + id + 1);
	}
};

template <int NUM_PRESETS>
struct EightFaceMk2LedButton : LEDButton {
	EightFaceMk2Base<NUM_PRESETS>* module;
	int id;
	bool eventConsumed = true;

	void onButton(const event::Button& e) override {
		if (e.action == GLFW_PRESS) {
			if (e.button == GLFW_MOUSE_BUTTON_LEFT && (e.mods & RACK_MOD_MASK) == GLFW_MOD_SHIFT) {
				module->faceSlotCmd(SLOT_CMD::LOAD, id);
				e.consume(this);
				eventConsumed = true;
			}
			else if (module->ctrlWrite && e.button == GLFW_MOUSE_BUTTON_RIGHT && (e.mods & RACK_MOD_MASK) == 0) {
				LEDButton::onButton(e);
				eventConsumed = false;
				extendContextMenu();
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

	void extendContextMenu() {
		// Hack for attaching additional menu items to parameter's context menu
		MenuOverlay* overlay = APP->scene->getFirstDescendantOfType<MenuOverlay>();
		if (!overlay) return;
		Widget* w = overlay->children.front();
		Menu* menu = dynamic_cast<Menu*>(w);
		if (!menu) return;

		struct SlotItem : MenuItem {
			EightFaceMk2Base<NUM_PRESETS>* module;
			int id;
			SLOT_CMD cmd;
			void onAction(const event::Action& e) override {
				module->faceSlotCmd(cmd, id);
			}
		};

		menu->addChild(new MenuSeparator);
		menu->addChild(construct<MenuLabel>(&MenuLabel::text, "Snapshot"));
		menu->addChild(construct<SlotItem>(&MenuItem::text, "Load", &MenuItem::rightText, RACK_MOD_SHIFT_NAME "+click", &SlotItem::module, module, &SlotItem::id, id, &SlotItem::cmd, SLOT_CMD::LOAD));
		menu->addChild(construct<SlotItem>(&MenuItem::text, "Clear", &SlotItem::module, module, &SlotItem::id, id, &SlotItem::cmd, SLOT_CMD::CLEAR));
		menu->addChild(construct<SlotItem>(&MenuItem::text, "Randomize and save", &SlotItem::module, module, &SlotItem::id, id, &SlotItem::cmd, SLOT_CMD::RANDOMIZE));
		menu->addChild(construct<SlotItem>(&MenuItem::text, "Copy", &SlotItem::module, module, &SlotItem::id, id, &SlotItem::cmd, SLOT_CMD::COPY));
		menu->addChild(construct<SlotItem>(&MenuItem::text, "Paste", &SlotItem::module, module, &SlotItem::id, id, &SlotItem::cmd, SLOT_CMD::PASTE));
	}
};

} // namespace EightFaceMk2
} // namespace StoermelderPackOne