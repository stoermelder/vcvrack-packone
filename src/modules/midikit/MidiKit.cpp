#include "MidiScriptEngine.hpp"
#include "MidiScriptEngineLua.hpp"
#include "MidiScriptEngineQuickJs.hpp"
#include "../../components/Knobs.hpp"
#include "../../components/MidiWidget.hpp"
#include "../../components/LedTextField.hpp"
#include "../../ui/OverlayMessageWidget.hpp"
#include "../../utils/MpmcTaskWorker.hpp"
#include "../midi/MidiProcessor.hpp"
#include "tipsy-encoder/include/tipsy/tipsy.h"
#include <osdialog.h>
#include <fstream>
#include <queue>
#include <atomic>

namespace StoermelderPackOne {
namespace MidiKit {


enum class LOG_FORMAT {
	RESET,
	TIMESTAMP,
	INDENTED,
	TEXT
};

struct MidiOutput : midi::Output {
	struct FrameSchedule {
		midi::Message msg;
		bool operator<(const FrameSchedule& other) const {
			return msg.frame > other.msg.frame;
		}
	};
	
	struct TickSchedule {
		midi::Message msg;
		uint64_t tick;
		bool operator<(const TickSchedule& other) const {
			return tick > other.tick;
		}
	};

	std::priority_queue<FrameSchedule> frameQueue;
	// One tick queue per polyphonic channel: sendAfterTrigger() schedules a
	// message against a specific channel's trigger clock, and only that
	// channel's clock advancing can flush it.
	std::priority_queue<TickSchedule> tickQueue[PORT_MAX_CHANNELS];

	std::vector<int> getChannels() override {
		std::vector<int> channels;
		for (int c = -1; c < 16; c++) {
			channels.push_back(c);
		}
		return channels;
	}

	void reset() {
		Output::reset();
		while (!frameQueue.empty()) frameQueue.pop();
		for (int i = 0; i < PORT_MAX_CHANNELS; i++) {
			while (!tickQueue[i].empty()) tickQueue[i].pop();
		}
		channel = -1;
	}

	void send(midi::Message& msg, uint8_t channel, uint64_t tick) {
		if (tick != 0) {
			TickSchedule s;
			s.msg = msg;
			s.tick = tick;
			tickQueue[channel < PORT_MAX_CHANNELS ? channel : 0].push(s);
			return;
		}

		if (msg.frame != -1) {
			FrameSchedule s;
			s.msg = msg;
			frameQueue.push(s);
			return;
		}

		sendMessage(msg);
	}

	void processFrame(int64_t frame) {
		while (true) {
			if (frameQueue.size() == 0) return;
			FrameSchedule s = frameQueue.top();
			// ">=" and not ">": s.msg.frame is the engine frame the message is
			// intended to be processed at (midi.hpp). With ">" a message due
			// exactly at the current frame is deferred to the next processFrame()
			// call — one divider period later. Mirrors the processTick() fix.
			if (frame >= s.msg.frame) {
				frameQueue.pop();
				s.msg.frame = -1;
				sendMessage(s.msg);
			}
			else {
				return;
			}
		}
	}

	void processTick(uint8_t channel, uint64_t tick) {
		// Each channel's messages are only ever drained by that channel's own
		// clock — a message scheduled against channel N must not fire on
		// another channel's trigger, so its queue is touched only when N fires.
		auto& q = tickQueue[channel < PORT_MAX_CHANNELS ? channel : 0];
		while (true) {
			if (q.size() == 0) return;
			TickSchedule s = q.top();
			// ">=" and not "==": process() calls processTick() before draining the
			// engine's out-queue, so a script can schedule for a tick the counter has
			// already consumed. With "==" such a message is never sent and, since the
			// queue is ordered smallest-tick-first, it blocks every later one behind it.
			if (tick >= s.tick) {
				q.pop();
				sendMessage(s.msg);
			}
			else {
				return;
			}
		}
	}
};


// ── Extended-CC input enables, one per module ──────────────────────────────
// Per-MIDI-channel bitmasks of what the script asked to have assembled
// (midi.enableNrpnIn/enableRpnIn/enableCc14bitIn). Bit c = MIDI channel c;
// "all channels" sets every bit. Nothing is assembled for the script until it
// asks, matching trig.enableIn()/trig.enableTipsyIn().
//
// Threading: the masks are atomic because the worker thread writes them from
// the enable bindings while the audio thread reads them in processMidi().
struct ExtendedCcEnables {
	std::atomic<uint16_t> nrpnEnabledMask{0};
	std::atomic<uint16_t> rpnEnabledMask{0};
	// One mask per 14-bit MSB controller (0-31), since registration is per-CC:
	// a script can take CC 7 as 14-bit while still seeing CC 39 raw.
	std::atomic<uint16_t> cc14bitEnabledMask[32];

	// Expands a script-supplied channel (0-based, or -1 for all) into a mask.
	// Returns 0 for an out-of-range channel, so the caller enables nothing.
	static uint16_t channelBits(int channel) {
		if (channel < 0) return 0xffff;
		if (channel >= 16) return 0;
		return static_cast<uint16_t>(1) << channel;
	}

	// Worker side — midi.enableNrpnIn()/enableRpnIn() binding. channel is
	// 0-based, or -1 for all; kind 1 = RPN, otherwise NRPN.
	void enableNrpn(int kind, int channel) {
		uint16_t bits = channelBits(channel);
		if (bits == 0) return;
		(kind == 1 ? rpnEnabledMask : nrpnEnabledMask).fetch_or(bits, std::memory_order_relaxed);
	}

	// Worker side — midi.enableCc14bitIn() binding. cc is the 0-31 MSB
	// controller, or -1 for all of them.
	void enableCc14bit(int cc, int channel) {
		if (cc >= 32) return;
		uint16_t bits = channelBits(channel);
		if (bits == 0) return;
		if (cc < 0) {
			for (int i = 0; i < 32; i++) cc14bitEnabledMask[i].fetch_or(bits, std::memory_order_relaxed);
		}
		else {
			cc14bitEnabledMask[cc].fetch_or(bits, std::memory_order_relaxed);
		}
	}

	// Whether the script asked for assembled events of each kind on this MIDI
	// channel. Audio thread.
	bool isNrpnEnabled(uint8_t ch, bool isRpn) const {
		if (ch >= 16) return false;
		auto& mask = isRpn ? rpnEnabledMask : nrpnEnabledMask;
		return (mask.load(std::memory_order_relaxed) >> ch) & 1;
	}
	bool isCc14bitEnabled(uint8_t ch, uint8_t cc) const {
		if (ch >= 16 || cc >= 32) return false;
		return (cc14bitEnabledMask[cc].load(std::memory_order_relaxed) >> ch) & 1;
	}

	// Whether a raw CC that MidiProcessor flagged as a component should be
	// withheld from midi.onMessage — true only if the script enabled the kind of
	// assembly this CC feeds. Audio thread.
	//
	// The controller ranges overlap and that is deliberate (see the plan's §4.1):
	// CC 0-31 are 14-bit MSBs, and CC 6/38 are simultaneously Data Entry for an
	// armed RPN/NRPN parameter. Both readings are honoured, so a script enabling
	// either kind stops seeing the CCs that feed it; a script enabling blanket
	// 14-bit therefore also consumes CC 6/38, which is why registration is
	// per-CC — that is the escape hatch for scripts wanting them raw.
	bool isComponentEnabled(const MessageEx& m) const {
		uint8_t ch = m.getChannel();
		uint8_t cc = m.getNote();

		// Parameter select belongs to whichever kind it selects.
		if (cc == 99 || cc == 98) return isNrpnEnabled(ch, false);
		if (cc == 101 || cc == 100) return isNrpnEnabled(ch, true);

		// Data entry for an armed parameter. MidiProcessor only flags 6/38 as
		// components while one is armed, so reaching here means it was.
		if (cc == 6 || cc == 38) {
			if (isNrpnEnabled(ch, false) || isNrpnEnabled(ch, true)) return true;
			// Fall through: 6/38 are also a 14-bit pair by the spec's numbering.
		}

		if (cc < 32) return isCc14bitEnabled(ch, cc);
		if (cc < 64) return isCc14bitEnabled(ch, cc - 32);
		return false;
	}

