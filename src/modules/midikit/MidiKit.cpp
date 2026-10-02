#include "MidiScriptEngine.hpp"
#include "MidiScriptEngineLua.hpp"
#include "MidiScriptEngineQuickJs.hpp"
#include "../../components/Knobs.hpp"
#include "../../components/MidiWidget.hpp"
#include "../../components/LedTextField.hpp"
#include "../../ui/OverlayMessageWidget.hpp"
#include "../../vcv/ui.hpp"
#include "../../vcv/fs.hpp"
#include "../../vcv/engine.hpp"
#include "../../utils/MpmcTaskWorker.hpp"
#include "../midi/MidiProcessor.hpp"
#include "tipsy-encoder/include/tipsy/tipsy.h"
#include <atomic>
#include <queue>

namespace StoermelderPackOne {
namespace MidiKit {


enum class LOG_FORMAT {
	RESET,
	TIMESTAMP,
	INDENTED,
	TEXT
};

// TPORTS is the number of trigger inputs of the module, one tick clock each.
template <int TPORTS = 1>
struct MidiOutput : midi::Output {
	struct FrameSchedule {
		midi::Message msg;
		// Send order, so messages sharing a frame keep it: std::priority_queue
		// is not stable, and a group (NRPN, 14-bit CC) shares one frame.
		uint64_t seq;
		bool operator<(const FrameSchedule& other) const {
			if (msg.frame != other.msg.frame) return msg.frame > other.msg.frame;
			return seq > other.seq;
		}
	};

	struct TickSchedule {
		midi::Message msg;
		uint64_t tick;
		uint64_t seq;
		bool operator<(const TickSchedule& other) const {
			if (tick != other.tick) return tick > other.tick;
			return seq > other.seq;
		}
	};

	std::priority_queue<FrameSchedule> frameQueue;
	uint64_t nextSeq = 0;

	// The module's midiOut.enableTiming() flag; null or clear = legacy mode.
	const std::atomic<bool>* timing = nullptr;
	int64_t lastHandOffFrame = -1;
	// Set (by the module) when the script asked for late messages to be reported.
	const std::atomic<bool>* reportLate = nullptr;
	// Messages handed over too late since the module last took the count, and the
	// worst of them in frames. Audio thread.
	uint32_t lateCount = 0;
	int64_t lateWorst = 0;

	bool timingOn() const {
		return timing != nullptr && timing->load(std::memory_order_relaxed);
	}

	// Rack's output thread places a framed message one block after its frame, so
	// once the block being processed starts a whole block past `f` the message is
	// already late and goes out at once. Counted against the block start, which
	// never flags a message that still has time.
	void countIfLate(int64_t f) {
		int64_t late = vcv::engine::getBlockFrame() - (f + vcv::engine::getBlockFrames());
		if (late <= 0) return;
		lateCount++;
		if (late > lateWorst) lateWorst = late;
	}

	// Timing mode: where a message is handed to Rack, which schedules it by
	// frame. Two rules keep order through Rack's unstable queue:
	//  - never hand over -1 (it bypasses the queue and overtakes framed
	//    messages), use the current frame instead;
	//  - frames are strictly increasing per output, so equal frames are moved
	//    one sample apart.
	void handOff(midi::Message& msg, int64_t now) {
		int64_t f = msg.frame < 0 ? now : msg.frame;
		if (f <= lastHandOffFrame) f = lastHandOffFrame + 1;
		lastHandOffFrame = f;
		msg.frame = f;
		if (reportLate != nullptr && reportLate->load(std::memory_order_relaxed)) countIfLate(f);
		sendMessage(msg);
	}

	// One tick queue per (trigger input, polyphonic channel), flattened as
	// trigPort * PORT_MAX_CHANNELS + channel: sendAfterTrigger() schedules a
	// message against a specific trigger input channel's clock, and only that
	// clock advancing can flush it. The first trigger input's channels come
	// first, so tickQueue[channel] is trigger input 1.
	std::priority_queue<TickSchedule> tickQueue[TPORTS * PORT_MAX_CHANNELS];

	static int tickQueueIndex(uint8_t channel, int trigPort) {
		if (trigPort < 0 || trigPort >= TPORTS) trigPort = 0;
		return trigPort * PORT_MAX_CHANNELS + (channel < PORT_MAX_CHANNELS ? channel : 0);
	}

	std::vector<int> getChannels() override {
		std::vector<int> channels;
		for (int c = -1; c < 16; c++) {
			channels.push_back(c);
		}
		return channels;
	}

	// Drops every message waiting for a trigger tick.
	void clearTickQueues() {
		for (int i = 0; i < TPORTS * PORT_MAX_CHANNELS; i++) {
			while (!tickQueue[i].empty()) tickQueue[i].pop();
		}
	}

	void reset() {
		Output::reset();
		while (!frameQueue.empty()) frameQueue.pop();
		clearTickQueues();
		lastHandOffFrame = -1;
		lateCount = 0;
		lateWorst = 0;
		channel = -1;
	}

	// `now`: the current engine frame, for frame-less messages in timing mode.
	void send(midi::Message& msg, uint8_t channel, uint64_t tick, int trigPort = 0, int64_t now = -1) {
		if (tick != 0) {
			TickSchedule s;
			s.msg = msg;
			s.tick = tick;
			s.seq = nextSeq++;
			tickQueue[tickQueueIndex(channel, trigPort)].push(s);
			return;
		}

		// Timing mode: a frame-less message takes the current frame and is queued
		// like the rest, so (frame, seq) keeps it behind earlier messages that are
		// still waiting for the end of this pump.
		if (msg.frame < 0 && now >= 0 && timingOn()) {
			msg.frame = now;
		}

		if (msg.frame != -1) {
			FrameSchedule s;
			s.msg = msg;
			s.seq = nextSeq++;
			frameQueue.push(s);
			return;
		}

		if (timingOn()) {
			handOff(msg, now);
		}
		else {
			sendMessage(msg);
		}
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
				if (timingOn()) {
					handOff(s.msg, frame);
				}
				else {
					s.msg.frame = -1;
					sendMessage(s.msg);
				}
			}
			else {
				return;
			}
		}
	}

	void processTick(uint8_t channel, uint64_t tick, int trigPort = 0, int64_t now = -1) {
		// Each (trigger input, channel) queue is only ever drained by that
		// clock — a message scheduled against channel N of trigger input P must
		// not fire on another channel's or input's trigger, so its queue is
		// touched only when that clock fires.
		auto& q = tickQueue[tickQueueIndex(channel, trigPort)];
		while (true) {
			if (q.size() == 0) return;
			TickSchedule s = q.top();
			// ">=" and not "==": process() calls processTick() before draining the
			// engine's out-queue, so a script can schedule for a tick the counter has
			// already consumed. With "==" such a message is never sent and, since the
			// queue is ordered smallest-tick-first, it blocks every later one behind it.
			if (tick >= s.tick) {
				q.pop();
				if (timingOn()) {
					// Drop the stale arrival frame; it is due now.
					s.msg.frame = -1;
					handOff(s.msg, now);
				}
				else {
					sendMessage(s.msg);
				}
			}
			else {
				return;
			}
		}
	}
};


// Extended-CC input enables, one per module
// Per-MIDI-channel bitmasks of what the script asked to have assembled
// (midi.enableNrpnIn/enableRpnIn/enableCc14bitIn). Bit c = MIDI channel c;
// "all channels" sets every bit. Nothing is assembled for the script until it
// asks, matching trig.enableIn()/trig.enableTipsyIn().
// Threading: the masks are atomic because the worker thread writes them from
// the enable bindings while the audio thread reads them in MidiInputs::Port::accepts().
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
	// The controller ranges overlap and that is deliberate: CC 0-31 are 14-bit
	// MSBs, and CC 6/38 are simultaneously Data Entry for an
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


// Script log + overlay, one per module
// Everything the module tells the widget about: the runtime log and the
// current overlay message. Touched from three threads, which is why the
// contract is stated here once rather than inferred from call sites:
//   - worker thread: writeLog()/writeOverlay() produce log entries + overlay;
//   - audio thread: overflow reporting in process() produces log entries;
//   - loadScript()/onReset() callers produce RESET markers and "No script";
//   - UI thread: the widget drains the log (midiLogMessages.try_pop) and the
//     overlay ring (nextOverlayMessageId/getOverlayMessage).
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

	// Plain text line. The timestamp is not displayed.
	void pushText(const std::string& text, float timestamp = 0.f) {
		push(LOG_FORMAT::TEXT, timestamp, text);
	}

	// Line prefixed with the timestamp (seconds).
	void pushTimestamped(float timestamp, const std::string& text) {
		push(LOG_FORMAT::TIMESTAMP, timestamp, text);
	}

	// Marks a script load/reset; the widget clears its display on it.
	void pushReset() {
		push(LOG_FORMAT::RESET, 0.f, std::string(""));
	}

	// Worker side — writeOverlay(). Marks one overlay slot with the current
	// message.
	void pushOverlay(const std::string& s1, const std::string& s2, const std::string& s3) {
		overlayQueue.push(0);
		overlayMessage = std::make_tuple(s1, s2, s3);
	}
};


// MIDI inputs: ports, decoders and the script's extended-CC enables ──────
// The input side of the module. Each port has its queue, the decoder turning
// its stream into semantic events (NRPN/RPN/14-bit CC assembly) and the
// extended-CC enables the script set for it.
// Threading: the worker calls enable*(); the audio thread calls pump(),
// discard() and resetDecoders(). The UI thread must not touch the decoders.
// A decoded message that passes the filter is handed to `onMessage`, so this
// struct never sees the script host. Set it once, before the first pump.
template <int NIN>
struct MidiInputs {
	// MUST keep the queue declared before the processor, so the queue outlives
	// it. The queue is injected rather than owned: it keeps its widget binding
	// and JSON. Only processMessage() is used, never process(): decoded messages
	// are queued for the worker, not dispatched inline.
	struct Port : MidiProcessorHandler {
		/** [Stored to Json] */
		midi::InputQueue queue;
		MidiProcessor processor{&queue};
		ExtendedCcEnables extendedCc;

		MidiInputs* owner = nullptr;
		int index = 0;

		// Whether a decoded message should reach the script.
		//
		// A CC belonging to an extended message is notified TWICE: once as
		// Type::CC (isComponent set) and again as NRPN/RPN/CC_14BIT once
		// assembled. What the script asked for decides which it sees:
		//  - assembled events pass only when the matching enable is set;
		//  - a raw component CC is dropped when the script enabled the kind of
		//    assembly it belongs to. isComponent alone must not decide: a script
		//    that enabled only 14-bit CC still wants to see CC 98 raw.
		bool accepts(const MessageEx& m) const {
			switch (m.type) {
				case MessageEx::Type::NRPN:
				case MessageEx::Type::RPN: {
					bool isRpn = (m.type == MessageEx::Type::RPN);
					// Parameter-select notifications carry no value (extraValue < 0),
					// and the RPN 127/127 reset carries paramNumber < 0. Neither is a
					// parameter change.
					if (!m.hasValue() || m.getParamNumber() < 0) return false;
					return extendedCc.isNrpnEnabled(m.getChannel(), isRpn);
				}
				case MessageEx::Type::CC_14BIT:
					return extendedCc.isCc14bitEnabled(m.getChannel(), uint8_t(m.getParamNumber()));
				case MessageEx::Type::CC:
					return !(m.isComponent && extendedCc.isComponentEnabled(m));
				default:
					return true;
			}
		}

