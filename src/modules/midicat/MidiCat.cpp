#include "../../plugin.hpp"
#include "../../vcv/api.hpp"
#include "../../utils/StripIdFixModule.hpp"
#include "../../utils/TaskProcessor.hpp"
#include "../../components/MenuLabelEx.hpp"
#include "../../components/CurveMenuItem.hpp"
#include "../../components/SubMenuSlider.hpp"
#include "../../components/MidiWidget.hpp"
#include "../../components/MenuColorField.hpp"
#include "../../components/MenuColorLabel.hpp"
#include "../../components/MenuColorPicker.hpp"
#include "../../ui/ParamWidgetContextExtender.hpp"
#include "../../ui/OverlayMessageWidget.hpp"
#include "../cvmap/MapModuleBase.hpp"
#include "MidiCat.hpp"
#include "MidiCat.output.hpp"
#include "MidiCat.input.hpp"
#include "MidiCat.param.hpp"
#include "MidiCat.slot.hpp"
#include "MidiCat.expanders.hpp"

namespace StoermelderPackOne {
namespace MidiCat {

static const char PRESET_FILTERS[] = "VCV Rack module preset (.vcvm):vcvm";
static const NVGcolor MAPPING_INDICATOR_COLOR_DEFAULT = nvgRGB(0xff, 0xff, 0x40);

enum MIDIMODE {
	MIDIMODE_DEFAULT = 0,
	MIDIMODE_LOCATE = 1
};

struct MidiCatModule : Module, StripIdFixModule, ModuleChangeListener {
	/** [Stored to Json] */
	midi::InputQueue midiInput;
	/** [Stored to Json] */
	MidiCatOutput midiOutput;

	/** [Stored to JSON] */
	int panelTheme = 0;

	/** Number of maps */
	int mapLen = 0;
	/** [Stored to Json] The mapping slots: MIDI binding, scaling and tracking state */
	MappingSlot slots[MAX_CHANNELS];
	/** [Stored to JSON] */
	bool midiIgnoreDevices;
	/** [Stored to JSON] */
	bool clearMapsOnLoad;

	/** [Stored to Json] The mapped param handle of each channel */
	ParamHandleIndicator paramHandles[MAX_CHANNELS];

	/** Channel ID of the learning session for MIDI/param mapping */
	int learningId;
	/** Wether multiple slots or just one slot should be learned */
	bool learnSingleSlot = false;
	/** Whether the CC has been set during the learning session */
	bool learnedCc;
	int learnedCcLast = -1;
	/** Whether the note has been set during the learning session */
	bool learnedNote;
	int learnedNoteLast = -1;
	/** Whether the param has been set during the learning session */
	bool learnedParam;

	/** Channel ID of the learning session forLED binding */
	int learningLightId = -1;

	/** [Stored to Json] */
	bool textScrolling = true;
	/** [Stored to Json] */
	bool locked;

	/** [Stored to Json] */
	NVGcolor mappingIndicatorColor = MAPPING_INDICATOR_COLOR_DEFAULT;
	/** [Stored to Json] */
	bool mappingIndicatorHidden = false;

	/** Last-seen value of every CC and note number, shared by all slots */
	MidiInputState midiInputState;

	MIDIMODE midiMode = MIDIMODE::MIDIMODE_DEFAULT;
	bool ccFineMode = false;
	// Use for temporary override of CC mode to DIRECT
	bool ccModeOverride = false;

	dsp::RingBuffer<int, 8> overlayQueue;
	/** [Stored to Json] */
	bool overlayEnabled;

	/** [Stored to Json] */
	bool midiResendPeriodically;
	ClockDividerEx midiResendDivider;

	ClockDividerEx processDivider;
	/** [Stored to Json] */
	int processDivision;
	ClockDividerEx indicatorDivider;
	/** [Stored to JSON] */
	bool parameterChangesDirect = false;

	/** Holds the time needed for long presses */
	uint64_t longPressDuration;

	TaskProcessor<> taskProcessorUi;

	// MEM-expander
	/** The four right-side expanders, if attached */
	ExpanderSet expanders;
	/** The module the MEM-expander mapping was last applied to */
	int64_t expMemModuleId = -1;

	ClkExpanderDriver expClkDriver;
	FineExpanderDriver expFineDriver;

	MidiCatModule() {
		panelTheme = pluginSettings.panelThemeDefault;
		registerModuleListener("MidiCat", this);
		config(0, 0, 0, 0);
		for (int id = 0; id < MAX_CHANNELS; id++) {
			paramHandles[id].color = mappingIndicatorColor;
			APP->engine->addParamHandle(&paramHandles[id]);
			slots[id].applyCcLimits();
		}
		indicatorDivider.setDivision(2048);
		midiResendDivider.setDivision(APP->engine->getSampleRate() / 2);

		Module::ResetEvent re;
		onReset(re);
	}

	~MidiCatModule() {
		unregisterModuleListener("MidiCat", this);
		for (int id = 0; id < MAX_CHANNELS; id++) {
			APP->engine->removeParamHandle(&paramHandles[id]);
		}
	}

	void onExpanderChange(const Module::ExpanderChangeEvent& e) override {
		notifyModuleListeners("MidiCat");
	}

	void onReset(const Module::ResetEvent& e) override {
		moduleChangedFlag = true;

		learningId = -1;
		learnedCc = false;
		learnedNote = false;
		learnedParam = false;
		// Use NoLock because Engine::resetModule() already holds the engine write lock.
		// We may also be called from the MIDIMap() constructor; that could be problematic, 
		// but during construction all ParamHandles are unbound (they do not point to any Module),
		// so this should be safe.
		clearMaps_NoLock();
		mapLen = 1;
		midiInputState.reset();
		for (int i = 0; i < MAX_CHANNELS; i++) {
			slots[i].reset();
		}
		mappingIndicatorHidden = false;
		mappingIndicatorColor = MAPPING_INDICATOR_COLOR_DEFAULT;
		locked = false;
		midiInput.reset();
		midiOutput.reset();
		midiOutput.midi::Output::reset();
		midiIgnoreDevices = false;
		midiResendPeriodically = false;
		midiResendDivider.reset();
		processDivision = 64;
		processDivider.setDivision(processDivision);
		processDivider.reset();
		overlayEnabled = true;
		clearMapsOnLoad = false;

		parameterChangesDirect = false;

		Module::onReset(e);
	}

	void onSampleRateChange(const SampleRateChangeEvent& e) override {
		midiResendDivider.setDivision(e.sampleRate / 2);
		longPressDuration = (uint64_t)(e.sampleRate / 2);
	}

	void processBypass(const ProcessArgs &args) override {
		midi::Message msg;
		// Drain the queue while bypassed
		while (midiInput.tryPop(&msg, args.frame)) {
			(void)0;
		}
		Module::processBypass(args);
	}

	void process(const ProcessArgs &args) override {
		midiInputState.tick();

		// Aquire new MIDI messages from the queue
		midi::Message msg;
		bool midiReceived = false;
		while (midiInput.tryPop(&msg, args.frame)) {
			bool r = midiProcessMessage(msg);
			midiReceived = midiReceived || r;
		}

		// Only step channels when some midi event has been received. Additionally
		// step channels for parameter changes made manually every 128th loop. Notice
		// that midi allows about 1000 messages per second, so checking for changes more often
		// won't lead to higher precision on midi output.
		if (processDivider.process() || midiReceived) {
			processMappings(args.sampleTime);
		}

		// Handle indicators - blinking
		if (indicatorDivider.process()) {
			float t = indicatorDivider.getDivision() * args.sampleTime;
			for (int i = 0; i < mapLen; i++) {
				paramHandles[i].color = mappingIndicatorHidden ? color::BLACK_TRANSPARENT : mappingIndicatorColor;
				if (paramHandles[i].moduleId >= 0) {
					paramHandles[i].process(t, learningId == i);
				}
			}
		}

		if (midiResendPeriodically && midiResendDivider.process()) {
			midiResendFeedback();
		}

		// Expanders
		if (moduleChangedFlag) {
			auto detached = expanders.scan(this);
			if (detached.clk) {
				for (int i = 0; i < MAX_CHANNELS; i++) {
					slots[i].resetClockMode();
				}
			}
			if (detached.fine) {
				ccFineMode = false;
			}
			moduleChangedFlag = false;
		}

		if (auto expClk = expanders.clk()) {
			expClkDriver.process(expClk, slots, mapLen);
		}
		if (auto expFine = expanders.fine()) {
			auto r = expFineDriver.process(expFine);
			if (r.changed) setFineMode(r.enabled, r.precision, r.updateRefPoint);
		}
	}

	void processMappings(float sampleTime) {
		float st = sampleTime * float(processDivision);

		for (int id = 0; id < mapLen; id++) {
			if (!slots[id].isBound())
				continue;

			ParamQuantity* paramQuantity = MappingSlot::resolveTarget(paramHandles[id]);
			if (!paramQuantity)
				continue;

			switch (midiMode) {
				case MIDIMODE::MIDIMODE_DEFAULT: {
					slots[id].param.setParamQuantity(paramQuantity);

					auto r = slots[id].processInput(midiInputState, ccModeOverride, ccFineMode, longPressDuration);
					if (r.value >= 0) {
						slots[id].param.setValue(r.value + r.fine);
						if (overlayEnabled && overlayQueue.capacity() > 0) overlayQueue.push(id);
					}

					slots[id].processOutput(midiOutput, paramQuantity, st);
				} 
				break;

				case MIDIMODE::MIDIMODE_LOCATE: {
					if (slots[id].pollIndicate(midiInputState)) {
						int64_t moduleId = paramQuantity->module->getId();
						taskProcessorUi.enqueue([this, id, moduleId]() {
							ModuleWidget* mw = vcv::getModuleWidget(moduleId);
							paramHandles[id].indicate(mw);
						});
					}
				} 
				break;
			}
		}
	}

