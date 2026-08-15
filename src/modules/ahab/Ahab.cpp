#include "../../plugin.hpp"
#include "../../pluginhelpers.hpp"
#include "../../components/Knobs.hpp"
#include "../../ui/InfoWindow.hpp"
#include "../../ui/FocusMode.hpp"
#include "orca_examples.hpp"
#include "AhabSim.hpp"
#include "AhabMidiDriver.hpp"
#include "AhabRenderer.hpp"
#include "AhabRandomizer.hpp"
#include <osdialog.h>

extern "C" {
	#include <orca-c/osc_out.h>
}

namespace StoermelderPackOne {
namespace Ahab {

struct AhabModule : Module {
	enum ParamIds {
		BPM_PARAM,
		RUN_PARAM,
		CLK_PARAM,
		NUM_PARAMS
	};
	enum InputIds {
		CLK_INPUT,
		ENUMS(IN_INPUT, 4),
		RESET_INPUT,
		NUM_INPUTS
	};
	enum OutputIds {
		CLK_OUTPUT,
		ENUMS(OUT_OUTPUT, 4),
		NUM_OUTPUTS
	};
	enum LightIds {
		RUN_LIGHT,
		CLK_LIGHT,
		NUM_LIGHTS
	};

	/** [Stored to JSON] */
	int panelTheme = 0;

	bool hasDataLoaded = false;

	AhabSim* sim = nullptr;
	/** [Stored to JSON] */
	bool simRunning = true;
	bool simRunToggleRequest = false;

	// Timing for simulation steps
	float clockPhase = 0.0f;  // Phase accumulator for timing

	// Scheduled note-offs (handled on simulator tick)
	struct ScheduledOff { Usz remaining_ticks; uint8_t channel; uint8_t note; };
	std::vector<ScheduledOff> scheduledOffs;

	/** [Stored to JSON] */
	int midiVirtualPortId = 0;

	// MIDI out support (routes to Rack's MIDI output port)
	/** [Stored to JSON] */
	rack::midi::Output midiOutPort;
	/** [Stored to JSON] */
	bool midiOutEnabled = true;
	/** [Stored to JSON] */
	uint16_t midiCcOffset = 64;

	/** [Stored to JSON] */
	int gridStepCol = 8;
	/** [Stored to JSON] */
	int gridStepRow = 8;

	/** [Stored to JSON] */
	bool overwriteZeroNoteDuration = true;

	dsp::SchmittTrigger simRunTrigger;
	dsp::SchmittTrigger clkButtonTrigger;
	dsp::SchmittTrigger clkInConnectTrigger;
	dsp::SchmittTrigger clkInDisconnectTrigger;
	dsp::SchmittTrigger clkInTrigger;
	dsp::PulseGenerator clkPulseGen;
	dsp::SchmittTrigger resetTrigger;

	dsp::ClockDivider lightDivider;

	AhabModule() {
		panelTheme = pluginSettings.panelThemeDefault;
		config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);
		configParam(BPM_PARAM, 30.0, 300.0, 120.0, "BPM");
		configSwitch(RUN_PARAM, 0.0, 1.0, 0.0, "Start/Stop");
		configSwitch(CLK_PARAM, 0.0, 1.0, 0.0, "Clock");
		for (size_t i = 0; i < 4; ++i) {
			auto p1 = configInput(IN_INPUT + i, string::f("CV %zu", i + 1));
			p1->description = "Use '<' operator to read voltage";
			auto p2 = configOutput(OUT_OUTPUT + i, string::f("CV %zu", i + 1));
			p2->description = "Use '>' operator to send voltage";
		}
		configInput(CLK_INPUT, "Clock");
		configInput(RESET_INPUT, "Reset");
		configOutput(CLK_OUTPUT, "Clock");
	
		sim = new AhabSim();
		sim->setDspTickCallback(std::bind(&AhabModule::processEvents, this, std::placeholders::_1));
		sim->setDspInputReader(std::bind(&AhabModule::readDspInput, this, std::placeholders::_1));
		sim->setDspOutputWriter(std::bind(&AhabModule::writeDspOutput, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3));
		sim->setDspResetCallback(std::bind(&AhabModule::flushNotes, this));
		
		ResetEvent e;
		onReset(e);
	}

	~AhabModule() override {
		delete sim;
	}

	void onSampleRateChange(const SampleRateChangeEvent& e) override {
		lightDivider.setDivision(e.sampleRate / 100.f);
	}

	void onReset(const ResetEvent& e) override {
		clkInConnectTrigger.state = dsp::SchmittTrigger::LOW;
		clkInDisconnectTrigger.state = dsp::SchmittTrigger::LOW;
		midiVirtualPortId = 0;
		midiOutEnabled = true;
		midiOutPort.reset();
		midiCcOffset = 64;
		scheduledOffs.clear();

		overwriteZeroNoteDuration = true;

		// Default grid ruler steps
		gridStepCol = 8;
		gridStepRow = 8;

		clockPhase = 0.0;
		simRunning = true;
		sim->setFieldSize(25, 49);
		sim->reset();

		Module::onReset(e);
	}

	void process(const ProcessArgs &args) override {
		// Ensure any UI-requested publishes are processed on the DSP thread
		if (sim) {
			sim->process();
		}

		if (simRunToggleRequest || simRunTrigger.process(params[RUN_PARAM].getValue())) {
			if (simRunning) {
				simRunning = false;
				clockPhase = 0.0;
				sim->notifyTick();
			}
			else {
				simRunning = true;
				clockPhase = 0.0;
				clkInConnectTrigger.state = dsp::SchmittTrigger::LOW;
				clkInDisconnectTrigger.state = dsp::SchmittTrigger::LOW;
			}
			simRunToggleRequest = false;
		}

		// Manual clock button always steps
		if (clkButtonTrigger.process(params[CLK_PARAM].getValue())) {
			sim->step();
		}

		// External clock input
		bool clkInputConnected = inputs[CLK_INPUT].isConnected();
		if (simRunning && clkInTrigger.process(inputs[CLK_INPUT].getVoltage())) {
			sim->step();
			clockPhase = 0.0;  // Reset phase on external clock
		}

		// Reset input
		if (resetTrigger.process(inputs[RESET_INPUT].getVoltage())) {
			sim->resetTickNumber();
		}

		// Auto-advance simulation based on BPM when running and no external clock
		if (simRunning && !clkInputConnected) {
			// Calculate how much phase to advance per sample
			float bpm = params[BPM_PARAM].getValue();
			float hz = bpm / 60.0f *4.0f;  // Quarter note rate
			float phasePerSample = hz * args.sampleTime;
			
			clockPhase += phasePerSample;
			if (clockPhase >= 1.0) {
				clockPhase -= 1.0;
				sim->step();
			}
		}

		outputs[CLK_OUTPUT].setVoltage(clkPulseGen.process(args.sampleTime) ? 10.0f : 0.0f);

		if (lightDivider.process()) {
			lights[RUN_LIGHT].setBrightness(simRunning ? 1.0f : 0.0f);
			lights[CLK_LIGHT].setBrightnessSmooth(clkPulseGen.isHigh() ? 1.0f : 0.0f, args.sampleTime * lightDivider.division);
		}
	}

