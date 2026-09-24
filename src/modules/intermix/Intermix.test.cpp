#include "Intermix.test.hpp"

namespace __module {
	#include "Intermix.test.module.hpp"
}

void testPluginInit(rack::Plugin* p) {
	pluginInstance = p;
	p->addModel(modelIntermix);
}
