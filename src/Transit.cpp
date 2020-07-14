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
		PARAM_RW,
		PARAM_FADE,
		PARAM_SHAPE,
		ENUMS(PARAM_PRESET, NUM_PRESETS),
		NUM_PARAMS
	};
	enum InputIds {
		INPUT_SLOT,
		INPUT_RESET,
		INPUT_FADE,
		INPUT_SHAPE,
		NUM_INPUTS
	};
	enum OutputIds {
		NUM_OUTPUTS
	};
	enum LightIds {
		ENUMS(LIGHT_PRESET, NUM_PRESETS * 3),
		NUM_LIGHTS
	};

	/** [Stored to JSON] */
	int panelTheme = 0;

	/** [Stored to JSON] */
	bool presetSlotUsed[NUM_PRESETS];
	/** [Stored to JSON] */
	std::vector<float> presetSlot[NUM_PRESETS];
	/** [Stored to JSON] */
	int preset;
	/** [Stored to JSON] */
	int presetCount;

	int presetNext;

	std::vector<float> presetOld;

	/** [Stored to JSON] mode for SEQ CV input */
	SLOTCVMODE slotCvMode = SLOTCVMODE_TRIG_FWD;
	int slotCvModeDir = 1;

	/** [Stored to JSON] */
	bool mappingIndicatorHidden = false;

	std::default_random_engine randGen{(uint16_t)std::chrono::system_clock::now().time_since_epoch().count()};
	std::uniform_int_distribution<int> randDist;
	bool inChange = false;

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
		configParam(PARAM_RW, 0, 1, 0, "Switch Read/write mode");
		for (int i = 0; i < NUM_PRESETS; i++) {
			configParam<TriggerParamQuantity>(PARAM_PRESET + i, 0, 1, 0, string::f("Preset slot %d", i + 1));
			typeButtons[i].param = &params[PARAM_PRESET + i];
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
	}

	void onReset() override {
		inChange = true;
		for (ParamHandle* sourceHandle : sourceHandles) {
			APP->engine->removeParamHandle(sourceHandle);
			delete sourceHandle;
		}
		sourceHandles.clear();
		inChange = false;

		for (int i = 0; i < NUM_PRESETS; i++) {
			presetSlotUsed[i] = false;
			presetSlot[i].clear();
		}

		preset = -1;
		presetCount = NUM_PRESETS;
		presetNext = -1;
		slewLimiter.out = 1.f;

		randDist = std::uniform_int_distribution<int>(0, presetCount - 1);
		mappingIndicatorHidden = false;
		Module::onReset();
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
		if (params[PARAM_RW].getValue() == 0.f) {
			// RESET input
			if (slotCvMode == SLOTCVMODE_TRIG_FWD || slotCvMode == SLOTCVMODE_TRIG_REV || slotCvMode == SLOTCVMODE_TRIG_PINGPONG) {
				if (inputs[INPUT_RESET].isConnected() && resetTrigger.process(inputs[INPUT_RESET].getVoltage())) {
					resetTimer.reset();
					presetLoad(0);
				}
			}

			// SLOT input
			if (resetTimer.process(args.sampleTime) >= 1e-3f && inputs[INPUT_SLOT].isConnected()) {
				switch (slotCvMode) {
					case SLOTCVMODE_10V:
						presetLoad(std::floor(rescale(inputs[INPUT_SLOT].getVoltage(), 0.f, 10.f, 0, presetCount)));
						break;
					case SLOTCVMODE_C4:
						presetLoad(std::round(clamp(inputs[INPUT_SLOT].getVoltage() * 12.f, 0.f, NUM_PRESETS - 1.f)));
						break;
					case SLOTCVMODE_TRIG_FWD:
						if (slotTrigger.process(inputs[INPUT_SLOT].getVoltage()))
							presetLoad((preset + 1) % presetCount);
						break;
					case SLOTCVMODE_TRIG_REV:
						if (slotTrigger.process(inputs[INPUT_SLOT].getVoltage()))
							presetLoad((preset - 1 + presetCount) % presetCount);
						break;
					case SLOTCVMODE_TRIG_PINGPONG:
						if (slotTrigger.process(inputs[INPUT_SLOT].getVoltage())) {
							int n = preset + slotCvModeDir;
							if (n == presetCount - 1) 
								slotCvModeDir = -1;
							if (n == 0) 
								slotCvModeDir = 1;
							presetLoad(n);
						}
						break;
					case SLOTCVMODE_TRIG_RANDOM:
						if (slotTrigger.process(inputs[INPUT_SLOT].getVoltage()))
							presetLoad(randDist(randGen));
						break;
					case SLOTCVMODE_ARM:
						if (slotTrigger.process(inputs[INPUT_SLOT].getVoltage()))
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

			for (int i = 0; i < NUM_PRESETS; i++) {
				if (params[PARAM_RW].getValue() == 0.f) {
					lights[LIGHT_PRESET + i * 3 + 0].setBrightness(presetNext == i ? 1.f : 0.f);
					lights[LIGHT_PRESET + i * 3 + 1].setSmoothBrightness(preset != i && presetCount > i ? (presetSlotUsed[i] ? 1.f : 0.2f) : 0.f, s);
					lights[LIGHT_PRESET + i * 3 + 2].setSmoothBrightness(preset == i ? 1.f : 0.f, s);
				}
				else {
					lights[LIGHT_PRESET + i * 3 + 0].setBrightness(presetSlotUsed[i] ? 1.f : 0.f);
					lights[LIGHT_PRESET + i * 3 + 1].setBrightness(0.f);
					lights[LIGHT_PRESET + i * 3 + 2].setBrightness(0.f);
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

	void bindModule() {
		Expander* exp = &leftExpander;
		if (exp->moduleId < 0) return;

		inChange = true;
		Module* m = exp->module;
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
		float fade = inputs[INPUT_FADE].getVoltage() / 10.f + params[PARAM_FADE].getValue();
		slewLimiter.setRise(fade);
		float shape = inputs[INPUT_SHAPE].getVoltage() / 10.f + params[PARAM_SHAPE].getValue();
		slewLimiter.setShape(shape);
		float s = slewLimiter.process(1.f, sampleTime);
		if (s >= (1.f - 1e-3f)) return;

		for (size_t i = 0; i < sourceHandles.size(); i++) {
			ParamQuantity* pq = getParamQuantity(sourceHandles[i]);
			if (!pq) continue;
			if (presetOld.size() <= i) return;
			float oldValue = presetOld[i];
			if (presetSlot[preset].size() <= i) return;
			float newValue = presetSlot[preset][i];
			float v = oldValue * (1.f - s) + newValue * s;
			if (s > (1.f - 1e-2f) && std::abs(std::round(v) - v) < 5e-2f) v = std::round(v);
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
		randDist = std::uniform_int_distribution<int>(0, presetCount - 1);
	}

	json_t* dataToJson() override {
		json_t* rootJ = json_object();
		json_object_set_new(rootJ, "panelTheme", json_integer(panelTheme));
		json_object_set_new(rootJ, "mappingIndicatorHidden", json_boolean(mappingIndicatorHidden));

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
		panelTheme = json_integer_value(json_object_get(rootJ, "panelTheme"));
		mappingIndicatorHidden = json_boolean_value(json_object_get(rootJ, "mappingIndicatorHidden"));

		slotCvMode = (SLOTCVMODE)json_integer_value(json_object_get(rootJ, "slotCvMode"));
		preset = json_integer_value(json_object_get(rootJ, "preset"));
		presetCount = json_integer_value(json_object_get(rootJ, "presetCount"));

		// Hack for preventing duplicating this module
		if (APP->engine->getModule(id) != NULL) return;

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

template < int NUM_PRESETS >
struct TransitWidget : ThemedModuleWidget<TransitModule<NUM_PRESETS>> {
	typedef ThemedModuleWidget<TransitModule<NUM_PRESETS>> BASE;
	typedef TransitModule<NUM_PRESETS> MODULE;
	
	TransitWidget(MODULE* module)
		: ThemedModuleWidget<MODULE>(module, "Transit") {
		BASE::setModule(module);

		BASE::addChild(createWidget<StoermelderBlackScrew>(Vec(RACK_GRID_WIDTH, 0)));
		BASE::addChild(createWidget<StoermelderBlackScrew>(Vec(BASE::box.size.x - 2 * RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));

		for (size_t i = 0; i < NUM_PRESETS; i++) {
			float o = i * (288.7f / (NUM_PRESETS - 1));
			BASE::addParam(createParamCentered<LEDButton>(Vec(17.1f, 45.4f + o), module, MODULE::PARAM_PRESET + i));
			BASE::addChild(createLightCentered<LargeLight<RedGreenBlueLight>>(Vec(17.1f, 45.4f + o), module, MODULE::LIGHT_PRESET + i * 3));
		}

		BASE::addInput(createInputCentered<StoermelderPort>(Vec(52.6f, 58.9f), module, MODULE::INPUT_SLOT));
		BASE::addInput(createInputCentered<StoermelderPort>(Vec(52.6f, 95.2f), module, MODULE::INPUT_RESET));

		BASE::addParam(createParamCentered<LEDSliderBlue>(Vec(52.6f, 170.2f), module, MODULE::PARAM_FADE));
		BASE::addInput(createInputCentered<StoermelderPort>(Vec(52.6f, 225.9f), module, MODULE::INPUT_FADE));

		BASE::addParam(createParamCentered<StoermelderTrimpot>(Vec(52.6f, 270.6f), module, MODULE::PARAM_SHAPE));
		BASE::addInput(createInputCentered<StoermelderPort>(Vec(52.6f, 295.6f), module, MODULE::INPUT_SHAPE));

		BASE::addParam(createParamCentered<CKSSH>(Vec(52.6f, 336.2f), module, MODULE::PARAM_RW));
	}

	void appendContextMenu(Menu* menu) override {
		ThemedModuleWidget<MODULE>::appendContextMenu(menu);
		MODULE* module = dynamic_cast<MODULE*>(this->module);
		assert(module);

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

		struct BindModuleItem : MenuItem {
			MODULE* module;
			void onAction(const event::Action& e) override {
				module->bindModule();
			}
		};

		menu->addChild(new MenuSeparator());
		menu->addChild(construct<MappingIndicatorHiddenItem>(&MenuItem::text, "Hide mapping indicators", &MappingIndicatorHiddenItem::module, module));
		menu->addChild(new MenuSeparator());
		menu->addChild(construct<SlovCvModeMenuItem>(&MenuItem::text, "Port SLOT mode", &SlovCvModeMenuItem::module, module));
		menu->addChild(new MenuSeparator());
		menu->addChild(construct<BindModuleItem>(&MenuItem::text, "Bind module (left)", &BindModuleItem::module, module));
	}
};

} // namespace Transit
} // namespace StoermelderPackOne

Model* modelTransit = createModel<StoermelderPackOne::Transit::TransitModule<14>, StoermelderPackOne::Transit::TransitWidget<14>>("Transit");