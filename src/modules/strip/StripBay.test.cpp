#include "StripBay.test.hpp"

namespace __module {
	#include "StripBay.test.module.hpp"
}

void testPluginInit(rack::Plugin* p) {
	pluginInstance = p;
	p->addModel(modelStripBay4);
}