	bool midiProcessMessage(const midi::Message& msg) {
		switch (msg.getStatus()) {
			case 0xb: { // cc
				return midiCc(msg);
			}
			case 0x8: {	// note off
				return midiNoteRelease(msg);
			}
			case 0x9: {	// note on
				if (msg.getValue() > 0) {
					return midiNotePress(msg);
				}
				else {
					// Many keyboards send a "note on" command with 0 velocity to mean "note release"
					return midiNoteRelease(msg);
				}
			}
			case 0xf: { // system
				if (msg.getChannel() == 0xf) { // reset
					midiReset();
				}
				return false;
			}
			default: {
				return false;
			}
		}
	}

	bool midiCc(const midi::Message& msg) {
		uint8_t cc = msg.getNote();
		uint8_t value = msg.getValue();
		// Learn
		if (learningId >= 0 && learnedCcLast != cc && (learnedCcLast == -1 || learnedCcLast != cc - 32) && midiInputState.getCc(cc) != value) {
			slots[learningId].bindCc(cc);
			slots[learningId].cc.ccMode = CCMODE::DIRECT;
			learnedCc = true;
			learnedCcLast = cc;
			commitLearn();
			updateMapLen();
			refreshParamHandleText(learningId);
		}
		return midiInputState.setCc(cc, value);
	}

	bool midiNotePress(const midi::Message& msg) {
		uint8_t note = msg.getNote();
		uint8_t vel = msg.getValue();
		// Learn
		if (learningId >= 0 && learnedNoteLast != note) {
			slots[learningId].bindNote(note);
			slots[learningId].note.noteMode = NOTEMODE::MOMENTARY;
			learnedNote = true;
			learnedNoteLast = note;
			commitLearn();
			updateMapLen();
			refreshParamHandleText(learningId);
		}
		return midiInputState.setNote(note, vel);
	}

	bool midiNoteRelease(const midi::Message& msg) {
		uint8_t note = msg.getNote();
		return midiInputState.setNote(note, 0);
	}

	void midiResendFeedback() {
		for (int i = 0; i < MAX_CHANNELS; i++) {
			slots[i].lastValueOut = -1;
			slots[i].cc.resetValue();
			slots[i].note.resetValue();
		}
	}

	void midiReset() {
		for (size_t i = 0; i < MAX_CHANNELS; i++) {
			slots[i].tracker.reset();
		}
	}

	MidiCatParam& getMap(int id) {
		return slots[id].param;
	}

	void clearMap(int id, bool midiOnly = false) {
		learningId = -1;
		slots[id].clearMidi();
		if (!midiOnly) {
			slots[id].label = "";
			APP->engine->updateParamHandle(&paramHandles[id], -1, 0, true);
			updateMapLen();
			refreshParamHandleText(id);
		}
	}

	void clearMaps_WithLock() {
		learningId = -1;
		for (int id = 0; id < MAX_CHANNELS; id++) {
			slots[id].clearMidi();
			slots[id].label = "";
			APP->engine->updateParamHandle(&paramHandles[id], -1, 0, true);
			refreshParamHandleText(id);
		}
		mapLen = 1;
		expMemModuleId = -1;
	}

	void clearMaps_NoLock() {
		learningId = -1;
		for (int id = 0; id < MAX_CHANNELS; id++) {
			slots[id].clearMidi();
			slots[id].label = "";
			APP->engine->updateParamHandle_NoLock(&paramHandles[id], -1, 0, true);
			refreshParamHandleText(id);
		}
		mapLen = 1;
		expMemModuleId = -1;
	}

	void updateMapLen() {
		// Find last nonempty map
		int id;
		for (id = MAX_CHANNELS - 1; id >= 0; id--) {
			if (slots[id].isUsed(paramHandles[id]))
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

		// Copy settings from the previous slot
		if (learningId > 0) {
			bool copy14bit = slots[learningId - 1].cc.get14bit() && learnedCc && learnedCcLast < 32;
			slots[learningId].copySettingsFrom(slots[learningId - 1], copy14bit);
		}
		slots[learningId].label = "";

		// Reset learned state
		learnedCc = false;
		learnedNote = false;
		learnedParam = false;

		// Find next incomplete map
		while (!learnSingleSlot && ++learningId < MAX_CHANNELS) {
			if (slots[learningId].isIncomplete(paramHandles[learningId]))
				return;
		}
		learningId = -1;
	}

	int enableLearn(int id, bool learnSingle = false) {
		if (id == -1) {
			// Find next incomplete map
			while (++id < MAX_CHANNELS) {
				if (slots[id].isEmpty(paramHandles[id]))
					break;
			}
			if (id == MAX_CHANNELS) {
				return -1;
			}
		}

		if (id == mapLen) {
			disableLearn();
			return -1;
		}
		if (learningId != id) {
			learningId = id;
			learnedCc = false;
			learnedCcLast = -1;
			learnedNote = false;
			learnedNoteLast = -1;
			learnedParam = false;
			learnSingleSlot = learnSingle;
		}
		return id;
	}

	void disableLearn() {
		learningId = -1;
	}

	void disableLearn(int id) {
		if (learningId == id) {
			learningId = -1;
		}
	}

	void learnParam(int id, int64_t moduleId, int paramId, bool resetMidiSettings = true) {
		APP->engine->updateParamHandle(&paramHandles[id], moduleId, paramId, true);
		slots[id].param.reset(resetMidiSettings);
		// Reset binding to light. Must use `id`, not `learningId`: moduleBind() and
		// moduleBindMem() call this with learningId == -1, which indexed slots[-1].
		slots[id].param.setLight();
		learnedParam = true;
		commitLearn();
		updateMapLen();
	}

	/** Bind a module to the MIDI-CAT mappings.
	 *  Called from the UI thread.
	 */
	void moduleBind(Module* m, bool keepCcAndNote) {
		if (!m) return;
		if (!keepCcAndNote) {
			clearMaps_WithLock();
		}
		else {
			// Clean up some additional mappings on the end
			for (int i = int(m->params.size()); i < mapLen; i++) {
				APP->engine->updateParamHandle(&paramHandles[i], -1, -1, true);
			}
		}
		for (size_t i = 0; i < m->params.size() && i < MAX_CHANNELS; i++) {
			learnParam(int(i), m->id, int(i));
		}

		updateMapLen();
	}

	/** Bind the module connected to the expander to the MIDI-CAT mappings.
	 *  Called from the UI thread.
	 */
	void moduleBindExpander(bool keepCcAndNote) {
		Module::Expander* exp = &leftExpander;
		if (exp->moduleId < 0) return;
		Module* m = exp->module;
		if (!m) return;
		moduleBind(m, keepCcAndNote);
	}

	/** Bind a module to the slots using the mapping stored for its model in the
	 *  MEM-expander. A sibling of moduleBind()/moduleBindExpander(): it rebinds every
	 *  slot and so goes through the same learn and map-length bookkeeping, differing
	 *  only in where the binding comes from. Does nothing if no mapping is stored.
	 *  Called from the UI thread.
	 */
	void moduleBindMem(Module* m) {
		if (!m) return;
		MemModule* map = expanders.memStore().find(MemStore::keyOf(m));
		if (!map) return;

		clearMaps_WithLock();
		expMemModuleId = m->id;
		int i = 0;
		for (MemParam* it : map->paramMap) {
			learnParam(i, m->id, it->paramId);
			slots[i].fromMemParam(*it);
			i++;
		}
		updateMapLen();
	}

	void refreshParamHandleText(int id) {
		paramHandles[id].text = "MIDI-CAT" + slots[id].bindingText();
	}

	json_t* dataToJson() override {
		json_t* rootJ = json_object();
		json_object_set_new(rootJ, "panelTheme", json_integer(panelTheme));

		json_object_set_new(rootJ, "textScrolling", json_boolean(textScrolling));
		json_object_set_new(rootJ, "mappingIndicatorHidden", json_boolean(mappingIndicatorHidden));
		json_object_set_new(rootJ, "mappingIndicatorColor", json_string(color::toHexString(mappingIndicatorColor).c_str()));
		json_object_set_new(rootJ, "locked", json_boolean(locked));
		json_object_set_new(rootJ, "processDivision", json_integer(processDivision));
		json_object_set_new(rootJ, "overlayEnabled", json_boolean(overlayEnabled));
		json_object_set_new(rootJ, "clearMapsOnLoad", json_boolean(clearMapsOnLoad));

		json_object_set_new(rootJ, "parameterChangesDirect", json_boolean(parameterChangesDirect));

		json_t* mapsJ = json_array();
		for (int id = 0; id < mapLen; id++) {
			json_t* mapJ = json_object();
			json_object_set_new(mapJ, "cc", json_integer(slots[id].cc.getCc()));
			json_object_set_new(mapJ, "ccMode", json_integer((int)slots[id].cc.ccMode));
			json_object_set_new(mapJ, "cc14bit", json_boolean(slots[id].cc.get14bit()));
			json_object_set_new(mapJ, "note", json_integer(slots[id].note.getNote()));
			json_object_set_new(mapJ, "noteMode", json_integer((int)slots[id].note.noteMode));
			json_object_set_new(mapJ, "moduleId", json_integer(paramHandles[id].moduleId));
			json_object_set_new(mapJ, "paramId", json_integer(paramHandles[id].paramId));
			json_object_set_new(mapJ, "label", json_string(slots[id].label.c_str()));
			json_object_set_new(mapJ, "midiOptions", json_integer(slots[id].midiOptions));
			json_object_set_new(mapJ, "slew", json_real(slots[id].param.getSlew()));
			json_object_set_new(mapJ, "min", json_real(slots[id].param.getMin()));
			json_object_set_new(mapJ, "max", json_real(slots[id].param.getMax()));
			json_object_set_new(mapJ, "curve", json_real(slots[id].param.getCurve()));
			json_object_set_new(mapJ, "clockMode", json_integer((int)slots[id].param.clockMode));
			json_object_set_new(mapJ, "clockSource", json_integer(slots[id].param.clockSource));
			json_object_set_new(mapJ, "lightFirstId", json_integer(slots[id].param.lightFirstId));
			json_object_set_new(mapJ, "lightNumColors", json_integer(slots[id].param.lightNumColors));
			json_array_append_new(mapsJ, mapJ);
		}
		json_object_set_new(rootJ, "maps", mapsJ);

		json_object_set_new(rootJ, "midiResendPeriodically", json_boolean(midiResendPeriodically));
		json_object_set_new(rootJ, "midiIgnoreDevices", json_boolean(midiIgnoreDevices));
		json_object_set_new(rootJ, "midiInput", midiInput.toJson());
		json_object_set_new(rootJ, "midiOutput", midiOutput.toJson());
		return rootJ;
	}

	void dataFromJson(json_t* rootJ) override {
		json_t* panelThemeJ = json_object_get(rootJ, "panelTheme");
		if (panelThemeJ) panelTheme = json_integer_value(panelThemeJ);

		json_t* textScrollingJ = json_object_get(rootJ, "textScrolling");
		if (textScrollingJ) textScrolling = json_boolean_value(textScrollingJ);
		json_t* mappingIndicatorHiddenJ = json_object_get(rootJ, "mappingIndicatorHidden");
		if (mappingIndicatorHiddenJ) mappingIndicatorHidden = json_boolean_value(mappingIndicatorHiddenJ);
		json_t* mappingIndicatorColorJ = json_object_get(rootJ, "mappingIndicatorColor");
		if (mappingIndicatorColorJ && json_is_string(mappingIndicatorColorJ)) mappingIndicatorColor = color::fromHexString(json_string_value(mappingIndicatorColorJ));
		json_t* lockedJ = json_object_get(rootJ, "locked");
		if (lockedJ) locked = json_boolean_value(lockedJ);
		json_t* processDivisionJ = json_object_get(rootJ, "processDivision");
		if (processDivisionJ) setProcessDivision(json_integer_value(processDivisionJ));
		json_t* overlayEnabledJ = json_object_get(rootJ, "overlayEnabled");
		if (overlayEnabledJ) overlayEnabled = json_boolean_value(overlayEnabledJ);
		json_t* clearMapsOnLoadJ = json_object_get(rootJ, "clearMapsOnLoad");
		if (clearMapsOnLoadJ) clearMapsOnLoad = json_boolean_value(clearMapsOnLoadJ);

		if (clearMapsOnLoad) {
			// Use NoLock because we're already in an Engine write-lock.
			clearMaps_NoLock();
		}

		json_t* parameterChangesDirectJ = json_object_get(rootJ, "parameterChangesDirect");
		if (parameterChangesDirectJ) setParameterChangesDirect(json_boolean_value(parameterChangesDirectJ));

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
				json_t* cc14bitJ = json_object_get(mapJ, "cc14bit");
				json_t* noteJ = json_object_get(mapJ, "note");
				json_t* noteModeJ = json_object_get(mapJ, "noteMode");
				json_t* moduleIdJ = json_object_get(mapJ, "moduleId");
				json_t* paramIdJ = json_object_get(mapJ, "paramId");
				json_t* labelJ = json_object_get(mapJ, "label");
				json_t* midiOptionsJ = json_object_get(mapJ, "midiOptions");
				json_t* slewJ = json_object_get(mapJ, "slew");
				json_t* minJ = json_object_get(mapJ, "min");
				json_t* maxJ = json_object_get(mapJ, "max");
				json_t* curveJ = json_object_get(mapJ, "curve");
				json_t* clockModeJ = json_object_get(mapJ, "clockMode");
				json_t* clockSourceJ = json_object_get(mapJ, "clockSource");
				json_t* lightFirstIdJ = json_object_get(mapJ, "lightFirstId");
				json_t* lightNumColorsJ = json_object_get(mapJ, "lightNumColors");

				if (!(ccJ || noteJ)) {
					slots[mapIndex].setBinding(-1, -1);
					APP->engine->updateParamHandle_NoLock(&paramHandles[mapIndex], -1, 0, true);
					continue;
				}
				if (!(moduleIdJ || paramIdJ)) {
					APP->engine->updateParamHandle_NoLock(&paramHandles[mapIndex], -1, 0, true);
				}

				slots[mapIndex].setCc(ccJ ? json_integer_value(ccJ) : -1);
				slots[mapIndex].cc.ccMode = (CCMODE)json_integer_value(ccModeJ);
				// A legacy preset predates 14-bit support and has no "cc14bit" key at all --
				// treat that the same as an explicit false, otherwise a slot already in
				// 14-bit mode stays there with a stale 14-bit value range.
				slots[mapIndex].setCc14bit(cc14bitJ ? json_boolean_value(cc14bitJ) : false);
				slots[mapIndex].setNote(noteJ ? json_integer_value(noteJ) : -1);
				slots[mapIndex].note.noteMode = (NOTEMODE)json_integer_value(noteModeJ);
				slots[mapIndex].midiOptions = json_integer_value(midiOptionsJ);
				int64_t moduleId = moduleIdJ ? json_integer_value(moduleIdJ) : -1;
				int paramId = paramIdJ ? json_integer_value(paramIdJ) : 0;
				if (moduleId >= 0) {
					moduleId = idFix(moduleId);
					if (moduleId != paramHandles[mapIndex].moduleId || paramId != paramHandles[mapIndex].paramId) {
						APP->engine->updateParamHandle_NoLock(&paramHandles[mapIndex], moduleId, paramId, false);
						refreshParamHandleText(mapIndex);
					}
				}
				if (const char* label = json_string_value(labelJ)) slots[mapIndex].label = label;
				if (slewJ) slots[mapIndex].param.setSlew(json_real_value(slewJ));
				if (minJ) slots[mapIndex].param.setMin(json_real_value(minJ));
				if (maxJ) slots[mapIndex].param.setMax(json_real_value(maxJ));
				if (curveJ) slots[mapIndex].param.setCurve(json_real_value(curveJ));
				if (clockModeJ) slots[mapIndex].param.clockMode = (MidiCatParam::CLOCKMODE)json_integer_value(clockModeJ);
				if (clockSourceJ) slots[mapIndex].param.clockSource = json_integer_value(clockSourceJ);
				if (lightFirstIdJ) slots[mapIndex].param.lightFirstId = json_integer_value(lightFirstIdJ);
				if (lightNumColorsJ) slots[mapIndex].param.lightNumColors = json_integer_value(lightNumColorsJ);
			}
		}

