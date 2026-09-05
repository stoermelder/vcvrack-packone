#include "../../plugin.hpp"
#include "../../utils/cursor.hpp"
#include "../../utils/digital.hpp"
#include "../../utils/ShapedSlewLimiter.hpp"
#include "../../utils/TaskProcessor.hpp"
#include "../../utils/SpscLatestValue.hpp"
#include "../../components/Knobs.hpp"
#include "../../components/ParamHandleIndicator.hpp"
#include "TransitBase.hpp"
#include "tipsy-encoder/include/tipsy/tipsy.h"
#include <random>

namespace StoermelderPackOne {
namespace Transit {

const int MAX_EXPANDERS = 15;

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
	OFF = -2,
	POLY = -1,
	ENV = 0,
	GATE = 1,
	TRIG_SNAPSHOT = 4,
	TRIG_SOC = 3,
	TRIG_EOC = 2,
	PHASE = 5,
	TIPSY = 6
};

struct ParamHandleEx : ParamHandleIndicator {
	bool isSwitch = false;
};


template <int NUM_PRESETS>
struct TransitModule : TransitBase<NUM_PRESETS>, TransitPadMaster, TransitCtrlMaster, ModuleChangeListener {
	typedef TransitBase<NUM_PRESETS> BASE;
	typedef typename TransitBase<NUM_PRESETS>::Slot SLOT;

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
	/** [Stored to JSON] */
	int presetFirst;
	/** [Stored to JSON] Last active snapshot (exclusive) */
	int presetLast;
	/** [Stored to JSON] */
	bool presetCountLongPress = true;

	/** Total number of snapshots including expanders */
	int presetTotal;
	int presetNext;
	int presetCopy = -1;
	float presetPhaseLast = -1.f;

	/** Holds the last values on transitions */
	std::vector<float> presetOld;
	std::vector<float> presetNew;
	float presetFadeTime;

	/** [Stored to JSON] */
	bool clampFadeCv = true;

	/** [Stored to JSON] mode for SEQ CV input */
	SLOTCVMODE slotCvMode = SLOTCVMODE::TRIG_FWD;
	SLOTCVMODE slotCvModeBak = SLOTCVMODE::OFF;
	int slotCvModeDir = 1;
	int slotCvModeAlt = 0;
	std::vector<int> slotCvModeShuffle;

	/** [Stored to JSON] */
	OUTMODE outMode;
	bool outEocArm;
	bool processing = false;
	dsp::PulseGenerator outSlotPulseGenerator;
	dsp::PulseGenerator outSocPulseGenerator;
	dsp::PulseGenerator outEocPulseGenerator;

	tipsy::ProtocolEncoder tipsyEncoder;
	std::string tipsyCurrentLabel;

	/** [Stored to JSON] */
	bool mappingIndicatorHidden = false;
	/** [Stored to JSON] */
	int presetProcessDivision;
	ClockDividerEx presetProcessDivider;

	std::default_random_engine randGen{(uint16_t)std::chrono::system_clock::now().time_since_epoch().count()};
	std::uniform_int_distribution<int> randDist;

	/** [Stored to JSON] */
	/*	This is owned by the engine thread */
	std::vector<ParamHandleEx*> sourceHandles;
	/*  Snapshot published for UI thread (engine writes, UI reads). */
	SpscLatestValue<std::vector<ParamHandleEx*>> sourceHandlesPtr;

	/** [Stored to JSON] */
	bool parameterChangesDirect = false;

	dsp::SchmittTrigger slotTrigger;
	dsp::SchmittTrigger slotC4Trigger;
	dsp::SchmittTrigger resetTrigger;
	dsp::Timer resetTimer;

	StoermelderShapedSlewLimiter slewLimiter;
	ClockDividerEx handleDivider;
	ClockDividerEx buttonDivider;

	ClockDividerEx lightDivider;
	dsp::Timer lightTimer;
	bool lightBlink = false;

	int sampleRate;

	TransitBase<NUM_PRESETS>* N[MAX_EXPANDERS + 1];
	TransitPadInterface* transitPad;
	
	TaskProcessor<256> taskProcessorDsp;
	TaskProcessor<256> taskProcessorUi;

	dsp::RingBuffer<CtrlChange, 16> ctrlChangeQueue;
	TransitCtrlReceiver* ctrlReceiver = nullptr;

	TransitModule() {
		BASE::panelTheme = pluginSettings.panelThemeDefault;
		registerModuleListener("Transit", this);
		sourceHandlesPtr.store({});

		Module::config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);
		Module::configSwitch(PARAM_CTRLMODE, 0.f, 2.f, 0.f, "Operating mode", {"Read", "Auto", "Write"});
		Module::paramQuantities[PARAM_CTRLMODE]->description = "Read: morph through presets with the CV input.\nAuto: auto-save snapshots on preset-change.\nWrite: snapshot the currently mapped parameters as a preset.";
		for (int i = 0; i < NUM_PRESETS; i++) {
			TransitParamQuantity<NUM_PRESETS>* pq = Module::configParam<TransitParamQuantity<NUM_PRESETS>>(PARAM_PRESET + i, 0, 1, 0);
			pq->module = this;
			pq->id = i;
			BASE::presetButton[i].param = &Module::params[PARAM_PRESET + i];

			BASE::slot[i].owner = this;
			BASE::slot[i].index = i;
			BASE::slot[i].indexParam = PARAM_PRESET + i;
			BASE::slot[i].indexLight = LIGHT_PRESET + i * 3;
		}
		Module::configParam(PARAM_FADE, 0.f, 1.f, 0.5f, "Fade");
		Module::paramQuantities[PARAM_FADE]->description = "Crossfade amount between presets.";
		Module::configParam(PARAM_SHAPE, -1.f, 1.f, 0.f, "Shape");
		Module::paramQuantities[PARAM_SHAPE]->description = "Shape of the crossfade: linear (0), ease-in (<0), or ease-out (>0).";
		Module::configInput(INPUT_CV, "CV");
		Module::inputInfos[INPUT_CV]->description = "Trigger/gate or CV that drives the transition between presets (operating mode selected on the context menu).";
		Module::configInput(INPUT_RESET, "Reset trigger");
		Module::inputInfos[INPUT_RESET]->description = "Resets the current position in the preset chain.";
		Module::configInput(INPUT_FADE, "Fade CV");
		Module::inputInfos[INPUT_FADE]->description = "Optional CV for the fade amount, summed with the FADE knob.";
		Module::configOutput(OUTPUT, "Envelope/trigger");
		Module::outputInfos[OUTPUT]->description = "Outputs a transition envelope, trigger, or gate depending on the input and operating mode.";

		handleDivider.setDivision(4096);
		buttonDivider.setDivision(128);
		reset(true);
	}

	~TransitModule() {
		unregisterModuleListener("Transit", this);
		for (ParamHandle* sourceHandle : sourceHandles) {
			APP->engine->removeParamHandle(sourceHandle);
			delete sourceHandle;
		}
	}

	void onSampleRateChange(const Module::SampleRateChangeEvent& e) override {
		lightDivider.setDivision(e.sampleRate / 100.f);
	}

	void onExpanderChange(const Module::ExpanderChangeEvent& e) override {
		notifyModuleListeners("Transit");
	}

	void onReset(const Module::ResetEvent& e) override {
		reset(false, true);
		Module::onReset(e);
	}

	void reset(bool stateOnly, bool createUiTask = false) {
		moduleChangedFlag = true;

		if (!stateOnly) {
			taskProcessorUi.enqueue([=]() { bindClearParameterRequest(); });
		}

		for (int i = 0; i < NUM_PRESETS; i++) {
			BASE::presetSlotUsed[i] = false;
			BASE::textLabel[i] = "";
			BASE::fadeTime[i] = -1.f;
			BASE::preset[i].clear();
			BASE::slotColorSet[i] = false;
			BASE::slotColor[i] = color::WHITE;
		}

		BASE::ctrlUniqueId = (int64_t)(rack::random::uniform() * (float)INT64_MAX);
		preset = -1;
		presetFirst = 0;
		presetLast = NUM_PRESETS;
		presetNext = -1;
		slewLimiter.reset(10.f);
		clampFadeCv = true;

		outMode = OUTMODE::ENV;
		outSlotPulseGenerator.reset();
		outSocPulseGenerator.reset();
		outEocPulseGenerator.reset();

		mappingIndicatorHidden = false;
		presetProcessDivision = settings::isPlugin ? 256 : 64;
		presetProcessDivider.setDivision(presetProcessDivision);
		presetProcessDivider.reset();
		
		parameterChangesDirect = false;
	}

