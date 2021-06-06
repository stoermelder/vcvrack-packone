#pragma once
#include "plugin.hpp"

namespace StoermelderPackOne {
namespace Intermix {

template<int PORTS>
struct IntermixBase {
    typedef float (*IntermixMatrix)[PORTS];
	virtual IntermixMatrix expGetCurrentMatrix() { return NULL; }
	virtual IntermixMatrix expGetMatrix() { return NULL; }
	virtual int expGetChannelCount() { return 0; }
};

} // namespace Intermix
} // namespace StoermelderPackOne