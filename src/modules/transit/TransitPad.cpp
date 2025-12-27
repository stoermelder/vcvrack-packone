#include "../../plugin.hpp"
#include "../../components/XyScreenWidget.hpp"
#include "../../components/XySeqWidget.hpp"
#include "TransitBase.hpp"

namespace StoermelderPackOne {
namespace Transit {

template <uint8_t SNAPSHOTS = 8>
struct TransitPadModule : Module, TransitPadInterface, XyScreenModule<SNAPSHOTS>, XySeqModule<1> {
	enum ParamIds {
		ENUMS(SNAPSHOT_X_POS, SNAPSHOTS),
		ENUMS(SNAPSHOT_Y_POS, SNAPSHOTS),
		OUT_X_POS,
		OUT_Y_POS,
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

	std::vector<TransitPadSource> snapshots;

	ClockDividerEx lightDivider;

	TransitPadModule() {
		panelTheme = pluginSettings.panelThemeDefault;
		config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);

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

		snapshots.resize(SNAPSHOTS);
		onReset();
	}

	void onExpanderChange(const Module::ExpanderChangeEvent& e) override {
		notifyExpanderListeners("Transit");
	}

	void onReset() override {
		Sc::scResetSelection();
		init();
		snapshotsUsed = 4;

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
		return snapshots;
	}

	void process(const ProcessArgs& args) override {
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
				snapshots[j].weight = std::min(1.0f, (r - dist[j]) / r * 1.1f);
			}
			else {
				snapshots[j].weight = 0.f;
			}
		}
	}

	bool seqPortUsed(int port) override {
		return port + 1 > 1;
	}

