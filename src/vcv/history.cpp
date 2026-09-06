#include "history.hpp"

namespace StoermelderPackOne {
namespace vcv {

// The production history implementation, backed by the live undo stack.
void RackHistoryAccess::push(::rack::history::Action* a) {
	APP->history->push(a);
}

// The shared production instance; namespace-scope so no __cxa_guard is tested on access.
// In a release build this is what the historyAccessFor() macro names directly.
RackHistoryAccess rackHistoryAccess;


#ifdef DEBUGPLUGIN
// One definition, external linkage: a mock installed in a test TU must be seen by
// code compiled into the dylib. See the declaration in the header.
HistoryAccess* historyAccess = nullptr;
HistoryAccess& historyAccessFor() {
	return historyAccess ? *historyAccess : rackHistoryAccess;
}
#endif

} // namespace vcv
} // namespace StoermelderPackOne
