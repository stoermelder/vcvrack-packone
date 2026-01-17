#include "../../plugin.hpp"
#include "../../components/XyScreenWidget.hpp"
#include "../../components/XySeqWidget.hpp"
#include "TransitBase.hpp"

namespace StoermelderPackOne {
namespace Transit {

static const std::vector<std::pair<NVGcolor, std::string>> colors = {
	{ color::GREEN, "Green" },
	{ color::MAGENTA, "Magenta" },
	{ color::BLUE, "Blue" },
	{ color::YELLOW, "Yellow" },
	{ color::CYAN, "Cyan" },
	{ color::WHITE, "White" },
	{ color::RED, "Red" },
	{ color::mult(color::WHITE, 0.45f), "Grey" }
};


template <uint8_t SNAPSHOTS = 8, uint8_t SETS = 8>
struct TransitPadModule : Module, TransitPadInterface, XyScreenModule<SNAPSHOTS>, XySeqModule<1> {
	enum ParamIds {
		ENUMS(SNAPSHOT_X_POS, SNAPSHOTS),
		ENUMS(SNAPSHOT_Y_POS, SNAPSHOTS),
		OUT_X_POS,
		OUT_Y_POS,
		ENUMS(SET_PARAM, SETS),
		NUM_PARAMS
	};
	enum InputIds {
		OUT_X_INPUT,
		OUT_Y_INPUT,
		OUT_SEQ_INPUT,
		OUT_SEQ_PH_INPUT,
		NUM_INPUTS
	};
	enum OutputIds {
		NUM_OUTPUTS
	};
	enum LightIds {
		ENUMS(SET_LIGHT, SETS),
		NUM_LIGHTS
	};

	typedef XyScreenModule<SNAPSHOTS> Sc;
	typedef XySeqModule<1> Seq;

	/** [Stored to JSON] */
	int panelTheme = 0;

	/** [Stored to JSON] */
	int snapshotsUsed = SNAPSHOTS;

	float dist[SNAPSHOTS];

	float inputInX[SNAPSHOTS];
	float inputInY[SNAPSHOTS];

	float outUiX, outInX;
	dsp::ExponentialFilter outXfilter;
	float outUiY, outInY;
	dsp::ExponentialFilter outYfilter;

	int currentSet = 0;
	std::vector<TransitPadSource> snapshots[SETS];
	NVGcolor setColor[SETS];

	ClockDividerEx buttonDivider;
	ClockDividerEx lightDivider;

	TransitPadModule() {
		panelTheme = pluginSettings.panelThemeDefault;
		config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);

		for (uint8_t s = 0; s < SETS; s++) {
			configSwitch(SET_PARAM + s, 0.0f, 1.0f, 0.0f, string::f("Snapshot-set #%i", s + 1));
		}

