#include "FourRounds.test.hpp"

namespace __module {
	#include "FourRounds.test.module.hpp"
}

void testPluginInit(rack::Plugin* p) {
	pluginInstance = p;
	p->addModel(modelFourRounds);
}