	// Forgets every enabled extended-CC kind. Called on script load/reset, like
	// the trigger enables: the enables belong to the script, not the module.
	void clear() {
		nrpnEnabledMask.store(0, std::memory_order_relaxed);
		rpnEnabledMask.store(0, std::memory_order_relaxed);
		for (int i = 0; i < 32; i++) cc14bitEnabledMask[i].store(0, std::memory_order_relaxed);
	}
};


// ── Tipsy protocol over the trigger CV, one per module ─────────────────────
// Encodes queued messages onto the trigger output and decodes the input back
// into completed messages. The audio thread drives both directions; the worker
// enqueues outbound (send/sendReset) and consumes inbound via the active
// engine. Owns every Tipsy member, so the only boundaries to the rest of the
// module are the voltages (processInput/processOutput) and the log flags
// (takeOverflow/takeError).
struct TipsyPort {
	using TipsyMessage = MidiScript::TipsyMessage;

	// What processOutput() did on one call (audio thread).
	enum class Output {
		IDLE,         // no message in flight; nothing written
		WROTE,        // one encoded float written to the out voltage
		INIT_ERROR,   // initiateMessage() failed; see lastInitErrorCode
		ENCODE_ERROR  // getNextMessageFloat() failed; message terminated
	};

	// ── Output side ────────────────────────────────────────────────────────
	// Encodes Tipsy messages onto the trigger CV output. Audio thread only.
	tipsy::ProtocolEncoder encoder;

	// SPSC queue of pending Tipsy messages (worker → audio). send() only
	// copies the payload; encoding happens in processOutput() on the audio
	// thread, which owns the encoder.
	//
	// Never cleared: clear() writes `start`, the consumer's index, so calling it
	// from the worker would break the single-consumer contract. Discarding goes
	// through discardCount + a queued sentinel instead.
	dsp::RingBuffer<TipsyMessage, 8> outQueue;

	// Discard sentinels enqueued so far. Written by sendReset() (worker), read
	// by processOutput() (audio); discardSeen is the audio thread's private
	// count of the ones it has consumed.
	//
	// A counter rather than a flag: two reloads in quick succession must discard
	// both batches, and a bool cleared after the first sentinel would let the
	// second play. The queued sentinel supplies the position the counter lacks —
	// anything pushed after it is new and survives.
	std::atomic<uint32_t> discardCount{0};
	uint32_t discardSeen = 0;

	// The message the encoder is streaming out. The encoder holds pointers into
	// its buffers for the whole (multi-cycle) message, so it must outlive the
	// encoding — a member, not a local (shift() hands out copies). Only ever
	// overwritten while the encoder is dormant, so an in-flight message is never
	// pulled out from under it.
	TipsyMessage outCurrentMessage;

	// Encoder result code from the last INIT_ERROR, for the module's log line.
	int lastInitErrorCode = 0;

	// ── Input side ─────────────────────────────────────────────────────────
	// Which trigger input carries the Tipsy stream, or -1 when disabled. Written
	// by claimInput() (worker), read by claimedInput() (audio).
	std::atomic<int> inPort{-1};

	// Decodes voltages back into messages. Audio thread only.
	tipsy::ProtocolDecoder inDecoder;

	// The decoder writes the payload here, so it must outlive the message being
	// assembled. Handed over once via provideDataBuffer() — which refuses while
	// a body is in flight, so it is only ever called when the decoder is idle.
	unsigned char inBuffer[MidiScript::tipsyMaxPayloadLength];

	// Completed message copied out of inDecoder's reusable buffer, so the
	// pointer processInput() returns stays valid until the next call.
	TipsyMessage inCurrentMessage;

	// Set (audio thread) when a decoded message is dropped for lack of room, or
	// when the decoder reports a malformed stream; cleared and reported once by
	// the module in process() via takeOverflow()/takeError(). Same rate-limiting
	// reason as the MIDI out overflow — readFloat() runs per sample, so logging
	// inline would flood the log from noise on the port.
	std::atomic<bool> inOverflow{false};
	std::atomic<bool> inError{false};

	// Worker side — midi.enableTipsyIn() binding. -1 releases the input.
	void claimInput(int port) {
		inPort.store(port, std::memory_order_relaxed);
	}

	// Audio thread reads this every sample.
	int claimedInput() const {
		return inPort.load(std::memory_order_relaxed);
	}

	// Worker side. Enqueues a validated message; keeps the last slot free so
	// sendReset() can always enqueue its sentinel. Returns false (without
	// queuing) when the queue has no room — the caller logs the drop.
	bool send(const char* mimeType, const unsigned char* data, uint32_t bytes) {
		if (outQueue.capacity() <= 1) return false;
		TipsyMessage p;
		p.mimeSize = (uint16_t)strlen(mimeType);
		p.dataSize = (uint16_t)bytes;
		std::memcpy(p.mime, mimeType, p.mimeSize + 1);
		std::memcpy(p.data, data, bytes);
		outQueue.push(p);
		return true;
	}

	// Worker side — queues a discard sentinel for the audio thread to drain.
	void sendReset() {
		// Order matters: the sentinel goes in first, so the audio thread can
		// never see the raised count without the sentinel that bounds it.
		TipsyMessage p;
		p.mimeSize = 0;
		p.dataSize = 0;
		outQueue.push(p);
		discardCount.fetch_add(1, std::memory_order_relaxed);
	}

	// Audio side. If the encoder is idle, drops any stale messages, starts the
	// next pending one, then drains one encoded float. WROTE sets voltageOut.
	Output processOutput(float& voltageOut) {
		if (encoder.isDormant()) {
			// Everything ahead of an unconsumed sentinel was queued by a script
			// that has since been replaced. Only done while dormant, so a message
			// already going out still completes. Bounded by the queue size, and
			// Tipsy messages are rare, so draining the run in one call is fine.
			while (discardSeen < discardCount.load(std::memory_order_relaxed) && !outQueue.empty()) {
				if (outQueue.shift().mimeSize == 0) discardSeen++;
			}

			if (!outQueue.empty()) {
				// Copy into the member the encoder points into for the whole message
				// (shift() returns a copy, so not a local).
				outCurrentMessage = outQueue.shift();
				auto initResult = encoder.initiateMessage(outCurrentMessage.mime, outCurrentMessage.dataSize, outCurrentMessage.data);
				if (encoder.isError(initResult)) {
					lastInitErrorCode = static_cast<int>(initResult);
					return Output::INIT_ERROR;
				}
			}
		}

		if (encoder.isDormant()) return Output::IDLE;

		float f;
		auto result = encoder.getNextMessageFloat(f);
		if (encoder.isError(result)) {
			encoder.terminateCurrentMessage();
			return Output::ENCODE_ERROR;
		}
		voltageOut = f;
		return Output::WROTE;
	}

	// Audio side. Feeds one sample into the decoder; on a completed message
	// returns a pointer to it (stable until the next call), else nullptr.
	// Malformed input is flagged via takeError() rather than logged: readFloat()
	// runs every sample, so a noisy port would otherwise flood the log. The
	// decoder resyncs on its own at the next message-begin sentinel.
	const TipsyMessage* processInput(float voltage) {
		auto result = inDecoder.readFloat(voltage);

		if (tipsy::ProtocolDecoder::isError(result)) {
			inError.store(true, std::memory_order_relaxed);
			return nullptr;
		}
		if (result != tipsy::ProtocolDecoder::DecoderResult::BODY_READY) return nullptr;

		// BODY_READY: the payload is complete in inBuffer, which the decoder
		// reuses for the next message — so copy it out now.
		size_t mimeSize = strnlen(inDecoder.getMimeType(), MidiScript::tipsyMaxMimeTypeSize - 1);
		uint32_t dataSize = inDecoder.getDataSize();
		if (dataSize > MidiScript::tipsyMaxPayloadLength) {
			// provideDataBuffer() sized the store, so the decoder should have
			// rejected this as ERROR_DATA_TOO_LARGE already. Defensive.
			inError.store(true, std::memory_order_relaxed);
			return nullptr;
		}
		TipsyMessage p;
		p.mimeSize = (uint16_t)mimeSize;
		p.dataSize = (uint16_t)dataSize;
		std::memcpy(p.mime, inDecoder.getMimeType(), mimeSize);
		p.mime[mimeSize] = '\0';
		std::memcpy(p.data, inBuffer, dataSize);
		inCurrentMessage = p;
		return &inCurrentMessage;
	}

	// Audio thread: set when the caller dropped a completed message for lack of
	// room; cleared and reported once by the module in process().
	void reportOverflow() {
		inOverflow.store(true, std::memory_order_relaxed);
	}

