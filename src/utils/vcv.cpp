#include "vcv_cables.hpp"

namespace StoermelderPackOne {
namespace vcv {

// The single definition of the swappable cable layer's active access. See the comment on
// the `extern` declaration in vcv_cables.hpp for why this must live in exactly one TU
// rather than being `static` in the header.
CableAccess* cableAccess = nullptr;

CableAccess& cableAccessFor() {
	static RackCableAccess rackAccess;
	return cableAccess ? *cableAccess : rackAccess;
}

} // namespace vcv
} // namespace StoermelderPackOne