	inline SLOT* getSlot(int index) {
		if (index >= presetTotal) return NULL;
		int n = index / NUM_PRESETS;
		assert(n < MAX_EXPANDERS + 1);
		return &N[n]->slot[index % NUM_PRESETS];
	}

	// TransitPadMaster
	int getSelectedSlot() override {
		return preset;
	}

	std::string getSlotLabel(int i) override {
		SLOT* slot = getSlot(i);
		if (!slot) return "";
		return slot->getLabel();
	}

	bool getSlotOwner(int slotIndex, Module*& module, int& localIndex) override {
		if (slotIndex < 0 || slotIndex >= presetTotal) return false;
		localIndex = slotIndex % NUM_PRESETS;
		module = N[slotIndex / NUM_PRESETS];
		return true;
	}

	void process(const Module::ProcessArgs& args) override {
		sampleRate = args.sampleRate;

		// Apply any parameter changes enqueued by TransitCtrl
		while (!ctrlChangeQueue.empty()) {
			CtrlChange change = ctrlChangeQueue.shift();
			ParamQuantity* pq = getCtrlParamQuantity(change.index);
			if (pq) pq->getParam()->setValue(change.value);
		}

		CTRLMODE ctrlMode = (CTRLMODE)Module::params[PARAM_CTRLMODE].getValue();

		if (moduleChangedFlag || ctrlMode != BASE::ctrlMode) {
			presetTotal = NUM_PRESETS;
			transitPad = nullptr;
			Module* m = this;
			TransitBase<NUM_PRESETS>* t = this;
			t->ctrlMode = ctrlMode;
			int c = 0;

			while (true) {
				N[c] = t;
				c++;
				if (c == MAX_EXPANDERS + 1) break;

				Module* exp = m->rightExpander.module;
				if (!exp) break;	
				if (exp->model == modelTransitPad) {
					transitPad = dynamic_cast<TransitPadInterface*>(exp);
					transitPad->masterModule = this;
					slotCvMode = SLOTCVMODE::OFF;
					outMode = OUTMODE::OFF;
					break;
				}
				if (exp->model != modelTransitEx) break;
				m = exp;
				t = dynamic_cast<TransitBase<NUM_PRESETS>*>(exp);
				if (t->ctrlUniqueId != BASE::ctrlUniqueId) expanderCleanUp(t);
				t->panelTheme = BASE::panelTheme;
				t->ctrlModuleId = Module::id;
				t->ctrlOffset = c;
				t->ctrlMode = BASE::ctrlMode;
				presetTotal += NUM_PRESETS;
			}

			// Right side: TransitCtrl may sit at the end of the chain, after all TransitEx
			// expanders. Transit pushes its TransitCtrlMaster pointer into TransitCtrl so
			// proxy PQs activate. The walk is anchored at `m`, which is the last expander
			// reached by the TransitEx scan above (or `this` if no TransitEx is present).
			ctrlReceiver = nullptr;
			{
				Module* afterEx = m->rightExpander.module;
				if (afterEx && afterEx->model == modelTransitCtrl) {
					ctrlReceiver = dynamic_cast<TransitCtrlReceiver*>(afterEx);
					if (ctrlReceiver) ctrlReceiver->setTransitCtrl(this);
				}
			}
			moduleChangedFlag = false;
		}
		int presetFirst = std::min(this->presetFirst, presetTotal);
		int presetLast = std::min(this->presetLast, presetTotal);

		if (handleDivider.process()) {
			float st = args.sampleTime * handleDivider.division;
			for (size_t i = 0; i < sourceHandles.size(); i++) {
				ParamHandleEx* sourceHandle = sourceHandles[i];
				sourceHandle->color = mappingIndicatorHidden ? color::BLACK_TRANSPARENT : nvgRGB(0x40, 0xff, 0xff);
				sourceHandle->process(st);
			}
		}

		// Read & Auto mode
		if (BASE::ctrlMode == CTRLMODE::READ || BASE::ctrlMode == CTRLMODE::AUTO) {
			// RESET input
			if (resetTrigger.process(Module::inputs[INPUT_RESET].getVoltage())) {
				resetTimer.reset();
				switch (slotCvMode) {
					case SLOTCVMODE::TRIG_FWD:
					case SLOTCVMODE::TRIG_RANDOM:
					case SLOTCVMODE::TRIG_RANDOM_WALK:
					case SLOTCVMODE::TRIG_RANDOM_WO_REPEAT: {
						presetLoad(presetFirst);
						break;
					}
					case SLOTCVMODE::TRIG_REV: {
						presetLoad(presetLast - 1);
						break;
					}
					case SLOTCVMODE::TRIG_PINGPONG: {
						slotCvModeDir = 1;
						presetLoad(presetFirst);
						break;
					}
					case SLOTCVMODE::TRIG_ALT: {
						slotCvModeDir = 1;
						slotCvModeAlt = 0;
						presetLoad(presetFirst);
						break;
					}
					case SLOTCVMODE::TRIG_SHUFFLE: {
						slotCvModeShuffle.clear();
						for (int i = presetFirst; i < presetLast; i++) {
							slotCvModeShuffle.push_back(i);
						}
						std::mt19937 rng(random::u32());
						std::shuffle(std::begin(slotCvModeShuffle), std::end(slotCvModeShuffle), rng);
						int p = std::min(std::max(presetFirst, slotCvModeShuffle.back()), presetLast - 1);
						slotCvModeShuffle.pop_back();
						presetLoad(p);
						break;
					}
					default: {
						break;
					}
				}
			} 
			else {
				resetTimer.process(args.sampleTime);
			}

			// CV input
			if (Module::inputs[INPUT_CV].isConnected()) {
				switch (slotCvMode) {
					case SLOTCVMODE::VOLT: {
						float voltage = clamp(Module::inputs[INPUT_CV].getVoltage(), 0.f, 10.f);
						int range = presetLast - presetFirst;
						int p = presetFirst + int((voltage / 10.f) * range);
						if (p >= presetLast) p = presetLast - 1;
						if (p < presetFirst) p = presetFirst;
						presetLoad(p);
						break;
					}
					case SLOTCVMODE::C4:
						presetLoad(std::round(clamp(Module::inputs[INPUT_CV].getVoltage() * 12.f, float(presetFirst), float(presetLast - 1))));
						if (Module::inputs[INPUT_CV].getChannels() == 2 && slotC4Trigger.process(Module::inputs[INPUT_CV].getVoltage(1))) {
							presetLoad(std::round(clamp(Module::inputs[INPUT_CV].getVoltage() * 12.f, float(presetFirst), float(presetLast - 1))), false, true);
						}
						break;
					case SLOTCVMODE::TRIG_FWD:
						if (slotTrigger.process(Module::inputs[INPUT_CV].getVoltage())) {
							if (resetTimer.getTime() >= 1e-3f) {
								int p = (preset + 1) % presetLast;
								if (p == 0) p = presetFirst;
								presetLoad(p);
							}
						}
						break;
					case SLOTCVMODE::TRIG_REV:
						if (slotTrigger.process(Module::inputs[INPUT_CV].getVoltage())) {
							if (resetTimer.getTime() >= 1e-3f) {
								int p = (preset - 1 + presetLast) % presetLast;
								if (p == presetFirst - 1) p = presetLast - 1;
								presetLoad(p);
							}
						}
						break;
					case SLOTCVMODE::TRIG_PINGPONG:
						if (slotTrigger.process(Module::inputs[INPUT_CV].getVoltage())) {
							if (resetTimer.getTime() >= 1e-3f) {
								int n = preset + slotCvModeDir;
								if (n >= presetLast - 1)
									slotCvModeDir = -1;
								if (n <= presetFirst)
									slotCvModeDir = 1;
								presetLoad(n);
							}
						}
						break;
					case SLOTCVMODE::TRIG_ALT:
						if (slotTrigger.process(Module::inputs[INPUT_CV].getVoltage())) {
							if (resetTimer.getTime() >= 1e-3f) {
								int n = presetFirst;
								if (preset == presetFirst) {
									n = slotCvModeAlt + slotCvModeDir;
									if (n >= presetLast - 1)
										slotCvModeDir = -1;
									if (n <= presetFirst)
										slotCvModeDir = 1;
									slotCvModeAlt = std::max(presetFirst, std::min(n, presetLast - 1));
								}
								presetLoad(n);
							}
						}
						break;
					case SLOTCVMODE::TRIG_RANDOM:
						if (slotTrigger.process(Module::inputs[INPUT_CV].getVoltage())) {
							if (randDist.min() != presetFirst || randDist.max() != presetLast - 1) randDist = std::uniform_int_distribution<int>(presetFirst, presetLast - 1);
							presetLoad(randDist(randGen));
						}
						break;
					case SLOTCVMODE::TRIG_RANDOM_WO_REPEAT:
						if (slotTrigger.process(Module::inputs[INPUT_CV].getVoltage())) {
							if (randDist.min() != presetFirst || randDist.max() != presetLast - 2) randDist = std::uniform_int_distribution<int>(presetFirst, presetLast - 2);
							int p = randDist(randGen);
							if (p >= preset) p++;
							presetLoad(p);
						}
						break;
					case SLOTCVMODE::TRIG_RANDOM_WALK:
						if (slotTrigger.process(Module::inputs[INPUT_CV].getVoltage())) {
							int p = std::min(std::max(presetFirst, preset + (random::u32() % 2 == 0 ? -1 : 1)), presetLast - 1);
							presetLoad(p);
						}
						break;
					case SLOTCVMODE::TRIG_SHUFFLE:
						if (slotTrigger.process(Module::inputs[INPUT_CV].getVoltage())) {
							if (slotCvModeShuffle.size() == 0) {
								for (int i = presetFirst; i < presetLast; i++) {
									slotCvModeShuffle.push_back(i);
								}
								std::mt19937 rng(random::u32());
								std::shuffle(std::begin(slotCvModeShuffle), std::end(slotCvModeShuffle), rng);
							}
							int p = std::min(std::max(presetFirst, slotCvModeShuffle.back()), presetLast - 1);
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
					SLOT* slot = getSlot(i);
					switch (slot->getPresetButton()->process(sampleTime)) {
						default:
						case LongPressButton::NO_PRESS:
							break;
						case LongPressButton::SHORT_PRESS:
							presetLoad(i, slotCvMode == SLOTCVMODE::ARM, true);
							break;
						case LongPressButton::LONG_PRESS:
							if (presetCountLongPress) presetSetLast(i + 1);
							break;
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
					SLOT* slot = getSlot(i);
					switch (slot->getPresetButton()->process(sampleTime)) {
						default:
						case LongPressButton::NO_PRESS:
							break;
						case LongPressButton::SHORT_PRESS:
							presetSave(i);
							break;
						case LongPressButton::LONG_PRESS:
							presetClear(i);
							break;
					}
				}
			}
		}

		if (isXyPadActive() && BASE::ctrlMode == CTRLMODE::READ) {
			presetProcessXyPad(args.sampleTime);
		}
		if (isPhaseCvActive() && BASE::ctrlMode == CTRLMODE::READ) {
			presetProcessPhase(args.sampleTime);
		} 
		else {
			presetProcess(args.sampleTime);
		}

		if (lightDivider.process()) {
			float s = args.sampleTime * lightDivider.getDivision();
			if (lightTimer.process(s) > 0.2f) {
				lightTimer.reset();
				lightBlink ^= true;
			}
			float intpart;
			float frac = std::modf(presetPhaseLast, &intpart);
			for (int i = 0; i < presetTotal; i++) {
				SLOT* slot = getSlot(i);
				bool u = slot->isUsed();

				if ((BASE::ctrlMode == CTRLMODE::READ || BASE::ctrlMode == CTRLMODE::AUTO) && isPhaseCvActive()) {
					float f = (intpart == i) ? (1.f - frac) : (intpart + 1 == i) ? (frac) : 0.f;
					float b1 = std::max(f, presetFirst <= i && i < presetLast ? (u ? 1.f : 0.25f) : 0.f);
					if (slot->isColorSet()) {
						NVGcolor c = slot->getColor();
						slot->getLights()[0].setBrightness(std::max(c.r, f));
						slot->getLights()[1].setBrightness(std::max(c.g, f));
						slot->getLights()[2].setBrightness(std::max(c.b, f));
					}
					else {
						slot->getLights()[0].setBrightness(b1);
						slot->getLights()[1].setBrightness(b1);
						slot->getLights()[2].setBrightness(b1);
					}
				}
				else {
					if (slot->isColorSet()) {
						float f = presetFirst <= i && i < presetLast ? (preset != i || lightBlink ? 1.f : 0.1f) : 0.f;
						NVGcolor c = slot->getColor();
						slot->getLights()[0].setBrightnessSmooth(c.r * f, s);
						slot->getLights()[1].setBrightnessSmooth(c.g * f, s);
						slot->getLights()[2].setBrightnessSmooth(c.b * f, s);
					}
					else {
						bool b = preset == i && lightBlink;
						//loat b0 = b ? 0.7f : (u ? 1.f : 0.f);
						float b1 = b ? 1.0f : (presetFirst <= i && i < presetLast ? (u ? 0.4f : 0.05f) : 0.f);
						//float b2 = b ? 0.7f : 0.f;
						slot->getLights()[0].setBrightnessSmooth(b1, s);
						slot->getLights()[1].setBrightnessSmooth(b1, s);
						slot->getLights()[2].setBrightnessSmooth(b1, s);
					}
				}
			}

			BASE::lights[LIGHT_CV].setBrightness((slotCvMode == SLOTCVMODE::OFF || (slotCvMode == SLOTCVMODE::PHASE && BASE::ctrlMode == CTRLMODE::WRITE)) && lightBlink);
		}

		taskProcessorDsp.process();
	}

	inline bool isXyPadActive() {
		return transitPad != nullptr;
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
		if (paramId >= (int)module->paramQuantities.size())
			return NULL;
		ParamQuantity* paramQuantity = module->paramQuantities[paramId];
		if (!paramQuantity)
			return NULL;
		return paramQuantity;
	}

	ParamQuantity* getCtrlParamQuantity(int index) override {
		auto snap = &sourceHandles;
		if (index < 0 || index >= (int)snap->size()) return nullptr;
		return getParamQuantity((*snap)[index]);
	}

	int getCtrlParamCount() override {
		auto snap = &sourceHandles;
		return (int)snap->size();
	}

	void pushCtrlChange(int index, float value) override {
		if (!ctrlChangeQueue.full())
			ctrlChangeQueue.push({index, value});
	}

	/** Bind all parameters of a module to be controlled by this Transit.
	 *  Called from the UI-thread.
	 */
	void bindAddModuleRequest(Module* m) {
		if (!m) return;
		for (size_t i = 0; i < m->params.size(); i++) {
			bindAddParameterRequest(m->id, i);
		}
	}

	/** Bind all parameters of the module next to Transit to be controlled by this Transit.
	 *  Called from the UI-thread.
	 */	 
	void bindAddModuleExpanderRequest() {
		Module::Expander* exp = &(Module::leftExpander);
		if (exp->moduleId < 0) return;
		Module* m = exp->module;
		bindAddModuleRequest(m);
	}

	/** Bind a parameter to be controlled by this Transit.
	 *  Called from the UI-thread.
	 */	 
	void bindAddParameterRequest(int64_t moduleId, int paramId, bool presetLoading = false) {
		const auto& snap = sourceHandlesPtr.peek();
		for (ParamHandle* handle : snap) {
			if (handle->moduleId == moduleId && handle->paramId == paramId) {
				// Parameter already bound
				return;
			}
		}

		ParamHandleEx* sourceHandle = new ParamHandleEx;
		sourceHandle->text = "stoermelder TRANSIT";
		APP->engine->addParamHandle(sourceHandle);
		APP->engine->updateParamHandle(sourceHandle, moduleId, paramId, true);

		ParamQuantity* pq = getParamQuantity(sourceHandle);
		SwitchQuantity* spq = dynamic_cast<SwitchQuantity*>(pq);
		sourceHandle->isSwitch = !!spq && pq->getMaxValue() != 1.f;

		taskProcessorDsp.enqueue([=]() {
			sourceHandles.push_back(sourceHandle);

			if (!presetLoading) {
				// Fill up exisitng snapshots with default values, but only if we are
				// not loading a preset into Tranit.
				float v = pq ? pq->getValue() : 0.f;
				for (int i = 0; i < presetTotal; i++) {
					SLOT* slot = getSlot(i);
					if (!slot->isUsed()) continue;
					slot->getPreset()->push_back(v);
					assert(sourceHandles.size() == slot->getPreset()->size());
				}
			}

			// Publish new sourceHandles snapshot for the dsp thread
			sourceHandlesPtr.store(sourceHandles);
		});
	}

	/** Request to clear all ParamHandles owned by this Transit.
	 *  Called from the UI-thread.
	 */
	void bindClearParameterRequest() {
		// Load to get the current snapshot
		const auto& snap = sourceHandlesPtr.peek();
		for (ParamHandle* sourceHandle : snap) {
			APP->engine->removeParamHandle(sourceHandle);
			delete sourceHandle;
		}
		
		if (snap.size() > 0) {
			taskProcessorDsp.enqueue([=]() {		
				sourceHandles.clear();	
				sourceHandlesPtr.store(sourceHandles);
			});
		}
	}

	void presetProcess(float sampleTime) {
		if (processing && presetProcessDivider.process()) {
			if (preset == -1) return;
			float deltaTime = sampleTime * presetProcessDivision;

			slewLimiter.clamp = clampFadeCv;
			float cv = BASE::inputs[INPUT_FADE].getVoltage() / 10.f;
			float base = presetFadeTime < 0.f ? BASE::params[PARAM_FADE].getValue() : presetFadeTime;
			float fade = base + cv;
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
				case OUTMODE::OFF:
				default:
					break;
			}

			float s10 = s / 10.f;
			for (size_t i = 0; i < sourceHandles.size(); i++) {
				ParamQuantity* pq = getParamQuantity(sourceHandles[i]);
				if (!pq) continue;
				if (presetOld.size() <= i) return;
				float oldValue = presetOld[i];
				if (presetNew.size() <= i) return;
				float newValue = presetNew[i];

				float v;
				if (sourceHandles[i]->isSwitch) {
					v = s10 < 0.5f ? oldValue : newValue;
					pq->getParam()->setValue(v);
				}
				else {
					v = crossfade(oldValue, newValue, s10);
					if (s10 > (1.f - 5e-3f) && std::abs(std::round(v) - v) < 5e-3f) v = std::round(v);
					if (settings::isPlugin && parameterChangesDirect)
						pq->setValue(v);
					else
						pq->getParam()->setValue(v);
				}
				if (ctrlReceiver) ctrlReceiver->setCtrlParamValue((int)i, v);
			}

			if (s == 10.f) {
				processing = false;
			}
		}

		if (outMode == OUTMODE::TIPSY) {
			float f = 0.f;
			if (!tipsyEncoder.isDormant()) {
				tipsyEncoder.getNextMessageFloat(f);
			}
			BASE::outputs[OUTPUT].setVoltage(f);
			BASE::outputs[OUTPUT].setChannels(1);
		}
	}

	void presetProcessXyPad(float sampleTime) {
		if (presetProcessDivider.process()) {
			const auto& snapshots = transitPad->getPadFactors();

			float weight = 0.f;
			std::vector<float> v(sourceHandles.size(), 0.f);
			for (auto snapshot : snapshots) {
				if (snapshot.id < 0) continue;
				SLOT* slot1 = getSlot(snapshot.id);
				if (!slot1 || !slot1->isUsed()) continue;
				weight += snapshot.weight;

				for (size_t i = 0; i < sourceHandles.size(); i++) {
					ParamQuantity* pq = getParamQuantity(sourceHandles[i]);
					if (!pq) continue;
					float v1 = (*slot1->getPreset())[i];
					v[i] += v1 * snapshot.weight;
				}
			}

			if (weight > 0.f) {
				for (size_t i = 0; i < sourceHandles.size(); i++) {
					ParamQuantity* pq = getParamQuantity(sourceHandles[i]);
					if (!pq) continue;
					if (settings::isPlugin && parameterChangesDirect)
						pq->setValue(v[i] / weight);
					else
						pq->getParam()->setValue(v[i] / weight);
				}
			}

			BASE::outputs[OUTPUT].setVoltage(0.f);
			BASE::outputs[OUTPUT].setChannels(1);
		}
	}

	void presetProcessPhase(float sampleTime) {
		if (presetProcessDivider.process()) {
			preset = -1;
			float deltaTime = sampleTime * presetProcessDivision;

			float p = clamp(BASE::inputs[INPUT_CV].getVoltage(), 0.f, 10.f);
			p = (presetLast - presetFirst - 1) * p / 10.f;

			float fade = BASE::inputs[INPUT_FADE].getVoltage() / 10.f + BASE::params[PARAM_FADE].getValue();
			slewLimiter.setRiseFall(fade, fade);
			float shape = BASE::params[PARAM_SHAPE].getValue();
			slewLimiter.setShape(shape);
			p = slewLimiter.process(p, deltaTime);

			p += presetFirst;
			if (presetPhaseLast == p) return;
			presetPhaseLast = p;

			int p1 = std::floor(p);
			SLOT* slot1 = getSlot(p1);
			while (p1 >= 0 && slot1 && !slot1->isUsed()) {
				p1--;
				slot1 = getSlot(p1);
			}
			
			int p2 = std::ceil(p);
			SLOT* slot2 = getSlot(p2);
			while (p2 <= presetLast - 1 && slot2 && !slot2->isUsed()) {
				p2++;
				slot2 = getSlot(p2);
			}
			
			if (p1 < 0 && p2 >= presetLast) return;
			if (p1 < 0) { p1 = p2; slot1 = slot2; }
			if (p2 >= presetLast) p2 = p1;
			
			if (p1 != p2) {
				p = (p - float(p1)) / (float(p2) - float(p1));
				for (size_t i = 0; i < sourceHandles.size(); i++) {
					ParamQuantity* pq = getParamQuantity(sourceHandles[i]);
					if (!pq) continue;
					float v1 = (*slot1->getPreset())[i];
					float v2 = (*slot2->getPreset())[i];
					float v = crossfade(v1, v2, p);
					if (settings::isPlugin && parameterChangesDirect)
						pq->setValue(v);
					else
						pq->getParam()->setValue(v);
					if (ctrlReceiver) ctrlReceiver->setCtrlParamValue((int)i, v);
				}
			}
			else {
				for (size_t i = 0; i < sourceHandles.size(); i++) {
					ParamQuantity* pq = getParamQuantity(sourceHandles[i]);
					if (!pq) continue;
					float v = (*slot1->getPreset())[i];
					if (settings::isPlugin && parameterChangesDirect)
						pq->setValue(v);
					else
						pq->getParam()->setValue(v);
					if (ctrlReceiver) ctrlReceiver->setCtrlParamValue((int)i, v);
				}
			}

			BASE::outputs[OUTPUT].setVoltage(presetPhaseLast / (presetLast - 1) * 10.f);
			BASE::outputs[OUTPUT].setChannels(1);
		}
	}

	void presetSetFirst(int offset) {
		if (offset > presetLast - 1) offset = presetLast - 1;
		if (preset < offset) preset = offset;
		presetFirst = offset;
		presetNext = -1;
	}

	void presetSetLast(int p) {
		if (p < presetFirst) p = presetFirst;
		if (preset >= p) preset = p - 1;
		presetLast = p;
		presetNext = -1;
	}

	/** Requests to load preset p.
	 *  Called from the UI thread.
	 */
	void presetLoadRequest(int p) {
		taskProcessorDsp.enqueue([=]() { presetLoad(p); });
	}

	/** Load preset p.
	 *  If isNext is true, preset p is loaded when the next trigger occurs.
	 *  If force is true, preset p is loaded even if it is already active.
	 *  Called from the engine thread only.
	 */
	void presetLoad(int p, bool isNext = false, bool force = false) {
		if (p < presetFirst || p >= presetLast)
			return;

		SLOT* slot = getSlot(p);
		if (!isNext) {
			if (p != preset || force) {
				int presetPrev = preset;
				preset = p;
				presetNext = -1;
				outSlotPulseGenerator.trigger();
				if (!slot->isUsed()) 
					return;
				if (BASE::ctrlMode == CTRLMODE::AUTO && presetPrev != -1) {
					SLOT* slotPrev = getSlot(presetPrev);
					if (slotPrev->isUsed()) {
						slotPrev->getPreset()->clear();
						for (size_t i = 0; i < sourceHandles.size(); i++) {
							ParamQuantity* pq = getParamQuantity(sourceHandles[i]);
							float v = pq ? pq->getValue() : 0.f;
							slotPrev->getPreset()->push_back(v);
						}
					}
				}
				slewLimiter.reset();
				outSocPulseGenerator.trigger();
				outEocArm = true;
				processing = true;
				presetFadeTime = getSlot(p)->getFadeTime();
				presetOld.clear();
				presetNew.clear();
				for (size_t i = 0; i < sourceHandles.size(); i++) {
					ParamQuantity* pq = getParamQuantity(sourceHandles[i]);
					presetOld.push_back(pq ? pq->getValue() : 0.f);
					if (slot->getPreset()->size() > i) {
						presetNew.push_back((*(slot->getPreset()))[i]);
					}
				}
			}
		}
		else {
			if (!slot->isUsed()) return;
			presetNext = p;
		}

		if (outMode == OUTMODE::TIPSY) {
			if (!tipsyEncoder.isDormant()) tipsyEncoder.terminateCurrentMessage();
			tipsyCurrentLabel = slot->isUsed() ? slot->getLabel() : "";
			if (tipsyCurrentLabel.empty()) tipsyCurrentLabel = string::f("Snapshot #%i", slot->index + 1);
			tipsyEncoder.initiateMessage(
				"text/plain",
				(uint32_t)tipsyCurrentLabel.length() + 1,
				(const unsigned char*)tipsyCurrentLabel.c_str()
			);
		}
	}

	/** Requests to save the current parameter values into the specified preset slot.
	 *  Called from the UI thread.
	 */
	void presetSaveRequest(int p) {
		taskProcessorDsp.enqueue([=]() { presetSave(p); });
	}

	/** Saves the current parameter values into the specified preset slot.
	 *  Called from the engine thread.
	 */
	void presetSave(int p) {
		SLOT* slot = getSlot(p);
		slot->setUsed(true);
		slot->getPreset()->clear();
		for (size_t i = 0; i < sourceHandles.size(); i++) {
			ParamQuantity* pq = getParamQuantity(sourceHandles[i]);
			float v = pq ? pq->getValue() : 0.f;
			slot->getPreset()->push_back(v);
		}
		assert(sourceHandles.size() == slot->getPreset()->size());
		preset = p;
	}

	/** Requests to clear the specified preset slot.
	 *  Called from the UI thread.
	 */
	void presetClearRequest(int p) {
		taskProcessorDsp.enqueue([=]() { presetClear(p); });
	}

	/** Clears the specified preset slot.
	 *  Called from the engine thread.
	 */
	void presetClear(int p) {
		SLOT* slot = getSlot(p);
		slot->setUsed(false);
		slot->getPreset()->clear();
		slot->setLabel("");
		slot->setFadeTime(-1.f);
		slot->setColor(color::WHITE, false);
		if (preset == p) preset = -1;
	}

	/**
	 * Requests randomize the specified preset slot.
	 * Called from the UI thread.
	 */
	void presetRandomizeRequest(int p) {
		taskProcessorDsp.enqueue([=]() { presetRandomize(p); });;
	}	

	/** Randomizes the specified preset slot.
	 *  Called from the engine thread.
	 */
	void presetRandomize(int p) {
		SLOT* slot = getSlot(p);
		slot->setUsed(true);
		slot->getPreset()->clear();
		for (size_t i = 0; i < sourceHandles.size(); i++) {
			float v = 0.f;
			{
				ParamQuantity* pq = getParamQuantity(sourceHandles[i]);
				if (!pq) goto s;
				pq->randomize();
				v = pq->getValue();
			}
			s:
			slot->getPreset()->push_back(v);
		}
		assert(sourceHandles.size() == slot->getPreset()->size());
		preset = p;
	}

	/** Requests copy-paste of the contents of one preset slot to another.
	 *  Called from the UI thread.
	 */
	void presetCopyPasteRequest(int source, int target) {
		taskProcessorDsp.enqueue([=]() { presetCopyPaste(source, target); });;
	}	

	/** Copies the contents of one preset slot to another.
	 *  Called from the engine thread.
	 */
	void presetCopyPaste(int source, int target) {
		SLOT* sourceSlot = getSlot(source);
		SLOT* targetSlot = getSlot(target);
		if (!sourceSlot->isUsed()) return;
		targetSlot->setUsed(true);
		std::vector<float>* sourcePreset = sourceSlot->getPreset();
		std::vector<float>* targetPreset = targetSlot->getPreset();
		targetPreset->clear();
		for (float v : *sourcePreset) {
			targetPreset->push_back(v);
		}
		if (preset == target) preset = -1;
	}

	/** Requests shift all presets back starting from the specified preset slot.
	 *  Called from the UI thread.
	 */
	void presetShiftBackRequest(int p) {
		taskProcessorDsp.enqueue([=]() { presetShiftBack(p); });
	}

	/** Shifts all presets back starting from the specified preset slot.
	 *  Called from the engine thread.
	 */
	void presetShiftBack(int p) {
		for (int i = presetTotal - 2; i >= p; i--) {
			SLOT* slot = getSlot(i);
			if (slot->isUsed()) {
				presetCopyPaste(i, i + 1);
				getSlot(i + 1)->setLabel(getSlot(i)->getLabel());
			}
			else {
				presetClear(i + 1);
			}
		}
		presetClear(p);
	}

	/** Requests shift all presets front starting from the specified preset slot.
	 *  Called from the UI thread.
	 */
	void presetShiftFrontRequest(int p) {
		taskProcessorDsp.enqueue([=]() { presetShiftFront(p); });
	}

	/** Shifts all presets front starting from the specified preset slot.
	 *  Called from the engine thread.
	 */
	void presetShiftFront(int p) {
		for (int i = 1; i <= p; i++) {
			SLOT* slot = getSlot(i);
			if (slot->isUsed()) {
				presetCopyPaste(i, i - 1);
				getSlot(i - 1)->setLabel(getSlot(i)->getLabel());
			}
			else {
				presetClear(i - 1);
			}
		}
		presetClear(p);
	}

	/** Requests to clean up presets by removing parameters that are no longer bound.
	 *  Called from the UI thread.
	 */
	void presetCleanUpRequest() {
		taskProcessorDsp.enqueue([=]() { presetCleanUp(); });
	}

	/** Cleans up presets by removing parameters that are no longer bound.
	 *  Called from the engine thread.
	 */
	void presetCleanUp() {
		for (size_t i = 0; i < sourceHandles.size(); ) {
			ParamQuantity* pq = getParamQuantity(sourceHandles[i]);
			if (!pq) {
				for (int j = 0; j < presetTotal; j++) {
					SLOT* slot = getSlot(j);
					if (slot->isUsed()) {
						if (slot->getPreset()->size() > i) {
							slot->getPreset()->erase(slot->getPreset()->begin() + i);
						}
					}
					else {
						presetClear(j);
					}
				}
				sourceHandles.erase(sourceHandles.begin() + i);
			}
			else {
				i++;
			}
		}
		for (int j = 0; j < presetTotal; j++) {
			SLOT* slot = getSlot(j);
			if (!slot->isUsed()) continue;
			assert(sourceHandles.size() == slot->getPreset()->size());
		}

		// Publish new sourceHandles snapshot for the UI thread
		sourceHandlesPtr.store(sourceHandles);
	}

	/** Cleans up an expander's presets if they are invalid.
	 *  Called from the engine thread.
	 */
	void expanderCleanUp(TransitBase<NUM_PRESETS>* t) {
		bool invalid = false;
		// ctrlUniqueId == -2 for presets before uniqueId was added
		if (t->ctrlUniqueId == -2) {
			for (int i = 0; i < NUM_PRESETS; i++) {
				SLOT* slot = &t->slot[i];
				if (slot->isUsed()) {
					if (slot->getPreset()->size() != sourceHandles.size()) {
						invalid = true;
						break;
					}
				}
			}
		}
		if (t->ctrlUniqueId != -2 || invalid) {
			Module::ResetEvent re;
			t->onReset(re);
		}
		t->ctrlUniqueId = BASE::ctrlUniqueId;
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
		if (isXyPadActive()) outMode = OUTMODE::OFF;
		if (slotCvMode == SLOTCVMODE::PHASE) outMode = OUTMODE::PHASE;
		else if (outMode == OUTMODE::PHASE) outMode = OUTMODE::ENV;
	}

	void setOutMode(OUTMODE mode) {
		outMode = mode;
		if (isXyPadActive()) outMode = OUTMODE::OFF;
		if (slotCvMode == SLOTCVMODE::PHASE) outMode = OUTMODE::PHASE;
		else if (outMode == OUTMODE::PHASE) outMode = OUTMODE::ENV;
	}

	int sendSlotCmd(SLOT_CMD cmd, int i) override {
		switch (cmd) {
			case SLOT_CMD::LOAD:
				presetLoadRequest(i); 
				return -1;
			case SLOT_CMD::CLEAR:
				presetClearRequest(i);
				return -1;
			case SLOT_CMD::RANDOMIZE:
				presetRandomizeRequest(i);
				return -1;
			case SLOT_CMD::COPY:
				presetCopy = getSlot(i)->isUsed() ? i : -1;
				return -1;
			case SLOT_CMD::PASTE_PREVIEW:
				return presetCopy;
			case SLOT_CMD::PASTE:
				presetCopyPasteRequest(presetCopy, i);
				return -1;
			case SLOT_CMD::SAVE:
				presetSaveRequest(i);
				return -1;
			case SLOT_CMD::SHIFT_BACK:
				presetShiftBackRequest(i);
				return -1;
			case SLOT_CMD::SHIFT_FRONT:
				presetShiftFrontRequest(i);
				return -1;
			case SLOT_CMD::SET_FIRST:
				presetSetFirst(i);
				return -1;
			case SLOT_CMD::SET_LAST:
				presetSetLast(i + 1);
				return -1;
			case SLOT_CMD::INDEX:
				return i;
			default:
				return -1;
		}
	}

	json_t* dataToJson() override {
		json_t* rootJ = BASE::dataToJson();
		json_object_set_new(rootJ, "mappingIndicatorHidden", json_boolean(mappingIndicatorHidden));
		json_object_set_new(rootJ, "presetProcessDivision", json_integer(getProcessDivision()));

		json_object_set_new(rootJ, "slotCvMode", json_integer((int)slotCvMode));
		json_object_set_new(rootJ, "outMode", json_integer((int)outMode));
		json_object_set_new(rootJ, "preset", json_integer(preset));
		json_object_set_new(rootJ, "presetFirst", json_integer(presetFirst));
		json_object_set_new(rootJ, "presetCount", json_integer(presetLast));
		json_object_set_new(rootJ, "presetCountLongPress", json_boolean(presetCountLongPress));
		json_object_set_new(rootJ, "clampFadeCv", json_boolean(clampFadeCv));
		json_object_set_new(rootJ, "parameterChangesDirect", json_boolean(parameterChangesDirect));

		const auto& snap = sourceHandlesPtr.peek();
		json_t* sourceMapsJ = json_array();
		for (size_t i = 0; i < snap.size(); i++) {
			json_t* sourceMapJ = json_object();
			json_object_set_new(sourceMapJ, "moduleId", json_integer(snap.at(i)->moduleId));
			json_object_set_new(sourceMapJ, "paramId", json_integer(snap.at(i)->paramId));
			json_array_append_new(sourceMapsJ, sourceMapJ);
		}
		json_object_set_new(rootJ, "sourceMaps", sourceMapsJ);

		return rootJ;
	}

	void dataFromJson(json_t* rootJ) override {
		BASE::panelTheme = json_integer_value(json_object_get(rootJ, "panelTheme"));
		json_t* mappingIndicatorHiddenJ = json_object_get(rootJ, "mappingIndicatorHidden");
		if (mappingIndicatorHiddenJ) mappingIndicatorHidden = json_boolean_value(mappingIndicatorHiddenJ);
		json_t* presetProcessDivisionJ = json_object_get(rootJ, "presetProcessDivision");
		if (presetProcessDivisionJ) setProcessDivision(json_integer_value(presetProcessDivisionJ));

		json_t* slotCvModeJ = json_object_get(rootJ, "slotCvMode");
		if (slotCvModeJ) slotCvMode = (SLOTCVMODE)json_integer_value(slotCvModeJ);
		json_t* outModeJ = json_object_get(rootJ, "outMode");
		if (outModeJ) outMode = (OUTMODE)json_integer_value(outModeJ);
		json_t* presetJ = json_object_get(rootJ, "preset");
		if (presetJ) preset = json_integer_value(presetJ);
		json_t* presetFirstJ = json_object_get(rootJ, "presetFirst");
		if (presetFirstJ) presetFirst = json_integer_value(presetFirstJ);
		json_t* presetLastJ = json_object_get(rootJ, "presetCount");
		if (presetLastJ) presetLast = json_integer_value(presetLastJ);
		json_t* presetCountLongPressJ = json_object_get(rootJ, "presetCountLongPress");
		if (presetCountLongPressJ) presetCountLongPress = json_boolean_value(presetCountLongPressJ);
		json_t* clampFadeCvJ = json_object_get(rootJ, "clampFadeCv");
		if (clampFadeCvJ) clampFadeCv = json_boolean_value(clampFadeCvJ);
		json_t* parameterChangesDirectJ = json_object_get(rootJ, "parameterChangesDirect");
		if (parameterChangesDirectJ) parameterChangesDirect = json_boolean_value(parameterChangesDirectJ);

		if (preset >= presetLast || preset < presetFirst) {
			preset = -1;
		}

		std::list<std::tuple<int64_t, int>> handleToDo;

		json_t* sourceMapsJ = json_object_get(rootJ, "sourceMaps");
		if (sourceMapsJ) {
			json_t* sourceMapJ;
			size_t sourceMapIndex;
			json_array_foreach(sourceMapsJ, sourceMapIndex, sourceMapJ) {
				json_t* moduleIdJ = json_object_get(sourceMapJ, "moduleId");
				int64_t moduleId = json_integer_value(moduleIdJ);
				json_t* paramIdJ = json_object_get(sourceMapJ, "paramId");
				int paramId = json_integer_value(paramIdJ);
				moduleId = BASE::idFix(moduleId);
		
				handleToDo.push_back(std::make_tuple(moduleId, paramId));
			}
		}

		BASE::idFixClearMap();

		// Enqueue on the UI-thread for clearing ParamHandles
		taskProcessorUi.enqueue([=]() {
			bindClearParameterRequest();
		});
		// Creating new ParamHandles will cause a deadlock as the engine's mutex could already been locked
		taskProcessorUi.enqueue([=]() {
			for (auto& s : handleToDo) {
				int64_t moduleId = std::get<0>(s);
				int paramId = std::get<1>(s);
				bindAddParameterRequest(moduleId, paramId, true);
			}
		});

		BASE::dataFromJson(rootJ);
		if (BASE::ctrlUniqueId == -2) BASE::ctrlUniqueId = Module::id;
		Module::params[PARAM_CTRLMODE].setValue(0.f);
	}
};

template <int NUM_PRESETS>
struct TransitSelectionWidget : Widget {
	TransitModule<NUM_PRESETS>* module;