	// Flags drained by the module into the log (audio thread).
	bool takeOverflow() {
		return inOverflow.exchange(false, std::memory_order_relaxed);
	}
	bool takeError() {
		return inError.exchange(false, std::memory_order_relaxed);
	}

	// Releases the input claim and re-arms the decoder's data store. Safe: no
	// decoding happens at reset, and provideDataBuffer() refuses mid-body. The
	// output side is deliberately untouched — stale queued output is discarded
	// by the sentinel protocol on script reload instead.
	void reset() {
		inPort.store(-1, std::memory_order_relaxed);
		inDecoder.provideDataBuffer(inBuffer, sizeof(inBuffer));
	}
};


// ── Script log + overlay, one per module ───────────────────────────────────
// Everything the module tells the widget about: the runtime log and the
// current overlay message. Touched from three threads, which is why the
// contract is stated here once rather than inferred from call sites:
//
//   - worker thread: writeLog()/writeOverlay() produce log entries + overlay;
//   - audio thread: overflow reporting in process() produces log entries;
//   - loadScript()/onReset() callers produce RESET markers and "No script";
//   - UI thread: the widget drains the log (midiLogMessages.try_pop) and the
//     overlay ring (nextOverlayMessageId/getOverlayMessage).
//
// midiLogMessages is an MPMC queue because the log has concurrent producers;
// overlayQueue is a single-producer ring (worker) drained by the widget.
struct ScriptLog {
	// Log entries, FIFO. MPMC: pushed by the worker (writeLog), the audio
	// thread (overflow reporting), and the loadScript/onReset callers.
	rigtorp::MPMCQueue<std::tuple<LOG_FORMAT, float, std::string>> midiLogMessages{512};

	// Overlay ring + current message. Single-producer (worker via writeOverlay),
	// single-consumer (widget).
	dsp::RingBuffer<int, 8> overlayQueue;
	std::tuple<std::string, std::string, std::string> overlayMessage;

	// Worker side — writeLog(). Enqueues one entry.
	void push(LOG_FORMAT format, float timestamp, const std::string& text) {
		midiLogMessages.try_push(std::make_tuple(format, timestamp, text));
	}

	// Worker side — writeOverlay(). Marks one overlay slot with the current
	// message.
	void pushOverlay(const std::string& s1, const std::string& s2, const std::string& s3) {
		overlayQueue.push(0);
		overlayMessage = std::make_tuple(s1, s2, s3);
	}
};


// ── Script host: engines + live script, one per module ─────────────────────
// Owns the two engine instances and which one is live, plus the script source
// and its persisted config. Every call into script code goes through here, so
// the "is there an active engine?" check lives in one place instead of at
// eight call sites.
//
// Threading: the engines are worker-thread-owned once loaded (interpreter and
// cached hook refs); the module's audio thread only enqueues into their SPSC
// queues (queueMessage/queueTick) and drains via pump(). load()/closeState()
// run on the UI thread and BLOCK — closeState() must complete before the active
// pointer is reassigned, so only one engine ever has outstanding worker tasks.
// The port/param `se` pointers (module-side) are re-bound by the module after
// load().
struct ScriptHost {
	// Port/param counts injected into both engines at construction.
	static constexpr int inputCount = 4;
	static constexpr int inputTrigCount = 1;
	static constexpr int outputTrigCount = 1;
	static constexpr int paramCount = 4;
	static constexpr int midiInputCount = 1;
	static constexpr int midiOutputCount = 1;

	// The engine currently selected to run the loaded script, or null. Written
	// only by load()/closeState(); read via getActiveEngine().
	MidiScript::MidiScriptEngine* activeEngine = nullptr;

	// The two engines. Only one is ever loaded (activeEngine); the other is
	// closed on switch. Widget/tests reach these directly (RAM usage, state).
	MidiScript::Lua::MidiScriptEngineLua seLua;
	MidiScript::QuickJs::MidiScriptEngineQuickJs seQuickJs;

	/** [Stored to JSON] */
	std::string script = "";
	/** [Stored to JSON] */
	std::string scriptConfigJson = "";

	ScriptHost(MidiScript::MidiScriptEngineHandler* handler)
		: seLua(handler, inputCount, inputTrigCount, outputTrigCount, paramCount, midiInputCount, midiOutputCount),
		  seQuickJs(handler, inputCount, inputTrigCount, outputTrigCount, paramCount, midiInputCount, midiOutputCount) {}

	// UI thread: wires the shared worker into both engines.
	void setWorker(std::shared_ptr<ITaskWorker> worker) {
		seLua.setWorker(worker);
		seQuickJs.setWorker(worker);
	}

	// The engine currently selected to run the loaded script, or null.
	MidiScript::MidiScriptEngine* getActiveEngine() const {
		return activeEngine;
	}
	// Non-const accessor returning the pointer by reference, so tests can inject
	// a mock engine (m->host.getActiveEngine() = &mock).
	MidiScript::MidiScriptEngine*& getActiveEngine() {
		return activeEngine;
	}

	// Whether the loaded script is running on the Lua engine.
	bool isLuaEngine() const {
		return activeEngine == &seLua;
	}
	// Whether the loaded script is running on the QuickJs engine.
	bool isQuickJsEngine() const {
		return activeEngine == &seQuickJs;
	}

	// Selects the engine for `src`, closes the previously active one (blocking,
	// so only one engine ever has outstanding worker tasks), and starts loading
	// the new script into it. Returns the newly selected engine (null if the
	// script matched neither engine). The port/param `se` pointers are the
	// module's to re-bind after this returns.
	MidiScript::MidiScriptEngine* load(const std::string& src, const std::string& configJson) {
		script = src;
		MidiScript::MidiScriptEngine* prevEngine = activeEngine;
		activeEngine = nullptr;
		if (seLua.testScript(src)) activeEngine = &seLua;
		if (seQuickJs.testScript(src)) activeEngine = &seQuickJs;

		// Close the engine that is no longer active (silently — the caller has
		// already pushed the RESET marker). Blocking rather than the async
		// loadScript("") this used to be: once this call returns, activeEngine is
		// again the only engine that can have any state or outstanding worker
		// task, which is the invariant onRemove() (and onReset()) rely on to know
		// what needs tearing down.
		if (prevEngine && prevEngine != activeEngine) prevEngine->closeState();

		scriptConfigJson = configJson;
		if (activeEngine) activeEngine->loadScript(script.c_str(), scriptConfigJson);
		return activeEngine;
	}

	// Closes the active engine and nulls the pointer, so process() stops
	// dispatching. Blocking: closeState() runs onUnload() to completion. Rack
	// dispatches onRemove()/onReset() with the engine mutex held, so process()
	// cannot run concurrently.
	void closeState() {
		MidiScript::MidiScriptEngine* engine = activeEngine;
		activeEngine = nullptr;   // stop process() dispatching
		if (engine) engine->closeState();
	}

	// Audio-thread dispatch; no-ops when nothing is loaded.
	void queueMessage(int port, const MidiScript::QueuedMessage& msg) {
		if (activeEngine) activeEngine->processInMessage(port, msg);
	}
	void queueTick(int trigPort, uint8_t channel) {
		if (activeEngine) activeEngine->processInTick(trigPort, channel);
	}
	// Audio thread: runs one pump of the active engine's queued work.
	void process() {
		if (activeEngine) activeEngine->process();
	}

	// Refreshes scriptConfigJson from the active engine's onSave(), only
	// overwriting on success (false means the config couldn't be determined —
	// keep the last known value). Returns the current config.
	std::string captureConfig() {
		if (activeEngine) {
			std::string captured;
			if (activeEngine->captureConfig(captured)) {
				scriptConfigJson = captured;
			}
		}
		return scriptConfigJson;
	}
};


// Returns the one shared async worker for all MidiKit modules.
// The weak_ptr lets it be destroyed when the last module is removed.
//
// No mutex guards the expired()/make_shared/assignment sequence below. This is
// safe because modules are constructed on the UI thread, which is a single
// thread, so defaultWorker() is never called concurrently -- the static
// weak_ptr is only ever touched from that one thread.
static std::shared_ptr<ITaskWorker> defaultWorker() {
	static std::weak_ptr<ITaskWorker> shared;
	if (shared.expired()) {
		auto worker = std::make_shared<MpmcTaskWorker>("MidiKit worker");
		shared = worker;
		return worker;
	}
	return shared.lock();
}

struct MidiKitModule : Module, MidiScript::MidiScriptEngineHandler, MidiProcessorHandler {
	enum ParamIds {
		ENUMS(PARAM, 4),
		NUM_PARAMS
	};
	enum InputIds {
		ENUMS(INPUT, 4),
		INPUT_TRIG,
		NUM_INPUTS
	};
	enum OutputIds {
		OUTPUT_TRIG,
		NUM_OUTPUTS
	};
	enum LightIds {
		NUM_LIGHTS
	};

