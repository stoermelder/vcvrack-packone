#pragma once
#include "../../plugin.hpp"

namespace StoermelderPackOne {
namespace MidiScript {

using rack::midi::Message;


struct MidiScriptEngine {
	// Arbitrary but generous cap on setSysEx's payload, to keep a script from
	// building an unbounded message that then flows through the fixed-size
	// midiOutQueue and out to the driver.
	static const int sysExMaxPayloadLength = 256;

	int inputCount;
	int inputTrigCount;
	int outputTrigCount;
	int paramCount;
	int midiInputCount;
	int midiOutputCount;

	virtual ~MidiScriptEngine() { }

	virtual void runAsync(std::function<void()> task) { }
	virtual void loadScript(const char* script) { }

	// Main interface for message processing
	virtual void processInMessage(int midiPort, Message& msg) { }
	virtual void process() { }
	virtual bool processOutMessage(int& midiPort, Message& msg, int& ticks) { return false; }

	// Callbacks from the script
	virtual void writeLog(std::string, bool useTimestamp = true) { }
	virtual void writeOverlay(std::string s1, std::string s2, std::string s3) { }
	virtual void enableInput(int i) { }
	virtual float getInputVoltage(int i, uint8_t ch) { return 0.f; }
	virtual float getTrigVoltage(int i, uint8_t ch) { return 0.f; }
	virtual uint64_t getTrigTicks(int i) { return 0; }
	virtual void enableParam(int i) { }
	virtual float getParamValue(int i) { return 0.f; }
	virtual void setTrig(int i, uint8_t ch, float duration = 1e-3f) { }
	virtual void setTrigVoltage(int i, uint8_t ch, float voltage) { }

	// Queries into the script from the UI
	virtual std::string getInputName(int i) { return ""; }
	virtual std::string getParamName(int i) { return ""; }
	virtual std::string getParamFormatValue(int i) { return ""; }
};


struct MidiScriptEnginePortInfo : PortInfo {
	bool enabled;
	MidiScriptEngine* se;
	std::string bufferedName;

	std::string getName() override {
		se->runAsync([=] {
			bufferedName = se->getInputName(portId);
		});
		return enabled ? bufferedName : "<Disabled>";
	}
};


struct MidiScriptEngineParamQuantity : ParamQuantity {
	bool enabled;
	MidiScriptEngine* se;
	std::string bufferedLabel;
	std::string bufferedDisplayValue;

	std::string getLabel() override {
		return enabled ? bufferedLabel : "";
	}
	std::string getDisplayValueString() override {
		if (enabled) {
			se->runAsync([=] {
				bufferedLabel = se->getParamName(paramId);
				bufferedDisplayValue = se->getParamFormatValue(paramId);
			});
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