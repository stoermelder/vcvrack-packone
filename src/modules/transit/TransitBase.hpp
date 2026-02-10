#pragma once
#include "../../plugin.hpp"
#include "../../utils/digital.hpp"
#include "../../utils/StripIdFixModule.hpp"
#include "../../components/MenuLabelEx.hpp"

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
	SHIFT_FRONT,
	SET_FIRST,
	SET_LAST
};

enum class CTRLMODE {
	READ,
	AUTO,
	WRITE
};


template <int NUM_PRESETS>
struct TransitBase : Module, StripIdFixModule {
	struct Slot {
		TransitBase* owner = NULL;
		int index = -1;
		int indexParam = -1;
		int indexLight = -1;

		inline std::vector<float>* getPreset() {
			return &owner->preset[index];
		}

		inline LongPressButton* getPresetButton() {
			return &owner->presetButton[index];
		}

		inline Param* getParam() {
			return &owner->getParam(indexParam);
		}

		inline Light* getLights() {
			return &owner->lights[indexLight];
		}

		inline bool isUsed() {
			return owner->presetSlotUsed[index];
		}

		inline void setUsed(bool used) {
			owner->presetSlotUsed[index] = used;
		}

		inline std::string getLabel() {
			return owner->textLabel[index];
		}

		inline float getFadeTime() {
			return owner->fadeTime[index];
		}

		inline void setFadeTime(float time) {
			owner->fadeTime[index] = time;
		}

		void setLabel(const std::string& label) {
			owner->textLabel[index] = label;
		}

		bool isColorSet() {
			return owner->slotColorSet[index];
		}

		NVGcolor getColor() {
			return owner->slotColor[index];
		}

		void setColor(const NVGcolor& color, bool use = true) {
			owner->slotColor[index] = color;
			owner->slotColorSet[index] = use;
		}
	};

	/** [Stored to JSON] */
	int panelTheme = 0;
	
	/** [Stored to JSON] */
	bool presetSlotUsed[NUM_PRESETS] = {false};
	/** [Stored to JSON] */
	std::vector<float> preset[NUM_PRESETS];
	/** [Stored to JSON] */
	std::string textLabel[NUM_PRESETS];
	/** [Stored to JSON] */
	float fadeTime[NUM_PRESETS];
	/** [Stored to JSON] optional hex color for a slot. When false, no custom color is used */
	NVGcolor slotColor[NUM_PRESETS];
	bool slotColorSet[NUM_PRESETS];

	LongPressButton presetButton[NUM_PRESETS];

	int64_t ctrlModuleId = -1;
	/** [Stored to JSON] */
	int64_t ctrlUniqueId = -1;
	int ctrlOffset = 0;
	CTRLMODE ctrlMode = CTRLMODE::READ;

	Slot slot[NUM_PRESETS];

	virtual int sendSlotCmd(SLOT_CMD cmd, int i) { return -1; }

	json_t* dataToJson() override {
		json_t* rootJ = json_object();
		json_object_set_new(rootJ, "panelTheme", json_integer(TransitBase<NUM_PRESETS>::panelTheme));
		json_object_set_new(rootJ, "ctrlUniqueId", json_integer(ctrlUniqueId));

		json_t* presetsJ = json_array();
		for (int i = 0; i < NUM_PRESETS; i++) {
			json_t* presetJ = json_object();
			json_object_set_new(presetJ, "slotUsed", json_boolean(TransitBase<NUM_PRESETS>::presetSlotUsed[i]));
			json_object_set_new(presetJ, "textLabel", json_string(TransitBase<NUM_PRESETS>::textLabel[i].c_str()));
			json_object_set_new(presetJ, "fadeTime", json_real(TransitBase<NUM_PRESETS>::fadeTime[i]));
			if (TransitBase<NUM_PRESETS>::presetSlotUsed[i]) {
				json_t* slotJ = json_array();
				for (size_t j = 0; j < TransitBase<NUM_PRESETS>::preset[i].size(); j++) {
					json_t* vJ = json_real(TransitBase<NUM_PRESETS>::preset[i][j]);
					json_array_append_new(slotJ, vJ);
				}
				json_object_set(presetJ, "slot", slotJ);
			}
			if (TransitBase<NUM_PRESETS>::slotColorSet[i]) {
				json_object_set_new(presetJ, "color", json_string(color::toHexString(TransitBase<NUM_PRESETS>::slotColor[i]).c_str()));
			}
			json_array_append_new(presetsJ, presetJ);
		}
		json_object_set_new(rootJ, "presets", presetsJ);

		return rootJ;
	}