	/** [Stored to JSON] */
	int panelTheme = 0;

	/** [Stored to Json] */
	midi::InputQueue midiInput;

	// Decodes the incoming stream into semantic events (NRPN/RPN/14-bit CC
	// assembly) before it reaches the script. The queue is injected rather than
	// owned: midiInput stays the module's, keeping its widget binding and JSON
	// exactly as they were. MUST stay declared after midiInput so destruction
	// order keeps the queue alive for the processor's whole lifetime.
	//
	// Only processMessage() is used -- never process(): the module pumps the
	// queue itself under processDivider, and each decoded message has to be
	// queued for the worker thread rather than dispatched inline.
	MidiProcessor midiProcessor{&midiInput};

	/** [Stored to Json] */
	MidiOutput midiOutput;

	// Script log + overlay, in their own struct (see ScriptLog for the
	// threading contract).
	ScriptLog log;

	// Script engines + live script, in their own struct (see ScriptHost for the
	// threading contract).
	/** [Stored to Json] */
	ScriptHost host;

	// MIDI output queue, owned by the module rather than either engine, so its
	// contents outlive engine switches and clearScript() rather than being tied
	// to whichever engine happened to be active when they were queued. Written
	// by sendMidi() (worker thread), drained by process() (audio thread) and by
	// onRemove() at teardown.
	dsp::RingBuffer<std::tuple<int, MidiScript::Message, uint8_t, uint64_t>, 128> midiOutQueue;
	// Set (worker thread) when sendMidi() drops a group for lack of room;
	// cleared and reported once (audio thread) in process(). A saturated output
	// must not flood the log through the same bottleneck that is already
	// saturated.
	std::atomic<bool> midiOutOverflow{false};

	// ── Tipsy protocol over the trigger CV (TipsyPort) ───────────────────────
	// All Tipsy state and encode/decode logic lives in the TipsyPort struct;
	// the module owns just this one object.
	TipsyPort tipsyPort;

	dsp::ClockDivider processDivider;
	dsp::Timer rateLimiterTimer;

	// One SchmittTrigger and tick counter per polyphonic channel of the trigger
	// input — trig.onTrigger/trig.getTicks() are channel-aware.
	dsp::SchmittTrigger inputTrigger[PORT_MAX_CHANNELS];
	uint64_t inputTriggerTick[PORT_MAX_CHANNELS];
	bool outputTriggerActive[PORT_MAX_CHANNELS];
	dsp::PulseGenerator outputPulseGenerator[PORT_MAX_CHANNELS];

	// Trigger inputs enabled by the script (trig.enableIn()), as a bitmask of
	// polyphonic channels (bit c = channel c of port 0). The module gates all
	// trigger processing on this: disabled channels get no ticks, no
	// sendAfterTrigger drains, and no trig.onTrigger. Atomic: written by the
	// worker (trig.enableIn()), read by the audio thread (process()).
	std::atomic<uint16_t> triggerEnabledMask{0};

	// Extended-CC input enables (midi.enableNrpnIn/enableRpnIn/enableCc14bitIn),
	// in their own struct. Atomic masks written by the worker from the enable
	// bindings, read by the audio thread in processMidi().
	ExtendedCcEnables extendedCc;

	uint64_t sample;
	float sampleRate;

	// Points every per-CV-port/param engine back-pointer at the active engine.
	// Passing null clears instead — no active engine means no port/param can be
	// enabled (a script re-enables via its bindings). Called at construction, on
	// reset, before a script loads (null), and after loadScript() selects it.
	void bindPortParamEngine(MidiScript::MidiScriptEngine* engine) {
		for (int i = 0; i < 4; i++) {
			reinterpret_cast<MidiScript::MidiScriptEnginePortInfo*>(inputInfos[i])->se = engine;
			reinterpret_cast<MidiScript::MidiScriptEngineParamQuantity*>(paramQuantities[i])->se = engine;
			if (!engine) {
				reinterpret_cast<MidiScript::MidiScriptEnginePortInfo*>(inputInfos[i])->enabled = false;
				reinterpret_cast<MidiScript::MidiScriptEngineParamQuantity*>(paramQuantities[i])->enabled = false;
			}
		}
	}

	// ── MidiScriptEngineHandler ──────────────────────────────────────────────
	// The engines call back into the module through these methods for every
	// module-facing operation (log/overlay/input/trig/param).

	// MidiScriptEngineHandler
	void writeLog(const std::string& text, bool useTimestamp = true) override {
		float timestamp = sampleRate != 0.f ? float(sample) / sampleRate : 0.f;
		log.push(useTimestamp ? LOG_FORMAT::TIMESTAMP : LOG_FORMAT::TEXT, timestamp, text);
	}

	// MidiScriptEngineHandler
	void writeOverlay(const std::string& s1, const std::string& s2, const std::string& s3) override {
		log.pushOverlay(s1, s2, s3);
	}

	// MidiScriptEngineHandler
	void enableInput(int i) override {
		reinterpret_cast<MidiScript::MidiScriptEnginePortInfo*>(inputInfos[i])->enabled = true;
	}

	// MidiScriptEngineHandler — trig.enableIn() binding (worker thread).
	void enableTrigger(int port, uint8_t channel) override {
		if (port != 0) return;
		if (channel >= PORT_MAX_CHANNELS) return;
		triggerEnabledMask.fetch_or(static_cast<uint16_t>(1) << channel, std::memory_order_relaxed);
	}

	// Gate for all trigger processing; see triggerEnabledMask. Audio thread.
	bool isTriggerEnabled(int port, uint8_t channel) const {
		if (port != 0) return false;
		if (channel >= PORT_MAX_CHANNELS) return false;
		return (triggerEnabledMask.load(std::memory_order_relaxed) >> channel) & 1;
	}

	// Forgets every enabled (port, channel) on script load/reset.
	void clearTriggerEnabled() {
		for (int i = 0; i < PORT_MAX_CHANNELS; i++) inputTriggerTick[i] = 0;
		triggerEnabledMask.store(0, std::memory_order_relaxed);
	}

	// MidiScriptEngineHandler
	float getInputVoltage(int i, uint8_t ch) override {
		if (reinterpret_cast<MidiScript::MidiScriptEnginePortInfo*>(inputInfos[i])->enabled)
			return inputs[INPUT + i].getVoltage(ch);
		return 0.f;
	}

	// MidiScriptEngineHandler — trig.enableTipsyIn() binding (worker thread).
	// i is a 0-based trigger input index, or -1 to disable decoding.
	void enableTipsyIn(int i) override {
		tipsyPort.claimInput(i);
	}

	// MidiScriptEngineHandler — midi.enableNrpnIn()/enableRpnIn() binding
	// (worker thread). midiPort 0 is the only MIDI input.
	void enableNrpnIn(int midiPort, int kind, int channel) override {
		if (midiPort != 0) return;
		extendedCc.enableNrpn(kind, channel);
	}

	// MidiScriptEngineHandler — midi.enableCc14bitIn() binding (worker thread).
	void enableCc14bitIn(int midiPort, int cc, int channel) override {
		if (midiPort != 0) return;
		extendedCc.enableCc14bit(cc, channel);
	}

	// Whether the script asked for assembled events of each kind on this MIDI
	// channel. Audio thread. Delegates to extendedCc; kept here because the
	// test suite inspects these on the module directly.
	bool isNrpnEnabled(uint8_t ch, bool isRpn) const {
		return extendedCc.isNrpnEnabled(ch, isRpn);
	}
	bool isCc14bitEnabled(uint8_t ch, uint8_t cc) const {
		return extendedCc.isCc14bitEnabled(ch, cc);
	}

	// MidiScriptEngineHandler
	float getTrigVoltage(int i, uint8_t ch) override {
		// Only channel 0 of the claimed trigger input carries the Tipsy stream:
		// that channel reads as 0 — the raw encoded voltages are protocol, not
		// a gate a script should act on. Other channels are unaffected.
		if (ch == 0 && i == tipsyPort.claimedInput()) return 0.f;
		return inputs[INPUT_TRIG + i].getVoltage(ch);
	}

