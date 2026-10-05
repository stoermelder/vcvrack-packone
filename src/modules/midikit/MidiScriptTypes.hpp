#pragma once
#include "../../plugin.hpp"
#include "../../utils/SlotRingBuffer.hpp"
#include "../../utils/MpmcTaskWorker.hpp"
#include "../../utils/TaskWorker.hpp"
#include "BroadcastBus.hpp"
#include "../midi/MidiProcessor.hpp"
#include <jansson.h>
#include <cmath>
#include <cstring>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace StoermelderPackOne {
namespace MidiScript {

using rack::midi::Message;
using MessageEx = StoermelderPackOne::MessageEx;

struct MidiScriptEngine;
using BroadcastBus = BasicBroadcastBus<MidiScriptEngine>;

// The worker running the script code of the engines sharing it, and their
// broadcast bus. One struct, so the broadcast scope follows the worker's scope.
struct WorkerDomain {
	std::shared_ptr<BroadcastBus> bus;
	std::shared_ptr<ITaskWorker> worker;

	// Injected by tests. Null: a new MpmcTaskWorker, a private bus (so no
	// broadcasts reach anyone unless a test shares the bus).
	explicit WorkerDomain(std::shared_ptr<ITaskWorker> worker = nullptr, std::shared_ptr<BroadcastBus> bus = nullptr)
		: bus(bus ? std::move(bus) : std::make_shared<BroadcastBus>()),
		  worker(worker ? std::move(worker) : std::make_shared<MpmcTaskWorker>("MidiKit worker")) {}
};


// Tipsy payload caps: entries are fixed-size PODs, so the audio thread never
// allocates (mime matches tipsy::kMaxMimeTypeSize).
static constexpr size_t tipsyMaxMimeTypeSize = 256;
static constexpr size_t tipsyMaxPayloadLength = 256;

// One Tipsy message in transit, either direction.
//
// Outbound (module's tipsyOutQueue), mimeSize == 0 marks a discard sentinel: no
// payload, it only ends a stale run of messages. sendTipsyOut() rejects an empty
// mime type, so it can't be confused with a real one. Inbound (tipsyInQueue)
// has no sentinels.
struct TipsyMessage {
	uint16_t mimeSize;                         // length without NUL
	uint16_t dataSize;
	char mime[tipsyMaxMimeTypeSize];
	unsigned char data[tipsyMaxPayloadLength];
	// Inbound only: the engine frame the final byte decoded on, -1 if unknown.
	int64_t frame = -1;
};


// One inbound MIDI message, audio thread -> worker. Carries the decode result
// with the raw message: an NRPN/RPN number or 14-bit value does not fit a
// rack::midi::Message. The four decode fields rebuild a MessageEx exactly. A
// plain message has both extras at -1, MessageEx's own default.
struct QueuedMessage {
	Message msg;
	MessageEx::Type type = MessageEx::Type::RESET;
	int16_t paramNumber = -1;
	int16_t extraValue = -1;
	// This CC is part of an extended message (MessageEx::isComponent). Set on the
	// audio thread; the worker decides from it whether the script sees the raw CC.
	bool isComponent = false;
	// The frame Rack assigned on arrival (the completing component's, if
	// assembled), -1 if unknown. Not msg.frame, which is the outbound request.
	int64_t frame = -1;

	QueuedMessage() {}
	// Explicit: a bare Message is an undecoded QueuedMessage, one that skipped the
	// input stage (MidiInputs::accepts(), the decoder, the arrival frame). Tests
	// that feed the engine directly say so with QueuedMessage(msg).
	explicit QueuedMessage(const Message& msg) : msg(msg) {}
};


// A message for the worker, with the input port it arrived on.
struct MidiInMessage {
	int port = 0;
	QueuedMessage msg;
};

// Audio thread -> worker queue of incoming messages. Slots reserve room for
// ordinary SysEx; a longer message grows its slot once, and it keeps that.
struct MidiInRingBuffer : SlotRingBuffer<MidiInMessage, 128> {
	enum { SLOT_BYTES = 64 };

