#include "MidiCatMem.test.hpp"

namespace __module {
	#include "MidiCatMem.test.module.hpp"
}

void testPluginInit(rack::Plugin* p) {
	pluginInstance = p;
	p->addModel(modelMidiCat);
	p->addModel(modelMidiCatMem);
}