	// Output callback from the simulator
	void processEvents(const Oevent_list* olist) {
		// Process scheduled note-offs (decrement by 1 tick)
		for (auto it = scheduledOffs.begin(); it != scheduledOffs.end();) {	
			if (it->remaining_ticks == 0) {
				// MIDI note off
				if (it->note < 255) {
					// Emit Note Off message
					rack::midi::Message m;
					m.setSize(3);
					m.bytes[0] = ((0x8 & 0xf) << 4) | (it->channel & 0xf);
					m.bytes[1] = it->note & 0x7f;
					m.bytes[2] = 0;
					sendMidiMessage(m);
				}
				// CV gate end
				else {
					outputs[OUT_OUTPUT + it->channel].setVoltage(0.f);
				}
				it = scheduledOffs.erase(it);
			} 
			else {
				--(it->remaining_ticks);
				++it;
			}
		}

		if (olist && olist->count > 0) {
			for (Usz i = 0; i < olist->count; ++i) {
				Oevent const* oe = &olist->buffer[i];

				switch ((Oevent_types)oe->any.oevent_type) {
					case Oevent_type_midi_note: {
						rack::midi::Message m;
						m.setSize(3);
						uint8_t ch = oe->midi_note.channel & 0xf;
						m.bytes[0] = ((0x9 & 0xf) << 4) | ch; // Note On
						m.bytes[1] = (oe->midi_note.note + oe->midi_note.octave * 12) & 0x7f;
						m.bytes[2] = oe->midi_note.velocity & 0x7f;
						sendMidiMessage(m);
						if (overwriteZeroNoteDuration && oe->midi_note.duration == 0) {
							// Treat duration 0 as a short note (1 tick)
							scheduledOffs.push_back({0, ch, m.bytes[1]});
						}
						// schedule a note-off if duration > 0 (duration is 7 bits)
						if (oe->midi_note.duration > 0) {
							scheduledOffs.push_back({(Usz)oe->midi_note.duration - 1, ch, m.bytes[1]});
						}
						break;
					}
					case Oevent_type_midi_cc: {
						rack::midi::Message m;
						m.setSize(3);
						uint8_t ch = oe->midi_cc.channel & 0xf;
						m.bytes[0] = ((0xB & 0xf) << 4) | ch; // CC
						m.bytes[1] = (uint8_t)std::min(midiCcOffset + oe->midi_cc.control, 127);
						m.bytes[2] = oe->midi_cc.value & 0x7f;
						sendMidiMessage(m);
						break;
					}
					case Oevent_type_midi_pb: {
						// Pitchbend: lsb/msb into two data bytes (14-bit value)
						rack::midi::Message m;
						m.setSize(3);
						uint8_t ch = oe->midi_pb.channel & 0xf;
						m.bytes[0] = ((0xE & 0xf) << 4) | ch; // PitchBend
						m.bytes[1] = oe->midi_pb.lsb & 0x7f;
						m.bytes[2] = oe->midi_pb.msb & 0x7f;
						sendMidiMessage(m);
						break;
					}
					case Oevent_type_osc_ints:
					case Oevent_type_udp_string:
						// OSC and UDP events are handled inside AhabSim::step()
						break;
				}
			}
		}

		// Trigger pulse on clock output
		clkPulseGen.trigger(0.01f);
	}

	void sendMidiMessage(const midi::Message& m) {
		if (pluginSettings.ahabMidiVirtualEnabled) {
			Ahab::Midi::sendToPort(midiVirtualPortId, m);
		}
		if (midiOutEnabled) {
			midiOutPort.sendMessage(m);
		}
	}

	// Called from the sim's DSP reset callback whenever the simulation is reset
	// (RESET command / resetRequest) or a new field is loaded (REPLACE_FIELD), and
	// indirectly from onReset via sim->reset(). Drops any pending note-offs so they
	// never fire against a newly loaded field, and sends All Notes Off across all
	// channels so no notes stay stuck.
	void flushNotes() {
		scheduledOffs.clear();
		if (pluginSettings.ahabMidiVirtualEnabled) {
			Ahab::Midi::resetMidi(midiVirtualPortId);
		}
		if (midiOutEnabled) {
			for (int ch = 0; ch < 16; ++ch) {
				for (int note = 0; note <= 127; note++) {
					// Note off
					midi::Message m;
					m.setStatus(0x8);
					m.setChannel(ch);
					m.setNote(note);
					m.setValue(0);
					m.setFrame(APP->engine->getFrame());
					midiOutPort.sendMessage(m);
				}
			}
		}
	}

	// Input callback from the simulator, it tied to operator '<'
	float readDspInput(uint8_t port_num) {
		if (port_num > 3) port_num = 3;
		int id = IN_INPUT + (int)port_num;
		if (!inputs[id].isConnected()) return 0.0f;
 		float val = clamp(inputs[id].getVoltage(), 0.0f, 10.0f);
		return val;
	}

	// Output callback from the simulator, it tied to operator '>'
	void writeDspOutput(size_t port_num, float value, int gateTicks = 0) {
		if (port_num > 3) port_num = 3;
		int id = OUT_OUTPUT + (int)port_num;
		outputs[id].setVoltage(value);
		if (gateTicks > 0) {
			scheduledOffs.push_back({(Usz)gateTicks, (uint8_t)port_num, (uint8_t)255});
		}
	}

	json_t* dataToJson() override {
		json_t* rootJ = json_object();
		json_object_set_new(rootJ, "panelTheme", json_integer(panelTheme));

		json_object_set_new(rootJ, "midiVirtualPortId", json_integer(midiVirtualPortId));
		json_object_set_new(rootJ, "midiOutEnabled", json_boolean(midiOutEnabled));
		json_object_set_new(rootJ, "midiOutPort", midiOutPort.toJson());
		json_object_set_new(rootJ, "midiCcOffset", json_integer(midiCcOffset));
		json_object_set_new(rootJ, "sim", sim->toJson());
		json_object_set_new(rootJ, "simRunning", json_boolean(simRunning));
		json_object_set_new(rootJ, "overwriteZeroNoteDuration", json_boolean(overwriteZeroNoteDuration));
		json_object_set_new(rootJ, "gridStepCol", json_integer(gridStepCol));
		json_object_set_new(rootJ, "gridStepRow", json_integer(gridStepRow));
		return rootJ;
	}

	void dataFromJson(json_t* rootJ) override {
		hasDataLoaded = true;
		json_t* panelThemeJ = json_object_get(rootJ, "panelTheme");
		if (panelThemeJ) panelTheme = json_integer_value(panelThemeJ);
		json_t* midiVirtualPortIdJ = json_object_get(rootJ, "midiVirtualPortId");
		if (midiVirtualPortIdJ) midiVirtualPortId = json_integer_value(midiVirtualPortIdJ);
		json_t* midiOutEnabledJ = json_object_get(rootJ, "midiOutEnabled");
		if (midiOutEnabledJ) midiOutEnabled = json_boolean_value(midiOutEnabledJ);
		json_t* midiOutPortJ = json_object_get(rootJ, "midiOutPort");
		if (midiOutPortJ) midiOutPort.fromJson(midiOutPortJ);
		json_t* midiCcOffsetJ = json_object_get(rootJ, "midiCcOffset");
		if (midiCcOffsetJ) midiCcOffset = (uint8_t)json_integer_value(midiCcOffsetJ);
		json_t* simJ = json_object_get(rootJ, "sim");
		if (simJ && json_is_object(simJ)) sim->fromJson(simJ);
		json_t* simRunningJ = json_object_get(rootJ, "simRunning");
		if (simRunningJ) simRunning = json_boolean_value(simRunningJ);
		json_t* overwriteZeroNoteDurationJ = json_object_get(rootJ, "overwriteZeroNoteDuration");
		if (overwriteZeroNoteDurationJ) overwriteZeroNoteDuration = json_boolean_value(overwriteZeroNoteDurationJ);
		json_t* gridStepColJ = json_object_get(rootJ, "gridStepCol");
		if (gridStepColJ) gridStepCol = (int)json_integer_value(gridStepColJ);
		json_t* gridStepRowJ = json_object_get(rootJ, "gridStepRow");
		if (gridStepRowJ) gridStepRow = (int)json_integer_value(gridStepRowJ);
	}
};


struct AhabSimWidget : OpaqueWidget {
	AhabModule* module = nullptr;
	AhabRenderer renderer;

	// Display snapshot using lock-free reads from sim
	Field display_field;
	Mbuf_reusable display_mbuf;
	const Field* display_ready = nullptr;

	// Mouse handling: click to set cursor, drag to create a temporary selection
	math::Vec mousePos;
	math::Vec mouse_selection_start;
	bool mouse_selecting = false;

	FocusMode focusMode;
	ui::Tooltip* tooltip = NULL;

	// Temporary variables for field size changes
	Usz fh, fh_, fw, fw_;

	AhabSimWidget() {
		field_init(&display_field);
		mbuf_reusable_init(&display_mbuf);
	}

	~AhabSimWidget() {
		// Clean up tooltip if it exists (prevents dangling reference)
		if (tooltip) {
			APP->scene->removeChild(tooltip);
			delete tooltip;
			tooltip = NULL;
		}
		mbuf_reusable_deinit(&display_mbuf);
		field_deinit(&display_field);
	}

	// Accessors for status widget
	void getCursorPos(Usz &y, Usz &x) {
		renderer.getCursor(y, x);
	}

	Usz getTickNumber() const {
		return module ? module->sim->getTickNumber() : 0;
	}

	void getFieldSize(Usz &h, Usz &w) const {
		if (module && module->sim) {
			h = module->sim->getFieldHeight();
			w = module->sim->getFieldWidth();
		}
		else {
			h = 0; w = 0;
		}
	}

	void setModule(AhabModule* module) {
		if (module) {
			this->module = module;
			// Set tick callback for display updates
			module->sim->setUiTickCallback(std::bind(&AhabSimWidget::simTick, this, std::placeholders::_1));
			module->sim->setUiResetCallback(std::bind(&AhabSimWidget::reset, this));
			module->sim->notifyTick();
			reset();
		}
		else {
			// only for module browser preview
			auto examples = getOrcaExamples();
			size_t exampleIndex = rack::random::u32() % examples.size();
			std::string example = examples[exampleIndex].second;
			AhabSim::buildFieldFromOrcaText(example, display_field);
			renderer.setCursor(0, 0);
		}
	}