		configParam<XyScreenParamQuantity>(SNAPSHOT_X_POS + 0, 0.0f, 1.0f, 0.1f, "Snapshot A x-pos");
		configParam<XyScreenParamQuantity>(SNAPSHOT_Y_POS + 0, 0.0f, 1.0f, 0.1f, "Snapshot A y-pos");
		configParam<XyScreenParamQuantity>(SNAPSHOT_X_POS + 1, 0.0f, 1.0f, 0.9f, "Snapshot B x-pos");
		configParam<XyScreenParamQuantity>(SNAPSHOT_Y_POS + 1, 0.0f, 1.0f, 0.1f, "Snapshot B y-pos");	
		configParam<XyScreenParamQuantity>(SNAPSHOT_X_POS + 2, 0.0f, 1.0f, 0.9f, "Snapshot C x-pos");
		configParam<XyScreenParamQuantity>(SNAPSHOT_Y_POS + 2, 0.0f, 1.0f, 0.9f, "Snapshot C y-pos");
		configParam<XyScreenParamQuantity>(SNAPSHOT_X_POS + 3, 0.0f, 1.0f, 0.1f, "Snapshot D x-pos");
		configParam<XyScreenParamQuantity>(SNAPSHOT_Y_POS + 3, 0.0f, 1.0f, 0.9f, "Snapshot D y-pos");
		configParam<XyScreenParamQuantity>(SNAPSHOT_X_POS + 4, 0.0f, 1.0f, 0.3f, "Snapshot E x-pos");
		configParam<XyScreenParamQuantity>(SNAPSHOT_Y_POS + 4, 0.0f, 1.0f, 0.3f, "Snapshot E y-pos");
		configParam<XyScreenParamQuantity>(SNAPSHOT_X_POS + 5, 0.0f, 1.0f, 0.7f, "Snapshot F x-pos");
		configParam<XyScreenParamQuantity>(SNAPSHOT_Y_POS + 5, 0.0f, 1.0f, 0.3f, "Snapshot F y-pos");
		configParam<XyScreenParamQuantity>(SNAPSHOT_X_POS + 6, 0.0f, 1.0f, 0.7f, "Snapshot G x-pos");
		configParam<XyScreenParamQuantity>(SNAPSHOT_Y_POS + 6, 0.0f, 1.0f, 0.7f, "Snapshot G y-pos");
		configParam<XyScreenParamQuantity>(SNAPSHOT_X_POS + 7, 0.0f, 1.0f, 0.3f, "Snapshot H x-pos");
		configParam<XyScreenParamQuantity>(SNAPSHOT_Y_POS + 7, 0.0f, 1.0f, 0.7f, "Snapshot H y-pos");

		configInput(OUT_X_INPUT, "Mix x-pos");
		configInput(OUT_Y_INPUT, "Mix y-pos");
		configInput(OUT_SEQ_INPUT, "Mix sequence select");
		configInput(OUT_SEQ_PH_INPUT, "Mix sequence phase");
		configParam<XyScreenParamQuantity>(OUT_X_POS, 0.0f, 1.0f, 0.5f, "Mix x-pos");
		configParam<XyScreenParamQuantity>(OUT_Y_POS, 0.0f, 1.0f, 0.5f, "Mix y-pos");

		for (uint8_t s = 0; s < SETS; s++) {
			snapshots[s].resize(SNAPSHOTS);
		}
		onReset();
	}

	void onSampleRateChange(const Module::SampleRateChangeEvent& e) override {
		buttonDivider.setDivision(e.sampleRate / 1000.f);
		lightDivider.setDivision(e.sampleRate / 100.f);
	}

	void onExpanderChange(const Module::ExpanderChangeEvent& e) override {
		masterModule = nullptr;
		notifyExpanderListeners("Transit");
	}

	void onReset() override {
		Sc::scResetSelection();
		init();
		snapshotsUsed = 4;
		currentSet = 0;

		Sc::scReset();
		Seq::seqReset();
		Module::onReset();
	}

	void onRandomize() override {
		Sc::scRandomizeAmountAll();
		Sc::scRandomizeRadiusAll();
		Sc::scRandomizeXAll();
		Sc::scRandomizeYAll();
		Module::onRandomize();
	}

	void init() {
		scInitItems();
		Sc::scInit();
		Seq::seqInit();
	}

	// TransitPadInterface
	const std::vector<TransitPadSource>& getPadFactors() override {
		return snapshots[currentSet];
	}

