#include "plugin.hpp"
#include "digital.hpp"
#include <plugin.hpp>
#include <random>

namespace StoermelderPackOne {
namespace Transit {

enum SLOTCVMODE {
	SLOTCVMODE_TRIG_FWD = 2,
	SLOTCVMODE_TRIG_REV = 4,
	SLOTCVMODE_TRIG_PINGPONG = 5,
	SLOTCVMODE_TRIG_RANDOM = 6,
	SLOTCVMODE_10V = 0,
	SLOTCVMODE_C4 = 1,
	SLOTCVMODE_ARM = 3
};

template < int NUM_PRESETS >
struct TransitModule : Module {
	enum ParamIds {
		MODE_PARAM,
		ENUMS(PRESET_PARAM, NUM_PRESETS),
		PARAM_FADE,
		PARAM_SHAPE,
		NUM_PARAMS
	};
	enum InputIds {
		SLOT_INPUT,
		RESET_INPUT,
		NUM_INPUTS
	};
	enum OutputIds {
		NUM_OUTPUTS
	};
	enum LightIds {
		ENUMS(PRESET_LIGHT, NUM_PRESETS * 3),
		NUM_LIGHTS
	};

	/** [Stored to JSON] */
	int panelTheme = 0;

	/** [Stored to JSON] */
	std::string sourcePluginSlug;
	/** [Stored to JSON] */
	std::string sourcePluginName;
	/** [Stored to JSON] */
	std::string sourceModelSlug;
	/** [Stored to JSON] */
	std::string sourceModelName;

	/** [Stored to JSON] */
	bool presetSlotUsed[NUM_PRESETS];
	/** [Stored to JSON] */
	std::vector<float> presetSlot[NUM_PRESETS];
	/** [Stored to JSON] */
	int preset = 0;
	/** [Stored to JSON] */
	int presetCount = NUM_PRESETS;

	int presetNext = -1;

	std::vector<float> presetOld;

	/** [Stored to JSON] mode for SEQ CV input */
	SLOTCVMODE slotCvMode = SLOTCVMODE_TRIG_FWD;
	int slotCvModeDir = 1;

	/** [Stored to JSON] */
	bool mappingIndicatorHidden = false;

	std::default_random_engine randGen{(uint16_t)std::chrono::system_clock::now().time_since_epoch().count()};
	std::uniform_int_distribution<int>* randDist = NULL;
	bool inChange = false;
	float modeLight = 0;

	/** [Stored to JSON] */
	std::vector<ParamHandle*> sourceHandles;

	LongPressButton typeButtons[NUM_PRESETS];
	dsp::SchmittTrigger slotTrigger;
	dsp::SchmittTrigger resetTrigger;
	dsp::Timer resetTimer;

	StoermelderSlewLimiter slewLimiter;
	dsp::ClockDivider handleDivider;
	dsp::ClockDivider lightDivider;
	dsp::ClockDivider buttonDivider;

	TransitModule() {
		panelTheme = pluginSettings.panelThemeDefault;
		config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);
		configParam(MODE_PARAM, 0, 1, 0, "Switch Read/write mode");
		for (int i = 0; i < NUM_PRESETS; i++) {
			configParam(PRESET_PARAM + i, 0, 1, 0, string::f("Preset slot %d", i + 1));
			typeButtons[i].param = &params[PRESET_PARAM + i];
			presetSlotUsed[i] = false;
		}
		configParam(PARAM_FADE, 0.f, 1.f, 0.5f, "Fade time");
		configParam(PARAM_SHAPE, 0.f, 1.f, 1.f, "Shape");

		handleDivider.setDivision(4096);
		lightDivider.setDivision(512);
		buttonDivider.setDivision(4);
		onReset();
	}

	~TransitModule() {
		for (ParamHandle* sourceHandle : sourceHandles) {
			APP->engine->removeParamHandle(sourceHandle);
			delete sourceHandle;
		}
		delete randDist;
	}

