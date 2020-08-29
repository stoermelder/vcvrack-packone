#include "plugin.hpp"
#include "MidiCat.hpp"
#include "MapModuleBase.hpp"
#include "StripIdFixModule.hpp"
#include <osdialog.h>

namespace StoermelderPackOne {
namespace MidiCat {

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

	void setGate(int vel, int note, bool noteOffVelocityZero) {
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
			m.setStatus(noteOffVelocityZero ? 0x9 : 0x8);
			m.setNote(note);
			m.setValue(0);
			sendMessage(m);
		}
		lastGates[note] = vel > 0;
	}
};


enum MIDIMODE {
	MIDIMODE_DEFAULT = 0,
	MIDIMODE_LOCATE = 1
};


struct MidiCatParam {
	ParamQuantity* paramQuantity;
	float min = 0.f;
	float max = 1.f;

	dsp::ExponentialSlewLimiter filter;
	bool filterInitialized;
	float filterSlew;
	int valueIn;
	float value;
	float valueOut;

	MidiCatParam() {
		reset();
	}

	void reset() {
		filter.reset();
		filterInitialized = false;
		filterSlew = 0.f;
		valueIn = -1;
		value = -1.f;
		valueOut = std::numeric_limits<float>::infinity();
		min = 0.f;
		max = 1.f;
	}

	void setSlew(float slew) {
		filterSlew = slew;
		float s = (1.f / slew) * 10.f;
		filter.setRiseFall(s, s);
	}
	float getSlew() {
		return filterSlew;
	}

	void setMin(float v) {
		min = v;
		if (valueIn != -1) setValue(valueIn);
	}
	float getMin() {
		return min;
	}

	void setMax(float v) {
		max = v;
		if (valueIn != -1) setValue(valueIn);
	}
	float getMax() {
		return max;
	}

	void setValue(int i) {
		float f = rescale(float(i), 0.f, 127.f, min, max);
		f = clamp(f, 0.f, 1.f);
		f = rescale(f, 0.f, 1.f, paramQuantity->getMinValue(), paramQuantity->getMaxValue());
		valueIn = i;
		value = f;
	}

	void process(float sampleTime = -1.f) {
		if (valueOut == std::numeric_limits<float>::infinity()) return;
		// Set filter from param value if filter is uninitialized
		if (!filterInitialized) {
			filter.out = value = paramQuantity->getValue();
			filterInitialized = true;
		}
		float f = filterSlew > 0.f && sampleTime > 0.f ? filter.process(sampleTime, value) : value;
		if (valueOut != f) {
			paramQuantity->setValue(f);
			valueOut = f;
		}
	}

	int getValue() {
		float f = paramQuantity->getValue();
		if (isNear(valueOut, f)) return valueIn;
		if (valueOut == std::numeric_limits<float>::infinity()) value = valueOut = f;
		f = rescale(f, paramQuantity->getMinValue(), paramQuantity->getMaxValue(), 0.f, 1.f);
		f = rescale(f, min, max, 0.f, 127.f);
		f = clamp(f, 0.f, 127.f);
		int i = std::round(f);
		if (valueIn == -1) valueIn = i;
		return i;
	}
}; // MidiCatParam


struct MidiCatModule : Module, StripIdFixModule {
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

	/** [Stored to JSON] */
	int panelTheme = 0;

	/** Number of maps */
	int mapLen = 0;
	/** [Stored to Json] The mapped CC number of each channel */
	int ccs[MAX_CHANNELS];
	/** [Stored to Json] */
	CCMODE ccsMode[MAX_CHANNELS];
	/** [Stored to Json] The mapped note number of each channel */
	int notes[MAX_CHANNELS];
	/** [Stored to Json] Use the velocity value of each channel when notes are used */
	NOTEMODE notesMode[MAX_CHANNELS];
	/** [Stored to JSON] */
	int midiOptions[MAX_CHANNELS];

	/** [Stored to Json] The mapped param handle of each channel */
	ParamHandle paramHandles[MAX_CHANNELS];
	ParamHandleIndicator paramHandleIndicator[MAX_CHANNELS];

	/** Channel ID of the learning session */
	int learningId;
	/** Whether the CC has been set during the learning session */
	bool learnedCc;
	int learnedCcLast = -1;
	/** Whether the note has been set during the learning session */
	bool learnedNote;
	int learnedNoteLast = -1;
	/** Whether the param has been set during the learning session */
	bool learnedParam;

	/** [Stored to Json] */
	bool textScrolling = true;
	/** [Stored to Json] */
	std::string textLabel[MAX_CHANNELS];
	/** [Stored to Json] */
	bool locked;

	NVGcolor mappingIndicatorColor = nvgRGB(0xff, 0xff, 0x40);
	/** [Stored to JSON] */
	bool mappingIndicatorHidden = false;

	/** The value of each CC number */
	int valuesCc[128];
	/** The value of each note number */
	int valuesNote[128];

	MIDIMODE midiMode = MIDIMODE::MIDIMODE_DEFAULT;

	/** Track last values */
	int lastValueIn[MAX_CHANNELS];
	int lastValueInIndicate[MAX_CHANNELS];
	int lastValueOut[MAX_CHANNELS];

	MidiCatParam midiParam[MAX_CHANNELS];

	dsp::ClockDivider processDivider;
	/** [Stored to JSON] */
	int processDivision;
	dsp::ClockDivider indicatorDivider;

	// Pointer of the MEM-expander's attribute
	std::map<std::pair<std::string, std::string>, MemModule*>* memStorage = NULL;
	Module* mem = NULL;

