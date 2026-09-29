#pragma once
#include "../../plugin.hpp"
#include "../../utils/SpscLatestValue.hpp"
#include "../../utils/TaskWorker.hpp"
#include "../midi/MidiProcessor.hpp"
#include <atomic>
#include <chrono>
#include <future>
#include <jansson.h>
#include <memory>
#include <thread>

namespace StoermelderPackOne {
namespace MidiScript {

using rack::midi::Message;
using MessageEx = StoermelderPackOne::MessageEx;


// Caps on Tipsy payloads: queue entries are fixed-size PODs so the audio
// thread's shift() never heap-allocates (mime matches tipsy::kMaxMimeTypeSize).
static constexpr size_t tipsyMaxMimeTypeSize = 256;
static constexpr size_t tipsyMaxPayloadLength = 256;

// One Tipsy message in transit, either direction. Fixed-size so the SPSC
// RingBuffers copy it without heap allocation.
//
// Outbound (module's tipsyOutQueue), mimeSize == 0 marks a discard sentinel
// rather than a real message: it carries no payload and exists only to mark
// where a stale run of messages ends. sendTipsyOut() rejects an empty mime type
// so the two can never be confused. (dataSize would not work as the marker — an
// empty payload with a valid mime type is a legitimate message.) Inbound
// (engine's tipsyInQueue) has no sentinels; every entry is a decoded message.
struct TipsyMessage {
	uint16_t mimeSize;                         // length without NUL
	uint16_t dataSize;
	char mime[tipsyMaxMimeTypeSize];
	unsigned char data[tipsyMaxPayloadLength];
};


// One inbound MIDI message on its way from the audio thread to the worker.
//
// Carries the decode result alongside the raw message because the assembled
// forms cannot be expressed by rack::midi::Message alone: an NRPN/RPN parameter
// number and a 14-bit value do not fit its 7-bit data bytes. Together these four
// fields reconstruct a StoermelderPackOne::MessageEx exactly — it holds nothing
// else — so the worker can rebuild one without the module keeping decoder state
// of its own.
//
// A plain (unassembled) message uses type CC/NOTE_ON/… with both extras at -1,
// which is what MessageEx itself defaults them to.
struct QueuedMessage {
	Message msg;
	MessageEx::Type type = MessageEx::Type::RESET;
	int16_t paramNumber = -1;
	int16_t extraValue = -1;
	// Whether this CC is part of an extended message (see MessageEx::isComponent).
	// Set by the module on the audio thread; the worker uses it to decide whether
	// the script should see the raw CC as well as the assembled event.
	bool isComponent = false;

	QueuedMessage() {}
	// Deliberately implicit: a bare Message IS an undecoded QueuedMessage, and
	// callers that inject raw MIDI (tests, and any path with no decoder in front
	// of it) should not have to spell out four defaulted fields to say so.
	QueuedMessage(const Message& msg) : msg(msg) {}
};


// A context-menu item registered via rack.registerContextMenu(). Carries
// presentation data only — the onChange/onGetValue callbacks live in the
// engine's map keyed by callbackId, so a copied spec is safe on the UI thread.
struct ScriptMenuItem {
	enum class Type { Boolean, Options } type = Type::Boolean;
	std::string label;
	// Options variant: selectable labels and the current selection index.
	std::vector<std::string> options;
	// checked (Boolean) and selected (Options) share storage — only the one
	// matching `type` is meaningful; selected(0) zeroes both.
	union {
		bool checked;
		int selected;
	};
	// Opaque handle resolving to the script's onChange callback in the engine.
	int callbackId = -1;

	ScriptMenuItem() : selected(0) {}
};


// Host-module interface for everything that touches the module's hardware
// (inputs/outputs/triggers) and UI (log/overlay), keeping the engine free of
// module-specific knowledge.
struct MidiScriptEngineHandler {
	virtual void writeLog(const std::string& s, bool useTimestamp = true) = 0;
	virtual void writeOverlay(const std::string& s1, const std::string& s2, const std::string& s3) = 0;
	virtual void enableInput(int i) = 0;

