#include "TransitPad.test.hpp"

namespace __module {
	// Order is load-bearing: each file uses static helpers declared earlier in
	// this list rather than redeclaring them (all four land in one namespace,
	// so there's nothing to \#include separately). module.hpp's fireTrigger()
	// is used by expander.hpp; expander.hpp's connectPad()/PadRig/bindParam()/
	// connectMixInputs()/setMixVoltage() are used by both ui.hpp and e2e.hpp.
	#include "TransitPad.test.module.hpp"
	#include "TransitPad.test.expander.hpp"
	#include "TransitPad.test.ui.hpp"
	#include "TransitPad.test.e2e.hpp"
}

void testPluginInit(rack::Plugin* p) {
	pluginInstance = p;
	p->addModel(modelTransit);
	p->addModel(modelTransitEx);
	p->addModel(modelTransitPad);
}