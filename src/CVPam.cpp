#include "plugin.hpp"

static const int MAX_CHANNELS = 32;


struct CV_Pam : Module {
	enum ParamIds {
		NUM_PARAMS
	};
	enum InputIds {
		NUM_INPUTS
	};
	enum OutputIds {
		POLY_OUTPUT1,
		POLY_OUTPUT2,		
		NUM_OUTPUTS
	};
	enum LightIds {
		ENUMS(CHANNEL_LIGHTS1, 16),
		ENUMS(CHANNEL_LIGHTS2, 16),
		NUM_LIGHTS
	};

	/** Number of maps */
	int mapLen = 0;
	/** The mapped param handle of each channel */
	ParamHandle paramHandles[MAX_CHANNELS];

	/** Channel ID of the learning session */
	int learningId;
	/** Whether the param has been set during the learning session */
	bool learnedParam;

	/** The smoothing processor (normalized between 0 and 1) of each channel */
	dsp::ExponentialFilter valueFilters[MAX_CHANNELS];

  	bool bipolarOutput = false;

	dsp::ClockDivider lightDivider;

	CV_Pam() {
		config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);
		for (int id = 0; id < MAX_CHANNELS; id++) {
			APP->engine->addParamHandle(&paramHandles[id]);
		}
		onReset();
		lightDivider.setDivision(512);
	}

	~CV_Pam() {
		for (int id = 0; id < MAX_CHANNELS; id++) {
			APP->engine->removeParamHandle(&paramHandles[id]);
		}
	}

	void onReset() override {
		learningId = -1;
		learnedParam = false;
		clearMaps();
		mapLen = 1;
	}

	void process(const ProcessArgs &args) override {		
		int lastChannel_out1 = -1;
		int lastChannel_out2 = -1;

		// Step channels
		for (int id = 0; id < mapLen; id++) {
			// Get Module
			Module *module = paramHandles[id].module;
			if (!module)
				continue;
			// Get ParamQuantity
			int paramId = paramHandles[id].paramId;
			ParamQuantity *paramQuantity = module->paramQuantities[paramId];
			if (!paramQuantity)
				continue;
			if (!paramQuantity->isBounded())
				continue;

			lastChannel_out1 = id < 16 ? id : lastChannel_out1;
			lastChannel_out2 = id >= 16 ? id - 16 : lastChannel_out2;
			
			// set voltage
			float v = paramQuantity->getScaledValue();
			v = valueFilters[id].process(args.sampleTime, v);
			v = rescale(v, 0.f, 1.f, 0.f, 10.f);
			if (bipolarOutput)
	  			v -= 5.f;
			if (id < 16) 
				outputs[POLY_OUTPUT1].setVoltage(v, id); 
			else 
				outputs[POLY_OUTPUT2].setVoltage(v, id - 16);			
		}
		
		outputs[POLY_OUTPUT1].setChannels(lastChannel_out1 + 1);
		outputs[POLY_OUTPUT2].setChannels(lastChannel_out2 + 1);

		// Set channel lights infrequently
		if (lightDivider.process()) {
			for (int c = 0; c < 16; c++) {
				bool active = (c < outputs[POLY_OUTPUT1].getChannels());
				lights[CHANNEL_LIGHTS1 + c].setBrightness(active);
			}
			for (int c = 0; c < 16; c++) {
				bool active = (c < outputs[POLY_OUTPUT2].getChannels());
				lights[CHANNEL_LIGHTS2 + c].setBrightness(active);
			}
		}
	}

	void clearMap(int id) {
		learningId = -1;
		APP->engine->updateParamHandle(&paramHandles[id], -1, 0, true);
		valueFilters[id].reset();
		updateMapLen();
	}

	void clearMaps() {
		learningId = -1;
		for (int id = 0; id < MAX_CHANNELS; id++) {
			APP->engine->updateParamHandle(&paramHandles[id], -1, 0, true);
			valueFilters[id].reset();
		}
		mapLen = 0;
	}

	void updateMapLen() {
		// Find last nonempty map
		int id;
		for (id = MAX_CHANNELS - 1; id >= 0; id--) {
			if (paramHandles[id].moduleId >= 0)
				break;
		}
		mapLen = id + 1;
		// Add an empty "Mapping..." slot
		if (mapLen < MAX_CHANNELS)
			mapLen++;
	}

	void commitLearn() {
		if (learningId < 0)
			return;
		if (!learnedParam)
			return;
		// Reset learned state
		learnedParam = false;
		// Find next incomplete map
		while (++learningId < MAX_CHANNELS) {
			if (paramHandles[learningId].moduleId < 0)
				return;
		}
		learningId = -1;
	}

	void enableLearn(int id) {
		if (learningId != id) {
			learningId = id;
			learnedParam = false;
		}
	}

	void disableLearn(int id) {
		if (learningId == id) {
			learningId = -1;
		}
	}

	void learnParam(int id, int moduleId, int paramId) {
		APP->engine->updateParamHandle(&paramHandles[id], moduleId, paramId, true);
		learnedParam = true;
		commitLearn();
		updateMapLen();
	}

	json_t *dataToJson() override {
		json_t *rootJ = json_object();

		json_t *mapsJ = json_array();
		for (int id = 0; id < mapLen; id++) {
			json_t *mapJ = json_object();
			json_object_set_new(mapJ, "moduleId", json_integer(paramHandles[id].moduleId));
			json_object_set_new(mapJ, "paramId", json_integer(paramHandles[id].paramId));
			json_array_append(mapsJ, mapJ);
		}
		json_object_set_new(rootJ, "maps", mapsJ);

		json_t *bipolarOutputJ = json_boolean(bipolarOutput);
		json_object_set_new(rootJ, "bipolarOutput", bipolarOutputJ);

		return rootJ;
	}

	void dataFromJson(json_t *rootJ) override {
		clearMaps();

		json_t *mapsJ = json_object_get(rootJ, "maps");
		if (mapsJ) {
			json_t *mapJ;
			size_t mapIndex;
			json_array_foreach(mapsJ, mapIndex, mapJ) {
				json_t *moduleIdJ = json_object_get(mapJ, "moduleId");
				json_t *paramIdJ = json_object_get(mapJ, "paramId");
				if (!(moduleIdJ && paramIdJ))
					continue;
				if (mapIndex >= MAX_CHANNELS)
					continue;
				APP->engine->updateParamHandle(&paramHandles[mapIndex], json_integer_value(moduleIdJ), json_integer_value(paramIdJ), false);
			}
		}
		updateMapLen();

		json_t *bipolarOutputJ = json_object_get(rootJ, "bipolarOutput");
		bipolarOutput = json_boolean_value(bipolarOutputJ);
	}
};


