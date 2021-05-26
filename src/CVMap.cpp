#include "plugin.hpp"
#include "MapModuleBase.hpp"
#include "CVMap.hpp"
#include "components/ParamWidgetContextExtender.hpp"
#include <chrono>

namespace StoermelderPackOne {
namespace CVMap {

static const int MAX_CHANNELS = 32;

struct CVMapModule : CVMapModuleBase<MAX_CHANNELS> {
	enum ParamIds {
		NUM_PARAMS
	};
	enum InputIds {
		POLY_INPUT1,
		POLY_INPUT2,
		NUM_INPUTS
	};
	enum OutputIds {
		NUM_OUTPUTS
	};
	enum LightIds {
		ENUMS(CHANNEL_LIGHTS1, 16),
		ENUMS(CHANNEL_LIGHTS2, 16),
		NUM_LIGHTS
	};

	/** [Stored to JSON] */
	int panelTheme = 0;
	/** [Stored to JSON] */
	bool audioRate;
	/** [Stored to JSON] */
	bool locked;
	/** [Stored to JSON] */
	int mapInput[MAX_CHANNELS];

	struct InputConfig {
		int channels;
		/** [Stored to JSON] */
		bool hideUnused;
		/** [Stored to JSON] */
		std::string label[16];
	};
	InputConfig inputConfig[2];

	dsp::ClockDivider processDivider;
	dsp::ClockDivider lightDivider;

	ScaledMapParam<float> mapParam[MAX_CHANNELS];

	Module* expCtx = NULL;

	CVMapModule() {
		panelTheme = pluginSettings.panelThemeDefault;
		config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);
		for (int i = 0; i < MAX_CHANNELS; i++) {
			paramHandles[i].text = string::f("CV-MAP Slot %02d", i + 1);
			mapParam[i].setLimits(0.f, 1.f, std::numeric_limits<float>::infinity());
		}
		processDivider.setDivision(32);
		lightDivider.setDivision(1024);
		onReset();
	}

	void onReset() override {
		CVMapModuleBase<MAX_CHANNELS>::onReset();
		audioRate = false;
		locked = false;
		for (size_t i = 0; i < MAX_CHANNELS; i++) {
			mapParam[i].reset();
			mapInput[i] = i;
		}
		for (size_t i = 0; i < 2; i++) {
			inputConfig[i].hideUnused = true;
			for (size_t j = 0; j < 16; j++) {
				inputConfig[i].label[j] = "";
			}
		}
	}

	void process(const ProcessArgs& args) override {
		if (audioRate || processDivider.process()) {
			float deltaTime = args.sampleTime * (audioRate ? 1.f : float(processDivider.getDivision()));

			// Step channels
			for (int i = 0; i < mapLen; i++) {
				ParamQuantity* paramQuantity = getParamQuantity(i);
				if (paramQuantity == NULL) continue;
				mapParam[i].setParamQuantity(paramQuantity);

				Input in = mapInput[i] < 16 ? inputs[POLY_INPUT1] : inputs[POLY_INPUT2];
				if (!in.isConnected()) continue;
				int c = mapInput[i] % 16;
				if (c >= in.getChannels()) continue;

				float t = in.getVoltage(c);
				if (bipolarInput) t += 5.f;
				t /= 10.f;

				// Set a new value for the mapped parameter
				mapParam[i].setValue(t);

				// Apply value on the mapped parameter (respecting slew and scale)
				mapParam[i].process(deltaTime, lockParameterChanges);
			}
		}

		// Set channel lights infrequently
		if (lightDivider.process()) {
			for (int c = 0; c < 16; c++) {
				bool active = (c < inputs[POLY_INPUT1].getChannels());
				lights[CHANNEL_LIGHTS1 + c].setBrightness(active);
			}
			for (int c = 0; c < 16; c++) {
				bool active = (c < inputs[POLY_INPUT2].getChannels());
				lights[CHANNEL_LIGHTS2 + c].setBrightness(active);
			}

			inputConfig[0].channels = inputs[POLY_INPUT1].getChannels();
			inputConfig[1].channels = inputs[POLY_INPUT2].getChannels();
		}
		
		CVMapModuleBase<MAX_CHANNELS>::process(args);

		// Expander
		bool expCtxFound = false;
		Module* exp = rightExpander.module;
		for (int i = 0; i < 2; i++) {
			if (!exp) break;
			if (exp->model == modelCVMapCtx && !expCtxFound) {
				expCtx = exp;
				expCtxFound = true;
				exp = exp->rightExpander.module;
				continue;
			}
			break;
		}
		if (!expCtxFound) {
			expCtx = NULL;
		}
	}

