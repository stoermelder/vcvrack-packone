#include "MidiEsx.test.hpp"

namespace __module {
	#include "MidiEsx.test.module.hpp"
}

void testPluginInit(rack::Plugin* p) {
	pluginInstance = p;
	p->addModel(modelMidiEsx);
}
