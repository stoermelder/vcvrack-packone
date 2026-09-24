#include "IntermixCv.test.hpp"

namespace __module {
	#include "IntermixCv.test.module.hpp"
}

void testPluginInit(rack::Plugin* p) {
	pluginInstance = p;
	p->addModel(modelIntermixCv);
}
