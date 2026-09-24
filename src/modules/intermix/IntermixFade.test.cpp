#include "IntermixFade.test.hpp"

namespace __module {
	#include "IntermixFade.test.module.hpp"
}

void testPluginInit(rack::Plugin* p) {
	pluginInstance = p;
	p->addModel(modelIntermixFade);
}
