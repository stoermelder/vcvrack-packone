#include "../../plugin.hpp"
#include "../../utils/digital.hpp"
#include "../../utils/TaskWorker.hpp"
#include "../../components/MenuColorLabel.hpp"
#include "../../components/MenuColorField.hpp"
#include "../../components/MenuColorPicker.hpp"
#include "../../ui/ModuleSelectProcessor.hpp"
#include "../../ui/ViewportHelper.hpp"
#include "EightFace.hpp"
#include "EightFaceMk2Base.hpp"
#include "../../utils/string.hpp"
#include <random>
#include <osdialog.h>

namespace StoermelderPackOne {
namespace EightFaceMk2 {

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
	ARM = 3
};

enum class GUISAFEMODE {
	WORKER,
	GUI,
	GUI_WITH_LOCK
};


template <int NUM_PRESETS>
struct EightFaceMk2Module : EightFaceMk2Base<NUM_PRESETS>, ModuleChangeListener {
	typedef EightFaceMk2Base<NUM_PRESETS> BASE;

	enum ParamIds {
		ENUMS(PARAM_PRESET, NUM_PRESETS),
		PARAM_RW,
		NUM_PARAMS
	};
	enum InputIds {
		INPUT_CV,
		INPUT_RESET,
		NUM_INPUTS
	};
	enum OutputIds {
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
	/** [Stored to JSON] */
	bool presetCountLongPress = true;

	/** Total number of snapshots including expanders */
	int presetTotal;
	int presetPrev = -1;
	int presetNext;
	int presetCopy = -1;

	std::set<int64_t> expandersConnected;

	/** [Stored to JSON] mode for SEQ CV input */
	SLOTCVMODE slotCvMode = SLOTCVMODE::TRIG_FWD;
	SLOTCVMODE slotCvModeBak = SLOTCVMODE::OFF;
	int slotCvModeDir = 1;
	int slotCvModeAlt = 0;
	std::vector<int> slotCvModeShuffle;

	std::default_random_engine randGen{(uint16_t)std::chrono::system_clock::now().time_since_epoch().count()};
	std::uniform_int_distribution<int> randDist;
	bool inChange = false;

	dsp::SchmittTrigger slotTrigger;
	dsp::SchmittTrigger slotC4Trigger;
	dsp::SchmittTrigger resetTrigger;
	dsp::Timer resetTimer;

	ClockDividerEx buttonDivider;
	ClockDividerEx boundModulesDivider;
	ClockDividerEx lightDivider;
	dsp::Timer lightTimer;
	bool lightBlink = false;

	EightFaceMk2Base<NUM_PRESETS>* N[MAX_EXPANDERS + 1];


	struct BoundModule {
		int64_t moduleId;
		std::string pluginSlug;
		std::string modelSlug;
		std::string moduleName;
		ModuleWidget* getModuleWidget() { return APP->scene->rack->getModule(moduleId); }
		bool needsGuiThread = false;
	};

	/** [Stored to JSON] */
	std::vector<BoundModule*> boundModules;
	/** [Stored to JSON] */
	EightFace::AUTOLOAD autoload = EightFace::AUTOLOAD::OFF;

	/** [Stored to JSON] Box draw mode: 0=never, 1=always, 2=when selected */
	int boxDraw = 2;
	/** [Stored to JSON] */
	NVGcolor boxColor;
	/** [Stored to JSON] Opacity of the module outline (0.0 - 1.0), default 0.5 (50%) */
	float boxOpacity = 0.5f;

	dsp::RingBuffer<std::tuple<ModuleWidget*, json_t*>, 32> workerGuiQueue;
	TaskWorker taskWorker;
	/** [Stored to JSON] */
	GUISAFEMODE guiSafeMode = GUISAFEMODE::GUI_WITH_LOCK;

	EightFaceMk2Module() {
		BASE::panelTheme = pluginSettings.panelThemeDefault;
		registerModuleListener("8FaceMk2", this);
		Module::config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);
		Module::configSwitch(PARAM_RW, 0.f, 2.f, 0.f, "Operating mode", {"Read", "Auto", "Write"});
		Module::paramQuantities[PARAM_RW]->description = "Read: load a slot manually.\nAuto: auto-save on snapshot-change.\nWrite: snapshot the currently mapped parameters into a slot.";
		Module::configInput(INPUT_CV, "Slot-selection");
		Module::inputInfos[INPUT_CV]->description = "Trigger/gate that selects the next slot, depending on the slot-CV mode selected on the context menu.\nChannel 2 can retrigger the current slot in C4 mode.";
		Module::configInput(INPUT_RESET, "Sequencer-mode reset");
		Module::inputInfos[INPUT_RESET]->description = "Resets the slot sequence to the first slot (depending on the selected CV mode).";

		for (int i = 0; i < NUM_PRESETS; i++) {
			EightFaceMk2ParamQuantity<NUM_PRESETS>* pq = Module::configParam<EightFaceMk2ParamQuantity<NUM_PRESETS>>(PARAM_PRESET + i, 0, 1, 0);
			pq->module = this;
			pq->id = i;
			BASE::presetButton[i].param = &Module::params[PARAM_PRESET + i];

			BASE::slot[i].param = &Module::params[PARAM_PRESET + i];
			BASE::slot[i].lights = &Module::lights[LIGHT_PRESET + i * 3];
			BASE::slot[i].presetSlotUsed = &BASE::presetSlotUsed[i];
			BASE::slot[i].preset = &BASE::preset[i];
			BASE::slot[i].presetButton = &BASE::presetButton[i];
		}

		buttonDivider.setDivision(128);
		boundModulesDivider.setDivision(APP->engine->getSampleRate());
		lightDivider.setDivision(512);

		Module::ResetEvent re;
		onReset(re);
	}