		// MidiProcessorHandler. Audio thread, synchronously from processMessage().
		bool processMidi(const MessageEx& m) override {
			return owner->dispatch(index, m);
		}
	};

	Port ports[NIN];

	// Inputs in use are ports 0..count-1 (at least 1), raised by the script's
	// midi.enablePorts(). Written by the worker, read by the audio thread.
	std::atomic<int> count{1};

	using Sink = std::function<void(int port, const MidiScript::QueuedMessage&)>;
	Sink onMessage;

	MidiInputs() {
		// Without this the processor decodes into an empty handler list and
		// nothing reaches the script.
		for (int i = 0; i < NIN; i++) {
			ports[i].owner = this;
			ports[i].index = i;
			ports[i].processor.subscribe(&ports[i]);
		}
	}

	// ── Enables (worker thread) ──
	void enablePorts(int n) {
		if (n > NIN) n = NIN;
		if (n > count.load(std::memory_order_relaxed)) count.store(n, std::memory_order_relaxed);
	}
	int enabledCount() const {
		return count.load(std::memory_order_relaxed);
	}
	bool isEnabled(int port) const {
		return port < count.load(std::memory_order_relaxed);
	}
	void enableNrpn(int port, int kind, int channel) {
		if (port < 0 || port >= NIN) return;
		ports[port].extendedCc.enableNrpn(kind, channel);
	}
	void enableCc14bit(int port, int cc, int channel) {
		if (port < 0 || port >= NIN) return;
		ports[port].extendedCc.enableCc14bit(cc, channel);
	}
	// Audio thread.
	bool isNrpnEnabled(uint8_t ch, bool isRpn, int port = 0) const {
		return ports[port].extendedCc.isNrpnEnabled(ch, isRpn);
	}
	bool isCc14bitEnabled(uint8_t ch, uint8_t cc, int port = 0) const {
		return ports[port].extendedCc.isCc14bitEnabled(ch, cc);
	}

	// Back to "only port 1 in use" — what a script starts with.
	void resetEnables() {
		count.store(1, std::memory_order_relaxed);
	}
	// Forgets every extended-CC enable: they belong to the script, not the module.
	void clearExtendedCc() {
		for (int i = 0; i < NIN; i++) ports[i].extendedCc.clear();
	}

	// Audio thread. Decodes the messages due at `frame` on the ports in use. The
	// others are only emptied: a device selected on an unused input must not pile
	// up messages that would flood the script once it is enabled.
	void process(int64_t frame) {
		midi::Message msg;
		int n = count.load(std::memory_order_relaxed);
		for (int i = 0; i < n; i++) {
			while (ports[i].queue.tryPop(&msg, frame)) {
				ports[i].processor.processMessage(msg);
			}
		}
		for (int i = n; i < NIN; i++) {
			while (ports[i].queue.tryPop(&msg, frame)) {}
		}
	}

	// Audio thread. Empties every queue without decoding (bypass).
	void processBypass(int64_t frame) {
		midi::Message msg;
		for (int i = 0; i < NIN; i++) {
			while (ports[i].queue.tryPop(&msg, frame)) {}
		}
	}

	// Audio thread. Drops the half-received NRPN/RPN/14-bit CC state of every
	// port; the decoders are not thread-safe, so the UI thread must not do this.
	void resetDecoders() {
		for (int i = 0; i < NIN; i++) ports[i].processor.reset();
	}

	// Module reset. The emptied queue leaves the stream discontinuous, so the
	// decoder state goes with it: a parameter still armed from before would
	// capture the next data entry.
	void reset() {
		for (int i = 0; i < NIN; i++) {
			ports[i].queue.reset();
			ports[i].processor.reset();
			ports[i].extendedCc.clear();
		}
	}

	// Audio thread, from a port's processMessage(): a pure enqueue — script code
	// runs on the worker and must never be entered from here.
	bool dispatch(int port, const MessageEx& m) {
		if (!isEnabled(port) || !ports[port].accepts(m)) return false;
		MidiScript::QueuedMessage q;
		q.msg = m.msg;
		q.type = m.type;
		q.paramNumber = m.paramNumber;
		q.extraValue = m.extraValue;
		q.isComponent = m.isComponent;
		q.frame = m.frame;
		if (onMessage) onMessage(port, q);
		// false keeps the message available to any other handler.
		return false;
	}
};


// MIDI outputs: ports, timing mode and the worker → audio hand-off
// The output side of the module: ports, port enables, the worker -> audio
// queue and the timing mode with its late-message report.
// Threading: the worker calls enqueue() and enable*(); the audio thread calls
// drain(), onTick() and flush(). The queue is owned here, not by an engine, so
// it outlives engine switches and clearScript().
// NOUT = MIDI outputs, TPORTS = trigger inputs (one tick clock each). Frame and
// sample rate are passed in; log lines go to the ScriptLog given at construction.
template <int NOUT, int TPORTS>
struct MidiOutputs {
	using Port = MidiOutput<TPORTS>;
	// port, message, channel, tick, trigger input
	using Entry = std::tuple<int, MidiScript::Message, uint8_t, uint64_t, int>;

	/** [Stored to Json] */
	Port ports[NOUT];

	std::atomic<int> count{1};
	// Bit per port already reported as dropping messages (log once).
	std::atomic<uint32_t> dropLogged{0};
	// Worker -> audio hand-off.
	dsp::RingBuffer<Entry, 128> queue;
	// Set by enqueue() on a full queue; cleared and logged by drain().
	std::atomic<bool> overflow{false};
	// midiOut.enableTiming(reportLate). Worker writes, audio thread reads.
	std::atomic<bool> timingEnabled{false};
	std::atomic<bool> timingReportLate{false};
	// Audio thread: frame of the last late-message log line, -1 for none.
	int64_t timingLateLoggedAt = -1;
	ScriptLog* log;

	explicit MidiOutputs(ScriptLog* log) : log(log) {
		for (int i = 0; i < NOUT; i++) {
			ports[i].timing = &timingEnabled;
			ports[i].reportLate = &timingReportLate;
		}
	}

	// Port enables
	// Outputs in use are ports 0..count-1 (at least 1), raised by the script's
	// midiOut.enablePorts(). The per-sample loops run to this count.
	void enablePorts(int n) {
		if (n > NOUT) n = NOUT;
		if (n > count.load(std::memory_order_relaxed)) count.store(n, std::memory_order_relaxed);
	}
	int enabledCount() const {
		return count.load(std::memory_order_relaxed);
	}
	bool isEnabled(int port) const {
		return port < count.load(std::memory_order_relaxed);
	}

	// Timing mode
	// Outgoing messages keep their frame (see MidiOutput::handOff()).
	void enableTiming(bool reportLate) {
		timingReportLate.store(reportLate, std::memory_order_relaxed);
		timingEnabled.store(true, std::memory_order_relaxed);
	}
	bool isTimingEnabled() const {
		return timingEnabled.load(std::memory_order_relaxed);
	}

	// What a script starts with: port 1 only, no timing mode.
	void resetEnables() {
		count.store(1, std::memory_order_relaxed);
		timingEnabled.store(false, std::memory_order_relaxed);
		timingReportLate.store(false, std::memory_order_relaxed);
		dropLogged.store(0, std::memory_order_relaxed);
	}

	// Worker. Queues a group of messages; false (group dropped) for an unknown or
	// disabled port, or a full queue.
	bool enqueue(int port, const MidiScript::Message* msgs, size_t n, uint8_t channel, uint64_t tick, int trigPort = 0) {
		if (port < 0 || port >= NOUT) return false;
		if (!isEnabled(port)) {
			// Once per port, not per message.
			uint32_t bit = uint32_t(1) << port;
			if (!(dropLogged.fetch_or(bit) & bit)) {
				log->pushText(string::f("MIDI output %d is not enabled, message(s) dropped; call midiOut.enablePorts(%d)", port + 1, port + 1));
			}
			return false;
		}
		// Whole group or nothing, so an NRPN quad or 14-bit CC pair is never
		// half-emitted. RingBuffer::push() does not bounds-check.
		if (queue.capacity() < n) {
			overflow.store(true, std::memory_order_relaxed);
			return false;
		}
		for (size_t i = 0; i < n; i++) {
			queue.push(std::make_tuple(port, msgs[i], channel, tick, trigPort));
		}
		return true;
	}

	// Audio thread. Queued messages go to their ports, then due frame-scheduled
	// ones are sent. Independent of the active engine, so a cleared script's
	// onUnload() output still reaches the device. Call after the script dispatch.
	void process(int64_t frame, float sampleRate) {
		// Once per drain: a saturated output must not flood the log.
		if (overflow.exchange(false, std::memory_order_relaxed)) {
			log->pushText("MIDI output queue full, message(s) dropped");
		}
		while (!queue.empty()) {
			Entry t = queue.shift();
			midi::Message msg = std::get<1>(t);
			ports[std::get<0>(t)].send(msg, std::get<2>(t), std::get<3>(t), std::get<4>(t), frame);
		}
		// All ports, not just enabled ones: a replaced script may have left framed
		// messages behind.
		for (int i = 0; i < NOUT; i++) ports[i].processFrame(frame);
		reportLateMessages(frame, sampleRate);
	}

	// Audio thread. A trigger channel's clock ticked: releases its
	// sendAfterTrigger() messages on every output in use.
	void onTick(uint8_t channel, uint64_t tick, int trigPort, int64_t frame) {
		for (int o = 0, n = count.load(std::memory_order_relaxed); o < n; o++) {
			ports[o].processTick(channel, tick, trigPort, frame);
		}
	}

	// Audio thread. Drops messages waiting for a tick; the counters restart with
	// a new script.
	void clearTickQueues() {
		for (int i = 0; i < NOUT; i++) ports[i].clearTickQueues();
	}

	// Teardown: sends what is left in the queue immediately, ignoring frame/tick
	// scheduling. Only after the worker has stopped.
	void flush() {
		while (!queue.empty()) {
			Entry t = queue.shift();
			midi::Message msg = std::get<1>(t);
			msg.frame = -1;
			// Immediate in timing mode too: Rack's output queue may be dropped
			// with the device.
			ports[std::get<0>(t)].sendMessage(msg);
		}
	}

	// Every port back to its initial state.
	void reset() {
		for (int i = 0; i < NOUT; i++) ports[i].reset();
		timingLateLoggedAt = -1;
	}

