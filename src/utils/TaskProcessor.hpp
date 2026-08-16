#pragma once

namespace StoermelderPackOne {

template<size_t SIZE = 8>
struct TaskProcessor {
    dsp::RingBuffer<std::function<void()>, SIZE> queue;

    void process() {
        while (queue.size() > 0) {
            std::function<void()> t = queue.shift();
            t();
        }
    }

    // Returns false when the ring buffer is full and t was dropped (never run inline).
    // Callers that must not lose a task can use the return value to retry later.
    bool enqueue(std::function<void()> t) {
        if (!queue.full()) {
            queue.push(t);
            return true;
        }
        return false;
    }

    void drain() {
        queue.clear();
    }
}; // struct TaskProcessor

} // namespace StoermelderPackOne