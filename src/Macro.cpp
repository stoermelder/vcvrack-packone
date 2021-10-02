#include "plugin.hpp"
#include "MapModuleBase.hpp"
#include "components/MapButton.hpp"
#include "components/VoltageLedDisplay.hpp"
#include "components/Knobs.hpp"
#include "ui/ParamWidgetContextExtender.hpp"

namespace StoermelderPackOne {
namespace Macro {

static const int MAPS = 4;
static const int CVPORTS = 2;

struct MacroModule : CVMapModuleBase<MAPS> {
	enum ParamIds {
		ENUMS(PARAM_MAP, MAPS),
		PARAM_KNOB,
		NUM_PARAMS
	};
	enum InputIds {
		INPUT,
		NUM_INPUTS
	};
	enum OutputIds {
		ENUMS(OUTPUT_CV, CVPORTS),
		NUM_OUTPUTS
	};
	enum LightIds {
		ENUMS(LIGHT_MAP, 2 * MAPS),
		ENUMS(LIGHT_MAP_CV, MAPS),
		NUM_LIGHTS
	};

	struct CvParamQuantity : ParamQuantity {
		Output* output;
		bool usePrepared = false;
		float prepared;
		void setValue(float v) override {
			output->setVoltage(v);
		}
		float getValue() override {
			if (usePrepared) {
				usePrepared = false;
				return rescale(prepared, 0.f, 1.f, getMinValue(), getMaxValue());
			}
			return output->getVoltage();
		}
		void prepareScaledValue(float v) {
			usePrepared = true;
			prepared = v;
		}
	};

	/** [Stored to Json] */
	ScaledMapParam<float> scaleParam[MAPS];
	/** [Stored to Json] */
	ScaledMapParam<float, CvParamQuantity> scaleCvs[CVPORTS];

	dsp::ClockDivider processDivider;
	/** [Stored to JSON] */
	int processDivision;

	/** [Stored to JSON] */
	int panelTheme = 0;

	dsp::ClockDivider lightDivider;

	MacroModule() {
		panelTheme = pluginSettings.panelThemeDefault;
		config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);
		configInput(INPUT, "Voltage");
		for (int i = 0; i < CVPORTS; i++) {
			configOutput(OUTPUT_CV + i, string::f("CV %i", i + 1));
		}
		configParam(PARAM_KNOB, 0.f, 1.f, 0.f, "Macro knob", "%", 0.f, 100.f);

		for (int i = 0; i < MAPS; i++) {
			MapParamQuantity<MacroModule>* pq = configParam<MapParamQuantity<MacroModule>>(PARAM_MAP + i, 0.f, 1.f, 0.f, string::f("Map %i", i + 1));
			pq->mymodule = this;
			pq->id = i;
			paramHandles[i].text = "MACRO";
			scaleParam[i].setLimits(0.f, 1.f, std::numeric_limits<float>::infinity());
		}

		for (int i = 0; i < CVPORTS; i++) {
			CvParamQuantity* pq = new CvParamQuantity;
			pq->output = &outputs[OUTPUT_CV + i];
			pq->minValue = 0.f;
			pq->maxValue = 10.f;
			scaleCvs[i].setParamQuantity(pq);
			scaleCvs[i].setLimits(0.f, 1.f, std::numeric_limits<float>::infinity());
		}

		lightDivider.setDivision(1024);
		onReset();
	}

	~MacroModule() {
		for (size_t i = 0; i < CVPORTS; i++) {
			delete scaleCvs[i].paramQuantity;
		}
	}

	void onReset() override {
		CVMapModuleBase<MAPS>::onReset();
		for (size_t i = 0; i < MAPS; i++) {
			scaleParam[i].reset();
			lastValue[i] = std::numeric_limits<float>::infinity();
		}
		for (size_t i = 0; i < CVPORTS; i++) {
			CvParamQuantity* pq = scaleCvs[i].paramQuantity;
			scaleCvs[i].reset();
			scaleCvs[i].setParamQuantity(pq);
		}
		lockParameterChanges = false;
		processDivision = 64;
		processDivider.setDivision(processDivision);
		processDivider.reset();
	}

