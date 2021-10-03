#pragma once
#include "plugin.hpp"
#include "digital.hpp"
#include "helpers/StripIdFixModule.hpp"

namespace StoermelderPackOne {
namespace Transit {

enum class SLOT_CMD {
	LOAD,
	CLEAR,
	RANDOMIZE,
	COPY,
	PASTE_PREVIEW,
	PASTE,
	SAVE,
	SHIFT_BACK,
	SHIFT_FRONT
};

enum class CTRLMODE {
	READ,
	AUTO,
	WRITE
};

struct TransitSlot {
	Param* param;
	Light* lights;
	bool* presetSlotUsed;
	std::vector<float>* preset;
	LongPressButton* presetButton;
};

template <int NUM_PRESETS>
struct TransitBase : Module, StripIdFixModule {
	/** [Stored to JSON] */
	int panelTheme = 0;
	
	/** [Stored to JSON] */
	bool presetSlotUsed[NUM_PRESETS] = {false};
	/** [Stored to JSON] */
	std::vector<float> preset[NUM_PRESETS];
	/** [Stored to JSON] */
	std::string textLabel[NUM_PRESETS];

	LongPressButton presetButton[NUM_PRESETS];

	int64_t ctrlModuleId = -1;
	int ctrlOffset = 0;
	CTRLMODE ctrlMode = CTRLMODE::READ;

	TransitSlot slot[NUM_PRESETS];

	virtual TransitSlot* transitSlot(int i) { return NULL; }

	virtual int transitSlotCmd(SLOT_CMD cmd, int i) { return -1; }