	MidiCatModule() {
		panelTheme = pluginSettings.panelThemeDefault;
		config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);
		for (int id = 0; id < MAX_CHANNELS; id++) {
			paramHandleIndicator[id].color = mappingIndicatorColor;
			paramHandleIndicator[id].handle = &paramHandles[id];
			APP->engine->addParamHandle(&paramHandles[id]);
		}
		indicatorDivider.setDivision(2048);
		onReset();
	}

	~MidiCatModule() {
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
			ccsMode[i] = CCMODE::CCMODE_DIRECT;
			notesMode[i] = NOTEMODE::NOTEMODE_MOMENTARY;
			textLabel[i] = "";
			midiOptions[i] = 0;
			midiParam[i].reset();
		}
		locked = false;
		midiInput.reset();
		midiOutput.reset();
		midiOutput.midi::Output::reset();
		processDivision = 64;
		processDivider.setDivision(processDivision);
		processDivider.reset();
	}

	void process(const ProcessArgs &args) override {
		midi::Message msg;
		bool changed = false;
		while (midiInput.shift(&msg)) {
			bool r = processMessage(msg);
			changed = changed || r;
		}

		// Only step channels when some midi event has been received. Additionally
		// step channels for parameter changes made manually every 128th loop. Notice
		// that midi allows about 1000 messages per second, so checking for changes more often
		// won't lead to higher precision on midi output.
		if (processDivider.process() || changed) {
			// Step channels
			for (int id = 0; id < mapLen; id++) {
				int cc = ccs[id];
				int note = notes[id];
				if (cc < 0 && note < 0)
					continue;

				// Get Module
				Module* module = paramHandles[id].module;
				if (!module)
					continue;

				// Get ParamQuantity
				int paramId = paramHandles[id].paramId;
				ParamQuantity* paramQuantity = module->paramQuantities[paramId];
				if (!paramQuantity)
					continue;

				if (!paramQuantity->isBounded())
					continue;

				switch (midiMode) {
					case MIDIMODE::MIDIMODE_DEFAULT: {
						midiParam[id].paramQuantity = paramQuantity;

						// Check if CC value has been set
						if (cc >= 0 && valuesCc[cc] >= 0) {
							int t = -1;
							switch (ccsMode[id]) {
								case CCMODE_DIRECT:
									if (lastValueIn[id] != valuesCc[cc]) {
										lastValueIn[id] = valuesCc[cc];
										t = valuesCc[cc];
									}
									break;
								case CCMODE_PICKUP1:
									if (lastValueIn[id] != valuesCc[cc]) {
										int p = (int)rescale(paramQuantity->getValue(), paramQuantity->getMinValue(), paramQuantity->getMaxValue(), 0.f, 127.f);
										if (p - 3 <= lastValueIn[id] && lastValueIn[id] <= p + 3) {
											t = valuesCc[cc];
										}
										lastValueIn[id] = valuesCc[cc];
									}
									break;
								case CCMODE_PICKUP2:
									if (lastValueIn[id] != valuesCc[cc]) {
										int p = (int)rescale(paramQuantity->getValue(), paramQuantity->getMinValue(), paramQuantity->getMaxValue(), 0.f, 127.f);
										if (p - 3 <= lastValueIn[id] && lastValueIn[id] <= p + 3 && p - 7 <= valuesCc[cc] && valuesCc[cc] <= p + 7) {
											t = valuesCc[cc];
										}
										lastValueIn[id] = valuesCc[cc];
									}
									break;
							}

							if (t >= 0) {
								//float v = rescale(t, 0.f, 127.f, paramQuantity->getMinValue(), paramQuantity->getMaxValue());
								//v = valueFilters[id].process(args.sampleTime * processDivider.getDivision(), v);
								//paramQuantity->setValue(v);
								midiParam[id].setValue(t);
							}
						}

						// Check if note value has been set
						if (note >= 0 && valuesNote[note] >= 0) {
							int t = -1;
							switch (notesMode[id]) {
								case NOTEMODE::NOTEMODE_MOMENTARY:
									if (lastValueIn[id] != valuesNote[note]) {
										t = valuesNote[note];
										if (t > 0) t = 127;
										lastValueIn[id] = valuesNote[note];
									} 
									break;
								case NOTEMODE::NOTEMODE_MOMENTARY_VEL:
									if (lastValueIn[id] != valuesNote[note]) {
										t = valuesNote[note];
										lastValueIn[id] = valuesNote[note];
									}
									break;
								case NOTEMODE::NOTEMODE_TOGGLE:
									if (valuesNote[note] > 0 && (lastValueIn[id] == -1 || lastValueIn[id] >= 0)) {
										t = 127;
										lastValueIn[id] = -2;
									} 
									else if (valuesNote[note] == 0 && lastValueIn[id] == -2) {
										t = 127;
										lastValueIn[id] = -3;
									}
									else if (valuesNote[note] > 0 && lastValueIn[id] == -3) {
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
								//float v = rescale(t, 0.f, 127.f, paramQuantity->getMinValue(), paramQuantity->getMaxValue());
								// Do not use filters on notes
								//paramQuantity->setValue(v);
								midiParam[id].setValue(t);
							}
						}

						midiParam[id].process(args.sampleTime * processDivider.getDivision());

						// Midi feedback
						//float v = paramQuantity->getValue();
						int v = midiParam[id].getValue();

						if (lastValueOut[id] != v) {
							lastValueOut[id] = v;
							//v = rescale(v, paramQuantity->getMinValue(), paramQuantity->getMaxValue(), 0.f, 127.f);
							if (cc >= 0 && ccsMode[id] == CCMODE_DIRECT)
								lastValueIn[id] = valuesCc[cc] = v;
							if (cc >= 0)
								midiOutput.setValue(v, cc);
							if (note >= 0)
								midiOutput.setGate(v, note, (midiOptions[id] >> MIDIOPTION_VELZERO_BIT) & 1U);
						}
					} break;

					case MIDIMODE::MIDIMODE_LOCATE: {
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
			processDivider.setDivision(processDivision);
		}

		if (indicatorDivider.process()) {
			float t = indicatorDivider.getDivision() * args.sampleTime;
			for (int i = 0; i < mapLen; i++) {
				paramHandleIndicator[i].color = mappingIndicatorHidden ? color::BLACK_TRANSPARENT : mappingIndicatorColor;
				if (paramHandles[i].moduleId >= 0) {
					paramHandleIndicator[i].process(t, learningId == i);
				}
			}
		}


		Module* exp = rightExpander.module;
		if (exp && exp->model->plugin->slug == "Stoermelder-P1" && exp->model->slug == "MidiCatEx") {
			memStorage = reinterpret_cast<std::map<std::pair<std::string, std::string>, MemModule*>*>(exp->leftExpander.consumerMessage);
			mem = exp;
		}
		else {
			memStorage = NULL;
			mem = NULL;
		}
	}

	void setMode(MIDIMODE midiMode) {
		if (this->midiMode == midiMode)
			return;
		this->midiMode = midiMode;
		switch (midiMode) {
			case MIDIMODE::MIDIMODE_LOCATE:
				for (int i = 0; i < MAX_CHANNELS; i++) 
					lastValueInIndicate[i] = std::max(0, lastValueIn[i]);
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
					// Many keyboards send a "note on" command with 0 velocity to mean "note release"
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
		if (learningId >= 0 && learnedCcLast != cc && valuesCc[cc] != value) {
			ccs[learningId] = cc;
			ccsMode[learningId] = CCMODE::CCMODE_DIRECT;
			notes[learningId] = -1;
			learnedCc = true;
			learnedCcLast = cc;
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
		if (learningId >= 0 && learnedNoteLast != note) {
			ccs[learningId] = -1;
			notes[learningId] = note;
			notesMode[learningId] = NOTEMODE::NOTEMODE_MOMENTARY;
			learnedNote = true;
			learnedNoteLast = note;
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
		textLabel[id] = "";
		midiOptions[id] = 0;
		midiParam[id].reset();
		APP->engine->updateParamHandle(&paramHandles[id], -1, 0, true);
		updateMapLen();
		refreshParamHandleText(id);
	}

	void clearMaps() {
		learningId = -1;
		for (int id = 0; id < MAX_CHANNELS; id++) {
			ccs[id] = -1;
			notes[id] = -1;
			textLabel[id] = "";
			midiOptions[id] = 0;
			midiParam[id].reset();
			APP->engine->updateParamHandle(&paramHandles[id], -1, 0, true);
			refreshParamHandleText(id);
		}
		mapLen = 1;
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
		if (mapLen < MAX_CHANNELS) {
			mapLen++;
		}
	}

	void commitLearn() {
		if (learningId < 0)
			return;
		if (!learnedCc && !learnedNote)
			return;
		if (!learnedParam && paramHandles[learningId].moduleId < 0)
			return;
		// Reset learned state
		learnedCc = false;
		learnedNote = false;
		learnedParam = false;
		// Copy modes from the previous slot
		if (learningId > 0) {
			ccsMode[learningId] = ccsMode[learningId - 1];
			notesMode[learningId] = notesMode[learningId - 1];
			midiOptions[learningId] = midiOptions[learningId - 1];
			midiParam[learningId].setSlew(midiParam[learningId - 1].getSlew());
			midiParam[learningId].setMin(midiParam[learningId - 1].getMin());
			midiParam[learningId].setMax(midiParam[learningId - 1].getMax());
		}
		textLabel[learningId] = "";

		// Find next incomplete map
		while (++learningId < MAX_CHANNELS) {
			if ((ccs[learningId] < 0 && notes[learningId] < 0) || paramHandles[learningId].moduleId < 0)
				return;
		}
		learningId = -1;
	}

	void enableLearn(int id) {
		if (id == mapLen) {
			disableLearn();
			return;
		}
		if (learningId != id) {
			learningId = id;
			learnedCc = false;
			learnedCcLast = -1;
			learnedNote = false;
			learnedNoteLast = -1;
			learnedParam = false;
		}
	}

	void disableLearn() {
		learningId = -1;
	}

	void disableLearn(int id) {
		if (learningId == id) {
			learningId = -1;
		}
	}

	void learnParam(int id, int moduleId, int paramId) {
		APP->engine->updateParamHandle(&paramHandles[id], moduleId, paramId, true);
		midiParam[id].reset();
		learnedParam = true;
		commitLearn();
		updateMapLen();
	}

	void moduleBind(Module* m, bool keepCcAndNote) {
		if (!m) return;
		if (!keepCcAndNote) {
			clearMaps();
		}
		else {
			// Clean up some additional mappings on the end
			for (int i = int(m->params.size()); i < mapLen; i++) {
				APP->engine->updateParamHandle(&paramHandles[i], -1, -1, true);
			}
		}
		for (size_t i = 0; i < m->params.size(); i++) {
			learnParam(int(i), m->id, int(i));
		}

		updateMapLen();
	}

	void moduleBindExpander(bool keepCcAndNote) {
		Module::Expander* exp = &leftExpander;
		if (exp->moduleId < 0) return;
		Module* m = exp->module;
		if (!m) return;
		moduleBind(m, keepCcAndNote);
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

	void resendMidiOut() {
		for (int i = 0; i < MAX_CHANNELS; i++) {
			lastValueOut[i] = -1;
		}
	}

	void memSave(std::string pluginSlug, std::string moduleSlug) {
		MemModule* m = new MemModule;
		Module* module;
		for (size_t i = 0; i < MAX_CHANNELS; i++) {
			if (paramHandles[i].moduleId < 0) continue;
			if (paramHandles[i].module->model->plugin->slug != pluginSlug && paramHandles[i].module->model->slug == moduleSlug) continue;
			module = paramHandles[i].module;

			MemParam* p = new MemParam;
			p->paramId = paramHandles[i].paramId;
			p->cc = ccs[i];
			p->ccMode = ccsMode[i];
			p->note = notes[i];
			p->noteMode = notesMode[i];
			p->label = textLabel[i];
			p->midiOptions = midiOptions[i];
			p->slew = midiParam[i].getSlew();
			p->min = midiParam[i].getMin();
			p->max = midiParam[i].getMax();
			m->paramMap.push_back(p);
		}
		m->pluginName = module->model->plugin->name;
		m->moduleName = module->model->name;

		auto p = std::pair<std::string, std::string>(pluginSlug, moduleSlug);
		auto it = memStorage->find(p);
		if (it != memStorage->end()) {
			delete it->second;
		}

		(*memStorage)[p] = m;
	}

	void memDelete(std::string pluginSlug, std::string moduleSlug) {
		auto p = std::pair<std::string, std::string>(pluginSlug, moduleSlug);
		auto it = memStorage->find(p);
		delete it->second;
		memStorage->erase(p);
	}

	void memApply(Module* m) {
		if (!m) return;
		auto p = std::pair<std::string, std::string>(m->model->plugin->slug, m->model->slug);
		auto it = memStorage->find(p);
		if (it == memStorage->end()) return;
		MemModule* map = it->second;

		clearMaps();
		int i = 0;
		for (MemParam* it : map->paramMap) {
			learnParam(i, m->id, it->paramId);
			ccs[i] = it->cc;
			ccsMode[i] = it->ccMode;
			notes[i] = it->note;
			notesMode[i] = it->noteMode;
			textLabel[i] = it->label;
			midiOptions[i] = it->midiOptions;
			midiParam[i].setSlew(it->slew);
			midiParam[i].setMin(it->min);
			midiParam[i].setMax(it->max);
			i++;
		}
		updateMapLen();
	}

	json_t* dataToJson() override {
		json_t* rootJ = json_object();
		json_object_set_new(rootJ, "panelTheme", json_integer(panelTheme));

		json_object_set_new(rootJ, "textScrolling", json_boolean(textScrolling));
		json_object_set_new(rootJ, "mappingIndicatorHidden", json_boolean(mappingIndicatorHidden));
		json_object_set_new(rootJ, "locked", json_boolean(locked));
		json_object_set_new(rootJ, "processDivision", json_integer(processDivision));

		json_t* mapsJ = json_array();
		for (int id = 0; id < mapLen; id++) {
			json_t* mapJ = json_object();
			json_object_set_new(mapJ, "cc", json_integer(ccs[id]));
			json_object_set_new(mapJ, "ccMode", json_integer(ccsMode[id]));
			json_object_set_new(mapJ, "note", json_integer(notes[id]));
			json_object_set_new(mapJ, "noteMode", json_integer(notesMode[id]));
			json_object_set_new(mapJ, "moduleId", json_integer(paramHandles[id].moduleId));
			json_object_set_new(mapJ, "paramId", json_integer(paramHandles[id].paramId));
			json_object_set_new(mapJ, "label", json_string(textLabel[id].c_str()));
			json_object_set_new(mapJ, "midiOptions", json_integer(midiOptions[id]));
			json_object_set_new(mapJ, "slew", json_real(midiParam[id].getSlew()));
			json_object_set_new(mapJ, "min", json_real(midiParam[id].getMin()));
			json_object_set_new(mapJ, "max", json_real(midiParam[id].getMax()));
			json_array_append_new(mapsJ, mapJ);
		}
		json_object_set_new(rootJ, "maps", mapsJ);

		json_object_set_new(rootJ, "midiInput", midiInput.toJson());
		json_object_set_new(rootJ, "midiOutput", midiOutput.toJson());
		return rootJ;
	}

	void dataFromJson(json_t* rootJ) override {
		//clearMaps();
		json_t* panelThemeJ = json_object_get(rootJ, "panelTheme");
		if (panelThemeJ) panelTheme = json_integer_value(panelThemeJ);

		json_t* textScrollingJ = json_object_get(rootJ, "textScrolling");
		if (textScrollingJ) textScrolling = json_boolean_value(textScrollingJ);
		json_t* mappingIndicatorHiddenJ = json_object_get(rootJ, "mappingIndicatorHidden");
		if (mappingIndicatorHiddenJ) mappingIndicatorHidden = json_boolean_value(mappingIndicatorHiddenJ);
		json_t* lockedJ = json_object_get(rootJ, "locked");
		if (lockedJ) locked = json_boolean_value(lockedJ);
		json_t* processDivisionJ = json_object_get(rootJ, "processDivision");
		if (processDivisionJ) processDivision = json_integer_value(processDivisionJ);

		json_t* mapsJ = json_object_get(rootJ, "maps");
		if (mapsJ) {
			json_t* mapJ;
			size_t mapIndex;
			json_array_foreach(mapsJ, mapIndex, mapJ) {
				if (mapIndex >= MAX_CHANNELS) {
					continue;
				}

				json_t* ccJ = json_object_get(mapJ, "cc");
				json_t* ccModeJ = json_object_get(mapJ, "ccMode");
				json_t* noteJ = json_object_get(mapJ, "note");
				json_t* noteModeJ = json_object_get(mapJ, "noteMode");
				json_t* moduleIdJ = json_object_get(mapJ, "moduleId");
				json_t* paramIdJ = json_object_get(mapJ, "paramId");
				json_t* labelJ = json_object_get(mapJ, "label");
				json_t* midiOptionsJ = json_object_get(mapJ, "midiOptions");
				json_t* slewJ = json_object_get(mapJ, "slew");
				json_t* minJ = json_object_get(mapJ, "min");
				json_t* maxJ = json_object_get(mapJ, "max");

				if (!(ccJ || noteJ)) {
					ccs[mapIndex] = -1;
					notes[mapIndex] = -1;
					APP->engine->updateParamHandle(&paramHandles[mapIndex], -1, 0, true);
					continue;
				}
				if (!(moduleIdJ || paramIdJ)) {
					APP->engine->updateParamHandle(&paramHandles[mapIndex], -1, 0, true);
				}

				ccs[mapIndex] = ccJ ? json_integer_value(ccJ) : -1;
				ccsMode[mapIndex] = (CCMODE)json_integer_value(ccModeJ);
				notes[mapIndex] = noteJ ? json_integer_value(noteJ) : -1;
				notesMode[mapIndex] = (NOTEMODE)json_integer_value(noteModeJ);
				midiOptions[mapIndex] = json_integer_value(midiOptionsJ);
				int moduleId = moduleIdJ ? json_integer_value(moduleIdJ) : -1;
				int paramId = paramIdJ ? json_integer_value(paramIdJ) : 0;
				if (moduleId >= 0) {
					moduleId = idFix(moduleId);
					if (moduleId != paramHandles[mapIndex].moduleId || paramId != paramHandles[mapIndex].paramId) {
						APP->engine->updateParamHandle(&paramHandles[mapIndex], moduleId, paramId, false);
						refreshParamHandleText(mapIndex);
					}
				}
				if (labelJ) textLabel[mapIndex] = json_string_value(labelJ);
				if (slewJ) midiParam[mapIndex].setSlew(json_real_value(slewJ));
				if (minJ) midiParam[mapIndex].setMin(json_real_value(minJ));
				if (maxJ) midiParam[mapIndex].setMax(json_real_value(maxJ));
			}
		}

		updateMapLen();
		idFixClearMap();
		
		json_t* midiInputJ = json_object_get(rootJ, "midiInput");
		if (midiInputJ) midiInput.fromJson(midiInputJ);
		json_t* midiOutputJ = json_object_get(rootJ, "midiOutput");
		if (midiOutputJ) midiOutput.fromJson(midiOutputJ);
	}
};


struct MidiCatChoice : MapModuleChoice<MAX_CHANNELS, MidiCatModule> {
	MidiCatChoice() {
		textOffset = Vec(6.f, 14.7f);
		color = nvgRGB(0xf0, 0xf0, 0xf0);
	}

	std::string getSlotPrefix() override {
		if (module->ccs[id] >= 0) {
			return string::f("cc%02d ", module->ccs[id]);
		}
		else if (module->notes[id] >= 0) {
			static const char* noteNames[] = {
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

	std::string getSlotLabel() override {
		return module->textLabel[id];
	}

	void appendContextMenu(Menu* menu) override {
		struct CcModeMenuItem : MenuItem {
			MidiCatModule* module;
			int id;

			CcModeMenuItem() {
				rightText = RIGHT_ARROW;
			}

			struct CcModeItem : MenuItem {
				MidiCatModule* module;
				int id;
				CCMODE ccMode;

				void onAction(const event::Action& e) override {
					module->ccsMode[id] = ccMode;
				}
				void step() override {
					rightText = module->ccsMode[id] == ccMode ? "✔" : "";
					MenuItem::step();
				}
			};

			Menu* createChildMenu() override {
				Menu* menu = new Menu;
				menu->addChild(construct<CcModeItem>(&MenuItem::text, "Direct", &CcModeItem::module, module, &CcModeItem::id, id, &CcModeItem::ccMode, CCMODE::CCMODE_DIRECT));
				menu->addChild(construct<CcModeItem>(&MenuItem::text, "Pickup (snap)", &CcModeItem::module, module, &CcModeItem::id, id, &CcModeItem::ccMode, CCMODE::CCMODE_PICKUP1));
				menu->addChild(construct<CcModeItem>(&MenuItem::text, "Pickup (jump)", &CcModeItem::module, module, &CcModeItem::id, id, &CcModeItem::ccMode, CCMODE::CCMODE_PICKUP2));
				return menu;
			}
		}; // CcModeMenuItem

		struct NoteModeMenuItem : MenuItem {
			MidiCatModule* module;
			int id;

			NoteModeMenuItem() {
				rightText = RIGHT_ARROW;
			}

			struct NoteModeItem : MenuItem {
				MidiCatModule* module;
				int id;
				NOTEMODE noteMode;

				void onAction(const event::Action& e) override {
					module->notesMode[id] = noteMode;
				}
				void step() override {
					rightText = module->notesMode[id] == noteMode ? "✔" : "";
					MenuItem::step();
				}
			};

			Menu* createChildMenu() override {
				Menu* menu = new Menu;
				menu->addChild(construct<NoteModeItem>(&MenuItem::text, "Momentary", &NoteModeItem::module, module, &NoteModeItem::id, id, &NoteModeItem::noteMode, NOTEMODE::NOTEMODE_MOMENTARY));
				menu->addChild(construct<NoteModeItem>(&MenuItem::text, "Momentary + Velocity", &NoteModeItem::module, module, &NoteModeItem::id, id, &NoteModeItem::noteMode, NOTEMODE::NOTEMODE_MOMENTARY_VEL));
				menu->addChild(construct<NoteModeItem>(&MenuItem::text, "Toggle", &NoteModeItem::module, module, &NoteModeItem::id, id, &NoteModeItem::noteMode, NOTEMODE::NOTEMODE_TOGGLE));
				return menu;
			}
		}; // NoteModeMenuItem

		struct NoteVelZeroMenuItem : MenuItem {
			MidiCatModule* module;
			int id;

			void onAction(const event::Action& e) override {
				module->midiOptions[id] ^= 1UL << MIDIOPTION_VELZERO_BIT;
			}
			void step() override {
				rightText = CHECKMARK((module->midiOptions[id] >> MIDIOPTION_VELZERO_BIT) & 1U);
				MenuItem::step();
			}
		}; // NoteVelZeroMenuItem

		if (module->ccs[id] >= 0) {
			menu->addChild(new MenuSeparator());
			menu->addChild(construct<CcModeMenuItem>(&MenuItem::text, "Input mode for CC", &CcModeMenuItem::module, module, &CcModeMenuItem::id, id));
		}
		if (module->notes[id] >= 0) {
			menu->addChild(new MenuSeparator());
			menu->addChild(construct<NoteModeMenuItem>(&MenuItem::text, "Input mode for notes", &NoteModeMenuItem::module, module, &NoteModeMenuItem::id, id));
			menu->addChild(construct<NoteVelZeroMenuItem>(&MenuItem::text, "Send \"note on, velocity 0\"", &NoteVelZeroMenuItem::module, module, &NoteVelZeroMenuItem::id, id));
		}

		struct SlewSlider : ui::Slider {
			struct SlewQuantity : Quantity {
				const float SLEW_MIN = 0.f;
				const float SLEW_MAX = 5.f;
				MidiCatParam* p;
				void setValue(float value) override {
					value = clamp(value, SLEW_MIN, SLEW_MAX);
					p->setSlew(value);
				}
				float getValue() override {
					return p->getSlew();
				}
				float getDefaultValue() override {
					return 0.f;
				}
				std::string getLabel() override {
					return "Slew-limiting";
				}
				int getDisplayPrecision() override {
					return 2;
				}
				float getMaxValue() override {
					return SLEW_MAX;
				}
				float getMinValue() override {
					return SLEW_MIN;
				}
			}; // struct SlewQuantity

			SlewSlider(MidiCatParam* p) {
				box.size.x = 140.0f;
				quantity = construct<SlewQuantity>(&SlewQuantity::p, p);
			}
			~SlewSlider() {
				delete quantity;
			}
		}; // struct SlewSlider


		struct ScalingLabel : MenuLabel {
			MidiCatParam* p;
			void step() override {
				float min = p->getMin();
				float max = p->getMax();

				float f1 = rescale(0.f, 0.f, 127.f, min, max);
				f1 = clamp(f1, 0.f, 1.f) * 100.f;
				float f2 = rescale(127.f, 0.f, 127.f, min, max);
				f2 = clamp(f2, 0.f, 1.f) * 100.f;

				float g1 = rescale(0.f, min, max, 0.f, 127.f);
				g1 = clamp(g1, 0.f, 127.f);
				int g1a = std::round(g1);
				float g2 = rescale(1.f, min, max, 0.f, 127.f);
				g2 = clamp(g2, 0.f, 127.f);
				int g2a = std::round(g2);

				text = string::f("[%i, %i] " RIGHT_ARROW " [%.1f%, %.1f%]", g1a, g2a, f1, f2);
			}
		}; // struct ScalingLabel

		struct MinSlider : ui::Slider {
			struct MinQuantity : Quantity {
				MidiCatParam* p;
				void setValue(float value) override {
					value = clamp(value, -1.f, 2.f);
					p->setMin(value);
				}
				float getValue() override {
					return p->getMin();
				}
				float getDefaultValue() override {
					return 0.f;
				}
				float getMinValue() override {
					return -1.f;
				}
				float getMaxValue() override {
					return 2.f;
				}
				float getDisplayValue() override {
					return getValue() * 100;
				}
				void setDisplayValue(float displayValue) override {
					setValue(displayValue / 100);
				}
				std::string getLabel() override {
					return "Low";
				}
				std::string getUnit() override {
					return "%";
				}
				int getDisplayPrecision() override {
					return 3;
				}
			}; // struct MinQuantity

			MinSlider(MidiCatParam* p) {
				box.size.x = 140.0f;
				quantity = construct<MinQuantity>(&MinQuantity::p, p);
			}
			~MinSlider() {
				delete quantity;
			}
		}; // struct MinSlider

		struct MaxSlider : ui::Slider {
			struct MaxQuantity : Quantity {
				MidiCatParam* p;
				void setValue(float value) override {
					value = clamp(value, -1.f, 2.f);
					p->setMax(value);
				}
				float getValue() override {
					return p->getMax();
				}
				float getDefaultValue() override {
					return 1.f;
				}
				float getMinValue() override {
					return -1.f;
				}
				float getMaxValue() override {
					return 2.f;
				}
				float getDisplayValue() override {
					return getValue() * 100;
				}
				void setDisplayValue(float displayValue) override {
					setValue(displayValue / 100);
				}
				std::string getLabel() override {
					return "High";
				}
				std::string getUnit() override {
					return "%";
				}
				int getDisplayPrecision() override {
					return 3;
				}
			}; // struct MaxQuantity

			MaxSlider(MidiCatParam* p) {
				box.size.x = 140.0f;
				quantity = construct<MaxQuantity>(&MaxQuantity::p, p);
			}
			~MaxSlider() {
				delete quantity;
			}
		}; // struct MaxSlider

		struct LabelMenuItem : MenuItem {
			MidiCatModule* module;
			int id;

			LabelMenuItem() {
				rightText = RIGHT_ARROW;
			}

			struct LabelField : ui::TextField {
				MidiCatModule* module;
				int id;
				void onSelectKey(const event::SelectKey& e) override {
					if (e.action == GLFW_PRESS && e.key == GLFW_KEY_ENTER) {
						module->textLabel[id] = text;

						ui::MenuOverlay* overlay = getAncestorOfType<ui::MenuOverlay>();
						overlay->requestDelete();
						e.consume(this);
					}

					if (!e.getTarget()) {
						ui::TextField::onSelectKey(e);
					}
				}
			};

			struct ResetItem : ui::MenuItem {
				MidiCatModule* module;
				int id;
				void onAction(const event::Action& e) override {
					module->textLabel[id] = "";
				}
			};

			Menu* createChildMenu() override {
				Menu* menu = new Menu;

				LabelField* labelField = new LabelField;
				labelField->placeholder = "Label";
				labelField->text = module->textLabel[id];
				labelField->box.size.x = 180;
				labelField->module = module;
				labelField->id = id;
				menu->addChild(labelField);

				ResetItem* resetItem = new ResetItem;
				resetItem->text = "Reset";
				resetItem->module = module;
				resetItem->id = id;
				menu->addChild(resetItem);

				return menu;
			}
		}; // LabelMenuItem

		menu->addChild(new SlewSlider(&module->midiParam[id]));
		menu->addChild(new MenuSeparator());
		menu->addChild(construct<MenuLabel>(&MenuLabel::text, "Scaling"));
		menu->addChild(construct<ScalingLabel>(&ScalingLabel::p, &module->midiParam[id]));
		menu->addChild(new MinSlider(&module->midiParam[id]));
		menu->addChild(new MaxSlider(&module->midiParam[id]));
		menu->addChild(new MenuSeparator());
		menu->addChild(construct<LabelMenuItem>(&MenuItem::text, "Custom label", &LabelMenuItem::module, module, &LabelMenuItem::id, id));
	}
};


struct MidiCatDisplay : MapModuleDisplay<MAX_CHANNELS, MidiCatModule, MidiCatChoice> {
	void step() override {
		if (module) {
			int mapLen = module->mapLen;
			for (int id = 0; id < MAX_CHANNELS; id++) {
				choices[id]->visible = (id < mapLen);
				separators[id]->visible = (id < mapLen);
			}
		}
		MapModuleDisplay<MAX_CHANNELS, MidiCatModule, MidiCatChoice>::step();
	}
};

struct MidiCatMidiWidget : MidiWidget {
	void setMidiPort(midi::Port* port) {
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

struct MidiCatWidget : ThemedModuleWidget<MidiCatModule> {
	MidiCatModule* module;
	Module* mem;
	BufferedTriggerParamQuantity* memParamQuantity;
	dsp::SchmittTrigger memParamTrigger;

	enum class LEARN_MODE {
		OFF = 0,
		BIND_CLEAR = 1,
		BIND_KEEP = 2,
		MEM = 3
	};

	LEARN_MODE learnMode = LEARN_MODE::OFF;

	Widget* lastSelectedWidget;

	MidiCatWidget(MidiCatModule* module)
		: ThemedModuleWidget<MidiCatModule>(module, "MidiCat") {
		setModule(module);
		this->module = module;

		addChild(createWidget<StoermelderBlackScrew>(Vec(RACK_GRID_WIDTH, 0)));
		addChild(createWidget<StoermelderBlackScrew>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, 0)));
		addChild(createWidget<StoermelderBlackScrew>(Vec(RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));
		addChild(createWidget<StoermelderBlackScrew>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));

		MidiCatMidiWidget* midiInputWidget = createWidget<MidiCatMidiWidget>(Vec(10.0f, 36.4f));
		midiInputWidget->box.size = Vec(130.0f, 67.0f);
		midiInputWidget->setMidiPort(module ? &module->midiInput : NULL);
		addChild(midiInputWidget);

		MidiCatMidiWidget* midiOutputWidget = createWidget<MidiCatMidiWidget>(Vec(10.0f, 107.4f));
		midiOutputWidget->box.size = Vec(130.0f, 67.0f);
		midiOutputWidget->setMidiPort(module ? &module->midiOutput : NULL);
		addChild(midiOutputWidget);

		MidiCatDisplay* mapWidget = createWidget<MidiCatDisplay>(Vec(10.0f, 178.5f));
		mapWidget->box.size = Vec(130.0f, 164.7f);
		mapWidget->setModule(module);
		addChild(mapWidget);
	}

	~MidiCatWidget() {
		if (learnMode != LEARN_MODE::OFF) {
			glfwSetCursor(APP->window->win, NULL);
		}
	}

	void loadMidiMapPreset_dialog() {
		osdialog_filters* filters = osdialog_filters_parse(PRESET_FILTERS);
		DEFER({
			osdialog_filters_free(filters);
		});

		char* path = osdialog_file(OSDIALOG_OPEN, "", NULL, filters);
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

		FILE* file = fopen(filename.c_str(), "r");
		if (!file) {
			WARN("Could not load patch file %s", filename.c_str());
			return;
		}
		DEFER({
			fclose(file);
		});

		json_error_t error;
		json_t* moduleJ = json_loadf(file, 0, &error);
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
		history::ModuleChange* h = new history::ModuleChange;
		h->name = "load module preset";
		h->moduleId = module->id;
		h->oldModuleJ = toJson();

		module->fromJson(moduleJ);

		h->newModuleJ = toJson();
		APP->history->push(h);
	}

	bool loadMidiMapPreset_convert(json_t* moduleJ) {
		std::string pluginSlug = json_string_value(json_object_get(moduleJ, "plugin"));
		std::string modelSlug = json_string_value(json_object_get(moduleJ, "model"));

		// Only handle presets for MIDI-Map
		if (!(pluginSlug == "Core" && modelSlug == "MIDI-Map"))
			return false;

		json_object_set_new(moduleJ, "plugin", json_string(module->model->plugin->slug.c_str()));
		json_object_set_new(moduleJ, "model", json_string(module->model->slug.c_str()));
		json_t* dataJ = json_object_get(moduleJ, "data");
		json_object_set(dataJ, "midiInput", json_object_get(dataJ, "midi"));
		return true;
	}

	void step() override {
		ThemedModuleWidget<MidiCatModule>::step();
		if (module) {
			if (module->mem != mem) {
				mem = module->mem;
				if (mem) {
					memParamQuantity = dynamic_cast<BufferedTriggerParamQuantity*>(mem->paramQuantities[0]);
					memParamQuantity->resetBuffer();
				}
			}
			if (mem) {
				if (memParamTrigger.process(memParamQuantity->buffer)) {
					memParamQuantity->resetBuffer();
					enableLearn(LEARN_MODE::MEM);
				}
				module->mem->lights[0].setBrightness(learnMode == LEARN_MODE::MEM);
			}
		}

		extendParamWidgetContextMenu();
	}

	void extendParamWidgetContextMenu() {
		if (!module) return;

		// Extend parameter's context menu with additional MenuItems
		Widget* w = APP->event->getDraggedWidget();
		if (!w) return;
		if (w != lastSelectedWidget) {
			lastSelectedWidget = w;
			if (module->learningId >= 0) return;
			ParamWidget* pw = dynamic_cast<ParamWidget*>(w);
			if (!pw) return;
			ParamQuantity* pq = pw->paramQuantity;
			if (!pq) return;
			ParamHandle* handle = APP->engine->getParamHandle(pq->module->id, pq->paramId);
			if (!handle) return;
			
			for (int i = 0; i < module->mapLen; i++) {
				if (&module->paramHandles[i] == handle) {
					// Hack for attaching additional menu items to parameter's context menu
					MenuOverlay* overlay;
					for (Widget* child : APP->scene->children) {
						overlay = dynamic_cast<MenuOverlay*>(child);
						if (overlay) break;
					}
					if (!overlay) return;
					Widget* w = overlay->children.front();
					Menu* menu = dynamic_cast<Menu*>(w);
					if (!menu) return;

					struct MapMenuItem : MenuItem {
						MidiCatModule* module;
						int id;
						void onAction(const event::Action& e) override {
							module->enableLearn(id);
						}
					};

					struct ShowMidicatItem : MenuItem {
						MidiCatWidget* mw;
						void onAction(const event::Action& e) override {
							StoermelderPackOne::Rack::ViewportCenter{mw};
						}
					};

					menu->addChild(new MenuSeparator);
					menu->addChild(construct<MenuLabel>(&MenuLabel::text, "MIDI-CAT"));
					menu->addChild(construct<MapMenuItem>(&MenuItem::text, "Learn MIDI", &MapMenuItem::module, module, &MapMenuItem::id, i));
					menu->addChild(construct<ShowMidicatItem>(&MenuItem::text, "Center mapping module", &ShowMidicatItem::mw, this));
					break;
				}
			}
		}
	}

	void onDeselect(const event::Deselect& e) override {
		ModuleWidget::onDeselect(e);
		if (learnMode != LEARN_MODE::OFF) {
			DEFER({
				disableLearn();
			});

			// Learn module
			Widget* w = APP->event->getDraggedWidget();
			if (!w) return;
			ModuleWidget* mw = dynamic_cast<ModuleWidget*>(w);
			if (!mw) mw = w->getAncestorOfType<ModuleWidget>();
			if (!mw || mw == this) return;
			Module* m = mw->module;
			if (!m) return;

			MidiCatModule* module = dynamic_cast<MidiCatModule*>(this->module);
			switch (learnMode) {
				case LEARN_MODE::BIND_CLEAR:
					module->moduleBind(m, false); break;
				case LEARN_MODE::BIND_KEEP:
					module->moduleBind(m, true); break;
				case LEARN_MODE::MEM:
					module->memApply(m); break;
				case LEARN_MODE::OFF:
					break;
			}
		}
	}

	void onHoverKey(const event::HoverKey& e) override {
		if (e.action == GLFW_PRESS) {
			switch (e.key) {
				case GLFW_KEY_D: {
					if ((e.mods & RACK_MOD_MASK) == GLFW_MOD_SHIFT) {
						enableLearn(LEARN_MODE::BIND_KEEP);
					}
					if ((e.mods & RACK_MOD_MASK) == (GLFW_MOD_SHIFT | RACK_MOD_CTRL)) {
						enableLearn(LEARN_MODE::BIND_CLEAR);
					}
					break;
				}
				case GLFW_KEY_E: {
					if ((e.mods & RACK_MOD_MASK) == GLFW_MOD_SHIFT) {
						MidiCatModule* module = dynamic_cast<MidiCatModule*>(this->module);
						module->moduleBindExpander(true);
					}
					if ((e.mods & RACK_MOD_MASK) == (GLFW_MOD_SHIFT | RACK_MOD_CTRL)) {
						MidiCatModule* module = dynamic_cast<MidiCatModule*>(this->module);
						module->moduleBindExpander(false);
					}
					break;
				}
				case GLFW_KEY_V: {
					if ((e.mods & RACK_MOD_MASK) == GLFW_MOD_SHIFT) {
						enableLearn(LEARN_MODE::MEM);
					}
					break;
				}
				case GLFW_KEY_ESCAPE: {
					MidiCatModule* module = dynamic_cast<MidiCatModule*>(this->module);
					disableLearn();
					module->disableLearn();
					e.consume(this);
					break;
				}
				case GLFW_KEY_SPACE: {
					if (module->learningId >= 0) {
						MidiCatModule* module = dynamic_cast<MidiCatModule*>(this->module);
						module->enableLearn(module->learningId + 1);
						if (module->learningId == -1) disableLearn();
						e.consume(this);
					}
					break;
				}
			}
		}
		ThemedModuleWidget<MidiCatModule>::onHoverKey(e);
	}

	void enableLearn(LEARN_MODE mode) {
		learnMode = learnMode == LEARN_MODE::OFF ? mode : LEARN_MODE::OFF;
		APP->event->setSelected(this);
		GLFWcursor* cursor = NULL;
		if (learnMode != LEARN_MODE::OFF) {
			cursor = glfwCreateStandardCursor(GLFW_CROSSHAIR_CURSOR);
		}
		glfwSetCursor(APP->window->win, cursor);
	}

	void disableLearn() {
		learnMode = LEARN_MODE::OFF;
		glfwSetCursor(APP->window->win, NULL);
	}

	void appendContextMenu(Menu* menu) override {
		ThemedModuleWidget<MidiCatModule>::appendContextMenu(menu);
		assert(module);

		struct MidiMapImportItem : MenuItem {
			MidiCatWidget* moduleWidget;
			void onAction(const event::Action& e) override {
				moduleWidget->loadMidiMapPreset_dialog();
			}
		};

		struct ResendMidiOutItem : MenuItem {
			MidiCatModule* module;
			void onAction(const event::Action& e) override {
				module->resendMidiOut();
			}
		};

		struct PrecisionMenuItem : MenuItem {
			struct PrecisionItem : MenuItem {
				MidiCatModule* module;
				int sampleRate;
				int division;
				std::string text;
				PrecisionItem() {
					sampleRate = int(APP->engine->getSampleRate());
				}
				void onAction(const event::Action& e) override {
					module->processDivision = division;
				}
				void step() override {
					MenuItem::text = string::f("%s (%i Hz)", text.c_str(), sampleRate / division);
					rightText = module->processDivision == division ? "✔" : "";
					MenuItem::step();
				}
			};

			MidiCatModule* module;
			PrecisionMenuItem() {
				rightText = RIGHT_ARROW;
			}

			Menu* createChildMenu() override {
				Menu* menu = new Menu;
				menu->addChild(construct<PrecisionItem>(&PrecisionItem::text, "Audio rate", &PrecisionItem::module, module, &PrecisionItem::division, 1));
				menu->addChild(construct<PrecisionItem>(&PrecisionItem::text, "Higher CPU", &PrecisionItem::module, module, &PrecisionItem::division, 8));
				menu->addChild(construct<PrecisionItem>(&PrecisionItem::text, "Moderate CPU", &PrecisionItem::module, module, &PrecisionItem::division, 64));
				menu->addChild(construct<PrecisionItem>(&PrecisionItem::text, "Lowest CPU", &PrecisionItem::module, module, &PrecisionItem::division, 256));
				return menu;
			}
		};

		struct MidiModeMenuItem : MenuItem {
			MidiModeMenuItem() {
				rightText = RIGHT_ARROW;
			}

			struct MidiModeItem : MenuItem {
				MidiCatModule* module;
				MIDIMODE midiMode;

				void onAction(const event::Action &e) override {
					module->setMode(midiMode);
				}
				void step() override {
					rightText = module->midiMode == midiMode ? "✔" : "";
					MenuItem::step();
				}
			};

			MidiCatModule *module;
			Menu* createChildMenu() override {
				Menu* menu = new Menu;
				menu->addChild(construct<MidiModeItem>(&MenuItem::text, "Operating", &MidiModeItem::module, module, &MidiModeItem::midiMode, MIDIMODE::MIDIMODE_DEFAULT));
				menu->addChild(construct<MidiModeItem>(&MenuItem::text, "Locate and indicate", &MidiModeItem::module, module, &MidiModeItem::midiMode, MIDIMODE::MIDIMODE_LOCATE));
				return menu;
			}
		};

		menu->addChild(new MenuSeparator());
		menu->addChild(construct<PrecisionMenuItem>(&MenuItem::text, "Precision", &PrecisionMenuItem::module, module));
		menu->addChild(construct<MidiModeMenuItem>(&MenuItem::text, "Mode", &MidiModeMenuItem::module, module));
		menu->addChild(construct<ResendMidiOutItem>(&MenuItem::text, "Re-send MIDI feedback", &ResendMidiOutItem::module, module));
		menu->addChild(construct<MidiMapImportItem>(&MenuItem::text, "Import MIDI-MAP preset", &MidiMapImportItem::moduleWidget, this));

		struct TextScrollItem : MenuItem {
			MidiCatModule* module;
			void onAction(const event::Action& e) override {
				module->textScrolling ^= true;
			}
			void step() override {
				rightText = module->textScrolling ? "✔" : "";
				MenuItem::step();
			}
		};

		struct MappingIndicatorHiddenItem : MenuItem {
			MidiCatModule* module;
			void onAction(const event::Action& e) override {
				module->mappingIndicatorHidden ^= true;
			}
			void step() override {
				rightText = module->mappingIndicatorHidden ? "✔" : "";
				MenuItem::step();
			}
		};

		struct LockedItem : MenuItem {
			MidiCatModule* module;
			void onAction(const event::Action& e) override {
				module->locked ^= true;
			}
			void step() override {
				rightText = module->locked ? "✔" : "";
				MenuItem::step();
			}
		};

		struct ClearMapsItem : MenuItem {
			MidiCatModule* module;
			void onAction(const event::Action& e) override {
				module->clearMaps();
			}
		};

		struct ModuleLearnExpanderMenuItem : MenuItem {
			MidiCatModule* module;
			ModuleLearnExpanderMenuItem() {
				rightText = RIGHT_ARROW;
			}
			Menu* createChildMenu() override {
				struct ModuleLearnExpanderItem : MenuItem {
					MidiCatModule* module;
					bool keepCcAndNote;
					void onAction(const event::Action& e) override {
						module->moduleBindExpander(keepCcAndNote);
					}
				};

				Menu* menu = new Menu;
				menu->addChild(construct<ModuleLearnExpanderItem>(&MenuItem::text, "Clear first", &MenuItem::rightText, RACK_MOD_CTRL_NAME "+" RACK_MOD_SHIFT_NAME "+E", &ModuleLearnExpanderItem::module, module, &ModuleLearnExpanderItem::keepCcAndNote, false));
				menu->addChild(construct<ModuleLearnExpanderItem>(&MenuItem::text, "Keep MIDI assignments", &MenuItem::rightText, RACK_MOD_SHIFT_NAME "+E", &ModuleLearnExpanderItem::module, module, &ModuleLearnExpanderItem::keepCcAndNote, true));
				return menu;
			}
		};

		struct ModuleLearnSelectMenuItem : MenuItem {
			MidiCatWidget* mw;
			ModuleLearnSelectMenuItem() {
				rightText = RIGHT_ARROW;
			}
			Menu* createChildMenu() override {
				struct ModuleLearnSelectItem : MenuItem {
					MidiCatWidget* mw;
					LEARN_MODE mode;
					void onAction(const event::Action& e) override {
						mw->enableLearn(mode);
					}
				};

				Menu* menu = new Menu;
				menu->addChild(construct<ModuleLearnSelectItem>(&MenuItem::text, "Clear first", &MenuItem::rightText, RACK_MOD_CTRL_NAME "+" RACK_MOD_SHIFT_NAME "+D", &ModuleLearnSelectItem::mw, mw, &ModuleLearnSelectItem::mode, LEARN_MODE::BIND_CLEAR));
				menu->addChild(construct<ModuleLearnSelectItem>(&MenuItem::text, "Keep MIDI assignments", &MenuItem::rightText, RACK_MOD_SHIFT_NAME "+D", &ModuleLearnSelectItem::mw, mw, &ModuleLearnSelectItem::mode, LEARN_MODE::BIND_KEEP));
				return menu;
			}
		};

		menu->addChild(new MenuSeparator());
		menu->addChild(construct<TextScrollItem>(&MenuItem::text, "Text scrolling", &TextScrollItem::module, module));
		menu->addChild(construct<MappingIndicatorHiddenItem>(&MenuItem::text, "Hide mapping indicators", &MappingIndicatorHiddenItem::module, module));
		menu->addChild(construct<LockedItem>(&MenuItem::text, "Lock mapping slots", &LockedItem::module, module));
		menu->addChild(new MenuSeparator());
		menu->addChild(construct<ClearMapsItem>(&MenuItem::text, "Clear mappings", &ClearMapsItem::module, module));
		menu->addChild(construct<ModuleLearnExpanderMenuItem>(&MenuItem::text, "Map module (left)", &ModuleLearnExpanderMenuItem::module, module));
		menu->addChild(construct<ModuleLearnSelectMenuItem>(&MenuItem::text, "Map module (select)", &ModuleLearnSelectMenuItem::mw, this));

		if (module->memStorage != NULL) appendContextMenuMem(menu);
	}

	void appendContextMenuMem(Menu* menu) {
		MidiCatModule* module = dynamic_cast<MidiCatModule*>(this->module);
		assert(module);

		struct MapMenuItem : MenuItem {
			MidiCatModule* module;
			MapMenuItem() {
				rightText = RIGHT_ARROW;
			}

			Menu* createChildMenu() override {
				struct MidimapModuleItem : MenuItem {
					MidiCatModule* module;
					std::string pluginSlug;
					std::string moduleSlug;
					MemModule* midimapModule;
					MidimapModuleItem() {
						rightText = RIGHT_ARROW;
					}
					Menu* createChildMenu() override {
						struct DeleteItem : MenuItem {
							MidiCatModule* module;
							std::string pluginSlug;
							std::string moduleSlug;
							void onAction(const event::Action& e) override {
								module->memDelete(pluginSlug, moduleSlug);
							}
						}; // DeleteItem

						Menu* menu = new Menu;
						menu->addChild(construct<DeleteItem>(&MenuItem::text, "Delete", &DeleteItem::module, module, &DeleteItem::pluginSlug, pluginSlug, &DeleteItem::moduleSlug, moduleSlug));
						return menu;
					}
				}; // MidimapModuleItem

				std::list<std::pair<std::string, MidimapModuleItem*>> l; 
				for (auto it : *module->memStorage) {
					MemModule* a = it.second;
					MidimapModuleItem* midimapModuleItem = new MidimapModuleItem;
					midimapModuleItem->text = string::f("%s %s", a->pluginName.c_str(), a->moduleName.c_str());
					midimapModuleItem->module = module;
					midimapModuleItem->midimapModule = a;
					midimapModuleItem->pluginSlug = it.first.first;
					midimapModuleItem->moduleSlug = it.first.second;
					l.push_back(std::pair<std::string, MidimapModuleItem*>(midimapModuleItem->text, midimapModuleItem));
				}

				l.sort();
				Menu* menu = new Menu;
				for (auto it : l) {
					menu->addChild(it.second);
				}
				return menu;
			}
		}; // MapMenuItem

		struct SaveMenuItem : MenuItem {
			MidiCatModule* module;
			SaveMenuItem() {
				rightText = RIGHT_ARROW;
			}

			Menu* createChildMenu() override {
				struct SaveItem : MenuItem {
					MidiCatModule* module;
					std::string pluginSlug;
					std::string moduleSlug;
					void onAction(const event::Action& e) override {
						module->memSave(pluginSlug, moduleSlug);
					}
				}; // SaveItem

				typedef std::pair<std::string, std::string> ppair;
				std::list<std::pair<std::string, ppair>> list;
				std::set<ppair> s;
				for (size_t i = 0; i < MAX_CHANNELS; i++) {
					int moduleId = module->paramHandles[i].moduleId;
					if (moduleId < 0) continue;
					Module* m = module->paramHandles[i].module;
					auto q = ppair(m->model->plugin->slug, m->model->slug);
					if (s.find(q) != s.end()) continue;
					s.insert(q);

					if (!m) continue;
					std::string l = string::f("%s %s", m->model->plugin->name.c_str(), m->model->name.c_str());
					auto p = std::pair<std::string, ppair>(l, q);
					list.push_back(p);
				}
				list.sort();

				Menu* menu = new Menu;
				for (auto it : list) {
					menu->addChild(construct<SaveItem>(&MenuItem::text, it.first, &SaveItem::module, module, &SaveItem::pluginSlug, it.second.first, &SaveItem::moduleSlug, it.second.second));
				}
				return menu;
			}
		}; // SaveMenuItem

		struct ApplyItem : MenuItem {
			MidiCatWidget* mw;
			void onAction(const event::Action& e) override {
				mw->enableLearn(LEARN_MODE::MEM);
			}
		}; // ApplyItem

		menu->addChild(new MenuSeparator());
		menu->addChild(construct<MenuLabel>(&MenuLabel::text, "MEM-expander"));
		menu->addChild(construct<MapMenuItem>(&MenuItem::text, "Available mappings", &MapMenuItem::module, module));
		menu->addChild(construct<SaveMenuItem>(&MenuItem::text, "Store mapping", &SaveMenuItem::module, module));
		menu->addChild(construct<ApplyItem>(&MenuItem::text, "Apply mapping", &MenuItem::rightText, RACK_MOD_SHIFT_NAME "+V", &ApplyItem::mw, this));
	}
};

} // namespace MidiCat
} // namespace StoermelderPackOne

Model *modelMidiCat = createModel<StoermelderPackOne::MidiCat::MidiCatModule, StoermelderPackOne::MidiCat::MidiCatWidget>("MidiCat");