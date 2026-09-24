#include "CVMapMicro.test.hpp"

namespace __module {
	#include "CVMapMicro.test.module.hpp"
}

void testPluginInit(rack::Plugin* p) {
	pluginInstance = p;
	p->addModel(modelCVMapMicro);
}
