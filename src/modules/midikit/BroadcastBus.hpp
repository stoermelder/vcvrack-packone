#pragma once
#include <algorithm>
#include <cassert>
#include <jansson.h>
#include <memory>
#include <mutex>
#include <vector>

namespace StoermelderPackOne {
namespace MidiScript {

// Serialised size cap of one broadcast, measured once at send.
static constexpr size_t broadcastMaxBytes = 4096;

// Longest topic, in bytes.
static constexpr size_t broadcastTopicMaxBytes = 64;

// One broadcast in a receiver's broadcastInQueue. Not a POD: only the worker
// touches it, so it can hold a shared_ptr and the size is a policy limit.
struct InboundBroadcast {
	// Immutable after send, shared by every receiver's queue.
	std::shared_ptr<json_t> value;
	// The sender's currentInFrame, -1 outside an event.
	int64_t frame = -1;
	// Optional label chosen by the sender. Not used for routing: every receiver
	// gets every broadcast, and its script filters.
	bool hasTopic = false;
	std::string topic;
};

// The engines whose script defines rack.onBroadcast. One per worker, so a send
// on the worker thread can push into another engine's broadcastInQueue without
// locking against script execution.
//
// The mutex is for the UI thread: a module's destruction leaves the bus even if
// the unload that would normally remove it timed out. Never taken on the audio
// thread.
//
// A template on the engine so it needs no complete engine type, which depends on
// the bus itself (through WorkerDomain). ENGINE provides broadcastInQueue and
// onBroadcastDropped().
template <typename ENGINE>
struct BasicBroadcastBus {
	std::vector<ENGINE*> receivers;
	std::mutex mutex;

	// Worker, at load. Idempotent.
	void join(ENGINE* e) {
		std::lock_guard<std::mutex> lock(mutex);
		if (std::find(receivers.begin(), receivers.end(), e) == receivers.end()) {
			receivers.push_back(e);
		}
	}

	// Worker (unload) or UI (destructor). A no-op for an engine not in the list.
	void leave(ENGINE* e) {
		std::lock_guard<std::mutex> lock(mutex);
		receivers.erase(std::remove(receivers.begin(), receivers.end(), e), receivers.end());
	}

	// Worker. Pushes `value` to every receiver except `from` that has room, and
	// returns how many it reached. A receiver with a full queue loses the message
	// (never overwritten) and is told through onBroadcastDropped().
	int send(ENGINE* from, const std::shared_ptr<json_t>& value, int64_t frame, const std::string* topic = nullptr) {
		std::lock_guard<std::mutex> lock(mutex);
		int count = 0;
		for (ENGINE* e : receivers) {
			if (e == from) continue;
			// push() does not bounds-check, see MidiScriptEngine::pushInQueue().
			if (e->broadcastInQueue.full()) {
				e->onBroadcastDropped();
				continue;
			}
			InboundBroadcast m;
			m.value = value;
			m.frame = frame;
			if (topic) {
				m.hasTopic = true;
				m.topic = *topic;
			}
			e->broadcastInQueue.push(std::move(m));
			count++;
		}
		return count;
	}

	~BasicBroadcastBus() {
		assert(receivers.empty());
	}
};

} // namespace MidiScript
} // namespace StoermelderPackOne