	void process(const Module::ProcessArgs& args) override {
		if (processDivider.process()) {
			float deltaTime = args.sampleTime * float(processDivision);

			float v = params[PARAM_KNOB].getValue();
			if (inputs[INPUT].isConnected()) {
				float v1 = inputs[INPUT].getVoltage();
				if (bipolarInput) v1 += 5.f;
				v = v * v1;
				v = rescale(v, 0.f, 10.f, 0.f, 1.f);
			}

			for (size_t i = 0; i < MAPS; i++) {
				ParamQuantity* paramQuantity = getParamQuantity(i);
				scaleParam[i].setParamQuantity(paramQuantity);
				if (paramQuantity) {
					if (lastValue[i] != v) {
						scaleParam[i].setValue(v);
						lastValue[i] = v;
					}
					scaleParam[i].process(deltaTime, lockParameterChanges);
				}
			}

			for (size_t i = 0; i < CVPORTS; i++) {
				if (!outputs[OUTPUT_CV + i].isConnected()) continue;
				scaleCvs[i].setValue(v);
				scaleCvs[i].process(deltaTime);
			}
		}

		if (lightDivider.process()) {
			for (int i = 0; i < MAPS; i++) {
				lights[LIGHT_MAP + i * 2].setBrightness(paramHandles[i].moduleId >= 0 && learningId != i ? 1.f : 0.f);
				lights[LIGHT_MAP + i * 2 + 1].setBrightness(learningId == i ? 1.f : 0.f);
				lights[LIGHT_MAP_CV + i].setBrightness(scaleParam[i].getLightBrightness());
			}
		}

		CVMapModuleBase<MAPS>::process(args);
	}

	void commitLearn() override {
		if (learningId >= 0) {
			scaleParam[learningId].reset();
			lastValue[learningId] = std::numeric_limits<float>::infinity();
		}
		CVMapModuleBase<MAPS>::commitLearn();
		disableLearn(learningId);
	}

	void setProcessDivision(int d) {
		processDivision = d;
		processDivider.setDivision(d);
		processDivider.reset();
	}

	float getCurrentVoltage() {
		return inputs[INPUT].getVoltage();
	}

	json_t* dataToJson() override {
		json_t* rootJ = CVMapModuleBase<MAPS>::dataToJson();
		json_object_set_new(rootJ, "panelTheme", json_integer(panelTheme));
		json_object_set_new(rootJ, "processDivision", json_integer(processDivision));

		json_t* cvsJ = json_array();
		for (int i = 0; i < CVPORTS; i++) {
			json_t* cvJ = json_object();
			json_object_set_new(cvJ, "slew", json_real(scaleCvs[i].getSlew()));
			json_object_set_new(cvJ, "min", json_real(scaleCvs[i].getMin()));
			json_object_set_new(cvJ, "max", json_real(scaleCvs[i].getMax()));
			json_object_set_new(cvJ, "bipolar", json_boolean(scaleCvs[i].paramQuantity->minValue == -5.f));
			json_object_set_new(cvJ, "value", json_real(scaleCvs[i].paramQuantity->getScaledValue()));
			json_array_append_new(cvsJ, cvJ);
		}
		json_object_set_new(rootJ, "cvs", cvsJ);

		return rootJ;
	}

	void dataToJsonMap(json_t* mapJ, int i) override {
		json_object_set_new(mapJ, "slew", json_real(scaleParam[i].getSlew()));
		json_object_set_new(mapJ, "min", json_real(scaleParam[i].getMin()));
		json_object_set_new(mapJ, "max", json_real(scaleParam[i].getMax()));
	}

