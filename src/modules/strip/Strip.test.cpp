#include "Strip.test.hpp"

namespace __module {
	#include "Strip.test.module.hpp"
}

void testPluginInit(rack::Plugin* p) {
	pluginInstance = p;
	p->addModel(modelStrip);
}
