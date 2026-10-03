#pragma once
#include <algorithm>
#include <cstddef>
#include <queue>

namespace StoermelderPackOne {

// A priority queue that is reserved up front and never grows past MAX, so it
// never reallocates on the audio thread (the caller checks full() before a
// push). Dropping entries in place keeps the capacity too.
template <typename T, size_t MAX>
struct BoundedPriorityQueue : std::priority_queue<T> {
	BoundedPriorityQueue() {
		this->c.reserve(MAX);
	}

	bool full() const {
		return this->c.size() >= MAX;
	}

	// Takes the front element out by move: top() is const, so copying it out
	// would allocate (midi::Message::bytes) on the audio thread.
	T popTop() {
		std::pop_heap(this->c.begin(), this->c.end(), this->comp);
		T t = std::move(this->c.back());
		this->c.pop_back();
		return t;
	}

	// Drops every element `pred` matches, keeping the heap and the capacity.
	template <typename Pred>
	void removeIf(Pred pred) {
		auto it = std::remove_if(this->c.begin(), this->c.end(), pred);
		if (it == this->c.end()) return;
		this->c.erase(it, this->c.end());
		std::make_heap(this->c.begin(), this->c.end(), this->comp);
	}
};

} // namespace StoermelderPackOne
