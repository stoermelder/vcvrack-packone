#pragma once
#include "../../plugin.hpp"
#include "../../utils/SpscLatestValue.hpp"
#include "../../utils/TaskWorker.hpp"
#include "MidiScriptTypes.hpp"
#include "MidiScriptEngineHandler.hpp"
#include <atomic>
#include <cmath>
#include <jansson.h>
#include <map>
#include <memory>
#include <sstream>
#include <thread>
#include <tuple>

namespace StoermelderPackOne {
namespace MidiScript {

struct MidiScriptEngine {
	// Cap on setSysEx's payload (the handler's out-queue is fixed-size).
	static const int sysExMaxPayloadLength = 256;

	// Live message handles per callback (slot 0 is the incoming message):
	// msgStoreDefault, or more with "@requires messages=N" up to msgStoreMax.
	static const int msgStoreDefault = 32;
	static const int msgStoreMax = 512;

	// ── Message handles ──────────────────────────────────────────────────────
	// A script message is a handle into msgStore, shared by both engines.

	// One slot: the incoming message (slot 0 in a MIDI callback) or one the
	// script builds for output.
	struct ScriptMessage {
		// The message plus its decode result (NRPN/RPN/14-bit CC). The same struct
		// the module filled in, so midi.getControl()/getValue() need no copy. For a
		// built message the decode fields stay at their defaults.
		QueuedMessage in;
		// Chain markers of a message built for SEND (createNRPN()/createCc14bit()),
		// unlike in.type, which says how a received message was decoded.
		bool isNrpn = false;
		bool isRpn = false;    // with isNrpn: the 4-message chain is an RPN (CC 101/100)
		bool isCc14bit = false;
	};

	// Worker thread only. Sized by sizeStore() at load and never resized while
	// bindings run, so no pointer into it outlives a resize.
	std::vector<ScriptMessage> msgStore = std::vector<ScriptMessage>(msgStoreDefault);

	// Sets the store to max(requested, msgStoreDefault) empty slots. Worker, in an
	// unloaded engine, before the script's top-level code. Shrinks memory, so one
	// big script doesn't pin it for the next.
	void sizeStore(int requested) {
		// By value: std::max would take the constants by reference, which needs
		// an out-of-class definition in C++11.
		int minimum = msgStoreDefault;
		size_t n = size_t(requested > minimum ? requested : minimum);
		bool shrink = n < msgStore.size();
		msgStore.assign(n, ScriptMessage());
		if (shrink) msgStore.shrink_to_fit();
		msgCount = 0;
	}

	// Writes the "message store full" error into `buf`, naming the fix. A plain
	// buffer because the Lua bindings raise it with luaL_error, which longjmps
	// past destructors.
	void storeFullMessage(char* buf, size_t size, const char* fn) const {
		snprintf(buf, size, "%s: message store full (%d handles; reuse a handle or raise it with @requires messages=N)", fn, int(msgStore.size()));
	}

	// Slots in use in the current callback. Initialised because top-level script
	// code runs before any callback starts the store.
	size_t msgCount = 0;
	// True only inside a script callback: a handle is valid only in the callback
	// that created it.
	bool inCallback = false;
	// Added to the slot index to make a handle. Advanced at every callback start,
	// so a handle is never issued twice and a stale one can't alias a new message.
	int64_t handleBase = 0;
	// Output port from midiOut.selectPort(), 0-based; sticky across callbacks.
	int selectedPort = 0;

	// Start of a callback: an empty store (or slot 0 taken by the incoming
	// message, used = 1) and fresh handle numbers. Callbacks never nest.
	void beginStore(size_t used) {
		handleBase += int64_t(msgCount);
		msgCount = used;
	}

	// The handle for store slot `slot` in the current callback.
	int64_t slotToHandle(size_t slot) const {
		return handleBase + int64_t(slot);
	}

	// Handle -> store slot, or -1 if it is not a live handle of this callback.
	long handleToSlot(int64_t h) const {
		if (!inCallback || h < handleBase) return -1;
		int64_t slot = h - handleBase;
		return slot < int64_t(msgCount) ? long(slot) : -1;
	}

	// Messages a handle sends as one group: 4 for an NRPN/RPN, 2 for a 14-bit CC
	// pair, 1 for anything else. Only meaningful on the lead slot.
	static size_t groupSize(const ScriptMessage& s) {
		return s.isNrpn ? 4 : s.isCc14bit ? 2 : 1;
	}