		updateMapLen();
		idFixClearMap();
		
		json_t* midiResendPeriodicallyJ = json_object_get(rootJ, "midiResendPeriodically");
		if (midiResendPeriodicallyJ) midiResendPeriodically = json_boolean_value(midiResendPeriodicallyJ);

		if (!midiIgnoreDevices) {
			json_t* midiIgnoreDevicesJ = json_object_get(rootJ, "midiIgnoreDevices");
			if (midiIgnoreDevicesJ)	midiIgnoreDevices = json_boolean_value(midiIgnoreDevicesJ);
			json_t* midiInputJ = json_object_get(rootJ, "midiInput");
			if (midiInputJ) midiInput.fromJson(midiInputJ);
			json_t* midiOutputJ = json_object_get(rootJ, "midiOutput");
			if (midiOutputJ) midiOutput.fromJson(midiOutputJ);
		}
	}

	void setProcessDivision(int d) {
		processDivision = d;
		processDivider.setDivision(d);
		processDivider.reset();
	}

	void setMode(MIDIMODE midiMode) {
		if (this->midiMode == midiMode)
			return;
		this->midiMode = midiMode;
		switch (midiMode) {
			case MIDIMODE::MIDIMODE_LOCATE:
				for (int i = 0; i < MAX_CHANNELS; i++)
					slots[i].primeIndicate();
				break;
			default:
				break;
		}
	}

	void setParameterChangesDirect(bool b) {
		parameterChangesDirect = b;
		for (int id = 0; id < MAX_CHANNELS; id++) {
			slots[id].param.parameterChangesDirect = parameterChangesDirect;
		}		
	}

	void setFineMode(bool enabled, float precision, bool updateRefPoint = false) {
		if (enabled) {
			if (!ccFineMode) {
				for (int id = 0; id < MAX_CHANNELS; id++) {
					slots[id].initFineMode();
				}
			}
			for (int id = 0; id < MAX_CHANNELS; id++) {
				slots[id].setFinePrecision(precision, updateRefPoint);
			}
			ccFineMode = true;
		}
		else {
			ccFineMode = false;
		}
	}

	MidiCatParam::CLOCKMODE getClockMode(int id) {
		return slots[id].getClockMode();
	}

	void setClockMode(int id, MidiCatParam::CLOCKMODE mode) {
		slots[id].setClockMode(mode);
	}
};


struct MidiCatCurveMenuItem : CurveMenuItem {
	MidiCatParam* p;
	MidiCatCurveMenuItem(MidiCatParam* p) {
		this->p = p;
	}
	float getCurveValue() override {
		return p->getCurve();
	}
	void setCurveValue(float v) override {
		p->setCurve(v);
	}
};

struct ScalingInputLabel : MenuLabelEx {
	MidiCatParam* p;
	void step() override {
		float min = std::min(p->getMin(), p->getMax());
		float max = std::max(p->getMin(), p->getMax());

		float g1 = rescale(0.f, min, max, p->limitMin, p->limitMax);
		g1 = clamp(g1, p->limitMin, p->limitMax);
		int g1a = std::round(g1);
		float g2 = rescale(1.f, min, max, p->limitMin, p->limitMax);
		g2 = clamp(g2, p->limitMin, p->limitMax);
		int g2a = std::round(g2);

		rightText = rack::string::f("[%i, %i]", g1a, g2a);
	}
}; // struct ScalingInputLabel

struct ScalingOutputLabel : MenuLabelEx {
	MidiCatParam* p;
	void step() override {
		float min = p->getMin();
		float max = p->getMax();

		float f1 = rescale(p->limitMin, p->limitMin, p->limitMax, min, max);
		f1 = clamp(f1, 0.f, 1.f) * 100.f;
		float f2 = rescale(p->limitMax, p->limitMin, p->limitMax, min, max);
		f2 = clamp(f2, 0.f, 1.f) * 100.f;

		rightText = rack::string::f("[%.1f%%, %.1f%%]", f1, f2);
	}
}; // struct ScalingOutputLabel


struct MidiCatSelectionWidget : Widget {
	enum class LEARN_MODE {
		OFF = 0,
		CLEAR = 1,
		APPEND = 2
	};