	// MidiScriptEngineHandler
	uint64_t getTrigTicks(int i, uint8_t ch) override {
		if (ch >= PORT_MAX_CHANNELS) return 0;
		return inputTriggerTick[ch];
	}

	// MidiScriptEngineHandler
	void enableParam(int i) override {
		reinterpret_cast<MidiScript::MidiScriptEngineParamQuantity*>(paramQuantities[i])->enabled = true;
	}

	// MidiScriptEngineHandler
	float getParamValue(int i) override {
		if (reinterpret_cast<MidiScript::MidiScriptEngineParamQuantity*>(paramQuantities[i])->enabled)
			return params[PARAM + i].getValue();
		return 0.f;
	}

	// MidiScriptEngineHandler
	void setTrig(int i, uint8_t ch, float duration = 1e-3f) override {
		outputTriggerActive[ch] = true;
		outputPulseGenerator[ch].trigger(duration);
	}

	// MidiScriptEngineHandler
	void setTrigVoltage(int i, uint8_t ch, float voltage) override {
		outputTriggerActive[ch] = false;
		outputs[OUTPUT_TRIG].setVoltage(voltage, ch);
	}

	// MidiScriptEngineHandler
	bool sendMidi(int midiPort, const MidiScript::Message* msgs, size_t count, uint8_t channel, uint64_t tick) override {
		// Capacity is checked for the whole group, so a multi-message value — an
		// NRPN quad or a 14-bit CC pair — is never half-emitted.
		// dsp::RingBuffer::push() does not bounds-check: on a full
		// buffer it overwrites unread entries and leaves size() > capacity.
		if (midiOutQueue.capacity() < count) {
			midiOutOverflow.store(true, std::memory_order_relaxed);
			return false;
		}
		for (size_t i = 0; i < count; i++) {
			midiOutQueue.push(std::make_tuple(midiPort, msgs[i], channel, tick));
		}
		return true;
	}

	// MidiScriptEngineHandler
	bool sendTipsyOut(const char* mimeType, const unsigned char* data, uint32_t dataBytes) override {
		if (!mimeType || !data || dataBytes > MidiScript::tipsyMaxPayloadLength) {
			writeLog("Tipsy: invalid parameters", false);
			return false;
		}
		size_t mimeSize = strlen(mimeType);
		// An empty mime type would be indistinguishable from a discard sentinel.
		if (mimeSize == 0) {
			writeLog("Tipsy: mime type must not be empty", false);
			return false;
		}
		if (mimeSize + 1 > MidiScript::tipsyMaxMimeTypeSize) {
			writeLog("Tipsy: mime type too long", false);
			return false;
		}
		// send() keeps the last slot free for the discard sentinel; a full queue
		// is reported here so the drop is logged.
		if (!tipsyPort.send(mimeType, data, dataBytes)) {
			writeLog("Tipsy: pending queue full", false);
			return false;
		}
		return true;
	}

	// MidiScriptEngineHandler
	void sendTipsyOutReset() override {
		tipsyPort.sendReset();
	}

	// Outputs the next Tipsy-encoded voltage on the trigger output (audio
	// thread). Delegates the encode to TipsyPort and turns its status into the
	// trigger output write or a log line. Returns true if a voltage was output.
	bool processTipsyOutput(uint8_t channel = 0) {
		float f;
		switch (tipsyPort.processOutput(f)) {
			case TipsyPort::Output::WROTE:
				// Tipsy messages always go to the first trigger output (port 0).
				setTrigVoltage(0, channel, f);
				return true;
			case TipsyPort::Output::INIT_ERROR:
				writeLog("Tipsy encoder error: " + std::to_string(tipsyPort.lastInitErrorCode), false);
				return false;
			case TipsyPort::Output::ENCODE_ERROR:
				writeLog("Tipsy encoding error", false);
				return false;
			default: // IDLE
				return false;
		}
	}

	// Feeds one sample from the Tipsy input trigger into the decoder (audio
	// thread). On a completed message, copies it into the active engine's
	// tipsyInQueue for the worker to dispatch. Returns true if a message
	// completed and was enqueued on this sample.
	//
	// The Tipsy stream is always carried on channel 0 of the trigger input —
	// other channels are never decoded. No-op unless a script claimed the
	// trigger input via trig.enableTipsyIn().
	bool processTipsyInput() {
		int port = tipsyPort.claimedInput();
		if (port < 0 || !host.getActiveEngine()) return false;
		if (!inputs[INPUT_TRIG + port].isConnected()) return false;

		const TipsyPort::TipsyMessage* m = tipsyPort.processInput(inputs[INPUT_TRIG + port].getVoltage(0));
		if (!m) return false;

		if (host.getActiveEngine()->tipsyInQueue.full()) {
			tipsyPort.reportOverflow();
			return false;
		}
		host.getActiveEngine()->tipsyInQueue.push(*m);
		return true;
	}

	MidiKitModule() : MidiKitModule(defaultWorker()) {}
	explicit MidiKitModule(std::shared_ptr<ITaskWorker> worker)
		: host(this) {
		panelTheme = pluginSettings.panelThemeDefault;
		config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);
		configInput(INPUT_TRIG, "Trigger");
		configOutput(OUTPUT_TRIG, "Trigger");
		for (int i = 0; i < 4; i++) {
			configInput<MidiScript::MidiScriptEnginePortInfo>(INPUT + i);
			configParam<MidiScript::MidiScriptEngineParamQuantity>(PARAM + i, 0.f, 1.f, 0.f);
		}
		// No engine is loaded yet — bind to null (clears the UI state); it is
		// bound to the active engine by loadScript() once a script loads.
		bindPortParamEngine(nullptr);

		processDivider.setDivision(8);
		// Routes decoded messages into processMidi() below. Without this the
		// processor decodes into an empty handler list and nothing reaches the
		// engine at all.
		midiProcessor.subscribe(this);
		host.setWorker(worker);
		onReset();
	}

	// Closes the active engine and drains whatever its onUnload() queued. Rack
	// dispatches this before the module leaves the engine and holds the engine
	// mutex across it, so process() cannot run concurrently.
	//
	// closeState() blocks, so the worker has stopped producing before the drain
	// — preserve that order, it is what makes the drain safe.
	void onRemove(const RemoveEvent& e) override {
		host.closeState();        // closes + nulls the active engine (blocking)
		flushOutput();            // drains the module queue; no engine needed
	}

	// Sends whatever is left in the module's out-queue straight to the device,
	// ignoring frame/tick scheduling — teardown is the last chance to emit.
	void flushOutput() {
		while (!midiOutQueue.empty()) {
			auto t = midiOutQueue.shift();
			midi::Message msg = std::get<1>(t);
			msg.frame = -1;
			midiOutput.sendMessage(msg);
		}
	}

	void onReset() override {
		midiInput.reset();
		// Emptying the queue leaves the stream discontinuous, so drop any
		// half-received NRPN/RPN/14-bit CC state with it: a parameter still armed
		// from before the reset would capture the next data entry that arrives.
		midiProcessor.reset();
		midiOutput.reset();
		sample = 0;
		// No script claims the trigger input until its trig.enableIn() runs.
		clearTriggerEnabled();
		// Likewise no NRPN/RPN/14-bit assembly until midi.enableNrpnIn() and
		// friends run.
		extendedCc.clear();
		// A script claims the trigger input for Tipsy explicitly, so a reset
		// releases it and re-arms the decoder's data store (safe: nothing is
		// decoding at reset, and provideDataBuffer() refuses mid-body).
		tipsyPort.reset();
		// No engine is active after a reset (host.closeState() nulls it), so
		// bind to null — it is rebound by the next loadScript().
		bindPortParamEngine(nullptr);
		for (uint8_t i = 0; i < PORT_MAX_CHANNELS; i++) {
			outputTriggerActive[i] = true;
			outputPulseGenerator[i].reset();
		}
		// Only the previously active engine can have real state — loadScript()
		// maintains that invariant by only ever loading one engine at a time and
		// closing the other on switch — so that is the only one that needs
		// tearing down here. Blocking (closeState()) rather than fire-and-forget,
		// same reasoning as loadScript(): once this returns, activeEngine is
		// again the only engine that can have any outstanding worker task.
		host.closeState();

		log.push(LOG_FORMAT::RESET, 0.f, std::string(""));
		log.push(LOG_FORMAT::TEXT, 0.f, std::string("No script"));
	}

	void onSampleRateChange(const SampleRateChangeEvent& e) override {
		sampleRate = e.sampleRate;
	}

