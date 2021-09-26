#include "plugin.hpp"
#include "digital.hpp"
#include "TransitBase.hpp"
#include "digital/ShapedSlewLimiter.hpp"
#include "components/ParamHandleIndicator.hpp"
#include <random>

namespace StoermelderPackOne {
namespace Transit {

const int MAX_EXPANDERS = 7;

enum class SLOTCVMODE {
	OFF = -1,
	TRIG_FWD = 2,
	TRIG_REV = 4,
	TRIG_PINGPONG = 5,
	TRIG_ALT = 9,
	TRIG_RANDOM = 6,
	TRIG_RANDOM_WO_REPEAT = 7,
	TRIG_RANDOM_WALK = 8,
	TRIG_SHUFFLE = 10,
	VOLT = 0,
	C4 = 1,
	ARM = 3,
	PHASE = 11
};

enum class OUTMODE {
	POLY = -1,
	ENV = 0,
	GATE = 1,
	TRIG_SNAPSHOT = 4,
	TRIG_SOC = 3,
	TRIG_EOC = 2,
	PHASE = 5
};

template <int NUM_PRESETS>
struct TransitModule : TransitBase<NUM_PRESETS> {
	typedef TransitBase<NUM_PRESETS> BASE;

	enum ParamIds {
		ENUMS(PARAM_PRESET, NUM_PRESETS),
		PARAM_CTRLMODE,
		PARAM_FADE,
		PARAM_SHAPE,
		NUM_PARAMS
	};
	enum InputIds {
		INPUT_CV,
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
		LIGHT_CV,
		NUM_LIGHTS
	};

	/** [Stored to JSON] Currently selected snapshot */
	int preset;
	/** [Stored to JSON] Number of currently active snapshots */
	int presetCount;

	/** Total number of snapshots including expanders */
	int presetTotal;
	int presetNext;
	int presetCopy = -1;
	float presetPhaseLast = -1.f;

	/** Holds the last values on transitions */
	std::vector<float> presetOld;
	std::vector<float> presetNew;

	/** [Stored to JSON] mode for SEQ CV input */
	SLOTCVMODE slotCvMode = SLOTCVMODE::TRIG_FWD;
	SLOTCVMODE slotCvModeBak = SLOTCVMODE::OFF;
	int slotCvModeDir = 1;
	int slotCvModeAlt = 1;
	std::vector<int> slotCvModeShuffle;

	/** [Stored to JSON] */
	OUTMODE outMode;
	bool outEocArm;
	dsp::PulseGenerator outSlotPulseGenerator;
	dsp::PulseGenerator outSocPulseGenerator;
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
	ParamHandleIndicator paramHandleIndicator;

	int sampleRate;

	TransitBase<NUM_PRESETS>* N[MAX_EXPANDERS + 1];
	

	TransitModule() {
		BASE::panelTheme = pluginSettings.panelThemeDefault;
		Module::config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);
		Module::configParam<TriggerParamQuantity>(PARAM_CTRLMODE, 0, 2, 0, "Read/Auto/Write mode");
		for (int i = 0; i < NUM_PRESETS; i++) {
			TransitParamQuantity<NUM_PRESETS>* pq = Module::configParam<TransitParamQuantity<NUM_PRESETS>>(PARAM_PRESET + i, 0, 1, 0);
			pq->mymodule = this;
			pq->id = i;
			BASE::presetButton[i].param = &Module::params[PARAM_PRESET + i];

			BASE::slot[i].param = &Module::params[PARAM_PRESET + i];
			BASE::slot[i].lights = &Module::lights[LIGHT_PRESET + i * 3];
			BASE::slot[i].presetSlotUsed = &BASE::presetSlotUsed[i];
			BASE::slot[i].preset = &BASE::preset[i];
			BASE::slot[i].presetButton = &BASE::presetButton[i];
		}
		Module::configParam(PARAM_FADE, 0.f, 1.f, 0.5f, "Fade");
		Module::configParam(PARAM_SHAPE, -1.f, 1.f, 0.f, "Shape");

		handleDivider.setDivision(4096);
		lightDivider.setDivision(512);
		buttonDivider.setDivision(128);
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
			BASE::presetSlotUsed[i] = false;
			BASE::textLabel[i] = "";
			BASE::preset[i].clear();
		}

		preset = -1;
		presetCount = NUM_PRESETS;
		presetNext = -1;
		slewLimiter.reset(10.f);

