#include "Flower.test.hpp"

namespace __module {
	#include "Flower.test.module.hpp"
}
namespace __seq {
	#include "Flower.test.seq.hpp"
}
namespace __engine {
	#include "Flower.test.engine.hpp"
}
namespace __chain {
	#include "Flower.test.chain.hpp"
}

void testPluginInit(rack::Plugin* p) {
	pluginInstance = p;
	p->addModel(modelFlowerSeq);
	p->addModel(modelFlowerSeqEx);
	p->addModel(modelFlowerSeqTrig);
}

