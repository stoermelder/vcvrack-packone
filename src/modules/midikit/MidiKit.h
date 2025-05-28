#pragma once
#include "../../plugin.hpp"

namespace StoermelderPackOne {
namespace MidiKit {

using rack::midi::Message;

struct ScriptEngine {
	int inputCount;
	int trigCount;
	int paramCount;
	int midiInputCount;
	int midiOutputCount;

	virtual ~ScriptEngine() { }
	std::function<void(std::string)> logCallback;
	std::function<void(std::string, std::string, std::string)> overlayCallback;
	std::function<void(int, Message&, uint64_t)> midiCallback;
	std::function<void(int)> inputEnable;
	std::function<float(int, int)> inputGetVoltage;
	std::function<float(int)> trigGetVoltage;
	std::function<uint64_t(int)> trigGetTicks;
	std::function<void(int)> paramEnable;
	std::function<float(int)> paramGetValue;

	virtual void loadScript(const char* script) { }
	virtual void process(int midiPort, Message& msg) { }

	virtual std::string getInputName(int i) { return ""; }
	virtual std::string getParamName(int i) { return ""; }
	virtual std::string getParamFormatValue(int i) { return ""; }
};

} // namespace MidiKit
} // namespace StoermelderPackOne