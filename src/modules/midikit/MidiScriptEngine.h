#pragma once
#include "../../plugin.hpp"
#include "../../utils/TaskWorker.hpp"

namespace StoermelderPackOne {
namespace MidiScript {

using rack::midi::Message;


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

	virtual void loadScript(const char* script) = 0;

	// Main interface for message processing
	virtual void processInMessage(int midiPort, Message& msg) = 0;
	virtual void processInTick(int trigPort) = 0;

	// onUnload()'s messages are queued just before the script
	// state is torn down and must still drain afterwards. Kept virtual (like
	// process()) so tests can override it to fabricate output without a real
	// script engine behind it.
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

	// Dispatches everything queued by processInMessage()/processInTick() onto
	// the script engine, asynchronously via runAsync(). Identical across
	// engines; only the per-message/-tick dispatch is engine-specific. Kept
	// virtual (rather than non-virtual) so tests can override it to observe
	// call counts without needing to route traffic through the real queues.
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