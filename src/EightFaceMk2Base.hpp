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
	PASTE_PREVIEW,
	PASTE,
	SAVE,
	SHIFT_BACK,
	SHIFT_FRONT
};

enum class CTRLMODE {
	READ,
	WRITE
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
	/** [Stored to JSON] */
	std::string textLabel[NUM_PRESETS];

	LongPressButton presetButton[NUM_PRESETS];

	int ctrlModuleId = -1;
	int ctrlOffset = 0;
	/** Current operating mode */
	CTRLMODE ctrlMode = CTRLMODE::READ;

	EightFaceMk2Slot slot[NUM_PRESETS];

	virtual EightFaceMk2Slot* faceSlot(int i) { return NULL; }

	virtual int faceSlotCmd(SLOT_CMD cmd, int i) { return -1; }


	json_t* dataToJson() override {
		json_t* rootJ = json_object();
		json_object_set_new(rootJ, "panelTheme", json_integer(EightFaceMk2Base<NUM_PRESETS>::panelTheme));

		json_t* presetsJ = json_array();
		for (int i = 0; i < NUM_PRESETS; i++) {
			json_t* presetJ = json_object();
			json_object_set_new(presetJ, "slotUsed", json_boolean(EightFaceMk2Base<NUM_PRESETS>::presetSlotUsed[i]));
			json_object_set_new(presetJ, "textLabel", json_string(EightFaceMk2Base<NUM_PRESETS>::textLabel[i].c_str()));
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
			json_t* textLabelJ = json_object_get(presetJ, "textLabel");
			if (textLabelJ) textLabel[presetIndex] = json_string_value(textLabelJ);
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
	EightFaceMk2Base<NUM_PRESETS>* mymodule;
	int id;

	std::string getDisplayValueString() override {
		return !mymodule->textLabel[id].empty() ? mymodule->textLabel[id] : (mymodule->presetSlotUsed[id] ? "Used" : "Empty");
	}
	std::string getLabel() override {
		return !mymodule->textLabel[id].empty() ? "" : string::f("Snapshot #%d", mymodule->ctrlOffset * NUM_PRESETS + id + 1);
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
			else if (module->ctrlMode == CTRLMODE::WRITE && e.button == GLFW_MOUSE_BUTTON_RIGHT && (e.mods & RACK_MOD_MASK) == 0) {
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

		struct PasteItem : SlotItem {
			void step() override {
				int i = this->module->faceSlotCmd(SLOT_CMD::PASTE_PREVIEW, this->id);
				this->rightText = i >= 0 ? string::f("Slot %d", i + 1) : "";
				this->disabled = i < 0;
				SlotItem::step();
			}
		};

		struct LabelMenuItem : MenuItem {
			EightFaceMk2Base<NUM_PRESETS>* module;
			int id;

			LabelMenuItem() {
				rightText = RIGHT_ARROW;
			}

			struct LabelField : ui::TextField {
				EightFaceMk2Base<NUM_PRESETS>* module;
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
				EightFaceMk2Base<NUM_PRESETS>* module;
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

} // namespace EightFaceMk2
} // namespace StoermelderPackOne