		outMode = OUTMODE::ENV;
		outSlotPulseGenerator.reset();
		outSocPulseGenerator.reset();
		outEocPulseGenerator.reset();

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
			if (exp->model != modelTransitEx) break;
			t = reinterpret_cast<TransitBase<NUM_PRESETS>*>(exp);
			t->onReset();
		}
	}

	TransitSlot* transitSlot(int i) override {
		return &BASE::slot[i];
	}

	inline TransitSlot* expSlot(int index) {
		if (index >= presetTotal) return NULL;
		int n = index / NUM_PRESETS;
		return N[n]->transitSlot(index % NUM_PRESETS);
	}

	inline std::string* expSlotLabel(int index) {
		if (index >= presetTotal) return NULL;
		int n = index / NUM_PRESETS;
		return &N[n]->textLabel[index % NUM_PRESETS];
	}

	void process(const Module::ProcessArgs& args) override {
		if (inChange) return;
		sampleRate = args.sampleRate;

		presetTotal = NUM_PRESETS;
		Module* m = this;
		TransitBase<NUM_PRESETS>* t = this;
		t->ctrlMode = (CTRLMODE)Module::params[PARAM_CTRLMODE].getValue();
		int c = 0;
		while (true) {
			N[c] = t;
			c++;
			if (c == MAX_EXPANDERS + 1) break;

			Module* exp = m->rightExpander.module;
			if (!exp) break;
			if (exp->model != modelTransitEx) break;
			m = exp;
			t = reinterpret_cast<TransitBase<NUM_PRESETS>*>(exp);
			if (t->ctrlModuleId >= 0 && t->ctrlModuleId != Module::id) t->onReset();
			t->panelTheme = BASE::panelTheme;
			t->ctrlModuleId = Module::id;
			t->ctrlOffset = c;
			t->ctrlMode = BASE::ctrlMode;
			presetTotal += NUM_PRESETS;
		}
		int presetCount = std::min(this->presetCount, presetTotal);

		if (handleDivider.process()) {
			for (size_t i = 0; i < sourceHandles.size(); i++) {
				ParamHandle* sourceHandle = sourceHandles[i];
				if (paramHandleIndicator.handle == sourceHandle) continue;
				sourceHandle->color = mappingIndicatorHidden ? color::BLACK_TRANSPARENT : nvgRGB(0x40, 0xff, 0xff);
			}
			paramHandleIndicator.color = mappingIndicatorHidden ? color::BLACK_TRANSPARENT : nvgRGB(0x40, 0xff, 0xff);
			paramHandleIndicator.process(args.sampleTime * handleDivider.division);
		}

		// Read & Auto mode
		if (BASE::ctrlMode == CTRLMODE::READ || BASE::ctrlMode == CTRLMODE::AUTO) {
			// RESET input
			if (slotCvMode == SLOTCVMODE::TRIG_FWD || slotCvMode == SLOTCVMODE::TRIG_REV || slotCvMode == SLOTCVMODE::TRIG_PINGPONG) {
				if (Module::inputs[INPUT_RESET].isConnected() && resetTrigger.process(Module::inputs[INPUT_RESET].getVoltage())) {
					resetTimer.reset();
					presetLoad(0);
				}
			}

			// CV input
			if (Module::inputs[INPUT_CV].isConnected() && resetTimer.process(args.sampleTime) >= 1e-3f) {
				switch (slotCvMode) {
					case SLOTCVMODE::VOLT:
						presetLoad(std::floor(rescale(Module::inputs[INPUT_CV].getVoltage(), 0.f, 10.f, 0, presetCount)));
						break;
					case SLOTCVMODE::C4:
						presetLoad(std::round(clamp(Module::inputs[INPUT_CV].getVoltage() * 12.f, 0.f, presetTotal - 1.f)));
						break;
					case SLOTCVMODE::TRIG_FWD:
						if (slotTrigger.process(Module::inputs[INPUT_CV].getVoltage())) {
							presetLoad((preset + 1) % presetCount);
						}
						break;
					case SLOTCVMODE::TRIG_REV:
						if (slotTrigger.process(Module::inputs[INPUT_CV].getVoltage())) {
							presetLoad((preset - 1 + presetCount) % presetCount);
						}
						break;
					case SLOTCVMODE::TRIG_PINGPONG:
						if (slotTrigger.process(Module::inputs[INPUT_CV].getVoltage())) {
							int n = preset + slotCvModeDir;
							if (n >= presetCount - 1)
								slotCvModeDir = -1;
							if (n <= 0)
								slotCvModeDir = 1;
							presetLoad(n);
						}
						break;
					case SLOTCVMODE::TRIG_ALT:
						if (slotTrigger.process(Module::inputs[INPUT_CV].getVoltage())) {
							int n = 0;
							if (preset == 0) {
								n = slotCvModeAlt + slotCvModeDir;
								if (n >= presetCount - 1)
									slotCvModeDir = -1;
								if (n <= 1)
									slotCvModeDir = 1;
								slotCvModeAlt = std::min(n, presetCount - 1);
							}
							presetLoad(n);
						}
						break;
					case SLOTCVMODE::TRIG_RANDOM:
						if (slotTrigger.process(Module::inputs[INPUT_CV].getVoltage())) {
							if (randDist.max() != presetCount - 1) randDist = std::uniform_int_distribution<int>(0, presetCount - 1);
							presetLoad(randDist(randGen));
						}
						break;
					case SLOTCVMODE::TRIG_RANDOM_WO_REPEAT:
						if (slotTrigger.process(Module::inputs[INPUT_CV].getVoltage())) {
							if (randDist.max() != presetCount - 2) randDist = std::uniform_int_distribution<int>(0, presetCount - 2);
							int p = randDist(randGen);
							if (p >= preset) p++;
							presetLoad(p);
						}
						break;
					case SLOTCVMODE::TRIG_RANDOM_WALK:
						if (slotTrigger.process(Module::inputs[INPUT_CV].getVoltage())) {
							int p = std::min(std::max(0, preset + (random::u32() % 2 == 0 ? -1 : 1)), presetCount - 1);
							presetLoad(p);
						}
						break;
					case SLOTCVMODE::TRIG_SHUFFLE:
						if (slotTrigger.process(Module::inputs[INPUT_CV].getVoltage())) {
							if (slotCvModeShuffle.size() == 0) {
								for (int i = 0; i < presetCount; i++) {
									slotCvModeShuffle.push_back(i);
								}
								std::random_shuffle(std::begin(slotCvModeShuffle), std::end(slotCvModeShuffle));
							}
							int p = std::min(std::max(0, slotCvModeShuffle.back()), presetCount - 1);
							slotCvModeShuffle.pop_back();
							presetLoad(p);
						}
						break;
					case SLOTCVMODE::ARM:
						if (slotTrigger.process(Module::inputs[INPUT_CV].getVoltage())) {
							presetLoad(presetNext);
						}
						break;
					default:
						break;
				}
			}

			// Buttons
			if (buttonDivider.process()) {
				float sampleTime = args.sampleTime * buttonDivider.division;
				for (int i = 0; i < presetTotal; i++) {
					TransitSlot* slot = expSlot(i);
					switch (slot->presetButton->process(sampleTime)) {
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
			// Buttons
			if (buttonDivider.process()) {
				float sampleTime = args.sampleTime * buttonDivider.division;
				for (int i = 0; i < presetTotal; i++) {
					TransitSlot* slot = expSlot(i);
					switch (slot->presetButton->process(sampleTime)) {
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

		if (isPhaseCvActive()) {
			if (BASE::ctrlMode == CTRLMODE::READ) {
				presetProcessPhase(args.sampleTime);
			}
		} 
		else {
			presetProcess(args.sampleTime);
		}

		// Set lights infrequently
		if (lightDivider.process()) {
			float s = args.sampleTime * lightDivider.getDivision();
			if (lightTimer.process(s) > 0.2f) {
				lightTimer.reset();
				lightBlink ^= true;
			}
			float intpart;
			float frac = std::modf(presetPhaseLast, &intpart);
			for (int i = 0; i < presetTotal; i++) {
				TransitSlot* slot = expSlot(i);
				bool u = *(slot->presetSlotUsed);
				if (BASE::ctrlMode == CTRLMODE::READ || BASE::ctrlMode == CTRLMODE::AUTO) {
					if (!isPhaseCvActive()) {
						slot->lights[0].setBrightness(preset == i ? 1.f : (presetNext == i ? 1.f : 0.f));
						slot->lights[1].setBrightness(preset == i ? 1.f : (presetCount > i ? (u ? 1.f : 0.25f) : 0.f));
						slot->lights[2].setBrightness(preset == i ? 1.f : 0.f);
					}
					else {
						float f = (intpart == i) ? (1.f - frac) : (intpart + 1 == i) ? (frac) : 0.f;
						slot->lights[0].setBrightness(f);
						slot->lights[1].setBrightness(std::max(f, presetCount > i ? (u ? 1.f : 0.25f) : 0.f));
						slot->lights[2].setBrightness(f);
					}
				}
				else {
					bool b = preset == i && lightBlink;
					slot->lights[0].setBrightness(b ? 0.7f : (u ? 1.f : 0.f));
					slot->lights[1].setBrightness(b ? 0.7f : (u ? 0.f : (presetCount > i ? 0.05f : 0.f)));
					slot->lights[2].setBrightness(b ? 0.7f : 0.f);
				}
			}

			BASE::lights[LIGHT_CV].setBrightness((slotCvMode == SLOTCVMODE::OFF || (slotCvMode == SLOTCVMODE::PHASE && BASE::ctrlMode == CTRLMODE::WRITE)) && lightBlink);
		}
	}

	inline bool isPhaseCvActive() {
		return slotCvMode == SLOTCVMODE::PHASE && BASE::inputs[INPUT_CV].isConnected();
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

	void bindModule(Module* m) {
		if (!m) return;
		for (size_t i = 0; i < m->params.size(); i++) {
			bindParameter(m->id, i);
		}
	}

	void bindModuleExpander() {
		Module::Expander* exp = &(Module::leftExpander);
		if (exp->moduleId < 0) return;
		Module* m = exp->module;
		bindModule(m);
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
		float v = pq ? pq->getValue() : 0.f;
		for (int i = 0; i < presetTotal; i++) {
			TransitSlot* slot = expSlot(i);
			if (!*(slot->presetSlotUsed)) continue;
			slot->preset->push_back(v);
			assert(sourceHandles.size() == slot->preset->size());
		}
	}

	void presetLoad(int p, bool isNext = false, bool force = false) {
		if (p < 0 || p >= presetCount)
			return;

		TransitSlot* slot = expSlot(p);
		if (!isNext) {
			if (p != preset || force) {
				int presetPrev = preset;
				preset = p;
				presetNext = -1;
				outSlotPulseGenerator.trigger();
				if (!*(slot->presetSlotUsed)) 
					return;
				if (BASE::ctrlMode == CTRLMODE::AUTO) {
					TransitSlot* slotPrev = expSlot(presetPrev);
					if (*(slotPrev->presetSlotUsed)) {
						slotPrev->preset->clear();
						for (size_t i = 0; i < sourceHandles.size(); i++) {
							ParamQuantity* pq = getParamQuantity(sourceHandles[i]);
							float v = pq ? pq->getValue() : 0.f;
							slotPrev->preset->push_back(v);
						}
					}
				}
				slewLimiter.reset();
				outSocPulseGenerator.trigger();
				outEocArm = true;
				presetOld.clear();
				presetNew.clear();
				for (size_t i = 0; i < sourceHandles.size(); i++) {
					ParamQuantity* pq = getParamQuantity(sourceHandles[i]);
					presetOld.push_back(pq ? pq->getValue() : 0.f);
					if (slot->preset->size() > i) {
						presetNew.push_back((*(slot->preset))[i]);
					}
				}
			}
		}
		else {
			if (!*(slot->presetSlotUsed)) return;
			presetNext = p;
		}
	}

	void presetProcess(float sampleTime) {
		if (presetProcessDivider.process()) {
			if (preset == -1) return;
			float deltaTime = sampleTime * presetProcessDivision;

			float fade = BASE::inputs[INPUT_FADE].getVoltage() / 10.f + BASE::params[PARAM_FADE].getValue();
			slewLimiter.setRise(fade);
			float shape = BASE::params[PARAM_SHAPE].getValue();
			slewLimiter.setShape(shape);
			float s = slewLimiter.process(10.f, deltaTime);

			if (s == 10.f && outEocArm) {
				outEocPulseGenerator.trigger();
				outEocArm = false;
			}

			switch (outMode) {
				case OUTMODE::ENV:
					BASE::outputs[OUTPUT].setVoltage(s == 10.f ? 0.f : s);
					BASE::outputs[OUTPUT].setChannels(1);
					break;
				case OUTMODE::GATE:
					BASE::outputs[OUTPUT].setVoltage(s != 10.f ? 10.f : 0.f);
					BASE::outputs[OUTPUT].setChannels(1);
					break;
				case OUTMODE::TRIG_SNAPSHOT:
					BASE::outputs[OUTPUT].setVoltage(outSlotPulseGenerator.process(deltaTime) ? 10.f : 0.f);
					BASE::outputs[OUTPUT].setChannels(1);
					break;
				case OUTMODE::TRIG_SOC:
					BASE::outputs[OUTPUT].setVoltage(outSocPulseGenerator.process(deltaTime) ? 10.f : 0.f);
					BASE::outputs[OUTPUT].setChannels(1);
					break;
				case OUTMODE::TRIG_EOC:
					BASE::outputs[OUTPUT].setVoltage(outEocPulseGenerator.process(deltaTime) ? 10.f : 0.f);
					BASE::outputs[OUTPUT].setChannels(1);
					break;
				case OUTMODE::POLY:
					BASE::outputs[OUTPUT].setVoltage(s == 10.f ? 0.f : s, 0);
					BASE::outputs[OUTPUT].setVoltage(s != 10.f ? 10.f : 0.f, 1);
					BASE::outputs[OUTPUT].setVoltage(outSlotPulseGenerator.process(deltaTime) ? 10.f : 0.f, 2);
					BASE::outputs[OUTPUT].setVoltage(outSocPulseGenerator.process(deltaTime) ? 10.f : 0.f, 3);
					BASE::outputs[OUTPUT].setVoltage(outEocPulseGenerator.process(deltaTime) ? 10.f : 0.f, 4);
					BASE::outputs[OUTPUT].setChannels(5);
					break;
				default:
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
	}

	void presetProcessPhase(float sampleTime) {
		if (presetProcessDivider.process()) {
			preset = -1;
			float deltaTime = sampleTime * presetProcessDivision;

			float p = clamp(BASE::inputs[INPUT_CV].getVoltage(), 0.f, 10.f);
			p = (presetCount - 1) * p / 10.f;

			float fade = BASE::inputs[INPUT_FADE].getVoltage() / 10.f + BASE::params[PARAM_FADE].getValue();
			slewLimiter.setRiseFall(fade, fade);
			float shape = BASE::params[PARAM_SHAPE].getValue();
			slewLimiter.setShape(shape);
			p = slewLimiter.process(p, deltaTime);

			if (presetPhaseLast == p) return;
			presetPhaseLast = p;

			int p1 = std::floor(p);
			TransitSlot* slot1 = expSlot(p1);
			while (p1 >= 0 && !*(slot1->presetSlotUsed)) {
				p1--;
				slot1 = expSlot(p1);
			}
			
			int p2 = std::ceil(p);
			TransitSlot* slot2 = expSlot(p2);
			while (p2 <= presetCount - 1 && !*(slot2->presetSlotUsed)) {
				p2++;
				slot2 = expSlot(p2);
			}
			
			if (p1 < 0 && p2 >= presetCount) return;
			if (p1 < 0) { p1 = p2; slot1 = slot2; }
			if (p2 >= presetCount) p2 = p1;
			
			if (p1 != p2) {
				p = (p - float(p1)) / (float(p2) - float(p1));
				for (size_t i = 0; i < sourceHandles.size(); i++) {
					ParamQuantity* pq = getParamQuantity(sourceHandles[i]);
					if (!pq) continue;
					float v1 = (*slot1->preset)[i];
					float v2 = (*slot2->preset)[i];
					float v = crossfade(v1, v2, p);
					pq->setValue(v);
				}
			}
			else {
				for (size_t i = 0; i < sourceHandles.size(); i++) {
					ParamQuantity* pq = getParamQuantity(sourceHandles[i]);
					if (!pq) continue;
					float v = (*slot1->preset)[i];
					pq->setValue(v);
				}
			}

			BASE::outputs[OUTPUT].setVoltage(presetPhaseLast / (presetCount - 1) * 10.f);
			BASE::outputs[OUTPUT].setChannels(1);
		}
	}

	void presetSave(int p) {
		TransitSlot* slot = expSlot(p);
		*(slot->presetSlotUsed) = true;
		slot->preset->clear();
		for (size_t i = 0; i < sourceHandles.size(); i++) {
			ParamQuantity* pq = getParamQuantity(sourceHandles[i]);
			float v = pq ? pq->getValue() : 0.f;
			slot->preset->push_back(v);
		}
		assert(sourceHandles.size() == slot->preset->size());
		preset = p;
	}

	void presetClear(int p) {
		TransitSlot* slot = expSlot(p);
		*(slot->presetSlotUsed) = false;
		slot->preset->clear();
		*expSlotLabel(p) = "";
		if (preset == p) preset = -1;
	}

	void presetSetCount(int p) {
		if (preset >= p) preset = 0;
		presetCount = p;
		presetNext = -1;
	}

	void presetRandomize(int p) {
		TransitSlot* slot = expSlot(p);
		*(slot->presetSlotUsed) = true;
		slot->preset->clear();
		for (size_t i = 0; i < sourceHandles.size(); i++) {
			float v = 0.f;
			{
				ParamQuantity* pq = getParamQuantity(sourceHandles[i]);
				if (!pq) goto s;
				pq->randomize();
				v = pq->getValue();
			}
			s:
			slot->preset->push_back(v);
		}
		assert(sourceHandles.size() == slot->preset->size());
		preset = p;
	}

	void presetCopyPaste(int source, int target) {
		TransitSlot* sourceSlot = expSlot(source);
		TransitSlot* targetSlot = expSlot(target);
		if (!*(sourceSlot->presetSlotUsed)) return;
		*(targetSlot->presetSlotUsed) = true;
		auto sourcePreset = sourceSlot->preset;
		auto targetPreset = targetSlot->preset;
		targetPreset->clear();
		for (auto v : *sourcePreset) {
			targetPreset->push_back(v);
		}
		if (preset == target) preset = -1;
	}

	void presetShiftBack(int p) {
		for (int i = presetTotal - 2; i >= p; i--) {
			TransitSlot* slot = expSlot(i);
			if (*(slot->presetSlotUsed)) {
				presetCopyPaste(i, i + 1);
				*expSlotLabel(i + 1) = *expSlotLabel(i);
			}
			else {
				presetClear(i + 1);
			}
		}
		presetClear(p);
	}

	void presetShiftFront(int p) {
		for (int i = 1; i <= p; i++) {
			TransitSlot* slot = expSlot(i);
			if (*(slot->presetSlotUsed)) {
				presetCopyPaste(i, i - 1);
				*expSlotLabel(i - 1) = *expSlotLabel(i);
			}
			else {
				presetClear(i - 1);
			}
		}
		presetClear(p);
	}

	void setProcessDivision(int d) {
		presetProcessDivision = d;
		presetProcessDivider.setDivision(presetProcessDivision);
		presetProcessDivider.reset();
	}

	int getProcessDivision() {
		return presetProcessDivision;
	}

	void setCvMode(SLOTCVMODE mode) {
		slotCvMode = slotCvModeBak = mode;
		if (slotCvMode == SLOTCVMODE::PHASE) outMode = OUTMODE::PHASE;
		else if (outMode == OUTMODE::PHASE) outMode = OUTMODE::ENV;
	}

	void setOutMode(OUTMODE mode) {
		outMode = mode;
		if (slotCvMode == SLOTCVMODE::PHASE) outMode = OUTMODE::PHASE;
		else if (outMode == OUTMODE::PHASE) outMode = OUTMODE::ENV;
	}

	int transitSlotCmd(SLOT_CMD cmd, int i) override {
		switch (cmd) {
			case SLOT_CMD::LOAD:
				presetLoad(i); 
				return -1;
			case SLOT_CMD::CLEAR:
				presetClear(i);
				return -1;
			case SLOT_CMD::RANDOMIZE:
				presetRandomize(i);
				return -1;
			case SLOT_CMD::COPY:
				presetCopy = *expSlot(i)->presetSlotUsed ? i : -1;
				return -1;
			case SLOT_CMD::PASTE_PREVIEW:
				return presetCopy;
			case SLOT_CMD::PASTE:
				presetCopyPaste(presetCopy, i);
				return -1;
			case SLOT_CMD::SAVE:
				presetSave(i);
				return -1;
			case SLOT_CMD::SHIFT_BACK:
				presetShiftBack(i);
				return -1;
			case SLOT_CMD::SHIFT_FRONT:
				presetShiftFront(i);
				return -1;
			default:
				return -1;
		}
	}

	json_t* dataToJson() override {
		json_t* rootJ = BASE::dataToJson();
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

		return rootJ;
	}

	void dataFromJson(json_t* rootJ) override {
		BASE::panelTheme = json_integer_value(json_object_get(rootJ, "panelTheme"));
		mappingIndicatorHidden = json_boolean_value(json_object_get(rootJ, "mappingIndicatorHidden"));
		presetProcessDivision = json_integer_value(json_object_get(rootJ, "presetProcessDivision"));

		slotCvMode = (SLOTCVMODE)json_integer_value(json_object_get(rootJ, "slotCvMode"));
		outMode = (OUTMODE)json_integer_value(json_object_get(rootJ, "outMode"));
		preset = json_integer_value(json_object_get(rootJ, "preset"));
		presetCount = json_integer_value(json_object_get(rootJ, "presetCount"));

		if (preset >= presetCount) {
			preset = -1;
		}

		// Hack for preventing duplicating this module
		if (APP->engine->getModule(BASE::id) != NULL && !BASE::idFixHasMap()) return;

		inChange = true;
		json_t* sourceMapsJ = json_object_get(rootJ, "sourceMaps");
		if (sourceMapsJ) {
			json_t* sourceMapJ;
			size_t sourceMapIndex;
			json_array_foreach(sourceMapsJ, sourceMapIndex, sourceMapJ) {
				json_t* moduleIdJ = json_object_get(sourceMapJ, "moduleId");
				int moduleId = json_integer_value(moduleIdJ);
				json_t* paramIdJ = json_object_get(sourceMapJ, "paramId");
				int paramId = json_integer_value(paramIdJ);

				moduleId = BASE::idFix(moduleId);
				ParamHandle* sourceHandle = new ParamHandle;
				sourceHandle->text = "stoermelder TRANSIT";
				APP->engine->addParamHandle(sourceHandle);
				APP->engine->updateParamHandle(sourceHandle, moduleId, paramId, false);
				sourceHandles.push_back(sourceHandle);
			}
		}
		inChange = false;

		BASE::idFixClearMap();
		BASE::dataFromJson(rootJ);
		Module::params[PARAM_CTRLMODE].setValue(0.f);
	}
};

template <int NUM_PRESETS>
struct TransitWidget : ThemedModuleWidget<TransitModule<NUM_PRESETS>> {
	typedef TransitWidget<NUM_PRESETS> WIDGET;
	typedef ThemedModuleWidget<TransitModule<NUM_PRESETS>> BASE;
	typedef TransitModule<NUM_PRESETS> MODULE;
	
	int learn = 0;

	TransitWidget(MODULE* module)
		: ThemedModuleWidget<MODULE>(module, "Transit") {
		BASE::setModule(module);

		BASE::addChild(createWidget<StoermelderBlackScrew>(Vec(RACK_GRID_WIDTH, 0)));
		BASE::addChild(createWidget<StoermelderBlackScrew>(Vec(BASE::box.size.x - 2 * RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));

		BASE::addChild(createLightCentered<TinyLight<YellowLight>>(Vec(10.4f, 46.2f), module, MODULE::LIGHT_CV));
		BASE::addInput(createInputCentered<StoermelderPort>(Vec(21.7f, 58.9f), module, MODULE::INPUT_CV));
		BASE::addInput(createInputCentered<StoermelderPort>(Vec(21.7f, 94.2f), module, MODULE::INPUT_RESET));

		BASE::addParam(createParamCentered<LEDSliderWhite>(Vec(21.7f, 166.7f), module, MODULE::PARAM_FADE));
		BASE::addInput(createInputCentered<StoermelderPort>(Vec(21.7f, 221.4f), module, MODULE::INPUT_FADE));

		BASE::addParam(createParamCentered<StoermelderTrimpot>(Vec(21.7f, 255.8f), module, MODULE::PARAM_SHAPE));
		BASE::addOutput(createOutputCentered<StoermelderPort>(Vec(21.7f, 300.3f), module, MODULE::OUTPUT));

		BASE::addParam(createParamCentered<CKSSThreeH>(Vec(21.7f, 336.2f), module, MODULE::PARAM_CTRLMODE));

		BASE::addChild(createLightCentered<TinyLight<WhiteLight>>(Vec(60.f, 355.7f), module, MODULE::LIGHT_LEARN));

		for (size_t i = 0; i < NUM_PRESETS; i++) {
			float o = i * (288.7f / (NUM_PRESETS - 1));
			TransitLedButton<NUM_PRESETS>* ledButton = createParamCentered<TransitLedButton<NUM_PRESETS>>(Vec(60.0f, 45.4f + o), module, MODULE::PARAM_PRESET + i);
			ledButton->module = module;
			ledButton->id = i;
			BASE::addParam(ledButton);
			BASE::addChild(createLightCentered<LargeLight<RedGreenBlueLight>>(Vec(60.0f, 45.4f + o), module, MODULE::LIGHT_PRESET + i * 3));
		}
	}
	
	void onHoverKey(const event::HoverKey& e) override {
		if (e.action == GLFW_PRESS && (e.mods & RACK_MOD_MASK) == GLFW_MOD_SHIFT) {
			MODULE* module = dynamic_cast<MODULE*>(this->module);
			switch (e.key) {
				case GLFW_KEY_B:
					enableLearn(2);
					e.consume(this);
					break;
				case GLFW_KEY_A:
					enableLearn(3);
					e.consume(this);
					break;
				case GLFW_KEY_Q:
					module->slotCvMode = module->slotCvMode == SLOTCVMODE::OFF ? module->slotCvModeBak : SLOTCVMODE::OFF;
					e.consume(this);
					break;
			}
		}
		BASE::onHoverKey(e);
	}

	void onDeselect(const event::Deselect& e) override {
		if (learn == 0) return;
		MODULE* module = dynamic_cast<MODULE*>(this->module);

		if (learn == 1) {
			DEFER({
				disableLearn();
			});

			// Learn module
			Widget* w = APP->event->getDraggedWidget();
			if (!w) return;
			ModuleWidget* mw = dynamic_cast<ModuleWidget*>(w);
			if (!mw) mw = w->getAncestorOfType<ModuleWidget>();
			if (!mw || mw == this) return;
			Module* m = mw->module;
			if (!m) return;
			module->bindModule(m);
		}

		if (learn == 2 || learn == 3) {
			// Check if a ParamWidget was touched, unstable API
			ParamWidget* touchedParam = APP->scene->rack->touchedParam;
			if (touchedParam && touchedParam->getParamQuantity()->module != module) {
				APP->scene->rack->touchedParam = NULL;
				int moduleId = touchedParam->getParamQuantity()->module->id;
				int paramId = touchedParam->getParamQuantity()->paramId;
				module->bindParameter(moduleId, paramId);
				if (learn == 2) { 
					disableLearn();
				}
			}
			else {
				disableLearn();
			}
		}
	}

	void step() override {
		if (learn == 3 && APP->event->getSelectedWidget() != this) {
			APP->event->setSelectedWidget(this);
		}
		if (BASE::module) {
			BASE::module->lights[MODULE::LIGHT_LEARN].setBrightness(learn > 0);
		}
		BASE::step();
	}

	void enableLearn(int mode) {
		learn = learn != mode ? mode : 0;
		APP->scene->rack->touchedParam = NULL;
		APP->event->setSelectedWidget(this);
		GLFWcursor* cursor = NULL;
		if (learn != 0) {
			cursor = glfwCreateStandardCursor(GLFW_CROSSHAIR_CURSOR);
		}
		glfwSetCursor(APP->window->win, cursor);
	}

	void disableLearn() {
		learn = 0;
		glfwSetCursor(APP->window->win, NULL);
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
					module->setProcessDivision(division);
				}
				void step() override {
					MenuItem::text = string::f("%s (%i Hz)", text.c_str(), module->sampleRate / division);
					rightText = module->getProcessDivision() == division ? "✔" : "";
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
				std::string rightTextEx = "";
				void onAction(const event::Action& e) override {
					module->setCvMode(slotCvMode);
				}
				void step() override {
					rightText = string::f("%s %s", module->slotCvMode == slotCvMode ? "✔" : "", rightTextEx.c_str());
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
				menu->addChild(construct<SlotCvModeItem>(&MenuItem::text, "Trigger alternating", &SlotCvModeItem::module, module, &SlotCvModeItem::slotCvMode, SLOTCVMODE::TRIG_ALT));
				menu->addChild(construct<SlotCvModeItem>(&MenuItem::text, "Trigger random", &SlotCvModeItem::module, module, &SlotCvModeItem::slotCvMode, SLOTCVMODE::TRIG_RANDOM));
				menu->addChild(construct<SlotCvModeItem>(&MenuItem::text, "Trigger pseudo-random", &SlotCvModeItem::module, module, &SlotCvModeItem::slotCvMode, SLOTCVMODE::TRIG_RANDOM_WO_REPEAT));
				menu->addChild(construct<SlotCvModeItem>(&MenuItem::text, "Trigger random walk", &SlotCvModeItem::module, module, &SlotCvModeItem::slotCvMode, SLOTCVMODE::TRIG_RANDOM_WALK));
				menu->addChild(construct<SlotCvModeItem>(&MenuItem::text, "Trigger shuffle", &SlotCvModeItem::module, module, &SlotCvModeItem::slotCvMode, SLOTCVMODE::TRIG_SHUFFLE));
				menu->addChild(construct<SlotCvModeItem>(&MenuItem::text, "0..10V", &SlotCvModeItem::module, module, &SlotCvModeItem::slotCvMode, SLOTCVMODE::VOLT));
				menu->addChild(construct<SlotCvModeItem>(&MenuItem::text, "C4", &SlotCvModeItem::module, module, &SlotCvModeItem::slotCvMode, SLOTCVMODE::C4));
				menu->addChild(construct<SlotCvModeItem>(&MenuItem::text, "Arm", &SlotCvModeItem::module, module, &SlotCvModeItem::slotCvMode, SLOTCVMODE::ARM));
				menu->addChild(new MenuSeparator);
				menu->addChild(construct<SlotCvModeItem>(&MenuItem::text, "Phase", &SlotCvModeItem::module, module, &SlotCvModeItem::slotCvMode, SLOTCVMODE::PHASE));
				menu->addChild(new MenuSeparator);
				menu->addChild(construct<SlotCvModeItem>(&MenuItem::text, "Off", &SlotCvModeItem::rightTextEx, RACK_MOD_SHIFT_NAME "+Q", &SlotCvModeItem::module, module, &SlotCvModeItem::slotCvMode, SLOTCVMODE::OFF));
				return menu;
			}
		};

		struct OutModeMenuItem : MenuItem {
			struct OutModeItem : MenuItem {
				MODULE* module;
				OUTMODE outMode;
				void onAction(const event::Action& e) override {
					module->setOutMode(outMode);
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
				bool phaseMode = module->slotCvMode == SLOTCVMODE::PHASE;
				Menu* menu = new Menu;
				menu->addChild(construct<OutModeItem>(&MenuItem::text, "Envelope", &OutModeItem::module, module, &OutModeItem::outMode, OUTMODE::ENV, &OutModeItem::disabled, phaseMode));
				menu->addChild(construct<OutModeItem>(&MenuItem::text, "Gate", &OutModeItem::module, module, &OutModeItem::outMode, OUTMODE::GATE, &OutModeItem::disabled, phaseMode));
				menu->addChild(construct<OutModeItem>(&MenuItem::text, "Trigger snapshot change", &OutModeItem::module, module, &OutModeItem::outMode, OUTMODE::TRIG_SNAPSHOT, &OutModeItem::disabled, phaseMode));
				menu->addChild(construct<OutModeItem>(&MenuItem::text, "Trigger fade start", &OutModeItem::module, module, &OutModeItem::outMode, OUTMODE::TRIG_SOC, &OutModeItem::disabled, phaseMode));
				menu->addChild(construct<OutModeItem>(&MenuItem::text, "Trigger fade end", &OutModeItem::module, module, &OutModeItem::outMode, OUTMODE::TRIG_EOC, &OutModeItem::disabled, phaseMode));
				menu->addChild(new MenuSeparator);
				menu->addChild(construct<OutModeItem>(&MenuItem::text, "Polyphonic", &OutModeItem::module, module, &OutModeItem::outMode, OUTMODE::POLY, &OutModeItem::disabled, phaseMode));
				menu->addChild(new MenuSeparator);
				menu->addChild(construct<OutModeItem>(&MenuItem::text, "Phase", &OutModeItem::module, module, &OutModeItem::outMode, OUTMODE::PHASE, &OutModeItem::disabled, !phaseMode));
				return menu;
			}
		};

		struct BindModuleItem : MenuItem {
			MODULE* module;
			WIDGET* widget;
			void onAction(const event::Action& e) override {
				widget->disableLearn();
				module->bindModuleExpander();
			}
		};

		struct BindModuleSelectItem : MenuItem {
			WIDGET* widget;
			void onAction(const event::Action& e) override {
				widget->enableLearn(1);
			}
		};

		struct BindParameterItem : MenuItem {
			WIDGET* widget;
			int mode;
			std::string rightText = "";
			void onAction(const event::Action& e) override {
				widget->enableLearn(mode);
			}
			void step() override {
				MenuItem::rightText = widget->learn == mode ? "Active" : rightText;
				MenuItem::step();
			}
		};

		struct ParameterMenuItem : MenuItem {
			struct ParameterItem : MenuItem {
				struct IndicateItem : MenuItem {
					MODULE* module;
					ParamHandle* handle;
					void onAction(const event::Action& e) override {
						ModuleWidget* mw = APP->scene->rack->getModule(handle->moduleId);
						module->paramHandleIndicator.handle = handle;
						module->paramHandleIndicator.indicate(mw);
					}
				};

				struct UnbindItem : MenuItem {
					MODULE* module;
					ParamHandle* handle;
					void onAction(const event::Action& e) override {
						APP->engine->updateParamHandle(handle, -1, 0, true);
					}
				};

				MODULE* module;
				ParamHandle* handle;
				ParameterItem() {
					rightText = RIGHT_ARROW;
				}
				Menu* createChildMenu() override {
					Menu* menu = new Menu;
					menu->addChild(construct<IndicateItem>(&MenuItem::text, "Locate and indicate", &IndicateItem::module, module, &IndicateItem::handle, handle));
					menu->addChild(construct<UnbindItem>(&MenuItem::text, "Unbind", &UnbindItem::module, module, &UnbindItem::handle, handle));
					return menu;
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
					
					std::string text = string::f("%s %s", moduleWidget->model->name.c_str(), paramWidget->getParamQuantity()->getLabel().c_str());
					menu->addChild(construct<ParameterItem>(&MenuItem::text, text, &ParameterItem::module, module, &ParameterItem::handle, handle));
				}
				return menu;
			}
		};

		struct ModuleMenuItem : MenuItem {
			MODULE* module;
			ModuleMenuItem() {
				rightText = RIGHT_ARROW;
			}

			Menu* createChildMenu() override {
				struct UnbindItem : MenuItem {
					MODULE* module;
					int moduleId;
					void onAction(const event::Action& e) override {
						for (size_t i = 0; i < module->sourceHandles.size(); i++) {
							ParamHandle* handle = module->sourceHandles[i];
							if (handle->moduleId != moduleId) continue;
							APP->engine->updateParamHandle(handle, -1, 0, true);
						}
					}
				};

				Menu* menu = new Menu;
				std::set<int> moduleIds;
				for (size_t i = 0; i < module->sourceHandles.size(); i++) {
					ParamHandle* handle = module->sourceHandles[i];
					if (moduleIds.find(handle->moduleId) == moduleIds.end())
						moduleIds.insert(handle->moduleId);
				}

				for (int moduleId : moduleIds) {
					ModuleWidget* moduleWidget = APP->scene->rack->getModule(moduleId);
					if (!moduleWidget) continue;	
					std::string text = string::f("Unbind \"%s %s\"", moduleWidget->model->plugin->name.c_str(), moduleWidget->model->name.c_str());
					menu->addChild(construct<UnbindItem>(&MenuItem::text, text, &UnbindItem::module, module, &UnbindItem::moduleId, moduleId));
				}
				return menu;
			}
		};

		menu->addChild(new MenuSeparator());
		menu->addChild(construct<MappingIndicatorHiddenItem>(&MenuItem::text, "Hide mapping indicators", &MappingIndicatorHiddenItem::module, module));
		menu->addChild(construct<PrecisionMenuItem>(&MenuItem::text, "Precision", &PrecisionMenuItem::module, module));
		menu->addChild(new MenuSeparator());
		menu->addChild(construct<SlotCvModeMenuItem>(&MenuItem::text, "Port CV mode", &SlotCvModeMenuItem::module, module));
		menu->addChild(construct<OutModeMenuItem>(&MenuItem::text, "Port OUT mode", &OutModeMenuItem::module, module));
		menu->addChild(new MenuSeparator());
		menu->addChild(construct<BindModuleItem>(&MenuItem::text, "Bind module (left)", &BindModuleItem::widget, this, &BindModuleItem::module, module));
		menu->addChild(construct<BindModuleSelectItem>(&MenuItem::text, "Bind module (select)", &BindModuleSelectItem::widget, this));
		menu->addChild(construct<BindParameterItem>(&MenuItem::text, "Bind single parameter", &BindParameterItem::rightText, RACK_MOD_SHIFT_NAME "+B", &BindParameterItem::widget, this, &BindParameterItem::mode, 2));
		menu->addChild(construct<BindParameterItem>(&MenuItem::text, "Bind multiple parameters", &BindParameterItem::rightText, RACK_MOD_SHIFT_NAME "+A", &BindParameterItem::widget, this, &BindParameterItem::mode, 3));

		if (module->sourceHandles.size() > 0) {
			menu->addChild(new MenuSeparator());
			menu->addChild(construct<ModuleMenuItem>(&MenuItem::text, "Bound modules", &ModuleMenuItem::module, module));
			menu->addChild(construct<ParameterMenuItem>(&MenuItem::text, "Bound parameters", &ParameterMenuItem::module, module));
		}
	}
};

} // namespace Transit
} // namespace StoermelderPackOne

Model* modelTransit = createModel<StoermelderPackOne::Transit::TransitModule<12>, StoermelderPackOne::Transit::TransitWidget<12>>("Transit");