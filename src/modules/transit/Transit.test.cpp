#include "Transit.test.hpp"

namespace __module {
	#include "Transit.test.module.hpp"
}

void testPluginInit(rack::Plugin* p) {
	pluginInstance = p;
	p->addModel(modelTransit);
}
