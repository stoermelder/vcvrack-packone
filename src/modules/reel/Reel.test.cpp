#include "Reel.test.hpp"

namespace __module {
	#include "Reel.test.module.hpp"
}

void testPluginInit(rack::Plugin* p) {
	pluginInstance = p;
	p->addModel(modelReel);
}
