#include "Affix.test.hpp"

namespace __module {
	#include "Affix.test.module.hpp"
}

void testPluginInit(rack::Plugin* p) {
	pluginInstance = p;
	p->addModel(modelAffix);
	p->addModel(modelAffixMicro);
}
