#pragma once
#include "../../plugin.hpp"
#include "../../utils/SpscLatestValue.hpp"
#include "../../utils/TaskWorker.hpp"
#include "../midi/MidiProcessor.hpp"
#include <atomic>
#include <cmath>
#include <jansson.h>
#include <map>
#include <memory>
#include <sstream>
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
	// Inbound only: the engine frame the final byte decoded on, -1 if unknown.
	int64_t frame = -1;
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
	// The engine frame Rack assigned on arrival (the completing component's, for
	// assembled events), -1 if unknown. Not msg.frame: that one is an outbound
	// request that the send bindings overwrite on the handle a script holds.
	int64_t frame = -1;

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

	// Enables the first `count` MIDI inputs / outputs (count >= 1), from the
	// script-facing midi.enablePorts() / midiOut.enablePorts() bindings (worker
	// thread). Enabling never shrinks the count. Only a consecutive run starting
	// at port 1 can be in use, and only port 1 is by default; ports beyond the
	// count are dropped — incoming messages never reach the script, outgoing
	// messages are discarded. The count belongs to the script and is forgotten
	// on load/reset.
	virtual void enableMidiIn(int count) = 0;
	virtual void enableMidiOut(int count) = 0;

	// Worker thread, from unloadScriptOnWorker() right before a running script's
	// onUnload(); endUnload() follows. Until then only immediate MIDI
	// messages are accepted, and they go out even though the script is gone (in
	// timing mode behind everything Rack's output queue still holds, so a
	// note-off cannot overtake its note-on). Whatever onUnload() schedules for
	// later (sendAfterMs, sendAtFrame, sendAfterTrigger), trigger output writes
	// and Tipsy messages are ignored.
	virtual void beginUnload() = 0;

	// Drops the module-side state that belongs to the outgoing script: what it
	// enabled (MIDI ports, timing, trigger inputs, extended CC, Tipsy input), its
	// queued Tipsy messages (a message already being encoded still completes) and
	// whatever it scheduled for later; resets the log. Worker thread, at the end
	// of every unloadScriptOnWorker(), so after onUnload(), which still runs with
	// everything the script set up, and before any new script. Safe to call when
	// nothing was running.
	virtual void endUnload() = 0;

	// The audio thread's latest process() frame, the engine's block size and the
	// sample rate, published atomically for the worker, which must not read
	// APP->engine.
	virtual int64_t getTimingCurrentFrame() const = 0;
	virtual int64_t getTimingBlockFrames() const = 0;
	virtual float getSampleRate() const = 0;
	virtual bool isTimingEnabled() const = 0;

	// midiOut.enableTiming([reportLate]) binding (worker thread): outgoing
	// messages keep their frame and Rack places them, at the cost of one block of
	// latency. With reportLate the module logs messages that reached Rack too late
	// to be placed. Forgotten on load/reset, like the port enables.
	virtual void enableTiming(bool reportLate) = 0;

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
	// `frame` >= 0 stamps the write: the module applies it on the audio thread
	// when that frame comes up instead of right away (see frameForTrig()).
	virtual void setTrig(int i, uint8_t ch, float duration = 1e-3f, int64_t frame = -1) = 0;
	virtual void setTrigVoltage(int i, uint8_t ch, float voltage, int64_t frame = -1) = 0;

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
	virtual bool sendMidi(int midiPort, const Message* msgs, size_t count, uint8_t channel, uint64_t tick, int trigPort = 0) = 0;

	// Queues a Tipsy protocol message for output on the module's trigger CV.
	// Called from the worker thread; the module encodes and emits it on the
	// audio thread. Returns false if the payload was rejected or there was no
	// room — like sendMidi(), saturation is normal rather than an error.
	virtual bool sendTipsyOut(const char* mimeType, const unsigned char* data, uint32_t dataBytes) = 0;
};


struct MidiScriptEngine {
	// Cap on setSysEx's payload, so a script can't build an unbounded message
	// for the handler's fixed-size out-queue.
	static const int sysExMaxPayloadLength = 256;

	// Message handles a script can hold live per callback (slot 0 is the
	// incoming message). Shared so both engines create the same number.
	static const int msgStoreSize = 128;

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
	// (trigPort, channel, frame) — the trigger input is polyphonic, so each tick
	// carries the channel that fired and the frame of its edge. Sized for the
	// worst case between two drains (every 8th sample): all trigger channels of
	// the largest variant (2 ports x 16 channels) firing on each of 8 samples.
	dsp::RingBuffer<std::tuple<int, uint8_t, int64_t>, 256> tickInQueue;