	void onReset() override {
		inChange = true;
		for (ParamHandle* sourceHandle : sourceHandles) {
			APP->engine->removeParamHandle(sourceHandle);
			delete sourceHandle;
		}
		sourceHandles.clear();
		inChange = false;

		sourcePluginSlug = "";
		sourcePluginName = "";
		sourceModelSlug = "";
		sourceModelName = "";

		for (int i = 0; i < NUM_PRESETS; i++) {
			presetSlotUsed[i] = false;
			presetSlot[i].clear();
		}

		preset = -1;
		presetCount = NUM_PRESETS;
		presetNext = -1;
        slewLimiter.out = 1.f;

		if (randDist) delete randDist;
		randDist = new std::uniform_int_distribution<int>(0, presetCount - 1);
	}

	void process(const ProcessArgs& args) override {
		if (inChange) return;

		if (handleDivider.process()) {
			for (size_t i = 0; i < sourceHandles.size(); i++) {
				ParamHandle* sourceHandle = sourceHandles[i];
				sourceHandle->color = mappingIndicatorHidden ? color::BLACK_TRANSPARENT : nvgRGB(0x40, 0xff, 0xff);
			}
		}

		// Read mode
		if (params[MODE_PARAM].getValue() == 0.f) {
			// RESET input
			if (slotCvMode == SLOTCVMODE_TRIG_FWD || slotCvMode == SLOTCVMODE_TRIG_REV || slotCvMode == SLOTCVMODE_TRIG_PINGPONG) {
				if (inputs[RESET_INPUT].isConnected() && resetTrigger.process(inputs[RESET_INPUT].getVoltage())) {
					resetTimer.reset();
					presetLoad(0);
				}
			}

			// SLOT input
			if (resetTimer.process(args.sampleTime) >= 1e-3f && inputs[SLOT_INPUT].isConnected()) {
				switch (slotCvMode) {
					case SLOTCVMODE_10V:
						presetLoad(std::floor(rescale(inputs[SLOT_INPUT].getVoltage(), 0.f, 10.f, 0, presetCount)));
						break;
					case SLOTCVMODE_C4:
						presetLoad(std::round(clamp(inputs[SLOT_INPUT].getVoltage() * 12.f, 0.f, NUM_PRESETS - 1.f)));
						break;
					case SLOTCVMODE_TRIG_FWD:
						if (slotTrigger.process(inputs[SLOT_INPUT].getVoltage()))
							presetLoad((preset + 1) % presetCount);
						break;
					case SLOTCVMODE_TRIG_REV:
						if (slotTrigger.process(inputs[SLOT_INPUT].getVoltage()))
							presetLoad((preset - 1 + presetCount) % presetCount);
						break;
					case SLOTCVMODE_TRIG_PINGPONG:
						if (slotTrigger.process(inputs[SLOT_INPUT].getVoltage())) {
							int n = preset + slotCvModeDir;
							if (n == presetCount - 1) 
								slotCvModeDir = -1;
							if (n == 0) 
								slotCvModeDir = 1;
							presetLoad(n);
						}
						break;
					case SLOTCVMODE_TRIG_RANDOM:
						if (slotTrigger.process(inputs[SLOT_INPUT].getVoltage()))
							presetLoad((*randDist)(randGen));
						break;
					case SLOTCVMODE_ARM:
						if (slotTrigger.process(inputs[SLOT_INPUT].getVoltage()))
							presetLoad(presetNext);
						break;
				}
			}

			// Buttons
			if (buttonDivider.process()) {
				float sampleTime = args.sampleTime * buttonDivider.division;
				for (int i = 0; i < NUM_PRESETS; i++) {
					switch (typeButtons[i].process(sampleTime)) {
						default:
						case LongPressButton::NO_PRESS:
							break;
						case LongPressButton::SHORT_PRESS:
							presetLoad(i, slotCvMode == SLOTCVMODE_ARM, true); break;
						case LongPressButton::LONG_PRESS:
							presetSetCount(i + 1); break;
					}
				}
			}
		}
		// Write mode
		else {
			if (buttonDivider.process()) {
				float sampleTime = args.sampleTime * buttonDivider.division;
				for (int i = 0; i < NUM_PRESETS; i++) {
					switch (typeButtons[i].process(sampleTime)) {
						default:
						case LongPressButton::NO_PRESS:
							break;
						case LongPressButton::SHORT_PRESS:
							presetSave(i); break;
						case LongPressButton::LONG_PRESS:
							presetClear(i); break;
					}
				}
			}
		}

		presetProcess(args.sampleTime);

		// Set channel lights infrequently
		if (lightDivider.process()) {
			float s = args.sampleTime * lightDivider.getDivision();
			modeLight += 0.7f * s;
			if (modeLight > 1.5f) modeLight = 0.f;

			for (int i = 0; i < NUM_PRESETS; i++) {
				if (params[MODE_PARAM].getValue() == 0.f) {
					lights[PRESET_LIGHT + i * 3 + 0].setBrightness(presetNext == i ? 1.f : 0.f);
					lights[PRESET_LIGHT + i * 3 + 1].setSmoothBrightness(preset != i && presetCount > i ? (presetSlotUsed[i] ? 1.f : 0.2f) : 0.f, s);
					lights[PRESET_LIGHT + i * 3 + 2].setSmoothBrightness(preset == i ? 1.f : 0.f, s);
				}
				else {
					lights[PRESET_LIGHT + i * 3 + 0].setBrightness(presetSlotUsed[i] ? 1.f : 0.f);
					lights[PRESET_LIGHT + i * 3 + 1].setBrightness(0.f);
					lights[PRESET_LIGHT + i * 3 + 2].setBrightness(0.f);
				}
			}
		}
	}