	int getEmptySlotId() {
		int i = -1;
		// Find next incomplete map
		while (++i < MAX_CHANNELS) {
			if (paramHandles[i].moduleId < 0)
				return i;
		}
		return -1;
	}

	json_t* dataToJson() override {
		json_t* rootJ = CVMapModuleBase<MAX_CHANNELS>::dataToJson();
		json_object_set_new(rootJ, "panelTheme", json_integer(panelTheme));
		json_object_set_new(rootJ, "audioRate", json_boolean(audioRate));
		json_object_set_new(rootJ, "locked", json_boolean(locked));

		json_t* inputConfigsJ = json_array();
		for (size_t i = 0; i < 2; i++) {
			json_t* inputConfigJ = json_object();
			json_object_set_new(inputConfigJ, "hideUnused", json_boolean(inputConfig[i].hideUnused));
			json_t* labelJ = json_array();
			for (size_t j = 0; j < 16; j++) {
				json_array_append_new(labelJ, json_string(inputConfig[i].label[j].c_str()));
			}
			json_object_set_new(inputConfigJ, "label", labelJ);
			json_array_append_new(inputConfigsJ, inputConfigJ);
		}
		json_object_set_new(rootJ, "inputConfig", inputConfigsJ);

		return rootJ;
	}

	void dataToJsonMap(json_t* mapJ, int index) override {
		json_object_set_new(mapJ, "input", json_integer(mapInput[index]));
		json_object_set_new(mapJ, "slew", json_real(mapParam[index].getSlew()));
		json_object_set_new(mapJ, "min", json_real(mapParam[index].getMin()));
		json_object_set_new(mapJ, "max", json_real(mapParam[index].getMax()));
	}

	void dataFromJson(json_t* rootJ) override {
		CVMapModuleBase<MAX_CHANNELS>::dataFromJson(rootJ);
		panelTheme = json_integer_value(json_object_get(rootJ, "panelTheme"));

		json_t* audioRateJ = json_object_get(rootJ, "audioRate");
		if (audioRateJ) audioRate = json_boolean_value(audioRateJ);
		json_t* lockedJ = json_object_get(rootJ, "locked");
		if (lockedJ) locked = json_boolean_value(lockedJ);

		json_t* inputConfigsJ = json_object_get(rootJ, "inputConfig");
		if (inputConfigsJ) {
			size_t i;
			json_t* inputConfigJ;
			json_array_foreach(inputConfigsJ, i, inputConfigJ) {
				inputConfig[i].hideUnused = json_boolean_value(json_object_get(inputConfigJ, "hideUnused"));
				json_t* labelJ = json_object_get(inputConfigJ, "label");
				size_t j;
				json_t* lJ;
				json_array_foreach(labelJ, j, lJ) {
					inputConfig[i].label[j] = json_string_value(lJ);
				}
			}
		}
	}

	void dataFromJsonMap(json_t* mapJ, int index) override {
		json_t* inputJ = json_object_get(mapJ, "input");
		json_t* slewJ = json_object_get(mapJ, "slew");
		json_t* minJ = json_object_get(mapJ, "min");
		json_t* maxJ = json_object_get(mapJ, "max");
		if (inputJ) mapInput[index] = json_integer_value(inputJ);
		if (slewJ) mapParam[index].setSlew(json_real_value(slewJ));
		if (minJ) mapParam[index].setMin(json_real_value(minJ));
		if (maxJ) mapParam[index].setMax(json_real_value(maxJ));
	}
};


struct CVMapPort : StoermelderPort {
	int i;