	// Audio thread. dsp::RingBuffer::push() does not bounds-check: on a full
	// buffer it overwrites unread entries and leaves size() > capacity, after
	// which empty()/full() misreport. Drops the new event instead.
	template <typename Q, typename T>
	void pushInQueue(Q& queue, T&& value) {
		if (!queue.full()) queue.push(std::forward<T>(value));
	}

	// Worker thread (the consumer of the in-queues), from unloadScriptOnWorker():
	// events captured for the closed script must not reach the next one.
	void discardInQueues() {
		while (!midiInQueue.empty()) midiInQueue.shift();
		while (!tickInQueue.empty()) tickInQueue.shift();
		while (!tipsyInQueue.empty()) tipsyInQueue.shift();
	}

	// Worker thread: the frame of the event being dispatched, -1 outside one.
	int64_t currentInFrame = -1;
	// Sets currentInFrame for a scope and restores the previous value, so a
	// dispatch cannot leak its frame into unrelated work.
	struct InFrameScope {
		int64_t& slot;
		int64_t prev;
		InFrameScope(int64_t& slot, int64_t frame) : slot(slot), prev(slot) { slot = frame; }
		~InFrameScope() { slot = prev; }
	};

	// The frame for sendAfterMs: `ms` after the causing event in timing mode,
	// otherwise (and with no event) after the module's latest process() frame.
	// A negative `ms` (-1) means "after Rack's output queue": a framed message
	// handed over late in a block is transmitted up to two blocks after that
	// block starts, so a frame-less message released two blocks (and a frame) on
	// goes out behind everything Rack still holds. Shared by both engines.
	int64_t frameAfterMs(double ms) const {
		int64_t base = handler->isTimingEnabled() && currentInFrame >= 0 ? currentInFrame : handler->getTimingCurrentFrame();
		if (ms < 0.0) return base + 2 * handler->getTimingBlockFrames() + 1;
		float sr = handler->getSampleRate();
		return base + int64_t(sr > 0.f ? ms / 1000.0 * sr : 0.0);
	}

	// rack.msToFrames() / rack.framesToMs(): conversion at the current sample
	// rate, which is read on every call because it can change while a script is
	// loaded. Frames are rounded to a whole number, milliseconds are not. NaN
	// and an unknown (zero) sample rate give 0. Shared by both engines.
	double msToFrames(double ms) const {
		float sr = handler->getSampleRate();
		if (std::isnan(ms) || !(sr > 0.f)) return 0.0;
		return std::round(ms / 1000.0 * sr);
	}
	double framesToMs(double frames) const {
		float sr = handler->getSampleRate();
		if (std::isnan(frames) || !(sr > 0.f)) return 0.0;
		return frames / sr * 1000.0;
	}

	// The stamp for a trigger output write (trig.setTrigger/setGate/setHigh/
	// setLow). In timing mode, inside an event, the write is applied on the audio
	// thread at the event's frame plus one audio block: the same offset Rack puts
	// on framed MIDI, so a trigger and the MIDI it belongs to stay together.
	// -1 (apply when the script runs) otherwise.
	int64_t frameForTrig() const {
		if (!handler->isTimingEnabled() || currentInFrame < 0) return -1;
		return currentInFrame + handler->getTimingBlockFrames();
	}

	// The frame for send: the causing event's in timing mode (-1 outside an
	// event), always -1 otherwise.
	int64_t frameForSend() const {
		return handler->isTimingEnabled() ? currentInFrame : -1;
	}

	// The frame for sendAtFrame. Negative values collide with the -1 "no frame"
	// encoding, so they all mean "no frame".
	static int64_t frameAtFrame(double frame) {
		return frame < 0.0 ? -1 : int64_t(frame);
	}

	// Setter-argument rule shared by both engines: a number is rounded to the
	// nearest integer and clamped to [lo, hi], never wrapped. Clamping happens in
	// double before the integer conversion (an out-of-range float -> integer
	// cast is undefined behaviour); NaN maps to lo.
	// T is the result type (uint8_t/uint16_t for MIDI fields); lo and hi must fit it.
	template <typename T = int>
	static T clampInt(double v, int lo, int hi) {
		if (std::isnan(v)) return static_cast<T>(lo);
		return static_cast<T>(std::max(static_cast<double>(lo), std::min(static_cast<double>(hi), std::round(v))));
	}

