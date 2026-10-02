#pragma once
#include <rack.hpp>
#include <atomic>
#include <cstdint>
#include <mutex>
#include "../../utils/SlotRingBuffer.hpp"
#include "../../utils/BoundedPriorityQueue.hpp"

namespace StoermelderPackOne {

// A drop-in replacement for midi::InputQueue whose consumer never locks or
// allocates. Same API and release order: by frame, then by arrival, released
// once frame <= maxFrame.
//
// Producers (driver threads, the UI thread, and other modules' process() via
// Loopback-style drivers) are serialised by a mutex, as on InputQueue. The
// consumer side (tryPop/peek/pop/clear/size) must be called from one thread
// only: the audio thread, or any thread while the engine mutex is held.
//
// Differences to InputQueue: the capacity is fixed by the template parameters;
// size() is exact on the consumer thread and approximate elsewhere; reset() is
// still midi::Input::reset(), it deselects the device and does not clear (see
// clear()).
template <size_t RING = 256, size_t HEAP = 1024>
struct MidiCInputQueue : rack::midi::Input {
	enum { SLOT_BYTES = 64 };

	// Stage 1: producers -> consumer. FIFO, unordered.
	SlotRingBuffer<rack::midi::Message, RING> arrival{[](rack::midi::Message& m) { m.bytes.reserve(SLOT_BYTES); }};
	std::mutex producerMutex;
	// A message was dropped (ring or heap full). Exchanged to false by whoever reports it.
	std::atomic<bool> overflow{false};

	// Stage 2: consumer only. Ordered by (frame, arrival).
	struct Pending {
		int64_t frame;
		uint64_t seq;
		int slot;
		// Min-heap via std::priority_queue.
		bool operator<(const Pending& o) const {
			if (frame != o.frame) return frame > o.frame;
			return seq > o.seq;
		}
	};
	rack::midi::Message pool[HEAP];
	// Stack of unused pool indices.
	int freeSlots[HEAP];
	int freeCount = int(HEAP);
	BoundedPriorityQueue<Pending, HEAP> heap;
	uint64_t nextSeq = 0;
	// Pending messages, for size() from other threads.
	std::atomic<size_t> heapCount{0};

	MidiCInputQueue() {
		for (int i = 0; i < int(HEAP); i++) {
			pool[i].bytes.reserve(SLOT_BYTES);
			freeSlots[i] = i;
		}
	}

	// Unsubscribe while the members still exist: ~Input() does it only after
	// they are destroyed, and a driver thread may still be delivering.
	~MidiCInputQueue() {
		setDeviceId(-1);
	}

	// ── midi::InputQueue API ──

	// Producer, any thread. Copies: the caller's message stays valid.
	void onMessage(const rack::midi::Message& message) override {
		std::lock_guard<std::mutex> lock(producerMutex);
		if (!arrival.tryPush(message)) overflow.store(true, std::memory_order_relaxed);
	}

	// Consumer. Same contract as InputQueue::tryPop(). Copy-assigns into
	// *messageOut, which allocates only if its capacity is too small.
	bool tryPop(rack::midi::Message* messageOut, int64_t maxFrame) {
		const rack::midi::Message* m = peek(maxFrame);
		if (!m) return false;
		*messageOut = *m;
		pop();
		return true;
	}

	// Exact on the consumer thread, approximate elsewhere.
	size_t size() const {
		return arrival.size() + heapCount.load(std::memory_order_relaxed);
	}

	// ── Additions ──

	// Consumer. The next message due at maxFrame, or null; valid until pop() or
	// clear(). Lets the caller decode in place, without tryPop()'s copy.
	const rack::midi::Message* peek(int64_t maxFrame) {
		transfer();
		if (heap.empty() || heap.top().frame > maxFrame) return nullptr;
		return &pool[heap.top().slot];
	}

	// Consumer. Drops the message peek() returned. Only after a non-null peek().
	void pop() {
		freeSlots[freeCount++] = heap.top().slot;
		// Pending is POD: nothing is freed.
		heap.pop();
		heapCount.fetch_sub(1, std::memory_order_relaxed);
	}

	// Consumer. Drops everything queued. midi::Input::reset() does not.
	void clear() {
		while (!arrival.empty()) arrival.pop();
		while (!heap.empty()) pop();
	}

	// Consumer. Moves everything that arrived into the heap.
	void transfer() {
		while (!arrival.empty()) {
			if (freeCount == 0) {
				overflow.store(true, std::memory_order_relaxed);
			}
			else {
				int s = freeSlots[--freeCount];
				// Copy-assign, never move: a move would free pool[s]'s reserved buffer.
				pool[s] = arrival.front();
				heap.push(Pending{pool[s].getFrame(), nextSeq++, s});
				heapCount.fetch_add(1, std::memory_order_relaxed);
			}
			arrival.pop();
		}
	}
};

} // namespace StoermelderPackOne