	// MidiProcessorHandler. Called synchronously from midiProcessor.processMessage()
	// on the AUDIO thread, so it stays a pure enqueue -- script code runs on the
	// shared worker and must never be entered from here.
	//
	// A CC belonging to an extended message is notified TWICE: once as Type::CC
	// (with isComponent set) and again as NRPN/RPN/CC_14BIT once assembled. What
	// the script asked for decides which of the two it sees:
	//
	//  - Assembled events are queued only when the matching enable is set,
	//    otherwise dropped. Queuing unconditionally would fire callbacks the
	//    script never asked for, and (before the callbacks existed) double-fire
	//    onMessage for every component.
	//  - The raw CC is dropped when it is a component AND the script enabled the
	//    kind of assembly it belongs to. isComponent alone must not decide: a
	//    script that enabled only 14-bit CC still wants to see CC 98 raw.
	//
	// Returning false keeps the message available to any other handler.
	bool processMidi(const MessageEx& m) override {
		if (!host.getActiveEngine()) return false;

		switch (m.type) {
			case MessageEx::Type::NRPN:
			case MessageEx::Type::RPN: {
				bool isRpn = (m.type == MessageEx::Type::RPN);
				// Parameter-select notifications carry no value (extraValue < 0),
				// and the RPN 127/127 reset carries paramNumber < 0. Neither is a
				// parameter change, so neither reaches the script.
				if (!m.hasValue() || m.getParamNumber() < 0) return false;
				if (!extendedCc.isNrpnEnabled(m.getChannel(), isRpn)) return false;
				break;
			}
			case MessageEx::Type::CC_14BIT:
				if (!extendedCc.isCc14bitEnabled(m.getChannel(), uint8_t(m.getParamNumber()))) return false;
				break;
			case MessageEx::Type::CC:
				if (m.isComponent && extendedCc.isComponentEnabled(m)) return false;
				break;
			default:
				break;
		}

		MidiScript::QueuedMessage q;
		q.msg = m.msg;
		q.type = m.type;
		q.paramNumber = m.paramNumber;
		q.extraValue = m.extraValue;
		q.isComponent = m.isComponent;
		host.queueMessage(0, q);
		return false;
	}

	void processBypass(const ProcessArgs& args) override {
		midi::Message msg;
		while (midiInput.tryPop(&msg, args.frame)) {
			(void)0;
		}
		Module::processBypass(args);
	}

	void process(const ProcessArgs& args) override {
		/*
		// play it safe and limit MIDI output rate to 200 Hz.
		const float rateLimiterPeriod = 1 / 200.f;
		bool rateLimiterTriggered = (rateLimiterTimer.process(args.sampleTime) >= rateLimiterPeriod);
		if (rateLimiterTriggered)
			rateLimiterTimer.time -= rateLimiterPeriod;
		else
			return;
		*/

		// While the trigger input carries a Tipsy stream on channel 1, the
		// encoded voltages cross the trigger threshold constantly — suppress
		// trig.onTrigger and tick counting on THAT channel only so decoding
		// isn't mistaken for clock ticks. Other channels keep firing normally.
		// The SchmittTriggers are still stepped so their states stay current.
		//
		// Each channel is detected independently: its tick clock advances and
		// drains that channel's tick-scheduled (sendAfterTrigger) messages.
		// All of it is gated on trig.enableIn() — disabled channels get no
		// ticks and no trig.onTrigger.
		bool tipsyStreaming = tipsyPort.claimedInput() >= 0;
		int channels = inputs[INPUT_TRIG].getChannels();
		if (channels <= 0) channels = 1;
		for (uint8_t c = 0; c < channels; c++) {
			// Tipsy only takes over channel 1's trigger; the other channels are
			// ordinary gates and must still fire trig.onTrigger.
			bool tipsyOnChannel = (c == 0) && tipsyStreaming;
			if (host.getActiveEngine() && isTriggerEnabled(0, c) && inputTrigger[c].process(inputs[INPUT_TRIG].getVoltage(c)) && !tipsyOnChannel) {
				inputTriggerTick[c]++;
				midiOutput.processTick(c, inputTriggerTick[c]);
				host.queueTick(0, c);
			}
		}

		// Every sample, not under processDivider: the sender emits one encoded
		// float per sample, so a divided read would drop most of the stream.
		processTipsyInput();

		if (processDivider.process()) {
			if (host.getActiveEngine()) {
				// Pumped here rather than via midiProcessor.process() because each
				// decoded message must be queued for the worker thread, not
				// dispatched inline: processMessage() notifies processMidi() below
				// synchronously, and script code never runs on the audio thread.
				midi::Message msg;
				while (midiInput.tryPop(&msg, args.frame)) {
					midiProcessor.processMessage(msg);
				}

				host.process();
			}

			// Drains the module's own out-queue regardless of activeEngine, so a
			// cleared script's onUnload() output (queued while activeEngine was
			// still set) still reaches the device even though activeEngine is now
			// null. Runs after dispatch so output produced by this tick's
			// activeEngine->process() above still drains this same tick.
			if (midiOutOverflow.exchange(false, std::memory_order_relaxed)) {
				float timestamp = sampleRate != 0.f ? float(sample) / sampleRate : 0.f;
				log.push(LOG_FORMAT::TEXT, timestamp, std::string("MIDI output queue full, message(s) dropped"));
			}
			if (tipsyPort.takeOverflow()) {
				float timestamp = sampleRate != 0.f ? float(sample) / sampleRate : 0.f;
				log.push(LOG_FORMAT::TEXT, timestamp, std::string("Tipsy input queue full, message(s) dropped"));
			}
			if (tipsyPort.takeError()) {
				float timestamp = sampleRate != 0.f ? float(sample) / sampleRate : 0.f;
				log.push(LOG_FORMAT::TEXT, timestamp, std::string("Tipsy input: malformed stream"));
			}
			while (!midiOutQueue.empty()) {
				auto t = midiOutQueue.shift();
				midi::Message msg = std::get<1>(t);
				uint8_t channel = std::get<2>(t);
				midiOutput.send(msg, channel, std::get<3>(t));
			}
			midiOutput.processFrame(args.frame);
		}

		if (outputs[OUTPUT_TRIG].isConnected()) {
			for (uint8_t i = 0; i < PORT_MAX_CHANNELS; i++) {
				bool s = outputPulseGenerator[i].process(args.sampleTime);
				if (outputTriggerActive[i]) {
					outputs[OUTPUT_TRIG].setVoltage(s ? 10.f : 0.f, i);
				}
			}
			
			// Drains the Tipsy queue regardless of activeEngine, for the same
			// reason as the MIDI out-queue above: messages queued by a script's
			// onUnload() must still reach the output after the engine is gone.
			processTipsyOutput(0);
		}

		sample++;
	}

	json_t* dataToJson() override {
		json_t* rootJ = json_object();
		json_object_set_new(rootJ, "panelTheme", json_integer(panelTheme));

		json_object_set_new(rootJ, "midiInput", midiInput.toJson());
		json_object_set_new(rootJ, "midiOutput", midiOutput.toJson());
		json_object_set_new(rootJ, "script", json_string(host.script.c_str()));

		// Refresh here rather than in onSave(): Rack's periodic autosave calls
		// saveAutosave() without dispatching onSave() first, so an onSave()-only
		// refresh would leave autosaves writing stale config. rack.onSave() is
		// side-effect-free by contract, so running it on every save is harmless.
		//
		// Only overwrite on success: false means the config couldn't be
		// determined (dispatch dropped or timed out), so keeping the last known
		// value writes slightly stale config instead of erasing the user's
		// settings. A script with no onSave() returns true with an empty string
		// and correctly clears any stale value.
		std::string configJson = host.captureConfig();
		if (!configJson.empty()) {
			json_t* configJ = json_loads(configJson.c_str(), 0, NULL);
			if (configJ) {
				json_object_set_new(rootJ, "scriptConfig", configJ);
			}
		}
		return rootJ;
	}

	void dataFromJson(json_t* rootJ) override {
		json_t* panelThemeJ = json_object_get(rootJ, "panelTheme");
		if (panelThemeJ) panelTheme = json_integer_value(panelThemeJ);

		json_t* midiInputJ = json_object_get(rootJ, "midiInput");
		if (midiInputJ && json_is_object(midiInputJ)) midiInput.fromJson(midiInputJ);
		json_t* midiOutputJ = json_object_get(rootJ, "midiOutput");
		if (midiOutputJ && json_is_object(midiOutputJ)) midiOutput.fromJson(midiOutputJ);

		json_t* scriptJ = json_object_get(rootJ, "script");
		if (scriptJ && json_is_string(scriptJ)) {
			// Restore any persisted script config alongside the script itself.
			json_t* configJ = json_object_get(rootJ, "scriptConfig");
			std::string configJson;
			if (configJ && json_is_object(configJ)) {
				char* s = json_dumps(configJ, JSON_COMPACT);
				if (s) {
					configJson = s;
					free(s);
				}
			}
			loadScript(json_string_value(scriptJ), configJson);
		}
	}