	// The 14-bit CC value is 0..127.992 (MSB plus a fraction in 1/128 steps), so
	// it is clamped but not rounded.
	static double clampCc14bitValue(double v) {
		if (std::isnan(v)) return 0.0;
		return std::max(0.0, std::min(127.0 + 127.0 / 128.0, v));
	}

	void setWorker(std::shared_ptr<ITaskWorker> w) {
		taskWorker = std::move(w);
	}

	bool runAsync(std::function<void()> task) {
		return taskWorker->work(task, APP);
	}

	// Low-priority lane for queries the UI makes into the script (port names,
	// param labels). The worker is one FIFO shared by every MidiKit module, so
	// queueing these with runAsync() would put them in line ahead of later
	// MIDI/trigger dispatch and use up its few slots. Instead they wait here,
	// and process() runs them only behind dispatch work: one at the tail of
	// each dispatch task, or on their own when there is no dispatch to do. A
	// query therefore delays MIDI by at most one script call, and never the
	// other way round.
	//
	// UI thread only (single producer); drained on the worker (single
	// consumer). Returns false, dropping the task, if the lane is full —
	// callers retry on their next redraw.
	bool runLowPriority(std::function<void()> task) {
		if (uiQueryQueue.full()) return false;
		uiQueryQueue.push(std::move(task));
		return true;
	}

	// True on the thread script code runs on. All script execution happens there
	// — dispatch, load/teardown, setConfig()/getConfig() — so the interpreter and
	// contextMenus are only ever touched from it, and the call sites assert that.
	// Always true under SyncTaskWorker (tests), which runs tasks inline.
	bool onWorkerThread() const {
		return taskWorker && taskWorker->isWorkerThread();
	}

	// Parses the value of a "@requires" tag: space-separated key=value pairs,
	// currently only "params=N" (the script needs at least N panel params, e.g. 4
	// does not fit MIDI-µKIT's 2). `params` is 0 when the tag doesn't ask for any.
	// Returns false with `error` set for an unknown key or a malformed value: a
	// requirement that can't be checked can't be assumed met.
	static bool parseRequires(const std::string& value, int& params, std::string& error) {
		params = 0;
		std::istringstream ss(value);
		std::string token;
		while (ss >> token) {
			size_t eq = token.find('=');
			std::string key = token.substr(0, eq);
			std::string number = eq == std::string::npos ? "" : token.substr(eq + 1);
			if (key != "params") {
				error = string::f("unknown @requires key \"%s\" (supported: params)", key.c_str());
				return false;
			}
			char* end = nullptr;
			long n = std::strtol(number.c_str(), &end, 10);
			if (number.empty() || *end != '\0' || n < 0 || n > 1000) {
				error = string::f("invalid @requires value \"%s\"", token.c_str());
				return false;
			}
			params = static_cast<int>(n);
		}
		return true;
	}

	// The params a script's "@requires" tag asks for, 0 when absent or invalid.
	// Only for UI hints (greying out example scripts a variant can't run);
	// checkRequires() is what enforces the tag at load time.
	static int requiredParams(const std::string& script) {
		std::string header = script.substr(0, 2048);
		size_t tag = header.find("@requires");
		if (tag == std::string::npos) return 0;
		size_t start = tag + std::string("@requires").size();
		size_t eol = header.find('\n', start);
		int params;
		std::string error;
		return parseRequires(header.substr(start, eol == std::string::npos ? std::string::npos : eol - start), params, error) ? params : 0;
	}

	// Checks the optional "@requires" header tag against this module variant.
	// Logs why and returns false when the script must not load. Called by both
	// engines right after the "@engine" check.
	bool checkRequires(const std::map<std::string, std::string>& topics) {
		auto it = topics.find("requires");
		if (it == topics.end()) return true;
		int params;
		std::string error;
		if (!parseRequires(it->second, params, error)) {
			handler->writeLog("Script not loaded: " + error, false);
			return false;
		}
		if (params > paramCount) {
			handler->writeLog(string::f("Script not loaded: it requires %d params, this module has %d", params, paramCount), false);
			return false;
		}
		return true;
	}

	// True if this engine should process `script`: a simple "@engine <name>"
	// substring match, so the module needs no header parser of its own.
	virtual bool testScript(const std::string& script) = 0;


