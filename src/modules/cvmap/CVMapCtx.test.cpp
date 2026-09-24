#include "CVMapCtx.test.hpp"

namespace __module {
	#include "CVMapCtx.test.module.hpp"
}

void testPluginInit(rack::Plugin* p) {
	pluginInstance = p;
	p->addModel(modelCVMapCtx);
}
