#include "Hive.test.hpp"

namespace __module {
	#include "Hive.test.module.hpp"
}

void testPluginInit(rack::Plugin* p) {
	pluginInstance = p;
	p->addModel(modelHive);
}
