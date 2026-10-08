#include "Stroke.test.hpp"

namespace __module {
	#include "Stroke.test.module.hpp"
}

void testPluginInit(rack::Plugin* p) {
	pluginInstance = p;
	p->addModel(modelStroke);
}