	void process(const ProcessArgs& args) override {
		if (buttonDivider.process()) {
			for (uint8_t s = 0; s < SETS; s++) {
				if (params[SET_PARAM + s].getValue() > 0.5f && currentSet != s) {
					currentSet = s;
					break;
				}
			}
		}	

		for (uint8_t j = 0; j < snapshotsUsed; j++) {
			inputInX[j] = Sc::scGetXFiltered(j, args.sampleTime);
			inputInY[j] = Sc::scGetYFiltered(j, args.sampleTime);

			Sc::scSetRadiusFinal(j, Sc::scGetRadiusRaw(j, args.sampleTime));
			Sc::scSetAmountFinal(j, Sc::scGetAmountFiltered(j, args.sampleTime));

			float x = inputInX[j];
			x = clamp(x, 0.f, 1.f);
			params[SNAPSHOT_X_POS + j].setValue(x);

			float y = inputInY[j];
			y = clamp(y, 0.f, 1.f);
			params[SNAPSHOT_Y_POS + j].setValue(y);
		}

		XyScreenParamQuantity* px = reinterpret_cast<XyScreenParamQuantity*>(paramQuantities[OUT_X_POS]);
		outInX = px->hasHandle ? px->getParam()->getValue() : outXfilter.process(args.sampleTime, outUiX);
		XyScreenParamQuantity* py = reinterpret_cast<XyScreenParamQuantity*>(paramQuantities[OUT_Y_POS]);
		outInY = py->hasHandle ? py->getParam()->getValue() : outYfilter.process(args.sampleTime, outUiY);

		if (inputs[OUT_SEQ_INPUT].isConnected()) {
			Seq::seqProcess(inputs[OUT_SEQ_INPUT], 0);
		}

		bool setX = false, setY = false;

		if (inputs[OUT_SEQ_PH_INPUT].isConnected()) {
			float v = clamp(inputs[OUT_SEQ_PH_INPUT].getVoltage() / 10.f, 0.f, 1.f);
			Vec d = Seq::seqValue(0, v);
			params[OUT_X_POS].setValue(d.x);
			setX = true;
			params[OUT_Y_POS].setValue(d.y);
			setY = true;
		}

		if (!setX && inputs[OUT_X_INPUT].isConnected()) {
			float x = inputs[OUT_X_INPUT].getVoltage() / 10.f;
			x += 0.5f;
			x = clamp(x, 0.f, 1.f);
			params[OUT_X_POS].setValue(x);
			setX = true;
		} 

		if (!setY && inputs[OUT_Y_INPUT].isConnected()) {
			float y = inputs[OUT_Y_INPUT].getVoltage() / 10.f;
			y += 0.5f;
			y = clamp(y, 0.f, 1.f);
			params[OUT_Y_POS].setValue(y);
			setY = true;
		}

		if (!setX) {
			params[OUT_X_POS].setValue(outInX);
		}
		if (!setY) {
			params[OUT_Y_POS].setValue(outInY);
		}

		float outX = params[OUT_X_POS].getValue();
		float outY = params[OUT_Y_POS].getValue();
		Vec outVec = Vec(outX, outY);

		for (int j = 0; j < snapshotsUsed; j++) {
			float inX = params[SNAPSHOT_X_POS + j].getValue();
			float inY = params[SNAPSHOT_Y_POS + j].getValue();

			Vec inVec = Vec(inX, inY);
			dist[j] = inVec.minus(outVec).norm();

			float r = Sc::scGetRadiusFinal(j);
			if (dist[j] < r) {
				snapshots[currentSet][j].weight = std::min(1.0f, (r - dist[j]) / r * 1.1f);
			}
			else {
				snapshots[currentSet][j].weight = 0.f;
			}
		}

		if (lightDivider.process()) {
			for (uint8_t s = 0; s < SETS; s++) {
				lights[SET_LIGHT + s].setBrightness(currentSet == s ? 1.0f : 0.0f);
			}
		}
	}

	size_t getSetCount() {
		return SETS;
	}

	bool seqPortUsed(int port) override {
		return port + 1 > 1;
	}

	void scInitItems() override {		
		scSetItemImmediate(1, 0, paramQuantities[OUT_X_POS]->getDefaultValue(), paramQuantities[OUT_Y_POS]->getDefaultValue());
		outXfilter.setTau(0.05f);
		outYfilter.setTau(0.05f);
		for (uint8_t s = 0; s < SETS; s++) {
			for (uint8_t i = 0; i < SNAPSHOTS; i++) {
				dist[i] = std::numeric_limits<float>::infinity();
				snapshots[s][i].id = i < 4 ? i : -1;
				snapshots[s][i].weight = 0.f;
			}
			setColor[s] = colors[s % colors.size()].first;
		}
	}

	inline uint8_t scGetItemCount(uint8_t type) override {
		return type == 0 ? SNAPSHOTS : 1;
	}

	inline uint8_t scGetItemCountActive(uint8_t type) override {
		return type == 0 ? snapshotsUsed : 1;
	}