	MidiCatModule* module;

	LEARN_MODE learnMode = LEARN_MODE::OFF;
	bool bindLights = false;

	bool selecting = false;
	math::Vec selectionStart;
	math::Vec selectionEnd;
	math::Vec mousePos;

	void enableLearn(LEARN_MODE mode, bool bindLights = false) {
		learnMode = learnMode == LEARN_MODE::OFF ? mode : LEARN_MODE::OFF;
		this->bindLights = bindLights;
		GLFWcursor* cursor = glfwCreateStandardCursor(GLFW_CROSSHAIR_CURSOR);
		if (APP->window) glfwSetCursor(APP->window->win, cursor);
	}

	void onHover(const HoverEvent& e) override {
		mousePos = e.pos;
		Widget::onHover(e);
	}

	void onButton(const ButtonEvent& e) override {
		if (learnMode != LEARN_MODE::OFF && e.button == GLFW_MOUSE_BUTTON_LEFT) {
			e.consume(this);
		}
		Widget::onButton(e);
	}

	void onDragStart(const DragStartEvent& e) override {
		if (learnMode != LEARN_MODE::OFF && e.button == GLFW_MOUSE_BUTTON_LEFT) {
			selecting = true;
			selectionStart = mousePos;
			selectionEnd = mousePos;
			e.consume(this);
		}
		Widget::onDragStart(e);
	}

	void onDragEnd(const DragEndEvent& e) override {
		if (selecting && e.button == GLFW_MOUSE_BUTTON_LEFT) {
			mapParamsFromRect();
			selecting = false;
			learnMode = LEARN_MODE::OFF;
			if (APP->window) glfwSetCursor(APP->window->win, NULL);
			e.consume(this);
		}
		Widget::onDragEnd(e);
	}

	void onDragHover(const DragHoverEvent& e) override {
		mousePos = e.pos;
		if (selecting) {
			selectionEnd = mousePos;
		}
		Widget::onDragHover(e);
	}

	void mapParamsFromRect() {
		math::Rect selectionBox = math::Rect::fromCorners(selectionStart, selectionEnd);
		std::list<ModuleWidget*> selected;
		for (ModuleWidget* mw : vcv::getModuleWidgets()) {
			if (selectionBox.intersects(mw->box)) {
				selected.push_back(mw);
			}
		}
		if (selected.size() != 1) {
			return;
		}

		ModuleWidget* mw = selected.front();
		std::list<ParamWidget*> selectedParams;
		math::Rect selectionBox1(selectionBox.pos.minus(mw->box.pos), selectionBox.size);
		getAllDescendentsByTypeAndBox<ParamWidget*>(mw, selectionBox1, selectedParams);

		if (learnMode == LEARN_MODE::CLEAR) {
			module->clearMaps_WithLock();
		}

		selectedParams.reverse();
		for (ParamWidget* pw : selectedParams) {
			if (!pw->module) continue;
			int id = module->enableLearn(-1, true);
			module->learnParam(id, pw->module->getId(), pw->paramId);

			if (bindLights) {
				std::list<ModuleLightWidget*> lw;
				getAllDescendentsByTypeAndBox(mw, pw->box, lw);
				if (lw.size() == 1) {
					module->getMap(id).setLight(lw.front()->firstLightId, lw.front()->getNumColors());
				}
			}
		}
		module->disableLearn();
	}

	void draw(const DrawArgs& args) override {
		// Draw selection rectangle
		if (selecting) {
			nvgBeginPath(args.vg);
			math::Rect selectionBox = math::Rect::fromCorners(selectionStart, selectionEnd);
			nvgRect(args.vg, RECT_ARGS(selectionBox));
			nvgFillColor(args.vg, nvgRGBAf(0, 0, 1, 0.25));
			nvgFill(args.vg);
			nvgStrokeWidth(args.vg, 2.0);
			nvgStrokeColor(args.vg, nvgRGBAf(0, 0, 1, 0.5));
			nvgStroke(args.vg);
		}
	}

	template <class T>
	void getAllDescendentsByTypeAndBox(Widget* w, math::Rect selectionBox, std::list<T>& selected) {
		for (auto it = w->children.rbegin(); it != w->children.rend(); it++) {
			Widget* child = *it;
			// Filter child by visibility and position
			if (!child->visible)
				continue;
			if (!selectionBox.intersects(child->box))
				continue;

			math::Rect selectionBox1(selectionBox.pos.minus(child->box.pos), selectionBox.size);
			getAllDescendentsByTypeAndBox<T>(child, selectionBox1, selected);
			T t = dynamic_cast<T>(child);
			if (t) {
				selected.push_back(t);
			}
		}
	}
}; // struct MidiCatSelectionWidget

struct MidiCatChoice : MapModuleChoice<MAX_CHANNELS, MidiCatModule> {
	MidiCatChoice() {
		textOffset = Vec(6.f, 14.7f);
		color = nvgRGB(0xf0, 0xf0, 0xf0);
	}

	template <class T>
	T getFirstDescendentByPos(Widget* w, Vec pos) {
		for (auto it = w->children.rbegin(); it != w->children.rend(); it++) {
			Widget* child = *it;
			// Filter child by visibility and position
			if (!child->visible)
				continue;
			if (!child->box.contains(pos))
				continue;

			T t = dynamic_cast<T>(child);
			if (t) return t;

			Vec pos1 = pos.minus(child->box.pos);
			t = getFirstDescendentByPos<T>(child, pos1);
			if (t) return t;
		}
		return NULL;
	}

	void onDeselect(const event::Deselect& e) override {
		if (!module) return;
		if (!processEvents) return;

		// Light-binding was not triggerd by context menu, proceed with regular mapping
		if (module->learningLightId != id) {
			MapModuleChoice<MAX_CHANNELS, MidiCatModule>::onDeselect(e);
		}

		if (module->learningLightId == id || (module->learnedParam && id > 0 && module->getMap(id - 1).hasLight())) {
			ModuleLightWidget* lw = NULL;
			Widget* w = APP->event->getDraggedWidget();
			if (w) {
				ModuleWidget* mw = w->getAncestorOfType<ModuleWidget>();
				if (mw) {
					Vec pos = APP->scene->rack->getMousePos().minus(mw->box.pos);
					lw = getFirstDescendentByPos<ModuleLightWidget*>(mw, pos);
				}
			}
			commitLearnLight(lw, module->learningLightId == id);
			e.consume(this);
		}
	}

	void step() override {
		MapModuleChoice<MAX_CHANNELS, MidiCatModule>::step();
		if (module && module->learningLightId == id) {
			text = getSlotPrefix() + "Binding LED...";
		}
	}

	void commitLearnLight(ModuleLightWidget* lw, bool keepBinding = true) {
		if (lw && lw->module == module->paramHandles[id].module) {
			module->getMap(id).setLight(lw->firstLightId, lw->getNumColors());
		}

		if (keepBinding) {
			// Find next slot for binding an LED
			int learningId = id;
			while (++learningId < MAX_CHANNELS) {
				if (module->paramHandles[learningId].moduleId >= 0 && !module->getMap(learningId).hasLight()) {
					module->learningLightId = learningId;
					module->enableLearn(learningId);
					return;
				}
			}

			disableLearnLight();
		}
	}

	void enableLearnLight() {
		module->learningLightId = id;
		module->enableLearn(id);
		APP->event->setSelectedWidget(this);
		GLFWcursor* cursor = glfwCreateStandardCursor(GLFW_CROSSHAIR_CURSOR);
		if (APP->window) glfwSetCursor(APP->window->win, cursor);
	}

	void disableLearnLight() {
		module->learningLightId = -1;
		module->disableLearn();
		if (APP->window) glfwSetCursor(APP->window->win, NULL);
	}

	std::string getSlotPrefix() override {
		if (module) {
			char light = ' ';
			if (module->getMap(id).hasLight()) {
				light = '*';
			}
			if (module->slots[id].cc.getCc() >= 0) {
				return rack::string::f("cc%02d%c", module->slots[id].cc.getCc(), light);
			}
			else if (module->slots[id].note.getNote() >= 0) {
				return rack::string::f(" %s%c", noteName(module->slots[id].note.getNote(), true).c_str(), light);
			}
			else if (module->paramHandles[id].moduleId >= 0) {
				return rack::string::f("....%c", light);
			}
			else {
				return "";
			}
		}
		else {
			// fake data for module browser
			return id % 2 == 0 ? rack::string::f("cc%02d ", id) : rack::string::f(" %s ", noteName(id % 12 + 36, true).c_str());
		}
	}

	std::string getSlotLabel() override {
		return module->slots[id].label;
	}

	void prependContextMenu(Menu* menu) override {
		menu->addChild(createSubmenuItem("MIDI-CAT Menu", "", [=](Menu* menu) {
			ModuleWidget* moduleWidget = vcv::getModuleWidget(module->getId());
			moduleWidget->appendContextMenu(menu);
		}));
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
					module->slots[id].cc.ccMode = ccMode;
				}
				void step() override {
					rightText = module->slots[id].cc.ccMode == ccMode ? "✔" : "";
					MenuItem::step();
				}
			};