	// Marks trigger input (port, channel) as enabled, from the script-facing
	// trig.enableIn() binding (worker thread). Disabled channels get no tick
	// processing (counting, sendAfterTrigger drains, or trig.onTrigger).
	virtual void enableTrigger(int port, uint8_t channel) = 0;

	// Routes trigger input i into the Tipsy decoder, or disables decoding when
	// i < 0. Today i is always 0 — the script-facing trig.enableTipsyIn() exposes
	// no port (Tipsy input is only supported on the first trigger input). While
	// claimed, the trigger input stops counting ticks and firing trig.onTrigger,
	// and channel 1 of trig.isHigh()/isLow() reads 0 (other channels are
	// unaffected). Worker thread.
	virtual void enableTipsyIn(int i) = 0;

	// Enables assembly of NRPN (kind 0) or RPN (kind 1) on midiPort, delivering
	// completed parameter changes to midi.onNrpn/onRpn. channel is 0-based, or
	// -1 for every channel. Worker thread.
	//
	// While enabled the component CCs (98/99/100/101, and 6/38 while a parameter
	// is armed) stop reaching midi.onMessage: a script that asked for assembled
	// events should not also have to filter the parts they were built from.
	virtual void enableNrpnIn(int midiPort, int kind, int channel) = 0;

	// Enables 14-bit CC assembly on midiPort for MSB controller `cc` (0-31, its
	// LSB is implicitly cc + 32), or every one of them when cc < 0. channel is
	// 0-based, or -1 for every channel. Worker thread.
	//
	// Consumption matches enableNrpnIn(): both halves of an enabled pair stop
	// reaching midi.onMessage. Registration is per-CC precisely so a script can
	// take CC 7 as 14-bit while still seeing CC 39 raw.
	virtual void enableCc14bitIn(int midiPort, int cc, int channel) = 0;

	virtual float getInputVoltage(int i, uint8_t ch) = 0;
	virtual float getTrigVoltage(int i, uint8_t ch) = 0;
	virtual uint64_t getTrigTicks(int i, uint8_t ch) = 0;
	virtual void enableParam(int i) = 0;
	virtual float getParamValue(int i) = 0;
	virtual void setTrig(int i, uint8_t ch, float duration = 1e-3f) = 0;
	virtual void setTrigVoltage(int i, uint8_t ch, float voltage) = 0;

	// Queues `count` MIDI messages for output, all sharing one tick and the same
	// trigger-input channel (only meaningful for tick-scheduled messages from
	// sendAfterTrigger(); immediate/frame messages pass channel 0). Called from
	// the worker thread.
	//
	// All-or-nothing: returns false without queuing any of them if there is not
	// room for the whole group. A group is a multi-message value — an NRPN (4
	// messages) or a 14-bit CC pair (2 messages) — and a partial group is a
	// malformed parameter change, worse than dropping it outright. A single
	// message (count == 1) is just the degenerate case, so there is one entry
	// point and one bounds check rather than two.
	//
	// Output saturation is expected, so callers treat false as normal, not an error.
	virtual bool sendMidi(int midiPort, const Message* msgs, size_t count, uint8_t channel, uint64_t tick) = 0;

	// Queues a Tipsy protocol message for output on the module's trigger CV.
	// Called from the worker thread; the module encodes and emits it on the
	// audio thread. Returns false if the payload was rejected or there was no
	// room — like sendMidi(), saturation is normal rather than an error.
	virtual bool sendTipsyOut(const char* mimeType, const unsigned char* data, uint32_t dataBytes) = 0;

