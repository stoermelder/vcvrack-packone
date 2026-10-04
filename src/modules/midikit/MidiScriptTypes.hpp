#pragma once
#include "../../plugin.hpp"
#include "../../utils/SlotRingBuffer.hpp"
#include "../../utils/MpmcTaskWorker.hpp"
#include "../../utils/TaskWorker.hpp"
#include "BroadcastBus.hpp"
#include "../midi/MidiProcessor.hpp"
#include <jansson.h>
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

// A context-menu item from rack.registerContextMenu(). Presentation data only;
// the callbacks stay in the engine under callbackId, so a copy is safe on the
// UI thread.
struct ScriptMenuItem {
	// Action: a plain entry, onChange() on every click. File: opens a file dialog and
	// calls onChange(content, fileName) with the file's text; the file is at most
	// fileMaxBytes long.
	enum class Type { Boolean, Options, Action, File } type = Type::Boolean;
	static const size_t fileMaxBytes = 2048;
	std::string label;
	// Options variant: selectable labels and the current selection index.
	std::vector<std::string> options;
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
// selected index of an options item, so those call sites just pass the number. A file
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

// One argument of onChange, in a form both engines push onto their own stack.
struct ScriptMenuArg {
	enum class Kind { Bool, Int, String } kind;
	bool b = false;
	int i = 0;
	std::string s;

	static ScriptMenuArg ofBool(bool v) { ScriptMenuArg a; a.kind = Kind::Bool; a.b = v; return a; }
	static ScriptMenuArg ofInt(int v) { ScriptMenuArg a; a.kind = Kind::Int; a.i = v; return a; }
	static ScriptMenuArg ofString(const std::string& v) { ScriptMenuArg a; a.kind = Kind::String; a.s = v; return a; }
};

// The arguments of onChange for a click on `item`:
//   boolean  (checked)             options  (selectedIndex, selectedLabel)
//   action   ()                    file     (content, fileName)
// False if the click does not fit the item, which is then not called: a file click on
// a non-file item (a reload may have put another item at that id while the dialog was
// open), a click without a file on a file item, or an options index out of range.
inline bool menuCallArgs(const ScriptMenuItem& item, const ScriptMenuClick& click, std::vector<ScriptMenuArg>& args) {
	args.clear();
	if (click.hasFile != (item.type == ScriptMenuItem::Type::File)) return false;
	switch (item.type) {
		case ScriptMenuItem::Type::Action:
			return true;
		case ScriptMenuItem::Type::Boolean:
			args.push_back(ScriptMenuArg::ofBool(click.value != 0));
			return true;
		case ScriptMenuItem::Type::Options:
			if (click.value < 0 || click.value >= static_cast<int>(item.options.size())) return false;
			args.push_back(ScriptMenuArg::ofInt(click.value));
			args.push_back(ScriptMenuArg::ofString(item.options[click.value]));
			return true;
		case ScriptMenuItem::Type::File:
			args.push_back(ScriptMenuArg::ofString(click.content));
			args.push_back(ScriptMenuArg::ofString(click.fileName));
			return true;
	}
	return false;
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
// how most keyboards send a release. midi.isNoteRelease(); the same folding as
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
