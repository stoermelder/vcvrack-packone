#include "Macro.test.hpp"

namespace __module {
	#include "Macro.test.module.hpp"
}

void testPluginInit(rack::Plugin* p) {
	pluginInstance = p;
	p->addModel(modelMacro);
	p->addModel(modelGlue);
}