	void onButton(const event::Button& e) override {
		if (e.action == GLFW_PRESS && e.button == GLFW_MOUSE_BUTTON_RIGHT) {
			createContextMenu();
			e.consume(this);
		}
		else {
			StoermelderPort::onButton(e);
		}
	}

	void createContextMenu() {
		CVMapModule* module = dynamic_cast<CVMapModule*>(this->module);

		struct LabelMenuItem : MenuItem {
			CVMapModule* module;
			int i, j;
			LabelMenuItem() {
				rightText = RIGHT_ARROW;
			}
			struct LabelField : ui::TextField {
				CVMapModule* module;
				int i, j;
				void onSelectKey(const event::SelectKey& e) override {
					if (e.action == GLFW_PRESS && e.key == GLFW_KEY_ENTER) {
						module->inputConfig[i].label[j] = text;
						ui::MenuOverlay* overlay = getAncestorOfType<ui::MenuOverlay>();
						overlay->requestDelete();
						e.consume(this);
					}
					if (!e.getTarget()) {
						ui::TextField::onSelectKey(e);
					}
				}
			};

			struct ResetItem : ui::MenuItem {
				CVMapModule* module;
				int i, j;
				void onAction(const event::Action& e) override {
					module->inputConfig[i].label[j] = "";
				}
			};

			Menu* createChildMenu() override {
				Menu* menu = new Menu;
				menu->addChild(construct<MenuLabel>(&MenuLabel::text, "Custom label"));
				LabelField* labelField = new LabelField;
				labelField->text = module->inputConfig[i].label[j];
				labelField->box.size.x = 180;
				labelField->module = module;
				labelField->i = i;
				labelField->j = j;
				menu->addChild(labelField);
				menu->addChild(construct<ResetItem>(&MenuItem::text, "Reset", &ResetItem::module, module, &ResetItem::i, i, &ResetItem::j, j));
				return menu;
			}
		}; // struct LabelMenuItem

		struct HideUnusedItem : MenuItem {
			CVMapModule* module;
			int i;
			void onAction(const event::Action& e) override {
				module->inputConfig[i].hideUnused ^= true;
			}
			void step() override {
				rightText = CHECKMARK(module->inputConfig[i].hideUnused);
				MenuItem::step();
			}
		}; // struct HideUnusedItem

		struct DisconnectItem : MenuItem {
			PortWidget* pw;
			void onAction(const event::Action& e) override {
				CableWidget* cw = APP->scene->rack->getTopCable(pw);
				if (cw) {
					// history::CableRemove
					history::CableRemove* h = new history::CableRemove;
					h->setCable(cw);
					APP->history->push(h);

					APP->scene->rack->removeCable(cw);
					delete cw;
				}
			}
		}; // struct DisconnectItem

		Menu* menu = createMenu();
		menu->addChild(construct<MenuLabel>(&MenuLabel::text, string::f("Port %i", i + 1)));
		menu->addChild(construct<DisconnectItem>(&MenuItem::text, "Disconnect", &DisconnectItem::pw, this));
		menu->addChild(new MenuSeparator());
		for (size_t j = 0; j < 16; j++) {
			menu->addChild(construct<LabelMenuItem>(&MenuItem::text, string::f("Channel %i", j + 1), &LabelMenuItem::module, module, &LabelMenuItem::i, i, &LabelMenuItem::j, j));
		}
		menu->addChild(construct<HideUnusedItem>(&MenuItem::text, "Hide unused", &HideUnusedItem::module, module, &HideUnusedItem::i, i));
	}
}; // struct CVMapPort


struct InputChannelMenuItem : MenuItem {
	struct InputChannelItem : MenuItem {
		CVMapModule* module;
		ParamQuantity* pq = NULL;
		int id;
		int channel;
		void onAction(const event::Action& e) override {
			if (pq) module->learnParam(id, pq->module->id, pq->paramId);
			module->mapInput[id] = channel;
		}
		void step() override {
			rightText = CHECKMARK(!pq && module->mapInput[id] == channel);
			MenuItem::step();
		}
	}; // struct InputChannelItem

