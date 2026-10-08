#include "engine.hpp"

namespace StoermelderPackOne {
namespace vcv {

int64_t RealEngineAccess::getFrame() const {
	return APP->engine->getFrame();
}

int64_t RealEngineAccess::getBlockFrame() const {
	return APP->engine->getBlockFrame();
}

int64_t RealEngineAccess::getBlockFrames() const {
	return APP->engine->getBlockFrames();
}

// The shared production instance; namespace-scope so no __cxa_guard is tested on access.
// In a release build this is what the engineAccessFor() macro names directly.
RealEngineAccess realEngineAccess;


#ifdef DEBUGPLUGIN
// One definition, external linkage: a mock installed in a test TU must be seen by
// code compiled into the dylib. See the declaration in the header.
EngineAccess* engineAccess = nullptr;
EngineAccess& engineAccessFor() {
	return engineAccess ? *engineAccess : realEngineAccess;
}
#endif

} // namespace vcv
} // namespace StoermelderPackOne
