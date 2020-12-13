#include "plugin.hpp"
#include "digital.hpp"
#include "EightFace.hpp"
#include "EightFaceMk2Base.hpp"
#include <functional>
#include <mutex>
#include <condition_variable>
#include <random>

namespace StoermelderPackOne {
namespace EightFaceMk2 {

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
	int slotCvModeDir = 1;
	int slotCvModeAlt = 1;
	std::vector<int> slotCvModeShuffle;

	std::default_random_engine randGen{(uint16_t)std::chrono::system_clock::now().time_since_epoch().count()};
	std::uniform_int_distribution<int> randDist;
	bool inChange = false;

	dsp::SchmittTrigger slotTrigger;
	dsp::SchmittTrigger resetTrigger;
	dsp::Timer resetTimer;

	dsp::ClockDivider buttonDivider;
	dsp::ClockDivider boundModulesDivider;
	dsp::ClockDivider lightDivider;
	dsp::Timer lightTimer;
	bool lightBlink = false;

	EightFaceMk2Base<NUM_PRESETS>* N[MAX_EXPANDERS + 1];


	struct BoundModule {
		int moduleId;
		std::string pluginSlug;
		std::string modelSlug;
		std::string moduleName;
		ModuleWidget* getModuleWidget() { return APP->scene->rack->getModule(moduleId); }
		bool needsGuiThread = false;
	};

	/** [Stored to JSON] */
	std::vector<BoundModule*> boundModules;
	/** [Stored to JSON] */
	bool autoload = false;

	std::mutex workerMutex;
	std::condition_variable workerCondVar;
	std::thread* worker;
	bool workerIsRunning = true;
	bool workerDoProcess = false;
	int workerPreset = -1;
	dsp::RingBuffer<std::tuple<ModuleWidget*, json_t*>, 16> workerGuiQueue;


	EightFaceMk2Module() {
		BASE::panelTheme = pluginSettings.panelThemeDefault;
		Module::config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);
		Module::configParam(PARAM_RW, 0, 1, 0, "Read/write mode");
		for (int i = 0; i < NUM_PRESETS; i++) {
			Module::configParam<EightFaceMk2ParamQuantity<NUM_PRESETS>>(PARAM_PRESET + i, 0, 1, 0);
			EightFaceMk2ParamQuantity<NUM_PRESETS>* pq = (EightFaceMk2ParamQuantity<NUM_PRESETS>*)Module::paramQuantities[PARAM_PRESET + i];
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
		onReset();
		worker = new std::thread(&EightFaceMk2Module::processWorker, this);
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

		workerIsRunning = false;
		workerDoProcess = true;
		workerCondVar.notify_one();
		worker->join();
		delete worker;
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
		}
		for (BoundModule* b : boundModules) {
			delete b;
		}
		boundModules.clear();
		inChange = false;

		preset = -1;
		presetCount = NUM_PRESETS;
		presetNext = -1;

		autoload = false;

