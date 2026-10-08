#include "Infix.test.hpp"

namespace __module {
	#include "Infix.test.module.hpp"
}

void testPluginInit(rack::Plugin* p) {
	pluginInstance = p;
	p->addModel(modelInfix);
	p->addModel(modelInfixMicro);
}
