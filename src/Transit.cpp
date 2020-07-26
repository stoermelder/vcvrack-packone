#include "plugin.hpp"
#include "digital.hpp"
#include "TransitBase.hpp"
#include <random>

namespace StoermelderPackOne {
namespace Transit {

const int MAX_EXPANDERS = 3;

enum class SLOTCVMODE {
	TRIG_FWD = 2,
	TRIG_REV = 4,
	TRIG_PINGPONG = 5,
	TRIG_RANDOM = 6,
	VOLT = 0,
	C4 = 1,
	ARM = 3
};

enum class OUTMODE {
	ENV = 0,
	GATE = 1,
	EOC = 2
};

template <int NUM_PRESETS>
struct TransitModule : TransitBase<NUM_PRESETS> {
	enum ParamIds {
		ENUMS(PARAM_PRESET, NUM_PRESETS),
		PARAM_RW,
		PARAM_FADE,
		PARAM_SHAPE,
		NUM_PARAMS
	};
	enum InputIds {
		INPUT_SLOT,
		INPUT_RESET,
		INPUT_FADE,
		NUM_INPUTS
	};
	enum OutputIds {
		OUTPUT,
		NUM_OUTPUTS
	};
	enum LightIds {
		ENUMS(LIGHT_PRESET, NUM_PRESETS * 3),
		LIGHT_LEARN,
		NUM_LIGHTS
	};

	/** [Stored to JSON] */
	int preset;
	/** [Stored to JSON] */
	int presetCount;

	int presetNext;

	/** Holds the last values on transitions */
	std::vector<float> presetOld;
	std::vector<float> presetNew;

	/** [Stored to JSON] mode for SEQ CV input */
	SLOTCVMODE slotCvMode = SLOTCVMODE::TRIG_FWD;
	int slotCvModeDir = 1;

	/** [Stored to JSON] */
	OUTMODE outMode;
	bool outEocArm;
	dsp::PulseGenerator outEocPulseGenerator;

	/** [Stored to JSON] */
	bool mappingIndicatorHidden = false;
	/** [Stored to JSON] */
	int presetProcessDivision;
	dsp::ClockDivider presetProcessDivider;

	std::default_random_engine randGen{(uint16_t)std::chrono::system_clock::now().time_since_epoch().count()};
	std::uniform_int_distribution<int> randDist;
	bool inChange = false;

	/** [Stored to JSON] */
	std::vector<ParamHandle*> sourceHandles;

	dsp::SchmittTrigger slotTrigger;
	dsp::SchmittTrigger resetTrigger;
	dsp::Timer resetTimer;

	StoermelderShapedSlewLimiter slewLimiter;
	dsp::ClockDivider handleDivider;
	dsp::ClockDivider buttonDivider;

	dsp::ClockDivider lightDivider;
	dsp::Timer lightTimer;
	bool lightBlink = false;

	int sampleRate;

	TransitModule() {
		TransitBase<NUM_PRESETS>::panelTheme = pluginSettings.panelThemeDefault;
		Module::config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);
		Module::configParam(PARAM_RW, 0, 1, 0, "Read/write mode");
		for (int i = 0; i < NUM_PRESETS; i++) {
			Module::configParam<TransitParamQuantity<NUM_PRESETS>>(PARAM_PRESET + i, 0, 1, 0);
			TransitParamQuantity<NUM_PRESETS>* pq = (TransitParamQuantity<NUM_PRESETS>*)Module::paramQuantities[PARAM_PRESET + i];
			pq->module = this;
			pq->i = i;
			TransitBase<NUM_PRESETS>::presetButton[i].param = &Module::params[PARAM_PRESET + i];
		}
		Module::configParam(PARAM_FADE, 0.f, 1.f, 0.5f, "Fade");
		Module::configParam(PARAM_SHAPE, -1.f, 1.f, 0.f, "Shape");

		handleDivider.setDivision(4096);
		lightDivider.setDivision(512);
		buttonDivider.setDivision(8);
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
			TransitBase<NUM_PRESETS>::presetSlotUsed[i] = false;
			TransitBase<NUM_PRESETS>::presetSlot[i].clear();
		}

		preset = -1;
		presetCount = NUM_PRESETS;
		presetNext = -1;
		slewLimiter.reset(10.f);

		outMode = OUTMODE::ENV;