	// Marks every Tipsy message queued so far as stale, so the module discards
	// them instead of emitting them (a script reload happened). A message
	// already being encoded still completes. Worker thread.
	virtual void sendTipsyOutReset() = 0;
};


struct MidiScriptEngine {
	// Cap on setSysEx's payload, so a script can't build an unbounded message
	// for the handler's fixed-size out-queue.
	static const int sysExMaxPayloadLength = 256;

	// The handler this engine runs inside, injected at construction. Every
	// module-facing callback (log/overlay/input/trig/param) routes through it.
	MidiScriptEngineHandler* handler;

	// rack.setConfig()/getConfig() limits — shared constants so the two
	// engines can't drift on what they accept.
	//
	// Depth 1 is the value passed to setConfig() itself; the cap is also what
	// makes cyclic Lua tables/JS objects terminate, since depth is the only
	// defence the converters apply without tracking visited nodes.
	static const int configMaxDepth = 4;
	// Total serialized size of the whole config, checked after conversion
	// against the prospective new config (not the single value being written).
	static const size_t configMaxBytes = 65536;

	// Wraps a json_t* (already owned/incref'd by the caller) in a shared_ptr
	// whose deleter decrefs it.
	static std::shared_ptr<json_t> ownJson(json_t* j) {
		return std::shared_ptr<json_t>(j, [](json_t* p) { json_decref(p); });
	}

	// The engine-owned working copy of the script's config (a flat JSON
	// object; values may nest). setConfig() mutates it, getConfig() reads it —
	// both run on the script thread, so this needs no synchronization at all.
	// Never null once constructed.
	std::shared_ptr<json_t> workingConfig = ownJson(json_object());

	// Published config, script thread -> UI thread.
	//
	// The published json_t is IMMUTABLE: setConfig() mutates the working copy,
	// then publishes a fresh copy, so a reader holding a shared_ptr can walk
	// its version safely while the script writes the next one. Mutating an
	// already-published object instead would race dataToJson() inside
	// json_dumps(): jansson's refcount is atomic but its containers are not.
	//
	// Single writer (the script thread), single reader (dataToJson()).
	// getConfig() must NOT read through this — it reads the working copy
	// directly, since SpscLatestValue permits only one reader.
	SpscLatestValue<std::shared_ptr<json_t>> publishedConfig{ownJson(json_object())};

	// Script thread. Mutates the working copy, then publishes a copy of it.
	// `value` is owned (may be null, meaning "delete key").
	//
	// json_copy() (shallow) suffices rather than json_deep_copy(): the copy
	// shares child *values* with the working copy, and those are only ever
	// replaced wholesale by json_object_set_new, never mutated in place.
	void setConfigValue(const char* key, json_t* value /* owned, may be null */) {
		assert(onWorkerThread());
		if (!value) json_object_del(workingConfig.get(), key);
		else        json_object_set_new(workingConfig.get(), key, value);
		std::shared_ptr<json_t> copy = ownJson(json_copy(workingConfig.get()));
		publishedConfig.store(std::move(copy));
	}

	// Script thread. Returns a borrowed pointer (no incref) into workingConfig,
	// or NULL if `key` is unset.
	json_t* getConfigValue(const char* key) const {
		assert(onWorkerThread());
		return json_object_get(workingConfig.get(), key);
	}

	// Replaces workingConfig wholesale (patch load, script switch, reset) and
	// publishes it immediately, so dataToJson() never observes a stale config
	// from the previous script/state. `initial` is owned (may be null, meaning
	// "start empty").
	void installConfig(json_t* initial /* owned, may be null */) {
		assert(onWorkerThread());
		workingConfig = ownJson(initial ? initial : json_object());
		std::shared_ptr<json_t> copy = ownJson(json_copy(workingConfig.get()));
		publishedConfig.store(std::move(copy));
	}

	// UI thread. Returns a reference into the SpscLatestValue slot, not a
	// copy: peek() returns const T&, stable until the next load()/peek()/
	// load_if_new() call on this (the only) reader thread.
	const std::shared_ptr<json_t>& peekConfig() {
		return publishedConfig.peek();
	}