	inline void notifyUiChanged() { 
		if (parent) dynamic_cast<FramebufferWidget*>(parent)->setDirty(true); 
	}

	void reset() {
		module->sim->resetUndo();
		renderer.setCursor(0, 0);
		renderer.setSelection(0, 0, 1, 1, module->sim->getFieldHeight(), module->sim->getFieldWidth());
		rendererGridStepChanged();
		notifyUiChanged();
	}

	// Tick callback from the simulator (called from DSP thread after step())
	void simTick(const Field* f) {
		display_ready = f;
		notifyUiChanged();
	}

	void simClear() {
		Usz fh = module->sim->getFieldHeight();
		Usz fw = module->sim->getFieldWidth();
		module->sim->fillRectRequest(0, 0, fh, fw);
	}

	void simRandomize(float density = 0.3f) {
		if (!module || !module->sim) return;
		
		// Get current selection bounds
		Usz sy, sx, sh, sw;
		renderer.getSelectionRect(sy, sx, sh, sw);
		
		// Use the AhabRandomizer class
		StoermelderPackOne::Ahab::AhabRandomizer randomizer;
		randomizer.randomize(module->sim, sy, sx, sh, sw, density);
		
		notifyUiChanged();
	}

	void simLoad() {
		osdialog_filters* filters = osdialog_filters_parse("Orca Files (*.orca):orca");
		DEFER({ osdialog_filters_free(filters); });
		char* path = osdialog_file(OSDIALOG_OPEN, NULL, NULL, filters);
		if (!path) return;
		DEFER({ free(path); });
		if (!module->sim->loadFromFileRequest(path)) {
			std::string msg = "Failed to load field from file:\n" + std::string(path);
			osdialog_message(OSDIALOG_WARNING, OSDIALOG_OK, msg.c_str());
		}
		renderer.setCursor(0, 0);
		renderer.setSelection(0, 0, 1, 1);
	}

	void simInjectFile() {
		osdialog_filters* filters = osdialog_filters_parse("Orca Files (*.orca):orca");
		DEFER({ osdialog_filters_free(filters); });
		char* path = osdialog_file(OSDIALOG_OPEN, NULL, NULL, filters);
		if (!path) return;
		DEFER({ free(path); });
		Usz sh = 0, sw = 0;
		std::string orca;
		if (!module->sim->convertFileToOrca(std::string(path), orca, sh, sw)) {
			osdialog_message(OSDIALOG_WARNING, OSDIALOG_OK, "Failed to load ORCA file into selection");
			return;
		}
		if (!orca.empty()) {
			glfwSetClipboardString(APP->window->win, orca.c_str());
		}
		// Place selection at cursor and clip to field
		Usz cy, cx; renderer.getCursor(cy, cx);
		Usz fh = module->sim->getFieldHeight();
		Usz fw = module->sim->getFieldWidth();
		if (cy + sh > fh) sh = fh - cy;
		if (cx + sw > fw) sw = fw - cx;
		if (sh == 0 || sw == 0) return;
		renderer.setSelection(cy, cx, sh, sw, fh, fw);
	}

	void simSave() {
		osdialog_filters* filters = osdialog_filters_parse("Orca Files (*.orca):orca");
		DEFER({ osdialog_filters_free(filters); });
		char* path = osdialog_file(OSDIALOG_SAVE, NULL, "patch.orca", filters);
		if (!path) return;
		DEFER({ free(path); });
		if (!module->sim->saveToFile(path)) {
			std::string msg = "Failed to save field to file:\n" + std::string(path);
			osdialog_message(OSDIALOG_WARNING, OSDIALOG_OK, msg.c_str());
		}
	}

	void simSaveSelection() {
		osdialog_filters* filters = osdialog_filters_parse("Orca Files (*.orca):orca");
		DEFER({ osdialog_filters_free(filters); });	
		char* path = osdialog_file(OSDIALOG_SAVE, NULL, "selection.orca", filters);
		if (!path) return;
		DEFER({ free(path); });
		Usz sy, sx, sh, sw;
		renderer.getSelectionRect(sy, sx, sh, sw);
		// Serialize the UI snapshot, not the sim's live buffer (the DSP thread
		// may be writing it from step()).
		std::string content = AhabSim::convertRectToOrca(display_field, sy, sx, sh, sw);
		FILE* file = fopen(path, "w");
		if (!file) {
			std::string message = string::f("Could not write to patch file %s", path);
			osdialog_message(OSDIALOG_WARNING, OSDIALOG_OK, message.c_str());
			return;
		}
		DEFER({ fclose(file); });
		fputs(content.c_str(), file);
	}

	void rendererGridStepChanged() {
		renderer.gridStepRow = module->gridStepRow;
		renderer.gridStepCol = module->gridStepCol;
		notifyUiChanged();
	}

	std::string getOperatorDescription(Glyph g, Mark m) {
		std::string desc;
		if (g == '.') {
			desc = "empty cell";
		} 
		else {
			static const std::map<char, std::string> descriptions = {
				{'A', "add: Outputs sum of inputs"},
				{'B', "subtract: Outputs difference of inputs"},
				{'C', "clock: Outputs modulo of frame"},
				{'D', "delay: Bangs on modulo of frame"},
				{'E', "east: Moves eastward, or bangs"},
				{'F', "if: Bangs if inputs are equal"},
				{'G', "generator: Writes operands with offset"},
				{'H', "halt: Halts southward operand"},
				{'I', "increment: Increments southward operand"},
				{'J', "jumper: Outputs northward operand"},
				{'K', "konkat: Reads multiple variables"},
				{'L', "less: Outputs smallest of inputs"},
				{'M', "multiply: Outputs product of inputs"},
				{'N', "north: Moves Northward, or bangs"},
				{'O', "read: Reads operand with offset"},
				{'P', "push: Writes eastward operand"},
				{'Q', "query: Reads operands with offset"},
				{'R', "random: Outputs random value"},
				{'S', "south: Moves southward, or bangs"},
				{'T', "track: Reads eastward operand"},
				{'U', "uclid: Bangs on Euclidean rhythm"},
				{'V', "variable: Reads and writes variable"},
				{'W', "west: Moves westward, or bangs"},
				{'X', "write: Writes operand with offset"},
				{'Y', "jymper: Outputs westward operand"},
				{'Z', "lerp: Transitions operand to input"},
				{'*', "bang: Bangs neighboring operands"},
				{'#', "comment: Halts a line"},
				{':', "midi: Sends a MIDI note"},
				{'%', "mono: Sends monophonic MIDI note"},
				{'!', "cc: Sends MIDI control change"},
				{'?', "pb: Sends MIDI pitch bench"},
				{';', "udp: Sends UDP message"},
				{'=', "osc: Sends OSC message"},
				{'<', "cv-input: Reads a value from a CV input"},
				{'>', "cv-output: Writes a value to a CV output"},
				{'$', "command: not supported in Ahab"}
			};
			auto it = descriptions.find(g);
			desc = (it != descriptions.end()) ? it->second : std::string(1, g) + ": variable / unknown operator";

			static const std::map<char, std::string> portInfos = {
				{':', "  →1: channel\n  →2: octave\n  →3: note\n  →4: velocity\n  →5: length"},
				{'%', "  →1: channel\n  →2: octave\n  →3: note\n  →4: velocity\n  →5: length"},
				{'!', "  →1: channel\n  →2: knob\n  →3: value"},
				{'?', "  →1: channel\n  →2: lsb\n  →3: msb"},
				{';', "  →1+: string"},
				{'=', "  →1: path\n  →2: len\n  →3+: in"},
				{'A', "  ←1: a\n  →1: b\n  ↓1: output"},
				{'B', "  ←1: a\n  →1: b\n  ↓1: output"},
				{'C', "  ←1: rate\n  →1: mod\n  ↓1: output"},
				{'D', "  ←1: rate\n  →1: mod\n  ↓1: output"},
				{'F', "  ←1: a\n  →1: b\n  ↓1: output"},
				{'G', "  ←1: x\n  ↑1: y\n  →1: len\n  →2+: in\n  (x,y)+: out"},
				{'H', "  ↓1: output"},
				{'I', "  ←1: step\n  →1: mod\n  ↓1: output"},
				{'J', "  ↑1: val\n  ↓1: output"},
				{'K', "  ←1: len\n  →1+: in\n  ↓1+: out"},
				{'L', "  ←1: a\n  →1: b\n  ↓1: output"},
				{'M', "  ←1: a\n  →1: b\n  ↓1: output"},
				{'N', "  (moves north)"},
				{'O', "  ←1: y\n  ↑1: x\n  (y,x): read\n  ↓1: output"},
				{'P', "  ←1: len\n  ↑1: key\n  →1: val\n  ↓(len(key)+1): output"},
				{'Q', "  ←1: len\n  ↑1: y\n  →1: x\n  (y,x)+: in\n  ↓1+: out"},
				{'R', "  ←1: min\n  →1: max\n  ↓1: val"},
				{'S', "  (moves south)"},
				{'T', "  ←1: len\n  ↑1: key\n  →(len(key)+1): val\n  ↓1: output"},
				{'U', "  ←1: write\n  →1: read\n  ↓1: output"},
				{'V', "  ←1: write\n  →1: read\n  ↓1: output"},
				{'W', "  (moves west)"},
				{'X', "  ←1: y\n  ↑1: x\n  →1: val\n  (y,x): output"},
				{'Y', "  ←1: val\n  →1: output"},
				{'Z', "  ←1: rate\n  →1: target\n  ↓1: output"},
				{'*', "  (bangs neighbors)"},
				{'<', "  →1: min\n  →2: port number (1-4 for cv mode / a-d for v/oct mode)\n  →3: max\n  ↓1: output"},
				{'>', "cv mode\n  →1: port number (1-4)\n  →2: min\n  →3: val\n  →4: max\nv/oct mode\n  →1: port number (a-d)\n  →2: octave\n  →3: note\n  →4: gate length"}
			};
			auto pit = portInfos.find(g);
			if (pit != portInfos.end()) {
				desc += "\n" + pit->second;
			}
		}

		/*
		// Add flags
		Mark_flags flags = (Mark_flags)m;
		std::string flagStr;
		if (flags & Mark_flag_lock) flagStr += " locked";
		if (flags & Mark_flag_sleep) flagStr += " sleeping";
		if (flags & Mark_flag_input) flagStr += " input";
		if (flags & Mark_flag_output) flagStr += " output";
		if (flags & Mark_flag_haste_input) flagStr += " haste";
		if (!flagStr.empty()) {
			desc += " (" + flagStr.substr(1) + ")"; // remove leading space
		}
		*/
		return desc;
	}

