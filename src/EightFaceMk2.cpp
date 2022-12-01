#include "plugin.hpp"
#include "digital.hpp"
#include "helpers/TaskWorker.hpp"
#include "components/MenuColorLabel.hpp"
#include "components/MenuColorField.hpp"
#include "ui/ModuleSelectProcessor.hpp"
#include "EightFace.hpp"
#include "EightFaceMk2Base.hpp"
#include <random>

namespace StoermelderPackOne {
namespace EightFaceMk2 {

const std::string WHITESPACE = " \n\r\t\f\v";

std::string ltrim(const std::string& s) {
	size_t start = s.find_first_not_of(WHITESPACE);
	return (start == std::string::npos) ? "" : s.substr(start);
}

std::string rtrim(const std::string& s) {
	size_t end = s.find_last_not_of(WHITESPACE);
	return (end == std::string::npos) ? "" : s.substr(0, end + 1);
}

std::string trim(const std::string& s) {
	return rtrim(ltrim(s));
}

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
	ARM = 3
};

template <int NUM_PRESETS>
struct EightFaceMk2Module : EightFaceMk2Base<NUM_PRESETS> {
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

	/** Total number of snapshots including expanders */
	int presetTotal;
	int presetNext;
	int presetCopy = -1;

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
	bool resetFlag = false;

	dsp::ClockDivider buttonDivider;
	dsp::ClockDivider boundModulesDivider;
	dsp::ClockDivider lightDivider;
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

	/** [Stored to JSON] */
	bool boxDraw;
	/** [Stored to JSON] */
	NVGcolor boxColor;

	dsp::RingBuffer<std::tuple<ModuleWidget*, json_t*>, 16> workerGuiQueue;
	TaskWorker taskWorker;

	EightFaceMk2Module() {
		BASE::panelTheme = pluginSettings.panelThemeDefault;
		Module::config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);
		Module::configParam(PARAM_RW, 0, 1, 0, "Read/write mode");
		Module::configInput(INPUT_CV, "Slot-selection");
		Module::inputInfos[INPUT_CV]->description = "Channel 2 can retrigger the current slot in C4 mode";
		Module::configInput(INPUT_RESET, "Sequencer-mode reset");

		for (int i = 0; i < NUM_PRESETS; i++) {
			EightFaceMk2ParamQuantity<NUM_PRESETS>* pq = Module::configParam<EightFaceMk2ParamQuantity<NUM_PRESETS>>(PARAM_PRESET + i, 0, 1, 0);
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
		onReset();
	}

