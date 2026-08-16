#include "history.hpp"

namespace StoermelderPackOne {
namespace vcv {

// The production history implementation, backed by the live undo stack.
struct RackHistoryAccess : HistoryAccess {
	void push(::rack::history::Action* a) override {
		APP->history->push(a);
	}
};

// The single definition of the swappable history layer's active access — external linkage,
// exactly one definition in this TU, so a mock installed in a test TU is seen by code
// compiled into the plugin dylib.
HistoryAccess* historyAccess = nullptr;

HistoryAccess& historyAccessFor() {
	static RackHistoryAccess realAccess;
	return historyAccess ? *historyAccess : realAccess;
}

} // namespace vcv
} // namespace StoermelderPackOne