	MidiInRingBuffer() : SlotRingBuffer<MidiInMessage, 128>([](MidiInMessage& m) {m.msg.msg.bytes.reserve(SLOT_BYTES); }) {}

	bool tryPush(int port, const QueuedMessage& msg) {
		return tryPushWith([&](MidiInMessage& slot) {
			slot.port = port;
			slot.msg = msg;
		});
	}
};

// A value in a menu: one argument of onChange, or the value of an options pair, in a form
// both engines convert from and to their own types. A number that is a whole 32-bit
// integer is an Int, so a script sees 4 and not 4.0.
struct ScriptMenuArg {
	enum class Kind { Bool, Int, Number, String } kind = Kind::Int;
	bool b = false;
	int i = 0;
	double d = 0.0;
	std::string s;

	static ScriptMenuArg ofBool(bool v) {
		ScriptMenuArg a; a.kind = Kind::Bool; a.b = v; return a;
	}
	static ScriptMenuArg ofInt(int v) {
		ScriptMenuArg a; a.kind = Kind::Int; a.i = v; return a;
	}
	static ScriptMenuArg ofNumber(double v) {
		if (v == std::floor(v) && std::fabs(v) <= 2147483647.0) return ofInt(static_cast<int>(v));
		ScriptMenuArg a; a.kind = Kind::Number; a.d = v; return a;
	}
	static ScriptMenuArg ofString(const std::string& v) {
		ScriptMenuArg a; a.kind = Kind::String; a.s = v; return a;
	}

	bool isNumeric() const {
		return kind == Kind::Int || kind == Kind::Number;
	}
	double asDouble() const {
		return kind == Kind::Int ? static_cast<double>(i) : d;
	}
	// Same type and value; an Int and a Number are both numbers (1 equals 1.0), as
	// `===` in JS and `==` in Lua see them.
	bool operator==(const ScriptMenuArg& o) const {
		if (isNumeric() && o.isNumeric()) return asDouble() == o.asDouble();
		if (kind != o.kind) return false;
		return kind == Kind::Bool ? b == o.b : s == o.s;
	}
};

// A context-menu item from rack.registerContextMenu(). Presentation data only;
// the callbacks stay in the engine under callbackId, so a copy is safe on the
// UI thread.
struct ScriptMenuItem {
	// Action: a plain entry, onChange() on every click. FileOpen: opens a file dialog and
	// calls onChange(content, fileName) with the file's text; the file is at most
	// fileMaxBytes long.
	// Separator and Label are display only: a divider line, and a heading with no click.
	enum class Type { Boolean, Options, Action, FileOpen, Separator, Label } type = Type::Boolean;
	static const size_t fileMaxBytes = 8192;
	std::string label;
	// The label as the script registered it, before a preset turned it into display text.
	// Registering again and unregisterContextMenu() match on this, never on `label`.
	std::string key;
	// Options variant: selectable labels and the current selection index (-1: none).
	// optionValues is empty for a plain list of labels, where an option stands for its
	// index; with [label, value] pairs it holds one value per label.
	std::vector<std::string> options;
	std::vector<ScriptMenuArg> optionValues;
	// Share storage; only the one matching `type` is meaningful.
	union {
		bool checked;
		int selected;
	};
	// Resolves to the script's onChange callback in the engine.
	int callbackId = -1;

	ScriptMenuItem() : selected(0) {}
};

// What one click on a menu item brings along, for the engine to turn into onChange's
// arguments. An int converts implicitly: it is the new state of a boolean (0/1) or the
// selected index of an options item, so those call sites just pass the number. A FileOpen
// item's click carries the file instead.
struct ScriptMenuClick {
	int value = 0;
	bool hasFile = false;
	std::string content, fileName;

	ScriptMenuClick(int value = 0) : value(value) {}