	// Audio thread. Logs late messages, at most once per second of audio.
	void reportLateMessages(int64_t frame, float sampleRate) {
		if (!timingReportLate.load(std::memory_order_relaxed)) return;
		uint32_t late = 0;
		int64_t worst = 0;
		int n = count.load(std::memory_order_relaxed);
		for (int i = 0; i < n; i++) {
			late += ports[i].lateCount;
			worst = std::max(worst, ports[i].lateWorst);
		}
		if (late == 0 || sampleRate <= 0.f) return;
		if (timingLateLoggedAt >= 0 && frame - timingLateLoggedAt < int64_t(sampleRate)) return;

		for (int i = 0; i < n; i++) {
			ports[i].lateCount = 0;
			ports[i].lateWorst = 0;
		}
		timingLateLoggedAt = frame;
		log->pushText(string::f("Timing: %u message(s) reached the output too late, worst by %.1f ms", late, 1000.f * float(worst) / sampleRate));
	}
};


// Tipsy output: encoding onto the trigger CV, one per module
// Encodes queued Tipsy messages onto the trigger CV output. The worker enqueues
// (send/reset); the audio thread encodes one float per process() call.
struct TipsyOutput {
	using TipsyMessage = MidiScript::TipsyMessage;

	// What process() did on one call (audio thread).
	enum class Output {
		IDLE,         // no message in flight; nothing written
		WROTE,        // one encoded float written to the out voltage
		INIT_ERROR,   // initiateMessage() failed; see lastInitErrorCode
		ENCODE_ERROR  // getNextMessageFloat() failed; message terminated
	};

	// Audio thread only.
	tipsy::ProtocolEncoder encoder;

	// SPSC queue of pending messages (worker -> audio); only the audio thread
	// encodes. Never cleared (clear() writes the consumer's index): discarding
	// goes through discardCount and a queued sentinel instead.
	dsp::RingBuffer<TipsyMessage, 8> outQueue;

	// Discard sentinels enqueued (worker) and consumed (discardSeen, audio).
	// A counter, not a flag, so two quick reloads discard both batches; the
	// queued sentinel marks where the new script's messages begin.
	std::atomic<uint32_t> discardCount{0};
	uint32_t discardSeen = 0;

	// The message being streamed out. The encoder points into it for the whole
	// message, so it is a member; only overwritten while the encoder is dormant.
	TipsyMessage currentMessage;

	// Result code of the last INIT_ERROR, for the log line.
	int lastInitErrorCode = 0;

	// Worker. Keeps the last slot free for reset()'s sentinel; false when full.
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

	// Worker. Queues a discard sentinel.
	void reset() {
		// Sentinel first, so the raised count never outruns it.
		TipsyMessage p;
		p.mimeSize = 0;
		p.dataSize = 0;
		outQueue.push(p);
		discardCount.fetch_add(1, std::memory_order_relaxed);
	}