	void dataFromJson(json_t* rootJ) override {
		CVMapModuleBase<MAPS>::dataFromJson(rootJ);
		panelTheme = json_integer_value(json_object_get(rootJ, "panelTheme"));
		json_t* processDivisionJ = json_object_get(rootJ, "processDivision");
		if (processDivisionJ) {
			processDivision = json_integer_value(processDivisionJ);
			processDivider.setDivision(processDivision);
		} 

		json_t* cvsJ = json_object_get(rootJ, "cvs");
		if (cvsJ) {
			json_t* cvJ;
			size_t i;
			json_array_foreach(cvsJ, i, cvJ) {
				if (i >= CVPORTS)
					continue;
				json_t* slewJ = json_object_get(cvJ, "slew");
				json_t* minJ = json_object_get(cvJ, "min");
				json_t* maxJ = json_object_get(cvJ, "max");
				if (slewJ) scaleCvs[i].setSlew(json_real_value(slewJ));
				if (minJ) scaleCvs[i].setMin(json_real_value(minJ));
				if (maxJ) scaleCvs[i].setMax(json_real_value(maxJ));
				json_t* bipolarJ = json_object_get(cvJ, "bipolar");
				if (bipolarJ) {
					bool bipolar = json_boolean_value(bipolarJ);
					scaleCvs[i].paramQuantity->minValue = bipolar ? -5.f : 0.f;
					scaleCvs[i].paramQuantity->maxValue = bipolar ? 5.f : 10.f;
				}
				json_t* valueJ = json_object_get(cvJ, "value");
				if (valueJ) {
					float v = json_real_value(valueJ);
					// Store the last value as output-ports get initialized after loading
					scaleCvs[i].paramQuantity->prepareScaledValue(v);
					scaleCvs[i].setValue(v);
				}
			}
		}
	}

	void dataFromJsonMap(json_t* mapJ, int i) override {
		json_t* slewJ = json_object_get(mapJ, "slew");
		json_t* minJ = json_object_get(mapJ, "min");
		json_t* maxJ = json_object_get(mapJ, "max");
		if (slewJ) scaleParam[i].setSlew(json_real_value(slewJ));
		if (minJ) scaleParam[i].setMin(json_real_value(minJ));
		if (maxJ) scaleParam[i].setMax(json_real_value(maxJ));
	}
};


struct MacroButton : MapButton<MacroModule> {
	void appendContextMenu(Menu* menu) override {
		menu->addChild(new MenuSeparator());
		menu->addChild(new MapSlewSlider<>(&module->scaleParam[id]));
		menu->addChild(new MenuSeparator());
		menu->addChild(construct<MenuLabel>(&MenuLabel::text, "Scaling"));
		menu->addChild(construct<MapScalingInputLabel<>>(&MenuLabel::text, "Input", &MapScalingInputLabel<>::p, &module->scaleParam[id]));
		menu->addChild(construct<MapScalingOutputLabel<>>(&MenuLabel::text, "Parameter range", &MapScalingOutputLabel<>::p, &module->scaleParam[id]));
		menu->addChild(new MapMinSlider<>(&module->scaleParam[id]));
		menu->addChild(new MapMaxSlider<>(&module->scaleParam[id]));
		menu->addChild(construct<MapPresetMenuItem<>>(&MenuItem::text, "Preset", &MapPresetMenuItem<>::p, &module->scaleParam[id]));
	}
}; // struct MacroButton


struct MacroPort : StoermelderPort {
	typedef ScaledMapParam<float, MacroModule::CvParamQuantity> SCALE;
	int id;

	void onButton(const event::Button& e) override {
		if (e.action == GLFW_PRESS && e.button == GLFW_MOUSE_BUTTON_RIGHT) {
			createContextMenu();
			e.consume(this);
		}
		else {
			StoermelderPort::onButton(e);
		}
	}

