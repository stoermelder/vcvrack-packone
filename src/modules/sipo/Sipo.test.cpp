#include "Sipo.test.hpp"

namespace __module {
	#include "Sipo.test.module.hpp"
}

void testPluginInit(rack::Plugin* p) {
	pluginInstance = p;
	p->addModel(modelSipo);
}
