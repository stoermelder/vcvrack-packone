#include "Arena.test.hpp"

namespace __core {
	#include "Arena.test.module.hpp"
}
namespace __seq {
	#include "Arena.test.seq.hpp"
}
namespace __seqinput {
	#include "Arena.test.seqinput.hpp"
}


void testPluginInit(rack::Plugin* p) {
	pluginInstance = p;
	p->addModel(modelArena);
}