	// Validates a setConfig()/getConfig() key: [A-Za-z_][A-Za-z0-9_]{0,63}.
	// Rejecting anything else (including '.') reserves '.' for a possible
	// future path-addressing form instead of silently accepting a flat key
	// that looks like a nested one.
	static bool isValidConfigKey(const char* key) {
		if (!key || key[0] == '\0') return false;
		size_t len = strlen(key);
		if (len > 64) return false;
		char c0 = key[0];
		if (!((c0 >= 'A' && c0 <= 'Z') || (c0 >= 'a' && c0 <= 'z') || c0 == '_')) return false;
		for (size_t i = 1; i < len; i++) {
			char c = key[i];
			bool ok = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_';
			if (!ok) return false;
		}
		return true;
	}

	int inputCount;
	int inputTrigCount;
	int outputTrigCount;
	int paramCount;
	int midiInputCount;
	int midiOutputCount;

	MidiScriptEngine(MidiScriptEngineHandler* handler, int inputCount, int inputTrigCount, int outputTrigCount, int paramCount, int midiInputCount, int midiOutputCount)
		: handler(handler), inputCount(inputCount), inputTrigCount(inputTrigCount), outputTrigCount(outputTrigCount), paramCount(paramCount), midiInputCount(midiInputCount), midiOutputCount(midiOutputCount) {}

	std::shared_ptr<ITaskWorker> taskWorker;
	dsp::RingBuffer<std::tuple<int, QueuedMessage>, 128> midiInQueue;
	// (trigPort, channel) — the trigger input is polyphonic, so each tick
	// carries the channel that fired.
	dsp::RingBuffer<std::tuple<int, uint8_t>, 4> tickInQueue;

	void setWorker(std::shared_ptr<ITaskWorker> w) {
		taskWorker = std::move(w);
	}

	bool runAsync(std::function<void()> task) {
		return taskWorker->work(task, APP);
	}

	// True on the thread script code runs on. All script execution happens there
	// — dispatch, load/teardown, setConfig()/getConfig() — so the interpreter and
	// contextMenus are only ever touched from it, and the call sites assert that.
	// Always true under SyncTaskWorker (tests), which runs tasks inline.
	bool onWorkerThread() const {
		return taskWorker && taskWorker->isWorkerThread();
	}

	// Runs `task` (script code) on the worker thread and blocks until done.
	//
	// Returns false, leaving `out` untouched, if the task never ran.
	// closeState() is the only caller left, and it discards the result.
	//
	// The wait is bounded for liveness, not latency: ~MpmcTaskWorker discards
	// pending tasks, which breaks the promise (hence the catch), so the timeout
	// only covers a wedged-but-alive worker. The shared_ptr keeps the promise
	// alive for a worker still running past it.
	bool runSync(std::function<std::string()> task, std::string& out) {
		auto promise = std::make_shared<std::promise<std::string>>();
		std::future<std::string> future = promise->get_future();

		bool queued = runAsync([task, promise]() {
			promise->set_value(task());
		});
		if (!queued) return false;

		// Function-local: wait_for() takes its duration by reference, so a static
		// constexpr member would be odr-used and need an out-of-line definition.
		const std::chrono::milliseconds timeout{500};

		if (future.wait_for(timeout) != std::future_status::ready) return false;
		try {
			out = future.get();
		}
		catch (const std::future_error&) {
			return false;
		}
		return true;
	}

	// True if this engine should process `script`: a simple "@engine <name>"
	// substring match, so the module needs no header parser of its own.
	virtual bool testScript(const std::string& script) = 0;