	json_t* dataToJson() override {
		json_t* rootJ = json_object();
		json_object_set_new(rootJ, "panelTheme", json_integer(TransitBase<NUM_PRESETS>::panelTheme));

		json_t* presetsJ = json_array();
		for (int i = 0; i < NUM_PRESETS; i++) {
			json_t* presetJ = json_object();
			json_object_set_new(presetJ, "slotUsed", json_boolean(TransitBase<NUM_PRESETS>::presetSlotUsed[i]));
			json_object_set_new(presetJ, "textLabel", json_string(TransitBase<NUM_PRESETS>::textLabel[i].c_str()));
			if (TransitBase<NUM_PRESETS>::presetSlotUsed[i]) {
				json_t* slotJ = json_array();
				for (size_t j = 0; j < TransitBase<NUM_PRESETS>::preset[i].size(); j++) {
					json_t* vJ = json_real(TransitBase<NUM_PRESETS>::preset[i][j]);
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
			json_t* textLabelJ = json_object_get(presetJ, "textLabel");
			if (textLabelJ) textLabel[presetIndex] = json_string_value(textLabelJ);
			preset[presetIndex].clear();
			if (presetSlotUsed[presetIndex]) {
				json_t* slotJ = json_object_get(presetJ, "slot");
				json_t* vJ;
				size_t j;
				json_array_foreach(slotJ, j, vJ) {
					float v = json_real_value(vJ);
					preset[presetIndex].push_back(v);
				}
			}
		}
	}
};

template <int NUM_PRESETS>
struct TransitParamQuantity : ParamQuantity {
	TransitBase<NUM_PRESETS>* mymodule;
	int id;

	std::string getDisplayValueString() override {
		return !mymodule->textLabel[id].empty() ? mymodule->textLabel[id] : (mymodule->presetSlotUsed[id] ? "Used" : "Empty");
	}
	std::string getLabel() override {
		return !mymodule->textLabel[id].empty() ? "" : string::f("Snapshot #%d", mymodule->ctrlOffset * NUM_PRESETS + id + 1);
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
				module->transitSlotCmd(SLOT_CMD::LOAD, id);
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

	void appendContextMenu(Menu* menu) override {
		if (module->ctrlMode != CTRLMODE::WRITE) return;

		struct SlotItem : MenuItem {
			TransitBase<NUM_PRESETS>* module;
			int id;
			SLOT_CMD cmd;
			void onAction(const event::Action& e) override {
				module->transitSlotCmd(cmd, id);
			}
		};

		struct PasteItem : SlotItem {
			void step() override {
				int i = this->module->transitSlotCmd(SLOT_CMD::PASTE_PREVIEW, this->id);
				this->rightText = i >= 0 ? string::f("Slot %d", i + 1) : "";
				this->disabled = i < 0;
				SlotItem::step();
			}
		};

		struct LabelMenuItem : MenuItem {
			TransitBase<NUM_PRESETS>* module;
			int id;

			LabelMenuItem() {
				rightText = RIGHT_ARROW;
			}

			struct LabelField : ui::TextField {
				TransitBase<NUM_PRESETS>* module;
				int id;
				void onSelectKey(const event::SelectKey& e) override {
					if (e.action == GLFW_PRESS && e.key == GLFW_KEY_ENTER) {
						module->textLabel[id] = text;

						ui::MenuOverlay* overlay = getAncestorOfType<ui::MenuOverlay>();
						overlay->requestDelete();
						e.consume(this);
					}

					if (!e.getTarget()) {
						ui::TextField::onSelectKey(e);
					}
				}

				void step() override {
					// Keep selected
					APP->event->setSelectedWidget(this);
					TextField::step();
				}
			};

			struct ResetItem : ui::MenuItem {
				TransitBase<NUM_PRESETS>* module;
				int id;
				void onAction(const event::Action& e) override {
					module->textLabel[id] = "";
				}
			};

			Menu* createChildMenu() override {
				Menu* menu = new Menu;

				LabelField* labelField = new LabelField;
				labelField->placeholder = "Label";
				labelField->text = module->textLabel[id];
				labelField->box.size.x = 180;
				labelField->module = module;
				labelField->id = id;
				menu->addChild(labelField);

				ResetItem* resetItem = new ResetItem;
				resetItem->text = "Reset";
				resetItem->module = module;
				resetItem->id = id;
				menu->addChild(resetItem);

				return menu;
			}
		}; // struct LabelMenuItem

		menu->addChild(new MenuSeparator);
		menu->addChild(construct<MenuLabel>(&MenuLabel::text, "Snapshot"));
		menu->addChild(construct<SlotItem>(&MenuItem::text, "Save", &MenuItem::rightText, "Click", &SlotItem::module, module, &SlotItem::id, id, &SlotItem::cmd, SLOT_CMD::SAVE));
		menu->addChild(construct<SlotItem>(&MenuItem::text, "Randomize and save", &SlotItem::module, module, &SlotItem::id, id, &SlotItem::cmd, SLOT_CMD::RANDOMIZE));
		menu->addChild(construct<SlotItem>(&MenuItem::text, "Load", &MenuItem::rightText, RACK_MOD_SHIFT_NAME "+Click", &SlotItem::module, module, &SlotItem::id, id, &SlotItem::cmd, SLOT_CMD::LOAD, &SlotItem::disabled, !module->presetSlotUsed[id]));
		menu->addChild(construct<SlotItem>(&MenuItem::text, "Clear", &SlotItem::module, module, &SlotItem::id, id, &SlotItem::cmd, SLOT_CMD::CLEAR, &SlotItem::disabled, !module->presetSlotUsed[id]));
		menu->addChild(construct<SlotItem>(&MenuItem::text, "Copy", &SlotItem::module, module, &SlotItem::id, id, &SlotItem::cmd, SLOT_CMD::COPY, &SlotItem::disabled, !module->presetSlotUsed[id]));
		menu->addChild(construct<PasteItem>(&MenuItem::text, "Paste", &SlotItem::module, module, &SlotItem::id, id, &SlotItem::cmd, SLOT_CMD::PASTE));
		menu->addChild(new MenuSeparator);
		menu->addChild(construct<SlotItem>(&MenuItem::text, "Shift front", &SlotItem::module, module, &SlotItem::id, id, &SlotItem::cmd, SLOT_CMD::SHIFT_FRONT));
		menu->addChild(construct<SlotItem>(&MenuItem::text, "Shift back", &SlotItem::module, module, &SlotItem::id, id, &SlotItem::cmd, SLOT_CMD::SHIFT_BACK));
		menu->addChild(new MenuSeparator);
		menu->addChild(construct<LabelMenuItem>(&MenuItem::text, "Custom label", &LabelMenuItem::module, module, &LabelMenuItem::id, id));
	}
};

} // namespace Transit
} // namespace StoermelderPackOne