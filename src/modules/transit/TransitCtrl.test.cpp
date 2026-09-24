#include "TransitCtrl.test.hpp"

namespace __module {
	#include "TransitCtrl.test.module.hpp"
}

void testPluginInit(rack::Plugin* p) {
	pluginInstance = p;
	p->addModel(modelTransit);
	p->addModel(modelTransitEx);
	p->addModel(modelTransitCtrl);
}
