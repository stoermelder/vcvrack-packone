#include "RotorA.test.hpp"

namespace __module {
	#include "RotorA.test.module.hpp"
}

void testPluginInit(rack::Plugin* p) {
	pluginInstance = p;
	p->addModel(modelRotorA);
}