	// Audio thread. If idle, drops stale messages and starts the next one, then
	// encodes one float (WROTE sets voltageOut).
	Output process(float& voltageOut) {
		if (encoder.isDormant()) {
			// Everything ahead of an unconsumed sentinel is from a replaced script.
			// Only while dormant, so a message already going out completes.
			while (discardSeen < discardCount.load(std::memory_order_relaxed) && !outQueue.empty()) {
				if (outQueue.shift().mimeSize == 0) discardSeen++;
			}

			if (!outQueue.empty()) {
				// Into the member the encoder points into.
				currentMessage = outQueue.shift();
				auto initResult = encoder.initiateMessage(currentMessage.mime, currentMessage.dataSize, currentMessage.data);
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
};

// Tipsy input: decoding the trigger CV, one per module
// Decodes the trigger CV input into messages. The worker claims the input; the
// audio thread feeds one sample per process() call, completed messages go to a
// callback and drops are reported to the ScriptLog.
struct TipsyInput {
	using TipsyMessage = MidiScript::TipsyMessage;

	// Trigger input carrying the stream, -1 when disabled. Worker writes, audio reads.
	std::atomic<int> inPort{-1};

	// Audio thread only.
	tipsy::ProtocolDecoder decoder;

	// Payload store of the decoder (see provideDataBuffer()).
	unsigned char buffer[MidiScript::tipsyMaxPayloadLength];

	// Completed message, copied out of the decoder's reusable buffer.
	TipsyMessage currentMessage;

	ScriptLog* log = nullptr;

	// Audio thread. Logging is edge-triggered: an episode spanning many samples
	// logs once, on its rising edge.
	bool overflowActive = false;
	bool errorActive = false;

	TipsyInput(ScriptLog* log) : log(log) {}

	// Worker, midi.enableTipsyIn(). -1 releases the input.
	void claim(int port) {
		inPort.store(port, std::memory_order_relaxed);
	}

	// Audio thread, every sample.
	int claimed() const {
		return inPort.load(std::memory_order_relaxed);
	}

	// Audio thread. Feeds one sample; a completed message goes to enqueue(msg),
	// which returns false if it dropped it. Returns whether a message was
	// accepted. The decoder resyncs itself at the next message-begin sentinel.
	template <typename EnqueueFn>
	bool process(float voltage, EnqueueFn&& enqueue) {
		auto result = decoder.readFloat(voltage);

		if (tipsy::ProtocolDecoder::isError(result)) {
			reportError();
			return false;
		}
		// A clean sample ends the episode, so a new one can log again.
		errorActive = false;

		if (result != tipsy::ProtocolDecoder::DecoderResult::BODY_READY) return false;

		// Payload complete; copy out before the decoder reuses the buffer.
		size_t mimeSize = strnlen(decoder.getMimeType(), MidiScript::tipsyMaxMimeTypeSize - 1);
		uint32_t dataSize = decoder.getDataSize();
		if (dataSize > MidiScript::tipsyMaxPayloadLength) {
			// Defensive: the decoder should have rejected it already.
			reportError();
			return false;
		}
		TipsyMessage p;
		p.mimeSize = (uint16_t)mimeSize;
		p.dataSize = (uint16_t)dataSize;
		std::memcpy(p.mime, decoder.getMimeType(), mimeSize);
		p.mime[mimeSize] = '\0';
		std::memcpy(p.data, buffer, dataSize);
		currentMessage = p;

		if (!enqueue(currentMessage)) {
			reportOverflow();
			return false;
		}
		overflowActive = false;
		return true;
	}

	void reportError() {
		if (errorActive) return;
		errorActive = true;
		if (log) log->pushText("Tipsy input: malformed stream");
	}

	void reportOverflow() {
		if (overflowActive) return;
		overflowActive = true;
		if (log) log->pushText("Tipsy input queue full, message(s) dropped");
	}

	// Releases the claim and re-arms the data store (nothing decodes at reset).
	// The output side is untouched: stale output goes via the sentinel protocol.
	void reset() {
		inPort.store(-1, std::memory_order_relaxed);
		decoder.provideDataBuffer(buffer, sizeof(buffer));
	}
};


// Module variants
// A MidiKit variant is a Config struct with the port/param counts of the
// module. MidiKitModuleBase is templated over it. The widget base class
// MidiKitWidgetBase holds everything but the layout: a variant derives from it,
// adds its own controls in its constructor (see MidiKitWidget) and is
// registered with its own panel, model and plugin.json entry.
// Config contract:
//   static constexpr int cvInputs, trigInputs, trigOutputs, params,
//                        midiInputs, midiOutputs;
// The trigger and MIDI input/output counts must be at least 1 (Tipsy always
// runs on trigger input/output 0, and the module aliases MIDI port 0); the CV
// input and param counts may be zero.

// Port/param counts injected into the script engines at construction.
struct PortCounts {
	int cvInputs;
	int trigInputs;
	int trigOutputs;
	int params;
	int midiInputs;
	int midiOutputs;
};

// The original single-panel MidiKit: 4 CV inputs, 4 params, 1 trigger in/out,
// 1 MIDI in/out.
struct MidiKitConfig {
	static constexpr int cvInputs = 4;
	static constexpr int trigInputs = 2;
	static constexpr int trigOutputs = 2;
	static constexpr int params = 4;
	static constexpr int midiInputs = 4;
	static constexpr int midiOutputs = 4;
};

// MidiKitMicro: MidiKit with 2 CV inputs and 2 params, without a log display.
struct MidiKitMicroConfig {
	static constexpr int cvInputs = 2;
	static constexpr int trigInputs = 2;
	static constexpr int trigOutputs = 2;
	static constexpr int params = 2;
	static constexpr int midiInputs = 4;
	static constexpr int midiOutputs = 4;
};


// Script host: engines + live script, one per module
// Owns the two engines, which one is live, and the script source. Every call
// into script code goes through here, so the "active engine?" check lives in
// one place.
// Threading: a loaded engine belongs to the worker; the audio thread only
// enqueues into its SPSC queues (queueMessage/queueTick) and calls process().
// load()/closeState() run on the UI thread and BLOCK, so only one engine ever
// has outstanding worker tasks.
struct ScriptHost {
	// The engine running the loaded script, or null. Written only by
	// load()/closeState().
	MidiScript::MidiScriptEngine* activeEngine = nullptr;

	// Only one is ever loaded; the other is closed on switch. Widget/tests reach
	// them directly.
	MidiScript::Lua::MidiScriptEngineLua seLua;
	MidiScript::QuickJs::MidiScriptEngineQuickJs seQuickJs;

	/** [Stored to JSON] */
	std::string script = "";

	ScriptHost(MidiScript::MidiScriptEngineHandler* handler, const PortCounts& c)
		: seLua(handler, c.cvInputs, c.trigInputs, c.trigOutputs, c.params, c.midiInputs, c.midiOutputs),
		  seQuickJs(handler, c.cvInputs, c.trigInputs, c.trigOutputs, c.params, c.midiInputs, c.midiOutputs) {}

	// UI thread: wires the shared worker into both engines.
	void setWorker(std::shared_ptr<ITaskWorker> worker) {
		seLua.setWorker(worker);
		seQuickJs.setWorker(worker);
	}

	MidiScript::MidiScriptEngine* getActiveEngine() const {
		return activeEngine;
	}
	// By reference, so tests can inject a mock engine.
	MidiScript::MidiScriptEngine*& getActiveEngine() {
		return activeEngine;
	}

	bool isLuaEngine() const {
		return activeEngine == &seLua;
	}
	bool isQuickJsEngine() const {
		return activeEngine == &seQuickJs;
	}

	// Selects the engine for `src`, closes the previous one (blocking) and loads
	// the script with `configJson` as its initial config (empty = fresh config,
	// not the previous script's). Returns the new engine, null if neither matches.
	// The module re-binds its port/param pointers afterwards.
	MidiScript::MidiScriptEngine* load(const std::string& src, const std::string& configJson) {
		script = src;
		MidiScript::MidiScriptEngine* prevEngine = activeEngine;
		activeEngine = nullptr;
		if (seLua.testScript(src)) activeEngine = &seLua;
		if (seQuickJs.testScript(src)) activeEngine = &seQuickJs;

		// Blocking: afterwards activeEngine is the only engine with state or
		// worker tasks, which onRemove()/onReset() rely on.
		if (prevEngine && prevEngine != activeEngine) prevEngine->closeState();

		if (activeEngine) activeEngine->loadScript(script.c_str(), configJson);
		return activeEngine;
	}

	// Closes the active engine (blocking: runs onUnload() to completion) and
	// nulls the pointer. Rack holds the engine mutex across onRemove()/onReset(),
	// so process() cannot run concurrently.
	void closeState() {
		MidiScript::MidiScriptEngine* engine = activeEngine;
		activeEngine = nullptr;
		if (engine) engine->closeState();
	}

	// Audio-thread dispatch; no-ops when nothing is loaded.
	void queueMessage(int port, const MidiScript::QueuedMessage& msg) {
		if (activeEngine) activeEngine->processInMessage(port, msg);
	}
	void queueTick(int trigPort, uint8_t channel, int64_t frame = -1) {
		if (activeEngine) activeEngine->processInTick(trigPort, channel, frame);
	}
	// Audio thread: one pump of the active engine's queued work.
	void process() {
		if (activeEngine) activeEngine->process();
	}

	// UI thread: the active engine's last-published config, null if nothing is
	// loaded. A plain read by reference, no interpreter round-trip (see
	// MidiScriptEngine::peekConfig()); the static fallback keeps it a reference.
	const std::shared_ptr<json_t>& peekConfig() const {
		static const std::shared_ptr<json_t> none;
		return activeEngine ? activeEngine->peekConfig() : none;
	}
};


// Trigger inputs, one per module
// Per-port, per-channel SchmittTriggers and tick counters, gated by the
// script's enable mask. The worker writes the mask (trig.enableIn); the audio
// thread steps the triggers.
template <int TPORTS = 1>
struct TriggerInputs {
	// First of TPORTS consecutive trigger inputs; set once by the module.
	rack::engine::Input* input = nullptr;

	// One per (port, channel).
	dsp::SchmittTrigger trigger[TPORTS][PORT_MAX_CHANNELS];
	uint64_t triggerTick[TPORTS][PORT_MAX_CHANNELS];

	// Enabled channels by trig.enableIn(), bit c = channel c. Worker writes,
	// audio thread reads.
	std::atomic<uint16_t> enabledMask[TPORTS];

	// Worker, trig.enableIn().
	void enable(int port, uint8_t channel) {
		if (port < 0 || port >= TPORTS) return;
		if (channel >= PORT_MAX_CHANNELS) return;
		enabledMask[port].fetch_or(static_cast<uint16_t>(1) << channel, std::memory_order_relaxed);
	}

	bool isEnabled(int port, uint8_t channel) const {
		if (port < 0 || port >= TPORTS) return false;
		if (channel >= PORT_MAX_CHANNELS) return false;
		return (enabledMask[port].load(std::memory_order_relaxed) >> channel) & 1;
	}

	// Forgets the enabled channels and tick counters: they belong to the script.
	void reset() {
		for (int p = 0; p < TPORTS; p++) {
			for (int i = 0; i < PORT_MAX_CHANNELS; i++) triggerTick[p][i] = 0;
			enabledMask[p].store(0, std::memory_order_relaxed);
		}
	}

	// Audio thread, for trig.getTicks().
	uint64_t getTicks(int port, uint8_t channel) const {
		if (port < 0 || port >= TPORTS) return 0;
		if (channel >= PORT_MAX_CHANNELS) return 0;
		return triggerTick[port][channel];
	}

	// Audio thread. Steps the enabled channels and calls onTick(channel, tick)
	// per rising edge. While a Tipsy stream owns channel 0 (tipsyClaimed), it is
	// stepped but never fires.
	template <typename TickFn>
	void process(int port, int channels, bool tipsyClaimed, TickFn&& onTick) {
		for (uint8_t c = 0; c < channels; c++) {
			bool tipsyOnChannel = (c == 0) && tipsyClaimed;
			if (isEnabled(port, c) && trigger[port][c].process(input[port].getVoltage(c)) && !tipsyOnChannel) {
				triggerTick[port][c]++;
				onTick(c, triggerTick[port][c]);
			}
		}
	}
};

// Trigger outputs, one per module
// Per-port, per-channel pulse generators. The worker arms them
// (setGate/setVoltage); the audio thread steps them.
template <int TPORTS = 1>
struct TriggerOutputs {
	static constexpr int STAMPED_MAX = 32;

	// First of TPORTS consecutive trigger outputs; set once by the module.
	rack::engine::Output* output = nullptr;

	bool triggerActive[TPORTS][PORT_MAX_CHANNELS];
	dsp::PulseGenerator pulseGenerator[TPORTS][PORT_MAX_CHANNELS];

	// Stamped writes (timing mode, see MidiScriptEngine::frameForTrig()). The
	// worker pushes into stampedIn; the audio thread moves them into `pending`,
	// ordered by frame because events reach the worker out of frame order (MIDI
	// before triggers). A full ring or pending queue applies the write at once,
	// which is what an unstamped write does anyway.
	struct Stamped {
		int64_t frame;
		int port;
		uint8_t channel;
		bool gate;      // true: setGate(duration); false: setVoltage(value)
		float value;    // pulse length in seconds, or the voltage
		uint64_t seq;   // order of arrival, so equal frames keep the order written
	};
	struct StampedLater {
		bool operator()(const Stamped& a, const Stamped& b) const {
			return a.frame != b.frame ? a.frame > b.frame : a.seq > b.seq;
		}
	};
	// std::priority_queue over a vector reserved up front: the cap below keeps it
	// from ever reallocating on the audio thread.
	struct StampedQueue : std::priority_queue<Stamped, std::vector<Stamped>, StampedLater> {
		StampedQueue() {
			this->c.reserve(STAMPED_MAX);
		}
	};
	dsp::RingBuffer<Stamped, STAMPED_MAX> stampedIn;
	StampedQueue pending;
	uint64_t stampedSeq = 0;
	// reset() runs on the worker, but the ring's consumer side and `pending` belong
	// to the audio thread: reset() only raises this, processStamped() clears.
	std::atomic<bool> clearStampedRequested{false};

	// Worker. Widens the port to cover `channel`.
	void useChannel(int port, uint8_t channel) {
		if (channel + 1 > output[port].getChannels()) output[port].setChannels(channel + 1);
	}

	// Script load: back to mono.
	void resetChannels() {
		for (int p = 0; p < TPORTS; p++) output[p].setChannels(1);
	}

	// Worker, trig.setGate(): arms the pulse. `frame` >= 0 stamps the write: the
	// audio thread applies it when that frame comes up (see processStamped()).
	void setGate(int port, uint8_t channel, float duration, int64_t frame = -1) {
		if (port < 0 || port >= TPORTS || channel >= PORT_MAX_CHANNELS) return;
		if (frame >= 0 && !stampedIn.full()) {
			stampedIn.push(Stamped{frame, port, channel, true, duration, 0});
			return;
		}
		applyGate(port, channel, duration);
	}

	// Worker, trig.setHigh()/setLow(): the raw voltage the caller writes wins.
	// Stamped like setGate().
	void setVoltage(int port, uint8_t channel, float voltage, int64_t frame = -1) {
		if (port < 0 || port >= TPORTS || channel >= PORT_MAX_CHANNELS) return;
		if (frame >= 0 && !stampedIn.full()) {
			stampedIn.push(Stamped{frame, port, channel, false, voltage, 0});
			return;
		}
		applyVoltage(port, channel, voltage);
	}

	// Audio thread, once per sample before process(): takes in new stamped writes
	// and applies those due at `frame`, in frame order (same frame: in the order
	// they were written).
	void processStamped(int64_t frame) {
		if (clearStampedRequested.exchange(false, std::memory_order_acquire)) {
			while (!stampedIn.empty()) stampedIn.shift();
			while (!pending.empty()) pending.pop();
		}
		while (!stampedIn.empty()) {
			Stamped w = stampedIn.shift();
			if (pending.size() >= STAMPED_MAX) {
				apply(w);
				continue;
			}
			w.seq = stampedSeq++;
			pending.push(w);
		}
		while (!pending.empty() && pending.top().frame <= frame) {
			apply(pending.top());
			pending.pop();
		}
	}

	void applyGate(int port, uint8_t channel, float duration) {
		useChannel(port, channel);
		triggerActive[port][channel] = true;
		pulseGenerator[port][channel].trigger(duration);
	}

	void applyVoltage(int port, uint8_t channel, float voltage) {
		useChannel(port, channel);
		triggerActive[port][channel] = false;
		output[port].setVoltage(voltage, channel);
	}

	void apply(const Stamped& w) {
		if (w.gate) applyGate(w.port, w.channel, w.value);
		else applyVoltage(w.port, w.channel, w.value);
	}

	void reset() {
		for (int p = 0; p < TPORTS; p++) {
			for (uint8_t i = 0; i < PORT_MAX_CHANNELS; i++) {
				triggerActive[p][i] = true;
				pulseGenerator[p][i].reset();
			}
		}
		clearStampedRequested.store(true, std::memory_order_release);
		resetChannels();
	}

	// Audio thread, once per sample: applies the stamped writes due at `frame`,
	// then steps every connected output.
	void process(int64_t frame, float sampleTime) {
		processStamped(frame);
		for (int p = 0; p < TPORTS; p++) {
			if (!output[p].isConnected()) continue;
			step(p, sampleTime);
		}
	}

	// Writes 10 V / 0 V for the active pulses of one port; the others keep what
	// setVoltage() wrote.
	void step(int port, float sampleTime) {
		for (uint8_t i = 0; i < PORT_MAX_CHANNELS; i++) {
			bool s = pulseGenerator[port][i].process(sampleTime);
			if (triggerActive[port][i]) {
				output[port].setVoltage(s ? 10.f : 0.f, i);
			}
		}
	}
};


// The one async worker shared by all MidiKit modules; the weak_ptr lets it die
// with the last module. Unguarded: modules are constructed on the UI thread only.
static std::shared_ptr<ITaskWorker> defaultWorker() {
	static std::weak_ptr<ITaskWorker> shared;
	if (shared.expired()) {
		auto worker = std::make_shared<MpmcTaskWorker>("MidiKit worker");
		shared = worker;
		return worker;
	}
	return shared.lock();
}

template <typename CONFIG>
struct MidiKitModuleBase : Module, MidiScript::MidiScriptEngineHandler {
	static constexpr int CV_INPUTS = CONFIG::cvInputs;
	static constexpr int TRIG_INPUTS = CONFIG::trigInputs;
	static constexpr int TRIG_OUTPUTS = CONFIG::trigOutputs;
	static constexpr int PARAMS = CONFIG::params;
	static constexpr int MIDI_INPUTS = CONFIG::midiInputs;
	static constexpr int MIDI_OUTPUTS = CONFIG::midiOutputs;

	enum ParamIds {
		ENUMS(PARAM, CONFIG::params),
		NUM_PARAMS
	};
	enum InputIds {
		ENUMS(INPUT, CONFIG::cvInputs),
		ENUMS(INPUT_TRIG, CONFIG::trigInputs),
		NUM_INPUTS
	};
	enum OutputIds {
		ENUMS(OUTPUT_TRIG, CONFIG::trigOutputs),
		NUM_OUTPUTS
	};
	enum LightIds {
		NUM_LIGHTS
	};

	/** [Stored to JSON] */
	int panelTheme = 0;

	// The MIDI inputs (see MidiInputs for the threading contract).
	MidiInputs<CONFIG::midiInputs> midiIns;

	// Frame of the latest process() call, for code without ProcessArgs.
	std::atomic<int64_t> timingCurrentFrame{0};
	// The engine's block size, published from the audio thread.
	std::atomic<int64_t> timingBlockFrames{0};

	// Script log + overlay, in their own struct (see ScriptLog for the
	// threading contract).
	ScriptLog log;

	// Script engines + live script, in their own struct (see ScriptHost for the
	// threading contract).
	/** [Stored to Json] */
	ScriptHost host;

	// The MIDI outputs (see MidiOutputs for the threading contract).
	MidiOutputs<CONFIG::midiOutputs, CONFIG::trigInputs> midiOuts{&log};

	// Set by loadScript() (UI thread), consumed by process() (audio thread):
	// messages the outgoing script scheduled with sendAfterTrigger() are still
	// waiting in the outputs' tick queues, but the tick counters they were
	// counted against restart at 0, so they would fire at an arbitrary tick of
	// the new script. The queues belong to the audio thread, hence the hand-off.
	// The same request also resets every input's decoder: its NRPN/RPN state is
	// non-atomic and owned by the audio thread, so the UI thread must not touch it.
	std::atomic<bool> clearPending{false};

	// ── Tipsy protocol over the trigger CV (TipsyInput/TipsyOutput) ──────────
	// All Tipsy encode/decode state lives in the TipsyOutput/TipsyInput structs;
	// the module owns just these two objects.
	TipsyOutput tipsyOut;
	TipsyInput tipsyIn{&log};

	dsp::ClockDivider processDivider;

	// Trigger inputs and outputs, in their own structs (see TriggerInputs /
	// TriggerOutputs for the threading contract). Their port pointers are wired
	// in the constructor, once config() has sized the port vectors.
	static_assert(CONFIG::trigInputs >= 1 && CONFIG::trigOutputs >= 1, "a variant needs at least one trigger input and output");
	static_assert(CONFIG::midiInputs >= 1 && CONFIG::midiOutputs >= 1, "a variant needs at least one MIDI input and output");
	TriggerInputs<CONFIG::trigInputs> triggerIns;
	TriggerOutputs<CONFIG::trigOutputs> triggerOuts;

	uint64_t sample = 0;
	std::atomic<float> sampleRate{0.f};

	// Points every per-CV-port/param engine back-pointer at the active engine.
	// Passing null clears instead — no active engine means no port/param can be
	// enabled (a script re-enables via its bindings). Called at construction, on
	// reset, and after loadScript() selects the engine.
	void bindPortsAndParams(MidiScript::MidiScriptEngine* engine) {
		for (int i = 0; i < CV_INPUTS; i++) {
			reinterpret_cast<MidiScript::MidiScriptEnginePortInfo*>(inputInfos[INPUT + i])->se = engine;
		}
		for (int i = 0; i < PARAMS; i++) {
			reinterpret_cast<MidiScript::MidiScriptEngineParamQuantity*>(paramQuantities[PARAM + i])->se = engine;
		}
		if (!engine) disablePortsAndParams();
	}

	// Every input/param back to disabled; the next script re-enables the ones it
	// uses. Must not run before the outgoing script's onUnload() has finished,
	// which still reads them (see resetScriptState()).
	void disablePortsAndParams() {
		for (int i = 0; i < CV_INPUTS; i++) {
			reinterpret_cast<MidiScript::MidiScriptEnginePortInfo*>(inputInfos[INPUT + i])->enabled = false;
		}
		for (int i = 0; i < PARAMS; i++) {
			reinterpret_cast<MidiScript::MidiScriptEngineParamQuantity*>(paramQuantities[PARAM + i])->enabled = false;
		}
	}

	// MidiScriptEngineHandler
	void writeLog(const std::string& text, bool useTimestamp = true) override {
		if (useTimestamp) {
			log.pushTimestamped(sampleRate != 0.f ? float(sample) / sampleRate : 0.f, text);
		}
		else {
			log.pushText(text);
		}
	}

	// MidiScriptEngineHandler
	void writeOverlay(const std::string& s1, const std::string& s2, const std::string& s3) override {
		log.pushOverlay(s1, s2, s3);
	}

	// MidiScriptEngineHandler
	void enableInput(int i) override {
		if (i < 0 || i >= CV_INPUTS) return;
		reinterpret_cast<MidiScript::MidiScriptEnginePortInfo*>(inputInfos[INPUT + i])->enabled = true;
	}

	// MidiScriptEngineHandler — midi.enablePorts() / midiOut.enablePorts()
	// bindings (worker thread).
	void enableMidiIn(int count) override {
		midiIns.enablePorts(count);
	}
	void enableMidiOut(int count) override {
		midiOuts.enablePorts(count);
	}

	// process() publishes timingCurrentFrame, but a script loads asynchronously and its
	// rack.onLoad can run before the first process(), so loadScript() seeds it
	// from the engine. UI thread.
	void seedTiming() {
		timingCurrentFrame.store(vcv::engine::getFrame(), std::memory_order_relaxed);
	}

	// MidiScriptEngineHandler
	int64_t getTimingCurrentFrame() const override {
		return timingCurrentFrame.load(std::memory_order_relaxed);
	}
	int64_t getTimingBlockFrames() const override {
		return timingBlockFrames.load(std::memory_order_relaxed);
	}
	float getSampleRate() const override {
		return sampleRate.load(std::memory_order_relaxed);
	}
	bool isTimingEnabled() const override {
		return midiOuts.isTimingEnabled();
	}

	// MidiScriptEngineHandler — midiOut.enableTiming() binding (worker thread).
	void enableTiming(bool reportLate) override {
		midiOuts.enableTiming(reportLate);
	}

	// MidiScriptEngineHandler — after the outgoing script's onUnload (worker thread).
	void resetScriptState() override {
		resetMidiPortEnables();
		disablePortsAndParams();
		tipsyOut.reset();
		triggerOuts.reset();
	}

	// Back to "only port 1 of each" — what a script starts with.
	void resetMidiPortEnables() {
		midiIns.resetEnables();
		midiOuts.resetEnables();
	}

	// MidiScriptEngineHandler — trig.enableIn() binding (worker thread).
	void enableTrigger(int port, uint8_t channel) override {
		triggerIns.enable(port, channel);
	}

	// MidiScriptEngineHandler
	float getInputVoltage(int i, uint8_t ch) override {
		if (i < 0 || i >= CV_INPUTS || ch >= PORT_MAX_CHANNELS) return 0.f;
		if (reinterpret_cast<MidiScript::MidiScriptEnginePortInfo*>(inputInfos[INPUT + i])->enabled)
			return inputs[INPUT + i].getVoltage(ch);
		return 0.f;
	}

	// MidiScriptEngineHandler — trig.enableTipsyIn() binding (worker thread).
	// i is a 0-based trigger input index, or -1 to disable decoding.
	void enableTipsyIn(int i) override {
		tipsyIn.claim(i);
	}

	// MidiScriptEngineHandler — midi.enableNrpnIn()/enableRpnIn() binding
	// (worker thread).
	void enableNrpnIn(int midiPort, int kind, int channel) override {
		midiIns.enableNrpn(midiPort, kind, channel);
	}

	// MidiScriptEngineHandler — midi.enableCc14bitIn() binding (worker thread).
	void enableCc14bitIn(int midiPort, int cc, int channel) override {
		midiIns.enableCc14bit(midiPort, cc, channel);
	}

	// MidiScriptEngineHandler
	float getTrigVoltage(int i, uint8_t ch) override {
		if (i < 0 || i >= TRIG_INPUTS || ch >= PORT_MAX_CHANNELS) return 0.f;
		// Only channel 0 of the claimed trigger input carries the Tipsy stream:
		// that channel reads as 0 — the raw encoded voltages are protocol, not
		// a gate a script should act on. Other channels are unaffected.
		if (ch == 0 && i == tipsyIn.claimed()) return 0.f;
		return inputs[INPUT_TRIG + i].getVoltage(ch);
	}

	// MidiScriptEngineHandler
	uint64_t getTrigTicks(int i, uint8_t ch) override {
		return triggerIns.getTicks(i, ch);
	}

	// MidiScriptEngineHandler
	void enableParam(int i) override {
		if (i < 0 || i >= PARAMS) return;
		reinterpret_cast<MidiScript::MidiScriptEngineParamQuantity*>(paramQuantities[PARAM + i])->enabled = true;
	}

	// MidiScriptEngineHandler
	float getParamValue(int i) override {
		if (i < 0 || i >= PARAMS) return 0.f;
		if (reinterpret_cast<MidiScript::MidiScriptEngineParamQuantity*>(paramQuantities[PARAM + i])->enabled)
			return params[PARAM + i].getValue();
		return 0.f;
	}

	// MidiScriptEngineHandler
	void setTrig(int i, uint8_t ch, float duration = 1e-3f, int64_t frame = -1) override {
		if (i < 0 || i >= TRIG_OUTPUTS || ch >= PORT_MAX_CHANNELS) return;
		triggerOuts.setGate(i, ch, duration, frame);
	}

	// MidiScriptEngineHandler
	void setTrigVoltage(int i, uint8_t ch, float voltage, int64_t frame = -1) override {
		if (i < 0 || i >= TRIG_OUTPUTS || ch >= PORT_MAX_CHANNELS) return;
		triggerOuts.setVoltage(i, ch, voltage, frame);
	}

	// MidiScriptEngineHandler
	bool sendMidi(int midiPort, const MidiScript::Message* msgs, size_t count, uint8_t channel, uint64_t tick, int trigPort = 0) override {
		return midiOuts.enqueue(midiPort, msgs, count, channel, tick, trigPort);
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
		if (!tipsyOut.send(mimeType, data, dataBytes)) {
			writeLog("Tipsy: pending queue full", false);
			return false;
		}
		return true;
	}

	// Audio thread — one sample of every connected trigger output.
	void processTriggerOutputs(float sampleTime, int64_t frame) {
		// Drains the Tipsy queue regardless of activeEngine, for the same reason as
		// the MIDI out-queue in process(): messages queued by a script's onUnload()
		// must still reach the output after the engine is gone. Tipsy always goes
		// to the first trigger output, connected or not.
		processTipsyOutput(0);
		triggerOuts.process(frame, sampleTime);
	}

	// Trigger detection, per channel: a rising edge advances that channel's
	// tick clock and drains its tick-scheduled (sendAfterTrigger) messages.
	// Skipped entirely with no active engine — the SchmittTriggers are only
	// stepped while a script is loaded. Gated on trig.enableIn(); while a
	// Tipsy stream owns channel 0 its encoded voltages are stepped but not
	// counted.
	void processTriggerInputs() {
		if (host.getActiveEngine()) {
			for (int port = 0; port < TRIG_INPUTS; port++) {
				int channels = triggerIns.input[port].getChannels();
				if (channels <= 0) channels = 1;
				// Only the claimed port carries the Tipsy stream.
				triggerIns.process(port, channels, tipsyIn.claimed() == port,
					[&](uint8_t c, uint64_t tick) {
						// Scheduled MIDI messages (sendAfterTrigger) run on the
						// tick clock of the trigger input they were scheduled
						// against, for every output.
						int64_t frame = timingCurrentFrame.load(std::memory_order_relaxed);
						midiOuts.onTick(c, tick, port, frame);
						host.queueTick(port, c, frame);
					});
			}
		}
	}

	// Outputs the next Tipsy-encoded voltage on the trigger output (audio
	// thread). Delegates the encode to TipsyOutput and turns its status into the
	// trigger output write or a log line. Returns true if a voltage was output.
	bool processTipsyOutput(uint8_t channel = 0) {
		float f;
		switch (tipsyOut.process(f)) {
			case TipsyOutput::Output::WROTE:
				// Tipsy messages always go to the first trigger output (port 0).
				setTrigVoltage(0, channel, f);
				return true;
			case TipsyOutput::Output::INIT_ERROR:
				writeLog("Tipsy encoder error: " + std::to_string(tipsyOut.lastInitErrorCode), false);
				return false;
			case TipsyOutput::Output::ENCODE_ERROR:
				writeLog("Tipsy encoding error", false);
				return false;
			default: // IDLE
				return false;
		}
	}

	// Feeds one sample from the Tipsy input trigger into the decoder (audio
	// thread). On a completed message, the callback enqueues it into the active
	// engine's tipsyInQueue for the worker to dispatch and returns whether it
	// was accepted (false = queue full, which tipsyIn reports as overflow).
	// Returns true if a message completed and was enqueued on this sample.
	// The Tipsy stream is always carried on channel 0 of the trigger input —
	// other channels are never decoded. No-op unless a script claimed the
	// trigger input via trig.enableTipsyIn().
	bool processTipsyInput() {
		int port = tipsyIn.claimed();
		if (port < 0 || port >= TRIG_INPUTS || !host.getActiveEngine()) return false;
		if (!inputs[INPUT_TRIG + port].isConnected()) return false;

		return tipsyIn.process(inputs[INPUT_TRIG + port].getVoltage(0),
			[&](const TipsyInput::TipsyMessage& m) {
				if (host.getActiveEngine()->tipsyInQueue.full()) return false;
				TipsyInput::TipsyMessage q = m;
				q.frame = timingCurrentFrame.load(std::memory_order_relaxed);
				host.getActiveEngine()->tipsyInQueue.push(q);
				return true;
			});
	}


	static PortCounts portCounts() {
		return PortCounts{ CONFIG::cvInputs, CONFIG::trigInputs, CONFIG::trigOutputs, CONFIG::params, CONFIG::midiInputs, CONFIG::midiOutputs };
	}

	MidiKitModuleBase() : MidiKitModuleBase(defaultWorker()) {}
	explicit MidiKitModuleBase(std::shared_ptr<ITaskWorker> worker)
		: host(this, portCounts()) {
		panelTheme = pluginSettings.panelThemeDefault;
		config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);
		// Wire the trigger ports into TriggerInputs/TriggerOutputs so they can
		// read/write them directly; the vectors are fully sized by config() and
		// never resized. The ports of each kind are consecutive.
		if (TRIG_INPUTS > 0) triggerIns.input = &inputs[INPUT_TRIG];
		if (TRIG_OUTPUTS > 0) triggerOuts.output = &outputs[OUTPUT_TRIG];
		for (int i = 0; i < TRIG_INPUTS; i++) {
			configInput(INPUT_TRIG + i, TRIG_INPUTS > 1 ? string::f("Trigger %d", i + 1) : "Trigger");
		}
		for (int i = 0; i < TRIG_OUTPUTS; i++) {
			configOutput(OUTPUT_TRIG + i, TRIG_OUTPUTS > 1 ? string::f("Trigger %d", i + 1) : "Trigger");
		}
		for (int i = 0; i < CV_INPUTS; i++) {
			configInput<MidiScript::MidiScriptEnginePortInfo>(INPUT + i);
		}
		for (int i = 0; i < PARAMS; i++) {
			configParam<MidiScript::MidiScriptEngineParamQuantity>(PARAM + i, 0.f, 1.f, 0.f);
		}
		// No engine is loaded yet — bind to null (clears the UI state); it is
		// bound to the active engine by loadScript() once a script loads.
		bindPortsAndParams(nullptr);

		processDivider.setDivision(8);
		midiIns.onMessage = [this](int port, const MidiScript::QueuedMessage& q) {
			host.queueMessage(port, q);
		};
		host.setWorker(worker);
		onReset();
	}

	// Closes the active engine and drains whatever its onUnload() queued. Rack
	// dispatches this before the module leaves the engine and holds the engine
	// mutex across it, so process() cannot run concurrently.
	// closeState() blocks, so the worker has stopped producing before the drain
	// — preserve that order, it is what makes the drain safe.
	void onRemove(const RemoveEvent& e) override {
		host.closeState();        // closes + nulls the active engine (blocking)
		midiOuts.flush();         // drains the module queue; no engine needed
	}

	void onReset() override {
		midiIns.reset();
		midiOuts.reset();
		sample = 0;
		// Enables belong to the script: none until it runs trig.enableIn() etc.
		triggerIns.reset();
		// Releases a Tipsy claim and re-arms the decoder (nothing decodes at reset).
		tipsyIn.reset();
		triggerOuts.reset();
		// Blocking, like loadScript(): afterwards no worker task is outstanding.
		host.closeState();
		// After the closing script's onUnload(), which still reads the enables.
		// No engine is active now, so bind to null until the next loadScript().
		resetMidiPortEnables();
		bindPortsAndParams(nullptr);

		log.pushReset();
		log.pushText("No script");
	}

	void onSampleRateChange(const SampleRateChangeEvent& e) override {
		sampleRate = e.sampleRate;
	}

	void processBypass(const ProcessArgs& args) override {
		midiIns.processBypass(args.frame);
		// The context menu's script items are a UI query that only the engine's
		// pump answers; a bypassed module must still be able to show them.
		host.process();
		Module::processBypass(args);
	}

	void process(const ProcessArgs& args) override {
		timingCurrentFrame.store(args.frame, std::memory_order_relaxed);
		// carries out a loadScript() request (see
		// clearPending): drops the messages still waiting for a trigger
		// tick and the half-received NRPN/RPN/14-bit CC state of every input. Runs
		// before anything of this sample is drained or decoded, so what the new
		// script schedules from rack.onLoad is queued afterwards and survives.
		if (clearPending.exchange(false)) {
			midiOuts.clearTickQueues();
			midiIns.resetDecoders();
		}

		processTriggerInputs();

		// Every sample, not under processDivider: the sender emits one encoded
		// float per sample, so a divided read would drop most of the stream.
		processTipsyInput();

		if (processDivider.process()) {
			timingBlockFrames.store(vcv::engine::getBlockFrames(), std::memory_order_relaxed);

			// Not via MidiProcessor::process(): each decoded message is queued for
			// the worker, never dispatched inline.
			midiIns.process(args.frame);

			host.process();

			midiOuts.process(args.frame, sampleRate.load(std::memory_order_relaxed));
		}

		processTriggerOutputs(args.sampleTime, args.frame);

		sample++;
	}

	// JSON keys of the MIDI ports. The first keeps the single-port module's key
	// ("midiInput"/"midiOutput") so existing patches load unchanged.
	static std::string midiInputKey(int i) {
		return i == 0 ? "midiInput" : string::f("midiInput%d", i + 1);
	}
	static std::string midiOutputKey(int i) {
		return i == 0 ? "midiOutput" : string::f("midiOutput%d", i + 1);
	}

	json_t* dataToJson() override {
		json_t* rootJ = json_object();
		json_object_set_new(rootJ, "panelTheme", json_integer(panelTheme));

		// Only the ports the script uses are written. The others keep their
		// driver/device/channel in memory (a script reload never touches them),
		// so they come back once a script enables them again, but a patch does
		// not carry settings of ports nobody uses.
		for (int i = 0, n = midiIns.enabledCount(); i < n; i++) {
			json_object_set_new(rootJ, midiInputKey(i).c_str(), midiIns.ports[i].queue.toJson());
		}
		for (int i = 0, n = midiOuts.enabledCount(); i < n; i++) {
			json_object_set_new(rootJ, midiOutputKey(i).c_str(), midiOuts.ports[i].toJson());
		}
		json_object_set_new(rootJ, "script", json_string(host.script.c_str()));

		// The script publishes its config as it changes (rack.setConfig), so this is a
		// plain read of the last published value — no interpreter round-trip, nothing
		// to time out, and no failure mode to defend against. Contrast the previous
		// rack.onSave() design, which fetched the config synchronously here and had to
		// distinguish "no config" from "the fetch failed" to avoid erasing the user's
		// settings on a slow save.
		// By reference: peekConfig() returns a bare reference into the
		// SpscLatestValue slot (no shared_ptr copy), stable until the next
		// load()/peek()/load_if_new() call on this (the UI/reader) thread —
		// i.e. for the duration of json_object_set()'s incref below.
		const std::shared_ptr<json_t>& cfg = host.peekConfig();
		if (cfg && json_object_size(cfg.get()) > 0) {
			json_object_set(rootJ, "scriptConfig", cfg.get());   // set, not set_new: shared
		}
		return rootJ;
	}

	void dataFromJson(json_t* rootJ) override {
		json_t* panelThemeJ = json_object_get(rootJ, "panelTheme");
		if (panelThemeJ) panelTheme = json_integer_value(panelThemeJ);

		for (int i = 0; i < MIDI_INPUTS; i++) {
			json_t* midiInputJ = json_object_get(rootJ, midiInputKey(i).c_str());
			if (midiInputJ && json_is_object(midiInputJ)) midiIns.ports[i].queue.fromJson(midiInputJ);
		}
		for (int i = 0; i < MIDI_OUTPUTS; i++) {
			json_t* midiOutputJ = json_object_get(rootJ, midiOutputKey(i).c_str());
			if (midiOutputJ && json_is_object(midiOutputJ)) midiOuts.ports[i].fromJson(midiOutputJ);
		}

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
		seedTiming();
		sample = 0;
		// Before the load, so the new script starts with all callbacks disabled.
		// The trigger outputs are reset by resetScriptState(), once the outgoing
		// script's onUnload() has run.
		triggerIns.reset();
		// Port enables and enabled params/inputs stay: the outgoing script's
		// onUnload() still needs them (the engines reset them via resetScriptState()).
		// Raised before the load, so what the new script schedules from rack.onLoad
		// survives the audio thread's clear (tick queues, decoder state).
		clearPending.store(true);
		midiIns.clearExtendedCc();
		log.pushReset();

		// Closes the outgoing engine (blocking) and loads the new one.
		MidiScript::MidiScriptEngine* engine = host.load(s, configJson);
		// No engine loaded, so nothing resets the enables and outputs on the worker.
		if (!engine) {
			resetMidiPortEnables();
			triggerOuts.reset();
		}

		bindPortsAndParams(engine);
	}

	void clearScript() {
		loadScript("");
	}
};


// One log entry as a display line (without a trailing newline); empty for
// entries that print nothing (RESET).
static std::string formatLogEntry(const std::tuple<LOG_FORMAT, float, std::string>& s) {
	const std::string& text = std::get<2>(s);
	switch (std::get<0>(s)) {
		case LOG_FORMAT::TIMESTAMP:
			return string::f("[%9.4f] %s", std::get<1>(s), text.c_str());
		case LOG_FORMAT::TEXT:
			return text;
		case LOG_FORMAT::INDENTED:
			return "     " + text;
		default:
			return "";
	}
}

struct LogDisplay : LedTextDisplay {
	std::list<std::tuple<LOG_FORMAT, float, std::string>>* buffer;
	bool dirty = true;
	// Set by the widget: adds the running script's section (engine, RAM usage,
	// rack.registerContextMenu items, ...) to the top of this menu and returns
	// whether it added anything. Kept as a hook because that needs the module,
	// which the display knows nothing about.
	std::function<bool(Menu*)> appendScriptItems;

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
			// Cap to the number of lines that vertically fit.
			size_t size = std::min(buffer->size(), static_cast<size_t>(box.size.y / fontSize) + 1);
			size_t i = 0;
			for (const auto& s : *buffer) {
				if (i >= size) break;
				if (std::get<0>(s) == LOG_FORMAT::RESET) continue;
				text += formatLogEntry(s) + "\n";
				i++;
			}
			dirty = false;
		}
	}

