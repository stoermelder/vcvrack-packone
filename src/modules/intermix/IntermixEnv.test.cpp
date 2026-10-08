#include "IntermixEnv.test.hpp"

namespace __module {
	#include "IntermixEnv.test.module.hpp"
}

void testPluginInit(rack::Plugin* p) {
	pluginInstance = p;
	p->addModel(modelIntermixEnv);
}