	engine::ParamQuantity* scGetPqX(uint8_t type, uint8_t id) override {
		if (type == 0)
			return paramQuantities[SNAPSHOT_X_POS + id];
		else
			return paramQuantities[OUT_X_POS];
	}

	engine::ParamQuantity* scGetPqY(uint8_t type, uint8_t id) override {
		if (type == 0)
			return paramQuantities[SNAPSHOT_Y_POS + id];
		else
			return paramQuantities[OUT_Y_POS];
	}

	inline void scSetItemFiltered(uint8_t type, uint8_t id, float x, float y) override {
		if (type == 1) {
			outUiX = x;
			outUiY = y;
		}
	}

	inline void scSetItemImmediate(uint8_t type, uint8_t id, float x, float y) override {
		if (type == 1) {
			paramQuantities[OUT_X_POS]->getParam()->setValue(x);
			outXfilter.out = outUiX = x;
			paramQuantities[OUT_Y_POS]->getParam()->setValue(y);
			outYfilter.out = outUiY = y;
		}
	}

	inline float scGetDistance(uint8_t typeSource, uint8_t idSource, uint8_t typeDest, uint8_t idDest) override {
		return dist[idDest];
	}

	inline float scGetRadiusDefault(uint8_t type) override {
		return 1.f;
	}

	virtual inline NVGcolor scGetColor(uint8_t type, uint8_t id) override {
		return type == 0 ? setColor[currentSet] : color::WHITE;
	}

	std::string getItemLabel(uint8_t s, uint8_t id) {
		if (masterModule == nullptr)
			return "<No TRANSIT module>";
		if (snapshots[s][id].id >= 0) {
			std::string custom = masterModule->getSlotLabel(snapshots[s][id].id);
			if (custom != "")
				return string::f("Snapshot #%i: %s", snapshots[s][id].id + 1, custom.c_str());
			else
				return string::f("Snapshot #%i", snapshots[s][id].id + 1);
		}
		else {
			return "No snapshot";
		}
	}

	json_t* dataToJson() override {
		json_t* rootJ = json_object();
		json_object_set_new(rootJ, "panelTheme", json_integer(panelTheme));
		json_object_set_new(rootJ, "snapshotsUsed", json_integer(snapshotsUsed));

		json_t* setsJ = json_array();
		for (uint8_t s = 0; s < SETS; s++) {
			json_t* setJ = json_object();
			json_t* snapshotsJ = json_array();
			for (uint8_t i = 0; i < SNAPSHOTS; i++) {
				json_t* snapshotJ = json_object();
				json_object_set_new(snapshotJ, "id", json_integer(snapshots[s][i].id));
				Sc::dataToJson(snapshotJ, 0, i);
				json_array_append_new(snapshotsJ, snapshotJ);
			}
			json_object_set_new(setJ, "snapshots", snapshotsJ);
			json_object_set_new(setJ, "color", json_string(color::toHexString(setColor[s]).c_str()));
			json_array_append_new(setsJ, setJ);
		}
		json_object_set_new(rootJ, "sets", setsJ);

		json_t* outputJ = json_object();
		Sc::dataToJson(outputJ, 1, 0);
		Seq::dataToJson(outputJ, 0);
		json_object_set_new(rootJ, "output", outputJ);

		return rootJ;
	}

	void dataFromJson(json_t* rootJ) override {
		panelTheme = json_integer_value(json_object_get(rootJ, "panelTheme"));
	
		int su = json_integer_value(json_object_get(rootJ, "snapshotsUsed"));
		snapshotsUsed = std::max(0, std::min(su, (int)SNAPSHOTS));

		json_t* setsJ = json_object_get(rootJ, "sets");
		for (size_t s = 0; s < json_array_size(setsJ); ++s) {
			json_t* setJ = json_array_get(setsJ, s);
			json_t* snapshotsJ = json_object_get(setJ, "snapshots");
			if (json_is_array(snapshotsJ)) {
				size_t n = json_array_size(snapshotsJ);
				size_t maxn = std::min((size_t)SNAPSHOTS, n);
				for (size_t i = 0; i < maxn; ++i) {
					json_t* snapshotJ = json_array_get(snapshotsJ, i);
					snapshots[s][i].id = json_integer_value(json_object_get(snapshotJ, "id"));
					Sc::dataFromJson(snapshotJ, 0, i);
				}
			}
			setColor[s] = color::fromHexString(json_string_value(json_object_get(setJ, "color")));
		}

		json_t* outputJ = json_object_get(rootJ, "output");
		Sc::dataFromJson(outputJ, 1, 0);
		Seq::dataFromJson(outputJ, 0);
	}
};


template <typename MODULE>
struct TransitPadSnapshotDragWidget : XyScreenDragWidget<MODULE> {
	typedef XyScreenDragWidget<MODULE> AW;

