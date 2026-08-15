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

    void enqueue(std::function<void()> t) {
        if (!queue.full()) {
            queue.push(t);
        }
    }

    void drain() {
        queue.clear();
    }
}; // struct TaskProcessor

} // namespace StoermelderPackOne