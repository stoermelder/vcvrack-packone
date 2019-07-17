#include "plugin.hpp"
#include "CVMapModule.hpp"
#include <thread>

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
	ParamHandleIndicator paramHandleIndicator[MAX_CHANNELS];

	/** Channel ID of the learning session */
	int learningId;
	/** Whether the CC has been set during the learning session */
	bool learnedCc;
	/** Whether the param has been set during the learning session */
	bool learnedParam;

	bool textScrolling = true;

	/** The value of each CC number */
	int8_t values[128];
	/** The smoothing processor (normalized between 0 and 1) of each channel */
	dsp::ExponentialFilter valueFilters[MAX_CHANNELS];

	dsp::ClockDivider indicatorDivider;

	/** Track last values */
	float lastValue[MAX_CHANNELS];
    float lastValue2[MAX_CHANNELS];
	/** Allow manual changes of target parameters */
	bool lockParameterChanges = false;

	MidiCat() {
		config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);
		for (int id = 0; id < MAX_CHANNELS; id++) {
			paramHandles[id].color = nvgRGB(0xff, 0xff, 0x40);
			paramHandleIndicator[id].handle = &paramHandles[id];
			APP->engine->addParamHandle(&paramHandles[id]);
		}
		indicatorDivider.setDivision(1024);
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

		if (indicatorDivider.process()) {
			float t = indicatorDivider.getDivision() * args.sampleTime;
			for (size_t i = 0; i < MAX_CHANNELS; i++) {
				if (paramHandles[i].moduleId >= 0)
					paramHandleIndicator[i].process(t);
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
		json_object_set_new(rootJ, "textScrolling", json_boolean(textScrolling));

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

		json_t *textScrollingJ = json_object_get(rootJ, "textScrolling");
		textScrolling = json_boolean_value(textScrollingJ);

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


struct MidiCatChoice : MapModuleChoice<MAX_CHANNELS, MidiCat> {
	MidiCatChoice() {
		textOffset = Vec(6.f, 14.7f);
	}

	std::string getTextPrefix() override {
		if (module->ccs[id] >= 0) {
			return string::f("CC%02d ", module->ccs[id]);
		}
		else if (module->paramHandles[id].moduleId >= 0) {
			return "CC.. ";
		}
		else {
			return "";
		}
	}
};


struct MidiCatDisplay : MapModuleDisplay<MAX_CHANNELS, MidiCat, MidiCatChoice> {
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

struct MidiCatMidiWidget : MidiWidget {
	void setMidiPort(midi::Port *port) {
		MidiWidget::setMidiPort(port);

		driverChoice->textOffset = Vec(6.f, 14.7f);
		driverChoice->box.size = mm2px(Vec(driverChoice->box.size.x, 7.5f));
		driverSeparator->box.pos = driverChoice->box.getBottomLeft();
		deviceChoice->textOffset = Vec(6.f, 14.7f);
		deviceChoice->box.size = mm2px(Vec(deviceChoice->box.size.x, 7.5f));
		deviceChoice->box.pos = driverChoice->box.getBottomLeft();
		deviceSeparator->box.pos = deviceChoice->box.getBottomLeft();
		channelChoice->textOffset = Vec(6.f, 14.7f);
		channelChoice->box.size = mm2px(Vec(channelChoice->box.size.x, 7.5f));
		channelChoice->box.pos = deviceChoice->box.getBottomLeft();
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

		MidiCatMidiWidget *midiInputWidget = createWidget<MidiCatMidiWidget>(Vec(10.0f, 36.4f));
		midiInputWidget->box.size = Vec(130.0f, 67.0f);
		midiInputWidget->setMidiPort(module ? &module->midiInput : NULL);
		addChild(midiInputWidget);

        MidiCatMidiWidget *midiOutputWidget = createWidget<MidiCatMidiWidget>(Vec(10.0f, 107.4f));
		midiOutputWidget->box.size = Vec(130.0f, 67.0f);
		midiOutputWidget->setMidiPort(module ? &module->midiOutput : NULL);
		addChild(midiOutputWidget);

        MidiCatDisplay *mapWidget = createWidget<MidiCatDisplay>(Vec(10.0f, 180.0f));
		mapWidget->box.size = Vec(130.0f, 174.4f);
		mapWidget->setModule(module);
		addChild(mapWidget);
	}

	void appendContextMenu(Menu *menu) override {
		MidiCat *module = dynamic_cast<MidiCat*>(this->module);
		assert(module);

        struct ManualItem : MenuItem {
            void onAction(const event::Action &e) override {
                std::thread t(system::openBrowser, "https://github.com/stoermelder/vcvrack-packone/blob/v1/docs/MidiCat.md");
                t.detach();
            }
        };

        menu->addChild(construct<ManualItem>(&MenuItem::text, "Module Manual"));
        menu->addChild(new MenuSeparator());

		struct TextScrollItem : MenuItem {
			MidiCat *module;

			void onAction(const event::Action &e) override {
				module->textScrolling ^= true;
			}

			void step() override {
				rightText = module->textScrolling ? "✔" : "";
				MenuItem::step();
			}
		};

		menu->addChild(construct<TextScrollItem>(&MenuItem::text, "Text scrolling", &TextScrollItem::module, module));
	}
};


Model *modelMidiCat = createModel<MidiCat, MidiCatWidget>("MidiCat");
