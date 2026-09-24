#include "Sail.test.hpp"

namespace __module {
	#include "Sail.test.module.hpp"
}

void testPluginInit(rack::Plugin* p) {
	pluginInstance = p;
	p->addModel(modelSail);
}
