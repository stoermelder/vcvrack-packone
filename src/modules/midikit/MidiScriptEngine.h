#pragma once
#include "../../plugin.hpp"
#include "../../utils/TaskWorker.hpp"
#include "tipsy-encoder/include/tipsy/tipsy.h"

namespace StoermelderPackOne {
namespace MidiScript {

using rack::midi::Message;


// A user-facing context-menu item registered via rack.registerContextMenu().
// Carries presentation data only — the actual onChange/onGetValue callbacks
// live in the engine's own map keyed by callbackId, so a copied spec can be
// handed freely to the UI thread without owning a script-function reference.
struct ScriptMenuItem {
	enum class Type { Boolean, Options } type = Type::Boolean;
	std::string label;
	// Options variant: selectable labels and the current selection index.
	std::vector<std::string> options;
	// checked (Boolean) and selected (Options) share storage — only the one
	// matching `type` is meaningful. Initialized via selected(0), which
	// zeroes both.
	union {
		bool checked;
		int selected;
	};
	// Opaque handle resolving to the script's onChange callback in the engine.
	int callbackId = -1;

	ScriptMenuItem() : selected(0) {}
};


// Implemented by the module that hosts a MidiScriptEngine. The engine calls
// back through this interface for everything that touches the module's
// hardware (inputs/outputs/triggers) and its UI (log/overlay), so the engine
// itself stays free of any module-specific knowledge.
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
	// Arbitrary but generous cap on setSysEx's payload, to keep a script from
	// building an unbounded message that then flows through the fixed-size
	// midiOutQueue and out to the driver.
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

	virtual ~MidiScriptEngine() { }

	void setWorker(std::shared_ptr<ITaskWorker> w) {
		taskWorker = std::move(w);
	}

	bool runAsync(std::function<void()> task) {
		return taskWorker->work(task, APP);
	}

	// persistedConfigJson, if non-empty, is parsed and passed to
	// rack.onLoad() so the script can restore its config; otherwise
	// rack.onLoad() gets no argument and the script uses its defaults.
	virtual void loadScript(const char* script, const std::string& persistedConfigJson = "") = 0;

	// True if this engine should process `script` — the module routes to
	// whichever engine's testScript() matches, so it needs no header parser
	// of its own. Each engine does a simple "@engine <name>" substring match.
	virtual bool testScript(const std::string& script) = 0;

	// Tears down the script state, running rack.onUnload() first so the
	// script can clean up (e.g. all-notes-off). Returns the JSON string of
	// onUnload()'s return value to persist, or "" if missing/errored/empty.
	virtual std::string closeState() = 0;

	// Like closeState(), but runs onUnload() WITHOUT tearing down the script
	// — used by dataToJson() to persist the live config at save time.
	// onUnload()'s messages are discarded (a save must have no audible
	// effect), unlike closeState() where they're flushed.
	virtual std::string captureConfig() = 0;

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

	// Caps on Tipsy payloads. The pending queue entry is a fixed-size POD so
	// the audio thread's shift() never heap-allocates; script payloads are
	// capped accordingly (mime types match tipsy::kMaxMimeTypeSize, data
	// matches the established sysExMaxPayloadLength cap).
	static constexpr size_t tipsyMaxMimeTypeSize = 256;
	static constexpr size_t tipsyMaxPayloadLength = 256;

	// A Tipsy message queued by sendTipsy() (script/worker thread) and
	// consumed by processTipsyOutput() (audio thread). Fixed-size so the SPSC
	// dsp::RingBuffer copies it without heap allocation on either side.
	struct TipsyMessage {
		uint16_t mimeSize;                         // length without NUL
		uint16_t dataSize;
		char mime[tipsyMaxMimeTypeSize];
		unsigned char data[tipsyMaxPayloadLength];
	};

	// Tipsy encoder for sending protocol-encoded messages via MIDI output.
	// Each engine instance gets its own encoder so scripts can initiate/send
	// independently without interfering with each other.
	tipsy::ProtocolEncoder tipsyEncoder;

	// SPSC queue of pending Tipsy messages (worker → audio). sendTipsy() only
	// copies the payload here; the actual Tipsy encoding is done on the audio
	// thread in processTipsyOutput(), which owns the encoder.
	dsp::RingBuffer<TipsyMessage, 8> tipsyPendingQueue;

	// The message the encoder is currently streaming out. The encoder holds
	// pointers into its mime/data buffers for the whole (multi-cycle) message,
	// so it must outlive the encoding — a dedicated member, not a stack local
	// (the queue's shift() hands out copies).
	TipsyMessage tipsyCurrentMessage;

	// Queues a Tipsy protocol message for transmission via the module's
	// trigger CV output. Runs on the script (worker) thread: it only copies
	// the payload into the SPSC pending queue — the actual encoding happens on
	// the audio thread in processTipsyOutput(). The message is always output on
	// the first trigger output (port 1). Returns true on success.
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

	// Outputs the next Tipsy-encoded voltage on the trigger output. Called
	// from the module's process() loop on the audio thread. If the encoder is
	// idle, starts the next pending message; then drains one encoded float
	// onto the selected trigger output. Returns true if a voltage was output.
	bool processTipsyOutput(uint8_t channel = 0) {
		if (tipsyEncoder.isDormant() && !tipsyPendingQueue.empty()) {
			// Copy into the member the encoder will point into for the whole
			// message (shift() returns a copy, so it must not be a local).
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

	// Dispatches everything queued by processInMessage()/processInTick() onto
	// the script engine via runAsync(). Virtual so tests can override it to
	// observe call counts without routing through the real queues.
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

	// Called from the UI thread while the context menu is being built.
	// Evaluates each item's onGetValue on the WORKER thread (script code must
	// never run on the UI thread), then invokes `callback` with the results.
	// The callback must not construct widgets — it only publishes the specs
	// for the menu widget to poll from step() on the UI thread.
	virtual void getContextMenus(const std::function<void(const std::vector<ScriptMenuItem>&)>& callback) = 0;
	// Called from the UI thread when the user clicks a script menu item.
	// value is 0/1 for Boolean, the selected index for Options. Runs the
	// script callback on the worker thread.
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