			Menu* createChildMenu() override {
				Menu* menu = new Menu;
				menu->addChild(construct<CcModeItem>(&MenuItem::text, "Direct", &CcModeItem::module, module, &CcModeItem::id, id, &CcModeItem::ccMode, CCMODE::DIRECT));
				menu->addChild(construct<CcModeItem>(&MenuItem::text, "Pickup (snap)", &CcModeItem::module, module, &CcModeItem::id, id, &CcModeItem::ccMode, CCMODE::PICKUP1));
				dynamic_cast<MenuItem*>(menu->children.back())->disabled = module->slots[id].param.clockMode != MidiCatParam::CLOCKMODE::OFF;
				menu->addChild(construct<CcModeItem>(&MenuItem::text, "Pickup (jump)", &CcModeItem::module, module, &CcModeItem::id, id, &CcModeItem::ccMode, CCMODE::PICKUP2));
				dynamic_cast<MenuItem*>(menu->children.back())->disabled = module->slots[id].param.clockMode != MidiCatParam::CLOCKMODE::OFF;
				menu->addChild(construct<CcModeItem>(&MenuItem::text, "Toggle", &CcModeItem::module, module, &CcModeItem::id, id, &CcModeItem::ccMode, CCMODE::TOGGLE));
				menu->addChild(construct<CcModeItem>(&MenuItem::text, "Toggle + Value", &CcModeItem::module, module, &CcModeItem::id, id, &CcModeItem::ccMode, CCMODE::TOGGLE_VALUE));
				menu->addChild(construct<CcModeItem>(&MenuItem::text, "Snapped", &CcModeItem::module, module, &CcModeItem::id, id, &CcModeItem::ccMode, CCMODE::SNAPPED));
				menu->addChild(construct<CcModeItem>(&MenuItem::text, "Snapped (short/long)", &CcModeItem::module, module, &CcModeItem::id, id, &CcModeItem::ccMode, CCMODE::SNAPPED_SL));
				return menu;
			}
		}; // struct CcModeMenuItem
		
		struct Cc14bitItem : MenuItem {
			MidiCatModule* module;
			int id;
			void onAction(const event::Action& e) override {
				module->slots[id].setCc14bit(!module->slots[id].cc.get14bit());
			}
			void step() override {
				rightText = CHECKMARK(module->slots[id].cc.get14bit());
				MenuItem::step();
			}
		};

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
					module->slots[id].note.noteMode = noteMode;
				}
				void step() override {
					rightText = module->slots[id].note.noteMode == noteMode ? "✔" : "";
					MenuItem::step();
				}
			};

			Menu* createChildMenu() override {
				Menu* menu = new Menu;
				menu->addChild(construct<NoteModeItem>(&MenuItem::text, "Momentary", &NoteModeItem::module, module, &NoteModeItem::id, id, &NoteModeItem::noteMode, NOTEMODE::MOMENTARY));
				dynamic_cast<MenuItem*>(menu->children.back())->disabled = module->slots[id].param.clockMode != MidiCatParam::CLOCKMODE::OFF;
				menu->addChild(construct<NoteModeItem>(&MenuItem::text, "Momentary + Velocity", &NoteModeItem::module, module, &NoteModeItem::id, id, &NoteModeItem::noteMode, NOTEMODE::MOMENTARY_VEL));
				dynamic_cast<MenuItem*>(menu->children.back())->disabled = module->slots[id].param.clockMode != MidiCatParam::CLOCKMODE::OFF;
				menu->addChild(construct<NoteModeItem>(&MenuItem::text, "Toggle", &NoteModeItem::module, module, &NoteModeItem::id, id, &NoteModeItem::noteMode, NOTEMODE::TOGGLE));
				menu->addChild(construct<NoteModeItem>(&MenuItem::text, "Toggle + Velocity", &NoteModeItem::module, module, &NoteModeItem::id, id, &NoteModeItem::noteMode, NOTEMODE::TOGGLE_VEL));
				menu->addChild(construct<NoteModeItem>(&MenuItem::text, "Snapped", &NoteModeItem::module, module, &NoteModeItem::id, id, &NoteModeItem::noteMode, NOTEMODE::SNAPPED));
				menu->addChild(construct<NoteModeItem>(&MenuItem::text, "Snapped (short/long)", &NoteModeItem::module, module, &NoteModeItem::id, id, &NoteModeItem::noteMode, NOTEMODE::SNAPPED_SL));
				return menu;
			}
		}; // struct NoteModeMenuItem

		struct NoteVelZeroMenuItem : MenuItem {
			MidiCatModule* module;
			int id;

			void onAction(const event::Action& e) override {
				module->slots[id].midiOptions ^= 1UL << MIDIOPTION_VELZERO_BIT;
			}
			void step() override {
				rightText = CHECKMARK((module->slots[id].midiOptions >> MIDIOPTION_VELZERO_BIT) & 1U);
				MenuItem::step();
			}
		}; // struct NoteVelZeroMenuItem

		if (module->slots[id].cc.getCc() >= 0 || module->slots[id].note.getNote() >= 0) {
			menu->addChild(createMenuItem("Clear MIDI assignment", "", [=]() { module->clearMap(id, true); }));
		}

		menu->addChild(createMenuItem("Bind feedback to LED (experimental)", CHECKMARK(module->getMap(id).hasLight()), [this] { enableLearnLight(); }));
		if (module->slots[id].param.hasLight()) {
			menu->addChild(createMenuItem("Remove LED binding", "", [this] { module->getMap(id).setLight(); }));
		}

		if (module->slots[id].cc.getCc() >= 0) {
			menu->addChild(new MenuSeparator());
			menu->addChild(construct<CcModeMenuItem>(&MenuItem::text, "Input mode for CC", &CcModeMenuItem::module, module, &CcModeMenuItem::id, id));
			menu->addChild(construct<Cc14bitItem>(&MenuItem::text, "14-bit", &MenuItem::disabled, module->slots[id].cc.getCc() >= 32, &Cc14bitItem::module, module, &Cc14bitItem::id, id));
		}
		if (module->slots[id].note.getNote() >= 0) {
			menu->addChild(new MenuSeparator());
			menu->addChild(construct<NoteModeMenuItem>(&MenuItem::text, "Input mode for notes", &NoteModeMenuItem::module, module, &NoteModeMenuItem::id, id));
			menu->addChild(construct<NoteVelZeroMenuItem>(&MenuItem::text, "Send \"note on, velocity 0\"", &NoteVelZeroMenuItem::module, module, &NoteVelZeroMenuItem::id, id));
		}

		struct PresetMenuItem : MenuItem {
			MidiCatModule* module;
			int id;
			PresetMenuItem() {
				rightText = RIGHT_ARROW;
			}

			Menu* createChildMenu() override {
				struct PresetItem : MenuItem {
					MidiCatParam* p;
					float min, max;
					void onAction(const event::Action& e) override {
						p->setMin(min);
						p->setMax(max);
					}
				};

				Menu* menu = new Menu;
				menu->addChild(construct<PresetItem>(&MenuItem::text, "Default", &PresetItem::p, &module->slots[id].param, &PresetItem::min, 0.f, &PresetItem::max, 1.f));
				menu->addChild(construct<PresetItem>(&MenuItem::text, "Inverted", &PresetItem::p, &module->slots[id].param, &PresetItem::min, 1.f, &PresetItem::max, 0.f));
				menu->addChild(construct<PresetItem>(&MenuItem::text, "Lower 50%", &PresetItem::p, &module->slots[id].param, &PresetItem::min, 0.f, &PresetItem::max, 0.5f));
				menu->addChild(construct<PresetItem>(&MenuItem::text, "Upper 50%", &PresetItem::p, &module->slots[id].param, &PresetItem::min, 0.5f, &PresetItem::max, 1.f));
				return menu;
			}
		}; // struct PresetMenuItem

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
						module->slots[id].label = text;

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
					module->slots[id].label = "";
				}
			};

			Menu* createChildMenu() override {
				Menu* menu = new Menu;

				LabelField* labelField = new LabelField;
				labelField->placeholder = "Label";
				labelField->text = module->slots[id].label;
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
		}; // struct LabelMenuItem

		menu->addChild(Rack::createSlider(
			[this]() { return module->slots[id].param.getSlew(); },
			[this](float v) { module->slots[id].param.setSlew(clamp(v, 0.f, 5.f)); },
			0.f, 5.f, 0.f, "Slew-limiting", "", 1.f, 220.0f
		));
		menu->addChild(construct<MenuLabel>(&MenuLabel::text, "Scaling"));
		std::string l = string::f("Input %s", module->slots[id].cc.getCc() >= 0 ? "MIDI CC" : (module->slots[id].note.getNote() >= 0 ? "MIDI vel" : ""));
		menu->addChild(construct<ScalingInputLabel>(&MenuLabel::text, l, &ScalingInputLabel::p, &module->slots[id].param));
		menu->addChild(construct<ScalingOutputLabel>(&MenuLabel::text, "Parameter range", &ScalingOutputLabel::p, &module->slots[id].param));
		menu->addChild(Rack::createSlider(
			[this]() { return module->slots[id].param.getMin(); },
			[this](float v) { module->slots[id].param.setMin(v); },
			-1.f, 2.f, 0.f, "Low", "%", 100.f, 220.0f
		));
		menu->addChild(Rack::createSlider(
			[this]() { return module->slots[id].param.getMax(); },
			[this](float v) { module->slots[id].param.setMax(v); },
			-1.f, 2.f, 1.f, "High", "%", 100.f, 220.0f
		));
		menu->addChild(construct<PresetMenuItem>(&MenuItem::text, "Presets", &PresetMenuItem::module, module, &PresetMenuItem::id, id));
		menu->addChild(new MidiCatCurveMenuItem(&module->slots[id].param));
		menu->addChild(new MenuSeparator());
		menu->addChild(construct<LabelMenuItem>(&MenuItem::text, "Custom label", &LabelMenuItem::module, module, &LabelMenuItem::id, id));

		if (module->expanders.clk() != NULL) {
			menu->addChild(new MenuSeparator());
			menu->addChild(createMenuLabel("CLK-expander"));
			if (!module->getMap(id).hasLight()) {
				menu->addChild(StoermelderPackOne::Rack::createMapSubmenuItem<MidiCatParam::CLOCKMODE>("Quantization",
					{
						{ MidiCatParam::CLOCKMODE::OFF, "Off" },
						{ MidiCatParam::CLOCKMODE::ARM, "On (instant feedback)" },
						{ MidiCatParam::CLOCKMODE::ARM_DEFERRED_FEEDBACK, "On (deferred feedback)" }
					},
					[=]() { return module->getClockMode(id); },
					[=](MidiCatParam::CLOCKMODE v) { module->setClockMode(id, v); }
				));
			}
			else {
				menu->addChild(StoermelderPackOne::Rack::createMapSubmenuItem<MidiCatParam::CLOCKMODE>("Quantization",
					{
						{ MidiCatParam::CLOCKMODE::OFF, "Off" },
						{ MidiCatParam::CLOCKMODE::ARM, "On" },
					},
					[=]() { return module->getClockMode(id); },
					[=](MidiCatParam::CLOCKMODE v) { module->setClockMode(id, v); }
				));
			}
			menu->addChild(StoermelderPackOne::Rack::createMapPtrSubmenuItem("Source",
				{
					{ 0, "Clock 1" },
					{ 1, "Clock 2" },
					{ 2, "Clock 3" },
					{ 3, "Clock 4" }
				},
				&module->slots[id].param.clockSource
			));
		}
	}
};

