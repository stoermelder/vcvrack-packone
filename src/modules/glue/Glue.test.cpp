#include "Glue.test.hpp"

namespace __module {
	#include "Glue.test.module.hpp"
}

void testPluginInit(rack::Plugin* p) {
	pluginInstance = p;
	p->addModel(modelGlue);
}
