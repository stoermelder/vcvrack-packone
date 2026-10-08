#include "CvMap.test.hpp"

namespace __module {
	#include "CvMap.test.module.hpp"
}

void testPluginInit(rack::Plugin* p) {
	pluginInstance = p;
	p->addModel(modelCVMap);
}