	void reset() {
		buffer->clear();
		dirty = true;
	}

	// The whole buffer as text, oldest line first (the display itself shows the
	// newest first, capped to what fits).
	std::string toText() const {
		std::string out;
		for (auto it = buffer->rbegin(); it != buffer->rend(); ++it) {
			if (std::get<0>(*it) == LOG_FORMAT::RESET) continue;
			out += formatLogEntry(*it) + "\n";
		}
		return out;
	}

	void appendContextMenu(Menu* menu) {
		bool empty = buffer->empty();
		if (appendScriptItems && appendScriptItems(menu)) menu->addChild(new MenuSeparator());
		menu->addChild(createMenuLabel("Log"));
		menu->addChild(createMenuItem("Copy to clipboard", "", [=]() {
			StoermelderPackOne::vcv::ui::setClipboard(toText());
		}, empty));
		menu->addChild(createMenuItem("Clear", "", [=]() {
			reset();
		}, empty));
	}

	void onButton(const ButtonEvent& e) override {
		if (e.action == GLFW_PRESS && e.button == GLFW_MOUSE_BUTTON_RIGHT) {
			appendContextMenu(createMenu());
			e.consume(this);
			return;
		}
		LedTextDisplay::onButton(e);
	}
};

// Placeholder menu entry that builds the script-registered items
// (rack.registerContextMenu) asynchronously. getContextMenus() evaluates each
// item's onGetValue callback on the worker thread and then invokes its
// callback with the evaluated specs.
template <typename MODULE>
struct ScriptContextMenuItems : ui::MenuEntry {
	struct Context {
		std::vector<MidiScript::ScriptMenuItem> specs;
		std::atomic<bool> loaded{false};
	};
	MODULE* module;
	std::shared_ptr<Context> ctx;
	bool built = false;

