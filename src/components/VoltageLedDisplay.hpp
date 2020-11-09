#include "plugin.hpp"

namespace StoermelderPackOne {

template<typename MODULE>
struct VoltageLedDisplay : StoermelderLedDisplay {
	MODULE* module;
	void step() override {
		if (module) {
			text = string::f("%+06.2f", std::max(std::min(module->getCurrentVoltage(), 99.99f), -99.99f));
		} 
		StoermelderLedDisplay::step();
	}
};

} // namespace StoermelderPackOne