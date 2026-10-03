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
	// Implicit on purpose: a bare Message is an undecoded QueuedMessage.
	QueuedMessage(const Message& msg) : msg(msg) {}
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
	enum class Type { Boolean, Options } type = Type::Boolean;
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

} // namespace MidiScript
} // namespace StoermelderPackOne