	char getItemChar() override {
		return 'A' + AW::id;
	}

 	std::string getItemName() override {
		return AW::module->getItemLabel(AW::module->currentSet, AW::id);
	}

	void prependContextMenu(Menu* menu) override {
		menu->addChild(createMenuItem("Bind snapshot", "", [=]() {
			AW::module->snapshots[AW::module->currentSet][AW::id].id = AW::module->masterModule->getSelectedSlot();
		}, AW::module->masterModule == nullptr));
		menu->addChild(createMenuItem("Unbind snapshot", "", [=]() {
			AW::module->snapshots[AW::module->currentSet][AW::id].id = -1;
		}));
		menu->addChild(new MenuSeparator());
		menu->addChild(createMenuLabel("Current set"));
		menu->addChild(Rack::createColorSubmenuItem("Color", &AW::module->setColor[AW::module->currentSet], colors));
		menu->addChild(new MenuSeparator());
		menu->addChild(createMenuLabel("All sets"));
	}
};


template <typename MODULE>
struct TransitPadOutDragWidget : XyScreenDragWidget<MODULE> {
	typedef XyScreenDragWidget<MODULE> B;

 	std::string getItemName() override {
		return "Mix";
	}

	char getItemChar() override {
		return '+';
	}

	void appendContextMenu(Menu* menu) override {
		menu->addChild(new MenuSeparator());
		menu->addChild(createMenuLabel("Motion-Sequence"));
		menu->addChild(new XySeqSlotMenuItem<MODULE>(B::module, B::id));
		menu->addChild(new XySeqInterpolateMenuItem<MODULE>(B::module, B::id));
		menu->addChild(new XySeqTriggerMenuItem<MODULE>(B::module, B::id));
	}
};

template <typename MODULE>
struct TransitPadXyScreenWidget : XyScreenWidget<MODULE> {
	TransitPadXyScreenWidget(MODULE* module, int inParamIdX, int inParamIdY, int mixParamIdX, int mixParamIdY) : XyScreenWidget<MODULE>(module) {
		uint8_t t0 = module ? module->scGetItemCount(0) : 4;
		this->template createDragWidgets<TransitPadSnapshotDragWidget<MODULE>>(module, 0, t0);
		uint8_t t1 = module ? module->scGetItemCount(1) : 1;
		this->template createDragWidgets<TransitPadOutDragWidget<MODULE>>(module, 1, t1);
	}

	void step() override {
		if (this->module) {
			// Preview interpolated automation line if mixport is selected
			this->module->seqPreview = -1;
			for (uint8_t i = 0; i < this->module->scGetItemCountActive(1); i++) {
				if (this->module->scIsSelected(1, i))
					this->module->seqPreview = i;
			}
		}
		XyScreenWidget<MODULE>::step();
	}