		Module::onReset();
		EightFaceMk2Base<NUM_PRESETS>* t = this;
		int c = 0;
		while (true) {
			c++;
			if (c == MAX_EXPANDERS + 1) break;
			Module* exp = t->rightExpander.module;
			if (!exp) break;
			if (exp->model->plugin->slug != "Stoermelder-P1" || exp->model->slug != "EightFaceMk2Ex") break;
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

	void process(const Module::ProcessArgs& args) override {
		if (inChange) return;

		presetTotal = NUM_PRESETS;
		Module* m = this;
		EightFaceMk2Base<NUM_PRESETS>* t = this;
		t->ctrlWrite = Module::params[PARAM_RW].getValue() > 0.f;
		int c = 0;
		while (true) {
			N[c] = t;
			c++;
			if (c == MAX_EXPANDERS + 1) break;

			Module* exp = m->rightExpander.module;
			if (!exp) break;
			if (exp->model->plugin->slug != "Stoermelder-P1" || exp->model->slug != "EightFaceMk2Ex") break;
			m = exp;
			t = reinterpret_cast<EightFaceMk2Base<NUM_PRESETS>*>(exp);
			if (t->ctrlModuleId >= 0 && t->ctrlModuleId != Module::id) t->onReset();
			t->panelTheme = BASE::panelTheme;
			t->ctrlModuleId = Module::id;
			t->ctrlOffset = c;
			t->ctrlWrite = BASE::ctrlWrite;
			presetTotal += NUM_PRESETS;
		}
		int presetCount = std::min(this->presetCount, presetTotal);

		// Read mode
		if (!BASE::ctrlWrite) {
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
				if (!BASE::ctrlWrite) {
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

	void presetLoad(int p, bool isNext = false, bool force = false) {
		if (p < 0 || p >= presetCount)
			return;

		EightFaceMk2Slot* slot = expSlot(p);
		if (!isNext) {
			if (p != preset || force) {
				preset = p;
				presetNext = -1;
				if (!*(slot->presetSlotUsed)) return;
				workerPreset = p;
				workerDoProcess = true;
				workerCondVar.notify_one();
			}
		}
		else {
			if (!*(slot->presetSlotUsed)) return;
			presetNext = p;
		}
	}

	void processWorker() {
		while (true) {
			std::unique_lock<std::mutex> lock(workerMutex);
			workerCondVar.wait(lock, std::bind(&EightFaceMk2Module::workerDoProcess, this));
			if (!workerIsRunning || workerPreset < 0) return;

			EightFaceMk2Slot* slot = expSlot(workerPreset);
			for (json_t* vJ : *slot->preset) {
				json_t* idJ = json_object_get(vJ, "id");
				if (!idJ) continue;
				int moduleId = json_integer_value(idJ);
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

			workerDoProcess = false;
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

	void setCvMode(SLOTCVMODE mode) {
		slotCvMode = mode;
	}

	void faceSlotCmd(SLOT_CMD cmd, int i) override {
		switch (cmd) {
			case SLOT_CMD::LOAD:
				presetLoad(i); break;
			case SLOT_CMD::CLEAR:
				presetClear(i); break;
			case SLOT_CMD::RANDOMIZE:
				presetRandomize(i); break;
			case SLOT_CMD::COPY:
				presetCopy = i; break;
			case SLOT_CMD::PASTE:
				presetCopyPaste(presetCopy, i); break;
		}
	}

	json_t* dataToJson() override {
		json_t* rootJ = BASE::dataToJson();

		json_object_set_new(rootJ, "slotCvMode", json_integer((int)slotCvMode));
		json_object_set_new(rootJ, "preset", json_integer(preset));
		json_object_set_new(rootJ, "presetCount", json_integer(presetCount));

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

		if (preset >= presetCount) {
			preset = -1;
		}

		// Hack for preventing duplicating this module
		if (APP->engine->getModule(BASE::id) != NULL && !BASE::idFixHasMap()) return;

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
				int moduleId = json_integer_value(moduleIdJ);
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
	}
};


template <int NUM_PRESETS>
struct EightFaceMk2Widget : ThemedModuleWidget<EightFaceMk2Module<NUM_PRESETS>> {
	typedef EightFaceMk2Widget<NUM_PRESETS> WIDGET;
	typedef ThemedModuleWidget<EightFaceMk2Module<NUM_PRESETS>> BASE;
	typedef EightFaceMk2Module<NUM_PRESETS> MODULE;
	MODULE* module;

	bool learn = false;

	EightFaceMk2Widget(MODULE* module)
		: ThemedModuleWidget<MODULE>(module, "EightFaceMk2") {
		BASE::setModule(module);
		this->module = module;

		BASE::addChild(createWidget<StoermelderBlackScrew>(Vec(RACK_GRID_WIDTH, 0)));
		BASE::addChild(createWidget<StoermelderBlackScrew>(Vec(BASE::box.size.x - 2 * RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));

		BASE::addChild(createLightCentered<TinyLight<YellowLight>>(Vec(11.4f, 46.2f), module, MODULE::LIGHT_CV));
		BASE::addInput(createInputCentered<StoermelderPort>(Vec(22.5f, 58.9f), module, MODULE::INPUT_CV));
		BASE::addInput(createInputCentered<StoermelderPort>(Vec(22.5f, 94.2f), module, MODULE::INPUT_RESET));

		BASE::addParam(createParamCentered<CKSSH>(Vec(22.5f, 336.2f), module, MODULE::PARAM_RW));

		BASE::addChild(createLightCentered<TinyLight<WhiteLight>>(Vec(11.4f, 322.7f), module, MODULE::LIGHT_LEARN));

		for (size_t i = 0; i < NUM_PRESETS; i++) {
			float o = i * (164.8f / (NUM_PRESETS - 1));
			EightFaceMk2LedButton<NUM_PRESETS>* ledButton = createParamCentered<EightFaceMk2LedButton<NUM_PRESETS>>(Vec(22.5f, 140.6f + o), module, MODULE::PARAM_PRESET + i);
			ledButton->module = module;
			ledButton->id = i;
			BASE::addParam(ledButton);
			BASE::addChild(createLightCentered<LargeLight<RedGreenBlueLight>>(Vec(22.5f, 140.6f + o), module, MODULE::LIGHT_PRESET + i * 3));
		}
	}

	void onDeselect(const event::Deselect& e) override {
		if (!learn) return;

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

	void step() override {
		if (BASE::module) {
			BASE::module->lights[MODULE::LIGHT_LEARN].setBrightness(learn > 0);
			module->processGui();
		}
		BASE::step();
	}

	void enableLearn() {
		learn ^= true;
		APP->event->setSelected(this);
		GLFWcursor* cursor = NULL;
		if (learn) {
			cursor = glfwCreateStandardCursor(GLFW_CROSSHAIR_CURSOR);
		}
		glfwSetCursor(APP->window->win, cursor);
	}

	void disableLearn() {
		learn = false;
		glfwSetCursor(APP->window->win, NULL);
	}

	void appendContextMenu(Menu* menu) override {
		ThemedModuleWidget<MODULE>::appendContextMenu(menu);
		MODULE* module = dynamic_cast<MODULE*>(this->module);
		assert(module);

		struct SlotCvModeMenuItem : MenuItem {
			struct SlotCvModeItem : MenuItem {
				MODULE* module;
				SLOTCVMODE slotCvMode;
				void onAction(const event::Action& e) override {
					module->setCvMode(slotCvMode);
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
				menu->addChild(construct<SlotCvModeItem>(&MenuItem::text, "Trigger alternating", &SlotCvModeItem::module, module, &SlotCvModeItem::slotCvMode, SLOTCVMODE::TRIG_ALT));
				menu->addChild(construct<SlotCvModeItem>(&MenuItem::text, "Trigger random", &SlotCvModeItem::module, module, &SlotCvModeItem::slotCvMode, SLOTCVMODE::TRIG_RANDOM));
				menu->addChild(construct<SlotCvModeItem>(&MenuItem::text, "Trigger pseudo-random", &SlotCvModeItem::module, module, &SlotCvModeItem::slotCvMode, SLOTCVMODE::TRIG_RANDOM_WO_REPEAT));
				menu->addChild(construct<SlotCvModeItem>(&MenuItem::text, "Trigger random walk", &SlotCvModeItem::module, module, &SlotCvModeItem::slotCvMode, SLOTCVMODE::TRIG_RANDOM_WALK));
				menu->addChild(construct<SlotCvModeItem>(&MenuItem::text, "Trigger shuffle", &SlotCvModeItem::module, module, &SlotCvModeItem::slotCvMode, SLOTCVMODE::TRIG_SHUFFLE));
				menu->addChild(construct<SlotCvModeItem>(&MenuItem::text, "0..10V", &SlotCvModeItem::module, module, &SlotCvModeItem::slotCvMode, SLOTCVMODE::VOLT));
				menu->addChild(construct<SlotCvModeItem>(&MenuItem::text, "C4", &SlotCvModeItem::module, module, &SlotCvModeItem::slotCvMode, SLOTCVMODE::C4));
				menu->addChild(construct<SlotCvModeItem>(&MenuItem::text, "Arm", &SlotCvModeItem::module, module, &SlotCvModeItem::slotCvMode, SLOTCVMODE::ARM));
				menu->addChild(new MenuSeparator);
				menu->addChild(construct<SlotCvModeItem>(&MenuItem::text, "Off", &SlotCvModeItem::module, module, &SlotCvModeItem::slotCvMode, SLOTCVMODE::OFF));
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
				widget->enableLearn();
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
					std::string text = (!mw ? "[MISSING] " : "") + b->moduleName;
					menu->addChild(construct<ModuleItem>(&MenuItem::text, text, &ModuleItem::module, module, &ModuleItem::b, b));
				}
				return menu;
			}
		};

		menu->addChild(new MenuSeparator());
		menu->addChild(construct<SlotCvModeMenuItem>(&MenuItem::text, "Port CV mode", &SlotCvModeMenuItem::module, module));
		menu->addChild(new MenuSeparator());
		menu->addChild(construct<BindModuleItem>(&MenuItem::text, "Bind module (left)", &BindModuleItem::widget, this, &BindModuleItem::module, module));
		menu->addChild(construct<BindModuleSelectItem>(&MenuItem::text, "Bind module (select)", &BindModuleSelectItem::widget, this));

		if (module->boundModules.size() > 0) {
			menu->addChild(new MenuSeparator());
			menu->addChild(construct<ModuleMenuItem>(&MenuItem::text, "Bound modules", &ModuleMenuItem::module, module));
		}
	}
};

} // namespace EightFaceMk2
} // namespace StoermelderPackOne

Model* modelEightFaceMk2 = createModel<StoermelderPackOne::EightFaceMk2::EightFaceMk2Module<8>, StoermelderPackOne::EightFaceMk2::EightFaceMk2Widget<8>>("EightFaceMk2");