	// The error tail for a single-message setter called on a group handle, or
	// nullptr for a plain handle. Such a setter would write only the lead slot
	// and leave a broken group, so the bindings raise this instead, before any
	// write (the Lua ones longjmp, see sendEntry()).
	static const char* groupSetterError(const ScriptMessage& s) {
		if (s.isNrpn) return s.isRpn ? "message is an RPN; use midi.setRPN()" : "message is an NRPN; use midi.setNRPN()";
		if (s.isCc14bit) return "message is a 14-bit CC; use midi.setCc14bit()";
		return nullptr;
	}

	// midi.clone(): appends a copy of the handle at slot `src` to the store, a whole
	// group (with its chain flags) for a group handle, and returns the new slot.
	// Only the MIDI payload is copied: the clone starts unsent, and a received
	// message's decode result is not carried over. The caller has checked that
	// groupSize() slots are free.
	size_t cloneGroup(size_t src) {
		size_t n = groupSize(msgStore[src]);
		size_t dst = msgCount;
		for (size_t k = 0; k < n; k++) {
			ScriptMessage copy;
			copy.in.msg = msgStore[src + k].in.msg;
			msgStore[dst + k] = copy;
		}
		msgStore[dst].isNrpn = msgStore[src].isNrpn;
		msgStore[dst].isRpn = msgStore[src].isRpn;
		msgStore[dst].isCc14bit = msgStore[src].isCc14bit;
		msgCount += n;
		return dst;
	}

	// midi.setChannel() on a handle: every message of its group, so the group
	// stays on one channel. `slot` from handleToSlot().
	void setGroupChannel(size_t slot, uint8_t channel) {
		size_t n = groupSize(msgStore[slot]);
		for (size_t k = 0; k < n; k++) msgStore[slot + k].in.msg.setChannel(channel);
	}

	// Worker, from a midiOut.send*() binding: sends `first` to `port` on `frame`,
	// with the 3 entries after it for an NRPN/RPN chain or the 1 after it for a
	// 14-bit CC pair, as one group. Works on copies, so the handle can be changed
	// and sent again. `first` must come from handleToSlot().
	//
	// Never raises: the Lua bindings rely on that (luaL_error longjmps past C++
	// destructors), so every check comes before this call.
	//
	// `scheduled`: sent by sendAfterMs/sendAtFrame/sendAfterTrigger, so
	// midiOut.cancel() may drop it. A plain send() never is.
	void sendEntry(const ScriptMessage& first, int port, int64_t frame, uint8_t channel = 0, uint64_t tick = 0, int trigPort = 0, bool scheduled = false) {
		size_t n = groupSize(first);
		Message group[4];
		for (size_t k = 0; k < n; k++) {
			group[k] = (&first)[k].in.msg;
			group[k].frame = frame;
		}
		OutTag tag;
		tag.scheduled = scheduled;
		if (first.isNrpn || first.isCc14bit) tag.group = groupOf(first);
		// A drop is expected under output saturation; the module reports it.
		handler->sendMidi(port, group, n, channel, tick, trigPort, tag);
	}

	// The group a chain handle sends as: channel and parameter number from the
	// CC 99/98 (101/100) pair of an NRPN/RPN, channel and MSB controller of a
	// 14-bit pair. NONE for a single message, or a chain whose setter never ran.
	static OutGroup groupOf(const ScriptMessage& first) {
		OutGroup g;
		if (!(first.isNrpn || first.isCc14bit) || !isCancelPattern(first)) return g;
		const Message& lead = first.in.msg;
		g.channel = lead.bytes[0] & 0x0F;
		if (first.isNrpn) {
			const Message& second = (&first)[1].in.msg;
			if (lead.bytes.size() < 3 || second.bytes.size() < 3) return g;
			g.kind = first.isRpn ? OutGroup::RPN : OutGroup::NRPN;
			g.param = uint16_t(((lead.bytes[2] & 0x7f) << 7) | (second.bytes[2] & 0x7f));
		}
		else {
			if (lead.bytes.size() < 2) return g;
			g.kind = OutGroup::CC14;
			g.param = lead.bytes[1];
		}
		return g;
	}

