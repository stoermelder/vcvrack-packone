#include "MidiKey.test.hpp"

namespace __module {
	#include "MidiKey.test.module.hpp"
}

void testPluginInit(rack::Plugin* p) {
	pluginInstance = p;
	p->addModel(modelMidiKey);
}
