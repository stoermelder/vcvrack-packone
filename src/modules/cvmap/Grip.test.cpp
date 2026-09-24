#include "Grip.test.hpp"

namespace __module {
	#include "Grip.test.module.hpp"
}

void testPluginInit(rack::Plugin* p) {
	pluginInstance = p;
	p->addModel(modelGrip);
}
