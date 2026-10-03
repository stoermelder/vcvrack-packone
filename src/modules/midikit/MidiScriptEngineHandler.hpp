#pragma once
#include "MidiScriptTypes.hpp"

namespace StoermelderPackOne {
namespace MidiScript {

// What the engine needs from its host module: hardware (inputs, outputs,
// triggers) and UI (log, overlay). Keeps the engine free of module knowledge.
struct MidiScriptEngineHandler {
	virtual void writeLog(const std::string& s, bool useTimestamp = true) = 0;
	virtual void writeOverlay(const std::string& s1, const std::string& s2, const std::string& s3) = 0;
	virtual void enableInput(int i) = 0;

	// Enable the first `count` MIDI ports (count >= 1); never shrinks. Only port 1
	// is on by default. Input beyond the count never reaches the script, output is
	// discarded. Forgotten on load/reset. Worker thread.
	virtual void enableMidiIn(int count) = 0;
	virtual void enableMidiOut(int count) = 0;

	// Worker thread, right before onUnload(); endUnload() follows. Only immediate
	// MIDI is accepted meanwhile (in timing mode queued behind Rack's output, so a
	// note-off cannot overtake its note-on). Scheduled sends, trigger writes and
	// Tipsy messages are ignored.
	virtual void beginUnload() = 0;

	// Worker thread, at the end of every unload, after onUnload(). Drops what the
	// outgoing script enabled and scheduled, and its queued Tipsy messages (one
	// already being encoded completes); resets the log. Safe when nothing ran.
	virtual void endUnload() = 0;

	// The audio thread's latest frame, the block size and the sample rate,
	// published atomically because the worker must not read APP->engine.
	virtual int64_t getTimingCurrentFrame() const = 0;
	virtual int64_t getTimingBlockFrames() const = 0;
	virtual float getSampleRate() const = 0;
	virtual bool isTimingEnabled() const = 0;

	// midiOut.enableTiming(). Outgoing messages keep their frame and Rack places
	// them, at the cost of one block of latency. `reportLate` logs messages that
	// arrived too late to be placed. Forgotten on load/reset. Worker thread.
	virtual void enableTiming(bool reportLate) = 0;

	// trig.enableIn(). Disabled channels get no tick processing. Worker thread.
	virtual void enableTrigger(int port, uint8_t channel) = 0;

	// Routes trigger input i into the Tipsy decoder; i < 0 disables it. Always 0
	// today. While claimed, the input stops counting ticks and firing
	// trig.onTrigger, and channel 1 of isHigh()/isLow() reads 0. Worker thread.
	virtual void enableTipsyIn(int i) = 0;

	// Assembles NRPN (kind 0) or RPN (kind 1) on midiPort into midi.onNrpn/onRpn.
	// channel is 0-based, -1 for all. While enabled, the component CCs
	// (98-101, and 6/38 while a parameter is armed) no longer reach midi.onMessage.
	// Worker thread.
	virtual void enableNrpnIn(int midiPort, int kind, int channel) = 0;

	// Assembles 14-bit CC on midiPort for MSB controller `cc` (0-31, LSB is
	// cc + 32), or all of them when cc < 0. channel is 0-based, -1 for all. Both
	// halves of an enabled pair no longer reach midi.onMessage. Per-CC, so a script
	// can take CC 7 as 14-bit and still see CC 39 raw. Worker thread.
	virtual void enableCc14bitIn(int midiPort, int cc, int channel) = 0;

	virtual float getInputVoltage(int i, uint8_t ch) = 0;
	virtual float getTrigVoltage(int i, uint8_t ch) = 0;
	virtual uint64_t getTrigTicks(int i, uint8_t ch) = 0;
	virtual void enableParam(int i) = 0;
	virtual float getParamValue(int i) = 0;
	// `frame` >= 0: the module applies the write when that frame comes up on the
	// audio thread (see frameForTrig()).
	virtual void setTrig(int i, uint8_t ch, float duration = 1e-3f, int64_t frame = -1) = 0;
	virtual void setTrigVoltage(int i, uint8_t ch, float voltage, int64_t frame = -1) = 0;

	// Queues `count` MIDI messages sharing one tick and trigger channel (channel
	// only matters for sendAfterTrigger(); others pass 0). Worker thread.
	// All-or-nothing: a group (NRPN: 4, 14-bit CC: 2) is never partially queued,
	// so false means none were. Saturation is normal, not an error.
	virtual bool sendMidi(int midiPort, const Message* msgs, size_t count, uint8_t channel, uint64_t tick, int trigPort = 0, const OutTag& tag = OutTag()) = 0;

	// midiOut.cancel(). Queued behind the messages already sent, applied on the
	// audio thread when it drains the queue. Ignored in onUnload(). False only on
	// a full queue, which is normal like sendMidi(). Worker thread.
	virtual bool cancelMidi(int midiPort, CancelMode mode, const Message& pattern, const OutGroup& group) = 0;

	// Queues a Tipsy message for output on the trigger CV; the audio thread
	// encodes it. False if rejected or full, which is normal like sendMidi().
	// Worker thread.
	virtual bool sendTipsyOut(const char* mimeType, const unsigned char* data, uint32_t dataBytes) = 0;
};

} // namespace MidiScript
} // namespace StoermelderPackOne