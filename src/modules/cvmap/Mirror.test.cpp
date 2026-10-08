#include "Mirror.test.hpp"

namespace __module {
	#include "Mirror.test.module.hpp"
}

void testPluginInit(rack::Plugin* p) {
	pluginInstance = p;
	p->addModel(modelMirror);
	p->addModel(modelMacro);
}