	~EightFaceMk2Module() {
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

	void onReset() override {
		inChange = true;
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

		preset = -1;
		presetCount = NUM_PRESETS;
		presetNext = -1;

		autoload = EightFace::AUTOLOAD::OFF;
		boxDraw = true;
		boxColor = color::BLUE;

		Module::onReset();
		EightFaceMk2Base<NUM_PRESETS>* t = this;
		int c = 0;
		while (true) {
			c++;
			if (c == MAX_EXPANDERS + 1) break;
			Module* exp = t->rightExpander.module;
			if (!exp) break;
			if (exp->model != modelEightFaceMk2Ex) break;
			t = reinterpret_cast<EightFaceMk2Base<NUM_PRESETS>*>(exp);
			t->onReset();
		}
	}

	void onSampleRateChange() override {
		boundModulesDivider.setDivision(APP->engine->getSampleRate());
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

		presetTotal = NUM_PRESETS;
		Module* m = this;
		EightFaceMk2Base<NUM_PRESETS>* t = this;
		t->ctrlMode = (CTRLMODE)Module::params[PARAM_RW].getValue();
		int c = 0;
		while (true) {
			N[c] = t;
			c++;
			if (c == MAX_EXPANDERS + 1) break;

			Module* exp = m->rightExpander.module;
			if (!exp) break;
			if (exp->model != modelEightFaceMk2Ex) break;
			m = exp;
			t = reinterpret_cast<EightFaceMk2Base<NUM_PRESETS>*>(exp);
			if (t->ctrlModuleId >= 0 && t->ctrlModuleId != Module::id) t->onReset();
			t->panelTheme = BASE::panelTheme;
			t->ctrlModuleId = Module::id;
			t->ctrlOffset = c;
			t->ctrlMode = BASE::ctrlMode;
			presetTotal += NUM_PRESETS;
		}
		int presetCount = std::min(this->presetCount, presetTotal);

		// Read mode
		if (BASE::ctrlMode == CTRLMODE::READ) {
			// RESET input
			if (slotCvMode == SLOTCVMODE::TRIG_FWD ||
				slotCvMode == SLOTCVMODE::TRIG_REV ||
				slotCvMode == SLOTCVMODE::TRIG_PINGPONG ||
				slotCvMode == SLOTCVMODE::TRIG_ALT ||
				slotCvMode == SLOTCVMODE::TRIG_SHUFFLE) {
				if (Module::inputs[INPUT_RESET].isConnected() && resetTrigger.process(Module::inputs[INPUT_RESET].getVoltage())) {
					resetTimer.reset();
					resetFlag = true;
					slotCvModeDir = 1;
					slotCvModeAlt = 0;
					slotCvModeShuffle.clear();
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
						if (Module::inputs[INPUT_CV].getChannels() == 2 && slotC4Trigger.process(Module::inputs[INPUT_CV].getVoltage(1))) {
							presetLoad(std::round(clamp(Module::inputs[INPUT_CV].getVoltage() * 12.f, 0.f, presetTotal - 1.f)), false, true);
						}
						break;
					case SLOTCVMODE::TRIG_FWD:
						if (slotTrigger.process(Module::inputs[INPUT_CV].getVoltage())) {
							presetLoad(resetFlag ? 0 : ((preset + 1) % presetCount));
							resetFlag = false;
						}
						break;
					case SLOTCVMODE::TRIG_REV:
						if (slotTrigger.process(Module::inputs[INPUT_CV].getVoltage())) {
							presetLoad(resetFlag ? (presetCount - 1) : ((preset - 1 + presetCount) % presetCount));
							resetFlag = false;
						}
						break;
					case SLOTCVMODE::TRIG_PINGPONG:
						if (slotTrigger.process(Module::inputs[INPUT_CV].getVoltage())) {
							int n = preset + slotCvModeDir;
							if (n >= presetCount - 1)
								slotCvModeDir = -1;
							if (n <= 0)
								slotCvModeDir = 1;
							presetLoad(resetFlag ? 0 : n);
							resetFlag = false;
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
							presetLoad(resetFlag ? 0 : n);
							resetFlag = false;
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
							resetFlag = false;
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
					EightFaceMk2Slot* slot = expSlot(i);
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
				if (BASE::ctrlMode == CTRLMODE::READ) {
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

	void bindModule(Module* m) {
		if (!m) return;
		for (BoundModule* b : boundModules) if (b->moduleId == m->id) return;
		BoundModule* b = new BoundModule;
		b->moduleId = m->id;
		b->moduleName = m->model->plugin->brand + " " + m->model->name;
		b->modelSlug = m->model->slug;
		b->pluginSlug = m->model->plugin->slug;
		auto it = EightFace::guiModuleSlugs.find(std::make_tuple(b->pluginSlug, b->modelSlug));
		b->needsGuiThread = it != EightFace::guiModuleSlugs.end();
		boundModules.push_back(b);
	}

	void bindModuleExpander() {
		Module::Expander* exp = &(Module::leftExpander);
		if (exp->moduleId < 0) return;
		Module* m = exp->module;
		bindModule(m);
	}

	void unbindModule(BoundModule* b) {
		for (int i = 0; i < presetTotal; i++) {
			EightFaceMk2Slot* slot = expSlot(i);
			for (auto it = std::begin(*slot->preset); it != std::end(*slot->preset); it++) {
				json_t* idJ = json_object_get(*it, "id");
				if (!idJ) continue;
				int id = json_integer_value(idJ);
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

				if (b->needsGuiThread) {
					workerGuiQueue.push(std::make_tuple(mw, vJ));
				}
				else {
					mw->fromJson(vJ);
				}
				break;
			}
		}
	}

	void processGui() {
		while (!workerGuiQueue.empty()) {
			auto t = workerGuiQueue.shift();
			ModuleWidget* mw = std::get<0>(t);
			json_t* vJ = std::get<1>(t);
			mw->fromJson(vJ);
		}
	}

	void presetLoad(int p, bool isNext = false, bool force = false) {
		if (p < 0 || p >= presetCount)
			return;

		EightFaceMk2Slot* slot = expSlot(p);
		if (!isNext) {
			if (p != preset || force) {
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

	bool isBoxActive() {
		return boxDraw && !BASE::isBypassed();
	}

	json_t* dataToJson() override {
		json_t* rootJ = BASE::dataToJson();

		json_object_set_new(rootJ, "slotCvMode", json_integer((int)slotCvMode));
		json_object_set_new(rootJ, "preset", json_integer(preset));
		json_object_set_new(rootJ, "presetCount", json_integer(presetCount));

		json_object_set_new(rootJ, "boxDraw", json_boolean(boxDraw));
		json_object_set_new(rootJ, "boxColor", json_string(color::toHexString(boxColor).c_str()));

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
		BASE::panelTheme = json_integer_value(json_object_get(rootJ, "panelTheme"));

		slotCvMode = (SLOTCVMODE)json_integer_value(json_object_get(rootJ, "slotCvMode"));
		preset = json_integer_value(json_object_get(rootJ, "preset"));
		presetCount = json_integer_value(json_object_get(rootJ, "presetCount"));

		boxDraw = json_boolean_value(json_object_get(rootJ, "boxDraw"));
		json_t* boxColorJ = json_object_get(rootJ, "boxColor");
		if (boxColorJ) boxColor = color::fromHexString(json_string_value(boxColorJ));

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

		BASE::idFixClearMap();
		BASE::dataFromJson(rootJ);
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


template <class MODULE>
struct ModuleOuterBoundsDrawerWidget : Widget {
	MODULE* module = NULL;

	void draw(const DrawArgs& args) override {
		if (!module || !module->isBoxActive()) return;

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
				nvgBeginPath(args.vg);
				nvgRect(args.vg, 1.f, 1.f, mw->box.size.x - 2.f, mw->box.size.y - 2.f);
				nvgStrokeColor(args.vg, module->boxColor);
				nvgStrokeWidth(args.vg, 2.f);
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
		if (!module || !module->isBoxActive()) return;
		nvgBeginPath(args.vg);
		nvgRoundedRect(args.vg, 0.f, 0.f, box.size.x, box.size.y, 2.2f);
		nvgFillColor(args.vg, module->boxColor);
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

	ModuleOuterBoundsDrawerWidget<MODULE>* boxDrawer = NULL;
	ModuleSelectProcessor moduleSelectProcessor;

	EightFaceMk2Widget(MODULE* module)
		: ThemedModuleWidget<MODULE>(module, "EightFaceMk2") {
		BASE::setModule(module);
		this->module = module;
		this->disableDuplicateAction = true;

		if (module) {
			boxDrawer = new ModuleOuterBoundsDrawerWidget<MODULE>;
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

		ModuleColorWidget<MODULE>* cw = createWidgetCentered<ModuleColorWidget<MODULE>>(Vec(22.5f, 118.1f));
		cw->module = module;
		BASE::addChild(cw);

		for (size_t i = 0; i < NUM_PRESETS; i++) {
			float o = i * (164.8f / (NUM_PRESETS - 1));
			EightFaceMk2LedButton<NUM_PRESETS>* ledButton = createParamCentered<EightFaceMk2LedButton<NUM_PRESETS>>(Vec(22.5f, 140.6f + o), module, MODULE::PARAM_PRESET + i);
			ledButton->module = module;
			ledButton->id = i;
			BASE::addParam(ledButton);
			BASE::addChild(createLightCentered<LargeLight<RedGreenBlueLight>>(Vec(22.5f, 140.6f + o), module, MODULE::LIGHT_PRESET + i * 3));
		}

		BASE::addChild(createLightCentered<TinyLight<WhiteLight>>(Vec(11.4f, 322.7f), module, MODULE::LIGHT_LEARN));
		BASE::addParam(createParamCentered<CKSSH>(Vec(22.5f, 336.2f), module, MODULE::PARAM_RW));
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
			module->processGui();
		}
		BASE::step();
	}

	void appendContextMenu(Menu* menu) override {
		ThemedModuleWidget<MODULE>::appendContextMenu(menu);
		MODULE* module = dynamic_cast<MODULE*>(this->module);
		assert(module);

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
				menu->addChild(construct<SlotCvModeItem>(&MenuItem::text, "Off", &SlotCvModeItem::rightTextEx, RACK_MOD_SHIFT_NAME "+Q", &SlotCvModeItem::module, module, &SlotCvModeItem::slotCvMode, SLOTCVMODE::OFF));
				return menu;
			}
		};

		struct AutoloadMenuItem : MenuItem {
			struct AutoloadItem : MenuItem {
				MODULE* module;
				EightFace::AUTOLOAD value;
				void onAction(const event::Action& e) override {
					module->autoload = value;
				}
				void step() override {
					rightText = CHECKMARK(module->autoload == value);
					MenuItem::step();
				}
			};

			MODULE* module;
			AutoloadMenuItem() {
				rightText = RIGHT_ARROW;
			}

			Menu* createChildMenu() override {
				Menu* menu = new Menu;
				menu->addChild(construct<AutoloadItem>(&MenuItem::text, "Off", &AutoloadItem::module, module, &AutoloadItem::value, EightFace::AUTOLOAD::OFF));
				menu->addChild(construct<AutoloadItem>(&MenuItem::text, "First preset", &AutoloadItem::module, module, &AutoloadItem::value, EightFace::AUTOLOAD::FIRST));
				menu->addChild(construct<AutoloadItem>(&MenuItem::text, "Last active preset", &AutoloadItem::module, module, &AutoloadItem::value, EightFace::AUTOLOAD::LASTACTIVE));
				return menu;
			}
		};

		struct BindModuleItem : MenuItem {
			MODULE* module;
			WIDGET* widget;
			void onAction(const event::Action& e) override {
				widget->moduleSelectProcessor.disableLearn();
				module->bindModuleExpander();
			}
		};

		struct ModuleMenuItem : MenuItem {
			struct ModuleItem : MenuItem {
				struct CenterItem : MenuItem {
					ModuleWidget* mw;
					void onAction(const event::Action& e) override {
						StoermelderPackOne::Rack::ViewportCenter{mw};
					}
				};

				struct UnbindItem : MenuItem {
					MODULE* module;
					typename MODULE::BoundModule* b;
					void onAction(const event::Action& e) override {
						module->unbindModule(b);
					}
				};

				MODULE* module;
				typename MODULE::BoundModule* b;
				ModuleItem() {
					rightText = RIGHT_ARROW;
				}
				Menu* createChildMenu() override {
					Menu* menu = new Menu;
					ModuleWidget* mw = b->getModuleWidget();
					if (mw) menu->addChild(construct<CenterItem>(&MenuItem::text, "Center module", &CenterItem::mw, mw));
					menu->addChild(construct<UnbindItem>(&MenuItem::text, "Unbind", &UnbindItem::module, module, &UnbindItem::b, b));
					return menu;
				}
			};

			MODULE* module;
			ModuleMenuItem() {
				rightText = RIGHT_ARROW;
			}

			Menu* createChildMenu() override {
				Menu* menu = new Menu;
				for (typename MODULE::BoundModule* b : module->boundModules) {
					ModuleWidget* mw = b->getModuleWidget();
					std::string text = (!mw ? "[ERROR] " : "") + b->moduleName;
					menu->addChild(construct<ModuleItem>(&MenuItem::text, text, &ModuleItem::module, module, &ModuleItem::b, b));
				}
				return menu;
			}
		};

		struct BoxDrawItem : MenuItem {
			MODULE* module;
			std::string rightTextEx;
			void onAction(const event::Action& e) override {
				module->boxDraw ^= true;
			}
			void step() override {
				rightText = (module->boxDraw ? "✔ " : "") + rightTextEx;
				MenuItem::step();
			}
		};

		struct BoxColorMenuItem : MenuItem {
			MODULE* module;
			BoxColorMenuItem() {
				rightText = RIGHT_ARROW;
			}
			Menu* createChildMenu() override {
				struct ColorField : MenuColorField {
					MODULE* module;
					NVGcolor initColor() override {
						return module->boxColor;
					}
					void returnColor(NVGcolor color) override {
						module->boxColor = color;
					}
				};

				Menu* menu = new Menu;
				MenuColorLabel* colorLabel = construct<MenuColorLabel>(&MenuColorLabel::fillColor, module->boxColor);
				menu->addChild(colorLabel);
				menu->addChild(construct<ColorField>(&ColorField::module, module, &MenuColorField::colorLabel, colorLabel));
				return menu;
			}
		};

		menu->addChild(new MenuSeparator());
		menu->addChild(construct<SlotCvModeMenuItem>(&MenuItem::text, "Port CV mode", &SlotCvModeMenuItem::module, module));
		//menu->addChild(construct<AutoloadMenuItem>(&MenuItem::text, "Autoload", &AutoloadMenuItem::module, module));
		menu->addChild(new MenuSeparator());
		menu->addChild(construct<BindModuleItem>(&MenuItem::text, "Bind module (left)", &BindModuleItem::widget, this, &BindModuleItem::module, module));
		menu->addChild(createMenuItem("Bind module (select one)", "", [=]() {
			moduleSelectProcessor.setOwner(this);
			moduleSelectProcessor.startLearn([module](ModuleWidget* mw, Vec pos) {
				module->bindModule(mw->module);
			});
		}));
		menu->addChild(createMenuItem("Bind module (select multiple)", "", [=]() {
			moduleSelectProcessor.setOwner(this);
			moduleSelectProcessor.startLearn([module](ModuleWidget* mw, Vec pos) {
				module->bindModule(mw->module);
			}, ModuleSelectProcessor::LEARN_MODE::MULTI);
		}));

		if (module->boundModules.size() > 0) {
			menu->addChild(new MenuSeparator());
			menu->addChild(construct<ModuleMenuItem>(&MenuItem::text, "Bound modules", &ModuleMenuItem::module, module));
		}

		menu->addChild(new MenuSeparator());
		menu->addChild(construct<BoxDrawItem>(&MenuItem::text, "Box visible", &BoxDrawItem::rightTextEx, RACK_MOD_SHIFT_NAME "+B", &BoxDrawItem::module, module));
		menu->addChild(construct<BoxColorMenuItem>(&MenuItem::text, "Box color", &BoxColorMenuItem::module, module));
	}

	void onHoverKey(const event::HoverKey& e) override {
		if (e.action == GLFW_PRESS && (e.mods & RACK_MOD_MASK) == GLFW_MOD_SHIFT) {
			switch (e.key) {
				case GLFW_KEY_B:
					module->boxDraw ^= true;
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