	void createContextMenu() {
		MacroModule* module = dynamic_cast<MacroModule*>(this->module);

		struct BipolarItem : MenuItem {
			MacroModule* module;
			int id;
			void onAction(const event::Action& e) override {
				bool b = module->scaleCvs[id].paramQuantity->minValue == -5.f;
				module->scaleCvs[id].paramQuantity->minValue = b ? 0.f : -5.f;
				module->scaleCvs[id].paramQuantity->maxValue = b ? 10.f : 5.f;
			}
			void step() override {
				MenuItem::step();
				rightText = module->scaleCvs[id].paramQuantity->minValue == -5.f ? "-5V..5V" : "0V..10V";
			}
		}; // struct BipolarItem

		struct DisconnectItem : MenuItem {
			PortWidget* pw;
			void onAction(const event::Action& e) override {
				CableWidget* cw = APP->scene->rack->getTopCable(pw);
				if (cw) {
					// history::CableRemove
					history::CableRemove* h = new history::CableRemove;
					h->setCable(cw);
					APP->history->push(h);

					APP->scene->rack->removeCable(cw);
					delete cw;
				}
			}
		}; // struct DisconnectItem

		Menu* menu = createMenu();
		menu->addChild(construct<MenuLabel>(&MenuLabel::text, string::f("CV %i input", id + 1)));
		menu->addChild(construct<BipolarItem>(&MenuItem::text, "Output voltage", &BipolarItem::module, module, &BipolarItem::id, id));
		menu->addChild(new MapSlewSlider<SCALE>(&module->scaleCvs[id]));
		menu->addChild(new MenuSeparator());
		menu->addChild(construct<MenuLabel>(&MenuLabel::text, "Scaling"));
		menu->addChild(construct<MapScalingInputLabel<SCALE>>(&MenuLabel::text, "Input", &MapScalingInputLabel<SCALE>::p, &module->scaleCvs[id]));
		menu->addChild(construct<MapScalingOutputLabelUnit<SCALE>>(&MenuLabel::text, "Output voltage", &MapScalingOutputLabelUnit<SCALE>::p, &module->scaleCvs[id]));
		menu->addChild(new MapMinSlider<SCALE>(&module->scaleCvs[id]));
		menu->addChild(new MapMaxSlider<SCALE>(&module->scaleCvs[id]));
		menu->addChild(construct<MapPresetMenuItem<SCALE>>(&MenuItem::text, "Presets", &MapPresetMenuItem<SCALE>::p, &module->scaleCvs[id]));
		menu->addChild(new MenuSeparator());
		menu->addChild(construct<DisconnectItem>(&MenuItem::text, "Disconnect", &DisconnectItem::pw, this));
	}
}; // struct MacroPort


struct MacroWidget : ThemedModuleWidget<MacroModule>, ParamWidgetContextExtender {
	typedef MacroModule MODULE;
	MacroWidget(MODULE* module)
		: ThemedModuleWidget<MODULE>(module, "Macro") {
		setModule(module);

		addChild(createWidget<StoermelderBlackScrew>(Vec(RACK_GRID_WIDTH, 0)));
		addChild(createWidget<StoermelderBlackScrew>(Vec(RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));

		float o = 28.8f;
		for (size_t i = 0; i < MAPS; i++) {
			MacroButton* button = createParamCentered<MacroButton>(Vec(22.5f, 60.3f + o * i), module, MODULE::PARAM_MAP + i);
			button->setModule(module);
			button->id = i;
			addParam(button);
			addChild(createLightCentered<MapLight<GreenRedLight>>(Vec(22.5f, 60.3f + o * i), module, MODULE::LIGHT_MAP + i * 2));
			addChild(createLightCentered<TinyLight<WhiteLight>>(Vec(33.6f, 50.7f + o * i), module, MODULE::LIGHT_MAP_CV + i));
		}

		o = 28.1f;
		for (size_t i = 0; i < CVPORTS; i++) {
			MacroPort* p = createOutputCentered<MacroPort>(Vec(22.5f, 191.f + o * i), module, MODULE::OUTPUT_CV + i);
			p->id = i;
			addOutput(p);
		}

		addParam(createParamCentered<StoermelderLargeKnob>(Vec(22.5f, 260.7f), module, MODULE::PARAM_KNOB));

		VoltageLedDisplay<MODULE>* ledDisplay = createWidgetCentered<VoltageLedDisplay<MODULE>>(Vec(22.5f, 291.9f));
		ledDisplay->box.size = Vec(39.1f, 13.2f);
		ledDisplay->module = module;
		addChild(ledDisplay);
		addInput(createInputCentered<StoermelderPort>(Vec(22.5f, 327.9f), module, MODULE::INPUT));
	}

	void step() override {
		ParamWidgetContextExtender::step();
		ThemedModuleWidget<MacroModule>::step();
	}

	void appendContextMenu(Menu* menu) override {
		ThemedModuleWidget<MODULE>::appendContextMenu(menu);
		MODULE* module = dynamic_cast<MODULE*>(this->module);

		int sampleRate = int(APP->engine->getSampleRate());
		menu->addChild(new MenuSeparator());
		menu->addChild(StoermelderPackOne::Rack::createMapSubmenuItem<int>("Precision",
			{
				{ 1, string::f("Audio rate (%i Hz)", sampleRate / 1) },
				{ 8, string::f("High (%i Hz)", sampleRate / 8) },
				{ 64, string::f("Moderate (%i Hz)", sampleRate / 64) },
				{ 256, string::f("Lowest (%i Hz)", sampleRate / 256) }
			},
			[=]() {
				return module->processDivision;
			},
			[=](int division) {
				module->setProcessDivision(division);
			}
		));
		menu->addChild(new MenuSeparator());
		menu->addChild(createBoolPtrMenuItem("Lock parameter changes", &module->lockParameterChanges));
		menu->addChild(createIndexPtrSubmenuItem("Input voltage", {"0V..10V", "-5V..5V"}, &module->bipolarInput));
	}

	void extendParamWidgetContextMenu(ParamWidget* pw, Menu* menu) override {
		ParamQuantity* pq = pw->getParamQuantity();
		if (!pq) return;

		for (int id = 0; id < module->mapLen; id++) {
			if (module->paramHandles[id].moduleId == pq->module->id && module->paramHandles[id].paramId == pq->paramId) {
				menu->addChild(new MenuSeparator);
				menu->addChild(construct<MenuLabel>(&MenuLabel::text, "MACRO"));
				menu->addChild(construct<CenterModuleItem>(&MenuItem::text, "Center mapping module", &CenterModuleItem::mw, this));
				menu->addChild(new MenuSeparator);
				menu->addChild(new MapSlewSlider<>(&module->scaleParam[id]));
				menu->addChild(new MenuSeparator());
				menu->addChild(construct<MenuLabel>(&MenuLabel::text, "Scaling"));
				menu->addChild(construct<MapScalingInputLabel<>>(&MenuLabel::text, "Input", &MapScalingInputLabel<>::p, &module->scaleParam[id]));
				menu->addChild(construct<MapScalingOutputLabel<>>(&MenuLabel::text, "Parameter range", &MapScalingOutputLabel<>::p, &module->scaleParam[id]));
				menu->addChild(new MapMinSlider<>(&module->scaleParam[id]));
				menu->addChild(new MapMaxSlider<>(&module->scaleParam[id]));
				menu->addChild(construct<MapPresetMenuItem<>>(&MenuItem::text, "Preset", &MapPresetMenuItem<>::p, &module->scaleParam[id]));
			}
		}
	}
}; // struct MacroWidget

} // namespace Macro
} // namespace StoermelderPackOne

Model* modelMacro = createModel<StoermelderPackOne::Macro::MacroModule, StoermelderPackOne::Macro::MacroWidget>("Macro");