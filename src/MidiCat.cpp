#include "plugin.hpp"
#include "CVMapModule.hpp"
#include <thread>

static const int MAX_CHANNELS = 128;

struct MidiCatOutput : midi::Output {
	int lastValues[128];
	bool lastGates[128];

	MidiCatOutput() {
		reset();
	}

	void reset() {
		for (int n = 0; n < 128; n++) {
			lastValues[n] = -1;
			lastGates[n] = false;
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

	void setGate(int vel, int note) {
		if (vel > 0 && !lastGates[note]) {
			// Note on
			midi::Message m;
			m.setStatus(0x9);
			m.setNote(note);
			m.setValue(vel);
			sendMessage(m);
		}
		else if (vel == 0 && lastGates[note]) {
			// Note off
			midi::Message m;
			m.setStatus(0x8);
			m.setNote(note);
			m.setValue(0);
			sendMessage(m);
		}
		lastGates[note] = vel > 0;
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

	/** [Stored to Json] */
	midi::InputQueue midiInput;
	/** [Stored to Json] */
	MidiCatOutput midiOutput;

	/** Number of maps */
	int mapLen = 0;
	/** [Stored to Json] The mapped CC number of each channel */
	int ccs[MAX_CHANNELS];
	/** [Stored to Json] The mapped note number of each channel */
	int notes[MAX_CHANNELS];
	/** [Stored to Json] Use the velocity value of each channel when notes are used */
	bool notesVel[MAX_CHANNELS];

	/** The mapped param handle of each channel */
	ParamHandle paramHandles[MAX_CHANNELS];
	ParamHandleIndicator paramHandleIndicator[MAX_CHANNELS];

	/** Channel ID of the learning session */
	int learningId;
	/** Whether the CC has been set during the learning session */
	bool learnedCc;
	/** Whether the note has been set during the learning session */
	bool learnedNote;
	/** Whether the param has been set during the learning session */
	bool learnedParam;

	/** [Stored to Json] */
	bool textScrolling = true;

	/** The value of each CC number */
	int8_t valuesCc[128];
	/** The value of each note number */
	int8_t valuesNote[128];

	/** Track last values */
	float lastValue[MAX_CHANNELS];

	/** [Stored to Json] Allow manual changes of target parameters */
	bool lockParameterChanges = false;

	dsp::ClockDivider indicatorDivider;

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
		learnedNote = false;
		learnedParam = false;
		clearMaps();
		mapLen = 1;
		for (int i = 0; i < 128; i++) {
			valuesCc[i] = -1;
			valuesNote[i] = -1;
		}
		for (int i = 0; i < MAX_CHANNELS; i++) {
			lastValue[i] = -1;
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
			int note = notes[id];
			if (cc < 0 && note < 0)
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
			if (cc >= 0 && valuesCc[cc] >= 0)
			{
				float v = rescale(valuesCc[cc], 0, 127, 0.f, 1.f);

				if (lockParameterChanges || lastValue[id] != v) {
					paramQuantity->setScaledValue(v);
					lastValue[id] = v;
				}
			}

			// Check if note value has been set
			if (note >= 0 && valuesNote[note] >= 0)
			{
				int t = valuesNote[note];
				if (t > 0 && !notesVel[id]) t = 127;
				float v = rescale(t, 0, 127, 0.f, 1.f);

				if (lockParameterChanges || lastValue[id] != v) {
					paramQuantity->setScaledValue(v);
					lastValue[id] = v;
				}
			}

			// Midi feedback
			float v = paramQuantity->getScaledValue();
			v = rescale(v, 0.f, 1.f, 0, 127);
			if (cc >= 0)
				midiOutput.setValue(v, cc);
			if (note >= 0)
				midiOutput.setGate(v, note);
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
			// note off
			case 0x8: {
				processNoteRelease(msg);
			} break;
			// note on
			case 0x9: {
				if (msg.getValue() > 0) {
					processNotePress(msg);
				}
				else {
					// Many stupid keyboards send a "note on" command with 0 velocity to mean "note release"
					processNoteRelease(msg);
				}
			} break;
			default: break;
		}
	}

	void processCC(midi::Message msg) {
		uint8_t cc = msg.getNote();
		uint8_t value = msg.getValue();
		// Learn
		if (learningId >= 0 && valuesCc[cc] != value) {
			ccs[learningId] = cc;
			notes[learningId] = -1;
			learnedCc = true;
			commitLearn();
			updateMapLen();
			refreshParamHandleText(learningId);
		}
		valuesCc[cc] = value;
	}

	void processNotePress(midi::Message msg) {
		uint8_t note = msg.getNote();
		uint8_t vel = msg.getValue();
		// Learn
		if (learningId >= 0) {
			ccs[learningId] = -1;
			notes[learningId] = note;
			notesVel[learningId] = false;
			learnedNote = true;
			commitLearn();
			updateMapLen();
			refreshParamHandleText(learningId);
		}
		valuesNote[note] = vel;
	}

	void processNoteRelease(midi::Message msg) {
		uint8_t note = msg.getNote();
		valuesNote[note] = 0;
	}

	void clearMap(int id) {
		learningId = -1;
		ccs[id] = -1;
		notes[id] = -1;
		APP->engine->updateParamHandle(&paramHandles[id], -1, 0, true);
		updateMapLen();
		refreshParamHandleText(id);
	}

	void clearMaps() {
		learningId = -1;
		for (int id = 0; id < MAX_CHANNELS; id++) {
			ccs[id] = -1;
			notes[id] = -1;
			APP->engine->updateParamHandle(&paramHandles[id], -1, 0, true);
			refreshParamHandleText(id);
		}
		mapLen = 0;
	}

	void updateMapLen() {
		// Find last nonempty map
		int id;
		for (id = MAX_CHANNELS - 1; id >= 0; id--) {
			if (ccs[id] >= 0 || notes[id] >= 0 || paramHandles[id].moduleId >= 0)
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
		if (!learnedCc && !learnedNote)
			return;
		if (!learnedParam)
			return;
		// Reset learned state
		learnedCc = false;
		learnedNote = false;
		learnedParam = false;
		// Find next incomplete map
		while (++learningId < MAX_CHANNELS) {
			if ((ccs[learningId] < 0 && notes[learningId] < 0) || paramHandles[learningId].moduleId < 0)
				return;
		}
		learningId = -1;
	}

	void enableLearn(int id) {
		if (learningId != id) {
			learningId = id;
			learnedCc = false;
			learnedNote = false;
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
		std::string text = "MIDI-CAT";
		if (ccs[id] >= 0) {
			text += string::f(" cc%02d", ccs[id]);
		}
		if (notes[id] >= 0) {
			static const char *noteNames[] = {
				"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"
			};
			int oct = notes[id] / 12 - 1;
			int semi = notes[id] % 12;
			text += string::f(" note %s%d", noteNames[semi], oct);
		}
		paramHandles[id].text = text;
	}

	json_t *dataToJson() override {
		json_t *rootJ = json_object();
		json_object_set_new(rootJ, "textScrolling", json_boolean(textScrolling));

		json_t *mapsJ = json_array();
		for (int id = 0; id < mapLen; id++) {
			json_t *mapJ = json_object();
			json_object_set_new(mapJ, "cc", json_integer(ccs[id]));
			json_object_set_new(mapJ, "note", json_integer(notes[id]));
			json_object_set_new(mapJ, "noteVel", json_boolean(notesVel[id]));
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
				json_t *noteJ = json_object_get(mapJ, "note");
				json_t *noteVelJ = json_object_get(mapJ, "noteVel");
				json_t *moduleIdJ = json_object_get(mapJ, "moduleId");
				json_t *paramIdJ = json_object_get(mapJ, "paramId");
				if (!(ccJ && noteJ && moduleIdJ && paramIdJ))
					continue;
				if (mapIndex >= MAX_CHANNELS)
					continue;
				ccs[mapIndex] = json_integer_value(ccJ);
				notes[mapIndex] = json_integer_value(noteJ);
				notesVel[mapIndex] = json_boolean_value(noteVelJ);
				APP->engine->updateParamHandle(&paramHandles[mapIndex], json_integer_value(moduleIdJ), json_integer_value(paramIdJ), true);
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
		color = componentlibrary::SCHEME_WHITE;
	}

	std::string getTextPrefix() override {
		if (module->ccs[id] >= 0) {
			return string::f("cc%02d ", module->ccs[id]);
		}
		else if (module->notes[id] >= 0) {
			static const char *noteNames[] = {
				" C", "C#", " D", "D#", " E", " F", "F#", " G", "G#", " A", "A#", " B"
			};
			int oct = module->notes[id] / 12 - 1;
			int semi = module->notes[id] % 12;
			return string::f(" %s%d ", noteNames[semi], oct);
		}
		else if (module->paramHandles[id].moduleId >= 0) {
			return ".... ";
		}
		else {
			return "";
		}
	}

	void appendContextMenu(Menu *menu) override {
		if (module->notes[id] >= 0) {
			menu->addChild(new MenuSeparator());

			struct VelocityItem : MenuItem {
				MidiCat *module;
				int id;

				void onAction(const event::Action &e) override {
					module->notesVel[id] ^= true;
				}

				void step() override {
					rightText = module->notesVel[id] ? "✔" : "";
					MenuItem::step();
				}
			};

			menu->addChild(construct<VelocityItem>(&MenuItem::text, "Note velocity", &VelocityItem::module, module, &VelocityItem::id, id));
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
		driverChoice->color = componentlibrary::SCHEME_WHITE;
		driverSeparator->box.pos = driverChoice->box.getBottomLeft();
		deviceChoice->textOffset = Vec(6.f, 14.7f);
		deviceChoice->box.size = mm2px(Vec(deviceChoice->box.size.x, 7.5f));
		deviceChoice->box.pos = driverChoice->box.getBottomLeft();
		deviceChoice->color = componentlibrary::SCHEME_WHITE;
		deviceSeparator->box.pos = deviceChoice->box.getBottomLeft();
		channelChoice->textOffset = Vec(6.f, 14.7f);
		channelChoice->box.size = mm2px(Vec(channelChoice->box.size.x, 7.5f));
		channelChoice->box.pos = deviceChoice->box.getBottomLeft();
		channelChoice->color = componentlibrary::SCHEME_WHITE;
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