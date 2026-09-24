#include "Orbit.test.hpp"

namespace __module {
	#include "Orbit.test.module.hpp"
}

void testPluginInit(rack::Plugin* p) {
	pluginInstance = p;
	p->addModel(modelOrbit);
}
