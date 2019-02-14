#include "plugin.hpp"


static const int MAX_CHANNELS = 16;


struct CV_Map : Module {
	enum ParamIds {
		NUM_PARAMS
	};
	enum InputIds {
		POLY_INPUT,
		NUM_INPUTS
	};
	enum OutputIds {
		NUM_OUTPUTS
	};
	enum LightIds {
		ENUMS(CHANNEL_LIGHTS, 16),
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

	CV_Map() {
		config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);
		for (int id = 0; id < MAX_CHANNELS; id++) {
			APP->engine->addParamHandle(&paramHandles[id]);
		}
		onReset();
	}

	~CV_Map() {
		for (int id = 0; id < MAX_CHANNELS; id++) {
			APP->engine->removeParamHandle(&paramHandles[id]);
		}
	}

	int lightFrame = 0;

	void onReset() override {
		learningId = -1;
		learnedParam = false;
		clearMaps();
		mapLen = 1;
	}

	void step() override {
		float deltaTime = APP->engine->getSampleTime();
		
		// Step channels
		for (int id = 0; id < mapLen; id++) {
			// Get module
			Module *module = paramHandles[id].module;
			if (!module)
				continue;
			// Get param
			int paramId = paramHandles[id].paramId;
			Param *param = &module->params[paramId];
			if (!param->isBounded())
				continue;
			// Set param
			float v = inputs[POLY_INPUT].getVoltage(id);
			v = rescale(v, -5.f, 5.f, 0.f, 1.f);
			v = valueFilters[id].process(deltaTime, v);
			v = rescale(v, 0.f, 1.f, param->minValue, param->maxValue);
			APP->engine->setParam(module, paramId, v);
		}
		
		// Set channel lights infrequently
		if (++lightFrame >= 512) {
			lightFrame = 0;
			for (int c = 0; c < 16; c++) {
				bool active = (c < inputs[POLY_INPUT].getChannels());
				lights[CHANNEL_LIGHTS + c].setBrightness(active);
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
	}
};


struct CV_MapChoice : LedDisplayChoice {
	CV_Map *module;
	int id;
	int disableLearnFrames = -1;

	void setModule(CV_Map *module) {
		this->module = module;
	}

	void onButton(const event::Button &e) override {
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
		APP->scene->rackWidget->touchedParam = NULL;
		module->enableLearn(id);
		e.consume(this);
	}

	void onDeselect(const event::Deselect &e) override {
		if (!module)
			return;
		// Check if a ParamWidget was touched
		ParamWidget *touchedParam = APP->scene->rackWidget->touchedParam;
		if (touchedParam) {
			APP->scene->rackWidget->touchedParam = NULL;
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
		text = "";
		if (module->paramHandles[id].moduleId >= 0) {
			text += getParamName();
		}
		if (module->paramHandles[id].moduleId < 0) {
			if (module->learningId == id) {
				text = "Mapping...";
			}
			else {
				text = "Unmapped";
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
		ModuleWidget *mw = APP->scene->rackWidget->getModule(paramHandle->moduleId);
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
		Param *param = &m->params[paramId];
		std::string s;
		s += mw->model->name;
		s += " ";
		s += param->label;
		return s;
	}
};


struct CV_MapDisplay : LedDisplay {
	CV_Map *module;
	ScrollWidget *scroll;
	CV_MapChoice *choices[MAX_CHANNELS];
	LedDisplaySeparator *separators[MAX_CHANNELS];

	void setModule(CV_Map *module) {
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

			CV_MapChoice *choice = createWidget<CV_MapChoice>(pos);
			choice->box.size.x = box.size.x;
			choice->id = id;
			choice->setModule(module);
			scroll->container->addChild(choice);
			choices[id] = choice;

			pos = choice->box.getBottomLeft();
		}
	}

	void step() override {
		if (!module)
			return;

		int mapLen = module->mapLen;
		for (int id = 0; id < MAX_CHANNELS; id++) {
			choices[id]->visible = (id < mapLen);
			separators[id]->visible = (id < mapLen);
		}

		LedDisplay::step();
	}
};

struct CV_MapWidget : ModuleWidget {
	CV_MapWidget(CV_Map *module) {	
		setModule(module);
		setPanel(APP->window->loadSvg(asset::plugin(pluginInstance, "res/CV-Map.svg")));

		addChild(createWidget<ScrewSilver>(Vec(RACK_GRID_WIDTH, 0)));
		addChild(createWidget<ScrewSilver>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, 0)));
		addChild(createWidget<ScrewSilver>(Vec(RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));
		addChild(createWidget<ScrewSilver>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));

		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(6.77, 21.347)), module, CV_Map::POLY_INPUT));