	// Whether `first` can be a midiOut.cancel() pattern: it has a status byte (a
	// chain: its setter ran). Checked by the bindings before cancelEntry().
	static bool isCancelPattern(const ScriptMessage& first) {
		const Message& m = first.in.msg;
		return !m.bytes.empty() && m.bytes[0] >= 0x80;
	}

	// Worker, from midiOut.cancel(): `first` null cancels everything on the
	// selected port. Never raises (see sendEntry()).
	void cancelEntry(const ScriptMessage* first) {
		CancelMode mode = CancelMode::ALL;
		Message pattern;
		OutGroup group;
		if (first != nullptr) {
			if (first->isNrpn || first->isCc14bit) {
				group = groupOf(*first);
				// An unset chain would match every plain message as NONE.
				if (group.kind == OutGroup::NONE) return;
				mode = CancelMode::GROUP;
			}
			else {
				pattern = first->in.msg;
				mode = CancelMode::MESSAGE;
			}
		}
		// A full queue is normal under saturation; the module reports it.
		handler->cancelMidi(selectedPort, mode, pattern, group);
	}

	// The module this engine runs in, injected at construction.
	MidiScriptEngineHandler* handler;

	// rack.random(): a generator of the engine's own, not Rack's global one, so a
	// module's sequence depends only on its stored seed and the script. The host
	// reseeds it at every load (worker thread), which makes a reload replay the
	// same values.
	void seedRandom(uint32_t seed) {
		// A fixed second word: with (seed, 0) a seed of 0 would leave the all-zero state.
		rng.seed(seed, 0x9E3779B97F4A7C15ull);
		// Low-entropy seeds (small integers) need a few shifts to spread.
		for (int i = 0; i < 4; i++) rng();
	}

	// rack.setRandomSeed(n): the script's own seed, taking effect at once. Any finite
	// number is accepted: truncated, then wrapped into 32 bits (negative too), so a
	// script can feed it a clock or a counter. False for NaN/infinity. Only the
	// running generator changes, not the seed stored in the patch, which the next
	// load restores: a script that wants its own seed calls this in onLoad.
	bool setRandomSeedFromNumber(double n) {
		if (!std::isfinite(n)) return false;
		double wrapped = std::fmod(std::trunc(n), 4294967296.0);
		if (wrapped < 0) wrapped += 4294967296.0;
		seedRandom(static_cast<uint32_t>(wrapped));
		return true;
	}

	// Uniform in [0, 1). Built from the raw 53 top bits instead of a std
	// distribution, whose output differs between standard libraries: a patch
	// must get the same values on every platform.
	double nextRandom() {
		return static_cast<double>(rng() >> 11) * (1.0 / 9007199254740992.0);
	}

	// rack.setConfig()/getConfig() limits, shared so the engines can't drift.
	// Depth 1 is the value itself; the cap also ends cyclic tables/objects, as the
	// converters track no visited nodes.
	static const int configMaxDepth = 4;
	// Serialized size of the whole prospective config, not the single value.
	static const size_t configMaxBytes = 65536;

	// Wraps an owned json_t* in a shared_ptr that decrefs it.
	static std::shared_ptr<json_t> ownJson(json_t* j) {
		return std::shared_ptr<json_t>(j, [](json_t* p) { json_decref(p); });
	}

	// The script's config (a flat object, values may nest). Script thread only,
	// so no synchronization. Never null.
	std::shared_ptr<json_t> workingConfig = ownJson(json_object());

	// Config published to the UI thread. The published json_t is IMMUTABLE:
	// setConfig() publishes a fresh copy, so a reader can walk its version while
	// the script writes the next (jansson containers are not thread-safe).
	// Single writer, single reader (dataToJson()); getConfig() reads the working
	// copy instead, since SpscLatestValue allows one reader.
	SpscLatestValue<std::shared_ptr<json_t>> publishedConfig{ownJson(json_object())};

	// Script thread. Mutates the working copy, then publishes a copy. `value` is
	// owned; null deletes the key. A shallow json_copy() suffices: child values are
	// only ever replaced wholesale, never mutated.
	void setConfigValue(const char* key, json_t* value /* owned, may be null */) {
		assert(onWorkerThread());
		if (!value) json_object_del(workingConfig.get(), key);
		else        json_object_set_new(workingConfig.get(), key, value);
		std::shared_ptr<json_t> copy = ownJson(json_copy(workingConfig.get()));
		publishedConfig.store(std::move(copy));
	}