struct MidiCatDisplay : MapModuleDisplay<MAX_CHANNELS, MidiCatModule, MidiCatChoice>, OverlayMessageProvider {
	void step() override {
		if (module) {
			int mapLen = module->mapLen;
			for (int id = 1; id < MAX_CHANNELS; id++) {
				choices[id]->visible = (id < mapLen);
				separators[id]->visible = (id < mapLen);
			}
		}
		MapModuleDisplay<MAX_CHANNELS, MidiCatModule, MidiCatChoice>::step();
	}

	int nextOverlayMessageId() override {
		if (module->overlayQueue.empty())
			return -1;
		return module->overlayQueue.shift();
	}

	void getOverlayMessage(int id, Message& m) override {
		ParamQuantity* paramQuantity = choices[id]->getParamQuantity();
		if (!paramQuantity) return;

		std::string label = choices[id]->getSlotLabel();
		if (label == "") label = paramQuantity->name;

		m.title = paramQuantity->getDisplayValueString() + paramQuantity->getUnit();
		m.subtitle[0] = paramQuantity->module->model->name;
		m.subtitle[1] = label;
	}
};

struct MidiCatBaseWidget : ThemedModuleWidget<MidiCatModule>, ParamWidgetContextExtender {
	MidiCatModule* module;
	MidiCatDisplay* mapWidget;
	MidiCatSelectionWidget* selectionWidget = NULL;

	MidiCatMemBase* expMem;
	BufferedSwitchQuantity* expMemPrevQuantity;
	BufferedSwitchQuantity* expMemNextQuantity;
	BufferedSwitchQuantity* expMemParamQuantity;

	MidiCatCtxBase* expCtx;
	BufferedSwitchQuantity* expCtxMapQuantity;

	enum class LEARN_MODE {
		OFF = 0,
		BIND_CLEAR = 1,
		BIND_KEEP = 2,
		MEM = 3
	};

	LEARN_MODE learnMode = LEARN_MODE::OFF;