	void drawLayer(const DrawArgs& args, int layer) override {
		OpaqueWidget::drawLayer(args, layer);
		if (layer != 1) return; // only draw on main layer

		if (APP->event->getSelectedWidget() == this && !focusMode.active) {
			// Draw keyboard focus highlight rectangle
			nvgBeginPath(args.vg);
			nvgRoundedRect(args.vg, -2.f, -2.f, box.size.x + 4.f, box.size.y + 4.f, 2.5f);
			nvgStrokeColor(args.vg, nvgRGBAf(1.f, 0.7f, 0.27f, 0.4f));
			nvgStrokeWidth(args.vg, 3.5f);
			nvgStroke(args.vg);
		}

		// Dim the display but don't darken it completely
		float b = std::max(0.2f, settings::rackBrightness);
		float b_inv = 1.f + std::max(b - settings::rackBrightness, 0.f) * 8.f;
		nvgGlobalAlpha(args.vg, b);

		math::Rect r = box.zeroPos();

		// Outer glow — screen light bleeding onto the panel surface
		float spread = 22.f;
		NVGpaint glow = nvgBoxGradient(args.vg,
			r.pos.x, r.pos.y, r.size.x, r.size.y,
			3.f, spread,
			nvgRGBAf(0.45f, 0.70f, 1.0f, 0.12f * b),
			nvgRGBAf(0.0f, 0.0f, 0.0f, 0.0f));
		nvgBeginPath(args.vg);
		nvgRect(args.vg, r.pos.x - spread, r.pos.y - spread,
			r.size.x + 2.f * spread, r.size.y + 2.f * spread);
		nvgFillPaint(args.vg, glow);
		nvgFill(args.vg);

		// Dark gradient background
		NVGcolor topColor = color::mult(nvgRGB(0x22, 0x22, 0x22), b_inv);
		NVGcolor bottomColor = color::mult(nvgRGB(0x12, 0x12, 0x12), b_inv);
		nvgBeginPath(args.vg);
		nvgRect(args.vg, RECT_ARGS(r));
		nvgFillPaint(args.vg, nvgLinearGradient(args.vg, 0.f, 0.f, 0.f, 25.f, topColor, bottomColor));
		nvgFill(args.vg);

		// Obtain a local snapshot of the simulator display read buffer
		if (display_ready) {
			field_copy((Field *)display_ready, &display_field);
			mbuf_reusable_ensure_size(&display_mbuf, display_field.height, display_field.width);
			// Get mark buffer from simulator
			Usz h, w;
			module->sim->getDisplayBuffer(h, w);
			Mark const* mbuf = module->sim->getMbufBuffer();
			if (mbuf && h == display_field.height && w == display_field.width) {
				memcpy(display_mbuf.buffer, mbuf, h * w * sizeof(Mark));
			}
			display_ready = nullptr;
		}
		// Draw the display field
		renderer.draw(args.vg, &display_field, display_mbuf.buffer, box.size, module ? module->simRunning : false);

		// Corner vignette — subtle darkening toward edges for screen depth
		NVGpaint vignette = nvgRadialGradient(args.vg,
			r.size.x * 0.5f, r.size.y * 0.5f,
			r.size.x * 0.35f, r.size.x * 0.75f,
			nvgRGBAf(0.f, 0.f, 0.f, 0.0f),
			nvgRGBAf(0.f, 0.f, 0.f, 0.45f));
		nvgBeginPath(args.vg);
		nvgRect(args.vg, RECT_ARGS(r));
		nvgFillPaint(args.vg, vignette);
		nvgFill(args.vg);

		// Outer top stroke (shadow)
		nvgBeginPath(args.vg);
		nvgMoveTo(args.vg, 0.f, -0.5f);
		nvgLineTo(args.vg, box.size.x, -0.5f);
		nvgStrokeColor(args.vg, nvgRGBAf(0.f, 0.f, 0.f, 0.24f));
		nvgStrokeWidth(args.vg, 1.f);
		nvgStroke(args.vg);

		// Outer bottom stroke (highlight)
		nvgBeginPath(args.vg);
		nvgMoveTo(args.vg, 0.f, box.size.y + 0.5f);
		nvgLineTo(args.vg, box.size.x, box.size.y + 0.5f);
		nvgStrokeColor(args.vg, nvgRGBAf(1.f, 1.f, 1.f, 0.25f));
		nvgStrokeWidth(args.vg, 1.f);
		nvgStroke(args.vg);

		// Inner top stroke
		nvgBeginPath(args.vg);
		nvgMoveTo(args.vg, 0.f, 2.5f);
		nvgLineTo(args.vg, box.size.x, 2.5f);
		nvgStrokeColor(args.vg, nvgRGBAf(1.f, 1.f, 1.f, 0.20f));
		nvgStrokeWidth(args.vg, 1.f);
		nvgStroke(args.vg);

		// Inner bottom stroke
		nvgBeginPath(args.vg);
		nvgMoveTo(args.vg, 0.f, box.size.y - 2.5f);
		nvgLineTo(args.vg, box.size.x, box.size.y - 2.5f);
		nvgStrokeColor(args.vg, nvgRGBAf(1.f, 1.f, 1.f, 0.20f));
		nvgStrokeWidth(args.vg, 1.f);
		nvgStroke(args.vg);

		// Black border (1 px inner shrink)
		math::Rect rBorder = r.shrink(math::Vec(1.f, 1.f));
		nvgBeginPath(args.vg);
		nvgRect(args.vg, RECT_ARGS(rBorder));
		nvgStrokeColor(args.vg, bottomColor);
		nvgStrokeWidth(args.vg, 2.f);
		nvgStroke(args.vg);
	}