	ScriptContextMenuItems(MODULE* module) : module(module) {
		box.size.y = 0.f;
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
		MODULE* m = module;
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


// Widget base for all variants: log display, overlay, script menu and file
// handling. Derived widgets add their controls after construction using the
// add*() helpers below.
template <typename CONFIG>
struct MidiKitWidgetBase : ThemedModuleWidget<MidiKitModuleBase<CONFIG>>, OverlayMessageProvider {
	using MODULE = MidiKitModuleBase<CONFIG>;
	using BASE = ThemedModuleWidget<MODULE>;
	using ScriptContextMenuItems = ScriptContextMenuItems<MODULE>;
	// Members of the dependent base need an explicit qualifier.
	using BASE::module;
	using BASE::box;
	using BASE::addChild;
	using BASE::addParam;
	using BASE::addInput;
	using BASE::addOutput;

	const size_t BUFFERSIZE = 800;
	// Null for variants without a log display (no addLogDisplay() call).
	LogDisplay* logDisplay = nullptr;
	// How many log entries the widget keeps; variants without a log display
	// can lower this to what they show elsewhere.
	size_t bufferLimit = BUFFERSIZE;
	std::list<std::tuple<LOG_FORMAT, float, std::string>> buffer;
	std::string filename = "";

	MidiKitWidgetBase(MODULE* module, const std::string& slug)
		: BASE(module, slug) {
		this->setModule(module);
		this->module = module;

		addChild(createWidget<StoermelderBlackScrew>(Vec(RACK_GRID_WIDTH, 0)));
		addChild(createWidget<StoermelderBlackScrew>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, 0)));
		addChild(createWidget<StoermelderBlackScrew>(Vec(RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));
		addChild(createWidget<StoermelderBlackScrew>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));

		if (module) {
			OverlayMessageWidget::registerProvider(this);
		}
	}