	// Script thread. A borrowed pointer into workingConfig, NULL if `key` is unset.
	json_t* getConfigValue(const char* key) const {
		assert(onWorkerThread());
		return json_object_get(workingConfig.get(), key);
	}

	// Replaces workingConfig (patch load, script switch, reset) and publishes it
	// at once, so dataToJson() never sees a stale one. `initial` is owned; null
	// starts empty.
	void installConfig(json_t* initial /* owned, may be null */) {
		assert(onWorkerThread());
		workingConfig = ownJson(initial ? initial : json_object());
		std::shared_ptr<json_t> copy = ownJson(json_copy(workingConfig.get()));
		publishedConfig.store(std::move(copy));
	}

	// UI thread. A reference into the slot, stable until the next call on this
	// (the only) reader thread.
	const std::shared_ptr<json_t>& peekConfig() {
		return publishedConfig.peek();
	}

	// Validates a config key: [A-Za-z_][A-Za-z0-9_]{0,63}. '.' is rejected to keep
	// it free for a future path form.
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

	rack::random::Xoroshiro128Plus rng;

	MidiScriptEngine(MidiScriptEngineHandler* handler, int inputCount, int inputTrigCount, int outputTrigCount, int paramCount, int midiInputCount, int midiOutputCount)
		: handler(handler), inputCount(inputCount), inputTrigCount(inputTrigCount), outputTrigCount(outputTrigCount), paramCount(paramCount), midiInputCount(midiInputCount), midiOutputCount(midiOutputCount) {
		seedRandom(1);   // engines driven without a host (tests) still get a defined sequence
	}

	// Not owned: the host owns the domain and outlives its engines' use of it,
	// so the worker is destroyed (and drains its queue) while the engines are
	// still alive. Null until the host injects one. The engine joins the domain's
	// bus only when its script defines rack.onBroadcast. The host removes the
	// engine from the bus before the engine is destroyed.
	WorkerDomain* domain = nullptr;
	MidiInRingBuffer midiInQueue;
	// (trigPort, channel, frame). Sized for the worst case between two drains
	// (every 8th sample): 2 ports x 16 channels firing on each of 8 samples.
	dsp::RingBuffer<std::tuple<int, uint8_t, int64_t>, 256> tickInQueue;

	// Audio thread. dsp::RingBuffer::push() does not bounds-check: a full buffer
	// is overwritten and empty()/full() then misreport. Drops the new event.
	template <typename Q, typename T>
	void pushInQueue(Q& queue, T&& value) {
		if (!queue.full()) queue.push(std::forward<T>(value));
	}

	// Worker thread, from unloadScriptOnWorker(): events captured for the closed
	// script must not reach the next one.
	void discardInQueues() {
		while (!midiInQueue.empty()) midiInQueue.pop();
		while (!tickInQueue.empty()) tickInQueue.shift();
		while (!tipsyInQueue.empty()) tipsyInQueue.shift();
		// Reset the slots too, so the queue does not pin stale payloads.
		for (InboundBroadcast& m : broadcastInQueue.data) m = InboundBroadcast();
		broadcastInQueue.clear();
		broadcastOverflowLogged = false;
	}

	// Worker thread: the frame of the event being dispatched, -1 outside one.
	int64_t currentInFrame = -1;
	// Sets currentInFrame for a scope and restores it after.
	struct InFrameScope {
		int64_t& slot;
		int64_t prev;
		InFrameScope(int64_t& slot, int64_t frame) : slot(slot), prev(slot) { slot = frame; }
		~InFrameScope() { slot = prev; }
	};

	// The frame for sendAfterMs: `ms` after the causing event in timing mode,
	// otherwise after the module's latest process() frame. A negative `ms` means
	// "behind Rack's output queue": framed messages handed over late in a block go
	// out up to two blocks later, so this lands two blocks and a frame on.
	int64_t frameAfterMs(double ms) const {
		int64_t base = handler->isTimingEnabled() && currentInFrame >= 0 ? currentInFrame : handler->getTimingCurrentFrame();
		if (ms < 0.0) return base + 2 * handler->getTimingBlockFrames() + 1;
		float sr = handler->getSampleRate();
		return base + int64_t(sr > 0.f ? ms / 1000.0 * sr : 0.0);
	}

