#include "MidiPlug.test.hpp"

namespace __module {
	#include "MidiPlug.test.module.hpp"
}

void testPluginInit(rack::Plugin* p) {
	pluginInstance = p;
	p->addModel(modelMidiPlug);
}