	void scInitItems() override {		
		scSetItemImmediate(1, 0, paramQuantities[OUT_X_POS]->getDefaultValue(), paramQuantities[OUT_Y_POS]->getDefaultValue());
		outXfilter.setTau(0.05f);
		outYfilter.setTau(0.05f);
		for (uint8_t i = 0; i < SNAPSHOTS; i++) {
			snapshots[i].id = i < 4 ? i : -1;
			snapshots[i].color = TransitPadSource::getDefaultColor();
			snapshots[i].weight = 0.f;
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
		return type == 0 ? snapshots[id].color : color::WHITE;
	}

	json_t* dataToJson() override {
		json_t* rootJ = json_object();
		json_object_set_new(rootJ, "panelTheme", json_integer(panelTheme));
		json_object_set_new(rootJ, "snapshotsUsed", json_integer(snapshotsUsed));

		json_t* snapshotsJ = json_array();
		for (uint8_t i = 0; i < SNAPSHOTS; i++) {
			json_t* snapshotJ = json_object();
			json_object_set_new(snapshotJ, "id", json_integer(snapshots[i].id));
        	json_object_set_new(snapshotJ, "color", json_string(color::toHexString(snapshots[i].color).c_str()));
			Sc::dataToJson(snapshotJ, 0, i);
			json_array_append_new(snapshotsJ, snapshotJ);
		}
		json_object_set_new(rootJ, "snapshots", snapshotsJ);

		json_t* outputJ = json_object();
		Sc::dataToJson(outputJ, 1, 0);
		Seq::dataToJson(outputJ, 0);
		json_object_set_new(rootJ, "output", outputJ);

		return rootJ;
	}

	void dataFromJson(json_t* rootJ) override {
		panelTheme = json_integer_value(json_object_get(rootJ, "panelTheme"));
		snapshotsUsed = json_integer_value(json_object_get(rootJ, "snapshotsUsed"));

		json_t* snapshotsJ = json_object_get(rootJ, "snapshots");
		json_t* snapshotJ;
		uint8_t inputIndex;
		json_array_foreach(snapshotsJ, inputIndex, snapshotJ) {
			snapshots[inputIndex].id = json_integer_value(json_object_get(snapshotJ, "id"));
			snapshots[inputIndex].color = color::fromHexString(json_string_value(json_object_get(snapshotJ, "color")));
			Sc::dataFromJson(snapshotJ, 0, inputIndex);
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
		if (AW::module->snapshots[AW::id].id >= 0) {
			std::string custom = *AW::module->masterModule->expSlotLabel(AW::module->snapshots[AW::id].id);
			if (custom != "")
				return string::f("Snapshot #%i: %s", AW::module->snapshots[AW::id].id + 1, custom.c_str());
			else
				return string::f("Snapshot #%i", AW::module->snapshots[AW::id].id + 1);
		}
		else
			return "No snapshot";
	}

	void appendContextMenu(Menu* menu) override {
		menu->addChild(new MenuSeparator());
		menu->addChild(createMenuItem("Bind snapshot", "", [=]() {
			AW::module->snapshots[AW::id].id = AW::module->masterModule->transitSlotSelected();
		}));
		menu->addChild(createMenuItem("Unbind snapshot", "", [=]() {
			AW::module->snapshots[AW::id].id = -1;
		}));
		menu->addChild(new MenuSeparator());
		menu->addChild(Rack::createColorSubmenuItem("Color", &AW::module->snapshots[AW::id].color, {
			{ color::GREEN, "Green" },
			{ color::YELLOW, "Yellow" },
			{ color::RED, "Red" },
			{ color::CYAN, "Cyan" },
			{ color::MAGENTA, "Magenta" },
			{ color::BLUE, "Blue" },
			{ color::WHITE, "White" },
		}));
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
		menu->addChild(createSubmenuItem("Number of IN-ports", string::f("%i", this->module->snapshotsUsed),
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


struct TransitPadWidget : ThemedModuleWidget<TransitPadModule<>> {
	typedef TransitPadModule<> MODULE;

	TransitPadWidget(MODULE* module) : ThemedModuleWidget<MODULE>(module, "TransitPad") {
		setModule(module);

		addChild(createWidget<StoermelderBlackScrew>(Vec(RACK_GRID_WIDTH, 0)));
		addChild(createWidget<StoermelderBlackScrew>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, 0)));
		addChild(createWidget<StoermelderBlackScrew>(Vec(RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));
		addChild(createWidget<StoermelderBlackScrew>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));

		addInput(createInputCentered<StoermelderPort>(Vec(22.9f, 327.0f), module, MODULE::OUT_SEQ_INPUT));
		addInput(createInputCentered<StoermelderPort>(Vec(135.0f, 327.0f), module, MODULE::OUT_SEQ_PH_INPUT));
		addInput(createInputCentered<StoermelderPort>(Vec(97.0f, 327.0f), module, MODULE::OUT_X_INPUT));
		addInput(createInputCentered<StoermelderPort>(Vec(173.0f, 327.0f), module, MODULE::OUT_Y_INPUT));

		addParam(createParamCentered<XyScreenDummyMapButton>(Vec(104.3f, 309.4f), module, MODULE::OUT_X_POS));
		addParam(createParamCentered<XyScreenDummyMapButton>(Vec(165.7f, 309.4f), module, MODULE::OUT_Y_POS));

		TransitPadXyScreenWidget<MODULE>* screenWidget = new TransitPadXyScreenWidget<MODULE>(module, MODULE::SNAPSHOT_X_POS, MODULE::SNAPSHOT_Y_POS, MODULE::OUT_X_POS, MODULE::OUT_Y_POS);
		screenWidget->box.pos = Vec(12.3f, 41.2f);
		screenWidget->box.size = Vec(245.4f, 245.4f);
		addChild(screenWidget);

		XySeqEditWidget<MODULE>* seqEditWidget = new XySeqEditWidget<MODULE>(module, MODULE::OUT_X_POS, MODULE::OUT_Y_POS);
		seqEditWidget->box.pos = screenWidget->box.pos;
		seqEditWidget->box.size = screenWidget->box.size;
		addChild(seqEditWidget);

		TransitPadXySeqLedDisplay<MODULE>* seqDisplay1 = createWidgetCentered<TransitPadXySeqLedDisplay<MODULE>>(Vec(55.2f, 309.7f));
		seqDisplay1->module = module;
		seqDisplay1->id = 0;
		addChild(seqDisplay1);
	}
};

} // namespace Transit
} // namespace StoermelderPackOne

Model* modelTransitPad = createModel<StoermelderPackOne::Transit::TransitPadModule<8>, StoermelderPackOne::Transit::TransitPadWidget>("TransitPad");