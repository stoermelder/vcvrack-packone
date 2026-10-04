#pragma once
#include <rack.hpp>
#include <cstddef>

namespace StoermelderPackOne {

/**
 * Lock-free single-producer single-consumer queue for element types that own
 * heap memory (a std::vector, a std::string, ...) and must be queued without
 * allocating on the producer's thread.
 *
 * dsp::RingBuffer<T> cannot do that: push() takes the element by value, so the
 * caller builds a temporary, which allocates. Here the elements live in
 * preallocated slots and the ring only carries slot indices. The producer
 * assigns into the next slot, which allocates nothing as long as the slot's
 * existing storage is big enough, and the slots keep their storage between uses
 * (size them once with the constructor's `init`).
 *
 * T must be default-constructible and copy-assignable.
 */
template <typename T, size_t N>
struct SlotRingBuffer {
	// One slot more than the ring holds: shift() frees the ring entry before the
	// consumer has copied the element out, and the producer may fill that ring
	// space at once. Slots are written in FIFO order, so the one still being read
	// is the oldest, and the producer can only come back to it after the consumer
	// has shifted again, which it does only once it is done with this one.
	enum { SLOTS = N + 1 };
	T slots[SLOTS];
	rack::dsp::RingBuffer<int, N> ring;
	// Producer only.
	int next = 0;

	SlotRingBuffer() {}

	/** Calls init(T&) on every slot once, e.g. to reserve their storage. */
	template <typename F>
	explicit SlotRingBuffer(F init) {
		for (auto& slot : slots) init(slot);
	}

	size_t size() const { return ring.size(); }
	bool empty() const { return ring.empty(); }
	bool full() const { return ring.full(); }
	/** Free slots. */
	size_t capacity() const { return ring.capacity(); }

	/** Producer. Drops the element and returns false when full. */
	bool tryPush(const T& value) {
		return tryPushWith([&](T& slot) { slot = value; });
	}

	/** Producer. Like tryPush(), but fills the slot in place with fill(T&) --
	 *  for elements that are only ever built from parts, where a T to pass in
	 *  would itself be a temporary. fill() must overwrite the whole element. */
	template <typename F>
	bool tryPushWith(F fill) {
		if (ring.full()) return false;
		int i = next;
		fill(slots[i]);
		next = (next + 1) % SLOTS;
		ring.push(i);
		return true;
	}

	/** Consumer. The oldest element, still in its slot, valid until pop(). The queue must not be empty. */
	const T& front() const {
		return slots[ring.data[ring.start % N]];
	}

	/** Consumer. Drops the oldest element without copying it. The queue must not be empty. */
	void pop() {
		ring.shift();
	}

	/** Consumer. The queue must not be empty. Returns a copy; the slot keeps its storage. */
	T shift() {
		return slots[ring.shift()];
	}
};

} // namespace StoermelderPackOne
