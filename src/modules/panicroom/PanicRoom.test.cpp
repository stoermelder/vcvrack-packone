#include "PanicRoom.test.hpp"

namespace __module {
	#include "PanicRoom.test.module.hpp"
}

void testPluginInit(rack::Plugin* p) {
	pluginInstance = p;
	p->addModel(modelPanicRoom);
}