	void onSelectText(const SelectTextEvent& e) override {
		if (!module || !module->sim) return;
		
		std::u32string s32(1, char32_t(e.codepoint));
		std::string s8 = string::UTF32toUTF8(s32);

		if (s8.size() == 1) {
			unsigned char c = (unsigned char)s8[0];

			// Selection toggle via quote (covers unshifted and Shift+quote)
			if (c == '\'') {
				//renderer.toggleSelectionAtCursor();
				e.consume(this);
				// Return for skipping character
				return;
			}

			// Curly braces to adjust horizontal grid step
			if (c == '{') {
				int newv = std::max(1, module->gridStepRow - 1);
				module->gridStepRow = newv;
				rendererGridStepChanged();
			}
			if (c == '}') {
				int newv = std::min(32, module->gridStepRow + 1);
				module->gridStepRow = newv;
				rendererGridStepChanged();
			}
			// Square brackets to adjust vertical grid step
			if (c == '[') {
				int newv = std::max(1, module->gridStepCol - 1);
				module->gridStepCol = newv;
				rendererGridStepChanged();
			}
			if (c == ']') {
				int newv = std::min(32, module->gridStepCol + 1);
				module->gridStepCol = newv;
				rendererGridStepChanged();
			}

			// General printable characters (letters, numbers, punctuation)
			if ((c >= 48 && c <= 90) || (c >= 97 && c <= 122) || c == 33 || (c >= 35 && c <= 38) || c == 42 || c == 43 || c == 46) {
				Glyph g = (Glyph)c;
				Usz cy, cx; renderer.getCursor(cy, cx);
				module->sim->setGlyphRequest(cy, cx, g);
				// If insert mode is active (held by renderer), advance cursor one cell to the right
				if (renderer.getInsertMode()) {
					Usz fh = module->sim->getFieldHeight();
					Usz fw = module->sim->getFieldWidth();
					renderer.moveCursorRelative(0, 1, fh, fw, false);
					renderer.getCursor(cy, cx);
					renderer.setSelection(cy, cx, 1, 1, fh, fw);
				}
				e.consume(this);
				OpaqueWidget::onSelectText(e);
				return;
			}
		}

		// Ignore everything else
		return;
	}

	void onSelectKey(const SelectKeyEvent& e) override {
		if (!module || !module->sim) return;
		const char* k = glfwGetKeyName(e.key, 0);

		// Spacebar in insert mode -> advance cursor one cell to the right
		if (renderer.getInsertMode() && (e.action == GLFW_PRESS || e.action == GLFW_REPEAT) && e.key == GLFW_KEY_SPACE) {
			Usz fh = module->sim->getFieldHeight();
			Usz fw = module->sim->getFieldWidth();
			renderer.moveCursorRelative(0, 1, fh, fw, false);
			Usz cy, cx; renderer.getCursor(cy, cx);
			renderer.setSelection(cy, cx, 1, 1, fh, fw);
			e.consume(this);
			notifyUiChanged();
			return;
		}

		// Spacebar -> Toggle run/stop
		if (e.action == GLFW_PRESS && e.key == GLFW_KEY_SPACE) {
			module->simRunToggleRequest = true;
			e.consume(this);
			module->sim->notifyTick();
			return;
		}

		// Ctrl/Cmd+Backspace -> Clear selection
		if (e.action == GLFW_PRESS && e.key == GLFW_KEY_BACKSPACE) {
			Usz sy, sx, sh, sw; renderer.getSelectionRect(sy, sx, sh, sw);
			module->sim->fillRectRequest(sy, sx, sh, sw);
			e.consume(this);
			return;
		}

		// Ctrl/Cmd+A -> Select all
		if (e.action == GLFW_PRESS && (e.mods & RACK_MOD_MASK) == RACK_MOD_CTRL && k && k[0] == 'a') {
			// Ctrl/Cmd+Y -> Redo
			renderer.setSelection(0, 0, module->sim->getFieldHeight(), module->sim->getFieldWidth());
			e.consume(this);
			return;
		}

		// Ctrl/Cmd+N -> Clear
		if (e.action == GLFW_PRESS && (e.mods & RACK_MOD_MASK) == RACK_MOD_CTRL && k && k[0] == 'n') {
			simClear();
			e.consume(this);
			return;
		}

		// Ctrl/Cmd+O -> Load file
		if (e.action == GLFW_PRESS && (e.mods & RACK_MOD_MASK) == RACK_MOD_CTRL && k && k[0] == 'o') {
			simLoad();
			e.consume(this);
			return;
		}

		// Ctrl/Cmd+B -> Inject file
		if (e.action == GLFW_PRESS && (e.mods & RACK_MOD_MASK) == RACK_MOD_CTRL && k && k[0] == 'b') {
			simInjectFile();
			e.consume(this);
			return;
		}

		// Ctrl/Cmd+S -> Save file
		if (e.action == GLFW_PRESS && (e.mods & RACK_MOD_MASK) == RACK_MOD_CTRL && k && k[0] == 's') {
			simSave();
			e.consume(this);
			return;
		}

		// Ctrl/Cmd+Shift+S -> Save selection to file
		if (e.action == GLFW_PRESS && (e.mods & RACK_MOD_MASK) == (RACK_MOD_CTRL | RACK_MOD_SHIFT) && k && k[0] == 's') {
			simSaveSelection();
			e.consume(this);
			return;
		}

		// Ctrl/Cmd+Z -> Undo
		if (e.action == GLFW_PRESS && (e.mods & RACK_MOD_MASK) == RACK_MOD_CTRL && k && k[0] == 'z') {
			// Request undo on the DSP thread
			module->sim->undoRequest();
			e.consume(this);
			return;
		}

		// Ctrl/Cmd+Y -> Redo
		if (e.action == GLFW_PRESS && (e.mods & RACK_MOD_MASK) == (RACK_MOD_CTRL | RACK_MOD_SHIFT) && k && k[0] == 'z') {
			// Request redo on the DSP thread
			module->sim->redoRequest();
			e.consume(this);
			return;
		}

		// Ctrl/Cmd+I -> Toggle insert mode (cursor moves forward after each input char)
		if (e.action == GLFW_PRESS && (e.mods & RACK_MOD_MASK) == RACK_MOD_CTRL && k && k[0] == 'i') {
			renderer.toggleInsertMode();
			e.consume(this);
			notifyUiChanged();
			return;
		}

		// Ctrl/Cmd+C -> Copy selection to clipboard (ORCA plain text)
		if (e.action == GLFW_PRESS && (e.mods & RACK_MOD_MASK) == RACK_MOD_CTRL && k && k[0] == 'c') {
			Usz sy, sx, sh, sw; renderer.getSelectionRect(sy, sx, sh, sw);
			// Read from the UI snapshot (display_field), not the sim's live buffer.
			std::string s = AhabSim::convertRectToOrca(display_field, sy, sx, sh, sw);
			if (!s.empty()) glfwSetClipboardString(APP->window->win, s.c_str());
			e.consume(this);
			return;
		}

		// Ctrl/Cmd+X -> Cut selection to clipboard (ORCA plain text)
		if (e.action == GLFW_PRESS && (e.mods & RACK_MOD_MASK) == RACK_MOD_CTRL && k && k[0] == 'x') {
			Usz sy, sx, sh, sw; renderer.getSelectionRect(sy, sx, sh, sw);
			// Read from the UI snapshot (display_field), not the sim's live buffer.
			std::string s = AhabSim::convertRectToOrca(display_field, sy, sx, sh, sw);
			if (!s.empty()) glfwSetClipboardString(APP->window->win, s.c_str());
			module->sim->cutRectRequest(sy, sx, sh, sw);
			e.consume(this);
			return;
		}

		// Ctrl/Cmd+V -> Paste selection from clipboard (accept ORCA plain text or JSON)
		if (e.action == GLFW_PRESS && (e.mods & RACK_MOD_MASK) == RACK_MOD_CTRL && k && k[0] == 'v') {
			Usz cy, cx; renderer.getCursor(cy, cx);
			const char* clip = glfwGetClipboardString(APP->window->win);
			if (clip) {
				Usz pasted_h = 0, pasted_w = 0;
				std::string clipStr(clip);
				if (module->sim->loadRectFromOrcaRequest(clipStr, cy, cx, pasted_h, pasted_w)) {
					if (pasted_h > 0 && pasted_w > 0) renderer.setSelection(cy, cx, pasted_h, pasted_w, module->sim->getFieldHeight(), module->sim->getFieldWidth());
				}
			}
			e.consume(this);
			return;
		}

		// Ctrl/Cmd+F -> Step one tick
		if (e.action == GLFW_PRESS && (e.mods & RACK_MOD_MASK) == RACK_MOD_CTRL && e.key == GLFW_KEY_F) {
			module->sim->stepRequest();
			e.consume(this);
		}

		// Ctrl/Cmd+P -> Trigger operator on cursor
		if (e.action == GLFW_PRESS && (e.mods & RACK_MOD_MASK) == RACK_MOD_CTRL && e.key == GLFW_KEY_P) {
			Usz cy, cx; renderer.getCursor(cy, cx);
			// TODO
			// Unclear how to implement this, it looks like orca-c does not have this feature
			e.consume(this);
			return;
		}

		// Shift+Escape -> Exit focus mode
		if (e.action == GLFW_PRESS && e.key == GLFW_KEY_ESCAPE && (e.mods & RACK_MOD_MASK) == RACK_MOD_SHIFT) {
			if (focusMode.active) {
				focusMode.deactivate();
			}
			e.consume(this);
			return;
		}

		// Escape -> Clear selection
		if (e.action == GLFW_PRESS && e.key == GLFW_KEY_ESCAPE) {
			Usz cy, cx; renderer.getCursor(cy, cx);
			renderer.setSelection(cy, cx, 1, 1);
			e.consume(this);
			notifyUiChanged();
			return;
		}

		// Ctrl/Cmd+Shift+7 -> Toggle comment block
		if (e.action == GLFW_PRESS && (e.mods & RACK_MOD_MASK) == (RACK_MOD_CTRL | RACK_MOD_SHIFT) && e.key == GLFW_KEY_7) {
			Usz sy, sx, sh, sw; renderer.getSelectionRect(sy, sx, sh, sw);
			Usz w = display_field.width;
			bool isComment = true;
			for (Usz y = sy; y < sy + sh; ++y) {
				char c1 = display_field.buffer[y * w + sx];
				char c2 = display_field.buffer[y * w + sx + sw - 1];
				if (c1 != '#' || c2 != '#') {
					isComment = false;
					break;
				}
			}
			// If all cells are marked as comment, remove comments; otherwise add comments
			module->sim->pushUndo();
			for (Usz y = sy; y < sy + sh; ++y) {
				module->sim->setGlyphRequest(y, sx, isComment ? '.' : '#', Mark_flag_input, false);
				module->sim->setGlyphRequest(y, sx + sw - 1, isComment ? '.' : '#', Mark_flag_input, false);
			}
			e.consume(this);
			return;
		}

		// Ctrl/Cmd+Shift+R -> Reset tick number to zero
		if (e.action == GLFW_PRESS && (e.mods & RACK_MOD_MASK) == (RACK_MOD_CTRL | RACK_MOD_SHIFT) && e.key == GLFW_KEY_R) {
			module->sim->resetTickNumber();
			e.consume(this);
		}

		// Navigation keys (handle press and repeat so holding arrows moves continuously)
		if ((e.action == GLFW_PRESS || e.action == GLFW_REPEAT) && (e.key == GLFW_KEY_UP || e.key == GLFW_KEY_DOWN || e.key == GLFW_KEY_LEFT || e.key == GLFW_KEY_RIGHT)) {
			// Compute deltas for navigation	
			Isz ddy = (e.mods & RACK_MOD_CTRL) ? module->gridStepRow : 1;
			Isz ddx = (e.mods & RACK_MOD_CTRL) ? module->gridStepCol : 1;
			Isz dy = 0, dx = 0;
			if (e.key == GLFW_KEY_UP) dy = -ddy;
			else if (e.key == GLFW_KEY_DOWN) dy = ddy;
			else if (e.key == GLFW_KEY_LEFT) dx = -ddx;
			else if (e.key == GLFW_KEY_RIGHT) dx = ddx;

			Usz sy, sx, sh, sw; renderer.getSelectionRect(sy, sx, sh, sw);
			Isz dest_y = (Isz)sy + dy;
			Isz dest_x = (Isz)sx + dx;
			
			if (e.mods & RACK_MOD_ALT) {
				// Schedule move; update renderer optimistically to reflect the intended move.
				module->sim->moveRectRequest(sy, sx, sh, sw, dest_y, dest_x);
			}

			Usz fh = module->sim->getFieldHeight();
			Usz fw = module->sim->getFieldWidth();
			if (e.mods & RACK_MOD_SHIFT) {
				// Extend selection by moving the cursor; renderer anchor will handle left-side extension
				// Move cursor while requesting selection extension so anchor/cursor logic takes effect
				renderer.moveCursorRelative((int)dy, (int)dx, fh, fw, true);
				renderer.updateSelectionToCursor();
				e.consume(this);
				notifyUiChanged();
			} 
			else {
				// Move selection normally
				// Delegate clamping to renderer; provide a reasonable start (clip negatives to 0)
				Usz start_y = dest_y < 0 ? 0 : (Usz)dest_y;
				Usz start_x = dest_x < 0 ? 0 : (Usz)dest_x;
				// Move cursor by requested delta; moveCursorRelative will itself clamp to field bounds.
				renderer.moveCursorRelative((int)dy, (int)dx, fh, fw, false);
				renderer.setSelection(start_y, start_x, sh, sw, fh, fw);
				notifyUiChanged();
			}
			e.consume(this);
		}
	}