	// rack.msToFrames() / framesToMs() at the current sample rate (read per call,
	// it can change). Frames are rounded. NaN and an unknown rate give 0.
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

	// The stamp for a trigger output write. In timing mode, inside an event: the
	// event's frame plus one block, the offset Rack puts on framed MIDI, so a
	// trigger stays with its MIDI. Otherwise -1 (apply when the script runs).
	int64_t frameForTrig() const {
		if (!handler->isTimingEnabled() || currentInFrame < 0) return -1;
		return currentInFrame + handler->getTimingBlockFrames();
	}

	// The frame for send: the causing event's in timing mode, otherwise -1.
	int64_t frameForSend() const {
		return handler->isTimingEnabled() ? currentInFrame : -1;
	}

	// The frame for sendAtFrame. Negatives collide with the -1 "no frame".
	static int64_t frameAtFrame(double frame) {
		return frame < 0.0 ? -1 : int64_t(frame);
	}

	// Setter-argument rule: round to nearest and clamp to [lo, hi], never wrap.
	// Clamps in double first (an out-of-range float -> integer cast is undefined);
	// NaN gives lo. `T` is the result type; lo and hi must fit it.
	template <typename T = int>
	static T clampInt(double v, int lo, int hi) {
		if (std::isnan(v)) return static_cast<T>(lo);
		return static_cast<T>(std::max(static_cast<double>(lo), std::min(static_cast<double>(hi), std::round(v))));
	}

	// A 14-bit CC value is 0..127.992 (1/128 steps): clamped, not rounded.
	static double clampCc14bitValue(double v) {
		if (std::isnan(v)) return 0.0;
		return std::max(0.0, std::min(127.0 + 127.0 / 128.0, v));
	}

	void setDomain(WorkerDomain* d) {
		domain = d;
	}

	bool runAsync(std::function<void()> task) {
		return domain->worker->work(task, APP);
	}

	// Low-priority lane for UI queries into the script (port names, param labels).
	// They must not use runAsync(): the worker is one FIFO shared by every MidiKit
	// module and its few slots belong to MIDI/trigger dispatch. process() runs
	// them behind dispatch work, so a query delays MIDI by at most one script call.
	//
	// UI thread only (single producer). False, dropping the task, if the lane is
	// full; callers retry on their next redraw.
	bool runLowPriority(std::function<void()> task) {
		if (uiQueryQueue.full()) return false;
		uiQueryQueue.push(std::move(task));
		return true;
	}

	// True on the thread script code runs on (dispatch, load/teardown,
	// setConfig()/getConfig()). Always true under SyncTaskWorker (tests).
	bool onWorkerThread() const {
		return domain && domain->worker->isWorkerThread();
	}

	// What a script's "@requires" tag asks for; 0 for a key it doesn't have.
	struct Requires {
		// At least N panel params (4 does not fit MIDI-µKIT's 2).
		int params = 0;
		// A message store of at least N slots (a minimum). Checked against
		// msgStoreMax in checkRequires(), so the error can name the limit.
		int messages = 0;
	};

	// Parses a "@requires" value: space-separated "params=N" and "messages=N".
	// False with `error` set for an unknown key or malformed value.
	static bool parseRequires(const std::string& value, Requires& req, std::string& error) {
		req = Requires();
		std::istringstream ss(value);
		std::string token;
		while (ss >> token) {
			size_t eq = token.find('=');
			std::string key = token.substr(0, eq);
			std::string number = eq == std::string::npos ? "" : token.substr(eq + 1);
			if (key != "params" && key != "messages") {
				error = string::f("unknown @requires key \"%s\" (supported: params, messages)", key.c_str());
				return false;
			}
			char* end = nullptr;
			long n = std::strtol(number.c_str(), &end, 10);
			long limit = key == "params" ? 1000 : 1000000;
			if (number.empty() || *end != '\0' || n < 0 || n > limit) {
				error = string::f("invalid @requires value \"%s\"", token.c_str());
				return false;
			}
			(key == "params" ? req.params : req.messages) = static_cast<int>(n);
		}
		return true;
	}

