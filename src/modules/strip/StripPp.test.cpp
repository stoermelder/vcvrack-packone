#include "StripPp.test.hpp"

namespace __module {
	#include "StripPp.test.module.hpp"
}

void testPluginInit(rack::Plugin* p) {
	pluginInstance = p;
	p->addModel(modelStripPp);
}