		addChild(createLightCentered<TinyLight<BlueLight>>(mm2px(Vec(15.276, 17.775)), module, CV_Map::CHANNEL_LIGHTS + 0));
		addChild(createLightCentered<TinyLight<BlueLight>>(mm2px(Vec(17.275, 17.775)), module, CV_Map::CHANNEL_LIGHTS + 1));
		addChild(createLightCentered<TinyLight<BlueLight>>(mm2px(Vec(19.275, 17.775)), module, CV_Map::CHANNEL_LIGHTS + 2));
		addChild(createLightCentered<TinyLight<BlueLight>>(mm2px(Vec(21.275, 17.775)), module, CV_Map::CHANNEL_LIGHTS + 3));
		addChild(createLightCentered<TinyLight<BlueLight>>(mm2px(Vec(15.276, 19.775)), module, CV_Map::CHANNEL_LIGHTS + 4));
		addChild(createLightCentered<TinyLight<BlueLight>>(mm2px(Vec(17.275, 19.775)), module, CV_Map::CHANNEL_LIGHTS + 5));
		addChild(createLightCentered<TinyLight<BlueLight>>(mm2px(Vec(19.275, 19.775)), module, CV_Map::CHANNEL_LIGHTS + 6));
		addChild(createLightCentered<TinyLight<BlueLight>>(mm2px(Vec(21.275, 19.775)), module, CV_Map::CHANNEL_LIGHTS + 7));
		addChild(createLightCentered<TinyLight<BlueLight>>(mm2px(Vec(15.276, 21.775)), module, CV_Map::CHANNEL_LIGHTS + 8));
		addChild(createLightCentered<TinyLight<BlueLight>>(mm2px(Vec(17.275, 21.775)), module, CV_Map::CHANNEL_LIGHTS + 9));
		addChild(createLightCentered<TinyLight<BlueLight>>(mm2px(Vec(19.275, 21.775)), module, CV_Map::CHANNEL_LIGHTS + 10));
		addChild(createLightCentered<TinyLight<BlueLight>>(mm2px(Vec(21.276, 21.775)), module, CV_Map::CHANNEL_LIGHTS + 11));
		addChild(createLightCentered<TinyLight<BlueLight>>(mm2px(Vec(15.276, 23.775)), module, CV_Map::CHANNEL_LIGHTS + 12));
		addChild(createLightCentered<TinyLight<BlueLight>>(mm2px(Vec(17.275, 23.775)), module, CV_Map::CHANNEL_LIGHTS + 13));
		addChild(createLightCentered<TinyLight<BlueLight>>(mm2px(Vec(19.275, 23.775)), module, CV_Map::CHANNEL_LIGHTS + 14));
		addChild(createLightCentered<TinyLight<BlueLight>>(mm2px(Vec(21.276, 23.775)), module, CV_Map::CHANNEL_LIGHTS + 15));

		CV_MapDisplay *mapWidget = createWidget<CV_MapDisplay>(mm2px(Vec(3.41891, 28.02)));
		mapWidget->box.size = mm2px(Vec(33.840, 91.664));
		mapWidget->setModule(module);
		addChild(mapWidget);
	}
};


Model *modelCV_Map = createModel<CV_Map, CV_MapWidget>("CV-Map");