	// Layout helpers for derived widgets' constructors.
	void addMidiInputDisplay(int i, Rect r) {
		MidiWidget<>* display = createWidget<MidiWidget<>>(r.pos);
		display->box.size = r.size;
		display->setMidiPort(module ? &module->midiIns.ports[i].queue : NULL, CONFIG::midiInputs > 1 ? string::f("In %d", i + 1) : "In");
		addChild(display);
	}

	void addMidiOutputDisplay(int i, Rect r) {
		MidiWidget<>* display = createWidget<MidiWidget<>>(r.pos);
		display->box.size = r.size;
		display->setMidiPort(module ? &module->midiOuts.ports[i] : NULL, CONFIG::midiOutputs > 1 ? string::f("Out %d", i + 1) : "Out");
		addChild(display);
	}

	void addLogDisplay(Rect r) {
		LedDisplay* textDisplay = createWidget<LedDisplay>(r.pos);
		textDisplay->box.size = r.size;
		addChild(textDisplay);

		logDisplay = createWidget<LogDisplay>(Vec());
		logDisplay->buffer = &buffer;
		logDisplay->box.size = textDisplay->box.size.minus(Vec(0.f, 6.f));
		logDisplay->fontSize = 7.2f;
		logDisplay->appendScriptItems = [this](Menu* menu) {
			return appendRunningScriptItems(menu);
		};
		textDisplay->addChild(logDisplay);
	}

	~MidiKitWidgetBase() {
		if (module) {
			OverlayMessageWidget::unregisterProvider(this);
		}
	}

	void step() override {
		BASE::step();
		if (!module) return;
		std::tuple<LOG_FORMAT, float, std::string> s;
		while (module->log.midiLogMessages.try_pop(s)) {
			if (buffer.size() >= bufferLimit) buffer.pop_back();
			if (std::get<0>(s) == LOG_FORMAT::RESET) {
				resetLog();
			}
			else {
				buffer.push_front(s);
				if (logDisplay) logDisplay->dirty = true;
			}
		}
	}

	void resetLog() {
		buffer.clear();
		if (logDisplay) logDisplay->reset();
	}

	void appendContextMenu(Menu* menu) override {
		BASE::appendContextMenu(menu);
		if (!module) return;

		menu->addChild(new MenuSeparator());
		// Ports 2+ are only configurable while the script has enabled them.
		for (int i = 0, n = module->midiIns.enabledCount(); i < n; i++) {
			menu->addChild(Rack::createStickyMidiMenuItem(CONFIG::midiInputs > 1 ? string::f("MIDI input %d", i + 1) : "MIDI input", &module->midiIns.ports[i].queue));
		}
		for (int i = 0, n = module->midiOuts.enabledCount(); i < n; i++) {
			menu->addChild(Rack::createStickyMidiMenuItem(CONFIG::midiOutputs > 1 ? string::f("MIDI output %d", i + 1) : "MIDI output", &module->midiOuts.ports[i]));
		}

		if (module->host.getActiveEngine()) {
			menu->addChild(new MenuSeparator());
			appendRunningScriptItems(menu);
		}

		menu->addChild(new MenuSeparator());
		menu->addChild(createMenuLabel("Script"));
		menu->addChild(createSubmenuItem("Examples (JavaScript)", "", [=](Menu* menu) {
			appendExampleItems(menu, vcv::fs::getPluginDirectory("presets/MidiKit/JavaScript"), ".js");
		}));
		menu->addChild(createSubmenuItem("Examples (Lua)", "", [=](Menu* menu) {
			appendExampleItems(menu, vcv::fs::getPluginDirectory("presets/MidiKit/Lua"), ".lua");
		}));
		menu->addChild(createMenuItem("Clear", "", [=]() { module->clearScript(); }));
		menu->addChild(createMenuItem("Paste from clipboard", RACK_MOD_ALT_NAME "+V", [=]() { pasteJsClipboard(); }));
		menu->addChild(createMenuItem("Copy to clipboard", RACK_MOD_ALT_NAME "+C", [=]() { copyJsClipboard(); }));
		menu->addChild(createMenuItem("Load", RACK_MOD_ALT_NAME "+L", [=]() { loadJsDialog(); }));
		menu->addChild(createMenuItem("Reload", RACK_MOD_ALT_NAME "+Y", [=]() { loadJs(filename); }, filename.empty()));
		menu->addChild(createMenuItem("Save as", "", [=]() { saveScriptDialog(); }));
	}

	// The running engine's section: engine name, RAM usage, the script's own
	// context-menu items and the variant's status entries. Nothing (and false)
	// without a running script. Shared by the module's menu and the log display's.
	bool appendRunningScriptItems(Menu* menu) {
		if (!module) return false;
		MidiScript::MidiScriptEngine* engine = module->host.getActiveEngine();
		if (!engine) return false;
		if (module->host.isLuaEngine()) menu->addChild(createMenuLabel("Running Script (Lua)"));
		if (module->host.isQuickJsEngine()) menu->addChild(createMenuLabel("Running Script (QuickJs)"));
		size_t used, total;
		if (engine->getMemoryUsage(used, total)) {
			float pct = total > 0 ? 100.f * used / total : 0.f;
			menu->addChild(createMenuLabel(string::f("RAM usage: %zu / %zu KB (%.0f%%)", used / 1024, total / 1024, pct)));
		}

		menu->addChild(new ScriptContextMenuItems(module));
		appendStatusMenuItems(menu);
		return true;
	}