struct CV_PamChoice : LedDisplayChoice {
	CV_Pam *module;
	int id;
	int disableLearnFrames = -1;

	int scrollCount = 0;
	int scrollOffset = 0;

	void setModule(CV_Pam *module) {
		this->module = module;
	}

	void onButton(const event::Button &e) override {
		e.stopPropagating();
		if (!module)
			return;

		if (e.action == GLFW_PRESS && e.button == GLFW_MOUSE_BUTTON_LEFT) {
			e.consume(this);
		}

		if (e.action == GLFW_PRESS && e.button == GLFW_MOUSE_BUTTON_RIGHT) {
			module->clearMap(id);
			e.consume(this);
		}
	}

	void onSelect(const event::Select &e) override {
		if (!module)
			return;

		ScrollWidget *scroll = getAncestorOfType<ScrollWidget>();
		scroll->scrollTo(box);

		// Reset touchedParam
		APP->scene->rack->touchedParam = NULL;
		module->enableLearn(id);
	}

	void onDeselect(const event::Deselect &e) override {
		if (!module)
			return;
		// Check if a ParamWidget was touched
		ParamWidget *touchedParam = APP->scene->rack->touchedParam;
		if (touchedParam) {
			APP->scene->rack->touchedParam = NULL;
			int moduleId = touchedParam->paramQuantity->module->id;
			int paramId = touchedParam->paramQuantity->paramId;
			module->learnParam(id, moduleId, paramId);
		}
		else {
			module->disableLearn(id);
		}
	}

