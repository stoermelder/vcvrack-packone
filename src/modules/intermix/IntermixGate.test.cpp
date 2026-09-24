#include "IntermixGate.test.hpp"

namespace __module {
	#include "IntermixGate.test.module.hpp"
}

void testPluginInit(rack::Plugin* p) {
	pluginInstance = p;
	p->addModel(modelIntermixGate);
}
