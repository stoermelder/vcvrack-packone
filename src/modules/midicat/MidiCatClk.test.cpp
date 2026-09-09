#include "MidiCatClk.test.hpp"

namespace __module {
	#include "MidiCatClk.test.module.hpp"
}

void testPluginInit(rack::Plugin* p) {
	pluginInstance = p;
	p->addModel(modelMidiCat);
	p->addModel(modelMidiCatClk);
}