	void step() override {
		if (!module)
			return;

		// Set bgColor and selected state
		if (module->learningId == id) {
			bgColor = color;
			bgColor.a = 0.15;

			// HACK
			if (APP->event->selectedWidget != this)
				APP->event->setSelected(this);
		}
		else {
			bgColor = nvgRGBA(0, 0, 0, 0);

			// HACK
			if (APP->event->selectedWidget == this)
				APP->event->setSelected(NULL);
		}

		// Set text
		text = ((id < 9 ? "0" : "") + std::to_string(id + 1)) + " ";
		if (module->paramHandles[id].moduleId >= 0) {
			std::string pn = getParamName();
			if (pn.length() > 15) {
				text += pn.substr(scrollOffset > (int)pn.length() ? 0 : scrollOffset);
				scrollCount = (scrollCount + 1) % 5;
				if (scrollCount == 0) {
					scrollOffset = (scrollOffset + 1) % (pn.length() + 15);
				}
			} else {
				text += pn;
			}
		}

		if (module->paramHandles[id].moduleId < 0) {
			if (module->learningId == id) {
				text += "Mapping...";
			}
			else {
				text += "Unmapped";
			}
		}

		// Set text color
		if (module->paramHandles[id].moduleId >= 0 || module->learningId == id) {
			color.a = 1.0;
		}
		else {
			color.a = 0.5;
		}
	}

	std::string getParamName() {
		if (!module)
			return "";
		if (id >= module->mapLen)
			return "";
		ParamHandle *paramHandle = &module->paramHandles[id];
		if (paramHandle->moduleId < 0)
			return "";
		ModuleWidget *mw = APP->scene->rack->getModule(paramHandle->moduleId);
		if (!mw)
			return "";
		// Get the Module from the ModuleWidget instead of the ParamHandle.
		// I think this is more elegant since this method is called in the app world instead of the engine world.
		Module *m = mw->module;
		if (!m)
			return "";
		int paramId = paramHandle->paramId;
		if (paramId >= (int) m->params.size())
			return "";
		ParamQuantity *paramQuantity = m->paramQuantities[paramId];
		std::string s;
		s += mw->model->name;
		s += " ";
		s += paramQuantity->label;
		return s;
	}
};


struct CV_PamDisplay : LedDisplay {
	CV_Pam *module;
	ScrollWidget *scroll;
	CV_PamChoice *choices[MAX_CHANNELS];
	LedDisplaySeparator *separators[MAX_CHANNELS];

	void setModule(CV_Pam *module) {
		this->module = module;

		scroll = new ScrollWidget;
		scroll->box.size.x = box.size.x;
		scroll->box.size.y = box.size.y - scroll->box.pos.y;
		addChild(scroll);

		LedDisplaySeparator *separator = createWidget<LedDisplaySeparator>(scroll->box.pos);
		separator->box.size.x = box.size.x;
		addChild(separator);
		separators[0] = separator;

		Vec pos;
		for (int id = 0; id < MAX_CHANNELS; id++) {
			if (id > 0) {
				LedDisplaySeparator *separator = createWidget<LedDisplaySeparator>(pos);
				separator->box.size.x = box.size.x;
				scroll->container->addChild(separator);
				separators[id] = separator;
			}

			CV_PamChoice *choice = createWidget<CV_PamChoice>(pos);
			choice->box.size.x = box.size.x;
			choice->id = id;
			choice->setModule(module);
			scroll->container->addChild(choice);
			choices[id] = choice;

			pos = choice->box.getBottomLeft();
		}
	}
};