	// initialConfigJson, if non-empty, is parsed and installed on the engine as
	// the script's workingConfig BEFORE any script code runs — top-level code
	// and onLoad() see it via rack.getConfig(). Empty installs a fresh, empty
	// config (script switch). This is an install, not an argument to a hook:
	// getConfig()/setConfig() are live calls, not hooks.
	//
	// Asynchronous, with no completion signal by design: the engine is NOT loaded
	// when this returns, and load messages/parse errors reach the user via
	// handler->writeLog() from the worker. Callers must not read engine state
	// expecting the new script — the dispatch paths no-op until the load lands.
	// The worker queue is FIFO, so successive calls (a script switch tearing one
	// engine down and loading another) still run in call order.
	//
	// `script` is copied, so the caller's buffer need not outlive this call.
	void loadScript(const char* script, const std::string& initialConfigJson = "") {
		std::string s = script ? script : "";
		runAsync([this, s, initialConfigJson]() {
			loadScriptOnWorker(s.c_str(), initialConfigJson);
		});
	}

	// The load itself, always on the worker thread. Tears down any previous
	// script (via closeStateOnWorker()) before loading the new one; callers
	// already on the worker invoke this directly rather than loadScript().
	// Implementations must installConfig() from initialConfigJson at the top,
	// before any script code runs.
	virtual void loadScriptOnWorker(const char* script, const std::string& initialConfigJson) = 0;

	// Tears down script state, running rack.onUnload() first (e.g. all-notes-off).
	// onUnload()'s return value is ignored — config comes from rack.setConfig(),
	// not from teardown. publishedConfig is deliberately left untouched: a save
	// racing this teardown still persists the last known config.
	// Always returns "".
	//
	// Blocks, unlike loadScript(): the caller (MidiKitModule's destructor) is
	// about to destroy the handler and these engines. UI thread only —
	// worker-side code calls closeStateOnWorker() directly.
	//
	// A failed dispatch is deliberately ignored rather than retried inline: the
	// worker may be wedged inside the interpreter, and freeing it here would put
	// two threads in it at once. Leaking it is the lesser evil.
	std::string closeState() {
		std::string ignored;
		runSync([this]() -> std::string {
			closeStateOnWorker();
			return "";
		}, ignored);
		return "";
	}

	// The teardown itself, always on the worker thread. Implementations free
	// their interpreter here; callers already on the worker (loadScriptOnWorker,
	// including its error paths) invoke this directly rather than closeState().
	virtual void closeStateOnWorker() = 0;

	// Main interface for message processing. Takes the decoded form so the
	// assembly the module already performed (NRPN/RPN/14-bit CC) travels with
	// the raw message instead of being redone on the worker.
	virtual void processInMessage(int midiPort, const QueuedMessage& msg) = 0;
	virtual void processInTick(int trigPort, uint8_t channel) = 0;

	// Decoded Tipsy messages awaiting dispatch. Engine-owned, like midiInQueue:
	// the decoding is the module's job but dispatching into script code is the
	// engine's. Pushed by the module's processTipsyInput() (audio thread),
	// drained by process() (worker) — the mirror image of the module's
	// tipsyOutQueue.
	dsp::RingBuffer<TipsyMessage, 8> tipsyInQueue;

	// Dispatches queued midiInQueue/tickInQueue/tipsyInQueue onto the engine via
	// runAsync(). Virtual so tests can override it to observe call counts.
	virtual void process() {
		if ((midiInQueue.size() > 0 || tickInQueue.size() > 0 || tipsyInQueue.size() > 0)) {
			runAsync([this]() {
				while (!midiInQueue.empty()) {
					auto t = midiInQueue.shift();
					int midiPort = std::get<0>(t);
					QueuedMessage q = std::get<1>(t);
					switch (q.type) {
						case MessageEx::Type::NRPN:
						case MessageEx::Type::RPN:
							dispatchNrpn(midiPort, q, q.type == MessageEx::Type::RPN);
							break;
						case MessageEx::Type::CC_14BIT:
							dispatchCc14bit(midiPort, q);
							break;
						default:
							dispatchMidiMessage(midiPort, q.msg);
							break;
					}
				}
				while (!tickInQueue.empty()) {
					auto t = tickInQueue.shift();
					dispatchTrigger(std::get<0>(t), std::get<1>(t));
				}
				while (!tipsyInQueue.empty()) {
					TipsyMessage msg = tipsyInQueue.shift();
					dispatchTipsyMessage(msg);
				}
			});
		}
	}

