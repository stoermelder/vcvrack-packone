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


enum class SETCVMODE {
	OFF = -1,
	TRIG_FWD = 0,
	VOLT = 1,
	C4 = 2
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
		SET_CV_INPUT,
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

	/** [Stored to JSON] */
	int currentSet = 0;
	/** [Stored to JSON] */
	SETCVMODE setCvMode = SETCVMODE::TRIG_FWD;
	dsp::SchmittTrigger setCvTrigger;
	std::vector<TransitPadSource> snapshots[SETS];
	NVGcolor setColor[SETS];

	ClockDividerEx buttonDivider;
	ClockDividerEx lightDivider;

	// Index of the pad point the user is currently hovering over, or -1.
	// Set by TransitPadSnapshotDragWidget::onEnter / onLeave.
	int vizHoveredId = -1;
	bool vizMode = false;

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
		configInput(SET_CV_INPUT, "Snapshot-set select CV");
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
		if (inputs[SET_CV_INPUT].isConnected()) {
			switch (setCvMode) {
				case SETCVMODE::OFF:
					break;
				case SETCVMODE::TRIG_FWD:
					if (setCvTrigger.process(inputs[SET_CV_INPUT].getVoltage())) {
						currentSet = (currentSet + 1) % SETS;
					}
					break;
				case SETCVMODE::VOLT: {
					float v = clamp(inputs[SET_CV_INPUT].getVoltage(), 0.f, 10.f);
					int s = int(v / 10.f * SETS);
					currentSet = std::min(s, (int)SETS - 1);
					break;
				}
				case SETCVMODE::C4:
					currentSet = clamp((int)std::round(inputs[SET_CV_INPUT].getVoltage() * 12.f), 0, (int)SETS - 1);
					break;
			}
		}
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
		json_object_set_new(rootJ, "setCvMode", json_integer((int)setCvMode));
		json_object_set_new(rootJ, "currentSet", json_integer(currentSet));

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

		json_t* setCvModeJ = json_object_get(rootJ, "setCvMode");
		if (setCvModeJ) setCvMode = (SETCVMODE)json_integer_value(setCvModeJ);

		json_t* currentSetJ = json_object_get(rootJ, "currentSet");
		if (currentSetJ) currentSet = std::max(0, std::min((int)json_integer_value(currentSetJ), (int)SETS - 1));

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
	ui::Tooltip* tooltip = NULL;
	// True while a drag from a TransitLedButton is hovering over this node,
	// so the draw code can highlight it as a valid drop target.
	bool dropArmed = false;

	~TransitPadSnapshotDragWidget() {
		if (tooltip) {
			APP->scene->removeChild(tooltip);
			delete tooltip;
		}
	}

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

	void onEnter(const event::Enter& e) override {
		if (!AW::module->scIsActive(AW::type, AW::id)) return;
		this->module->vizHoveredId = this->id;
		if (settings::tooltips && !tooltip) {
			tooltip = new ui::Tooltip;
			tooltip->text = getItemName();
			APP->scene->addChild(tooltip);
		}
		AW::onEnter(e);
	}

	void onLeave(const event::Leave& e) override {
		if (!AW::module->scIsActive(AW::type, AW::id)) return;
		if (this->module->vizHoveredId == this->id) {
			this->module->vizHoveredId = -1;
		}
		if (tooltip) {
			APP->scene->removeChild(tooltip);
			delete tooltip;
			tooltip = NULL;
		}
		AW::onLeave(e);
	}

	void onDragEnter(const event::DragEnter& e) override {
		// Highlight as a valid drop target when a TransitLedButton is being dragged.
		if (dynamic_cast<TransitSnapshotButton*>(e.origin) != nullptr) {
			dropArmed = true;
			e.consume(this);
		}
		AW::onDragEnter(e);
	}

	void onDragLeave(const event::DragLeave& e) override {
		dropArmed = false;
		AW::onDragLeave(e);
	}

