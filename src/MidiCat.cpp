#include "plugin.hpp"
#include "CVMapModule.hpp"
#include <osdialog.h>
#include <thread>

static const int MAX_CHANNELS = 128;
static const char PRESET_FILTERS[] = "VCV Rack module preset (.vcvm):vcvm";


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


enum MIDICAT_INMODE {
	DEFAULT = 0,
	LOCATE = 1
};

enum MIDICAT_NOTEMODE {
	MOMENTARY = 0,
	MOMENTARY_VELOCITY = 1,
	TOGGLE = 2
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
	MIDICAT_NOTEMODE notesMode[MAX_CHANNELS];

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
	int valuesCc[128];
	/** The value of each note number */
	int valuesNote[128];

	MIDICAT_INMODE inMode = MIDICAT_INMODE::DEFAULT;

	/** Track last values */
	int lastValueIn[MAX_CHANNELS];
	int lastValueInIndicate[MAX_CHANNELS];
	float lastValueOut[MAX_CHANNELS];

	dsp::ClockDivider loopDivider;
	dsp::ClockDivider indicatorDivider;

	MidiCat() {
		config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);
		for (int id = 0; id < MAX_CHANNELS; id++) {
			paramHandles[id].color = nvgRGB(0xff, 0xff, 0x40);
			paramHandleIndicator[id].handle = &paramHandles[id];
			APP->engine->addParamHandle(&paramHandles[id]);
		}
		loopDivider.setDivision(64);
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
			lastValueIn[i] = -1;
			lastValueOut[i] = -1;
			notesMode[i] = MIDICAT_NOTEMODE::MOMENTARY;
		}
		midiInput.reset();
		midiOutput.reset();
		midiOutput.midi::Output::reset();
	}

	void process(const ProcessArgs &args) override {
		midi::Message msg;
		bool changed = false;
		while (midiInput.shift(&msg)) {
			changed = changed || processMessage(msg);
		}

		// Only step channels when some midi event has been received. Additionally
		// step channels for parameter changes made manually every 64th loop. Notice
		// that midi allows about 1000 messages per second, to checking for changes more often
		// won't lead to higher precision on midi output.
		if (changed || loopDivider.process()) {
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

				switch (inMode) {
					case MIDICAT_INMODE::DEFAULT: {
						// Check if CC value has been set
						if (cc >= 0 && valuesCc[cc] >= 0)
						{
							if (lastValueIn[id] != valuesCc[cc]) {
								lastValueIn[id] = valuesCc[cc];
								float v = rescale(valuesCc[cc], 0.f, 127.f, paramQuantity->getMinValue(), paramQuantity->getMaxValue());
								paramQuantity->setValue(v);
							}
						}

						// Check if note value has been set
						if (note >= 0 && valuesNote[note] >= 0)
						{
							int t = -1;
							switch (notesMode[id]) {
								case MIDICAT_NOTEMODE::MOMENTARY:
									if (lastValueIn[id] != valuesNote[note]) {
										t = valuesNote[note];
										if (t > 0) t = 127;
										lastValueIn[id] = valuesNote[note];
									} 
									break;
								case MIDICAT_NOTEMODE::MOMENTARY_VELOCITY:
									if (lastValueIn[id] != valuesNote[note]) {
										t = valuesNote[note];
										lastValueIn[id] = valuesNote[note];
									}
									break;
								case MIDICAT_NOTEMODE::TOGGLE:
									if (valuesNote[note] == 127 && (lastValueIn[id] == -1 || lastValueIn[id] >= 0)) {
										t = 127;
										lastValueIn[id] = -2;
									} 
									else if (valuesNote[note] == 0 && lastValueIn[id] == -2) {
										t = 127;
										lastValueIn[id] = -3;
									}
									else if (valuesNote[note] == 127 && lastValueIn[id] == -3) {
										t = 0;
										lastValueIn[id] = -4;
									}
									else if (valuesNote[note] == 0 && lastValueIn[id] == -4) {
										t = 0;
										lastValueIn[id] = -1;
									}
									break;
							}

							if (t >= 0) {
								float v = rescale(t, 0.f, 127.f, paramQuantity->getMinValue(), paramQuantity->getMaxValue());
								paramQuantity->setValue(v);
							}
						}

						// Midi feedback
						float v = paramQuantity->getValue();
						if (lastValueOut[id] != v) {
							lastValueOut[id] = v;
							v = rescale(v, paramQuantity->getMinValue(), paramQuantity->getMaxValue(), 0.f, 127.f);
							if (cc >= 0)
								midiOutput.setValue(v, cc);
							if (note >= 0)
								midiOutput.setGate(v, note);
						}
					} break;

					case MIDICAT_INMODE::LOCATE: {
						bool indicate = false;
						if ((cc >= 0 && valuesCc[cc] >= 0) && lastValueInIndicate[id] != valuesCc[cc]) {
							lastValueInIndicate[id] = valuesCc[cc];
							indicate = true;
						}
						if ((note >= 0 && valuesNote[note] >= 0) && lastValueInIndicate[id] != valuesNote[note]) {
							lastValueInIndicate[id] = valuesNote[note];
							indicate = true;
						}
						if (indicate) {
							ModuleWidget *mw = APP->scene->rack->getModule(paramQuantity->module->id);
							paramHandleIndicator[id].indicate(mw);
						}
					} break;
				}
			}
		}

		if (indicatorDivider.process()) {
			float t = indicatorDivider.getDivision() * args.sampleTime;
			for (int i = 0; i < mapLen; i++) {
				if (paramHandles[i].moduleId >= 0)
					paramHandleIndicator[i].process(t);
			}
		}
	}

	void setMode(MIDICAT_INMODE inMode) {
		this->inMode = inMode;
		switch (inMode) {
			case MIDICAT_INMODE::LOCATE:
				for (int i = 0; i < MAX_CHANNELS; i++) 
					lastValueInIndicate[i] = lastValueIn[i];
				break;
			default:
				break;
		}
	}

	bool processMessage(midi::Message msg) {
		switch (msg.getStatus()) {
			// cc
			case 0xb: {
				return processCC(msg);
			}
			// note off
			case 0x8: {
				return processNoteRelease(msg);
			}
			// note on
			case 0x9: {
				if (msg.getValue() > 0) {
					return processNotePress(msg);
				}
				else {
					// Many stupid keyboards send a "note on" command with 0 velocity to mean "note release"
					return processNoteRelease(msg);
				}
			} 
			default: {
				return false;
			}
		}
	}

	bool processCC(midi::Message msg) {
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
		bool changed = valuesCc[cc] != value;
		valuesCc[cc] = value;
		return changed;
	}

	bool processNotePress(midi::Message msg) {
		uint8_t note = msg.getNote();
		uint8_t vel = msg.getValue();
		// Learn
		if (learningId >= 0) {
			ccs[learningId] = -1;
			notes[learningId] = note;
			notesMode[learningId] = MIDICAT_NOTEMODE::MOMENTARY;
			learnedNote = true;
			commitLearn();
			updateMapLen();
			refreshParamHandleText(learningId);
		}
		bool changed = valuesNote[note] != vel;
		valuesNote[note] = vel;
		return changed;
	}

	bool processNoteRelease(midi::Message msg) {
		uint8_t note = msg.getNote();
		bool changed = valuesNote[note] != 0;
		valuesNote[note] = 0;
		return changed;
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
			json_object_set_new(mapJ, "noteMode", json_integer(notesMode[id]));
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
				json_t *noteModeJ = json_object_get(mapJ, "noteMode");
				json_t *moduleIdJ = json_object_get(mapJ, "moduleId");
				json_t *paramIdJ = json_object_get(mapJ, "paramId");
				if (!((ccJ || noteJ) && moduleIdJ && paramIdJ))
					continue;
				if (mapIndex >= MAX_CHANNELS)
					continue;
				ccs[mapIndex] = json_integer_value(ccJ);
				notes[mapIndex] = noteJ ? json_integer_value(noteJ) : -1;
				notesMode[mapIndex] = (MIDICAT_NOTEMODE)json_integer_value(noteModeJ);
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


struct MidiCatInModeMenuItem : MenuItem {
	MidiCatInModeMenuItem() {
		rightText = RIGHT_ARROW;
	}

	struct MidiCatInModeItem : MenuItem {
		MidiCat *module;
		MIDICAT_INMODE inMode;

		void onAction(const event::Action &e) override {
			module->setMode(inMode);
		}

		void step() override {
			rightText = module->inMode == inMode ? "✔" : "";
			MenuItem::step();
		}
	};

	MidiCat *module;
	Menu *createChildMenu() override {
		Menu *menu = new Menu;
		menu->addChild(construct<MidiCatInModeItem>(&MenuItem::text, "Default", &MidiCatInModeItem::module, module, &MidiCatInModeItem::inMode, MIDICAT_INMODE::DEFAULT));
		menu->addChild(construct<MidiCatInModeItem>(&MenuItem::text, "Locate and indicate", &MidiCatInModeItem::module, module, &MidiCatInModeItem::inMode, MIDICAT_INMODE::LOCATE));
		return menu;
	}
};

struct MidiCatChoice : MapModuleChoice<MAX_CHANNELS, MidiCat> {
	MidiCatChoice() {
		textOffset = Vec(6.f, 14.7f);
		color = nvgRGB(0xf0, 0xf0, 0xf0);
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

			struct NoteModeMenuItem : MenuItem {
				MidiCat *module;
				int id;

				NoteModeMenuItem() {
					rightText = RIGHT_ARROW;
				}

				struct NoteModeItem : MenuItem {
					MidiCat *module;
					int id;
					MIDICAT_NOTEMODE noteMode;

					void onAction(const event::Action &e) override {
						module->notesMode[id] = noteMode;
					}

					void step() override {
						rightText = module->notesMode[id] == noteMode ? "✔" : "";
						MenuItem::step();
					}
				};

				Menu *createChildMenu() override {
					Menu *menu = new Menu;
					menu->addChild(construct<NoteModeItem>(&MenuItem::text, "Momentary", &NoteModeItem::module, module, &NoteModeItem::id, id, &NoteModeItem::noteMode, MIDICAT_NOTEMODE::MOMENTARY));
					menu->addChild(construct<NoteModeItem>(&MenuItem::text, "Momentary + Velocity", &NoteModeItem::module, module, &NoteModeItem::id, id, &NoteModeItem::noteMode, MIDICAT_NOTEMODE::MOMENTARY_VELOCITY));
					menu->addChild(construct<NoteModeItem>(&MenuItem::text, "Toggle", &NoteModeItem::module, module, &NoteModeItem::id, id, &NoteModeItem::noteMode, MIDICAT_NOTEMODE::TOGGLE));
					return menu;
				}
			};

			menu->addChild(construct<NoteModeMenuItem>(&MenuItem::text, "Note mode", &NoteModeMenuItem::module, module, &NoteModeMenuItem::id, id));
		}
	}
};