	// Hook for variants: extra entries at the end of the "Script" section.
	virtual void appendStatusMenuItems(Menu* menu) {}

	int nextOverlayMessageId() override {
		if (!module || module->log.overlayQueue.empty()) {
			return -1;
		}
		return module->log.overlayQueue.shift();
	}

	void getOverlayMessage(int id, OverlayMessageProvider::Message& m) override {
		if (!module) return;
		m.title = std::get<0>(module->log.overlayMessage);
		m.subtitle[0] = std::get<1>(module->log.overlayMessage);
		m.subtitle[1] = std::get<2>(module->log.overlayMessage);
	}

	void loadJsDialog() {
		std::string path = vcv::ui::openDialog("MIDI-KIT file:js,lua", "");
		if (path.empty()) return;
		filename = path;
		loadJs(path);
	}

	void loadJs(std::string filename) {
		// Read first: an unreadable file leaves the running script and its log alone.
		std::string script;
		if (!vcv::fs::read(filename, script)) {
			vcv::ui::message(vcv::MessageType::WARNING, vcv::MessageButtons::OK,
				string::f("Could not read the script file %s", filename.c_str()));
			return;
		}
		resetLog();
		module->loadScript(script);
	}

	// Returns true if dir (or any of its subfolders, recursively) contains at
	// least one script file with the given extension. Used to avoid creating
	// empty submenus for folders that hold no scripts of the active engine.
	bool hasExampleScripts(std::string dir, std::string ext) {
		if (!vcv::fs::isDirectory(dir)) return false;
		for (std::string path : vcv::fs::getEntries(dir)) {
			if (vcv::fs::isDirectory(path)) {
				if (hasExampleScripts(path, ext)) return true;
			}
			else if (vcv::fs::getExtension(path) == ext) {
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
		if (vcv::fs::isDirectory(dir)) {
			std::vector<std::string> entries = vcv::fs::getEntries(dir);
			std::sort(entries.begin(), entries.end());
			// Subfolders first (sorted)
			for (std::string path : entries) {
				if (!vcv::fs::isDirectory(path)) continue;
				if (!hasExampleScripts(path, ext)) continue;
				hasExamples = true;
				std::string name = vcv::fs::getFilename(path);
				menu->addChild(createSubmenuItem(name, "", [=](Menu* menu) {
					appendExampleItems(menu, path, ext);
				}));
			}
			// Files second (sorted)
			for (std::string path : entries) {
				if (vcv::fs::isDirectory(path)) continue;
				if (vcv::fs::getExtension(path) != ext) continue;
				hasExamples = true;
				std::string name = vcv::fs::getStem(path);
				// Grey out scripts that declare "@requires params=N" for more params
				// than this variant has; loading one would be refused anyway.
				std::string source;
				int needed = vcv::fs::read(path, source) ? MidiScript::MidiScriptEngine::requiredParams(source) : 0;
				bool unsupported = needed > CONFIG::params;
				MenuItem* item = createMenuItem(name, unsupported ? string::f("needs %d params", needed) : "", [=]() {
					filename = path;
					loadJs(path);
				});
				item->disabled = unsupported;
				menu->addChild(item);
			}
		}
		if (!hasExamples) {
			menu->addChild(createMenuLabel("None found"));
		}
	}

	void saveScriptDialog() {
		if (module->host.script == "") {
			return;
		}

		const std::string& script = module->host.script;
		std::string ext = module->host.isLuaEngine() ? ".lua" : ".js";

		std::string dir = vcv::fs::getUserDirectory("");
		std::string filename = "script" + ext;
		std::string newPath = vcv::ui::saveDialog("", dir, filename);
		if (newPath.empty()) {
			return;
		}
		// Add extension if user didn't specify one
		std::string newExt = vcv::fs::getExtension(vcv::fs::getFilename(newPath));
		if (newExt == "") newPath += ext;

		if (!vcv::fs::write(newPath, script)) {
			std::string msg = string::f("Could not save the script to %s", newPath.c_str());
			vcv::ui::message(vcv::MessageType::WARNING, vcv::MessageButtons::OK, msg);
		}
	}

	void onPathDrop(const event::PathDrop& e) override {
		if (module && e.paths.size() > 0) {
			loadJs(e.paths[0]);
			e.consume(this);
		}
		BASE::onPathDrop(e);
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
		BASE::onHoverKey(e);
	}

	void pasteJsClipboard() {
		std::string script = vcv::ui::getClipboard();
		if (!module || script.empty()) return;
		module->loadScript(script);
	}

	void copyJsClipboard() {
		vcv::ui::setClipboard(module->host.script);
	}
};


using MidiKitModule = MidiKitModuleBase<MidiKitConfig>;

struct MidiKitWidget : MidiKitWidgetBase<MidiKitConfig> {
	MidiKitWidget(MidiKitModule* module) : MidiKitWidgetBase<MidiKitConfig>(module, "MidiKit") {
		addMidiInputDisplay(0, Rect(Vec(0.f, 36.4f), Vec(195.f, 44.6f)));
		addLogDisplay(Rect(Vec(0.f, 81.0f), Vec(195.f, 140.6f)));
		addMidiOutputDisplay(0, Rect(Vec(0.f, 221.6f), Vec(195.f, 44.6f)));

		static const float x[] = { 56.7f, 83.9f, 111.0f, 138.2f };
		for (int i = 0; i < MidiKitConfig::params; i++) {
			addParam(createParamCentered<StoermelderTrimpot>(Vec(x[i], 296.9f), module, MidiKitModule::PARAM + i));
		}
		for (int i = 0; i < MidiKitConfig::cvInputs; i++) {
			addInput(createInputCentered<StoermelderPort>(Vec(x[i], 327.5f), module, MidiKitModule::INPUT + i));
		}
		
		addInput(createInputCentered<StoermelderPort>(Vec(21.4f, 296.9f), module, MidiKitModule::INPUT_TRIG + 0));
		addInput(createInputCentered<StoermelderPort>(Vec(21.4f, 327.5f), module, MidiKitModule::INPUT_TRIG + 1));
		addOutput(createOutputCentered<StoermelderPort>(Vec(173.6, 296.9f), module, MidiKitModule::OUTPUT_TRIG + 0));
		addOutput(createOutputCentered<StoermelderPort>(Vec(173.6f, 327.5f), module, MidiKitModule::OUTPUT_TRIG + 1));
	}
};


using MidiKitMicroModule = MidiKitModuleBase<MidiKitMicroConfig>;

struct MidiKitMicroWidget : MidiKitWidgetBase<MidiKitMicroConfig> {
	MidiKitMicroWidget(MidiKitMicroModule* module) : MidiKitWidgetBase<MidiKitMicroConfig>(module, "MidiKitMicro") {
		// No log display: the last few log lines are shown in the context menu.
		bufferLimit = 5;
		addParam(createParamCentered<StoermelderTrimpot>(Vec(22.5f, 85.1f), module, MidiKitMicroModule::PARAM + 0));
		addParam(createParamCentered<StoermelderTrimpot>(Vec(22.5f, 110.7f), module, MidiKitMicroModule::PARAM + 1));

		addInput(createInputCentered<StoermelderPort>(Vec(22.5f, 155.4f), module, MidiKitMicroModule::INPUT + 0));
		addInput(createInputCentered<StoermelderPort>(Vec(22.5f, 186.0f), module, MidiKitMicroModule::INPUT + 1));

		addInput(createInputCentered<StoermelderPort>(Vec(22.5f, 223.2f), module, MidiKitMicroModule::INPUT_TRIG + 0));	
		addInput(createInputCentered<StoermelderPort>(Vec(22.5f, 253.7f), module, MidiKitMicroModule::INPUT_TRIG + 1));

		addOutput(createOutputCentered<StoermelderPort>(Vec(22.5f, 296.9f), module, MidiKitMicroModule::OUTPUT_TRIG + 0));
		addOutput(createOutputCentered<StoermelderPort>(Vec(22.5f, 327.5f), module, MidiKitMicroModule::OUTPUT_TRIG + 1));
	}

	void appendStatusMenuItems(Menu* menu) override {
		// Menu entry with word-wrapped text at a fixed width; its height follows the
		// wrapped text. ui::MenuLabel is always one line as wide as its text, which is
		// unusable for long log lines.
		struct MenuMultilineLabel : ui::MenuEntry {
			float WIDTH = 320.f;
			float FONT_SIZE = 12.f;
			float PADDING_X = 10.f;
			float PADDING_Y = 3.f;

			std::string text;
			// The text the current height was measured for.
			std::string measuredText;
			bool measured = false;

			MenuMultilineLabel(const std::string& text) : text(text) {
				box.size = Vec(WIDTH, 20.f);
			}

			void step() override {
				if (!measured || measuredText != text) {
					math::Vec size = vcv::ui::measureTextBox(text, FONT_SIZE, WIDTH - 2.f * PADDING_X);
					box.size = Vec(WIDTH, std::max(20.f, size.y + 2.f * PADDING_Y));
					measuredText = text;
					measured = true;
				}
				ui::MenuEntry::step();
			}

			void draw(const DrawArgs& args) override {
				nvgFontFaceId(args.vg, APP->window->uiFont->handle);
				nvgFontSize(args.vg, FONT_SIZE);
				nvgTextAlign(args.vg, NVG_ALIGN_LEFT | NVG_ALIGN_TOP);
				// Same line height as vcv::ui::measureTextBox() measures with.
				nvgTextLineHeight(args.vg, 1.2f);
				nvgFillColor(args.vg, bndGetTheme()->menuTheme.textColor);
				nvgTextBox(args.vg, PADDING_X, PADDING_Y, WIDTH - 2.f * PADDING_X, text.c_str(), nullptr);
			}
		};

		menu->addChild(createSubmenuItem("Log", "", [=](Menu* menu) {
			bool any = false;
			for (const auto& entry : buffer) {
				if (std::get<0>(entry) == LOG_FORMAT::RESET) continue;
				menu->addChild(new MenuMultilineLabel(formatLogEntry(entry)));
				any = true;
			}
			if (!any) menu->addChild(createMenuLabel("(empty)"));
		}));
	}
};

} // namespace MidiKit
} // namespace StoermelderPackOne

Model* modelMidiKit = createModel<StoermelderPackOne::MidiKit::MidiKitModule, StoermelderPackOne::MidiKit::MidiKitWidget>("MidiKit");
Model* modelMidiKitMicro = createModel<StoermelderPackOne::MidiKit::MidiKitMicroModule, StoermelderPackOne::MidiKit::MidiKitMicroWidget>("MidiKitMicro");
