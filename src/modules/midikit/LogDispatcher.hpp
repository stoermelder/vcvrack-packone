#pragma once
#include <algorithm>
#include <functional>
#include <map>
#include <utility>
#include <vector>

namespace StoermelderPackOne {
namespace MidiKit {

// Fans log entries out to subscribers. A log queue has one consumer; whatever else
// wants to see the entries (a display's buffer, an editor's log area) subscribes here
// instead of competing for the queue, and the consumer pumps the queue through this.
// UI thread only. Listeners are called in subscription order.
template <typename ENTRY>
struct LogDispatcher {
	using Entry = ENTRY;
	using Listener = std::function<void(const ENTRY&)>;

	// Returns the id to remove the listener with.
	int add(Listener listener) {
		int id = nextId++;
		listeners[id] = std::move(listener);
		return id;
	}

	// Safe from inside a listener, including the one being removed: during a dispatch
	// the removal is only recorded and applied once the dispatch is over.
	void remove(int id) {
		if (dispatching) {
			if (listeners.count(id) && !isPending(id)) pendingRemoval.push_back(id);
			return;
		}
		listeners.erase(id);
	}

	size_t size() const {
		return listeners.size() - pendingRemoval.size();
	}

	void dispatch(const ENTRY& entry) {
		bool outer = !dispatching;
		dispatching = true;
		for (const auto& l : listeners) {
			if (!isPending(l.first)) l.second(entry);
		}
		if (outer) {
			dispatching = false;
			for (int id : pendingRemoval) listeners.erase(id);
			pendingRemoval.clear();
		}
	}

	// Drains `source` (anything with `bool tryPop(ENTRY&)`) into the listeners and
	// returns how many entries it delivered.
	template <typename SOURCE>
	size_t pump(SOURCE& source) {
		size_t n = 0;
		ENTRY entry;
		while (source.tryPop(entry)) {
			dispatch(entry);
			n++;
		}
		return n;
	}

private:
	std::map<int, Listener> listeners;
	int nextId = 0;
	bool dispatching = false;
	std::vector<int> pendingRemoval;

	bool isPending(int id) const {
		return std::find(pendingRemoval.begin(), pendingRemoval.end(), id) != pendingRemoval.end();
	}
};

} // namespace MidiKit
} // namespace StoermelderPackOne