	ParamQuantity* getParamQuantity(ParamHandle* handle) {
		if (handle->moduleId < 0)
			return NULL;
		// Get Module
		Module* module = handle->module;
		if (!module)
			return NULL;
		// Get ParamQuantity
		int paramId = handle->paramId;
		ParamQuantity* paramQuantity = module->paramQuantities[paramId];
		if (!paramQuantity)
			return NULL;
		if (!paramQuantity->isBounded())
			return NULL;
		return paramQuantity;
	}

	void bindToSource() {
		Expander* exp = &leftExpander;
		if (exp->moduleId < 0) return;

		inChange = true;
		onReset();
		Module* m = exp->module;
		sourcePluginSlug = m->model->plugin->slug;
		sourcePluginName = m->model->plugin->name;
		sourceModelSlug = m->model->slug;
		sourceModelName = m->model->name;

		for (size_t i = 0; i < m->params.size(); i++) {
			ParamHandle* sourceHandle = new ParamHandle;
			sourceHandle->text = "stoermelder TRANSIT";
			APP->engine->addParamHandle(sourceHandle);
			APP->engine->updateParamHandle(sourceHandle, m->id, i, true);
			sourceHandles.push_back(sourceHandle);
		}

		inChange = false;
	}

	void presetLoad(int p, bool isNext = false, bool force = false) {
		if (p < 0 || p >= presetCount)
			return;

		if (!isNext) {
			if (p != preset || force) {
				preset = p;
				presetNext = -1;
				if (!presetSlotUsed[p]) return;
				slewLimiter.reset();
				presetOld.clear();
				for (size_t i = 0; i < sourceHandles.size(); i++) {
					ParamQuantity* pq = getParamQuantity(sourceHandles[i]);
					presetOld.push_back(pq ? pq->getValue() : 0.f);
				}
			}
		}
		else {
			if (!presetSlotUsed[p]) return;
			presetNext = p;
		}
	}

	void presetProcess(float sampleTime) {
		if (preset == -1) return;
		slewLimiter.setRise(params[PARAM_FADE].getValue());
		slewLimiter.setShape(params[PARAM_SHAPE].getValue());
		float s = slewLimiter.process(1.f, sampleTime);
		if (s == 1.f) return;

		for (size_t i = 0; i < sourceHandles.size(); i++) {
			ParamQuantity* pq = getParamQuantity(sourceHandles[i]);
			if (!pq) continue;
			float oldValue = presetOld[i];
			float newValue = presetSlot[preset][i];
			float v = oldValue * (1.f - s) + newValue * s;
			pq->setValue(v);
		}
	}

	void presetSave(int p) {
		presetSlotUsed[p] = true;
		for (size_t i = 0; i < sourceHandles.size(); i++) {
			ParamQuantity* pq = getParamQuantity(sourceHandles[i]);
			if (!pq) continue;
			float v = pq->getValue();
			presetSlot[p].push_back(v);
		}
	}

