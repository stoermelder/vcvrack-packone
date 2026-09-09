#include "ReMove.test.hpp"

namespace __module {
	#include "ReMove.test.module.hpp"
}

void testPluginInit(rack::Plugin* p) {
	pluginInstance = p;
	p->addModel(modelReMoveLite);
}
