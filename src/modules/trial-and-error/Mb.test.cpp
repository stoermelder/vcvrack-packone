#include "Mb.test.hpp"
#include "Mb_autotag.hpp"
#include "Mb_autotag_widgets.hpp"
#include "Mb_manifests.cpp"

namespace __module {
	#include "Mb.test.module.hpp"
}
namespace __autotag {
	#include "Mb.test.autotag.hpp"
}
namespace __manifests {
	#include "Mb.test.manifests.hpp"
}

void testPluginInit(rack::Plugin* p) {
	pluginInstance = p;
	p->addModel(modelMb);
}
