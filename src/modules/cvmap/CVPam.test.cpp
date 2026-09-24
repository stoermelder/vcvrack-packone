#include "CVPam.test.hpp"

namespace __module {
	#include "CVPam.test.module.hpp"
}

void testPluginInit(rack::Plugin* p) {
	pluginInstance = p;
	p->addModel(modelCVPam);
	p->addModel(modelGlue);
}