	// The params "@requires" asks for, 0 when absent or invalid. A UI hint only
	// (greys out example scripts a variant can't run); checkRequires() enforces.
	static int requiredParams(const std::string& script) {
		std::string header = script.substr(0, 2048);
		size_t tag = header.find("@requires");
		if (tag == std::string::npos) return 0;
		size_t start = tag + std::string("@requires").size();
		size_t eol = header.find('\n', start);
		Requires req;
		std::string error;
		return parseRequires(header.substr(start, eol == std::string::npos ? std::string::npos : eol - start), req, error) ? req.params : 0;
	}

	// Checks "@requires" against this variant and sizes the message store. Logs
	// why and returns false if the script must not load. Called by both engines
	// after the "@engine" check; the store is still at its default, so a refused
	// script leaves no big one behind.
	bool checkRequires(const std::map<std::string, std::string>& topics) {
		auto it = topics.find("requires");
		if (it == topics.end()) return true;
		Requires req;
		std::string error;
		if (!parseRequires(it->second, req, error)) {
			handler->writeLog("Script not loaded: " + error, false);
			return false;
		}
		if (req.params > paramCount) {
			handler->writeLog(string::f("Script not loaded: it requires %d params, this module has %d", req.params, paramCount), false);
			return false;
		}
		if (req.messages > msgStoreMax) {
			handler->writeLog(string::f("Script not loaded: @requires messages=%d exceeds the maximum of %d", req.messages, msgStoreMax), false);
			return false;
		}
		sizeStore(req.messages);
		return true;
	}

	// True if this engine should process `script` ("@engine <name>" substring).
	virtual bool testScript(const std::string& script) = 0;


	// The load, on the worker thread, into an unloaded engine (the caller ran
	// unloadScriptOnWorker() first; implementations assert it).
	//
	// A non-empty initialConfigJson is installed as workingConfig BEFORE any script
	// code runs, so top-level code and onLoad() see it via rack.getConfig(). Empty
	// installs a fresh config. Implementations installConfig() at the top.
	//
	// Load messages and parse errors go to handler->writeLog().
	virtual void loadScriptOnWorker(const char* script, const std::string& initialConfigJson) = 0;

	// The end of a script however it ends (replaced, cleared, reset, removed,
	// memory limit, load error), on the worker thread. Implementations run
	// rack.onUnload() after handler->beginUnload(), free the interpreter if a
	// script is running, then always discardInQueues() and handler->endUnload().
	// onUnload()'s return value is ignored. publishedConfig is left untouched, so a
	// racing save still persists the last known config.
	virtual void unloadScriptOnWorker() = 0;

	// Takes the decoded form, so the NRPN/RPN/14-bit CC assembly the module
	// already did travels with the message.
	virtual void processInMessage(int midiPort, const QueuedMessage& msg) = 0;
	virtual void processInTick(int trigPort, uint8_t channel, int64_t frame = -1) = 0;

	// Decoded Tipsy messages awaiting dispatch. Pushed by the module's
	// processTipsyInput() (audio thread), drained by process() (worker).
	dsp::RingBuffer<TipsyMessage, 8> tipsyInQueue;

	// Broadcasts from other engines, pushed and drained on the worker thread (see
	// BroadcastBus). The audio thread only reads size().
	static const size_t broadcastInQueueSize = 16;
	dsp::RingBuffer<InboundBroadcast, broadcastInQueueSize> broadcastInQueue;

	// Worker thread, from BasicBroadcastBus::send(): a broadcast for this engine was
	// dropped, its queue is full. Logs once per episode, which ends when the queue
	// is next drained.
	virtual void onBroadcastDropped() {
		if (broadcastOverflowLogged) return;
		broadcastOverflowLogged = true;
		if (handler) handler->writeLog("Broadcast input queue full, message(s) dropped");
	}
	bool broadcastOverflowLogged = false;

	// Worker thread, from the sendBroadcast bindings. Takes ownership of `owned`
	// (a converted script value) and sends it, with the optional `topic`, to every
	// other receiver. Returns how many it reached; 0 and a log line if the value
	// exceeds broadcastMaxBytes or the topic broadcastTopicMaxBytes.
	// Never raises: the Lua bindings must not luaL_error after building C++ objects.
	int sendBroadcast(json_t* owned, const std::string* topic = nullptr) {
		std::shared_ptr<json_t> value = ownJson(owned);
		if (topic && topic->size() > broadcastTopicMaxBytes) {
			handler->writeLog(string::f("sendBroadcast: topic exceeds %d bytes (ignored)", (int)broadcastTopicMaxBytes));
			return 0;
		}
		char* dump = json_dumps(value.get(), JSON_COMPACT);
		size_t size = dump ? strlen(dump) : 0;
		if (dump) free(dump);
		if (size > broadcastMaxBytes) {
			handler->writeLog(string::f("sendBroadcast: message exceeds the %d KB limit (ignored)", (int)(broadcastMaxBytes / 1024)));
			return 0;
		}
		if (!domain) return 0;
		return domain->bus->send(this, value, currentInFrame, topic);
	}