	// The load, always on the worker thread (ScriptHost::load() queues it), into
	// an unloaded engine: the caller ends any previous script with
	// unloadScriptOnWorker() first (implementations assert it).
	//
	// initialConfigJson, if non-empty, is parsed and installed on the engine as
	// the script's workingConfig BEFORE any script code runs — top-level code
	// and onLoad() see it via rack.getConfig(). Empty installs a fresh, empty
	// config (script switch). This is an install, not an argument to a hook:
	// getConfig()/setConfig() are live calls, not hooks. Implementations must
	// installConfig() from it at the top, before any script code runs.
	//
	// Load messages and parse errors reach the user via handler->writeLog().
	virtual void loadScriptOnWorker(const char* script, const std::string& initialConfigJson) = 0;

	// The end of a script, however it ends (replaced, cleared, reset, removed,
	// memory limit, load error), always on the worker thread. Implementations run
	// rack.onUnload() after handler->beginUnload() and free their interpreter if
	// a script is running, then always discardInQueues() and
	// handler->endUnload(). onUnload()'s return value is ignored: config comes
	// from rack.setConfig(), not from teardown. publishedConfig is deliberately
	// left untouched, so a save racing this teardown still persists the last
	// known config.
	virtual void unloadScriptOnWorker() = 0;

	// Main interface for message processing. Takes the decoded form so the
	// assembly the module already performed (NRPN/RPN/14-bit CC) travels with
	// the raw message instead of being redone on the worker.
	virtual void processInMessage(int midiPort, const QueuedMessage& msg) = 0;
	virtual void processInTick(int trigPort, uint8_t channel, int64_t frame = -1) = 0;

	// Decoded Tipsy messages awaiting dispatch. Engine-owned, like midiInQueue:
	// the decoding is the module's job but dispatching into script code is the
	// engine's. Pushed by the module's processTipsyInput() (audio thread),
	// drained by process() (worker) — the mirror image of the module's
	// tipsyOutQueue.
	dsp::RingBuffer<TipsyMessage, 8> tipsyInQueue;

	// Worker thread, after each dispatch pass: lets an engine refresh state the UI
	// reads through atomics (e.g. memory usage).
	virtual void publishMemoryUsage() {}

	// UI thread. Bytes in use by the loaded script's heap and the limit it is
	// held to, or false if no script is loaded. Reads only atomics.
	virtual bool getMemoryUsage(size_t& used, size_t& total) = 0;

	// Pending runLowPriority() tasks. See there.
	dsp::RingBuffer<std::function<void()>, 16> uiQueryQueue;
	// A stand-alone drain task is in the worker's queue. Keeps process(), which
	// runs every few samples, from flooding the shared worker while it waits.
	std::atomic<bool> uiDrainScheduled{false};

	// Worker thread. Runs at most one pending UI query, so the time MIDI can
	// wait behind this is a single script call.
	void drainUiQuery() {
		if (!uiQueryQueue.empty()) {
			std::function<void()> task = uiQueryQueue.shift();
			task();
		}
	}

	// Dispatches queued midiInQueue/tickInQueue/tipsyInQueue onto the engine via
	// runAsync(). Virtual so tests can override it to observe call counts.
	virtual void process() {
		if ((midiInQueue.size() > 0 || tickInQueue.size() > 0 || tipsyInQueue.size() > 0)) {
			runAsync([this]() {
				while (!midiInQueue.empty()) {
					auto t = midiInQueue.shift();
					int midiPort = std::get<0>(t);
					QueuedMessage q = std::get<1>(t);
					InFrameScope scope(currentInFrame, q.frame);
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
					InFrameScope scope(currentInFrame, std::get<2>(t));
					dispatchTrigger(std::get<0>(t), std::get<1>(t));
				}
				while (!tipsyInQueue.empty()) {
					TipsyMessage msg = tipsyInQueue.shift();
					InFrameScope scope(currentInFrame, msg.frame);
					dispatchTipsyMessage(msg);
				}
				// After everything above, so queries never hold up MIDI.
				drainUiQuery();
				publishMemoryUsage();
			});
		}
		else if (!uiQueryQueue.empty() && !uiDrainScheduled.exchange(true)) {
			bool queued = runAsync([this]() {
				uiDrainScheduled.store(false);
				drainUiQuery();
				publishMemoryUsage();
			});
			if (!queued) uiDrainScheduled.store(false);
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
				bool queued = se->runLowPriority([=] {
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
				bool queued = se->runLowPriority([=] {
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