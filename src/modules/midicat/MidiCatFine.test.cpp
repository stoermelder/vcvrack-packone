#include "MidiCatFine.test.hpp"

namespace __module {
	#include "MidiCatFine.test.module.hpp"
}

void testPluginInit(rack::Plugin* p) {
	pluginInstance = p;
	p->addModel(modelMidiCat);
	p->addModel(modelMidiCatFine);
}
