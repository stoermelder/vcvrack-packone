#include "plugin.hpp"
#include <midi.hpp>

static const int MAX_CHANNELS = 128;

struct CCMidiOutput : midi::Output {
	int lastValues[128];

	CCMidiOutput() {
		reset();
	}

	void reset() {
		for (int n = 0; n < 128; n++) {
			lastValues[n] = -1;
		}
	}

	void setValue(int value, int cc) {
		if (value == lastValues[cc])
			return;
		lastValues[cc] = value;
		// CC
		midi::Message m;
		m.setStatus(0xb);
		m.setNote(cc);
		m.setValue(value);
		sendMessage(m);
	}
};

struct MidiCat : Module {
	enum ParamIds {
		NUM_PARAMS
	};
	enum InputIds {
		NUM_INPUTS
	};
	enum OutputIds {
		NUM_OUTPUTS
	};
	enum LightIds {
		NUM_LIGHTS
	};

	midi::InputQueue midiInput;
    CCMidiOutput midiOutput;

	/** Number of maps */
	int mapLen = 0;
	/** The mapped CC number of each channel */
	int ccs[MAX_CHANNELS];
	/** The mapped param handle of each channel */
	ParamHandle paramHandles[MAX_CHANNELS];

	/** Channel ID of the learning session */
	int learningId;
	/** Whether the CC has been set during the learning session */
	bool learnedCc;
	/** Whether the param has been set during the learning session */
	bool learnedParam;

	/** The value of each CC number */
	int8_t values[128];
	/** The smoothing processor (normalized between 0 and 1) of each channel */
	dsp::ExponentialFilter valueFilters[MAX_CHANNELS];

	/** Track last values */
	float lastValue[MAX_CHANNELS];
    float lastValue2[MAX_CHANNELS];
	/** Allow manual changes of target parameters */
	bool lockParameterChanges = false;