	MidiCatBaseWidget(MidiCatModule* module, std::string baseName)
		: ThemedModuleWidget<MidiCatModule>(module, baseName, "MidiCat") {
		setModule(module);
		this->module = module;

		addChild(createWidget<StoermelderBlackScrew>(Vec(RACK_GRID_WIDTH, 0)));
		addChild(createWidget<StoermelderBlackScrew>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, 0)));
		addChild(createWidget<StoermelderBlackScrew>(Vec(RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));
		addChild(createWidget<StoermelderBlackScrew>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));

		if (module) {
			selectionWidget = new MidiCatSelectionWidget;
			selectionWidget->module = module;
			APP->scene->rack->addChild(selectionWidget);
		}
	}

	~MidiCatBaseWidget() {
		if (learnMode != LEARN_MODE::OFF && APP->window) {
			if (APP->window) glfwSetCursor(APP->window->win, NULL);
		}

		if (selectionWidget) {
			APP->scene->rack->removeChild(selectionWidget);
			delete selectionWidget;
		}
	}

	void loadMidiMapPreset_dialog() {
		std::string path = vcv::ui::openDialog(PRESET_FILTERS, "");
		if (path.empty()) {
			// No path selected
			return;
		}

		loadMidiMapPreset_action(path);
	}

	void loadMidiMapPreset_action(std::string filename) {
		INFO("Loading preset %s", filename.c_str());

		std::string data;
		if (!vcv::fs::read(filename, data)) {
			WARN("Could not load patch file %s", filename.c_str());
			return;
		}

		std::string error;
		json_t* moduleJ = vcv::parseJson(data, error);
		if (!moduleJ) {
			std::string message = rack::string::f("File is not a valid patch file. %s", error.c_str());
			vcv::ui::message(vcv::MessageType::WARNING, vcv::MessageButtons::OK, message.c_str());
			return;
		}
		DEFER({
			json_decref(moduleJ);
		});

		if (!loadMidiMapPreset_convert(moduleJ))
			return;

		// history::ModuleChange
		rack::history::ModuleChange* h = new rack::history::ModuleChange;
		h->name = "load module preset";
		h->moduleId = module->id;
		h->oldModuleJ = toJson();

		module->fromJson(moduleJ);

		h->newModuleJ = toJson();
		vcv::history::push(h);
	}

	bool loadMidiMapPreset_convert(json_t* moduleJ) {
		// Null-safe: hand-edited or foreign preset files may lack these keys or
		// carry non-string values; json_string_value would return NULL (UB).
		json_t* pluginJ = json_object_get(moduleJ, "plugin");
		json_t* modelJ = json_object_get(moduleJ, "model");
		std::string pluginSlug = json_is_string(pluginJ) ? json_string_value(pluginJ) : "";
		std::string modelSlug = json_is_string(modelJ) ? json_string_value(modelJ) : "";

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
			// MEM-expander
			auto expMem_ = module->expanders.mem();
			if (expMem_ != expMem) {
				expMem = expMem_;
				if (expMem) {
					expMemPrevQuantity = dynamic_cast<BufferedSwitchQuantity*>(expMem->paramQuantities[1]);
					expMemPrevQuantity->resetBuffer();
					expMemNextQuantity = dynamic_cast<BufferedSwitchQuantity*>(expMem->paramQuantities[2]);
					expMemNextQuantity->resetBuffer();
					expMemParamQuantity = dynamic_cast<BufferedSwitchQuantity*>(expMem->paramQuantities[0]);
					expMemParamQuantity->resetBuffer();
				}
			}
			if (expMem) {
				if (expMemPrevQuantity->getBuffer()) {
					expMemPrevQuantity->resetBuffer();
					expMemPrevModule();
				}
				if (expMemNextQuantity->getBuffer()) {
					expMemNextQuantity->resetBuffer();
					expMemNextModule();
				}
				if (expMemParamQuantity->getBuffer()) {
					expMemParamQuantity->resetBuffer();
					enableLearn(LEARN_MODE::MEM);
				}
				expMem->lights[0].setBrightness(learnMode == LEARN_MODE::MEM);
			}

			// CTX-expander
			auto expCtx_ = module->expanders.ctx();
			if (expCtx_ != expCtx) {
				expCtx = expCtx_;
				if (expCtx) {
					expCtxMapQuantity = dynamic_cast<BufferedSwitchQuantity*>(expCtx->paramQuantities[0]);
					expCtxMapQuantity->resetBuffer();
				}
			}
			if (expCtx) {
				if (expCtxMapQuantity->getBuffer()) {
					expCtxMapQuantity->resetBuffer();
					module->enableLearn(-1, true);
				}
			}

			module->taskProcessorUi.process();
		}

		ParamWidgetContextExtender::step();
	}

	void expMemPrevModule() {
		std::vector<ModuleWidget*> modules = APP->scene->rack->getModules();
		auto sort = [&](Widget* w1, Widget* w2) {
			auto t1 = std::make_tuple(w1->box.pos.y, w1->box.pos.x);
			auto t2 = std::make_tuple(w2->box.pos.y, w2->box.pos.x);
			return t1 > t2;
		};
		std::sort(modules.begin(), modules.end(), sort);
		expMemScanModules(modules);
	}

	void expMemNextModule() {
		std::vector<ModuleWidget*> modules = APP->scene->rack->getModules();
		auto sort = [&](Widget* w1, Widget* w2) {
			auto t1 = std::make_tuple(w1->box.pos.y, w1->box.pos.x);
			auto t2 = std::make_tuple(w2->box.pos.y, w2->box.pos.x);
			return t1 < t2;
		};
		std::sort(modules.begin(), modules.end(), sort);
		expMemScanModules(modules);
	}

	void expMemScanModules(std::vector<ModuleWidget*>& modules) {
		f:
		std::vector<ModuleWidget*>::iterator it = modules.begin();
		// Scan for current module in the list
		if (module->expMemModuleId != -1) {
			for (; it != modules.end(); it++) {
				ModuleWidget* mw = *it;
				Module* m = mw->module;
				if (m->id == module->expMemModuleId) {
					it++;
					break;
				}
			}
			// Module not found
			if (it == modules.end()) {
				it = modules.begin();
			}
		}
		// Scan for next module with stored mapping
		for (; it != modules.end(); it++) {
			ModuleWidget* mw = *it;
			Module* m = mw->module;
			if (module->expanders.memStore().test(m)) {
				module->moduleBindMem(m);
				return;
			}
		}
		// No module found yet -> retry from the beginning
		if (module->expMemModuleId != -1) {
			module->expMemModuleId = -1;
			goto f;
		}
	}

	void extendParamWidgetContextMenu(ParamWidget* pw, Menu* menu) override {
		if (!module) return;
		if (module->learningId >= 0) return;
		ParamQuantity* pq = pw->getParamQuantity();
		if (!pq) return;
		
		struct MidiCatBeginItem : MenuLabel {
			MidiCatBeginItem() {
				text = "MIDI-CAT";
			}
		};

		struct MidiCatEndItem : MenuEntry {
			MidiCatEndItem() {
				box.size = Vec();
			}
		};

		struct MapMenuItem : MenuItem {
			MidiCatModule* module;
			ParamQuantity* pq;
			int currentId = -1;

			MapMenuItem() {
				rightText = RIGHT_ARROW;
			}

			Menu* createChildMenu() override {
				struct MapItem : MenuItem {
					MidiCatModule* module;
					int currentId;
					void onAction(const event::Action& e) override {
						module->enableLearn(currentId, true);
					}
				};

				struct MapEmptyItem : MenuItem {
					MidiCatModule* module;
					ParamQuantity* pq;
					void onAction(const event::Action& e) override {
						int id = module->enableLearn(-1, true);
						if (id >= 0) module->learnParam(id, pq->module->id, pq->paramId);
					}
				};

				struct RemapItem : MenuItem {
					MidiCatModule* module;
					ParamQuantity* pq;
					int id;
					int currentId;
					void onAction(const event::Action& e) override {
						module->learnParam(id, pq->module->id, pq->paramId, false);
					}
					void step() override {
						rightText = CHECKMARK(id == currentId);
						MenuItem::step();
					}
				};

				Menu* menu = new Menu;
				if (currentId < 0) {
					menu->addChild(construct<MapEmptyItem>(&MenuItem::text, "Learn MIDI", &MapEmptyItem::module, module, &MapEmptyItem::pq, pq));
				}
				else {
					menu->addChild(construct<MapItem>(&MenuItem::text, "Learn MIDI", &MapItem::module, module, &MapItem::currentId, currentId));
				}

				if (module->mapLen > 0) {
					menu->addChild(new MenuSeparator);
					for (int i = 0; i < module->mapLen; i++) {
						if (module->slots[i].cc.getCc() >= 0 || module->slots[i].note.getNote() >= 0) {
							std::string text = module->slots[i].menuLabel();
							menu->addChild(construct<RemapItem>(&MenuItem::text, text, &RemapItem::module, module, &RemapItem::pq, pq, &RemapItem::id, i, &RemapItem::currentId, currentId));
						}
					}
				}
				return menu;
			} 
		};

		std::list<Widget*>::iterator beg = menu->children.begin();
		std::list<Widget*>::iterator end = menu->children.end();
		std::list<Widget*>::iterator itCvBegin = end;
		std::list<Widget*>::iterator itCvEnd = end;
		
		for (auto it = beg; it != end; it++) {
			if (itCvBegin == end) {
				MidiCatBeginItem* ml = dynamic_cast<MidiCatBeginItem*>(*it);
				if (ml) { itCvBegin = it; continue; }
			}
			else {
				MidiCatEndItem* ml = dynamic_cast<MidiCatEndItem*>(*it);
				if (ml) { itCvEnd = it; break; }
			}
		}

		for (int id = 0; id < module->mapLen; id++) {
			if (module->paramHandles[id].moduleId == pq->module->id && module->paramHandles[id].paramId == pq->paramId) {
				std::string midiCatId = expCtx ? "on \"" + expCtx->getMidiCatId() + "\"" : "";
				std::list<Widget*> w;
				w.push_back(construct<MapMenuItem>(&MenuItem::text, rack::string::f("Re-map %s", midiCatId.c_str()), &MapMenuItem::module, module, &MapMenuItem::pq, pq, &MapMenuItem::currentId, id));
				w.push_back(Rack::createSlider(
					[this, id]() { return module->slots[id].param.getSlew(); },
					[this, id](float v) { module->slots[id].param.setSlew(clamp(v, 0.f, 5.f)); },
					0.f, 5.f, 0.f, "Slew-limiting", "", 1.f, 220.0f
				));
				w.push_back(construct<MenuLabel>(&MenuLabel::text, "Scaling"));
				std::string l = rack::string::f("Input %s", module->slots[id].cc.getCc() >= 0 ? "MIDI CC" : (module->slots[id].note.getNote() >= 0 ? "MIDI vel" : ""));
				w.push_back(construct<ScalingInputLabel>(&MenuLabel::text, l, &ScalingInputLabel::p, &module->slots[id].param));
				w.push_back(construct<ScalingOutputLabel>(&MenuLabel::text, "Parameter range", &ScalingOutputLabel::p, &module->slots[id].param));
				w.push_back(Rack::createSlider(
					[this, id]() { return module->slots[id].param.getMin(); },
					[this, id](float v) { module->slots[id].param.setMin(v); },
					-1.f, 2.f, 0.f, "Low", "%", 100.f, 220.0f
				));
				w.push_back(Rack::createSlider(
					[this, id]() { return module->slots[id].param.getMax(); },
					[this, id](float v) { module->slots[id].param.setMax(v); },
					-1.f, 2.f, 1.f, "High", "%", 100.f, 220.0f
				));
				w.push_back(new MidiCatCurveMenuItem(&module->slots[id].param));
				w.push_back(construct<CenterModuleItem>(&MenuItem::text, "Go to mapping module", &CenterModuleItem::mw, this));
				w.push_back(new MidiCatEndItem);

				if (itCvBegin == end) {
					menu->addChild(new MenuSeparator);
					menu->addChild(construct<MidiCatBeginItem>());
					for (Widget* wm : w) {
						menu->addChild(wm);
					}
				}
				else {
					for (auto i = w.rbegin(); i != w.rend(); ++i) {
						Widget* wm = *i;
						menu->addChild(wm);
						auto it = std::prev(menu->children.end());
						menu->children.splice(std::next(itCvBegin), menu->children, it);
					}
				}
				return;
			}
		}

		if (expCtx) {
			std::string midiCatId = expCtx->getMidiCatId();
			if (midiCatId != "") {
				MenuItem* mapMenuItem = construct<MapMenuItem>(&MenuItem::text, rack::string::f("Map on \"%s\"", midiCatId.c_str()), &MapMenuItem::module, module, &MapMenuItem::pq, pq);
				if (itCvBegin == end) {
					menu->addChild(new MenuSeparator);
					menu->addChild(construct<MidiCatBeginItem>());
					menu->addChild(mapMenuItem);
				}
				else {
					menu->addChild(mapMenuItem);
					auto it = std::find(beg, end, mapMenuItem);
					menu->children.splice(std::next(itCvEnd == end ? itCvBegin : itCvEnd), menu->children, it);
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
					module->moduleBindMem(m); break;
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
				case GLFW_KEY_R: {
					if ((e.mods & RACK_MOD_MASK) == (RACK_MOD_SHIFT | RACK_MOD_CTRL)) {
						MidiCatModule* module = dynamic_cast<MidiCatModule*>(this->module);
						module->midiReset();
						e.consume(this);
					}
					break;
				}
				case GLFW_KEY_F: {
					if ((e.mods & RACK_MOD_MASK) == (RACK_MOD_SHIFT | RACK_MOD_CTRL)) {
						MidiCatModule* module = dynamic_cast<MidiCatModule*>(this->module);
						module->midiResendFeedback();
						e.consume(this);
					}
					break;
				}
				case GLFW_KEY_I: {
					if ((e.mods & RACK_MOD_MASK) == (RACK_MOD_SHIFT | RACK_MOD_CTRL)) {
						MidiCatModule* module = dynamic_cast<MidiCatModule*>(this->module);
						module->ccModeOverride = true;
						e.consume(this);
					}
					break;
				}
			}
		}
		if (e.action == GLFW_RELEASE) {
			switch (e.key) {
				case GLFW_KEY_I: {
					if ((e.mods & RACK_MOD_MASK) == (RACK_MOD_SHIFT | RACK_MOD_CTRL)) {
						MidiCatModule* module = dynamic_cast<MidiCatModule*>(this->module);
						module->ccModeOverride = false;
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
		APP->event->setSelectedWidget(this);
		GLFWcursor* cursor = NULL;
		if (learnMode != LEARN_MODE::OFF) {
			cursor = glfwCreateStandardCursor(GLFW_CROSSHAIR_CURSOR);
		}
		if (APP->window) glfwSetCursor(APP->window->win, cursor);
	}

	void disableLearn() {
		learnMode = LEARN_MODE::OFF;
		if (APP->window) glfwSetCursor(APP->window->win, NULL);
	}

	void appendContextMenu(Menu* menu) override {
		int menuSize = menu->children.size();
		if (menuSize > 0) {
			menu->addChild(new MenuSeparator());
		}
		menu->addChild(createSubmenuItem("MIDI Input", "", [=](Menu* menu) { appendMidiMenu(menu, &module->midiInput); }));
		menu->addChild(createSubmenuItem("MIDI Output", "", [=](Menu* menu) { appendMidiMenu(menu, &module->midiOutput); }));
		/*
		menu->addChild(createMenuItem("Reset", "", [=]() {
			module->midiReset();
		}));
		*/

		if (menuSize > 0) {
			ThemedModuleWidget<MidiCatModule>::appendContextMenu(menu);
		}

		int sampleRate = int(APP->engine->getSampleRate());
		menu->addChild(new MenuSeparator());
		menu->addChild(createSubmenuItem("Preset load", "",
			[=](Menu* menu) {
				menu->addChild(createBoolPtrMenuItem("Ignore MIDI devices", "", &module->midiIgnoreDevices));
				menu->addChild(createBoolPtrMenuItem("Clear mapping slots", "", &module->clearMapsOnLoad));
			}
		));
		menu->addChild(StoermelderPackOne::Rack::createMapSubmenuItem<int>("Precision", {
				{ 1, rack::string::f("Samplerate (%i Hz)", sampleRate / 1) },
				{ 8, rack::string::f("High (%i Hz)", sampleRate / 8) },
				{ 64, rack::string::f("Moderate (%i Hz)", sampleRate / 64) },
				{ 256, rack::string::f("Lowest (%i Hz)", sampleRate / 256) }
			},
			[=]() {
				return module->processDivision;
			},
			[=](int division) {
				module->setProcessDivision(division);
			}
		));
		if (settings::isPlugin) {
			menu->addChild(createBoolMenuItem("Report parameter changes", "", 
				[=]() {
					return module->parameterChangesDirect;
				},
				[=](bool b) {
					module->setParameterChangesDirect(b);
				}
			));
		}
		menu->addChild(StoermelderPackOne::Rack::createMapSubmenuItem<MIDIMODE>("Mode", {
				{ MIDIMODE::MIDIMODE_DEFAULT, "Operating" },
				{ MIDIMODE::MIDIMODE_LOCATE, "Locate and indicate" }
			},
			[=]() {
				return module->midiMode;
			},
			[=](MIDIMODE midiMode) {
				module->setMode(midiMode);
			}
		));
		menu->addChild(createSubmenuItem("Re-send MIDI feedback", "",
			[=](Menu* menu) {
				menu->addChild(createMenuItem("Now", "", [=]() { module->midiResendFeedback(); }));
				menu->addChild(createBoolPtrMenuItem("Periodically", "", &module->midiResendPeriodically));
			}
		));
		menu->addChild(createMenuItem("Import MIDI-MAP preset", "", [=]() { loadMidiMapPreset_dialog(); }));

		menu->addChild(new MenuSeparator());
		menu->addChild(createSubmenuItem("User interface", "",
			[=](Menu* menu) {
				menu->addChild(createBoolPtrMenuItem("Text scrolling", "", &module->textScrolling));
				menu->addChild(createBoolPtrMenuItem("Lock mapping slots", "", &module->locked));
				menu->addChild(new MenuSeparator);
				menu->addChild(createMenuLabel("Mapping indicators"));
				menu->addChild(createBoolPtrMenuItem("Hide", "", &module->mappingIndicatorHidden));
				menu->addChild(construct<MenuColorLabel>(&MenuColorLabel::fillColor, &module->mappingIndicatorColor));
				menu->addChild(construct<MenuColorPicker>(&MenuColorPicker::color, &module->mappingIndicatorColor));
				menu->addChild(createMenuItem("Reset color", "", [=]() {
					module->mappingIndicatorColor = MAPPING_INDICATOR_COLOR_DEFAULT;
				}));
			}
		));
		menu->addChild(createBoolPtrMenuItem("Status overlay", "", &module->overlayEnabled));

		menu->addChild(new MenuSeparator());
		menu->addChild(createMenuItem("Clear all mappings", "", [=]() { module->clearMaps_WithLock(); }));
		menu->addChild(createSubmenuItem("Map module (left)", "",
			[=](Menu* menu) {
				menu->addChild(createMenuItem("Clear first", RACK_MOD_CTRL_NAME "+" RACK_MOD_SHIFT_NAME "+E", [=]() { module->moduleBindExpander(false); }));
				menu->addChild(createMenuItem("Keep MIDI assignments", RACK_MOD_SHIFT_NAME "+E", [=]() { module->moduleBindExpander(true); }));
			}
		));
		menu->addChild(createSubmenuItem("Map module (select)", "",
			[=](Menu* menu) {
				menu->addChild(createMenuItem("Clear first", RACK_MOD_CTRL_NAME "+" RACK_MOD_SHIFT_NAME "+D", [=]() { enableLearn(LEARN_MODE::BIND_CLEAR); }));
				menu->addChild(createMenuItem("Keep MIDI assignments", RACK_MOD_SHIFT_NAME "+D", [=]() { enableLearn(LEARN_MODE::BIND_KEEP); }));
			}
		));
		menu->addChild(createSubmenuItem("Map parameters by selection", "",
			[=](Menu* menu) {
				menu->addChild(createMenuItem("Clear first", "", [=]() { selectionWidget->enableLearn(MidiCatSelectionWidget::LEARN_MODE::CLEAR); }));
				menu->addChild(createMenuItem("Clear first, bind LEDs (experimental)", "", [=]() { selectionWidget->enableLearn(MidiCatSelectionWidget::LEARN_MODE::CLEAR, true); }));
				menu->addChild(createMenuItem("Append", "", [=]() { selectionWidget->enableLearn(MidiCatSelectionWidget::LEARN_MODE::APPEND); }));
				menu->addChild(createMenuItem("Append, bind LEDs (experimental)", "", [=]() { selectionWidget->enableLearn(MidiCatSelectionWidget::LEARN_MODE::APPEND, true); }));
			}
		));

		if (module->expanders.mem() != NULL) {
			appendContextMenuMem(menu);
		}
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
								module->expanders.memStore().erase(MemStore::Key(pluginSlug, moduleSlug));
							}
						}; // DeleteItem

						Menu* menu = new Menu;
						menu->addChild(construct<DeleteItem>(&MenuItem::text, "Delete", &DeleteItem::module, module, &DeleteItem::pluginSlug, pluginSlug, &DeleteItem::moduleSlug, moduleSlug));
						return menu;
					}
				}; // MidimapModuleItem

				std::list<std::pair<std::string, MidimapModuleItem*>> l;
				auto expMem = module->expanders.mem();
				for (auto it : *expMem->getMemStorage()) {
					MemModule* a = it.second;
					MidimapModuleItem* midimapModuleItem = new MidimapModuleItem;
					midimapModuleItem->text = rack::string::f("%s %s", a->pluginName.c_str(), a->moduleName.c_str());
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
						module->expanders.memStore().save(MemStore::Key(pluginSlug, moduleSlug), module->slots, module->paramHandles, MAX_CHANNELS);
					}
				}; // SaveItem

				typedef std::pair<std::string, std::string> ppair;
				std::list<std::pair<std::string, ppair>> list;
				std::set<ppair> s;
				for (size_t i = 0; i < MAX_CHANNELS; i++) {
					int64_t moduleId = module->paramHandles[i].moduleId;
					if (moduleId < 0) continue;
					Module* m = module->paramHandles[i].module;
					auto q = ppair(m->model->plugin->slug, m->model->slug);
					if (s.find(q) != s.end()) continue;
					s.insert(q);

					if (!m) continue;
					std::string l = rack::string::f("%s %s", m->model->plugin->name.c_str(), m->model->name.c_str());
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

		menu->addChild(new MenuSeparator());
		menu->addChild(createMenuLabel("MEM-expander"));
		menu->addChild(construct<MapMenuItem>(&MenuItem::text, "Available mappings", &MapMenuItem::module, module));
		menu->addChild(construct<SaveMenuItem>(&MenuItem::text, "Store mapping", &SaveMenuItem::module, module));
		menu->addChild(createMenuItem("Apply mapping", RACK_MOD_SHIFT_NAME "+V", [=]() { enableLearn(LEARN_MODE::MEM); }));
		menu->addChild(createSubmenuItem("Restriction", 
			expMem->getMemModuleRestriction()->size() == 0 ? "Disabled" : rack::string::f("%i modules", expMem->getMemModuleRestriction()->size()),
			[=](Menu* menu) {
				if (APP->scene->rack->hasSelection()) {
					menu->addChild(createMenuItem("Save selection", "", [=]() {
						expMem->getMemModuleRestriction()->clear();
						for (auto it : APP->scene->rack->getSelected()) {
							expMem->getMemModuleRestriction()->insert(it->getModule()->getId());
						}
					}));
				}
				if (expMem->getMemModuleRestriction()->size() > 0) {
					menu->addChild(createMenuItem("Show", "", [=]() {
						APP->scene->rack->deselectAll();
						for (auto it : *expMem->getMemModuleRestriction()) {
							ModuleWidget* mw = APP->scene->rack->getModule(it);
							APP->scene->rack->select(mw);
						}
					}));
					menu->addChild(createMenuItem("Clear", "", [=]() { expMem->getMemModuleRestriction()->clear(); }));
				}
			}
		));
	}
};