	bool learn = false;

	bool selecting = false;
	math::Vec selectionStart;
	math::Vec selectionEnd;
	math::Vec mousePos;

	void enableLearn() {
		learn = !learn;
		cursor::setLearnCursor(learn);
	}

	void onHover(const HoverEvent& e) override {
		mousePos = e.pos;
		Widget::onHover(e);
	}

	void onButton(const ButtonEvent& e) override {
		if (learn && e.button == GLFW_MOUSE_BUTTON_LEFT) {
			e.consume(this);
		}
		Widget::onButton(e);
	}

	void onDragStart(const DragStartEvent& e) override {
		if (learn && e.button == GLFW_MOUSE_BUTTON_LEFT) {
			selecting = true;
			selectionStart = mousePos;
			selectionEnd = mousePos;
			e.consume(this);
		}
		Widget::onDragStart(e);
	}

	void onDragEnd(const DragEndEvent& e) override {
		if (selecting && e.button == GLFW_MOUSE_BUTTON_LEFT) {
			mapParamsFromRect();
			selecting = false;
			learn = false;
			cursor::resetCursor();
			e.consume(this);
		}
		Widget::onDragEnd(e);
	}

	void onDragHover(const DragHoverEvent& e) override {
		mousePos = e.pos;
		if (selecting) {
			selectionEnd = mousePos;
		}
		Widget::onDragHover(e);
	}

