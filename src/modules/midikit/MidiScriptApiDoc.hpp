#pragma once
#include "../../ui/ScriptEditorText.hpp"

// What the script editor's context menu offers (templates and API reference).
// The script API as the editor's context menu lists it (docs/midikit/SCRIPTING.md,
// Part 3). Both engines expose the same objects and function names, so one table
// serves QuickJs and Lua. Hooks (rack.onLoad, midi.onMessage, ...) are not listed:
// they are assigned, not called. Parameter descriptions end up in a one-line
// comment where ';' separates parameters, so they contain none.

namespace StoermelderPackOne {
namespace MidiScript {

// The file headers of docs/midikit/SCRIPTING.md ("Required file header"). Only the
// leading comment block is scanned for tags, so each goes at the top of the script.
inline std::vector<ui::editor::scripttext::ScriptTemplate> scriptTemplates() {
	using ui::editor::scripttext::ScriptTemplate;
	std::vector<ScriptTemplate> t;
	t.push_back(ScriptTemplate("QuickJs header",
		"/**\n"
		" * @target stoermelder MIDI-KIT\n"
		" * @engine QuickJs@v1\n"
		" * @author yourname\n"
		" * @description One-line summary shown in the module log on load\n"
		" */\n\n"));
	t.push_back(ScriptTemplate("Lua header",
		"--[[\n"
		"@target stoermelder MIDI-KIT\n"
		"@engine minilua@v1\n"
		"@author yourname\n"
		"@description One-line summary shown in the module log on load\n"
		"--]]\n\n"));
	return t;
}

inline std::vector<ui::editor::scripttext::ApiGroup> apiReference() {
	using ui::editor::scripttext::ApiFunction;
	using ui::editor::scripttext::ApiGroup;
	using ui::editor::scripttext::ApiParam;

	const char* MSG = "message handle";
	const char* PORT = "MIDI input port, 1-based";
	const char* CH = "channel 1-16";
	const char* CHANNEL_OPT = "channel 1-16, default all";
	const char* TRIG_CH = "channel, 1-based, default 1";
	const char* NRPN = "handle from midi.createNRPN()";

	std::vector<ApiGroup> api;

	api.push_back(ApiGroup("rack", {
		ApiFunction("log", "", {{"value", "any value, further arguments are appended without separator"}}),
		ApiFunction("overlay", "", {{"s1", "first line of the panel overlay"}, {"s2", "second line", true}, {"s3", "third line", true}}),
		ApiFunction("getEventFrame", "", {}),
		ApiFunction("msToFrames", "", {{"ms", "milliseconds"}}),
		ApiFunction("framesToMs", "", {{"frames", "number of frames"}}),
		ApiFunction("random", "", {}),
		ApiFunction("registerContextMenu", "", {{"options", "{type: \"boolean\" or \"options\", label, options, onGetValue, onChange}"}}),
		ApiFunction("unregisterContextMenu", "", {{"label", "label of the item to remove"}}),
		ApiFunction("getConfig", "", {{"key", "name of the persisted value"}, {"default", "returned if key is unset", true}}),
		ApiFunction("setConfig", "", {{"key", "name of the value to persist"}, {"value", "boolean, number, string, array or object, undefined/nil removes the key"}}),
		ApiFunction("sendBroadcast", "", {{"value", "value for the other MIDI-KIT modules, at most 4 KB as JSON"}, {"topic", "string of at most 64 bytes", true}}),
	}));

	api.push_back(ApiGroup("number", {
		ApiFunction("crossfade", "", {{"a", "value at pos 0"}, {"b", "value at pos 1"}, {"pos", "crossfade position 0-1"}}),
		ApiFunction("rescale", "", {{"x", "input value"}, {"xMin", "input range start"}, {"xMax", "input range end"}, {"yMin", "output range start"}, {"yMax", "output range end"}, {"curve", "response curve, 0 is linear", true}}),
		ApiFunction("toString", "", {{"x", "number to format"}}),
	}));

	api.push_back(ApiGroup("input", {
		ApiFunction("enable", "", {{"i", "CV input, 1-based"}}),
		ApiFunction("getVoltage", "", {{"i", "CV input, 1-based"}, {"ch", "polyphonic channel, default 1", true}}),
		ApiFunction("isHigh", "", {{"i", "CV input, 1-based"}, {"ch", "polyphonic channel, default 1", true}}),
		ApiFunction("isLow", "", {{"i", "CV input, 1-based"}, {"ch", "polyphonic channel, default 1", true}}),
	}));

	api.push_back(ApiGroup("trig", {
		ApiFunction("enableIn", "", {{"trigPort", "trigger input 1-2"}, {"ch", TRIG_CH, true}}),
		ApiFunction("getTicks", "", {{"i", "trigger input 1-2"}, {"ch", TRIG_CH, true}}),
		ApiFunction("isHigh", "", {{"i", "trigger input 1-2"}, {"ch", TRIG_CH, true}}),
		ApiFunction("isLow", "", {{"i", "trigger input 1-2"}, {"ch", TRIG_CH, true}}),
		ApiFunction("setHigh", "", {{"i", "trigger output 1-2"}, {"ch", TRIG_CH, true}}),
		ApiFunction("setLow", "", {{"i", "trigger output 1-2"}, {"ch", TRIG_CH, true}}),
		ApiFunction("setTrigger", "", {{"i", "trigger output 1-2"}, {"ch", TRIG_CH, true}}),
		ApiFunction("setGate", "", {{"i", "trigger output 1-2"}, {"ch", TRIG_CH, true}, {"durationMs", "gate length in milliseconds"}}),
		ApiFunction("sendTipsy", "", {{"data", "string of at most 256 bytes"}, {"mimeType", "default \"text/plain\"", true}}),
		ApiFunction("enableTipsyIn", "", {{"enabled", "false releases the input, default true", true}}),
	}));

	api.push_back(ApiGroup("param", {
		ApiFunction("enable", "", {{"i", "panel knob, 1-based"}}),
		ApiFunction("getValue", "", {{"i", "panel knob, 1-based"}, {"fallback", "returned if the knob does not exist", true}}),
	}));

	const char* kConstructors = "Constructors";
	const char* kGetters = "Getters";
	const char* kPredicates = "Type predicates";
	const char* kSetters = "Setters";
	const char* kExtended = "Extended input";
	api.push_back(ApiGroup("midi", {
		ApiFunction("create", kConstructors, {}),
		ApiFunction("clone", kConstructors, {{"msg", "message handle to copy"}}),
		ApiFunction("createNRPN", kConstructors, {}),
		ApiFunction("createRPN", kConstructors, {}),
		ApiFunction("createCc14bit", kConstructors, {}),

		ApiFunction("getChannel", kGetters, {{"msg", MSG}}),
		ApiFunction("getChanPressure", kGetters, {{"msg", MSG}}),
		ApiFunction("getControl", kGetters, {{"msg", MSG}}),
		ApiFunction("getNote", kGetters, {{"msg", MSG}}),
		ApiFunction("getValue", kGetters, {{"msg", MSG}}),
		ApiFunction("getLength", kGetters, {{"msg", MSG}}),
		ApiFunction("getPitchWheel", kGetters, {{"msg", MSG}}),
		ApiFunction("getProgramChange", kGetters, {{"msg", MSG}}),
		ApiFunction("getSysEx", kGetters, {{"msg", MSG}}),
		ApiFunction("getSysExLength", kGetters, {{"msg", MSG}}),
		ApiFunction("getRaw", kGetters, {{"msg", MSG}}),

		ApiFunction("isCc", kPredicates, {{"msg", MSG}}),
		ApiFunction("isNoteOn", kPredicates, {{"msg", MSG}}),
		ApiFunction("isNoteOff", kPredicates, {{"msg", MSG}}),
		ApiFunction("isKeyPressure", kPredicates, {{"msg", MSG}}),
		ApiFunction("isChanPressure", kPredicates, {{"msg", MSG}}),
		ApiFunction("isProgramChange", kPredicates, {{"msg", MSG}}),
		ApiFunction("isPitchWheel", kPredicates, {{"msg", MSG}}),
		ApiFunction("isSysEx", kPredicates, {{"msg", MSG}}),
		ApiFunction("isClock", kPredicates, {{"msg", MSG}}),
		ApiFunction("isStart", kPredicates, {{"msg", MSG}}),
		ApiFunction("isContinue", kPredicates, {{"msg", MSG}}),
		ApiFunction("isStop", kPredicates, {{"msg", MSG}}),
		ApiFunction("isNrpn", kPredicates, {{"msg", MSG}}),
		ApiFunction("isRpn", kPredicates, {{"msg", MSG}}),
		ApiFunction("isCc14bit", kPredicates, {{"msg", MSG}}),

		ApiFunction("setCc", kSetters, {{"msg", MSG}, {"ch", CH}, {"cc", "controller number 0-127"}, {"value", "value 0-127"}}),
		ApiFunction("setCc14bit", kSetters, {{"cc14", "handle from midi.createCc14bit()"}, {"ch", CH}, {"cc", "MSB controller 0-31, the LSB is cc + 32"}, {"value", "float 0-127.99, the fraction is the LSB"}}),
		ApiFunction("setChannel", kSetters, {{"msg", MSG}, {"ch", CH}}),
		ApiFunction("setChanPressure", kSetters, {{"msg", MSG}, {"ch", CH}, {"value", "pressure 0-127"}}),
		ApiFunction("setKeyPressure", kSetters, {{"msg", MSG}, {"ch", CH}, {"note", "note number 0-127"}, {"vel", "pressure 0-127"}}),
		ApiFunction("setNote", kSetters, {{"msg", MSG}, {"note", "note number 0-127"}}),
		ApiFunction("setNoteOn", kSetters, {{"msg", MSG}, {"ch", CH}, {"note", "note number 0-127"}, {"vel", "velocity 0-127"}}),
		ApiFunction("setNoteOff", kSetters, {{"msg", MSG}, {"ch", CH}, {"note", "note number 0-127"}, {"vel", "release velocity 0-127, default 0", true}}),
		ApiFunction("setNRPN", kSetters, {{"nrpn", NRPN}, {"ch", CH}, {"number", "parameter number 0-16383"}, {"value", "value 0-16383"}}),
		ApiFunction("setRPN", kSetters, {{"rpn", "handle from midi.createRPN()"}, {"ch", CH}, {"number", "parameter number 0-16383"}, {"value", "value 0-16383"}}),
		ApiFunction("setPitchWheel", kSetters, {{"msg", MSG}, {"ch", CH}, {"value", "0-16383, 8192 is the centre"}}),
		ApiFunction("setProgramChange", kSetters, {{"msg", MSG}, {"ch", CH}, {"program", "program number 0-127"}}),
		ApiFunction("setSysEx", kSetters, {{"msg", MSG}, {"hexString", "payload as hex, without f0/f7 framing"}}),
		ApiFunction("setRaw", kSetters, {{"msg", MSG}, {"hexString", "exact bytes as hex, no framing added"}}),
		ApiFunction("setValue", kSetters, {{"msg", MSG}, {"value", "data byte 0-127"}}),

		ApiFunction("enablePorts", "", {{"n", "deliver MIDI inputs 1-n, n is 1-4"}}),
		ApiFunction("enableNrpnIn", kExtended, {{"midiPort", PORT}, {"channel", CHANNEL_OPT, true}}),
		ApiFunction("enableRpnIn", kExtended, {{"midiPort", PORT}, {"channel", CHANNEL_OPT, true}}),
		ApiFunction("enableCc14bitIn", kExtended, {{"midiPort", PORT}, {"cc", "MSB controller 0-31, default all", true}, {"channel", CHANNEL_OPT, true}}),
	}));

	api.push_back(ApiGroup("midiOut", {
		ApiFunction("enablePorts", "", {{"count", "enable MIDI outputs 1-count, count is 1-4"}}),
		ApiFunction("enableTiming", "", {{"reportLate", "true logs messages that arrive too late", true}}),
		ApiFunction("selectPort", "", {{"midiPort", "MIDI output port, 1-based, for the following sends"}}),
		ApiFunction("send", "", {{"msg", "message handle, or the first handle of an NRPN or 14-bit CC group"}}),
		ApiFunction("sendAfterMs", "", {{"msg", MSG}, {"ms", "delay in milliseconds, -1 sends after Rack's output queue"}}),
		ApiFunction("sendAtFrame", "", {{"msg", MSG}, {"frame", "engine frame, see rack.getEventFrame()"}}),
		ApiFunction("sendAfterTrigger", "", {{"msg", MSG}, {"ticks", "clock ticks to wait"}, {"trigPort", "trigger input 1-2, default 1", true}, {"channel", TRIG_CH, true}}),
	}));

	return api;
}

} // namespace MidiScript
} // namespace StoermelderPackOne