	void appendContextMenu(Menu* menu) override {
		using StoermelderPackOne::Rack::createValuePtrMenuItem;
		menu->addChild(new MenuSeparator());
		menu->addChild(createSubmenuItem("Number of snapshots", string::f("%i", this->module->snapshotsUsed),
			[=](Menu* menu) {
				for (int i = 0; i < this->module->scGetItemCount(0); i++) {
					menu->addChild(createValuePtrMenuItem(string::f("%i", i + 1), &this->module->snapshotsUsed, i + 1));
				}
			}
		));
	}
};


template <typename MODULE>
struct TransitPadXySeqLedDisplay : XySeqLedDisplay<MODULE> {	
	std::string getPortName() override {
		return "Mix";
	}
};


template <typename MODULE>
struct TransitPadSetButton : VCVButton {
	MODULE* module;
	size_t setIndex;
	virtual void appendContextMenu(ui::Menu* menu) override {
		menu->addChild(new MenuSeparator());
		menu->addChild(Rack::createColorSubmenuItem("Color", &module->setColor[setIndex], colors));
		menu->addChild(new MenuSeparator());
		for (size_t i = 0; i < module->scGetItemCountActive(0); i++) {
			menu->addChild(createMenuLabel(module->getItemLabel(setIndex, i))); 
		}
	}
};

struct TransitPadWidget : ThemedModuleWidget<TransitPadModule<>> {
	typedef TransitPadModule<> MODULE;

	TransitPadWidget(MODULE* module) : ThemedModuleWidget<MODULE>(module, "TransitPad") {
		setModule(module);

		for (size_t s = 0; s < module->getSetCount(); s++) {
			TransitPadSetButton<MODULE>* button = createParamCentered<TransitPadSetButton<MODULE>>(Vec(17.6f + s * 27.1f, 46.4f), module, MODULE::SET_PARAM + s);
			button->module = module;
			button->setIndex = s;
			addChild(button);
			addChild(createLightCentered<MediumSimpleLight<WhiteLight>>(Vec(17.6f + s * 27.1f, 46.4f), module, MODULE::SET_LIGHT + s));
		}

		addChild(createWidget<StoermelderBlackScrew>(Vec(RACK_GRID_WIDTH, 0)));
		addChild(createWidget<StoermelderBlackScrew>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, 0)));
		addChild(createWidget<StoermelderBlackScrew>(Vec(RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));
		addChild(createWidget<StoermelderBlackScrew>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));

		addInput(createInputCentered<StoermelderPort>(Vec(21.0f, 327.0f), module, MODULE::OUT_SEQ_INPUT));
		addInput(createInputCentered<StoermelderPort>(Vec(57.5f, 327.0f), module, MODULE::OUT_SEQ_PH_INPUT));
		addInput(createInputCentered<StoermelderPort>(Vec(167.7f, 327.0f), module, MODULE::OUT_X_INPUT));
		addInput(createInputCentered<StoermelderPort>(Vec(204.3f, 327.0f), module, MODULE::OUT_Y_INPUT));

		addParam(createParamCentered<XyScreenDummyMapButton>(Vec(175.0f, 309.4f), module, MODULE::OUT_X_POS));
		addParam(createParamCentered<XyScreenDummyMapButton>(Vec(197.0f, 309.4f), module, MODULE::OUT_Y_POS));

		TransitPadXyScreenWidget<MODULE>* screenWidget = new TransitPadXyScreenWidget<MODULE>(module, MODULE::SNAPSHOT_X_POS, MODULE::SNAPSHOT_Y_POS, MODULE::OUT_X_POS, MODULE::OUT_Y_POS);
		screenWidget->box.pos = Vec(3.f, 63.3f + 3.f);
		screenWidget->box.size = Vec(225.f - 6.f, 225.f - 6.f);
		addChild(screenWidget);

		XySeqEditWidget<MODULE>* seqEditWidget = new XySeqEditWidget<MODULE>(module, MODULE::OUT_X_POS, MODULE::OUT_Y_POS);
		seqEditWidget->box.pos = screenWidget->box.pos;
		seqEditWidget->box.size = screenWidget->box.size;
		addChild(seqEditWidget);

		TransitPadXySeqLedDisplay<MODULE>* seqDisplay1 = createWidget<TransitPadXySeqLedDisplay<MODULE>>(Vec(77.9f, 329.8f));
		seqDisplay1->box.size = Vec(24.6f, 13.2f);
		seqDisplay1->module = module;
		seqDisplay1->id = 0;
		addChild(seqDisplay1);
	}
};

} // namespace Transit
} // namespace StoermelderPackOne

Model* modelTransitPad = createModel<StoermelderPackOne::Transit::TransitPadModule<8>, StoermelderPackOne::Transit::TransitPadWidget>("TransitPad");