	static ScriptMenuClick file(const std::string& content, const std::string& fileName) {
		ScriptMenuClick c;
		c.hasFile = true;
		c.content = content;
		c.fileName = fileName;
		return c;
	}
};

// The arguments of onChange for a click on `item`:
//   boolean  (checked)             options  (selectedIndex, selectedLabel), or with pairs
//   action   ()                             (selectedValue, selectedLabel)
//   fileopen (content, fileName)   separator, label: never called
// False if the click does not fit the item, which is then not called: a file click on
// a non-file item (a reload may have put another item at that id while the dialog was
// open), a click without a file on a file item, or an options index out of range.
inline bool menuCallArgs(const ScriptMenuItem& item, const ScriptMenuClick& click, std::vector<ScriptMenuArg>& args) {
	args.clear();
	if (click.hasFile != (item.type == ScriptMenuItem::Type::FileOpen)) return false;
	switch (item.type) {
		case ScriptMenuItem::Type::Action:
			return true;
		case ScriptMenuItem::Type::Boolean:
			args.push_back(ScriptMenuArg::ofBool(click.value != 0));
			return true;
		case ScriptMenuItem::Type::Options:
			if (click.value < 0 || click.value >= static_cast<int>(item.options.size())) return false;
			args.push_back(item.optionValues.empty() ? ScriptMenuArg::ofInt(click.value) : item.optionValues[click.value]);
			args.push_back(ScriptMenuArg::ofString(item.options[click.value]));
			return true;
		case ScriptMenuItem::Type::Separator:
		case ScriptMenuItem::Type::Label:
			return false;
		case ScriptMenuItem::Type::FileOpen:
			args.push_back(ScriptMenuArg::ofString(click.content));
			args.push_back(ScriptMenuArg::ofString(click.fileName));
			return true;
	}
	return false;
}

// Ready-made options menus. An options item whose label is a preset's key (optionally followed
// by a space and a suffix) gets the preset's label and options, and the script's own `options` are
// ignored. A suffix is shown in brackets after the label: "#midichannel Output" is the menu
// "MIDI channel (Output)", so that two menus of one preset can be told apart. To add a preset,
// add a row with a fill lambda to menuPresets().
struct MenuPreset {
	const char* key;
	const char* label;
	// Fills item.options and item.optionValues.
	std::function<void(ScriptMenuItem& item)> fill;
};

inline const std::vector<MenuPreset>& menuPresets() {
	// The channels 1-16 and, with `withAll`, "All" for 0 in front of them.
	auto channels = [](ScriptMenuItem& item, bool withAll) {
		if (withAll) {
			item.options.push_back("All");
			item.optionValues.push_back(ScriptMenuArg::ofInt(0));
		}
		for (int c = 1; c <= 16; c++) {
			item.options.push_back(std::to_string(c));
			item.optionValues.push_back(ScriptMenuArg::ofInt(c));
		}
	};
	static const std::vector<MenuPreset> presets = {
		{"#midichannel", "MIDI channel", [channels](ScriptMenuItem& item) { channels(item, false); }},
		{"#midichannel+all", "MIDI channel", [channels](ScriptMenuItem& item) { channels(item, true); }},
	};
	return presets;
}

// The preset that `label` names, or NULL. `suffix` is set to the text after the key and its space
// (an empty string if there is none). No allocation, so a Lua engine can call it where an error
// may be raised.
inline const MenuPreset* menuFindPreset(const char* label, const char** suffix) {
	for (const MenuPreset& preset : menuPresets()) {
		size_t keyLen = strlen(preset.key);
		if (strncmp(label, preset.key, keyLen) != 0) continue;
		if (label[keyLen] != '\0' && label[keyLen] != ' ') continue;
		*suffix = label[keyLen] == ' ' ? label + keyLen + 1 : label + keyLen;
		return &preset;
	}
	return NULL;
}

// Turns an options item into the menu of `preset`.
inline void menuApplyPreset(ScriptMenuItem& item, const MenuPreset& preset, const char* suffix) {
	item.label = preset.label;
	if (*suffix) item.label += std::string(" (") + suffix + ")";
	item.options.clear();
	item.optionValues.clear();
	preset.fill(item);
	item.selected = 0;
}

// The option of a [label, value] item that stands for `value`, or -1. A value that
// matches none is not an error: a stored config can hold one from an older version of
// the script.
inline int menuFindOptionValue(const ScriptMenuItem& item, const ScriptMenuArg& value) {
	for (size_t i = 0; i < item.optionValues.size(); i++) {
		if (item.optionValues[i] == value) return static_cast<int>(i);
	}
	return -1;
}


// The group a queued message belongs to: midiOut.send*() of an NRPN/RPN or
// 14-bit CC handle queues it as consecutive messages that carry the same
// OutGroup, so midiOut.cancel() can take the group whole and never splits it.
struct OutGroup {
	enum Kind : uint8_t { NONE, NRPN, RPN, CC14 };
	Kind kind = NONE;
	uint8_t channel = 0;   // 0-based
	uint16_t param = 0;    // NRPN/RPN: 14-bit parameter number; CC14: MSB controller
	bool operator==(const OutGroup& o) const { return kind == o.kind && channel == o.channel && param == o.param; }
};

// What a queued message carries besides its bytes.
struct OutTag {
	// From sendAfterMs/sendAtFrame/sendAfterTrigger. Only these can be cancelled:
	// a timing-mode send() waits in the same frame queue.
	bool scheduled = false;
	OutGroup group;
};

// midiOut.cancel(): everything, one address, or one group address.
enum class CancelMode : uint8_t { ALL, MESSAGE, GROUP };

// The type nibble, with a velocity-0 Note-On folded into Note-Off (0x8).
inline uint8_t addressType(const Message& m) {
	uint8_t type = m.bytes[0] >> 4;
	if (type == 0x9 && m.bytes.size() >= 3 && m.bytes[2] == 0) return 0x8;
	return type;
}

// Whether `m` releases a note: a Note-Off, or a Note-On with velocity 0, which is
// how most keyboards send a release. midi.getType() reports it as noteOff; the same folding as
// the address rules of midiOut.cancel(). False for anything without a status byte.
inline bool isNoteRelease(const Message& m) {
	if (m.bytes.empty() || m.bytes[0] < 0x80) return false;
	return addressType(m) == 0x8;
}

// Same address under the rules of midiOut.cancel() (see SCRIPTING.md). False for
// anything without a status byte. A default midi::Message has size 3 (zeros), so
// "empty" is a status byte below 0x80, not bytes.empty().
inline bool sameAddress(const Message& pattern, const Message& m) {
	if (pattern.bytes.empty() || m.bytes.empty()) return false;
	uint8_t a = pattern.bytes[0];
	uint8_t b = m.bytes[0];
	if (a < 0x80 || b < 0x80) return false;
	// SysEx matches any SysEx, other system messages their whole status byte.
	if (a >= 0xF0 || b >= 0xF0) return a == b;
	if ((a & 0x0F) != (b & 0x0F)) return false;
	uint8_t type = addressType(pattern);
	if (type != addressType(m)) return false;
	// Note, poly aftertouch and control change also address a data byte.
	if (type >= 0x8 && type <= 0xB) {
		if (pattern.bytes.size() < 2 || m.bytes.size() < 2) return false;
		return pattern.bytes[1] == m.bytes[1];
	}
	return true;
}

inline bool cancelMatches(CancelMode mode, const Message& p, const OutGroup& pg, const Message& m, const OutGroup& mg) {
	switch (mode) {
		case CancelMode::ALL: return true;
		case CancelMode::GROUP: return mg == pg;
		case CancelMode::MESSAGE: return mg.kind == OutGroup::NONE && sameAddress(p, m);
	}
	return false;
}

} // namespace MidiScript
} // namespace StoermelderPackOne