	~EightFaceMk2Module() {
		unregisterModuleListener("8FaceMk2", this);
		for (int i = 0; i < NUM_PRESETS; i++) {
			if (BASE::presetSlotUsed[i]) {
				for (json_t* vJ : BASE::preset[i]) {
					json_decref(vJ);
				}
			}
		}
		for (BoundModule* b : boundModules) {
			delete b;
		}
	}

	void onExpanderChange(const Module::ExpanderChangeEvent& e) override {
		notifyModuleListeners("8FaceMk2");
	}

	void onReset(const Module::ResetEvent& e) override {
		inChange = true;
		moduleChangedFlag = true;
		for (int i = 0; i < NUM_PRESETS; i++) {
			if (BASE::presetSlotUsed[i]) {
				for (json_t* vJ : BASE::preset[i]) {
					json_decref(vJ);
				}
				BASE::preset[i].clear();
			}
			BASE::presetSlotUsed[i] = false;
			BASE::textLabel[i] = "";
		}
		for (BoundModule* b : boundModules) {
			delete b;
		}
		boundModules.clear();
		inChange = false;
		guiSafeMode = GUISAFEMODE::GUI_WITH_LOCK;

		BASE::ctrlUniqueId = (int64_t)(rack::random::uniform() * (float)INT64_MAX);
		preset = -1;
		presetCount = NUM_PRESETS;
		presetNext = -1;

		autoload = EightFace::AUTOLOAD::OFF;
		boxDraw = 2;
		boxColor = color::BLUE;
		boxOpacity = 0.5f;

		Module::onReset(e);
	}

	void onSampleRateChange(const Module::SampleRateChangeEvent& e) override {
		boundModulesDivider.setDivision(e.sampleRate);
	}

	EightFaceMk2Slot* faceSlot(int i) override {
		return &BASE::slot[i];
	}

	inline EightFaceMk2Slot* expSlot(int index) {
		if (index >= presetTotal) return NULL;
		int n = index / NUM_PRESETS;
		return N[n]->faceSlot(index % NUM_PRESETS);
	}

	inline std::string* expSlotLabel(int index) {
		if (index >= presetTotal) return NULL;
		int n = index / NUM_PRESETS;
		return &N[n]->textLabel[index % NUM_PRESETS];
	}

