#include "PilePoly.test.hpp"

namespace __module {
	#include "PilePoly.test.module.hpp"
}

void testPluginInit(rack::Plugin* p) {
	pluginInstance = p;
	p->addModel(modelPilePoly);
}