	// Engine-specific dispatch of a single message/tick, invoked from
	// process() above on the worker thread.
	virtual void dispatchMidiMessage(int midiPort, Message& msg) = 0;

	// Dispatches an assembled parameter change to midi.onNrpn/onRpn, or a 14-bit
	// controller change to midi.onCc14bit.
	//
	// The whole QueuedMessage is passed, not the decoded scalars, because the
	// script receives it as a message HANDLE — the same shape onMessage gets —
	// and reads it through midi.getControl()/getValue()/getChannel(). That keeps
	// the raw bytes reachable and lets the handle be cloned or forwarded like any
	// other. Worker thread.
	virtual void dispatchNrpn(int midiPort, const QueuedMessage& q, bool isRpn) = 0;
	virtual void dispatchCc14bit(int midiPort, const QueuedMessage& q) = 0;
	virtual void dispatchTrigger(int trigPort, uint8_t channel) = 0;
	virtual void dispatchTipsyMessage(const TipsyMessage& msg) = 0;

	// Queries into the script from the UI
	virtual std::string getInputName(int i) = 0;
	virtual std::string getParamName(int i) = 0;
	virtual std::string getParamFormatValue(int i) = 0;

	// Called from the UI thread while the context menu is built. Evaluates each
	// item's onGetValue on the WORKER thread (script code must never run on the
	// UI thread), then invokes `callback`. The callback must not construct
	// widgets — it only publishes specs for the menu to poll from step().
	virtual void getContextMenus(const std::function<void(const std::vector<ScriptMenuItem>&)>& callback) = 0;
	// Called from the UI thread when the user clicks a menu item. value is 0/1
	// for Boolean, the selected index for Options. Runs the callback on the
	// worker thread.
	virtual void invokeContextMenuCallback(int callbackId, int value) = 0;
};


struct MidiScriptEnginePortInfo : PortInfo {
	bool enabled;
	MidiScriptEngine* se;
	std::string bufferedName;
	std::atomic<bool> queryInFlight{false};

	std::string getName() override {
		if (enabled) {
			bool expected = false;
			if (queryInFlight.compare_exchange_strong(expected, true)) {
				bool queued = se->runAsync([=] {
					bufferedName = se->getInputName(portId);
					queryInFlight.store(false);
				});
				if (!queued) {
					queryInFlight.store(false);
				}
			}
			return bufferedName;
		}
		return "<Disabled>";
	}
};


struct MidiScriptEngineParamQuantity : ParamQuantity {
	bool enabled;
	MidiScriptEngine* se;
	std::string bufferedLabel;
	std::string bufferedDisplayValue;
	std::atomic<bool> queryInFlight{false};

	std::string getLabel() override {
		return enabled ? bufferedLabel : "";
	}
	std::string getDisplayValueString() override {
		if (enabled) {
			bool expected = false;
			if (queryInFlight.compare_exchange_strong(expected, true)) {
				bool queued = se->runAsync([=] {
					bufferedLabel = se->getParamName(paramId);
					bufferedDisplayValue = se->getParamFormatValue(paramId);
					queryInFlight.store(false);
				});
				if (!queued) {
					queryInFlight.store(false);
				}
			}
			std::string s = bufferedDisplayValue;
			return !s.empty() ? s : ParamQuantity::getDisplayValueString();
		}
		else {
			return "<Disabled>";
		}
	}
};


} // namespace MidiScript
} // namespace StoermelderPackOne