	MidiCat() {
		config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);
		for (int id = 0; id < MAX_CHANNELS; id++) {
			paramHandles[id].color = nvgRGB(0xff, 0xff, 0x40);
			APP->engine->addParamHandle(&paramHandles[id]);
		}
		onReset();
	}

	~MidiCat() {
		for (int id = 0; id < MAX_CHANNELS; id++) {
			APP->engine->removeParamHandle(&paramHandles[id]);
		}
	}

	void onReset() override {
		learningId = -1;
		learnedCc = false;
		learnedParam = false;
		clearMaps();
		mapLen = 1;
		for (int i = 0; i < MAX_CHANNELS; i++) {
			values[i] = -1;
            lastValue[i] = -1;
            lastValue2[i] = -1;
		}
		midiInput.reset();
		midiOutput.reset();
		midiOutput.midi::Output::reset();
	}

	void process(const ProcessArgs &args) override {
		midi::Message msg;
		while (midiInput.shift(&msg)) {
			processMessage(msg);
		}

		// Step channels
		for (int id = 0; id < mapLen; id++) {
			int cc = ccs[id];
			if (cc < 0)
				continue;

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

            // Check if CC value has been set
            if (values[cc] >= 0)
            {
                // Set ParamQuantity
                float v = rescale(values[cc], 0, 127, 0.f, 1.f);

                if (lockParameterChanges || lastValue[id] != v) {
                    paramQuantity->setScaledValue(v);
                    lastValue[id] = v;
                }
            }

            float v = paramQuantity->getScaledValue();
            v = rescale(v, 0.f, 1.f, 0, 127);
            if (lastValue2[id] != v) {
                lastValue2[id] = v;
                midiOutput.setValue(v, cc);
            }
		}
	}

    void processMessage(midi::Message msg) {
        switch (msg.getStatus()) {
            // cc
            case 0xb: {
                processCC(msg);
            } break;
            default: break;
        }
    }

	void processCC(midi::Message msg) {
		uint8_t cc = msg.getNote();
		int8_t value = msg.getValue();
		// Learn
		if (0 <= learningId && values[cc] != value) {
			ccs[learningId] = cc;
			valueFilters[learningId].reset();
			learnedCc = true;
			commitLearn();
			updateMapLen();
			refreshParamHandleText(learningId);
		}
		values[cc] = value;
	}

	void clearMap(int id) {
		learningId = -1;
		ccs[id] = -1;
		APP->engine->updateParamHandle(&paramHandles[id], -1, 0, true);
		valueFilters[id].reset();
		updateMapLen();
		refreshParamHandleText(id);
	}

	void clearMaps() {
		learningId = -1;
		for (int id = 0; id < MAX_CHANNELS; id++) {
			ccs[id] = -1;
			APP->engine->updateParamHandle(&paramHandles[id], -1, 0, true);
			valueFilters[id].reset();
			refreshParamHandleText(id);
		}
		mapLen = 0;
	}

	void updateMapLen() {
		// Find last nonempty map
		int id;
		for (id = MAX_CHANNELS - 1; id >= 0; id--) {
			if (ccs[id] >= 0 || paramHandles[id].moduleId >= 0)
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
		if (!learnedCc)
			return;
		if (!learnedParam)
			return;
		// Reset learned state
		learnedCc = false;
		learnedParam = false;
		// Find next incomplete map
		while (++learningId < MAX_CHANNELS) {
			if (ccs[learningId] < 0 || paramHandles[learningId].moduleId < 0)
				return;
		}
		learningId = -1;
	}

	void enableLearn(int id) {
		if (learningId != id) {
			learningId = id;
			learnedCc = false;
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

	void refreshParamHandleText(int id) {
		std::string text;
		if (ccs[id] >= 0)
			text = string::f("CC%02d", ccs[id]);
		else
			text = "MIDI-Map";
		paramHandles[id].text = text;
	}

	json_t *dataToJson() override {
		json_t *rootJ = json_object();

		json_t *mapsJ = json_array();
		for (int id = 0; id < mapLen; id++) {
			json_t *mapJ = json_object();
			json_object_set_new(mapJ, "cc", json_integer(ccs[id]));
			json_object_set_new(mapJ, "moduleId", json_integer(paramHandles[id].moduleId));
			json_object_set_new(mapJ, "paramId", json_integer(paramHandles[id].paramId));
			json_array_append_new(mapsJ, mapJ);
		}
		json_object_set_new(rootJ, "maps", mapsJ);

		json_object_set_new(rootJ, "midiInput", midiInput.toJson());
		json_object_set_new(rootJ, "midiOutput", midiOutput.toJson());
		return rootJ;
	}

	void dataFromJson(json_t *rootJ) override {
		clearMaps();

		json_t *mapsJ = json_object_get(rootJ, "maps");
		if (mapsJ) {
			json_t *mapJ;
			size_t mapIndex;
			json_array_foreach(mapsJ, mapIndex, mapJ) {
				json_t *ccJ = json_object_get(mapJ, "cc");
				json_t *moduleIdJ = json_object_get(mapJ, "moduleId");
				json_t *paramIdJ = json_object_get(mapJ, "paramId");
				if (!(ccJ && moduleIdJ && paramIdJ))
					continue;
				if (mapIndex >= MAX_CHANNELS)
					continue;
				ccs[mapIndex] = json_integer_value(ccJ);
				APP->engine->updateParamHandle(&paramHandles[mapIndex], json_integer_value(moduleIdJ), json_integer_value(paramIdJ), false);
				refreshParamHandleText(mapIndex);
			}
		}

		updateMapLen();

		json_t *midiInputJ = json_object_get(rootJ, "midiInput");
		if (midiInputJ)
			midiInput.fromJson(midiInputJ);
		json_t *midiOutputJ = json_object_get(rootJ, "midiOutput");
		if (midiOutputJ)
			midiOutput.fromJson(midiOutputJ);
	}
};


struct MidiCatChoice : LedDisplayChoice {
	MidiCat *module;
	int id;
	int disableLearnFrames = -1;

	MidiCatChoice() {
		box.size = mm2px(Vec(0, 7.5));
		textOffset = Vec(10, 14.7);
	}

	void setModule(MidiCat *module) {
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
		text = "";
		if (module->ccs[id] >= 0) {
			text += string::f("CC%02d ", module->ccs[id]);
		}
		if (module->paramHandles[id].moduleId >= 0) {
			text += getParamName();
		}
		if (module->ccs[id] < 0 && module->paramHandles[id].moduleId < 0) {
			if (module->learningId == id) {
				text = "Mapping...";
			}
			else {
				text = "Unmapped";
			}
		}

		// Set text color
		if ((module->ccs[id] >= 0 && module->paramHandles[id].moduleId >= 0) || module->learningId == id) {
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


struct MidiCatDisplay : LedDisplay {
	MidiCat *module;
	ScrollWidget *scroll;
	MidiCatChoice *choices[MAX_CHANNELS];
	LedDisplaySeparator *separators[MAX_CHANNELS];

	void setModule(MidiCat *module) {
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

			MidiCatChoice *choice = createWidget<MidiCatChoice>(pos);
			choice->box.size.x = box.size.x;
			choice->id = id;
			choice->setModule(module);
			scroll->container->addChild(choice);
			choices[id] = choice;

			pos = choice->box.getBottomLeft();
		}
	}

	void step() override {
		if (module) {
			int mapLen = module->mapLen;
			for (int id = 0; id < MAX_CHANNELS; id++) {
				choices[id]->visible = (id < mapLen);
				separators[id]->visible = (id < mapLen);
			}
		}

		LedDisplay::step();
	}
};


struct MidiCatWidget : ModuleWidget {
	MidiCatWidget(MidiCat *module) {
		setModule(module);
		setPanel(APP->window->loadSvg(asset::plugin(pluginInstance, "res/MidiCat.svg")));

		addChild(createWidget<ScrewSilver>(Vec(RACK_GRID_WIDTH, 0)));
		addChild(createWidget<ScrewSilver>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, 0)));
		addChild(createWidget<ScrewSilver>(Vec(RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));
		addChild(createWidget<ScrewSilver>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));

		MidiWidget *midiInputWidget = createWidget<MidiWidget>(mm2px(Vec(3.41891, 12.f)));
		midiInputWidget->box.size = mm2px(Vec(43.999, 28));
		midiInputWidget->setMidiPort(module ? &module->midiInput : NULL);
		addChild(midiInputWidget);

        MidiWidget *midiOutputWidget = createWidget<MidiWidget>(mm2px(Vec(3.41891, 42.f)));
		midiOutputWidget->box.size = mm2px(Vec(43.999, 28));
		midiOutputWidget->setMidiPort(module ? &module->midiOutput : NULL);
		addChild(midiOutputWidget);

        MidiCatDisplay *mapWidget = createWidget<MidiCatDisplay>(mm2px(Vec(3.41891, 72.f)));
		mapWidget->box.size = mm2px(Vec(43.999, 47));
		mapWidget->setModule(module);
		addChild(mapWidget);
	}
};


Model *modelMidiCat = createModel<MidiCat, MidiCatWidget>("MidiCat");
