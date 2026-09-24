#include "MidiStep.test.hpp"

namespace __module {
	#include "MidiStep.test.module.hpp"
}

void testPluginInit(rack::Plugin* p) {
	pluginInstance = p;
	p->addModel(modelMidiStep);
}