struct MidiCatTextScrollItem : MenuItem {
	MidiCat *module;

	void onAction(const event::Action &e) override {
		module->textScrolling ^= true;
	}

	void step() override {
		rightText = module->textScrolling ? "✔" : "";
		MenuItem::step();
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
		driverChoice->color = nvgRGB(0xf0, 0xf0, 0xf0);
		driverSeparator->box.pos = driverChoice->box.getBottomLeft();
		deviceChoice->textOffset = Vec(6.f, 14.7f);
		deviceChoice->box.size = mm2px(Vec(deviceChoice->box.size.x, 7.5f));
		deviceChoice->box.pos = driverChoice->box.getBottomLeft();
		deviceChoice->color = nvgRGB(0xf0, 0xf0, 0xf0);
		deviceSeparator->box.pos = deviceChoice->box.getBottomLeft();
		channelChoice->textOffset = Vec(6.f, 14.7f);
		channelChoice->box.size = mm2px(Vec(channelChoice->box.size.x, 7.5f));
		channelChoice->box.pos = deviceChoice->box.getBottomLeft();
		channelChoice->color = nvgRGB(0xf0, 0xf0, 0xf0);
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

	void loadMidiMapPreset_dialog() {
		osdialog_filters *filters = osdialog_filters_parse(PRESET_FILTERS);
		DEFER({
			osdialog_filters_free(filters);
		});

		char *path = osdialog_file(OSDIALOG_OPEN, "", NULL, filters);
		if (!path) {
			// No path selected
			return;
		}
		DEFER({
			free(path);
		});

		loadMidiMapPreset_action(path);
	}

	void loadMidiMapPreset_action(std::string filename) {
		INFO("Loading preset %s", filename.c_str());

		FILE *file = fopen(filename.c_str(), "r");
		if (!file) {
			WARN("Could not load patch file %s", filename.c_str());
			return;
		}
		DEFER({
			fclose(file);
		});

		json_error_t error;
		json_t *moduleJ = json_loadf(file, 0, &error);
		if (!moduleJ) {
			std::string message = string::f("File is not a valid patch file. JSON parsing error at %s %d:%d %s", error.source, error.line, error.column, error.text);
			osdialog_message(OSDIALOG_WARNING, OSDIALOG_OK, message.c_str());
			return;
		}
		DEFER({
			json_decref(moduleJ);
		});

		if (!loadMidiMapPreset_convert(moduleJ))
			return;

		// history::ModuleChange
		history::ModuleChange *h = new history::ModuleChange;
		h->name = "load module preset";
		h->moduleId = module->id;
		h->oldModuleJ = toJson();

		module->fromJson(moduleJ);

		h->newModuleJ = toJson();
		APP->history->push(h);
	}

	bool loadMidiMapPreset_convert(json_t *moduleJ) {
		std::string pluginSlug = json_string_value(json_object_get(moduleJ, "plugin"));
		std::string modelSlug = json_string_value(json_object_get(moduleJ, "model"));

		// Only handle MIDI-Map
		if (!(pluginSlug == "Core" && modelSlug == "MIDI-Map"))
			return false;

		json_object_set_new(moduleJ, "plugin", json_string(module->model->plugin->slug.c_str()));
		json_object_set_new(moduleJ, "model", json_string(module->model->slug.c_str()));
		json_t *dataJ = json_object_get(moduleJ, "data");
		json_object_set(dataJ, "midiInput", json_object_get(dataJ, "midi"));
		return true;
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

		menu->addChild(construct<MidiCatInModeMenuItem>(&MenuItem::text, "Mode", &MidiCatInModeMenuItem::module, module));
		menu->addChild(construct<MidiCatTextScrollItem>(&MenuItem::text, "Text scrolling", &MidiCatTextScrollItem::module, module));
		menu->addChild(new MenuSeparator());

		struct MidiMapImportItem : MenuItem {
			MidiCatWidget *moduleWidget;

			void onAction(const event::Action &e) override {
				moduleWidget->loadMidiMapPreset_dialog();
			}
		};

		menu->addChild(construct<MidiMapImportItem>(&MenuItem::text, "Import MIDI-MAP preset", &MidiMapImportItem::moduleWidget, this));
	}
};


Model *modelMidiCat = createModel<MidiCat, MidiCatWidget>("MidiCat");