	void mapParamsFromRect() {
		math::Rect selectionBox = math::Rect::fromCorners(selectionStart, selectionEnd);
		std::list<ModuleWidget*> selected;
		for (ModuleWidget* mw : APP->scene->rack->getModules()) {
			if (selectionBox.intersects(mw->box)) {
				selected.push_back(mw);
			}
		}
		for (ModuleWidget* mw : selected) {
			std::list<ParamWidget*> selectedParams;
			math::Rect selectionBox1(selectionBox.pos.minus(mw->box.pos), selectionBox.size);
			getAllDescendentsByTypeAndBox<ParamWidget*>(mw, selectionBox1, selectedParams);

			selectedParams.reverse();
			for (ParamWidget* pw : selectedParams) {
				if (!pw->module) continue;
				module->bindAddParameterRequest(pw->module->getId(), pw->paramId);
			}
		}
	}

	void draw(const DrawArgs& args) override {
		// Draw selection rectangle
		if (selecting) {
			nvgBeginPath(args.vg);
			math::Rect selectionBox = math::Rect::fromCorners(selectionStart, selectionEnd);
			nvgRect(args.vg, RECT_ARGS(selectionBox));
			nvgFillColor(args.vg, nvgRGBAf(0, 0, 1, 0.25));
			nvgFill(args.vg);
			nvgStrokeWidth(args.vg, 2.0);
			nvgStrokeColor(args.vg, nvgRGBAf(0, 0, 1, 0.5));
			nvgStroke(args.vg);
		}
	}