	void loadScript(std::string s, std::string configJson = "") {
		sample = 0;
		// The incoming script inherits no half-received NRPN/RPN/14-bit CC state
		// from the previous one: assembly belongs to the script's view of the
		// stream, not to the module.
		midiProcessor.reset();
		for (int i = 0; i < PORT_MAX_CHANNELS; i++) inputTriggerTick[i] = 0;
		// Forgets every trig.enableIn()d channel BEFORE the new script loads, so
		// it starts with all callbacks disabled. Same for the extended-CC enables:
		// they belong to the outgoing script, not to the module.
		clearTriggerEnabled();
		extendedCc.clear();
		// Disable the outgoing script's ports/params before loading the new one.
		bindPortParamEngine(nullptr);
		log.push(LOG_FORMAT::RESET, 0.f, std::string(""));

		// Select the engine for the script and load it, closing the outgoing
		// engine (blocking) — see ScriptHost::load().
		MidiScript::MidiScriptEngine* engine = host.load(s, configJson);

		// Keep port/param info pointers in sync with the active engine
		bindPortParamEngine(engine);
	}

	void clearScript() {
		loadScript("");
	}
};


struct LogDisplay : LedTextDisplay {
	std::list<std::tuple<LOG_FORMAT, float, std::string>>* buffer;
	bool dirty = true;

	LogDisplay() {
		color = nvgRGB(0xf0, 0xf0, 0xf0);
		bgColor.a = 0.f;
		fontSize = 9.2f;
		textOffset.y += 2.f;
	}

	void step() override {
		LedTextDisplay::step();
		if (dirty) {
			text = "";
			size_t size = std::min(buffer->size(), static_cast<size_t>(box.size.x / fontSize) + 1);
			size_t i = 0;
			for (std::tuple<LOG_FORMAT, float, std::string> s : *buffer) {
				if (i >= size) break;
				LOG_FORMAT f = std::get<0>(s);
				float timestamp = std::get<1>(s);
				switch (f) {
					case LOG_FORMAT::TIMESTAMP:
						text += string::f("[%9.4f] %s\n", timestamp, std::get<2>(s).c_str());
						break;
					case LOG_FORMAT::TEXT:
						text += string::f("%s\n", std::get<2>(s).c_str());
						break;
					case LOG_FORMAT::INDENTED:
						text += string::f("     %s\n", std::get<2>(s).c_str());
						break;
					default:
						break;
				};
			}
		}
	}

	void reset() {
		buffer->clear();
		dirty = true;
	}
};

// Placeholder menu entry that builds the script-registered items
// (rack.registerContextMenu) asynchronously. getContextMenus() evaluates each
// item's onGetValue callback on the worker thread and then invokes its
// callback with the evaluated specs.
struct ScriptContextMenuItems : ui::MenuEntry {
	struct Context {
		std::vector<MidiScript::ScriptMenuItem> specs;
		std::atomic<bool> loaded{false};
	};
	MidiKitModule* module;
	std::shared_ptr<Context> ctx;
	bool built = false;

	ScriptContextMenuItems(MidiKitModule* module) : module(module) {
		ctx = std::make_shared<Context>();
		// Capture a local copy: Apple's Clang rejects capturing the data
		// member `ctx` by name in a capture list.
		std::shared_ptr<Context> c = ctx;
		module->host.getActiveEngine()->getContextMenus([c](const std::vector<MidiScript::ScriptMenuItem>& specs) {
			// Runs on the worker thread once every onGetValue has been
			// evaluated. Only publishes the specs; the menu widgets are
			// constructed by step() on the UI thread.
			c->specs = specs;
			c->loaded.store(true, std::memory_order_release);
		});
	}

	void step() override {
		if (!built && ctx->loaded.load(std::memory_order_acquire)) {
			built = true;
			buildItems();
			requestDelete();
		}
		ui::MenuEntry::step();
	}

	void buildItems() {
		Menu* menu = dynamic_cast<Menu*>(parent);
		if (!menu) return;
		MidiKitModule* m = module;
		Widget* anchor = this;
		for (const MidiScript::ScriptMenuItem& spec : ctx->specs) {
			Widget* item;
			if (spec.type == MidiScript::ScriptMenuItem::Type::Boolean) {
				item = createMenuItem(spec.label, CHECKMARK(spec.checked), [m, spec]() {
					m->host.getActiveEngine()->invokeContextMenuCallback(spec.callbackId, spec.checked ? 0 : 1);
				});
			}
			else {
				item = createSubmenuItem(spec.label, "", [m, spec](Menu* sub) {
					for (size_t i = 0; i < spec.options.size(); i++) {
						sub->addChild(createMenuItem(spec.options[i], CHECKMARK(i == static_cast<size_t>(spec.selected)), [m, spec, i]() {
							m->host.getActiveEngine()->invokeContextMenuCallback(spec.callbackId, static_cast<int>(i));
						}));
					}
				});
			}
			menu->addChildAbove(item, anchor);
			anchor = item;
		}
	}
};

struct MidiKitWidget : ThemedModuleWidget<MidiKitModule>, OverlayMessageProvider {
	const size_t BUFFERSIZE = 800;
	LogDisplay* logDisplay;
	std::list<std::tuple<LOG_FORMAT, float, std::string>> buffer;
	std::string filename = "";

	MidiKitWidget(MidiKitModule *module)
		: ThemedModuleWidget<MidiKitModule>(module, "MidiKit") {
		setModule(module);
		this->module = module;

		addChild(createWidget<StoermelderBlackScrew>(Vec(RACK_GRID_WIDTH, 0)));
		addChild(createWidget<StoermelderBlackScrew>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, 0)));
		addChild(createWidget<StoermelderBlackScrew>(Vec(RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));
		addChild(createWidget<StoermelderBlackScrew>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));

		MidiWidget<>* display1 = createWidget<MidiWidget<>>(Vec(0.f, 36.4f));
		display1->box.size = Vec(180.0f, 44.6f);
		display1->setMidiPort(module ? &module->midiInput : NULL, "In");
		addChild(display1);

		LedDisplay* textDisplay = createWidget<LedDisplay>(Vec(0.f, 81.0f));
		textDisplay->box.size = Vec(180.f, 140.6f);
		addChild(textDisplay);

		logDisplay = createWidget<LogDisplay>(Vec());
		logDisplay->buffer = &buffer;
		logDisplay->box.size = textDisplay->box.size.minus(Vec(0.f, 6.f));
		logDisplay->fontSize = 7.2f;
		textDisplay->addChild(logDisplay);

		MidiWidget<>* display2 = createWidget<MidiWidget<>>(Vec(0.f, 221.6f));
		display2->box.size = Vec(180.0f, 44.6f);
		display2->setMidiPort(module ? &module->midiOutput : NULL, "Out");
		addChild(display2);

		addParam(createParamCentered<StoermelderTrimpot>(Vec(24.7f, 287.3f), module, MidiKitModule::PARAM + 0));
		addParam(createParamCentered<StoermelderTrimpot>(Vec(56.2f, 287.3f), module, MidiKitModule::PARAM + 1));
		addParam(createParamCentered<StoermelderTrimpot>(Vec(87.6f, 287.3f), module, MidiKitModule::PARAM + 2));
		addParam(createParamCentered<StoermelderTrimpot>(Vec(119.1f, 287.3f), module, MidiKitModule::PARAM + 3));

		addInput(createInputCentered<StoermelderPort>(Vec(24.7f, 328.4f), module, MidiKitModule::INPUT + 0));
		addInput(createInputCentered<StoermelderPort>(Vec(56.2f, 328.4f), module, MidiKitModule::INPUT + 1));
		addInput(createInputCentered<StoermelderPort>(Vec(87.6f, 328.4f), module, MidiKitModule::INPUT + 2));
		addInput(createInputCentered<StoermelderPort>(Vec(119.1f, 328.4f), module, MidiKitModule::INPUT + 3));

		addOutput(createOutputCentered<StoermelderPort>(Vec(156.f, 287.3f), module, MidiKitModule::OUTPUT_TRIG));