	CVMapModule* module;
	ParamQuantity* pq = NULL;
	int id;
	InputChannelMenuItem() {
		rightText = RIGHT_ARROW;
	}
	Menu* createChildMenu() override {
		Menu* menu = new Menu;
		for (int i = 0; i < 2; i++) {
			for (int j = 0; j < 16; j++) {
				if (module->inputConfig[i].hideUnused && j == module->inputConfig[i].channels) break;
				std::string text = module->inputConfig[i].label[j] != "" ? module->inputConfig[i].label[j] :
					string::f("Input %02d - Port %i Channel %i", i * 16 + j + 1, i + 1, j + 1);
				menu->addChild(construct<InputChannelItem>(&MenuItem::text, text, &InputChannelItem::module, module, &InputChannelItem::id, id, &InputChannelItem::channel, i * 16 + j, &InputChannelItem::pq, pq));
			}
		}
		return menu;
	}
}; // struct InputChannelMenuItem


struct CVMapChoice : MapModuleChoice<MAX_CHANNELS, CVMapModule> {
	void appendContextMenu(Menu* menu) override {
		menu->addChild(new MenuSeparator);
		menu->addChild(construct<InputChannelMenuItem>(&MenuItem::text, "Input channel", &InputChannelMenuItem::module, module, &InputChannelMenuItem::id, id));
		menu->addChild(new MapSlewSlider<>(&module->mapParam[id]));
		menu->addChild(construct<MenuLabel>(&MenuLabel::text, "Scaling"));
		menu->addChild(construct<MapScalingInputLabel<>>(&MenuLabel::text, "Input", &MapScalingInputLabel<>::p, &module->mapParam[id]));
		menu->addChild(construct<MapScalingOutputLabel<>>(&MenuLabel::text, "Parameter range", &MapScalingOutputLabel<>::p, &module->mapParam[id]));
		menu->addChild(new MapMinSlider<>(&module->mapParam[id]));
		menu->addChild(new MapMaxSlider<>(&module->mapParam[id]));
		menu->addChild(construct<MapPresetMenuItem<>>(&MenuItem::text, "Presets", &MapPresetMenuItem<>::p, &module->mapParam[id]));
	}

	std::string getSlotPrefix() override {
		return string::f("In%02d ", module->mapInput[id] + 1);
	}
}; // struct CVMapChoice


struct CVMapWidget : ThemedModuleWidget<CVMapModule>, ParamWidgetContextExtender {
	CVMapModule* module;
	CVMapCtxBase* expCtx;

	CVMapWidget(CVMapModule* module)
		: ThemedModuleWidget<CVMapModule>(module, "CVMap") {
		setModule(module);
		this->module = module;

		addChild(createWidget<StoermelderBlackScrew>(Vec(RACK_GRID_WIDTH, 0)));
		addChild(createWidget<StoermelderBlackScrew>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, 0)));
		addChild(createWidget<StoermelderBlackScrew>(Vec(RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));
		addChild(createWidget<StoermelderBlackScrew>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));

		CVMapPort* input1 = createInputCentered<CVMapPort>(Vec(26.9f, 60.8f), module, CVMapModule::POLY_INPUT1);
		input1->i = 0;
		addInput(input1);
		CVMapPort* input2 = createInputCentered<CVMapPort>(Vec(123.1f, 60.8f), module, CVMapModule::POLY_INPUT2);
		input2->i = 1;
		addInput(input2);

