#pragma once
#include "../../plugin.hpp"
#include "../../utils/TaskWorker.hpp"
#include <atomic>
#include <chrono>
#include <future>
#include <thread>

namespace StoermelderPackOne {
namespace MidiScript {

using rack::midi::Message;


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

	// Routes trigger input i into the Tipsy decoder, or disables decoding when
	// i < 0. Today i is always 0 — the script-facing trig.enableTipsyIn() exposes
	// no port (Tipsy input is only supported on the first trigger input). While
	// claimed, the trigger input stops counting ticks and firing rack.onTrigger,
	// and channel 1 of trig.isHigh()/isLow() reads 0 (other channels are
	// unaffected). Worker thread.
	virtual void enableTipsyIn(int i) = 0;

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

	// Whether the loaded script defines rack.onSave(). Written on the worker as
	// part of load/teardown, read on the UI thread by captureConfig() to skip
	// the round-trip when there is nothing to ask for. Atomic because it is the
	// only script-derived state crossing threads — the interpreter and its
	// cached hook refs stay worker-owned.
	std::atomic<bool> hasOnSave{false};

	int inputCount;
	int inputTrigCount;
	int outputTrigCount;
	int paramCount;
	int midiInputCount;
	int midiOutputCount;

	MidiScriptEngine(MidiScriptEngineHandler* handler, int inputCount, int inputTrigCount, int outputTrigCount, int paramCount, int midiInputCount, int midiOutputCount)
		: handler(handler), inputCount(inputCount), inputTrigCount(inputTrigCount), outputTrigCount(outputTrigCount), paramCount(paramCount), midiInputCount(midiInputCount), midiOutputCount(midiOutputCount) {}

	std::shared_ptr<ITaskWorker> taskWorker;
	dsp::RingBuffer<std::tuple<int, Message>, 128> midiInQueue;
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
	// — dispatch, onSave, load/teardown — so the interpreter and contextMenus are
	// only ever touched from it, and the call sites assert that. Always true
	// under SyncTaskWorker (tests), which runs tasks inline.
	bool onWorkerThread() const {
		return taskWorker && taskWorker->isWorkerThread();
	}

	// Runs `task` (script code) on the worker thread and blocks until done.
	//
	// Returns false, leaving `out` untouched, if the task never ran. Callers MUST
	// NOT treat that as an empty result: in captureConfig() it would erase the
	// user's settings on save.
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


	// persistedConfigJson, if non-empty, is parsed and passed to rack.onLoad()
	// to restore config; otherwise onLoad() gets no argument (script defaults).
	//
	// Asynchronous, with no completion signal by design: the engine is NOT loaded
	// when this returns, and load messages/parse errors reach the user via
	// handler->writeLog() from the worker. Callers must not read engine state
	// expecting the new script — the dispatch paths no-op until the load lands.
	// The worker queue is FIFO, so successive calls (a script switch tearing one
	// engine down and loading another) still run in call order.
	//
	// `script` is copied, so the caller's buffer need not outlive this call.
	void loadScript(const char* script, const std::string& persistedConfigJson = "") {
		std::string s = script ? script : "";
		runAsync([this, s, persistedConfigJson]() {
			loadScriptOnWorker(s.c_str(), persistedConfigJson);
		});
	}

	// The load itself, always on the worker thread. Tears down any previous
	// script (via closeStateOnWorker()) before loading the new one; callers
	// already on the worker invoke this directly rather than loadScript().
	virtual void loadScriptOnWorker(const char* script, const std::string& persistedConfigJson) = 0;

	// Tears down script state, running rack.onUnload() first (e.g. all-notes-off).
	// onUnload()'s return value is ignored — config is captureConfig()'s job.
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

	// Runs rack.onSave() and writes its return value (the config to persist) as
	// JSON into `out` without disturbing script state. onSave() is expected
	// side-effect-free and may be called repeatedly (e.g. every explicit save);
	// used by toJson() at save time. Messages onSave() queues are discarded (a
	// save must have no audible effect).
	//
	// Returns false, leaving `out` untouched, only when the dispatch failed:
	// callers keep their previous value then, since overwriting it with "" would
	// erase the user's settings. Having nothing to persist — no script, or a
	// script without onSave() — is a definite answer, not a failure, so it
	// returns true with an empty `out` and the caller clears its stored config.
	//
	// Callable from any thread; implementations dispatch via runSync().
	virtual bool captureConfig(std::string& out) = 0;

	// Main interface for message processing
	virtual void processInMessage(int midiPort, Message& msg) = 0;
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
					Message msg = std::get<1>(t);
					dispatchMidiMessage(midiPort, msg);
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