struct MidiCatWidget : MidiCatBaseWidget {
	MidiCatWidget(MidiCatModule* module) : MidiCatBaseWidget(module, "MidiCat") {
		MidiWidget<>* midiInputWidget = createWidget<MidiWidget<>>(Vec(0.0f, 36.4f));
		midiInputWidget->box.size = Vec(150.0f, 67.0f);
		midiInputWidget->setMidiPort(module ? &module->midiInput : NULL, "In");
		addChild(midiInputWidget);

		MidiWidget<>* midiOutputWidget = createWidget<MidiWidget<>>(Vec(0.0f, 107.4f));
		midiOutputWidget->box.size = Vec(150.0f, 67.0f);
		midiOutputWidget->setMidiPort(module ? &module->midiOutput : NULL, "Out");
		addChild(midiOutputWidget);

		mapWidget = createWidget<MidiCatDisplay>(Vec(0.0f, 178.5f));
		mapWidget->box.size = Vec(150.0f, 164.7f);
		mapWidget->setModule(module);
		addChild(mapWidget);

		if (module) {
			OverlayMessageWidget::registerProvider(mapWidget);
		}
	}

	~MidiCatWidget() {
		if (module) {
			OverlayMessageWidget::unregisterProvider(mapWidget);
		}
	}
};

struct MidiCatXlWidget : MidiCatBaseWidget {
	MidiCatXlWidget(MidiCatModule* module) : MidiCatBaseWidget(module, "MidiCatXl") {
		mapWidget = createWidget<MidiCatDisplay>(Vec(0.0f, 36.4f));
		mapWidget->box.size = Vec(270.0f, 307.0f);
		mapWidget->setModule(module);
		addChild(mapWidget);

		if (module) {
			OverlayMessageWidget::registerProvider(mapWidget);
		}
	}

	~MidiCatXlWidget() {
		if (module) {
			OverlayMessageWidget::unregisterProvider(mapWidget);
		}
	}
};

} // namespace MidiCat
} // namespace StoermelderPackOne

Model* modelMidiCat = createModel<StoermelderPackOne::MidiCat::MidiCatModule, StoermelderPackOne::MidiCat::MidiCatWidget>("MidiCat");
Model* modelMidiCatXl = createModel<StoermelderPackOne::MidiCat::MidiCatModule, StoermelderPackOne::MidiCat::MidiCatXlWidget>("MidiCatXl");