	void dataFromJson(json_t* rootJ) override {
		panelTheme = json_integer_value(json_object_get(rootJ, "panelTheme"));

		json_t* ctrlUniqueIdJ = json_object_get(rootJ, "ctrlUniqueId");
		ctrlUniqueId = ctrlUniqueIdJ ? json_integer_value(ctrlUniqueIdJ) : -2;

		json_t* presetsJ = json_object_get(rootJ, "presets");
		json_t* presetJ;
		size_t presetIndex;
		json_array_foreach(presetsJ, presetIndex, presetJ) {
			presetSlotUsed[presetIndex] = json_boolean_value(json_object_get(presetJ, "slotUsed"));
			json_t* textLabelJ = json_object_get(presetJ, "textLabel");
			if (textLabelJ) textLabel[presetIndex] = json_string_value(textLabelJ);
			json_t* fadeTimeJ = json_object_get(presetJ, "fadeTime");
			if (fadeTimeJ) fadeTime[presetIndex] = json_real_value(fadeTimeJ);
			json_t* colorJ = json_object_get(presetJ, "color");
			if (colorJ) {
				slotColor[presetIndex] = color::fromHexString(json_string_value(colorJ));
				slotColorSet[presetIndex] = true;
			}
			else {
				slotColorSet[presetIndex] = false;
			}
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
struct TransitParamQuantity : SwitchQuantity {
	TransitBase<NUM_PRESETS>* module;
	int id;

	std::string getDisplayValueString() override {
		return !module->textLabel[id].empty() ? module->textLabel[id] : (module->slot[id].isUsed() ? "Used" : "Empty");
	}
	std::string getLabel() override {
		return string::f("Snapshot #%d", module->ctrlOffset * NUM_PRESETS + id + 1);
	}
};

template <int NUM_PRESETS>
struct TransitLedButton : VCVButton {
	TransitBase<NUM_PRESETS>* module;
	int id;
	bool eventConsumed = true;

	void onButton(const event::Button& e) override {
		if (e.action == GLFW_PRESS) {
			if (e.button == GLFW_MOUSE_BUTTON_LEFT && (e.mods & RACK_MOD_MASK) == GLFW_MOD_SHIFT) {
				module->sendSlotCmd(SLOT_CMD::LOAD, id);
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
		struct SlotItem : MenuItem {
			TransitBase<NUM_PRESETS>* module;
			int id;
			SLOT_CMD cmd;
			void onAction(const event::Action& e) override {
				module->sendSlotCmd(cmd, id);
			}
		};

		if (module->ctrlMode == CTRLMODE::WRITE) {
			struct PasteItem : SlotItem {
				void step() override {
					int i = this->module->sendSlotCmd(SLOT_CMD::PASTE_PREVIEW, this->id);
					this->rightText = i >= 0 ? string::f("Slot %d", i + 1) : "";
					this->disabled = i < 0;
					SlotItem::step();
				}
			};

			menu->addChild(new MenuSeparator);
			menu->addChild(createSubmenuItem("Fade", "",
				[=](Menu* menu) {
					menu->addChild(createCheckMenuItem("Parameter/CV", "",
						[=]() {
							return module->fadeTime[id] == -1.f;
						},
						[=]() {
							module->fadeTime[id] = -1.f;
						}
					));

					struct FadeTimeLabel : MenuLabelEx {
						TransitBase<NUM_PRESETS>* module;
						int id;
						void step() override {
							this->rightText = CHECKMARK(module->fadeTime[id] != -1.f);
							MenuLabelEx::step();
						}
					};
					menu->addChild(construct<FadeTimeLabel>(&MenuLabel::text, "Custom", &FadeTimeLabel::module, module, &FadeTimeLabel::id, id));

					struct FadeTimeSlider : ui::Slider {
						struct FadeTimeQuantity : Quantity {
							TransitBase<NUM_PRESETS>* module;
							int id;
							FadeTimeQuantity(TransitBase<NUM_PRESETS>* module, int id) {
								this->module = module;
								this->id = id;
							}
							void setValue(float value) override {
								if (value == -1.f) {
									module->fadeTime[id] = value;
									return;
								}
								module->fadeTime[id] = math::clamp(value, 0.f, 1.f);
							}
							float getValue() override {
								return module->fadeTime[id];
							}
							float getDefaultValue() override {
								return -1.f;
							}
							std::string getLabel() override {
								return "Fade time";
							}
							std::string getString() override {
								if (getValue() == -1.f) return "Default";
								return Quantity::getString();
							}
							int getDisplayPrecision() override {
								return 3;
							}
							float getMaxValue() override {
								return 1.f;
							}
							float getMinValue() override {
								return 0.f;
							}
						};

						FadeTimeSlider(TransitBase<NUM_PRESETS>* module, int id) {
							box.size.x = 160.0f;
							quantity = new FadeTimeQuantity(module, id);
						}
						~FadeTimeSlider() {
							delete quantity;
						}
					};
					menu->addChild(new FadeTimeSlider(module, id));
				}
			));
			menu->addChild(new MenuSeparator);
			menu->addChild(construct<SlotItem>(&MenuItem::text, "Save", &MenuItem::rightText, "Click", &SlotItem::module, module, &SlotItem::id, id, &SlotItem::cmd, SLOT_CMD::SAVE));
			menu->addChild(construct<SlotItem>(&MenuItem::text, "Randomize and save", &SlotItem::module, module, &SlotItem::id, id, &SlotItem::cmd, SLOT_CMD::RANDOMIZE));
			menu->addChild(construct<SlotItem>(&MenuItem::text, "Load", &MenuItem::rightText, RACK_MOD_SHIFT_NAME "+Click", &SlotItem::module, module, &SlotItem::id, id, &SlotItem::cmd, SLOT_CMD::LOAD, &SlotItem::disabled, !module->slot[id].isUsed()));
			menu->addChild(construct<SlotItem>(&MenuItem::text, "Clear", &SlotItem::module, module, &SlotItem::id, id, &SlotItem::cmd, SLOT_CMD::CLEAR, &SlotItem::disabled, !module->slot[id].isUsed()));
			menu->addChild(construct<SlotItem>(&MenuItem::text, "Copy", &SlotItem::module, module, &SlotItem::id, id, &SlotItem::cmd, SLOT_CMD::COPY, &SlotItem::disabled, !module->slot[id].isUsed()));
			menu->addChild(construct<PasteItem>(&MenuItem::text, "Paste", &SlotItem::module, module, &SlotItem::id, id, &SlotItem::cmd, SLOT_CMD::PASTE));
			menu->addChild(new MenuSeparator);
			menu->addChild(construct<SlotItem>(&MenuItem::text, "Shift front", &SlotItem::module, module, &SlotItem::id, id, &SlotItem::cmd, SLOT_CMD::SHIFT_FRONT));
			menu->addChild(construct<SlotItem>(&MenuItem::text, "Shift back", &SlotItem::module, module, &SlotItem::id, id, &SlotItem::cmd, SLOT_CMD::SHIFT_BACK));
		}

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
		menu->addChild(construct<SlotItem>(&MenuItem::text, "Set as first", &SlotItem::module, module, &SlotItem::id, id, &SlotItem::cmd, SLOT_CMD::SET_FIRST));
		menu->addChild(construct<SlotItem>(&MenuItem::text, "Set as last", &SlotItem::module, module, &SlotItem::id, id, &SlotItem::cmd, SLOT_CMD::SET_LAST));
		menu->addChild(new MenuSeparator);
		menu->addChild(construct<LabelMenuItem>(&MenuItem::text, "Custom label", &LabelMenuItem::module, module, &LabelMenuItem::id, id));
		menu->addChild(createSubmenuItem("Custom color", "", [=](Menu* menu) {
			menu->addChild(createBoolPtrMenuItem("Use color", "", &module->slotColorSet[id]));
			menu->addChild(new MenuSeparator);
			std::vector<std::pair<NVGcolor, std::string>> presets = {
				{ color::YELLOW, "Yellow" },
				{ color::RED, "Red" },
				{ color::CYAN, "Cyan" },
				{ color::GREEN, "Green" },
				{ color::BLUE, "Blue" },
				{ color::WHITE, "White" }
			};
			Rack::appendColorSubmenuItems(menu, &module->slotColor[id], presets, true, true);
		}));
	}
};

} // namespace Transit
} // namespace StoermelderPackOne