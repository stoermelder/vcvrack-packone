#pragma once
#include <atomic>
#include <utility>

/**
 * Lock-free single-producer single-consumer "latest value" buffer.
 *
 * One thread calls store(), one (different) thread calls load(), peek(),
 * or load_if_new(). Internally uses triple buffering: each side owns a
 * private slot, and a single shared "back" slot is exchanged atomically.
 * The only atomic operation is std::atomic<uint8_t>::exchange — no locks,
 * no heap allocation.
 *
 * Slot index invariant: write_idx, read_idx, and the back-slot index are
 * always all three different values from {0, 1, 2}.
 */
template<typename T>
class SpscLatestValue {
	T    slots[3];
	int  write_idx = 0;   // writer-thread private
	int  read_idx  = 1;   // reader-thread private
	// bits [1:0] = back slot index, bit 2 = dirty (new value available)
	std::atomic<uint8_t> back{2};

	void consume() {
		// acquire: pairs with the release in store(), guaranteeing that
		// slots[new read_idx] (written before that release) is visible here.
		uint8_t prev = back.exchange(
			static_cast<uint8_t>(read_idx),
			std::memory_order_acquire);
		read_idx = prev & 0x3;
	}

public:
	SpscLatestValue() = default;
	explicit SpscLatestValue(T init) : slots{init, init, init} {}

	// Writer thread only.
	void store(T value) {
		slots[write_idx] = std::move(value);
		// release: makes the slots[write_idx] write above visible to any
		// reader that subsequently acquires from `back`.
		uint8_t prev = back.exchange(
			static_cast<uint8_t>(write_idx | 0x4),
			std::memory_order_release);
		write_idx = prev & 0x3;
	}

	// Reader thread only. Returns latest value by copy.
	T load() {
		// acquire: if dirty, synchronises with the store() release so the
		// slot data is visible before consume() reads it.
		uint8_t s = back.load(std::memory_order_acquire);
		if (s & 0x4) consume();
		return slots[read_idx];
	}

	// Reader thread only. Returns reference to latest value.
	// Stable until the next call to load(), peek(), or load_if_new().
	const T& peek() {
		// acquire: same synchronisation as load() above.
		uint8_t s = back.load(std::memory_order_acquire);
		if (s & 0x4) consume();
		return slots[read_idx];
	}

	// Reader thread only. Updates `out` and returns true only if a new
	// value has been stored since the last call. `out` is untouched when
	// returning false.
	bool load_if_new(T& out) {
		// acquire: same synchronisation as load() above.
		uint8_t s = back.load(std::memory_order_acquire);
		if (!(s & 0x4)) return false;
		consume();
		out = slots[read_idx];
		return true;
	}
};