	void presetClear(int p) {
		presetSlotUsed[p] = false;
		presetSlot[p].clear();
		if (preset == p) preset = -1;
	}

	void presetSetCount(int p) {
		if (preset >= p) preset = 0;
		presetCount = p;
		presetNext = -1;
		delete randDist;
		randDist = new std::uniform_int_distribution<int>(0, presetCount - 1);
	}

	json_t* dataToJson() override {
		json_t* rootJ = json_object();
		json_object_set_new(rootJ, "panelTheme", json_integer(panelTheme));
		json_object_set_new(rootJ, "mappingIndicatorHidden", json_boolean(mappingIndicatorHidden));

		json_object_set_new(rootJ, "sourcePluginSlug", json_string(sourcePluginSlug.c_str()));
		json_object_set_new(rootJ, "sourcePluginName", json_string(sourcePluginName.c_str()));
		json_object_set_new(rootJ, "sourceModelSlug", json_string(sourceModelSlug.c_str()));
		json_object_set_new(rootJ, "sourceModelName", json_string(sourceModelName.c_str()));

		json_object_set_new(rootJ, "slotCvMode", json_integer(slotCvMode));
		json_object_set_new(rootJ, "preset", json_integer(preset));
		json_object_set_new(rootJ, "presetCount", json_integer(presetCount));

		json_t* sourceMapsJ = json_array();
		for (size_t i = 0; i < sourceHandles.size(); i++) {
			json_t* sourceMapJ = json_object();
			json_object_set_new(sourceMapJ, "moduleId", json_integer(sourceHandles[i]->moduleId));
			json_object_set_new(sourceMapJ, "paramId", json_integer(sourceHandles[i]->paramId));
			json_array_append_new(sourceMapsJ, sourceMapJ);
		}
		json_object_set_new(rootJ, "sourceMaps", sourceMapsJ);

		json_t* presetsJ = json_array();
		for (int i = 0; i < NUM_PRESETS; i++) {
			json_t* presetJ = json_object();
			json_object_set_new(presetJ, "slotUsed", json_boolean(presetSlotUsed[i]));
			if (presetSlotUsed[i]) {
                json_t* slotJ = json_array();
                for (size_t j = 0; j < presetSlot[i].size(); j++) {
                    json_t* vJ = json_real(presetSlot[i][j]);
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
		// Hack for preventing duplicating this module
		if (APP->engine->getModule(id) != NULL) return;

		panelTheme = json_integer_value(json_object_get(rootJ, "panelTheme"));
		mappingIndicatorHidden = json_boolean_value(json_object_get(rootJ, "mappingIndicatorHidden"));

		json_t* sourcePluginSlugJ = json_object_get(rootJ, "sourcePluginSlug");
		if (sourcePluginSlugJ) sourcePluginSlug = json_string_value(sourcePluginSlugJ);
		json_t* sourcePluginNameJ = json_object_get(rootJ, "sourcePluginName");
		if (sourcePluginNameJ) sourcePluginName = json_string_value(sourcePluginNameJ);
		json_t* sourceModelSlugJ = json_object_get(rootJ, "sourceModelSlug");
		if (sourceModelSlugJ) sourceModelSlug = json_string_value(sourceModelSlugJ);
		json_t* sourceModelNameJ = json_object_get(rootJ, "sourceModelName");
		if (sourceModelNameJ) sourceModelName = json_string_value(sourceModelNameJ);

		slotCvMode = (SLOTCVMODE)json_integer_value(json_object_get(rootJ, "slotCvMode"));
		preset = json_integer_value(json_object_get(rootJ, "preset"));
		presetCount = json_integer_value(json_object_get(rootJ, "presetCount"));

		inChange = true;
		json_t* sourceMapsJ = json_object_get(rootJ, "sourceMaps");
		if (sourceMapsJ) {
			json_t* sourceMapJ;
			size_t sourceMapIndex;
			json_array_foreach(sourceMapsJ, sourceMapIndex, sourceMapJ) {
				json_t* moduleIdJ = json_object_get(sourceMapJ, "moduleId");
				json_t* paramIdJ = json_object_get(sourceMapJ, "paramId");

				ParamHandle* sourceHandle = new ParamHandle;
				sourceHandle->text = "stoermelder TRANSIT";
				APP->engine->addParamHandle(sourceHandle);
				APP->engine->updateParamHandle(sourceHandle, json_integer_value(moduleIdJ), json_integer_value(paramIdJ), false);
				sourceHandles.push_back(sourceHandle);
			}
		}
		inChange = false;

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

		if (preset >= presetCount) {
			preset = 0;
		}
	}
};

struct TransitWidget : ThemedModuleWidget<TransitModule<8>> {
	typedef TransitModule<8> MODULE;

	TransitWidget(MODULE* module)
		: ThemedModuleWidget<MODULE>(module, "Transit") {
		setModule(module);

		addChild(createWidget<StoermelderBlackScrew>(Vec(RACK_GRID_WIDTH, 0)));
		addChild(createWidget<StoermelderBlackScrew>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));

		addInput(createInputCentered<StoermelderPort>(Vec(22.5f, 59.8f), module, MODULE::SLOT_INPUT));
		//addInput(createInputCentered<StoermelderPort>(Vec(22.5f, 95.2f), module, MODULE::RESET_INPUT));

		addParam(createParamCentered<StoermelderTrimpot>(Vec(22.5f, 88.0f), module, MODULE::PARAM_FADE));
		addParam(createParamCentered<StoermelderTrimpot>(Vec(22.5f, 115.6f), module, MODULE::PARAM_SHAPE));

		addParam(createParamCentered<LEDButton>(Vec(22.5f, 140.6f), module, MODULE::PRESET_PARAM + 0));
		addParam(createParamCentered<LEDButton>(Vec(22.5f, 164.1f), module, MODULE::PRESET_PARAM + 1));
		addParam(createParamCentered<LEDButton>(Vec(22.5f, 187.7f), module, MODULE::PRESET_PARAM + 2));
		addParam(createParamCentered<LEDButton>(Vec(22.5f, 211.2f), module, MODULE::PRESET_PARAM + 3));
		addParam(createParamCentered<LEDButton>(Vec(22.5f, 234.8f), module, MODULE::PRESET_PARAM + 4));
		addParam(createParamCentered<LEDButton>(Vec(22.5f, 258.3f), module, MODULE::PRESET_PARAM + 5));
		addParam(createParamCentered<LEDButton>(Vec(22.5f, 281.9f), module, MODULE::PRESET_PARAM + 6));
		addParam(createParamCentered<LEDButton>(Vec(22.5f, 305.4f), module, MODULE::PRESET_PARAM + 7));

		addChild(createLightCentered<LargeLight<RedGreenBlueLight>>(Vec(22.5f, 140.6f), module, MODULE::PRESET_LIGHT + 0 * 3));
		addChild(createLightCentered<LargeLight<RedGreenBlueLight>>(Vec(22.5f, 164.1f), module, MODULE::PRESET_LIGHT + 1 * 3));
		addChild(createLightCentered<LargeLight<RedGreenBlueLight>>(Vec(22.5f, 187.7f), module, MODULE::PRESET_LIGHT + 2 * 3));
		addChild(createLightCentered<LargeLight<RedGreenBlueLight>>(Vec(22.5f, 211.2f), module, MODULE::PRESET_LIGHT + 3 * 3));
		addChild(createLightCentered<LargeLight<RedGreenBlueLight>>(Vec(22.5f, 234.8f), module, MODULE::PRESET_LIGHT + 4 * 3));
		addChild(createLightCentered<LargeLight<RedGreenBlueLight>>(Vec(22.5f, 258.3f), module, MODULE::PRESET_LIGHT + 5 * 3));
		addChild(createLightCentered<LargeLight<RedGreenBlueLight>>(Vec(22.5f, 281.9f), module, MODULE::PRESET_LIGHT + 6 * 3));
		addChild(createLightCentered<LargeLight<RedGreenBlueLight>>(Vec(22.5f, 305.4f), module, MODULE::PRESET_LIGHT + 7 * 3));

		addParam(createParamCentered<CKSSH>(Vec(22.5f, 336.2f), module, MODULE::MODE_PARAM));
	}

	void appendContextMenu(Menu* menu) override {
		ThemedModuleWidget<MODULE>::appendContextMenu(menu);
		MODULE* module = dynamic_cast<MODULE*>(this->module);
		assert(module);

		if (module->sourceModelSlug != "") {
			menu->addChild(new MenuSeparator());

			ui::MenuLabel* textLabel = new ui::MenuLabel;
			textLabel->text = "Configured for...";
			menu->addChild(textLabel);

			ui::MenuLabel* modelLabel = new ui::MenuLabel;
			modelLabel->text = module->sourcePluginName + " " + module->sourceModelName;
			menu->addChild(modelLabel);
		}

		struct MappingIndicatorHiddenItem : MenuItem {
			MODULE* module;
			void onAction(const event::Action& e) override {
				module->mappingIndicatorHidden ^= true;
			}
			void step() override {
				rightText = module->mappingIndicatorHidden ? "✔" : "";
				MenuItem::step();
			}
		};

		struct BindSourceItem : MenuItem {
			MODULE* module;
			void onAction(const event::Action& e) override {
				module->bindToSource();
			}
		};

		struct SlovCvModeMenuItem : MenuItem {
			struct SlotCvModeItem : MenuItem {
				MODULE* module;
				SLOTCVMODE slotCvMode;

				void onAction(const event::Action& e) override {
					module->slotCvMode = slotCvMode;
				}

				void step() override {
					rightText = module->slotCvMode == slotCvMode ? "✔" : "";
					MenuItem::step();
				}
			};

			MODULE* module;
			SlovCvModeMenuItem() {
				rightText = RIGHT_ARROW;
			}

			Menu* createChildMenu() override {
				Menu* menu = new Menu;
				menu->addChild(construct<SlotCvModeItem>(&MenuItem::text, "Trigger forward", &SlotCvModeItem::module, module, &SlotCvModeItem::slotCvMode, SLOTCVMODE_TRIG_FWD));
				menu->addChild(construct<SlotCvModeItem>(&MenuItem::text, "Trigger reverse", &SlotCvModeItem::module, module, &SlotCvModeItem::slotCvMode, SLOTCVMODE_TRIG_REV));
				menu->addChild(construct<SlotCvModeItem>(&MenuItem::text, "Trigger pingpong", &SlotCvModeItem::module, module, &SlotCvModeItem::slotCvMode, SLOTCVMODE_TRIG_PINGPONG));
				menu->addChild(construct<SlotCvModeItem>(&MenuItem::text, "Trigger random", &SlotCvModeItem::module, module, &SlotCvModeItem::slotCvMode, SLOTCVMODE_TRIG_RANDOM));
				menu->addChild(construct<SlotCvModeItem>(&MenuItem::text, "0..10V", &SlotCvModeItem::module, module, &SlotCvModeItem::slotCvMode, SLOTCVMODE_10V));
				menu->addChild(construct<SlotCvModeItem>(&MenuItem::text, "C4", &SlotCvModeItem::module, module, &SlotCvModeItem::slotCvMode, SLOTCVMODE_C4));
				menu->addChild(construct<SlotCvModeItem>(&MenuItem::text, "Arm", &SlotCvModeItem::module, module, &SlotCvModeItem::slotCvMode, SLOTCVMODE_ARM));
				return menu;
			}
		};

		menu->addChild(construct<BindSourceItem>(&MenuItem::text, "Bind module (left)", &BindSourceItem::module, module));
		menu->addChild(new MenuSeparator());
		menu->addChild(construct<MappingIndicatorHiddenItem>(&MenuItem::text, "Hide mapping indicators", &MappingIndicatorHiddenItem::module, module));
		menu->addChild(new MenuSeparator());
		menu->addChild(construct<SlovCvModeMenuItem>(&MenuItem::text, "Port SLOT mode", &SlovCvModeMenuItem::module, module));
	}
};

} // namespace Transit
} // namespace StoermelderPackOne

Model* modelTransit = createModel<StoermelderPackOne::Transit::TransitModule<8>, StoermelderPackOne::Transit::TransitWidget>("Transit");