	template <class T>
	void getAllDescendentsByTypeAndBox(Widget* w, math::Rect selectionBox, std::list<T>& selected) {
		for (auto it = w->children.rbegin(); it != w->children.rend(); it++) {
			Widget* child = *it;
			// Filter child by visibility and position
			if (!child->visible)
				continue;
			if (!selectionBox.intersects(child->box))
				continue;

			math::Rect selectionBox1(selectionBox.pos.minus(child->box.pos), selectionBox.size);
			getAllDescendentsByTypeAndBox<T>(child, selectionBox1, selected);
			T t = dynamic_cast<T>(child);
			if (t) {
				selected.push_back(t);
			}
		}
	}
}; // struct TransitSelectionWidget

template <int NUM_PRESETS>
struct TransitWidget : ThemedModuleWidget<TransitModule<NUM_PRESETS>> {
	typedef TransitWidget<NUM_PRESETS> WIDGET;
	typedef ThemedModuleWidget<TransitModule<NUM_PRESETS>> BASE;
	typedef TransitModule<NUM_PRESETS> MODULE;
	
	int learn = 0;
	TransitSelectionWidget<NUM_PRESETS>* selectionWidget = nullptr;

	TransitWidget(MODULE* module)
		: ThemedModuleWidget<MODULE>(module, "Transit") {
		BASE::setModule(module);
		BASE::disableDuplicateAction = true;

		BASE::addChild(createWidget<StoermelderBlackScrew>(Vec(RACK_GRID_WIDTH, 0)));
		BASE::addChild(createWidget<StoermelderBlackScrew>(Vec(BASE::box.size.x - 2 * RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));

		BASE::addChild(createLightCentered<TinyLight<YellowLight>>(Vec(10.4f, 46.2f), module, MODULE::LIGHT_CV));
		BASE::addInput(createInputCentered<StoermelderPort>(Vec(21.7f, 58.9f), module, MODULE::INPUT_CV));
		BASE::addInput(createInputCentered<StoermelderPort>(Vec(21.7f, 94.2f), module, MODULE::INPUT_RESET));

		BASE::addParam(createParamCentered<LEDSliderWhite>(Vec(21.7f, 166.7f), module, MODULE::PARAM_FADE));
		BASE::addInput(createInputCentered<StoermelderPort>(Vec(21.7f, 221.4f), module, MODULE::INPUT_FADE));

		BASE::addParam(createParamCentered<StoermelderTrimpot>(Vec(21.7f, 255.8f), module, MODULE::PARAM_SHAPE));
		BASE::addOutput(createOutputCentered<StoermelderPort>(Vec(21.7f, 327.6f), module, MODULE::OUTPUT));

		BASE::addParam(createParamCentered<CKSSThreeH>(Vec(21.7f, 292.0f), module, MODULE::PARAM_CTRLMODE));

		BASE::addChild(createLightCentered<TinyLight<WhiteLight>>(Vec(10.4f, 353.5f), module, MODULE::LIGHT_LEARN));

		for (size_t i = 0; i < NUM_PRESETS; i++) {
			float o = i * (259.0f / (NUM_PRESETS - 1));
			TransitLedButton<NUM_PRESETS>* ledButton = createParamCentered<TransitLedButton<NUM_PRESETS>>(Vec(60.0f, 46.4f + o), module, MODULE::PARAM_PRESET + i);
			ledButton->module = module;
			ledButton->id = i;
			BASE::addParam(ledButton);
			BASE::addChild(createLightCentered<MediumSimpleLight<RedGreenBlueLight>>(Vec(60.0f, 46.4f + o), module, MODULE::LIGHT_PRESET + i * 3));
		}

		if (module) {
			selectionWidget = new TransitSelectionWidget<NUM_PRESETS>;
			selectionWidget->module = module;
			APP->scene->rack->addChild(selectionWidget);
		}
	}

	~TransitWidget() {
		if (learn != 0 && APP->window) {
			cursor::resetCursor();
		}

		if (selectionWidget) {
			APP->scene->rack->removeChild(selectionWidget);
			delete selectionWidget;
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
			module->bindAddModuleRequest(m);
		}

		if (learn == 2 || learn == 3) {
			// Check if a ParamWidget was touched
			ParamWidget* pw = APP->scene->rack->getTouchedParam();
			if (pw) {
				ParamQuantity* pq = pw->getParamQuantity();
				if (pq && pq->module && pq->module != module) {
					APP->scene->rack->setTouchedParam(NULL);
					int64_t moduleId = pq->module->id;
					int paramId = pq->paramId;
					module->bindAddParameterRequest(moduleId, paramId);
					if (learn == 2) { 
						disableLearn();
					}
					return;
				}
			}
			disableLearn();
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
		if (BASE::module) BASE::module->taskProcessorUi.process();
	}

	void enableLearn(int mode) {
		learn = learn != mode ? mode : 0;
		APP->scene->rack->setTouchedParam(NULL);
		APP->event->setSelectedWidget(this);
		cursor::setLearnCursor(learn != 0);
	}

	void disableLearn() {
		learn = 0;
		cursor::resetCursor();
	}

	void appendContextMenu(Menu* menu) override {
		ThemedModuleWidget<MODULE>::appendContextMenu(menu);
		int sampleRate = int(APP->engine->getSampleRate());
		MODULE* module = dynamic_cast<MODULE*>(this->module);

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

		menu->addChild(new MenuSeparator());
		menu->addChild(createBoolPtrMenuItem("Hide mapping indicators", "", &module->mappingIndicatorHidden));
		menu->addChild(StoermelderPackOne::Rack::createMapSubmenuItem<int>("Precision", {
				{ 1, string::f("Audio rate (%i Hz)", sampleRate / 1) },
				{ 8, string::f("Lower CPU (%i Hz)", sampleRate / 8) },
				{ 64, string::f("Lowest CPU (%i Hz)", sampleRate / 64) },
				{ 256, string::f("Even lower CPU (%i Hz)", sampleRate / 256) },
				{ 1024, string::f("Crazy low CPU (%i Hz)", sampleRate / 1024) }
			},
			[=]() {
				return module->getProcessDivision();
			},
			[=](int division) {
				module->setProcessDivision(division);
			}
		));
		if (settings::isPlugin) {
			menu->addChild(createBoolPtrMenuItem("Report parameter changes", "", &module->parameterChangesDirect));			
		}

		menu->addChild(new MenuSeparator());
		menu->addChild(createSubmenuItem("Snapshots", string::f("%i-%i", module->presetFirst + 1, module->presetLast),
			[=](Menu* menu) {
				menu->addChild(StoermelderPackOne::Rack::createSteppedSlider<int>(
					[=]() { return module->presetFirst + 1; },
					[=](int v) { module->presetSetFirst(v - 1); },
					1, module->presetTotal, 1, "First"
				));
				menu->addChild(StoermelderPackOne::Rack::createSteppedSlider<int>(
					[=]() { return module->presetLast; },
					[=](int v) { module->presetSetLast(v); },
					1, module->presetTotal, module->presetTotal, "Last"
				));
				menu->addChild(createBoolPtrMenuItem("Set by long-press", "", &module->presetCountLongPress));
			}
		));

		menu->addChild(createSubmenuItem("Port CV mode", "", [=](Menu* menu) { 
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

			bool xyMode = module->isXyPadActive();
			menu->addChild(construct<SlotCvModeItem>(&MenuItem::text, "Trigger forward", &SlotCvModeItem::module, module, &SlotCvModeItem::slotCvMode, SLOTCVMODE::TRIG_FWD, &SlotCvModeItem::disabled, xyMode));
			menu->addChild(construct<SlotCvModeItem>(&MenuItem::text, "Trigger reverse", &SlotCvModeItem::module, module, &SlotCvModeItem::slotCvMode, SLOTCVMODE::TRIG_REV, &SlotCvModeItem::disabled, xyMode));
			menu->addChild(construct<SlotCvModeItem>(&MenuItem::text, "Trigger pingpong", &SlotCvModeItem::module, module, &SlotCvModeItem::slotCvMode, SLOTCVMODE::TRIG_PINGPONG, &SlotCvModeItem::disabled, xyMode));
			menu->addChild(construct<SlotCvModeItem>(&MenuItem::text, "Trigger alternating", &SlotCvModeItem::module, module, &SlotCvModeItem::slotCvMode, SLOTCVMODE::TRIG_ALT, &SlotCvModeItem::disabled, xyMode));
			menu->addChild(construct<SlotCvModeItem>(&MenuItem::text, "Trigger random", &SlotCvModeItem::module, module, &SlotCvModeItem::slotCvMode, SLOTCVMODE::TRIG_RANDOM, &SlotCvModeItem::disabled, xyMode));
			menu->addChild(construct<SlotCvModeItem>(&MenuItem::text, "Trigger pseudo-random", &SlotCvModeItem::module, module, &SlotCvModeItem::slotCvMode, SLOTCVMODE::TRIG_RANDOM_WO_REPEAT, &SlotCvModeItem::disabled, xyMode));
			menu->addChild(construct<SlotCvModeItem>(&MenuItem::text, "Trigger random walk", &SlotCvModeItem::module, module, &SlotCvModeItem::slotCvMode, SLOTCVMODE::TRIG_RANDOM_WALK, &SlotCvModeItem::disabled, xyMode));
			menu->addChild(construct<SlotCvModeItem>(&MenuItem::text, "Trigger shuffle", &SlotCvModeItem::module, module, &SlotCvModeItem::slotCvMode, SLOTCVMODE::TRIG_SHUFFLE, &SlotCvModeItem::disabled, xyMode));
			menu->addChild(construct<SlotCvModeItem>(&MenuItem::text, "0..10V", &SlotCvModeItem::module, module, &SlotCvModeItem::slotCvMode, SLOTCVMODE::VOLT, &SlotCvModeItem::disabled, xyMode));
			menu->addChild(construct<SlotCvModeItem>(&MenuItem::text, "C4", &SlotCvModeItem::module, module, &SlotCvModeItem::slotCvMode, SLOTCVMODE::C4, &SlotCvModeItem::disabled, xyMode));
			menu->addChild(construct<SlotCvModeItem>(&MenuItem::text, "Arm", &SlotCvModeItem::module, module, &SlotCvModeItem::slotCvMode, SLOTCVMODE::ARM, &SlotCvModeItem::disabled, xyMode));
			menu->addChild(new MenuSeparator);
			menu->addChild(construct<SlotCvModeItem>(&MenuItem::text, "Phase", &SlotCvModeItem::module, module, &SlotCvModeItem::slotCvMode, SLOTCVMODE::PHASE, &SlotCvModeItem::disabled, xyMode));
			menu->addChild(new MenuSeparator);
			menu->addChild(construct<SlotCvModeItem>(&MenuItem::text, "Off", &SlotCvModeItem::rightTextEx, RACK_MOD_SHIFT_NAME "+Q", &SlotCvModeItem::module, module, &SlotCvModeItem::slotCvMode, SLOTCVMODE::OFF));
		}));

		menu->addChild(createSubmenuItem("Port OUT mode", "", [=](Menu* menu) {
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

			bool phaseMode = module->slotCvMode == SLOTCVMODE::PHASE;
			bool xyMode = module->isXyPadActive();

			menu->addChild(construct<OutModeItem>(&MenuItem::text, "Envelope", &OutModeItem::module, module, &OutModeItem::outMode, OUTMODE::ENV, &OutModeItem::disabled, phaseMode || xyMode));
			menu->addChild(construct<OutModeItem>(&MenuItem::text, "Gate", &OutModeItem::module, module, &OutModeItem::outMode, OUTMODE::GATE, &OutModeItem::disabled, phaseMode || xyMode));
			menu->addChild(construct<OutModeItem>(&MenuItem::text, "Trigger snapshot change", &OutModeItem::module, module, &OutModeItem::outMode, OUTMODE::TRIG_SNAPSHOT, &OutModeItem::disabled, phaseMode || xyMode));
			menu->addChild(construct<OutModeItem>(&MenuItem::text, "Trigger fade start", &OutModeItem::module, module, &OutModeItem::outMode, OUTMODE::TRIG_SOC, &OutModeItem::disabled, phaseMode || xyMode));
			menu->addChild(construct<OutModeItem>(&MenuItem::text, "Trigger fade end", &OutModeItem::module, module, &OutModeItem::outMode, OUTMODE::TRIG_EOC, &OutModeItem::disabled, phaseMode || xyMode));
			menu->addChild(new MenuSeparator);
			menu->addChild(construct<OutModeItem>(&MenuItem::text, "Polyphonic", &OutModeItem::module, module, &OutModeItem::outMode, OUTMODE::POLY, &OutModeItem::disabled, phaseMode || xyMode));
			menu->addChild(new MenuSeparator);
			menu->addChild(construct<OutModeItem>(&MenuItem::text, "Phase", &OutModeItem::module, module, &OutModeItem::outMode, OUTMODE::PHASE, &OutModeItem::disabled, !phaseMode || xyMode));
			menu->addChild(new MenuSeparator);
			menu->addChild(construct<OutModeItem>(&MenuItem::text, "Off", &OutModeItem::module, module, &OutModeItem::outMode, OUTMODE::OFF));
			menu->addChild(new MenuSeparator);
			menu->addChild(construct<OutModeItem>(&MenuItem::text, "Tipsy", &OutModeItem::module, module, &OutModeItem::outMode, OUTMODE::TIPSY, &OutModeItem::disabled, phaseMode));
		}));
		menu->addChild(createBoolPtrMenuItem("Clamp Fade CV input", "", &module->clampFadeCv));

		menu->addChild(new MenuSeparator());
		menu->addChild(createMenuItem("Bind module (left)", "", [=]() { disableLearn(); module->bindAddModuleExpanderRequest(); }));
		menu->addChild(createMenuItem("Bind module (select)", "", [=]() { enableLearn(1); }));
		menu->addChild(construct<BindParameterItem>(&MenuItem::text, "Bind single parameter", &BindParameterItem::rightText, RACK_MOD_SHIFT_NAME "+B", &BindParameterItem::widget, this, &BindParameterItem::mode, 2));
		menu->addChild(construct<BindParameterItem>(&MenuItem::text, "Bind multiple parameters", &BindParameterItem::rightText, RACK_MOD_SHIFT_NAME "+A", &BindParameterItem::widget, this, &BindParameterItem::mode, 3));
		menu->addChild(createMenuItem("Bind parameters by selection", "", [=]() { selectionWidget->enableLearn(); }));

		const auto& snap = module->sourceHandlesPtr.peek();
		if (snap.size() > 0) {
			menu->addChild(new MenuSeparator());

			std::set<int64_t> moduleIds;
			for (size_t i = 0; i < snap.size(); i++) {
				ParamHandle* handle = snap.at(i);
				if (moduleIds.find(handle->moduleId) == moduleIds.end()) {
					moduleIds.insert(handle->moduleId);
				}
			}
			menu->addChild(createSubmenuItem(string::f("Bound modules: %lli", moduleIds.size()), "", [=](Menu* menu) {
				for (int64_t moduleId : moduleIds) {
					ModuleWidget* moduleWidget = APP->scene->rack->getModule(moduleId);
					if (!moduleWidget) continue;
					std::string text = string::f("Unbind \"%s %s\"", moduleWidget->model->plugin->name.c_str(), moduleWidget->model->name.c_str());
					menu->addChild(createMenuItem(text, "", [=]() {
						for (size_t i = 0; i < snap.size(); i++) {
							ParamHandle* handle = snap.at(i);
							if (handle->moduleId != moduleId) continue;
							APP->engine->updateParamHandle(handle, -1, 0, true);
						}
					}));
				}
			}));

			menu->addChild(createSubmenuItem(string::f("Bound parameters: %lli", snap.size()), "", [=](Menu* menu) {
				for (size_t i = 0; i < snap.size(); i++) {
					ParamHandleEx* handle = snap[i];
					ModuleWidget* moduleWidget = APP->scene->rack->getModule(handle->moduleId);
					if (!moduleWidget) continue;

					ParamWidget* paramWidget = moduleWidget->getParam(handle->paramId);
					if (paramWidget) {
						std::string text = string::f("%s %s", moduleWidget->model->name.c_str(), paramWidget->getParamQuantity()->getLabel().c_str());
						menu->addChild(createSubmenuItem(text, "", [=](Menu* menu) {
							menu->addChild(createMenuItem("Locate and indicate", "", [=]() { handle->indicate(APP->scene->rack->getModule(handle->moduleId)); }));
							menu->addChild(createMenuItem("Unbind", "", [=]() { APP->engine->updateParamHandle(handle, -1, 0, true); }));
						}));
					}
					else {
						std::string text = string::f("%s <hidden parameter>", moduleWidget->model->name.c_str());
						menu->addChild(createSubmenuItem(text, "", [=](Menu* menu) {
							menu->addChild(createMenuItem("Unbind", "", [=]() { APP->engine->updateParamHandle(handle, -1, 0, true); }));
						}));
					}
				}
			}));

			menu->addChild(createMenuItem("Clean invalid parameters up", "", [=]() { module->presetCleanUpRequest(); }));
		}
	}
};

} // namespace Transit
} // namespace StoermelderPackOne

Model* modelTransit = createModel<StoermelderPackOne::Transit::TransitModule<12>, StoermelderPackOne::Transit::TransitWidget<12>>("Transit");