		PolyLedWidget<>* w0 = createWidgetCentered<PolyLedWidget<>>(Vec(54.2f, 60.8f));
		w0->setModule(module, CVMapModule::CHANNEL_LIGHTS1);
		addChild(w0);

		PolyLedWidget<>* w1 = createWidgetCentered<PolyLedWidget<>>(Vec(95.8f, 60.8f));
		w1->setModule(module, CVMapModule::CHANNEL_LIGHTS2);
		addChild(w1);

		typedef MapModuleDisplay<MAX_CHANNELS, CVMapModule, CVMapChoice> TMapDisplay;
		TMapDisplay* mapWidget = createWidget<TMapDisplay>(Vec(10.6f, 81.5f));
		mapWidget->box.size = Vec(128.9f, 261.7f);
		mapWidget->setModule(module);
		addChild(mapWidget);
	}

	void step() override {
		ParamWidgetContextExtender::step();
		ThemedModuleWidget<CVMapModule>::step();

		if (module) {
			// CTX-expander
			if (module->expCtx != (Module*)expCtx) {
				expCtx = dynamic_cast<CVMapCtxBase*>(module->expCtx);
			}
		}
	}

	void appendContextMenu(Menu* menu) override {
		ThemedModuleWidget<CVMapModule>::appendContextMenu(menu);
		CVMapModule* module = dynamic_cast<CVMapModule*>(this->module);
		assert(module);

		struct LockItem : MenuItem {
			CVMapModule* module;
			void onAction(const event::Action& e) override {
				module->lockParameterChanges ^= true;
			}
			void step() override {
				rightText = module->lockParameterChanges ? "Locked" : "Unlocked";
				MenuItem::step();
			}
		};

		struct UniBiItem : MenuItem {
			CVMapModule* module;
			void onAction(const event::Action& e) override {
				module->bipolarInput ^= true;
			}
			void step() override {
				rightText = module->bipolarInput ? "-5V..5V" : "0V..10V";
				MenuItem::step();
			}
		};

		struct AudioRateItem : MenuItem {
			CVMapModule* module;
			void onAction(const event::Action& e) override {
				module->audioRate ^= true;
			}
			void step() override {
				rightText = module->audioRate ? "✔" : "";
				MenuItem::step();
			}
		};

		struct TextScrollItem : MenuItem {
			CVMapModule* module;
			void onAction(const event::Action& e) override {
				module->textScrolling ^= true;
			}
			void step() override {
				rightText = module->textScrolling ? "✔" : "";
				MenuItem::step();
			}
		};

		struct MappingIndicatorHiddenItem : MenuItem {
			CVMapModule* module;
			void onAction(const event::Action& e) override {
				module->mappingIndicatorHidden ^= true;
			}
			void step() override {
				rightText = module->mappingIndicatorHidden ? "✔" : "";
				MenuItem::step();
			}
		};

		struct LockedItem : MenuItem {
			CVMapModule* module;
			void onAction(const event::Action& e) override {
				module->locked ^= true;
			}
			void step() override {
				rightText = module->locked ? "✔" : "";
				MenuItem::step();
			}
		};

		menu->addChild(new MenuSeparator());
		menu->addChild(construct<LockItem>(&MenuItem::text, "Parameter changes", &LockItem::module, module));
		menu->addChild(construct<UniBiItem>(&MenuItem::text, "Signal input", &UniBiItem::module, module));
		menu->addChild(construct<AudioRateItem>(&MenuItem::text, "Audio rate processing", &AudioRateItem::module, module));
		menu->addChild(new MenuSeparator());
		menu->addChild(construct<TextScrollItem>(&MenuItem::text, "Text scrolling", &TextScrollItem::module, module));
		menu->addChild(construct<MappingIndicatorHiddenItem>(&MenuItem::text, "Hide mapping indicators", &MappingIndicatorHiddenItem::module, module));
		menu->addChild(construct<LockedItem>(&MenuItem::text, "Lock mapping slots", &LockedItem::module, module));
	}

	void extendParamWidgetContextMenu(ParamWidget* pw, Menu* menu) override {
		ParamQuantity* pq = pw->paramQuantity;
		if (!pq) return;

		struct CVMapBeginItem : MenuLabel {
			CVMapBeginItem() {
				text = "CV-MAP";
			}
		};

		struct CVMapEndItem : MenuEntry {
			CVMapEndItem() {
				box.size = Vec();
			}
		};

		std::list<Widget*>::iterator beg = menu->children.begin();
		std::list<Widget*>::iterator end = menu->children.end();
		std::list<Widget*>::iterator itCvBegin = end;
		std::list<Widget*>::iterator itCvEnd = end;
		
		for (auto it = beg; it != end; it++) {
			if (itCvBegin == end) {
				CVMapBeginItem* ml = dynamic_cast<CVMapBeginItem*>(*it);
				if (ml) { itCvBegin = it; continue; }
			}
			else {
				CVMapEndItem* ml = dynamic_cast<CVMapEndItem*>(*it);
				if (ml) { itCvEnd = it; break; }
			}
		}

		for (int id = 0; id < module->mapLen; id++) {
			if (module->paramHandles[id].moduleId == pq->module->id && module->paramHandles[id].paramId == pq->paramId) {
				std::list<Widget*> w;
				w.push_back(construct<CenterModuleItem>(&MenuItem::text, "Center mapping module", &CenterModuleItem::mw, this));
				w.push_back(construct<InputChannelMenuItem>(&MenuItem::text, "Input channel", &InputChannelMenuItem::module, module, &InputChannelMenuItem::id, id));
				w.push_back(new MapSlewSlider<>(&module->mapParam[id]));
				w.push_back(construct<MenuLabel>(&MenuLabel::text, "Scaling"));
				w.push_back(construct<MapScalingInputLabel<>>(&MenuLabel::text, "Input", &MapScalingInputLabel<>::p, &module->mapParam[id]));
				w.push_back(construct<MapScalingOutputLabel<>>(&MenuLabel::text, "Parameter range", &MapScalingOutputLabel<>::p, &module->mapParam[id]));
				w.push_back(new MapMinSlider<>(&module->mapParam[id]));
				w.push_back(new MapMaxSlider<>(&module->mapParam[id]));
				w.push_back(new CVMapEndItem);

				if (itCvBegin == end) {
					menu->addChild(new MenuSeparator());
					menu->addChild(construct<CVMapBeginItem>());
					for (Widget* wm : w) {
						menu->addChild(wm);
					}
				}
				else {
					for (auto i = w.rbegin(); i != w.rend(); ++i) {
						Widget* wm = *i;
						menu->addChild(wm);
						auto it = std::find(beg, end, wm);
						menu->children.splice(std::next(itCvBegin), menu->children, it);
					}
				}
				return;
			}
		}

		if (expCtx) {
			std::string id = expCtx->getCVMapId();
			if (id != "") {
				int nextId = module->getEmptySlotId();
				if (nextId >= 0) {
					MenuItem* mapMenuItem = construct<InputChannelMenuItem>(&MenuItem::text, string::f("Map on \"%s\"", id.c_str()), &InputChannelMenuItem::module, module, &InputChannelMenuItem::id, nextId, &InputChannelMenuItem::pq, pq);
					if (itCvBegin == end) {
						menu->addChild(new MenuSeparator);
						menu->addChild(construct<CVMapBeginItem>());
						menu->addChild(mapMenuItem);
					}
					else {
						menu->addChild(mapMenuItem);
						auto it = std::find(beg, end, mapMenuItem);
						menu->children.splice(std::next(itCvEnd == end ? itCvBegin : itCvEnd), menu->children, it);
					}
				}
			}
		}
	}
};

} // namespace CVMap
} // namespace StoermelderPackOne

Model* modelCVMap = createModel<StoermelderPackOne::CVMap::CVMapModule, StoermelderPackOne::CVMap::CVMapWidget>("CVMap");