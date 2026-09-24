#include "Goto.test.hpp"

namespace __module {
	#include "Goto.test.module.hpp"
}

void testPluginInit(rack::Plugin* p) {
	pluginInstance = p;
	p->addModel(modelGoto);
}
