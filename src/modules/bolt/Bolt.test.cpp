#include "Bolt.test.hpp"

namespace __module {
	#include "Bolt.test.module.hpp"
}

void testPluginInit(rack::Plugin* p) {
	pluginInstance = p;
	p->addModel(modelBolt);
}
