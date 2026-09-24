#include "ReMove.test.hpp"

namespace __json {
	#include "ReMove.test.json.hpp"
}
namespace __process {
	#include "ReMove.test.process.hpp"
}
namespace __playback {
	#include "ReMove.test.playback.hpp"
}

void testPluginInit(rack::Plugin* p) {
	pluginInstance = p;
	p->addModel(modelReMoveLite);
}
