#include "Dirt.test.hpp"

namespace __module {
	#include "Dirt.test.module.hpp"
}

void testPluginInit(rack::Plugin* p) {
	pluginInstance = p;
	p->addModel(modelDirt);
}
