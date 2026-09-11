#include "Mb.test.hpp"
#include "Mb_autotag.hpp"
#include "Mb_autotag_widgets.hpp"
#include "Mb_manifests.cpp"
#include "Mb_v2.cpp"
#include "Mb_v2.hpp"

namespace __module {
	#include "Mb.test.module.hpp"
}
namespace __autotag {
	#include "Mb.test.autotag.hpp"
}
namespace __manifests {
	#include "Mb.test.manifests.hpp"
}
namespace __ui {
	#include "Mb.test.ui.hpp"
}

void testPluginInit(rack::Plugin* p) {
	pluginInstance = p;
	p->addModel(modelMb);
}