		addInput(createInputCentered<StoermelderPort>(Vec(156.f, 328.4f), module, MidiKitModule::INPUT_TRIG));

		if (module) {
			OverlayMessageWidget::registerProvider(this);
		}
	}

	~MidiKitWidget() {
		if (module) {
			OverlayMessageWidget::unregisterProvider(this);
		}
	}

	void step() override {
		ThemedModuleWidget<MidiKitModule>::step();
		if (!module) return;
		std::tuple<LOG_FORMAT, float, std::string> s;
		while (module->log.midiLogMessages.try_pop(s)) {
			if (buffer.size() == BUFFERSIZE) buffer.pop_back();
			if (std::get<0>(s) == LOG_FORMAT::RESET) {
				resetLog();
			}
			else {
				buffer.push_front(s);
				logDisplay->dirty = true;
			}
		}
	}

	void resetLog() {
		buffer.clear();
		logDisplay->reset();
	}

	void appendContextMenu(Menu* menu) override {
		ThemedModuleWidget<MidiKitModule>::appendContextMenu(menu);

		if (module->host.getActiveEngine()) {
			menu->addChild(new MenuSeparator());
			if (module->host.isLuaEngine()) {
				menu->addChild(createMenuLabel("Running Script (Lua)"));
				size_t used;
				if (module->host.seLua.getMemoryUsage(used)) {
					menu->addChild(createMenuLabel(string::f("RAM usage: %zu KB", used / 1024)));
				}
			}
			if (module->host.isQuickJsEngine()) {
				menu->addChild(createMenuLabel("Running Script (QuickJs)"));
				size_t used, total;
				if (module->host.seQuickJs.getMemoryUsage(used, total)) {
					float pct = total > 0 ? 100.f * used / total : 0.f;
					menu->addChild(createMenuLabel(string::f("RAM usage: %zu / %zu KB (%.0f%%)", used / 1024, total / 1024, pct)));
				}
			}

			menu->addChild(new ScriptContextMenuItems(module));
		}

		menu->addChild(new MenuSeparator());
		menu->addChild(createMenuLabel("Script"));
		menu->addChild(createSubmenuItem("Examples (JavaScript)", "", [=](Menu* menu) {
			appendExampleItems(menu, asset::plugin(pluginInstance, "presets/MidiKit/JavaScript"), ".js");
		}));
		menu->addChild(createSubmenuItem("Examples (Lua)", "", [=](Menu* menu) {
			appendExampleItems(menu, asset::plugin(pluginInstance, "presets/MidiKit/Lua"), ".lua");
		}));
		menu->addChild(createMenuItem("Clear", "", [=]() { module->clearScript(); }));
		menu->addChild(createMenuItem("Paste from clipboard", RACK_MOD_ALT_NAME "+V", [=]() { pasteJsClipboard(); }));
		menu->addChild(createMenuItem("Copy to clipboard", RACK_MOD_ALT_NAME "+C", [=]() { copyJsClipboard(); }));
		menu->addChild(createMenuItem("Load", RACK_MOD_ALT_NAME "+L", [=]() { loadJsDialog(); }));
		menu->addChild(createMenuItem("Reload", RACK_MOD_ALT_NAME "+Y", [=]() { loadJs(filename); }, filename.empty()));
		menu->addChild(createMenuItem("Save as", "", [=]() { saveScriptDialog(); }));
	}

	int nextOverlayMessageId() override {
		if (module->log.overlayQueue.empty())
			return -1;
		return module->log.overlayQueue.shift();
	}

	void getOverlayMessage(int id, OverlayMessageProvider::Message& m) override {
		m.title = std::get<0>(module->log.overlayMessage);
		m.subtitle[0] = std::get<1>(module->log.overlayMessage);
		m.subtitle[1] = std::get<2>(module->log.overlayMessage);
	}

	void loadJsDialog() {
		osdialog_filters* filters = osdialog_filters_parse("MIDI-KIT file:js,lua");
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

		filename = path;
		loadJs(path);
	}

	void loadJs(std::string filename) {
		resetLog();

		// Read file
		std::ifstream file;
		file.exceptions(std::ifstream::failbit | std::ifstream::badbit);
		try {
			file.open(filename);
			std::stringstream buffer;
			buffer << file.rdbuf();
			std::string script = buffer.str();
			module->loadScript(script);
		}
		catch (const std::runtime_error& err) {
			// Fail silently
		}
	}

	// Returns true if dir (or any of its subfolders, recursively) contains at
	// least one script file with the given extension. Used to avoid creating
	// empty submenus for folders that hold no scripts of the active engine.
	bool hasExampleScripts(std::string dir, std::string ext) {
		if (!system::isDirectory(dir)) return false;
		for (std::string path : system::getEntries(dir)) {
			if (system::isDirectory(path)) {
				if (hasExampleScripts(path, ext)) return true;
			}
			else if (system::getExtension(path) == ext) {
				return true;
			}
		}
		return false;
	}

	// Lists .js/.lua example scripts bundled under src/modules/midikit/, sorted,
	// as clickable menu items (mirrors ModuleWidget's factory-preset submenu, but
	// for raw scripts). Subfolders become nested submenus, recursing arbitrarily
	// deep. All other file types in those folders (.cpp, .h, .md, ...) are
	// ignored. Subfolders are listed before files within each directory.
	void appendExampleItems(Menu* menu, std::string dir, std::string ext) {
		bool hasExamples = false;
		if (system::isDirectory(dir)) {
			std::vector<std::string> entries = system::getEntries(dir);
			std::sort(entries.begin(), entries.end());
			// Subfolders first (sorted)
			for (std::string path : entries) {
				if (!system::isDirectory(path)) continue;
				if (!hasExampleScripts(path, ext)) continue;
				hasExamples = true;
				std::string name = system::getFilename(path);
				menu->addChild(createSubmenuItem(name, "", [=](Menu* menu) {
					appendExampleItems(menu, path, ext);
				}));
			}
			// Files second (sorted)
			for (std::string path : entries) {
				if (system::isDirectory(path)) continue;
				if (system::getExtension(path) != ext) continue;
				hasExamples = true;
				std::string name = system::getStem(path);
				menu->addChild(createMenuItem(name, "", [=]() {
					filename = path;
					loadJs(path);
				}));
			}
		}
		if (!hasExamples) {
			menu->addChild(createMenuLabel("None found"));
		}
	}

	void saveScriptDialog() {
		if (module->host.script == "")
			return;

		std::string dir = asset::userDir;
		std::string filename = "script.js";
		char* newPathC = osdialog_file(OSDIALOG_SAVE, dir.c_str(), filename.c_str(), NULL);
		if (!newPathC) {
			return;
		}
		std::string newPath = newPathC;
		std::free(newPathC);
		// Add extension if user didn't specify one
		std::string newExt = system::getExtension(system::getFilename(newPath));
		if (newExt == "") newPath += ".js";

		// Write and close file
		{
			std::ofstream f(newPath);
			f << module->host.script;
		}
	}

	void onPathDrop(const event::PathDrop& e) override {
		if (module && e.paths.size() > 0) {
			loadJs(e.paths[0]);
			e.consume(this);
		}
		ThemedModuleWidget<MidiKitModule>::onPathDrop(e);
	}

	void onHoverKey(const event::HoverKey& e) override {
		if (e.action == GLFW_PRESS && (e.mods & RACK_MOD_MASK) == GLFW_MOD_ALT) {
			if (e.keyName == "c") {
				copyJsClipboard();
				e.consume(this);
			}
			if (e.keyName == "v") {
				pasteJsClipboard();
				e.consume(this);
			}
			if (e.keyName == "l") {
				loadJsDialog();
				e.consume(this);
			}
			if (e.keyName == "y") {
				if (!filename.empty()) {
					loadJs(filename);
				}
				e.consume(this);
			}
		}
		ThemedModuleWidget<MidiKitModule>::onHoverKey(e);
	}

	void pasteJsClipboard() {
		const char* script = glfwGetClipboardString(APP->window->win);
		module->loadScript(script);
	}

	void copyJsClipboard() {
		const char* script = module->host.script.c_str();
		glfwSetClipboardString(APP->window->win, script);
	}
};


} // namespace MidiKit
} // namespace StoermelderPackOne

Model* modelMidiKit = createModel<StoermelderPackOne::MidiKit::MidiKitModule, StoermelderPackOne::MidiKit::MidiKitWidget>("MidiKit");