	void process(const Module::ProcessArgs& args) override {
		if (inChange) return;

		CTRLMODE ctrlMode = (CTRLMODE)Module::params[PARAM_RW].getValue();

		if (moduleChangedFlag || ctrlMode != BASE::ctrlMode) {
			expandersConnected.clear();
			presetTotal = NUM_PRESETS;
			Module* m = this;
			EightFaceMk2Base<NUM_PRESETS>* t = this;
			t->ctrlMode = ctrlMode;
			int c = 0;
			while (true) {
				N[c] = t;
				c++;
				if (c == MAX_EXPANDERS + 1) break;

				Module* exp = m->rightExpander.module;
				if (!exp) break;
				if (exp->model != modelEightFaceMk2Ex) break;
				m = exp;
				t = dynamic_cast<EightFaceMk2Base<NUM_PRESETS>*>(exp);
				if (t->ctrlUniqueId != BASE::ctrlUniqueId) expanderCleanUp(t);
				expandersConnected.insert(m->getId());
				t->panelTheme = BASE::panelTheme;
				t->ctrlModuleId = Module::id;
				t->ctrlOffset = c;
				t->ctrlMode = BASE::ctrlMode;
				presetTotal += NUM_PRESETS;
			}
			moduleChangedFlag = false;
		}
		int presetCount = std::min(this->presetCount, presetTotal);

		// Read & Auto modes
		if (BASE::ctrlMode == CTRLMODE::READ || BASE::ctrlMode == CTRLMODE::AUTO) {
			// RESET input
			if (resetTrigger.process(Module::inputs[INPUT_RESET].getVoltage())) {
				resetTimer.reset();
				switch (slotCvMode) {
					case SLOTCVMODE::TRIG_FWD:
					case SLOTCVMODE::TRIG_RANDOM:
					case SLOTCVMODE::TRIG_RANDOM_WALK:
					case SLOTCVMODE::TRIG_RANDOM_WO_REPEAT:
						presetLoad(0);
						break;
					case SLOTCVMODE::TRIG_REV:
						presetLoad(presetCount - 1);
						break;
					case SLOTCVMODE::TRIG_PINGPONG:
						slotCvModeDir = 1;
						presetLoad(0);
						break;
					case SLOTCVMODE::TRIG_ALT:
						slotCvModeDir = 1;
						slotCvModeAlt = 0;
						presetLoad(0);
						break;
					case SLOTCVMODE::TRIG_SHUFFLE:
						slotCvModeShuffle.clear();
						break;
					default:
						break;
				}
			}
			else {
				resetTimer.process(args.sampleTime);
			}

			// CV input
			if (Module::inputs[INPUT_CV].isConnected()) {
				switch (slotCvMode) {
					case SLOTCVMODE::VOLT:
						presetLoad(std::floor(rescale(clamp(Module::inputs[INPUT_CV].getVoltage(), 0.f, 10.f - 1e-6f), 0.f, 10.f, 0, presetCount)));
						break;
					case SLOTCVMODE::C4:
						presetLoad(std::round(clamp(Module::inputs[INPUT_CV].getVoltage() * 12.f, 0.f, presetTotal - 1.f)));
						if (Module::inputs[INPUT_CV].getChannels() == 2 && slotC4Trigger.process(Module::inputs[INPUT_CV].getVoltage(1))) {
							presetLoad(std::round(clamp(Module::inputs[INPUT_CV].getVoltage() * 12.f, 0.f, presetTotal - 1.f)), false, true);
						}
						break;
					case SLOTCVMODE::TRIG_FWD:
						if (slotTrigger.process(Module::inputs[INPUT_CV].getVoltage())) {
							if (resetTimer.getTime() >= 1e-3f) {
								presetLoad((preset + 1) % presetCount);
							}
						}
						break;
					case SLOTCVMODE::TRIG_REV:
						if (slotTrigger.process(Module::inputs[INPUT_CV].getVoltage())) {
							if (resetTimer.getTime() >= 1e-3f) {
								presetLoad((preset - 1 + presetCount) % presetCount);
							}
						}
						break;
					case SLOTCVMODE::TRIG_PINGPONG:
						if (slotTrigger.process(Module::inputs[INPUT_CV].getVoltage())) {
							if (resetTimer.getTime() >= 1e-3f) {
								int n = preset + slotCvModeDir;
								if (n >= presetCount - 1)
									slotCvModeDir = -1;
								if (n <= 0)
									slotCvModeDir = 1;
								presetLoad(n);
							}
						}
						break;
					case SLOTCVMODE::TRIG_ALT:
						if (slotTrigger.process(Module::inputs[INPUT_CV].getVoltage())) {
							if (resetTimer.getTime() >= 1e-3f) {
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
								std::mt19937 rng(random::u32());
								std::shuffle(std::begin(slotCvModeShuffle), std::end(slotCvModeShuffle), rng);
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
					EightFaceMk2Slot* slot = expSlot(i);
					switch (slot->presetButton->process(sampleTime)) {
						default:
						case LongPressButton::NO_PRESS:
							break;
						case LongPressButton::SHORT_PRESS:
							presetLoad(i, slotCvMode == SLOTCVMODE::ARM, true);
							break;
						case LongPressButton::LONG_PRESS:
							if (presetCountLongPress) presetSetCount(i + 1);
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
					EightFaceMk2Slot* slot = expSlot(i);
					switch (slot->presetButton->process(sampleTime)) {
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

		// Set lights infrequently
		if (lightDivider.process()) {
			float s = args.sampleTime * lightDivider.getDivision();
			if (lightTimer.process(s) > 0.2f) {
				lightTimer.reset();
				lightBlink ^= true;
			}
			for (int i = 0; i < presetTotal; i++) {
				EightFaceMk2Slot* slot = expSlot(i);
				bool u = *(slot->presetSlotUsed);
				if (BASE::ctrlMode == CTRLMODE::READ || BASE::ctrlMode == CTRLMODE::AUTO) {
					slot->lights[0].setBrightness(preset == i ? 1.f : (presetNext == i ? 1.f : 0.f));
					slot->lights[1].setBrightness(preset == i ? 1.f : (presetCount > i ? (u ? 1.f : 0.25f) : 0.f));
					slot->lights[2].setBrightness(preset == i ? 1.f : 0.f);
				}
				else {
					bool b = preset == i && lightBlink;
					slot->lights[0].setBrightness(b ? 0.7f : (u ? 1.f : 0.f));
					slot->lights[1].setBrightness(b ? 0.7f : (u ? 0.f : (presetCount > i ? 0.05f : 0.f)));
					slot->lights[2].setBrightness(b ? 0.7f : 0.f);
				}
			}

			BASE::lights[LIGHT_CV].setBrightness((slotCvMode == SLOTCVMODE::OFF) && lightBlink);
		}
	}

	std::string bindModule(Module* m) {
		if (!m) return std::string();
		for (BoundModule* b : boundModules) if (b->moduleId == m->id) return std::string();
		BoundModule* b = new BoundModule;
		b->moduleId = m->id;
		b->moduleName = m->model->plugin->brand + " " + m->model->name;
		b->modelSlug = m->model->slug;
		b->pluginSlug = m->model->plugin->slug;
		auto it = EightFace::guiModuleSlugs.find(std::make_tuple(b->pluginSlug, b->modelSlug));
		b->needsGuiThread = it != EightFace::guiModuleSlugs.end();
		boundModules.push_back(b);

		std::string ret;
		ModuleWidget* mw = b->getModuleWidget();
		json_t* vJ = mw->toJson();
		char* moduleJson = json_dumps(vJ, JSON_INDENT(2) | JSON_REAL_PRECISION(9));
		size_t size = strlen(moduleJson);
		if (size > 400000) {
			ret = string::f("The preset size of %s is about %ikb, which might cause performance issues.", b->moduleName, size / 1024);
		}
		free(moduleJson);
		json_decref(vJ);

		return ret;
	}

	std::string bindModuleExpander() {
		Module::Expander* exp = &(Module::leftExpander);
		if (exp->moduleId < 0) return std::string();
		Module* m = exp->module;
		return bindModule(m);
	}

	void unbindModule(BoundModule* b) {
		for (int i = 0; i < presetTotal; i++) {
			EightFaceMk2Slot* slot = expSlot(i);
			for (auto it = std::begin(*slot->preset); it != std::end(*slot->preset); it++) {
				json_t* idJ = json_object_get(*it, "id");
				if (!idJ) continue;
				int64_t id = json_integer_value(idJ);
				if (id == b->moduleId) {
					slot->preset->erase(it);
					break;
				}
			}
			*(slot->presetSlotUsed) = slot->preset->size() > 0;
		}
		for (auto it = std::begin(boundModules); it != std::end(boundModules); it++) {
			if ((*it)->moduleId == b->moduleId) {
				boundModules.erase(it);
				break;
			}
		}
		delete b;
	}

	void processWorker(int workerPreset) {
		if (workerPreset < 0) return;

		EightFaceMk2Slot* slot = expSlot(workerPreset);
		EightFaceMk2Slot* slotPrev = NULL;
		if (presetPrev >= 0) {
			slotPrev = expSlot(presetPrev);
		}

		int i = 0;
		for (json_t* vJ : *slot->preset) {
			json_t* idJ = json_object_get(vJ, "id");
			if (!idJ) continue;
			int64_t moduleId = json_integer_value(idJ);
			std::string plugin = json_string_value(json_object_get(vJ, "plugin"));
			std::string model = json_string_value(json_object_get(vJ, "model"));
			for (BoundModule* b : boundModules) {
				if (b->moduleId != moduleId) continue;
				if (b->pluginSlug != plugin || b->modelSlug != model) break;
				ModuleWidget* mw = b->getModuleWidget();
				if (!mw) continue;

				if (BASE::ctrlMode == CTRLMODE::AUTO && slotPrev && *slotPrev->presetSlotUsed) {
					json_decref((*slotPrev->preset)[i]);
					(*slotPrev->preset)[i] = mw->toJson();
				}
				// There is no stepping of the UI if the plugin window is closed,
				// in this case we must use the worker thread
				if (settings::isPlugin && !APP->window) {
					mw->fromJson(vJ);
				}
				// Hand it off to the UI thread
				else if (b->needsGuiThread || guiSafeMode != GUISAFEMODE::WORKER) {
					workerGuiQueue.push(std::make_tuple(mw, vJ));
				}
				// Explicitly configured to use the worker thread
				else {
					mw->fromJson(vJ);
				}
				break;
			}
			i++;
		}
	}

	void processGui() {
		while (!workerGuiQueue.empty()) {
			auto t = workerGuiQueue.shift();
			ModuleWidget* mw = std::get<0>(t);
			json_t* vJ = std::get<1>(t);
			if (guiSafeMode == GUISAFEMODE::GUI) {
				// This is an unlocked operation, it is not perfectly thread-safe, as the Engine
				// thread would lock on preset loading
				mw->module->fromJson(vJ);
			} else {
				mw->fromJson(vJ);
			}
		}
	}

	void presetLoad(int p, bool isNext = false, bool force = false) {
		if (p < 0 || p >= presetCount)
			return;

		EightFaceMk2Slot* slot = expSlot(p);
		if (!isNext) {
			if (p != preset || force) {
				presetPrev = preset;
				preset = p;
				presetNext = -1;
				if (!*(slot->presetSlotUsed)) return;
				taskWorker.work([=]() { processWorker(p); });
			}
		}
		else {
			if (!*(slot->presetSlotUsed)) return;
			presetNext = p;
		}
	}

	void presetSave(int p) {
		EightFaceMk2Slot* slot = expSlot(p);
		if (*(slot->presetSlotUsed)) {
			for (json_t* vJ : *(slot->preset)) {
				json_decref(vJ);
			}
			slot->preset->clear();
		}

		*(slot->presetSlotUsed) = true;
		for (BoundModule* b : boundModules) {
			ModuleWidget* mw = b->getModuleWidget();
			if (!mw) continue;
			json_t* vJ = mw->toJson();
			slot->preset->push_back(vJ);
		}
		preset = p;
	}

	void presetClear(int p) {
		EightFaceMk2Slot* slot = expSlot(p);
		if (*(slot->presetSlotUsed)) {
			for (json_t* vJ : *(slot->preset)) {
				json_decref(vJ);
			}
			slot->preset->clear();
			*expSlotLabel(p) = "";
		}
		*(slot->presetSlotUsed) = false;
		if (preset == p) preset = -1;
	}

	void presetSetCount(int p) {
		if (preset >= p) preset = 0;
		presetCount = p;
		presetPrev = -1;
		presetNext = -1;
	}

	void presetRandomize(int p) {
		for (BoundModule* b : boundModules) {
			ModuleWidget* mw = b->getModuleWidget();
			if (!mw) continue;
			mw->randomizeAction();
		}
		presetSave(p);
	}

	void presetCopyPaste(int source, int target) {
		EightFaceMk2Slot* sourceSlot = expSlot(source);
		if (!*(sourceSlot->presetSlotUsed)) return;

		EightFaceMk2Slot* targetSlot = expSlot(target);
		if (*(targetSlot->presetSlotUsed)) {
			for (json_t* vJ : *(targetSlot->preset)) {
				json_decref(vJ);
			}
			targetSlot->preset->clear();
		}

		*(targetSlot->presetSlotUsed) = true;
		auto sourcePreset = sourceSlot->preset;
		auto targetPreset = targetSlot->preset;
		for (json_t* vJ : *sourcePreset) {
			targetPreset->push_back(json_deep_copy(vJ));
		}
		if (preset == target) preset = -1;
	}

	void presetShiftBack(int p) {
		for (int i = presetTotal - 2; i >= p; i--) {
			EightFaceMk2Slot* slot = expSlot(i);
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
			EightFaceMk2Slot* slot = expSlot(i);
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

	void expanderCleanUp(EightFaceMk2Base<NUM_PRESETS>* t) {
		// ctrlUniqueId == -2 for presets before uniqueId was added
		if (t->ctrlUniqueId != -2 || (t->ctrlModuleId >= 0 && t->ctrlModuleId != Module::id)) {
			Module::ResetEvent re;
			t->onReset(re);
		}
		t->ctrlUniqueId = BASE::ctrlUniqueId;
	}

	void setCvMode(SLOTCVMODE mode) {
		slotCvMode = slotCvModeBak = mode;
	}

	int faceSlotCmd(SLOT_CMD cmd, int i) override {
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

		json_object_set_new(rootJ, "slotCvMode", json_integer((int)slotCvMode));
		json_object_set_new(rootJ, "preset", json_integer(preset));
		json_object_set_new(rootJ, "presetCount", json_integer(presetCount));
		json_object_set_new(rootJ, "presetCountLongPress", json_boolean(presetCountLongPress));

		json_object_set_new(rootJ, "boxDraw", json_integer(boxDraw));
		json_object_set_new(rootJ, "boxColor", json_string(color::toHexString(boxColor).c_str()));
		json_object_set_new(rootJ, "boxOpacity", json_real(boxOpacity));

		json_object_set_new(rootJ, "guiSafeMode", json_integer((int)guiSafeMode));

		json_t* boundModulesJ = json_array();
		for (BoundModule* b : boundModules) {
			json_t* boundModuleJ = json_object();
			json_object_set_new(boundModuleJ, "moduleId", json_integer(b->moduleId));
			json_object_set_new(boundModuleJ, "pluginSlug", json_string(b->pluginSlug.c_str()));
			json_object_set_new(boundModuleJ, "modelSlug", json_string(b->modelSlug.c_str()));
			json_object_set_new(boundModuleJ, "moduleName", json_string(b->moduleName.c_str()));
			json_array_append_new(boundModulesJ, boundModuleJ);
		}
		json_object_set_new(rootJ, "boundModules", boundModulesJ);

		return rootJ;
	}

	void dataFromJson(json_t* rootJ) override {
		json_t* panelThemeJ = json_object_get(rootJ, "panelTheme");
		if (panelThemeJ) BASE::panelTheme = json_integer_value(panelThemeJ);

		json_t* slotCvModeJ = json_object_get(rootJ, "slotCvMode");
		if (slotCvModeJ) slotCvMode = (SLOTCVMODE)json_integer_value(slotCvModeJ);
		json_t* presetJ = json_object_get(rootJ, "preset");
		if (presetJ) preset = json_integer_value(presetJ);
		json_t* presetCountJ = json_object_get(rootJ, "presetCount");
		if (presetCountJ) presetCount = json_integer_value(presetCountJ);
		json_t* presetCountLongPressJ = json_object_get(rootJ, "presetCountLongPress");
		if (presetCountLongPressJ) presetCountLongPress = json_boolean_value(presetCountLongPressJ);

		json_t* boxDrawJ = json_object_get(rootJ, "boxDraw");
		if (boxDrawJ) boxDraw = json_integer_value(boxDrawJ);
		json_t* boxColorJ = json_object_get(rootJ, "boxColor");
		if (boxColorJ && json_is_string(boxColorJ)) boxColor = color::fromHexString(json_string_value(boxColorJ));
		json_t* boxOpacityJ = json_object_get(rootJ, "boxOpacity");
		if (boxOpacityJ) boxOpacity = json_real_value(boxOpacityJ);

		json_t* guiSafeModeJ = json_object_get(rootJ, "guiSafeMode");
		guiSafeMode = guiSafeModeJ ? (GUISAFEMODE)json_integer_value(guiSafeModeJ) : GUISAFEMODE::WORKER;
	
		if (preset >= presetCount) {
			preset = -1;
		}

		inChange = true;
		for (BoundModule* b : boundModules) {
			delete b;
		}
		boundModules.clear();

		json_t* boundModulesJ = json_object_get(rootJ, "boundModules");
		if (boundModulesJ) {
			json_t* boundModuleJ;
			size_t boundModuleIndex;
			json_array_foreach(boundModulesJ, boundModuleIndex, boundModuleJ) {
				json_t* moduleIdJ = json_object_get(boundModuleJ, "moduleId");
				int64_t moduleId = json_integer_value(moduleIdJ);
				json_t* pluginSlugJ = json_object_get(boundModuleJ, "pluginSlug");
				std::string pluginSlug = json_string_value(pluginSlugJ);
				json_t* modelSlugJ = json_object_get(boundModuleJ, "modelSlug");
				std::string modelSlug = json_string_value(modelSlugJ);
				json_t* moduleNameJ = json_object_get(boundModuleJ, "moduleName");
				std::string moduleName = json_string_value(moduleNameJ);

				moduleId = BASE::idFix(moduleId);
				BoundModule* b = new BoundModule;
				b->moduleId = moduleId;
				b->pluginSlug = pluginSlug;
				b->modelSlug = modelSlug;
				b->moduleName = moduleName;
				auto it = EightFace::guiModuleSlugs.find(std::make_tuple(b->pluginSlug, b->modelSlug));
				b->needsGuiThread = it != EightFace::guiModuleSlugs.end();
				boundModules.push_back(b);
			}
		}
		inChange = false;

		BASE::dataFromJson(rootJ);
		if (BASE::ctrlUniqueId == -2) BASE::ctrlUniqueId = Module::id;
		Module::params[PARAM_RW].setValue(0.f);

		switch (autoload) {
			case EightFace::AUTOLOAD::FIRST:
				presetLoad(0, false, true);
				break;
			case EightFace::AUTOLOAD::LASTACTIVE:
				presetLoad(preset, false, true);
				break;
			default:
				break;
		}
	}
};


template <int NUM_PRESETS>
struct ModuleOuterBoundsDrawerWidget : Widget {
	typedef EightFaceMk2Module<NUM_PRESETS> MODULE;
	MODULE* module = NULL;
	bool bindingActive = false;

	void draw(const DrawArgs& args) override {
		if (!module) return;
		
		if (!bindingActive) {
			switch (module->boxDraw) {
				case 0:
					return;
				case 1:
					break;
				case 2:
					Widget* w = APP->event->getSelectedWidget();
					if (!w) return;
					ModuleWidget* mw = dynamic_cast<ModuleWidget*>(w);
					if (mw && mw->module == module) break;
					if (mw && module->expandersConnected.find(mw->module->getId()) != module->expandersConnected.end()) break;
					return;
			}
		}

		Rect viewPort = getViewport(box);
		for (typename MODULE::BoundModule* b : module->boundModules) {
			ModuleWidget* mw = APP->scene->rack->getModule(b->moduleId);
			if (!mw) continue;

			Vec p1 = mw->getRelativeOffset(Vec(), this);
			Vec p = getAbsoluteOffset(Vec()).neg();
			p = p.plus(p1);
			p = p.div(APP->scene->rackScroll->zoomWidget->zoom);

			// Draw only if currently visible
			if (viewPort.isIntersecting(Rect(p, mw->box.size))) {
				nvgSave(args.vg);
				nvgResetScissor(args.vg);
				nvgTranslate(args.vg, p.x, p.y);

				float r = 3.f;
				float x = 1.f, y = 1.f, w = mw->box.size.x - 2.f, h = mw->box.size.y - 2.f;

				// Subtle tinted fill
				nvgBeginPath(args.vg);
				nvgRoundedRect(args.vg, x, y, w, h, r);
				NVGcolor fillColor = module->boxColor;
				fillColor.a = module->boxOpacity * 0.08f;
				nvgFillColor(args.vg, fillColor);
				nvgFill(args.vg);

				// Soft glow halo
				nvgBeginPath(args.vg);
				nvgRoundedRect(args.vg, x, y, w, h, r);
				NVGcolor glowColor = module->boxColor;
				glowColor.a = module->boxOpacity * 0.25f;
				nvgStrokeColor(args.vg, glowColor);
				nvgStrokeWidth(args.vg, 5.f);
				nvgStroke(args.vg);

				// Crisp outline
				nvgBeginPath(args.vg);
				nvgRoundedRect(args.vg, x, y, w, h, r);
				NVGcolor strokeColor = module->boxColor;
				strokeColor.a = module->boxOpacity;
				nvgStrokeColor(args.vg, strokeColor);
				nvgStrokeWidth(args.vg, 1.5f);
				nvgStroke(args.vg);

				nvgRestore(args.vg);
			}
		}
		Widget::draw(args);
	}
};

template <class MODULE>
struct ModuleColorWidget : Widget {
	MODULE* module = NULL;
	ModuleColorWidget() {
		box.size = Vec(13.0f, 4.5f);
	}
	void draw(const DrawArgs& args) override {
		if (!module || module->boxDraw == 0) return;
		nvgBeginPath(args.vg);
		nvgRoundedRect(args.vg, 0.f, 0.f, box.size.x, box.size.y, 2.2f);
		NVGcolor fillColor = module->boxColor;
		fillColor.a = module->boxOpacity;
		nvgFillColor(args.vg, fillColor);
		nvgFill(args.vg);
		Widget::draw(args);
	}
};

template <int NUM_PRESETS>
struct EightFaceMk2Widget : ThemedModuleWidget<EightFaceMk2Module<NUM_PRESETS>> {
	typedef EightFaceMk2Widget<NUM_PRESETS> WIDGET;
	typedef ThemedModuleWidget<EightFaceMk2Module<NUM_PRESETS>> BASE;
	typedef EightFaceMk2Module<NUM_PRESETS> MODULE;
	MODULE* module;

	ModuleOuterBoundsDrawerWidget<NUM_PRESETS>* boxDrawer = NULL;
	ModuleSelectProcessor moduleSelectProcessor;
	std::string moduleSelectProcessorStr;

	EightFaceMk2Widget(MODULE* module)
		: ThemedModuleWidget<MODULE>(module, "EightFaceMk2") {
		BASE::setModule(module);
		this->module = module;
		this->disableDuplicateAction = true;

		if (module) {
			boxDrawer = new ModuleOuterBoundsDrawerWidget<NUM_PRESETS>;
			boxDrawer->module = module;
			// This is where the magic happens: add a new widget on top-level to Rack
			APP->scene->rack->addChild(boxDrawer);

			// Move the cable-widget to the end, boxes should appear below cables
			// NB: this should be considered unstable API
			std::list<Widget*>::iterator it;
			for (it = APP->scene->rack->children.begin(); it != APP->scene->rack->children.end(); ++it){
				if (*it == APP->scene->rack->getCableContainer()) break;
			}
			if (it != APP->scene->rack->children.end()) {
				APP->scene->rack->children.splice(APP->scene->rack->children.end(), APP->scene->rack->children, it);
			}
		}

		BASE::addChild(createWidget<StoermelderBlackScrew>(Vec(RACK_GRID_WIDTH, 0)));
		BASE::addChild(createWidget<StoermelderBlackScrew>(Vec(BASE::box.size.x - 2 * RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));

		BASE::addChild(createLightCentered<TinyLight<YellowLight>>(Vec(11.4f, 46.2f), module, MODULE::LIGHT_CV));
		BASE::addInput(createInputCentered<StoermelderPort>(Vec(22.5f, 58.9f), module, MODULE::INPUT_CV));
		BASE::addInput(createInputCentered<StoermelderPort>(Vec(22.5f, 94.2f), module, MODULE::INPUT_RESET));

		BASE::addChild(createLightCentered<TinyLight<WhiteLight>>(Vec(11.4f, 118.1f), module, MODULE::LIGHT_LEARN));
		ModuleColorWidget<MODULE>* cw = createWidgetCentered<ModuleColorWidget<MODULE>>(Vec(22.5f, 118.1f));
		cw->module = module;
		BASE::addChild(cw);

		for (size_t i = 0; i < NUM_PRESETS; i++) {
			float o = i * (164.8f / (NUM_PRESETS - 1));
			EightFaceMk2LedButton<NUM_PRESETS>* ledButton = createParamCentered<EightFaceMk2LedButton<NUM_PRESETS>>(Vec(22.5f, 140.6f + o), module, MODULE::PARAM_PRESET + i);
			ledButton->module = module;
			ledButton->id = i;
			BASE::addParam(ledButton);
			BASE::addChild(createLightCentered<MediumSimpleLight<RedGreenBlueLight>>(Vec(22.5f, 140.6f + o), module, MODULE::LIGHT_PRESET + i * 3));
		}

		BASE::addParam(createParamCentered<CKSSThreeH>(Vec(22.5f, 336.2f), module, MODULE::PARAM_RW));
	}

	~EightFaceMk2Widget() {
		if (boxDrawer) {
			APP->scene->rack->removeChild(boxDrawer);
			delete boxDrawer;
		}
	}

	void onDeselect(const event::Deselect& e) override {
		BASE::onDeselect(e);
		moduleSelectProcessor.processDeselect();
	}

	void step() override {
		if (BASE::module) {
			moduleSelectProcessor.step();
			BASE::module->lights[MODULE::LIGHT_LEARN].setBrightness(moduleSelectProcessor.isLearning());
			if (boxDrawer) boxDrawer->bindingActive = moduleSelectProcessor.isLearning();
			module->processGui();
		}
		BASE::step();
	}

	void appendContextMenu(Menu* menu) override {
		ThemedModuleWidget<MODULE>::appendContextMenu(menu);
		MODULE* module = dynamic_cast<MODULE*>(this->module);
		assert(module);

		struct NumberOfSlotsSlider : ui::Slider {
			struct NumberOfSlotsQuantity : Quantity {
				MODULE* module;
				float v = -1.f;

				NumberOfSlotsQuantity(MODULE* module) {
					this->module = module;
				}
				void setValue(float value) override {
					v = clamp(value, 1.f, float(module->presetTotal));
					module->presetSetCount(int(v));
				}
				float getValue() override {
					if (v < 0.f) v = module->presetCount;
					return v;
				}
				float getDefaultValue() override {
					return 8.f;
				}
				float getMinValue() override {
					return 1.f;
				}
				float getMaxValue() override {
					return float(module->presetTotal);
				}
				float getDisplayValue() override {
					return getValue();
				}
				std::string getDisplayValueString() override {
					int i = int(getValue());
					return string::f("%i", i);
				}
				void setDisplayValue(float displayValue) override {
					setValue(displayValue);
				}
				std::string getLabel() override {
					return "Slots";
				}
				std::string getUnit() override {
					return "";
				}
			};

			NumberOfSlotsSlider(MODULE* module) {
				box.size.x = 160.0;
				quantity = new NumberOfSlotsQuantity(module);
			}
			~NumberOfSlotsSlider() {
				delete quantity;
			}
			void onDragMove(const event::DragMove& e) override {
				if (quantity) {
					quantity->moveScaledValue(0.002f * e.mouseDelta.x);
				}
			}
		};

		menu->addChild(new MenuSeparator());
		menu->addChild(createMenuLabel("Stability & performance mode"));
		menu->addChild(createBoolMenuItem("Safe", "",
			[=]() { return module->guiSafeMode == GUISAFEMODE::GUI_WITH_LOCK; },
			[=](bool v) {
				std::string msg = "Using \"Safe\" will load presets perfectly safe without risking any crashes, but may lead to performance issues (e.g. stuttering). Proceed?";
				if (osdialog_message(OSDIALOG_WARNING, OSDIALOG_YES_NO, msg.c_str()))
					module->guiSafeMode = GUISAFEMODE::GUI_WITH_LOCK;
			}
		));
		menu->addChild(createBoolMenuItem("Unsafe", "",
			[=]() { return module->guiSafeMode == GUISAFEMODE::GUI; },
			[=](bool v) {
				std::string msg = "Using \"Unsafe-mode\" will load presets quickly but may lead to crashing VCV Rack or other issues. Proceed?";
				if (osdialog_message(OSDIALOG_WARNING, OSDIALOG_YES_NO, msg.c_str()))
					module->guiSafeMode = GUISAFEMODE::GUI;
			}
		));
		menu->addChild(createBoolMenuItem("Unsafe fast", "",
			[=]() { return module->guiSafeMode == GUISAFEMODE::WORKER; },
			[=](bool v) {
				std::string msg = "Using \"Unsafe fast-mode\" will load presets most quickly but may lead to crashing VCV Rack or other issues. Proceed?";
				if (osdialog_message(OSDIALOG_WARNING, OSDIALOG_YES_NO, msg.c_str()))
					module->guiSafeMode = GUISAFEMODE::WORKER;
			}
		));

		menu->addChild(new MenuSeparator());
		menu->addChild(createSubmenuItem("Number of slots", string::f("%i", module->presetCount),
			[=](Menu* menu) {
				menu->addChild(new NumberOfSlotsSlider(module));
				menu->addChild(createBoolPtrMenuItem("Set by long-press", "", &module->presetCountLongPress));
			}
		));

		const std::map<SLOTCVMODE, std::string> slotCvModes {
			{ SLOTCVMODE::TRIG_FWD, "Trigger forward" },
			{ SLOTCVMODE::TRIG_REV, "Trigger reverse" },
			{ SLOTCVMODE::TRIG_PINGPONG, "Trigger pingpong" },
			{ SLOTCVMODE::TRIG_ALT, "Trigger alternating" },
			{ SLOTCVMODE::TRIG_RANDOM, "Trigger random" },
			{ SLOTCVMODE::TRIG_RANDOM_WO_REPEAT, "Trigger pseudo-random" },
			{ SLOTCVMODE::TRIG_RANDOM_WALK, "Trigger random walk" },
			{ SLOTCVMODE::TRIG_SHUFFLE, "Trigger shuffle" },
			{ SLOTCVMODE::VOLT, "0..10V" },
			{ SLOTCVMODE::C4, "C4" },
			{ SLOTCVMODE::ARM, "Arm" },
			{ SLOTCVMODE::OFF, "Off" }
		};

		menu->addChild(createSubmenuItem("Port SLOT mode", slotCvModes.at(module->slotCvMode),
			[=](Menu* menu) {
				auto f = [=](SLOTCVMODE slotCvMode, std::string rightText = "") {
					menu->addChild(StoermelderPackOne::Rack::createValuePtrMenuItem(slotCvModes.at(slotCvMode), rightText, &module->slotCvMode, slotCvMode));
				};
				f(SLOTCVMODE::TRIG_FWD);
				f(SLOTCVMODE::TRIG_REV);
				f(SLOTCVMODE::TRIG_PINGPONG);
				f(SLOTCVMODE::TRIG_ALT);
				f(SLOTCVMODE::TRIG_RANDOM);
				f(SLOTCVMODE::TRIG_RANDOM_WO_REPEAT);
				f(SLOTCVMODE::TRIG_RANDOM_WALK);
				f(SLOTCVMODE::TRIG_SHUFFLE);
				f(SLOTCVMODE::VOLT);
				f(SLOTCVMODE::C4);
				f(SLOTCVMODE::ARM);
				menu->addChild(new MenuSeparator);
				f(SLOTCVMODE::OFF, RACK_MOD_SHIFT_NAME "+Q");
			}
		));
		/*
		menu->addChild(createSubmenuItem("Autoload", "",
			[=](Menu* menu) {
				menu->addChild(StoermelderPackOne::Rack::createValuePtrMenuItem("Off", &module->autoload, EightFace::AUTOLOAD::OFF));
				menu->addChild(StoermelderPackOne::Rack::createValuePtrMenuItem("First preset", &module->autoload, EightFace::AUTOLOAD::FIRST));
				menu->addChild(StoermelderPackOne::Rack::createValuePtrMenuItem("Last active preset", &module->autoload, EightFace::AUTOLOAD::LASTACTIVE));
			}
		));
		*/
		menu->addChild(createSubmenuItem("Modules outline", "", [module](Menu* menu) {
			menu->addChild(StoermelderPackOne::Rack::createValuePtrMenuItem("When selected", &module->boxDraw, 2));
			menu->addChild(StoermelderPackOne::Rack::createValuePtrMenuItem("Never", &module->boxDraw, 0));
			menu->addChild(StoermelderPackOne::Rack::createValuePtrMenuItem("Always", &module->boxDraw, 1));
			menu->addChild(new MenuSeparator());
			menu->addChild(Rack::createPtrSlider(&module->boxOpacity, 0.0f, 1.0f, 0.5f, "Outline opacity", "%", 100));
			menu->addChild(new MenuSeparator());
        	Rack::appendColorSubmenuItems(menu, &module->boxColor);
		}));

		menu->addChild(new MenuSeparator());
		menu->addChild(createMenuItem("Bind module (left)", "", [=]() {
			moduleSelectProcessor.disableLearn();
			std::string s = module->bindModuleExpander();
			if (!s.empty()) {
				osdialog_message(OSDIALOG_WARNING, OSDIALOG_OK, s.c_str());
			}
		}));
		menu->addChild(createMenuItem("Bind module (select one)", "", [=]() {
			moduleSelectProcessor.setOwner(this);
			moduleSelectProcessor.startLearn([module](ModuleWidget* mw, Vec pos) {
				std::string s = module->bindModule(mw->module);
				if (!s.empty()) {
					osdialog_message(OSDIALOG_WARNING, OSDIALOG_OK, s.c_str());
				}
			});
		}));
		menu->addChild(createMenuItem("Bind modules (select multiple)", "", [=]() {
			moduleSelectProcessor.setOwner(this);
			moduleSelectProcessorStr = "";
			moduleSelectProcessor.startLearn(
				[this, module](ModuleWidget* mw, Vec pos) {
					std::string s = module->bindModule(mw->module);
					if (!s.empty()) moduleSelectProcessorStr += s + "\n";
				}, ModuleSelectProcessor::LEARN_MODE::MULTI,
				[this]() {
					if (!moduleSelectProcessorStr.empty()) {
						osdialog_message(OSDIALOG_WARNING, OSDIALOG_OK, moduleSelectProcessorStr.c_str());
					}
				}
			);
		}));
		menu->addChild(createMenuItem("Bind modules (current selection)", "", [=]() {
			std::string s;
			for (ModuleWidget* mw : APP->scene->rack->getSelected()) {
				std::string _s = module->bindModule(mw->module);
				if (!_s.empty()) s += _s + "\n";
			}
			if (!s.empty()) {
				osdialog_message(OSDIALOG_WARNING, OSDIALOG_OK, s.c_str());
			}
			APP->scene->rack->deselectAll();
		}));

		if (module->boundModules.size() > 0) {
			menu->addChild(new MenuSeparator());
			menu->addChild(createSubmenuItem("Bound modules", string::f("%i", module->boundModules.size()),
				[=](Menu* menu) {
					for (typename MODULE::BoundModule* b : module->boundModules) {
						ModuleWidget* mw = b->getModuleWidget();
						std::string text = (!mw ? "[ERROR] " : "") + b->moduleName;
						menu->addChild(createSubmenuItem(text, "",
							[=](Menu* menu) {
								ModuleWidget* mw = b->getModuleWidget();
								if (mw) menu->addChild(createMenuItem("Zoom to module", "", [=]() { StoermelderPackOne::Rack::ViewportCenter{mw}; }));
								menu->addChild(createMenuItem("Unbind", "", [=]() { module->unbindModule(b); }));
							}
						));
					}
				}
			));
		}
	}

	void onHoverKey(const event::HoverKey& e) override {
		if (e.action == GLFW_PRESS && (e.mods & RACK_MOD_MASK) == GLFW_MOD_SHIFT) {
			switch (e.key) {
				case GLFW_KEY_B:
					module->boxDraw = (module->boxDraw + 1) % 3;
					e.consume(this);
					break;
				case GLFW_KEY_Q:
					module->slotCvMode = module->slotCvMode == SLOTCVMODE::OFF ? module->slotCvModeBak : SLOTCVMODE::OFF;
					e.consume(this);
					break;
			}
		}
		ModuleWidget::onHoverKey(e);
	}
};

} // namespace EightFaceMk2
} // namespace StoermelderPackOne

Model* modelEightFaceMk2 = createModel<StoermelderPackOne::EightFaceMk2::EightFaceMk2Module<8>, StoermelderPackOne::EightFaceMk2::EightFaceMk2Widget<8>>("EightFaceMk2");