	void onHover(const HoverEvent& e) override {
		mousePos = e.pos;
		OpaqueWidget::onHover(e);
	}

	void onButton(const event::Button& e) override {
		OpaqueWidget::onButton(e);
		// Right button for context menu
		if (e.action == GLFW_PRESS && e.button == GLFW_MOUSE_BUTTON_RIGHT) {
			createContextMenu();
			e.consume(this);
			return;
		}
		// Left click: set cursor (and possibly begin selection only when Shift is held)
		if (e.button == GLFW_MOUSE_BUTTON_LEFT) {
			if (e.action == GLFW_PRESS) {
				Usz cy, cx;
				if (renderer.pixelToCell(e.pos, box.size, &display_field, cy, cx)) {
					renderer.setCursor(cy, cx);
					mouse_selecting = true;
					mouse_selection_start = e.pos;
					// If a selection already exists, extend it immediately to the cursor
					renderer.updateSelectionToCursor();
					notifyUiChanged();
					e.consume(this);
					return;
				}
			} 
			else if (e.action == GLFW_RELEASE) {
				if (mouse_selecting) {
					// End of drag selection: keep selection active after mouse release
					mouse_selecting = false;
					e.consume(this);
					return;
				}
			}
		}
	}

	void onDragStart(const DragStartEvent& e) override {
		if (e.button == GLFW_MOUSE_BUTTON_LEFT) {
			// Don't implicitly start selection here; selection must be initiated on Button press with Shift held.
			e.consume(this);
		}
		OpaqueWidget::onDragStart(e);
	}

	void onDragHover(const DragHoverEvent& e) override {
		if (mouse_selecting && e.button == GLFW_MOUSE_BUTTON_LEFT) {
			// Compute selection rect from start -> current
			Usz y0, x0, y1, x1;
			if (renderer.pixelToCell(mouse_selection_start, box.size, &display_field, y0, x0) && renderer.pixelToCell(e.pos, box.size, &display_field, y1, x1)) {
				Usz sy = std::min(y0, y1);
				Usz sx = std::min(x0, x1);
				Usz sh = std::max(y0, y1) - sy + 1;
				Usz sw = std::max(x0, x1) - sx + 1;
				renderer.setCursor(y1, x1);
				renderer.setSelection(sy, sx, sh, sw, display_field.height, display_field.width);
				notifyUiChanged();
			}
			e.consume(this);
		}
		OpaqueWidget::onDragHover(e);
	}

	void onDragEnd(const DragEndEvent& e) override {
		if (e.button == GLFW_MOUSE_BUTTON_LEFT && mouse_selecting) {
			// End drag: keep selection active after drag
			mouse_selecting = false;
			notifyUiChanged();
			e.consume(this);
		}
		OpaqueWidget::onDragEnd(e);
	}

	void onEnter(const event::Enter& e) override {
		struct CellTooltip : ui::Tooltip {
			AhabSimWidget* widget = nullptr;
			void step() override {
				if (widget->display_mbuf.buffer) {
					visible = false;
					Usz y, x;
					if (widget->renderer.pixelToCell(widget->mousePos, widget->box.size, &widget->display_field, y, x)) {
						size_t idx = y * widget->display_field.width + x;
						Mark m = widget->display_mbuf.buffer[idx];
						Mark_flags flags = (Mark_flags)m;
						Glyph g = widget->display_field.buffer[idx];
						visible = 
							((g == ':' || g == ';' || g == '%' || g == '?' || g == '!' || g == '=' || g == '<' || g == '>') && (!(flags & (Mark_flag_lock)) || (flags & Mark_flag_output))) || 
							((g != '.') && !(flags & (Mark_flag_lock | Mark_flag_sleep)));
						if (visible) text = widget->getOperatorDescription(g, m);
					}
				}
				Tooltip::step();
			}
		};

		if (settings::tooltips && !tooltip) {
			CellTooltip* cellTooltip = new CellTooltip;
			cellTooltip->widget = this;
			APP->scene->addChild(cellTooltip);
			tooltip = cellTooltip;
		}
	}

	void onLeave(const event::Leave& e) override {
		if (tooltip) {
			APP->scene->removeChild(tooltip);
			delete tooltip;
			tooltip = NULL;
		}
	}

	void createContextMenu() {
		ui::Menu* menu = createMenu();
		appendContextMenu(menu, true);
	}

