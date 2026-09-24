#include "TransitEx.test.hpp"

namespace __module {
	#include "TransitEx.test.module.hpp"
}

void testPluginInit(rack::Plugin* p) {
	pluginInstance = p;
	p->addModel(modelTransit);
	p->addModel(modelTransitEx);
}
