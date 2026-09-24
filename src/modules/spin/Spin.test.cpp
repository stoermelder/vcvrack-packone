#include "Spin.test.hpp"

namespace __module {
	#include "Spin.test.module.hpp"
}

void testPluginInit(rack::Plugin* p) {
	pluginInstance = p;
	p->addModel(modelSpin);
}