	void onDragDrop(const event::DragDrop& e) override {
		if (!AW::module->scIsActive(AW::type, AW::id)) return;
		// Bind the pad point to the slot of the dropped TransitLedButton.
		TransitSnapshotButton* src = dynamic_cast<TransitSnapshotButton*>(e.origin);
		if (src && e.button == GLFW_MOUSE_BUTTON_LEFT) {
			int slot = src->getSlotIndex();
			if (slot >= 0) {
				this->module->snapshots[this->module->currentSet][this->id].id = slot;
			}
			dropArmed = false;
			e.consume(this);
		}
		AW::onDragDrop(e);
	}

	void drawLayer(const Widget::DrawArgs& args, int layer) override {
		AW::drawLayer(args, layer);
		if (!AW::module->scIsActive(AW::type, AW::id)) return;
		if (layer != 1 || !dropArmed) return;

		// Bright halo around the node while a snapshot button is being dragged over it,
		// so the user can see at a glance that this is a valid drop target.
		Vec c = Vec(this->box.size.x / 2.f, this->box.size.y / 2.f);
		float r = this->box.size.x / 2.f;

		// Bright outer ring.
		nvgBeginPath(args.vg);
		nvgCircle(args.vg, c.x, c.y, r + 1.5f);
		nvgStrokeColor(args.vg, nvgRGBAf(1.f, 1.f, 1.f, 0.95f));
		nvgStrokeWidth(args.vg, 1.5f);
		nvgStroke(args.vg);
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
		menu->addChild(createBoolPtrMenuItem("Visualize", "Space", &this->module->vizMode));
		menu->addChild(createSubmenuItem("Number of snapshots", string::f("%i", this->module->snapshotsUsed),
			[=](Menu* menu) {
				for (int i = 0; i < this->module->scGetItemCount(0); i++) {
					menu->addChild(createValuePtrMenuItem(string::f("%i", i + 1), &this->module->snapshotsUsed, i + 1));
				}
			}
		));
		menu->addChild(createSubmenuItem("Snapshot-set CV mode", "",
			[=](Menu* menu) {
				menu->addChild(createValuePtrMenuItem("Off", &this->module->setCvMode, SETCVMODE::OFF));
				menu->addChild(new MenuSeparator);
				menu->addChild(createValuePtrMenuItem("Trigger forward", &this->module->setCvMode, SETCVMODE::TRIG_FWD));
				menu->addChild(createValuePtrMenuItem("0..10V", &this->module->setCvMode, SETCVMODE::VOLT));
				menu->addChild(createValuePtrMenuItem("C4", &this->module->setCvMode, SETCVMODE::C4));
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


// Overlay widget added directly to APP->scene->rack — drawn in rack coordinates.
// Activated by the space key; draws a spline from each pad point to the snapshot
// button on the host TRANSIT (or +T expander) it is bound to.
struct TransitPadVizOverlay : TransparentWidget {
	TransitPadModule<>* module = nullptr;
	// Non-owning pointer to the host widget (for absolute position).
	Widget* hostWidget = nullptr;

	void step() override {
		// Track parent size so NVG scissor doesn't clip our drawings.
		if (parent) { box.pos = Vec(0.f, 0.f); box.size = parent->box.size; }
		TransparentWidget::step();
	}

	// Resolves the absolute rack-space center of a TRANSIT/-T snapshot button.
	// Returns Vec() if the slot's owner module or its button widget cannot be found.
	Vec getButtonPos(int slotIndex) {
		if (!module || !module->masterModule) return Vec();
		Module* ownerModule;
		int localIndex;
		if (!module->masterModule->getSlotOwner(slotIndex, ownerModule, localIndex)) return Vec();
		ModuleWidget* ownerMw = APP->scene->rack->getModule(ownerModule->id);
		if (!ownerMw) return Vec();
		// TransitSnapshotButton is used by both TRANSIT and +T to render the per-slot
		// LED button. Walk the owner's param widgets, find the one whose
		// TransitLedButton carries the matching local id, and use its center.
		for (ParamWidget* pw : ownerMw->getParams()) {
			auto* btn = dynamic_cast<TransitSnapshotButton*>(pw);
			if (btn && btn->id == localIndex) {
				return ownerMw->box.pos.plus(pw->box.getCenter());
			}
		}
		return Vec();
	}

	// Draws a Bezier curve between two points with a glow + core pass.
	void drawSpline(NVGcontext* vg, Vec a, Vec b, NVGcolor col, bool highlighted = false, Vec aDir = Vec(0.f, 0.f)) {
		float dist = a.minus(b).norm();
		Vec dir  = b.minus(a).normalize();
		Vec perp = Vec(-dir.y, dir.x);  // 90° CCW — always lateral to the connection
		float tang  = dist * 0.35f;
		float bulge = dist * 0.45f;
		Vec cp1 = (aDir.norm() > 0.001f)
			? a.plus(aDir.normalize().mult(tang))
			: a.plus(dir.mult(tang)).plus(perp.mult(bulge));
		Vec cp2 = b.minus(dir.mult(tang)).plus(perp.mult(bulge));

		nvgBeginPath(vg);
		nvgMoveTo(vg, a.x, a.y);
		nvgBezierTo(vg, cp1.x, cp1.y, cp2.x, cp2.y, b.x, b.y);
		nvgLineCap(vg, NVG_ROUND);
		// Glow pass
		nvgStrokeColor(vg, nvgRGBAf(col.r, col.g, col.b, highlighted ? 0.45f : 0.25f));
		nvgStrokeWidth(vg, highlighted ? 12.f : 6.f);
		nvgStroke(vg);
		// Core pass
		nvgStrokeColor(vg, nvgRGBAf(col.r, col.g, col.b, 1.f));
		nvgStrokeWidth(vg, highlighted ? 3.f : 1.5f);
		nvgStroke(vg);
	}

	void drawLayer(const DrawArgs& args, int layer) override {
		if (layer != 1 || !visible || !module || !hostWidget) return;
		NVGcontext* vg = args.vg;

		Vec origin = hostWidget->box.pos;
		uint8_t currentSet = module->currentSet;
		bool anyHover = (module->vizHoveredId >= 0);

		// The screen widget is at (3, 66.3) with size 219x219 inside the host widget.
		// Each pad point is a 20x20 square centered in that area.
		const float screenX = 3.f;
		const float screenY = 66.3f;
		const float screenSize = 225.f - 6.f;
		const float pointSize = 20.f;

		// Two passes when hovering: dim unrelated connectors first, then draw the
		// hovered connector at full opacity on top.
		auto drawConnection = [&](uint8_t i, bool highlighted, bool dimmed) {
			const auto& src = module->snapshots[currentSet][i];
			if (src.id < 0) return;

			Vec buttonPos = getButtonPos(src.id);
			if (buttonPos == Vec()) return;

			// Pad-point center in module-local coords.
			float x = module->params[TransitPadModule<>::SNAPSHOT_X_POS + i].getValue();
			float y = module->params[TransitPadModule<>::SNAPSHOT_Y_POS + i].getValue();
			Vec padPos = origin.plus(Vec(
				screenX + x * (screenSize - pointSize) + pointSize * 0.5f,
				screenY + y * (screenSize - pointSize) + pointSize * 0.5f
			));

			NVGcolor col = module->setColor[currentSet];
			// Start the spline at the edge of the XY drag circle (radius 10px), not its center.
			Vec dir = buttonPos.minus(padPos).normalize();
			Vec padEdge = padPos.plus(dir.mult(10.f));

			if (dimmed) {
				// Cheap dim: lower alpha on the same glow + core passes.
				nvgSave(vg);
				nvgGlobalAlpha(vg, 0.18f);
				drawSpline(vg, padEdge, buttonPos, col, false, dir);
				nvgRestore(vg);
				nvgBeginPath(vg);
				nvgCircle(vg, buttonPos.x, buttonPos.y, 2.5f);
				nvgFillColor(vg, nvgRGBAf(col.r, col.g, col.b, 0.4f));
				nvgFill(vg);
			}
			else {
				drawSpline(vg, padEdge, buttonPos, col, highlighted, dir);

				// End-point dot at the button.
				nvgBeginPath(vg);
				nvgCircle(vg, buttonPos.x, buttonPos.y, highlighted ? 4.5f : 3.5f);
				nvgFillColor(vg, nvgRGBAf(col.r, col.g, col.b, highlighted ? 1.f : 0.9f));
				nvgFill(vg);
				nvgStrokeColor(vg, nvgRGBAf(1.f, 1.f, 1.f, highlighted ? 0.9f : 0.6f));
				nvgStrokeWidth(vg, highlighted ? 1.2f : 0.8f);
				nvgStroke(vg);
			}
		};

		if (anyHover) {
			// Pass 1: draw all non-hovered connectors dimmed.
			for (uint8_t i = 0; i < module->snapshotsUsed; i++) {
				if (i == (uint8_t)module->vizHoveredId) continue;
				drawConnection(i, false, true);
			}
			// Pass 2: draw the hovered connector highlighted.
			drawConnection((uint8_t)module->vizHoveredId, true, false);
		}
		else {
			// No hover — draw every connector at normal opacity.
			for (uint8_t i = 0; i < module->snapshotsUsed; i++) {
				drawConnection(i, false, false);
			}
		}
	}
};


struct TransitPadWidget : ThemedModuleWidget<TransitPadModule<>> {
	typedef TransitPadModule<> MODULE;
	TransitPadVizOverlay* vizOverlay = nullptr;

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
		addInput(createInputCentered<StoermelderPort>(Vec(112.5f, 327.0f), module, MODULE::OUT_SEQ_PH_INPUT));
		addInput(createInputCentered<StoermelderPort>(Vec(84.5f, 327.0f), module, MODULE::OUT_X_INPUT));
		addInput(createInputCentered<StoermelderPort>(Vec(140.5f, 327.0f), module, MODULE::OUT_Y_INPUT));
		addInput(createInputCentered<StoermelderPort>(Vec(204.3f, 327.0f), module, MODULE::SET_CV_INPUT));

		addParam(createParamCentered<XyScreenDummyMapButton>(Vec(77.6f, 309.8f), module, MODULE::OUT_X_POS));
		addParam(createParamCentered<XyScreenDummyMapButton>(Vec(147.4f, 309.8f), module, MODULE::OUT_Y_POS));

		TransitPadXyScreenWidget<MODULE>* screenWidget = new TransitPadXyScreenWidget<MODULE>(module, MODULE::SNAPSHOT_X_POS, MODULE::SNAPSHOT_Y_POS, MODULE::OUT_X_POS, MODULE::OUT_Y_POS);
		screenWidget->box.pos = Vec(3.f, 63.3f + 3.f);
		screenWidget->box.size = Vec(225.f - 6.f, 225.f - 6.f);
		addChild(screenWidget);

		XySeqEditWidget<MODULE>* seqEditWidget = new XySeqEditWidget<MODULE>(module, MODULE::OUT_X_POS, MODULE::OUT_Y_POS);
		seqEditWidget->box.pos = screenWidget->box.pos;
		seqEditWidget->box.size = screenWidget->box.size;
		addChild(seqEditWidget);

		TransitPadXySeqLedDisplay<MODULE>* seqDisplay1 = createWidget<TransitPadXySeqLedDisplay<MODULE>>(Vec(41.5f, 329.8f));
		seqDisplay1->box.size = Vec(20.4f, 13.2f);
		seqDisplay1->module = module;
		seqDisplay1->id = 0;
		addChild(seqDisplay1);

		if (module) {
			vizOverlay = new TransitPadVizOverlay;
			vizOverlay->module = module;
			vizOverlay->hostWidget = this;
			vizOverlay->visible = false;
			APP->scene->rack->addChild(vizOverlay);
		}
	}

	~TransitPadWidget() {
		if (vizOverlay) {
			APP->scene->rack->removeChild(vizOverlay);
			delete vizOverlay;
			vizOverlay = nullptr;
		}
	}

	void step() override {
		if (vizOverlay) vizOverlay->visible = module->vizMode;
		ThemedModuleWidget<TransitPadModule<>>::step();
	}

	void onHoverKey(const event::HoverKey& e) override {
		if (e.key == GLFW_KEY_SPACE && e.action == GLFW_PRESS && (e.mods & RACK_MOD_MASK) == 0) {
			module->vizMode = !module->vizMode;
			e.consume(this);
			return;
		}
		ThemedModuleWidget<MODULE>::onHoverKey(e);
	}
};

} // namespace Transit
} // namespace StoermelderPackOne

Model* modelTransitPad = createModel<StoermelderPackOne::Transit::TransitPadModule<8>, StoermelderPackOne::Transit::TransitPadWidget>("TransitPad");