	void appendContextMenu(Menu* menu, bool isWidgetMenu) {
		if (!module || !module->sim) return;
		
		menu->addChild(createMenuItem("Undo", string::f("[%i] " RACK_MOD_CTRL_NAME "+Z", module->sim->getUndoCount()), [this]() {
			module->sim->undoRequest();
			APP->event->setSelectedWidget(this);
		}, !module->sim->canUndo()));
		if (module->sim->canRedo()) {
			menu->addChild(createMenuItem("Redo", RACK_MOD_CTRL_NAME "+" RACK_MOD_SHIFT_NAME "+Z", [this]() {
				module->sim->redoRequest();
				APP->event->setSelectedWidget(this);
			}));
		}
		menu->addChild(createMenuItem("Zoom in", "", [this]() {
			APP->scene->rackScroll->zoomToBound(Rect(parent->parent->box.pos + parent->box.pos, box.size).shrink(Vec(24.f, 24.f)));
			APP->event->setSelectedWidget(this);
		}, focusMode.active));
		menu->addChild(createMenuItem(focusMode.active ? "Exit focus mode" : "Focus mode", RACK_MOD_SHIFT_NAME "+Esc", 
			[this]() {
				if (focusMode.active) {
					focusMode.deactivate();
				} 
				else {
					APP->scene->rackScroll->zoomToBound(Rect(parent->parent->box.pos + parent->box.pos, box.size).grow(Vec(4.f, 4.f)));
					focusMode.activate(this);
				}
				APP->event->setSelectedWidget(this);
			}
		));

		menu->addChild(createSubmenuItem("Terminal", "", [this](ui::Menu* menu) {
			fh = fh_ = module->sim->getFieldHeight();
			fw = fw_ = module->sim->getFieldWidth();
			menu->addChild(Rack::createSlider(
				[=]() { return (float)fh; },
				[=](float v) {
					float d = v - float(fh);
					if (d != 0.f) {
						Usz h = std::max((Usz)1, (Usz)(module->sim->getFieldHeight() + (d > 0.f ? 1 : -1)));
						module->sim->setFieldSizeRequest(h, fw, fh == fh_);
						renderer.moveCursorRelative(0, 0, h, fw);
						fh = (float)h;
						fh_ = 0;
					}
				},
				8.f, 49.f, (float)fh, "Rows", "", 1.f, 200.f
			));
			menu->addChild(Rack::createSlider(
				[=]() { return (float)fw; },
				[=](float v) {
					float d = v - float(fw);
					if (d != 0.f) {
						Usz w = std::max((Usz)1, (Usz)(module->sim->getFieldWidth() + (d > 0.f ? 1 : -1)));
						module->sim->setFieldSizeRequest(fh, w, fw == fw_);
						renderer.moveCursorRelative(0, 0, fh, w);
						fw = (float)w;
						fw_ = 0;
					}
				},
				12.f, 97.f, (float)fw, "Cols", "", 1.f, 200.f
			));
	
			// Grid step settings
			menu->addChild(Rack::createSteppedSlider<int>(
				[this]() { return module->gridStepCol; },
				[this](int v) {
					module->gridStepCol = std::max(1, v);
					rendererGridStepChanged();
				},
				1.f, 32.f, 8.f, "Grid step cols", " cells"
			));
			menu->addChild(Rack::createSteppedSlider<int>(
				[this]() { return module->gridStepRow; },
				[this](int v) {
					module->gridStepRow = std::max(1, v);
					rendererGridStepChanged();
				},
				1.f, 32.f, 8.f, "Grid step rows", " cells"
			));
		}));

		menu->addChild(new MenuSeparator());
		menu->addChild(createMenuItem("Clear", isWidgetMenu ? RACK_MOD_CTRL_NAME "+N" : "", [this]() {
			simClear();
			APP->event->setSelectedWidget(this);
		}));
		menu->addChild(createMenuItem("Load from file", isWidgetMenu ? RACK_MOD_CTRL_NAME "+O" : "", [this]() {
			simLoad();
			APP->event->setSelectedWidget(this);
		}));
		menu->addChild(createMenuItem("Inject file", isWidgetMenu ? RACK_MOD_CTRL_NAME "+B" : "", [this]() {
			simInjectFile();
			APP->event->setSelectedWidget(this);
		}));
		menu->addChild(createMenuItem("Save to file", isWidgetMenu ? RACK_MOD_CTRL_NAME "+S" : "", [this]() {
			simSave();
			APP->event->setSelectedWidget(this);
		}));
		menu->addChild(createMenuItem("Save selection to file", isWidgetMenu ? RACK_MOD_CTRL_NAME "+" RACK_MOD_SHIFT_NAME "+S" : "", [this]() {
			simSaveSelection();
			APP->event->setSelectedWidget(this);
		}));

		auto loadString = [this](const std::string& content) {
			Usz tmp_h = 0, tmp_w = 0;
			if (!module->sim->loadRectFromOrcaRequest(content, 0, 0, tmp_h, tmp_w, true)) {
				osdialog_message(OSDIALOG_WARNING, OSDIALOG_OK, "Failed to load example");
			} 
			else {
				APP->event->setSelectedWidget(this);
			}
		};

		// Examples embedded at build time (from dep/orca-c/examples)
		menu->addChild(createSubmenuItem("Examples", "", [=](ui::Menu* m) {
			auto examples = getOrcaExamples();
			std::map<std::string, std::vector<std::pair<std::string,std::string>>> groups;
			for (auto &e : examples) {
				std::string rel = e.first;
				size_t pos = rel.find('/');
				std::string cat = (pos == std::string::npos) ? std::string() : rel.substr(0, pos);
				std::string name = (pos == std::string::npos) ? rel : rel.substr(pos + 1);
				groups[cat].push_back({name, e.second});
			}
			for (auto g : groups) {
				if (g.first.empty()) {
					for (auto &p : g.second) {
						std::string content = p.second;
						m->addChild(createMenuItem(p.first, "", [=]() { loadString(content); }));
					}
				} 
				else {
					m->addChild(createSubmenuItem(g.first.c_str(), "", [=](ui::Menu* sub) {
						for (auto &p : g.second) {
							std::string content = p.second;
							sub->addChild(createMenuItem(p.first, "", [=]() { loadString(content); }));
						}
					}));
				}
			}
		}));

		menu->addChild(createSubmenuItem("Randomize selection", "", [this](ui::Menu* menu) {
			menu->addChild(createMenuItem("Sparse (10%)", "", [this]() { 
				simRandomize(0.1f); 
				APP->event->setSelectedWidget(this);
			}));
			menu->addChild(createMenuItem("Medium (30%)", "", [this]() { 
				simRandomize(0.3f); 
				APP->event->setSelectedWidget(this);
			}));
			menu->addChild(createMenuItem("Very dense (50%)", "", [this]() { 
				simRandomize(0.5f); 
				APP->event->setSelectedWidget(this);
			}));
		}));

		menu->addChild(new MenuSeparator());
		menu->addChild(createSubmenuItem("MIDI", "", [this](ui::Menu* menu) {
			menu->addChild(createBoolPtrMenuItem("Driver enabled", "", &module->midiOutEnabled));
			menu->addChild(createSubmenuItem("Driver", "", [this](ui::Menu* menu) {
				rack::app::appendMidiMenu(menu, &module->midiOutPort);
			}));

			menu->addChild(new MenuSeparator());
			menu->addChild(createBoolMenuItem("Virtual driver enabled", "",
				[=]() { return pluginSettings.ahabMidiVirtualEnabled; },
				[=](bool v) { pluginSettings.ahabMidiVirtualEnabled = v; if (v) Ahab::Midi::init(); pluginSettings.saveToJson(); }
			));
			menu->addChild(createSubmenuItem("Virtual", "", [this](ui::Menu* menu) {
				int ports = Ahab::Midi::numPorts();
				for (int i = 0; i < ports; ++i) {
					menu->addChild(createCheckMenuItem(string::f("Port %i", i + 1), "", [this, i]() { return module->midiVirtualPortId == i; }, [this, i]() { module->midiVirtualPortId = i; }));
				}
			}));

			menu->addChild(new MenuSeparator());
			menu->addChild(createBoolMenuItem("CC range 0-35", "",
				[=]() { return module->midiCcOffset == 0; }, [=](bool v) { module->midiCcOffset = 0; }
			));
			menu->addChild(createBoolMenuItem("CC range 32-67", "",
				[=]() { return module->midiCcOffset == 32; }, [=](bool v) { module->midiCcOffset = 32; }
			));
			menu->addChild(createBoolMenuItem("CC range 64-99", "",
				[=]() { return module->midiCcOffset == 64; }, [=](bool v) { module->midiCcOffset = 64; }
			));
			menu->addChild(createBoolMenuItem("CC range 96-127", "",
				[=]() { return module->midiCcOffset == 96; }, [=](bool v) { module->midiCcOffset = 96; }
			));

			menu->addChild(new MenuSeparator());
			menu->addChild(createBoolPtrMenuItem("Always send Note Off", "", &module->overwriteZeroNoteDuration));
		}));

		menu->addChild(createSubmenuItem("UDP", "", [this](ui::Menu* m) {
			auto* addrField = Rack::createTextField(module->sim->getUdpAddress(), "Address");
			m->addChild(addrField);
			auto* portField = Rack::createTextField(module->sim->getUdpPort(), "Port");
			m->addChild(portField);
			m->addChild(createMenuItem("Apply", "", [this, addrField, portField]() { 
				module->sim->setUdpDestination(addrField->text, portField->text); 
			}));
		}));

		menu->addChild(createSubmenuItem("OSC", "", [this](ui::Menu* m) {
			auto* addrField = Rack::createTextField(module->sim->getOscAddress(), "Address");
			m->addChild(addrField);
			auto* portField = Rack::createTextField(module->sim->getOscPort(), "Port");
			m->addChild(portField);
			m->addChild(createMenuItem("Apply", "", [this, addrField, portField]() { 
				module->sim->setOscDestination(addrField->text, portField->text); 
			}));
		}));

		menu->addChild(new MenuSeparator());
		menu->addChild(createSubmenuItem("Help", "", [](ui::Menu* menu) {
			menu->addChild(createMenuItem("Learn ORCΛ", "", []() {
				system::openBrowser("https://metasyn.srht.site/learn-orca/");
			}));
			menu->addChild(createMenuItem("ORCΛ cheat sheet", "", []() {
				system::openBrowser("https://100r.co/media/content/projects/zine_orca.png");
			}));
			menu->addChild(createMenuItem("ORCΛ online manual", "", []() {
				system::openBrowser("https://100r.co/site/orca.html");
			}));
			menu->addChild(createMenuItem("ORCΛ GitHub repository", "", []() {
				system::openBrowser("https://github.com/hundredrabbits/Orca");
			}));
		}));
	}
};

struct AhabSimFramebuffer : FramebufferWidget {
	AhabSimWidget* setModule(AhabModule* m) {
		AhabSimWidget* simWidget = new AhabSimWidget();
		simWidget->box.pos = math::Vec(0,0);
		simWidget->box.size = box.size;
		simWidget->setModule(m);
		addChild(simWidget);
		return simWidget;
	}
};


struct AhabStatusWidget : LedDisplay {
	AhabSimWidget* simWidget = nullptr;
	void draw(const DrawArgs &args) override {
		LedDisplay::draw(args);
		if (!simWidget) return;

		Usz cy, cx; simWidget->getCursorPos(cy, cx);
		Usz fh, fw; simWidget->getFieldSize(fh, fw);
		Usz tick = simWidget->getTickNumber();

		float fontSize = 10.2f;
		nvgFontSize(args.vg, fontSize);
		auto fontFace_ = APP->window->loadFont(asset::plugin(pluginInstance, "res/fonts/VictorMono-SemiBold.ttf"));
		nvgFontFaceId(args.vg, fontFace_->handle);
		nvgTextAlign(args.vg, NVG_ALIGN_LEFT | NVG_ALIGN_TOP);
		nvgFillColor(args.vg, nvgRGBAf(1, 1, 1, 0.8f));

		std::string line1 = string::f("%02i,%02i", (int)cx, (int)cy);
		std::string line2 = string::f("%05i", (int)tick);
		std::string line3 = string::f("%02ix%02i", (int)fw, (int)fh);

		float x = 4.1f;
		float y = 6.0f;
		nvgText(args.vg, x, y, line1.c_str(), NULL);
		nvgText(args.vg, x, y + fontSize + 4.0f, line2.c_str(), NULL);
		nvgText(args.vg, x, y + 2 * (fontSize + 4.0f), line3.c_str(), NULL);
	}
};

struct AhabWidget : ThemedModuleWidget<AhabModule> {
	AhabSimWidget* simWidget = nullptr;
	AhabStatusWidget* statusWidget = nullptr;

