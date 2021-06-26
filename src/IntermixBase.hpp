#pragma once
#include "plugin.hpp"

namespace StoermelderPackOne {
namespace Intermix {

template<int PORTS>
struct IntermixBase {
    typedef float (*IntermixMatrix)[PORTS];
	virtual IntermixMatrix expGetCurrentMatrix() { return NULL; }
	virtual int expGetChannelCount() { return 0; }
	virtual void expSetFade(int i, float* fadeIn, float* fadeOut) { }
};

} // namespace Intermix
} // namespace StoermelderPackOne