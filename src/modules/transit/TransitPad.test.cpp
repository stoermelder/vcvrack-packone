#include "TransitPad.test.hpp"

namespace __module {
	#include "TransitPad.test.module.hpp"
}

void testPluginInit(rack::Plugin* p) {
	pluginInstance = p;
	p->addModel(modelTransit);
	p->addModel(modelTransitPad);
}