	AhabWidget(AhabModule *module) 
		: ThemedModuleWidget<AhabModule>(module, "Ahab") {
		setModule(module);

		addChild(createWidget<StoermelderBlackScrew>(Vec(RACK_GRID_WIDTH, 0)));
		addChild(createWidget<StoermelderBlackScrew>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, 0)));
		addChild(createWidget<StoermelderBlackScrew>(Vec(RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));
		addChild(createWidget<StoermelderBlackScrew>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));

		addChild(createInputCentered<StoermelderPort>(math::Vec(22.5f, 67.9f), module, AhabModule::CLK_INPUT));
		addChild(createInputCentered<StoermelderPort>(math::Vec(22.5f, 112.9f), module, AhabModule::RESET_INPUT));
		addChild(createInputCentered<StoermelderPort>(math::Vec(22.5f, 228.3f), module, AhabModule::IN_INPUT + 0));
		addChild(createInputCentered<StoermelderPort>(math::Vec(22.5f, 261.0f), module, AhabModule::IN_INPUT + 1));
		addChild(createInputCentered<StoermelderPort>(math::Vec(22.5f, 293.7f), module, AhabModule::IN_INPUT + 2));
		addChild(createInputCentered<StoermelderPort>(math::Vec(22.5f, 326.2f), module, AhabModule::IN_INPUT + 3));

		addChild(createOutputCentered<StoermelderPort>(math::Vec(577.5f, 67.9f), module, AhabModule::CLK_OUTPUT));
		addChild(createOutputCentered<StoermelderPort>(math::Vec(577.5f, 228.3f), module, AhabModule::OUT_OUTPUT + 0));
		addChild(createOutputCentered<StoermelderPort>(math::Vec(577.5f, 261.0f), module, AhabModule::OUT_OUTPUT + 1));
		addChild(createOutputCentered<StoermelderPort>(math::Vec(577.5f, 293.7f), module, AhabModule::OUT_OUTPUT + 2));
		addChild(createOutputCentered<StoermelderPort>(math::Vec(577.5f, 326.2f), module, AhabModule::OUT_OUTPUT + 3));

		addChild(createParamCentered<VCVButton>(math::Vec(22.5f, 151.3f), module, AhabModule::RUN_PARAM));
		addChild(createLightCentered<MediumSimpleLight<WhiteLight>>(Vec(22.5f, 151.3f), module, AhabModule::RUN_LIGHT));
		addChild(createParamCentered<VCVButton>(math::Vec(22.5f, 182.3f), module, AhabModule::CLK_PARAM));
		addChild(createLightCentered<MediumSimpleLight<WhiteLight>>(Vec(22.5f, 182.3f), module, AhabModule::CLK_LIGHT));

		addChild(createParamCentered<StoermelderSmallKnob>(math::Vec(577.5f, 111.5f), module, AhabModule::BPM_PARAM));
		
		AhabSimFramebuffer* fb = new AhabSimFramebuffer();
		fb->box.pos = math::Vec(45.f, 10.2f);
		fb->box.size = math::Vec(510.f, 354.f);
		simWidget = fb->setModule(module);
		addChild(fb);

		// Status widget to the right of the sim and above clock input
		statusWidget = new AhabStatusWidget();
		statusWidget->simWidget = simWidget;
		statusWidget->box.pos = math::Vec(562.f, 136.5f);
		statusWidget->box.size = math::Vec(31.1, 50.8f);
		addChild(statusWidget);

		if (module && !module->hasDataLoaded && pluginSettings.ahabInfo) {
			Widget* info = infoOverlayCreate(&pluginSettings.ahabInfo,
				"stoermelder AHAB",
				"AHAB is a livecoding sequencer based on ORCA, an esoteric programming "
				"language designed to quickly create procedural sequencers, in which every "
				"letter of the alphabet is an operation, where lowercase letters operate on bang, "
				"uppercase letters operate each frame.\n"
				"Examples and helpful links can be found in the context menu.",
				"https://github.com/hundredrabbits/Orca");
			addChild(info);
		}
    }

	void onButton(const event::Button& e) override {
		ModuleWidget::onButton(e);
		if (e.action == GLFW_RELEASE && e.button == GLFW_MOUSE_BUTTON_LEFT && e.isConsumed() && e.getTarget() == this) {
			APP->event->setSelectedWidget(simWidget);
		}
	}

	void appendContextMenu(Menu* menu) override {
		ThemedModuleWidget<AhabModule>::appendContextMenu(menu);
		menu->addChild(new MenuSeparator());
		simWidget->appendContextMenu(menu, false);
	}
};

} // namespace Ahab
} // namespace StoermelderPackOne

Model* modelAhab = createModel<StoermelderPackOne::Ahab::AhabModule, StoermelderPackOne::Ahab::AhabWidget>("Ahab");