#include "MidiMon.test.hpp"

namespace __module {
	#include "MidiMon.test.module.hpp"
}

void testPluginInit(rack::Plugin* p) {
	pluginInstance = p;
	p->addModel(modelMidiMon);
}