	// Worker thread, after each dispatch pass: lets an engine refresh state the UI
	// reads through atomics (e.g. memory usage).
	virtual void publishMemoryUsage() {}

	// UI thread. Bytes used by the script's heap and its limit, or false if no
	// script is loaded. Reads only atomics.
	virtual bool getMemoryUsage(size_t& used, size_t& total) = 0;

	// Pending runLowPriority() tasks.
	dsp::RingBuffer<std::function<void()>, 16> uiQueryQueue;
	// A stand-alone drain task is queued; keeps process() from flooding the worker.
	std::atomic<bool> uiDrainScheduled{false};

	// Worker thread. Runs at most one UI query, so MIDI waits behind a single
	// script call.
	void drainUiQuery() {
		if (!uiQueryQueue.empty()) {
			std::function<void()> task = uiQueryQueue.shift();
			task();
		}
	}

	// Dispatches the queued input onto the engine via runAsync(). Virtual so tests
	// can observe call counts.
	virtual void process() {
		if ((midiInQueue.size() > 0 || tickInQueue.size() > 0 || tipsyInQueue.size() > 0 || broadcastInQueue.size() > 0)) {
			runAsync([this]() {
				while (!midiInQueue.empty()) {
					auto t = midiInQueue.shift();
					int midiPort = t.port;
					QueuedMessage q = std::move(t.msg);
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
				// Only what was queued before this pass: a reply from onBroadcast is
				// answered on the next pass, so two replying scripts ping-pong
				// instead of spinning the shared worker.
				size_t n = broadcastInQueue.size();
				while (n-- > 0 && !broadcastInQueue.empty()) {
					// Moved out, so the slot drops its reference right away.
					InboundBroadcast& slot = broadcastInQueue.data[broadcastInQueue.start % broadcastInQueueSize];
					InboundBroadcast msg = std::move(slot);
					slot = InboundBroadcast();
					broadcastInQueue.start++;
					InFrameScope scope(currentInFrame, msg.frame);
					dispatchBroadcast(msg);
				}
				broadcastOverflowLogged = false;
				// Last, so queries never hold up MIDI.
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

	// Engine-specific dispatch, from process() on the worker thread.
	virtual void dispatchMidiMessage(int midiPort, Message& msg) = 0;

	// An assembled parameter change (midi.onNrpn/onRpn) or 14-bit controller
	// change (midi.onCc14bit). The whole QueuedMessage is passed because the script
	// gets it as a message handle, like onMessage, so the raw bytes stay reachable.
	virtual void dispatchNrpn(int midiPort, const QueuedMessage& q, bool isRpn) = 0;
	virtual void dispatchCc14bit(int midiPort, const QueuedMessage& q) = 0;
	virtual void dispatchTrigger(int trigPort, uint8_t channel) = 0;
	virtual void dispatchTipsyMessage(const TipsyMessage& msg) = 0;
	// A broadcast from another engine. The value is shared with the other
	// receivers and must not be modified.
	virtual void dispatchBroadcast(const InboundBroadcast& msg) = 0;

	// Queries into the script from the UI
	virtual std::string getInputName(int i) = 0;
	virtual std::string getParamName(int i) = 0;
	virtual std::string getParamFormatValue(int i) = 0;

	// UI thread, while the context menu is built. Evaluates each item's onGetValue
	// on the WORKER thread, then calls `callback`, which must not construct
	// widgets: it only publishes specs for the menu to poll from step().
	virtual void getContextMenus(const std::function<void(const std::vector<ScriptMenuItem>&)>& callback) = 0;
	// UI thread, on a menu click. Runs the callback on the worker thread; the click's
	// content is described at menuCallArgs(). A click that does not fit the item is ignored.
	virtual void invokeContextMenuCallback(int callbackId, const ScriptMenuClick& click) = 0;
};

} // namespace MidiScript
} // namespace StoermelderPackOne