struct CV_PamWidget : ModuleWidget {
	CV_PamWidget(CV_Pam *module) {	
		setModule(module);
		setPanel(APP->window->loadSvg(asset::plugin(pluginInstance, "res/CV-Pam.svg")));

		addChild(createWidget<ScrewSilver>(Vec(RACK_GRID_WIDTH, 0)));
		addChild(createWidget<ScrewSilver>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, 0)));
		addChild(createWidget<ScrewSilver>(Vec(RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));
		addChild(createWidget<ScrewSilver>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));

		float o = 9.f;
		float v = 13.5f;
		float d = 6.8f;
		addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(o, 21.1)), module, CV_Pam::POLY_OUTPUT1));
		addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(o + d + v + 12.4f, 21.1)), module, CV_Pam::POLY_OUTPUT2));

		addChild(createLightCentered<TinyLight<BlueLight>>(mm2px(Vec(o + d, 17.975)), module, CV_Pam::CHANNEL_LIGHTS1 + 0));
		addChild(createLightCentered<TinyLight<BlueLight>>(mm2px(Vec(o + d + 2, 17.975)), module, CV_Pam::CHANNEL_LIGHTS1 + 1));
		addChild(createLightCentered<TinyLight<BlueLight>>(mm2px(Vec(o + d + 4, 17.975)), module, CV_Pam::CHANNEL_LIGHTS1 + 2));
		addChild(createLightCentered<TinyLight<BlueLight>>(mm2px(Vec(o + d + 6, 17.975)), module, CV_Pam::CHANNEL_LIGHTS1 + 3));
		addChild(createLightCentered<TinyLight<BlueLight>>(mm2px(Vec(o + d, 19.975)), module, CV_Pam::CHANNEL_LIGHTS1 + 4));
		addChild(createLightCentered<TinyLight<BlueLight>>(mm2px(Vec(o + d + 2, 19.975)), module, CV_Pam::CHANNEL_LIGHTS1 + 5));
		addChild(createLightCentered<TinyLight<BlueLight>>(mm2px(Vec(o + d + 4, 19.975)), module, CV_Pam::CHANNEL_LIGHTS1 + 6));
		addChild(createLightCentered<TinyLight<BlueLight>>(mm2px(Vec(o + d + 6, 19.975)), module, CV_Pam::CHANNEL_LIGHTS1 + 7));
		addChild(createLightCentered<TinyLight<BlueLight>>(mm2px(Vec(o + d, 21.975)), module, CV_Pam::CHANNEL_LIGHTS1 + 8));
		addChild(createLightCentered<TinyLight<BlueLight>>(mm2px(Vec(o + d + 2, 21.975)), module, CV_Pam::CHANNEL_LIGHTS1 + 9));
		addChild(createLightCentered<TinyLight<BlueLight>>(mm2px(Vec(o + d + 4, 21.975)), module, CV_Pam::CHANNEL_LIGHTS1 + 10));
		addChild(createLightCentered<TinyLight<BlueLight>>(mm2px(Vec(o + d + 6, 21.975)), module, CV_Pam::CHANNEL_LIGHTS1 + 11));
		addChild(createLightCentered<TinyLight<BlueLight>>(mm2px(Vec(o + d, 23.975)), module, CV_Pam::CHANNEL_LIGHTS1 + 12));
		addChild(createLightCentered<TinyLight<BlueLight>>(mm2px(Vec(o + d + 2, 23.975)), module, CV_Pam::CHANNEL_LIGHTS1 + 13));
		addChild(createLightCentered<TinyLight<BlueLight>>(mm2px(Vec(o + d + 4, 23.975)), module, CV_Pam::CHANNEL_LIGHTS1 + 14));
		addChild(createLightCentered<TinyLight<BlueLight>>(mm2px(Vec(o + d + 6, 23.975)), module, CV_Pam::CHANNEL_LIGHTS1 + 15));

		addChild(createLightCentered<TinyLight<BlueLight>>(mm2px(Vec(o + d + v, 17.975)), module, CV_Pam::CHANNEL_LIGHTS2 + 0));
		addChild(createLightCentered<TinyLight<BlueLight>>(mm2px(Vec(o + d + 2 + v, 17.975)), module, CV_Pam::CHANNEL_LIGHTS2 + 1));
		addChild(createLightCentered<TinyLight<BlueLight>>(mm2px(Vec(o + d + 4 + v, 17.975)), module, CV_Pam::CHANNEL_LIGHTS2 + 2));
		addChild(createLightCentered<TinyLight<BlueLight>>(mm2px(Vec(o + d + 6 + v, 17.975)), module, CV_Pam::CHANNEL_LIGHTS2 + 3));
		addChild(createLightCentered<TinyLight<BlueLight>>(mm2px(Vec(o + d + v, 19.975)), module, CV_Pam::CHANNEL_LIGHTS2 + 4));
		addChild(createLightCentered<TinyLight<BlueLight>>(mm2px(Vec(o + d + 2 + v, 19.975)), module, CV_Pam::CHANNEL_LIGHTS2 + 5));
		addChild(createLightCentered<TinyLight<BlueLight>>(mm2px(Vec(o + d + 4 + v, 19.975)), module, CV_Pam::CHANNEL_LIGHTS2 + 6));
		addChild(createLightCentered<TinyLight<BlueLight>>(mm2px(Vec(o + d + 6 + v, 19.975)), module, CV_Pam::CHANNEL_LIGHTS2 + 7));
		addChild(createLightCentered<TinyLight<BlueLight>>(mm2px(Vec(o + d + v, 21.975)), module, CV_Pam::CHANNEL_LIGHTS2 + 8));
		addChild(createLightCentered<TinyLight<BlueLight>>(mm2px(Vec(o + d + 2 + v, 21.975)), module, CV_Pam::CHANNEL_LIGHTS2 + 9));
		addChild(createLightCentered<TinyLight<BlueLight>>(mm2px(Vec(o + d + 4 + v, 21.975)), module, CV_Pam::CHANNEL_LIGHTS2 + 10));
		addChild(createLightCentered<TinyLight<BlueLight>>(mm2px(Vec(o + d + 6 + v, 21.975)), module, CV_Pam::CHANNEL_LIGHTS2 + 11));
		addChild(createLightCentered<TinyLight<BlueLight>>(mm2px(Vec(o + d + v, 23.975)), module, CV_Pam::CHANNEL_LIGHTS2 + 12));
		addChild(createLightCentered<TinyLight<BlueLight>>(mm2px(Vec(o + d + 2 + v, 23.975)), module, CV_Pam::CHANNEL_LIGHTS2 + 13));
		addChild(createLightCentered<TinyLight<BlueLight>>(mm2px(Vec(o + d + 4 + v, 23.975)), module, CV_Pam::CHANNEL_LIGHTS2 + 14));
		addChild(createLightCentered<TinyLight<BlueLight>>(mm2px(Vec(o + d + 6 + v, 23.975)), module, CV_Pam::CHANNEL_LIGHTS2 + 15));


		CV_PamDisplay *pamWidget = createWidget<CV_PamDisplay>(mm2px(Vec(3.41891, 29.f)));
		pamWidget->box.size = mm2px(Vec(43.999, 91));
		pamWidget->setModule(module);
		addChild(pamWidget);
	}


	void appendContextMenu(Menu *menu) override {
		CV_Pam *cv_pam = dynamic_cast<CV_Pam*>(module);
		assert(cv_pam);

		struct UniBiItem : MenuItem {
			CV_Pam *cv_pam;

			void onAction(const event::Action &e) override {
				cv_pam->bipolarOutput ^= true;
			}

			void step() override {
				rightText = cv_pam->bipolarOutput ? "-5V..5V" : "0V..10V";
				MenuItem::step();
			}
		};

		menu->addChild(construct<MenuLabel>());
		menu->addChild(construct<UniBiItem>(&MenuItem::text, "Signal output", &UniBiItem::cv_pam, cv_pam));
  	};
};


Model *modelCV_Pam = createModel<CV_Pam, CV_PamWidget>("CVPam");
