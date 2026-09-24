#include "Raw.test.hpp"

namespace __module {
	#include "Raw.test.module.hpp"
}

void testPluginInit(rack::Plugin* p) {
	pluginInstance = p;
	p->addModel(modelRaw);
}
