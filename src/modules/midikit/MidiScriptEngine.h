#pragma once
#include "../../plugin.hpp"
#include "../../utils/TaskWorker.hpp"
#include "tipsy-encoder/include/tipsy/tipsy.h"
#include <atomic>
#include <chrono>
#include <future>
#include <thread>

namespace StoermelderPackOne {
namespace MidiScript {

using rack::midi::Message;


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
	virtual float getInputVoltage(int i, uint8_t ch) = 0;
	virtual float getTrigVoltage(int i, uint8_t ch) = 0;
	virtual uint64_t getTrigTicks(int i) = 0;
	virtual void enableParam(int i) = 0;
	virtual float getParamValue(int i) = 0;
	virtual void setTrig(int i, uint8_t ch, float duration = 1e-3f) = 0;
	virtual void setTrigVoltage(int i, uint8_t ch, float voltage) = 0;
};


struct MidiScriptEngine {
	// Cap on setSysEx's payload, so a script can't build an unbounded message
	// for the fixed-size midiOutQueue.
	static const int sysExMaxPayloadLength = 256;

	// The handler this engine runs inside, injected at construction. Every
	// module-facing callback (log/overlay/input/trig/param) routes through it.
	MidiScriptEngineHandler* handler;

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
	dsp::RingBuffer<int, 4> tickInQueue;
	dsp::RingBuffer<std::tuple<int, Message, uint64_t>, 128> midiOutQueue;

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
	// Returns false, leaving `out` untouched, only when the config couldn't be
	// determined (no script, or dispatch failed). Callers keep their previous
	// value then; overwriting it with "" would erase the user's settings. A
	// script without onSave() is NOT a failure — it returns true with empty
	// `out`, so the caller clears its stored config.
	//
	// Callable from any thread; implementations dispatch via runSync().
	virtual bool captureConfig(std::string& out) = 0;

	// Main interface for message processing
	virtual void processInMessage(int midiPort, Message& msg) = 0;
	virtual void processInTick(int trigPort) = 0;

	// Virtual (like process()) so tests can override it to fabricate output
	// without a real script engine behind it.
	virtual bool processOutMessage(int& midiPort, Message& msg, int& ticks) {
		if (!midiOutQueue.empty()) {
			auto t = midiOutQueue.shift();
			midiPort = std::get<0>(t);
			msg = std::get<1>(t);
			ticks = std::get<2>(t);
			return true;
		}
		return false;
	}

	// Caps on Tipsy payloads: the queue entry is a fixed-size POD so the audio
	// thread's shift() never heap-allocates (mime matches
	// tipsy::kMaxMimeTypeSize, data matches sysExMaxPayloadLength).
	static constexpr size_t tipsyMaxMimeTypeSize = 256;
	static constexpr size_t tipsyMaxPayloadLength = 256;

	// A Tipsy message queued by sendTipsy() (worker) and consumed by
	// processTipsyOutput() (audio). Fixed-size so the SPSC RingBuffer copies it
	// without heap allocation.
	struct TipsyMessage {
		uint16_t mimeSize;                         // length without NUL
		uint16_t dataSize;
		char mime[tipsyMaxMimeTypeSize];
		unsigned char data[tipsyMaxPayloadLength];
	};

	// Encodes protocol messages for MIDI output. Per-instance so scripts can
	// send independently without interfering.
	tipsy::ProtocolEncoder tipsyEncoder;

	// SPSC queue of pending Tipsy messages (worker → audio). sendTipsy() only
	// copies the payload; encoding happens in processTipsyOutput() on the audio
	// thread, which owns the encoder.
	dsp::RingBuffer<TipsyMessage, 8> tipsyPendingQueue;

	// The message the encoder is streaming out. The encoder holds pointers into
	// its buffers for the whole (multi-cycle) message, so it must outlive the
	// encoding — a member, not a local (shift() hands out copies).
	TipsyMessage tipsyCurrentMessage;

	// Queues a Tipsy message for transmission via the module's trigger CV
	// output. Worker thread: only copies the payload into the pending queue —
	// encoding happens on the audio thread in processTipsyOutput(). Always
	// output on the first trigger output (port 1). Returns true on success.
	bool sendTipsy(const char* mimeType, const unsigned char* data, uint32_t dataBytes) {
		if (!mimeType || !data || dataBytes > tipsyMaxPayloadLength) {
			handler->writeLog("Tipsy: invalid parameters", false);
			return false;
		}
		size_t mimeSize = strlen(mimeType);
		if (mimeSize + 1 > tipsyMaxMimeTypeSize) {
			handler->writeLog("Tipsy: mime type too long", false);
			return false;
		}
		if (tipsyPendingQueue.full()) {
			handler->writeLog("Tipsy: pending queue full", false);
			return false;
		}

		TipsyMessage p;
		p.mimeSize = (uint16_t)mimeSize;
		p.dataSize = (uint16_t)dataBytes;
		std::memcpy(p.mime, mimeType, mimeSize + 1);
		std::memcpy(p.data, data, dataBytes);
		tipsyPendingQueue.push(p);
		return true;
	}

	// Outputs the next Tipsy-encoded voltage on the trigger output (audio
	// thread). If the encoder is idle, starts the next pending message, then
	// drains one encoded float. Returns true if a voltage was output.
	bool processTipsyOutput(uint8_t channel = 0) {
		if (tipsyEncoder.isDormant() && !tipsyPendingQueue.empty()) {
			// Copy into the member the encoder points into for the whole message
			// (shift() returns a copy, so not a local).
			tipsyCurrentMessage = tipsyPendingQueue.shift();
			auto initResult = tipsyEncoder.initiateMessage(tipsyCurrentMessage.mime, tipsyCurrentMessage.dataSize, tipsyCurrentMessage.data);
			if (tipsyEncoder.isError(initResult)) {
				handler->writeLog("Tipsy encoder error: " + std::to_string(static_cast<int>(initResult)), false);
				return false;
			}
		}

		if (tipsyEncoder.isDormant()) {
			return false;
		}

		float f;
		auto result = tipsyEncoder.getNextMessageFloat(f);
		if (tipsyEncoder.isError(result)) {
			handler->writeLog("Tipsy encoding error", false);
			tipsyEncoder.terminateCurrentMessage();
			return false;
		}
		// Tipsy messages always go to the first trigger output (port 0).
		handler->setTrigVoltage(0, channel, f);
		return true;
	}

	// Resets the Tipsy state (called on reset/new script load): drops pending
	// messages and terminates any in-flight encoding.
	void resetTipsyOutput() {
		tipsyPendingQueue.clear();
		tipsyEncoder.terminateCurrentMessage();
	}

	// Dispatches queued midiInQueue/tickInQueue onto the engine via runAsync().
	// Virtual so tests can override it to observe call counts.
	virtual void process() {
		if ((midiInQueue.size() > 0 || tickInQueue.size() > 0)) {
			runAsync([this]() {
				while (!midiInQueue.empty()) {
					auto t = midiInQueue.shift();
					int midiPort = std::get<0>(t);
					Message msg = std::get<1>(t);
					dispatchMidiMessage(midiPort, msg);
				}
				while (!tickInQueue.empty()) {
					int trigPort = tickInQueue.shift();
					dispatchTrigger(trigPort);
				}
			});
		}
	}

	// Engine-specific dispatch of a single message/tick, invoked from
	// process() above on the worker thread.
	virtual void dispatchMidiMessage(int midiPort, Message& msg) = 0;
	virtual void dispatchTrigger(int trigPort) = 0;

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