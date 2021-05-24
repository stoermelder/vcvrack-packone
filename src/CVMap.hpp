#pragma once
#include "plugin.hpp"

namespace StoermelderPackOne {
namespace CVMap {

struct CVMapCtxBase : Module {
	virtual std::string getCVMapId() { return ""; }
};

} // namespace CVMap
} // namespace StoermelderPackOne