#include "X4.test.hpp"

namespace __module {
	#include "X4.test.module.hpp"
}

void testPluginInit(rack::Plugin* p) {
	pluginInstance = p;
	p->addModel(modelX4);
}