		mappingIndicatorHidden = false;
		presetProcessDivision = 8;
		presetProcessDivider.setDivision(presetProcessDivision);
		presetProcessDivider.reset();
		
		Module::onReset();
		TransitBase<NUM_PRESETS>* t = this;
		int c = 0;
		while (true) {
			c++;
			if (c == MAX_EXPANDERS + 1) break;
			Module* exp = t->rightExpander.module;
			if (!exp) break;
			if (exp->model->plugin->slug != "Stoermelder-P1" || exp->model->slug != "TransitEx") break;
			t = reinterpret_cast<TransitBase<NUM_PRESETS>*>(exp);
			t->onReset();
		}
	}

	TransitBase<NUM_PRESETS>* N[MAX_EXPANDERS + 1];

	Param* transitParam(int i) override {
		return &Module::params[PARAM_PRESET + i];
	}

	Light* transitLight(int i) override {
		return &Module::lights[LIGHT_PRESET + i];
	}

	inline Param* expParam(int index) {
		int n = index / NUM_PRESETS;
		return N[n]->transitParam(index % NUM_PRESETS);
	}

	inline Light* expLight(int index, int j) {
		int n = index / NUM_PRESETS;
		return N[n]->transitLight((index % NUM_PRESETS) * 3 + j);
	}

	inline bool* expPresetSlotUsed(int index) {
		int n = index / NUM_PRESETS;
		return &N[n]->presetSlotUsed[index % NUM_PRESETS];
	}

	inline std::vector<float>* expPresetSlot(int index) {
		int n = index / NUM_PRESETS;
		return &N[n]->presetSlot[index % NUM_PRESETS];
	}

	inline LongPressButton* expPresetButton(int index) {
		int n = index / NUM_PRESETS;
		return &N[n]->presetButton[index % NUM_PRESETS];
	}

