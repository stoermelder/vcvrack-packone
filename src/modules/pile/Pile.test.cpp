#include "Pile.test.hpp"

namespace __module {
	#include "Pile.test.module.hpp"
}

void testPluginInit(rack::Plugin* p) {
	pluginInstance = p;
	p->addModel(modelPile);
}