	void process(const Module::ProcessArgs& args) override {
		if (inChange) return;
		sampleRate = args.sampleRate;

		int presetNum = NUM_PRESETS;
		Module* m = this;
		TransitBase<NUM_PRESETS>* t = this;
		int c = 0;
		while (true) {
			N[c] = t;

			c++;
			if (c == MAX_EXPANDERS + 1) break;

			Module* exp = m->rightExpander.module;
			if (!exp) break;
			if (exp->model->plugin->slug != "Stoermelder-P1" || exp->model->slug != "TransitEx") break;
			m = exp;
			t = reinterpret_cast<TransitBase<NUM_PRESETS>*>(exp);
			if (t->ctrlModuleId >= 0 && t->ctrlModuleId != Module::id) t->onReset();
			t->panelTheme = TransitBase<NUM_PRESETS>::panelTheme;
			t->ctrlModuleId = Module::id;
			t->ctrlOffset = c;
			presetNum += NUM_PRESETS;
		}
		presetCount = std::min(presetCount, presetNum);

		if (handleDivider.process()) {
			for (size_t i = 0; i < sourceHandles.size(); i++) {
				ParamHandle* sourceHandle = sourceHandles[i];
				sourceHandle->color = mappingIndicatorHidden ? color::BLACK_TRANSPARENT : nvgRGB(0x40, 0xff, 0xff);
			}
		}

		// Read mode
		if (Module::params[PARAM_RW].getValue() == 0.f) {
			// RESET input
			if (slotCvMode == SLOTCVMODE::TRIG_FWD || slotCvMode == SLOTCVMODE::TRIG_REV || slotCvMode == SLOTCVMODE::TRIG_PINGPONG) {
				if (Module::inputs[INPUT_RESET].isConnected() && resetTrigger.process(Module::inputs[INPUT_RESET].getVoltage())) {
					resetTimer.reset();
					presetLoad(0);
				}
			}

			// SLOT input
			if (Module::inputs[INPUT_SLOT].isConnected() && resetTimer.process(args.sampleTime) >= 1e-3f) {
				switch (slotCvMode) {
					case SLOTCVMODE::VOLT:
						presetLoad(std::floor(rescale(Module::inputs[INPUT_SLOT].getVoltage(), 0.f, 10.f, 0, presetCount)));
						break;
					case SLOTCVMODE::C4:
						presetLoad(std::round(clamp(Module::inputs[INPUT_SLOT].getVoltage() * 12.f, 0.f, presetNum - 1.f)));
						break;
					case SLOTCVMODE::TRIG_FWD:
						if (slotTrigger.process(Module::inputs[INPUT_SLOT].getVoltage())) {
							presetLoad((preset + 1) % presetCount);
						}
						break;
					case SLOTCVMODE::TRIG_REV:
						if (slotTrigger.process(Module::inputs[INPUT_SLOT].getVoltage())) {
							presetLoad((preset - 1 + presetCount) % presetCount);
						}
						break;
					case SLOTCVMODE::TRIG_PINGPONG:
						if (slotTrigger.process(Module::inputs[INPUT_SLOT].getVoltage())) {
							int n = preset + slotCvModeDir;
							if (n == presetCount - 1) 
								slotCvModeDir = -1;
							if (n == 0) 
								slotCvModeDir = 1;
							presetLoad(n);
						}
						break;
					case SLOTCVMODE::TRIG_RANDOM:
						if (slotTrigger.process(Module::inputs[INPUT_SLOT].getVoltage())) {
							if (randDist.max() != presetCount - 1) randDist = std::uniform_int_distribution<int>(0, presetCount - 1);
							presetLoad(randDist(randGen));
						}
						break;
					case SLOTCVMODE::ARM:
						if (slotTrigger.process(Module::inputs[INPUT_SLOT].getVoltage())) {
							presetLoad(presetNext);
						}
						break;
				}
			}

			// Buttons
			if (buttonDivider.process()) {
				float sampleTime = args.sampleTime * buttonDivider.division;
				for (int i = 0; i < presetNum; i++) {
					switch (expPresetButton(i)->process(sampleTime)) {
						default:
						case LongPressButton::NO_PRESS:
							break;
						case LongPressButton::SHORT_PRESS:
							presetLoad(i, slotCvMode == SLOTCVMODE::ARM, true); break;
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
				for (int i = 0; i < presetNum; i++) {
					switch (expPresetButton(i)->process(sampleTime)) {
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
			if (lightTimer.process(s) > 0.2f) {
				lightTimer.reset();
				lightBlink ^= true;
			}
			for (int i = 0; i < presetNum; i++) {
				if (Module::params[PARAM_RW].getValue() == 0.f) {
					expLight(i, 0)->setBrightness(preset == i ? 1.f : (presetNext == i ? 1.f : 0.f));
					expLight(i, 1)->setBrightness(preset == i ? 1.f : (presetCount > i ? (*expPresetSlotUsed(i) ? 1.f : 0.2f) : 0.f));
					expLight(i, 2)->setBrightness(preset == i ? 1.f : 0.f);
				}
				else {
					expLight(i, 0)->setBrightness(preset == i && lightBlink ? 0.6f : (*expPresetSlotUsed(i) ? 1.f : 0.f));
					expLight(i, 1)->setBrightness(preset == i && lightBlink ? 0.6f : 0.f);
					expLight(i, 2)->setBrightness(preset == i && lightBlink ? 0.6f : 0.f);
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
		return paramQuantity;
	}

	void bindModule() {
		Module::Expander* exp = &(Module::leftExpander);
		if (exp->moduleId < 0) return;

		Module* m = exp->module;
		for (size_t i = 0; i < m->params.size(); i++) {
			bindParameter(m->id, i);
		}
	}

	void bindParameter(int moduleId, int paramId) {
		for (ParamHandle* handle : sourceHandles) {
			if (handle->moduleId == moduleId && handle->paramId == paramId) {
				// Parameter already bound
				return;
			}
		}

		ParamHandle* sourceHandle = new ParamHandle;
		sourceHandle->text = "stoermelder TRANSIT";
		APP->engine->addParamHandle(sourceHandle);
		APP->engine->updateParamHandle(sourceHandle, moduleId, paramId, true);
		inChange = true;
		sourceHandles.push_back(sourceHandle);
		inChange = false;

		ParamQuantity* pq = getParamQuantity(sourceHandle);
		if (pq) {
			float v = pq->getValue();
			for (size_t i = 0; i < NUM_PRESETS; i++) {
				if (!expPresetSlotUsed(i)) continue;
				expPresetSlot(i)->push_back(v);
			}
		}
	}

	void presetLoad(int p, bool isNext = false, bool force = false) {
		if (p < 0 || p >= presetCount)
			return;

		if (!isNext) {
			if (p != preset || force) {
				preset = p;
				presetNext = -1;
				if (!*expPresetSlotUsed(p)) return;
				slewLimiter.reset();
				outEocArm = true;
				presetOld.clear();
				presetNew.clear();
				for (size_t i = 0; i < sourceHandles.size(); i++) {
					ParamQuantity* pq = getParamQuantity(sourceHandles[i]);
					presetOld.push_back(pq ? pq->getValue() : 0.f);
					if (expPresetSlot(preset)->size() > i) {
						presetNew.push_back((*expPresetSlot(preset))[i]);
					}
				}
			}
		}
		else {
			if (!*expPresetSlotUsed(p)) return;
			presetNext = p;
		}
	}

	void presetProcess(float sampleTime) {
		if (presetProcessDivider.process()) {
			if (preset == -1) return;
			float deltaTime = sampleTime * presetProcessDivision;

			float fade = TransitBase<NUM_PRESETS>::inputs[INPUT_FADE].getVoltage() / 10.f + TransitBase<NUM_PRESETS>::params[PARAM_FADE].getValue();
			slewLimiter.setRise(fade);
			float shape = TransitBase<NUM_PRESETS>::params[PARAM_SHAPE].getValue();
			slewLimiter.setShape(shape);
			float s = slewLimiter.process(10.f, deltaTime);

			if (s == 10.f && outEocArm) {
				outEocPulseGenerator.trigger();
				outEocArm = false;
			}

			switch (outMode) {
				case OUTMODE::ENV:
					TransitBase<NUM_PRESETS>::outputs[OUTPUT].setVoltage(s == 10.f ? 0.f : s);
					break;
				case OUTMODE::GATE:
					TransitBase<NUM_PRESETS>::outputs[OUTPUT].setVoltage(s != 10.f ? 10.f : 0.f);
					break;
				case OUTMODE::EOC:
					TransitBase<NUM_PRESETS>::outputs[OUTPUT].setVoltage(outEocPulseGenerator.process(deltaTime) ? 10.f : 0.f);
					break;
			}

			if (s == 10.f) return;
			s /= 10.f;

			for (size_t i = 0; i < sourceHandles.size(); i++) {
				ParamQuantity* pq = getParamQuantity(sourceHandles[i]);
				if (!pq) continue;
				if (presetOld.size() <= i) return;
				float oldValue = presetOld[i];
				if (presetNew.size() <= i) return;
				float newValue = presetNew[i];
				float v = crossfade(oldValue, newValue, s);
				if (s > (1.f - 5e-3f) && std::abs(std::round(v) - v) < 5e-3f) v = std::round(v);
				pq->setValue(v);
			}
		}
		presetProcessDivider.setDivision(presetProcessDivision);
	}

	void presetSave(int p) {
		*expPresetSlotUsed(p) = true;
		expPresetSlot(p)->clear();
		for (size_t i = 0; i < sourceHandles.size(); i++) {
			ParamQuantity* pq = getParamQuantity(sourceHandles[i]);
			if (!pq) continue;
			float v = pq->getValue();
			expPresetSlot(p)->push_back(v);
		}
	}

	void presetClear(int p) {
		*expPresetSlotUsed(p) = false;
		expPresetSlot(p)->clear();
		if (preset == p) preset = -1;
	}

	void presetSetCount(int p) {
		if (preset >= p) preset = 0;
		presetCount = p;
		presetNext = -1;
	}

	json_t* dataToJson() override {
		json_t* rootJ = json_object();
		json_object_set_new(rootJ, "panelTheme", json_integer(TransitBase<NUM_PRESETS>::panelTheme));
		json_object_set_new(rootJ, "mappingIndicatorHidden", json_boolean(mappingIndicatorHidden));
		json_object_set_new(rootJ, "presetProcessDivision", json_integer(presetProcessDivision));

		json_object_set_new(rootJ, "slotCvMode", json_integer((int)slotCvMode));
		json_object_set_new(rootJ, "outMode", json_integer((int)outMode));
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
		TransitBase<NUM_PRESETS>::panelTheme = json_integer_value(json_object_get(rootJ, "panelTheme"));
		mappingIndicatorHidden = json_boolean_value(json_object_get(rootJ, "mappingIndicatorHidden"));
		presetProcessDivision = json_integer_value(json_object_get(rootJ, "presetProcessDivision"));

		slotCvMode = (SLOTCVMODE)json_integer_value(json_object_get(rootJ, "slotCvMode"));
		outMode = (OUTMODE)json_integer_value(json_object_get(rootJ, "outMode"));
		preset = json_integer_value(json_object_get(rootJ, "preset"));
		presetCount = json_integer_value(json_object_get(rootJ, "presetCount"));

		// Hack for preventing duplicating this module
		if (APP->engine->getModule(TransitBase<NUM_PRESETS>::id) != NULL) return;

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
			TransitBase<NUM_PRESETS>::presetSlotUsed[presetIndex] = json_boolean_value(json_object_get(presetJ, "slotUsed"));
			TransitBase<NUM_PRESETS>::presetSlot[presetIndex].clear();
			if (TransitBase<NUM_PRESETS>::presetSlotUsed[presetIndex]) {
				json_t* slotJ = json_object_get(presetJ, "slot");
				json_t* vJ;
				size_t j;
				json_array_foreach(slotJ, j, vJ) {
					float v = json_real_value(vJ);
					TransitBase<NUM_PRESETS>::presetSlot[presetIndex].push_back(v);
				}
			}
		}

		if (preset >= presetCount) {
			preset = -1;
		}

		Module::params[PARAM_RW].setValue(0.f);
	}
};

template <int NUM_PRESETS>
struct TransitWidget : ThemedModuleWidget<TransitModule<NUM_PRESETS>> {
	typedef TransitWidget<NUM_PRESETS> WIDGET;
	typedef ThemedModuleWidget<TransitModule<NUM_PRESETS>> BASE;
	typedef TransitModule<NUM_PRESETS> MODULE;
	
	int learnParam = 0;

	TransitWidget(MODULE* module)
		: ThemedModuleWidget<MODULE>(module, "Transit") {
		BASE::setModule(module);

		BASE::addChild(createWidget<StoermelderBlackScrew>(Vec(RACK_GRID_WIDTH, 0)));
		BASE::addChild(createWidget<StoermelderBlackScrew>(Vec(BASE::box.size.x - 2 * RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));

		BASE::addInput(createInputCentered<StoermelderPort>(Vec(21.7f, 58.9f), module, MODULE::INPUT_SLOT));
		BASE::addInput(createInputCentered<StoermelderPort>(Vec(21.7f, 94.2f), module, MODULE::INPUT_RESET));

		BASE::addParam(createParamCentered<LEDSliderWhite>(Vec(21.7f, 166.7f), module, MODULE::PARAM_FADE));
		BASE::addInput(createInputCentered<StoermelderPort>(Vec(21.7f, 221.4f), module, MODULE::INPUT_FADE));

		BASE::addParam(createParamCentered<StoermelderTrimpot>(Vec(21.7f, 255.8f), module, MODULE::PARAM_SHAPE));
		BASE::addOutput(createOutputCentered<StoermelderPort>(Vec(21.7f, 300.3f), module, MODULE::OUTPUT));

		BASE::addParam(createParamCentered<CKSSH>(Vec(21.7f, 336.2f), module, MODULE::PARAM_RW));

		BASE::addChild(createLightCentered<TinyLight<WhiteLight>>(Vec(7.4f, 336.2f), module, MODULE::LIGHT_LEARN));

		for (size_t i = 0; i < NUM_PRESETS; i++) {
			float o = i * (288.7f / (NUM_PRESETS - 1));
			BASE::addParam(createParamCentered<LEDButton>(Vec(60.0f, 45.4f + o), module, MODULE::PARAM_PRESET + i));
			BASE::addChild(createLightCentered<LargeLight<RedGreenBlueLight>>(Vec(60.0f, 45.4f + o), module, MODULE::LIGHT_PRESET + i * 3));
		}
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

		struct PrecisionMenuItem : MenuItem {
			struct PrecisionItem : MenuItem {
				MODULE* module;
				int division;
				std::string text;
				void onAction(const event::Action& e) override {
					module->presetProcessDivision = division;
				}
				void step() override {
					MenuItem::text = string::f("%s (%i Hz)", text.c_str(), module->sampleRate / division);
					rightText = module->presetProcessDivision == division ? "✔" : "";
					MenuItem::step();
				}
			};

			MODULE* module;
			PrecisionMenuItem() {
				rightText = RIGHT_ARROW;
			}

			Menu* createChildMenu() override {
				Menu* menu = new Menu;
				menu->addChild(construct<PrecisionItem>(&PrecisionItem::text, "Audio rate", &PrecisionItem::module, module, &PrecisionItem::division, 1));
				menu->addChild(construct<PrecisionItem>(&PrecisionItem::text, "Lower CPU", &PrecisionItem::module, module, &PrecisionItem::division, 8));
				menu->addChild(construct<PrecisionItem>(&PrecisionItem::text, "Lowest CPU", &PrecisionItem::module, module, &PrecisionItem::division, 64));
				return menu;
			}
		};

		struct SlotCvModeMenuItem : MenuItem {
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
			SlotCvModeMenuItem() {
				rightText = RIGHT_ARROW;
			}

			Menu* createChildMenu() override {
				Menu* menu = new Menu;
				menu->addChild(construct<SlotCvModeItem>(&MenuItem::text, "Trigger forward", &SlotCvModeItem::module, module, &SlotCvModeItem::slotCvMode, SLOTCVMODE::TRIG_FWD));
				menu->addChild(construct<SlotCvModeItem>(&MenuItem::text, "Trigger reverse", &SlotCvModeItem::module, module, &SlotCvModeItem::slotCvMode, SLOTCVMODE::TRIG_REV));
				menu->addChild(construct<SlotCvModeItem>(&MenuItem::text, "Trigger pingpong", &SlotCvModeItem::module, module, &SlotCvModeItem::slotCvMode, SLOTCVMODE::TRIG_PINGPONG));
				menu->addChild(construct<SlotCvModeItem>(&MenuItem::text, "Trigger random", &SlotCvModeItem::module, module, &SlotCvModeItem::slotCvMode, SLOTCVMODE::TRIG_RANDOM));
				menu->addChild(construct<SlotCvModeItem>(&MenuItem::text, "0..10V", &SlotCvModeItem::module, module, &SlotCvModeItem::slotCvMode, SLOTCVMODE::VOLT));
				menu->addChild(construct<SlotCvModeItem>(&MenuItem::text, "C4", &SlotCvModeItem::module, module, &SlotCvModeItem::slotCvMode, SLOTCVMODE::C4));
				menu->addChild(construct<SlotCvModeItem>(&MenuItem::text, "Arm", &SlotCvModeItem::module, module, &SlotCvModeItem::slotCvMode, SLOTCVMODE::ARM));
				return menu;
			}
		};

		struct OutModeMenuItem : MenuItem {
			struct OutModeItem : MenuItem {
				MODULE* module;
				OUTMODE outMode;
				void onAction(const event::Action& e) override {
					module->outMode = outMode;
				}
				void step() override {
					rightText = module->outMode == outMode ? "✔" : "";
					MenuItem::step();
				}
			};

			MODULE* module;
			OutModeMenuItem() {
				rightText = RIGHT_ARROW;
			}

			Menu* createChildMenu() override {
				Menu* menu = new Menu;
				menu->addChild(construct<OutModeItem>(&MenuItem::text, "Envelope", &OutModeItem::module, module, &OutModeItem::outMode, OUTMODE::ENV));
				menu->addChild(construct<OutModeItem>(&MenuItem::text, "Gate", &OutModeItem::module, module, &OutModeItem::outMode, OUTMODE::GATE));
				menu->addChild(construct<OutModeItem>(&MenuItem::text, "EOC", &OutModeItem::module, module, &OutModeItem::outMode, OUTMODE::EOC));
				return menu;
			}
		};

		struct BindModuleItem : MenuItem {
			MODULE* module;
			WIDGET* widget;
			void onAction(const event::Action& e) override {
				widget->learnParam = 0;
				module->bindModule();
			}
		};

		struct BindParameterItem : MenuItem {
			WIDGET* widget;
			int mode;
			std::string rightText = "";
			void onAction(const event::Action& e) override {
				widget->learnParam = widget->learnParam != mode ? mode : 0;
				APP->scene->rack->touchedParam = NULL;
				APP->event->setSelected(widget);
			}
			void step() override {
				MenuItem::rightText = widget->learnParam == mode ? "Active" : rightText;
				MenuItem::step();
			}
		};

		struct ParameterMenuItem : MenuItem {
			struct ParameterItem : MenuItem {
				MODULE* module;
				ParamHandle* handle;
				void onAction(const event::Action& e) override {
					APP->engine->updateParamHandle(handle, -1, 0, true);
				}
			};

			MODULE* module;
			ParameterMenuItem() {
				rightText = RIGHT_ARROW;
			}

			Menu* createChildMenu() override {
				Menu* menu = new Menu;
				for (size_t i = 0; i < module->sourceHandles.size(); i++) {
					ParamHandle* handle = module->sourceHandles[i];
					ModuleWidget* moduleWidget = APP->scene->rack->getModule(handle->moduleId);
					if (!moduleWidget) continue;
					ParamWidget* paramWidget = moduleWidget->getParam(handle->paramId);
					if (!paramWidget) continue;
					
					std::string text = string::f("Unbind \"%s %s\"", moduleWidget->model->name.c_str(), paramWidget->paramQuantity->getLabel().c_str());
					menu->addChild(construct<ParameterItem>(&MenuItem::text, text, &ParameterItem::module, module, &ParameterItem::handle, handle));
				}
				return menu;
			}
		};

		menu->addChild(new MenuSeparator());
		menu->addChild(construct<MappingIndicatorHiddenItem>(&MenuItem::text, "Hide mapping indicators", &MappingIndicatorHiddenItem::module, module));
		menu->addChild(construct<PrecisionMenuItem>(&MenuItem::text, "Precision", &PrecisionMenuItem::module, module));
		menu->addChild(new MenuSeparator());
		menu->addChild(construct<SlotCvModeMenuItem>(&MenuItem::text, "SLOT-port", &SlotCvModeMenuItem::module, module));
		menu->addChild(construct<OutModeMenuItem>(&MenuItem::text, "OUT-port", &OutModeMenuItem::module, module));
		menu->addChild(new MenuSeparator());
		menu->addChild(construct<BindModuleItem>(&MenuItem::text, "Bind module (left)", &BindModuleItem::widget, this, &BindModuleItem::module, module));
		menu->addChild(construct<BindParameterItem>(&MenuItem::text, "Bind single parameter", &BindParameterItem::rightText, RACK_MOD_SHIFT_NAME "+B", &BindParameterItem::widget, this, &BindParameterItem::mode, 1));
		menu->addChild(construct<BindParameterItem>(&MenuItem::text, "Bind multiple parameters", &BindParameterItem::rightText, RACK_MOD_SHIFT_NAME "+A", &BindParameterItem::widget, this, &BindParameterItem::mode, 2));

		if (module->sourceHandles.size() > 0) {
			menu->addChild(construct<ParameterMenuItem>(&MenuItem::text, "Bound parameters", &ParameterMenuItem::module, module));
		}
	}

	void onHoverKey(const event::HoverKey& e) override {
		BASE::onHoverKey(e);
		if (e.action == GLFW_PRESS && (e.mods & GLFW_MOD_SHIFT)) {
			switch (e.key) {
				case GLFW_KEY_B:
					learnParam = learnParam != 1 ? 1 : 0;
					APP->event->setSelected(this);
					break;
				case GLFW_KEY_A: 
					learnParam = learnParam != 2 ? 2 : 0; 
					break;
			}
		}
	}

	void onDeselect(const event::Deselect& e) override {
		if (learnParam == 0) return;
		MODULE* module = dynamic_cast<MODULE*>(this->module);

		// Check if a ParamWidget was touched, unstable API
		ParamWidget* touchedParam = APP->scene->rack->touchedParam;
		if (touchedParam && touchedParam->paramQuantity->module != module) {
			APP->scene->rack->touchedParam = NULL;
			int moduleId = touchedParam->paramQuantity->module->id;
			int paramId = touchedParam->paramQuantity->paramId;
			module->bindParameter(moduleId, paramId);
			if (learnParam == 1) learnParam = 0;
		} 
	}

	void step() override {
		if (learnParam == 2 && APP->event->getSelectedWidget() != this) {
			APP->event->setSelected(this);
		}
		if (BASE::module) {
			BASE::module->lights[MODULE::LIGHT_LEARN].setBrightness(learnParam > 0);
		}
		BASE::step();
	}
};

} // namespace Transit
} // namespace StoermelderPackOne

Model* modelTransit = createModel<StoermelderPackOne::Transit::TransitModule<12>, StoermelderPackOne::Transit::TransitWidget<12>>("Transit");