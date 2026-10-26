#pragma once
#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <cstring>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <jansson.h>
#include <dsp/ringbuffer.hpp>
#include "net.hpp"

namespace StoermelderPackOne {

// The only thing that crosses from the receiver thread to the DSP thread.
struct Datagram {
	uint16_t len;
	char data[256];
	// Receiver session that produced it; see UdpInput::tryPop().
	uint32_t session;
};

// Pure transport: bytes in, no command knowledge. One receiver thread produces,
// the DSP thread consumes (tryPop). Virtual so tests can inject a scripted fake.
//
// The receiver thread is started on first use and lives until stop() / the
// destructor. Changing a setting only updates atomics and wakes the thread,
// which rebinds itself: nothing on the UI/load path ever joins or waits (Rack
// calls onReset()/dataFromJson() with the engine lock held, which the audio
// thread needs).
struct UdpInput {
	enum class State { Off, Listening, BindFailed };

	static constexpr const char* DEFAULT_PORT = "49160";
	// Upper bound for the receiver to notice a changed setting or stop().
	static constexpr int POLL_MS = 100;

	std::function<void(const Datagram&)> handler_;

	// Settings. The string is for UI/JSON only; the receiver reads the atomics.
	std::atomic<bool> enabled_{false};
	std::string port_ = DEFAULT_PORT;
	std::atomic<uint16_t> portNum_{49160};
	std::atomic<bool> bindAll_{false};

	// Platform socket handle (SOCKET or fd), kept opaque so no platform headers
	// are needed here; they are confined to UdpInput.cpp.
	using Sock = intptr_t;
	static constexpr Sock INVALID = -1;

	std::thread thread;
	std::mutex wakeMutex;
	std::condition_variable wake;
	std::atomic<bool> stopFlag{false};
	// Bumped by every settings change; the receiver rebinds when it differs from
	// the generation it last applied.
	std::atomic<uint32_t> generation_{0};
	// Session whose datagrams are valid: the generation of the current binding.
	std::atomic<uint32_t> activeSession_{0};
	std::atomic<State> state{State::Off};
	std::atomic<int> bindError{0};
	std::atomic<uint32_t> received{0};
	std::atomic<uint32_t> dropped{0};
	rack::dsp::RingBuffer<Datagram, 64> queue;

	virtual ~UdpInput() {
		stop();
	}

	// Ends the receiver thread for good (joins; up to POLL_MS). Safe to call
	// repeatedly. Only for destruction, never for a settings change.
	virtual void stop() {
		state = State::Off;
		stopFlag = true;
		{
			std::lock_guard<std::mutex> lock(wakeMutex);
		}
		wake.notify_all();
		if (thread.joinable()) thread.join();
	}

	// DSP thread only, and the only place that ever pops the queue (the UI thread
	// never clears it). Yields nothing unless listening, and throws away datagrams
	// from an earlier binding or from a switched-off input, so commands queued
	// before a switch-off or port change never execute afterwards and stale
	// entries don't fill the queue while the input is off.
	virtual bool tryPop(Datagram& d) {
		while (!queue.empty()) {
			d = queue.shift();
			// State is read after the shift: an entry can only have been pushed
			// once the receiver had set Listening, so a valid one is never lost.
			if (d.session == activeSession_ && state == State::Listening) return true;
		}
		return false;
	}

	// Receives each datagram popped by poll(). Set once, before the first poll().
	void setHandler(std::function<void(const Datagram&)> fn) {
		handler_ = std::move(fn);
	}

	// DSP thread only: hands at most one queued datagram to the handler.
	void poll() {
		Datagram d;
		if (tryPop(d) && handler_) handler_(d);
	}

	// --- Settings (UI thread / load only). A setter that doesn't change anything
	// does nothing; otherwise the receiver is told to rebind, without waiting. ---

	bool isEnabled() const {
		return enabled_;
	}
	void setEnabled(bool v) {
		update(v, port_, bindAll_);
	}
	bool isBindAll() const {
		return bindAll_;
	}
	void setBindAll(bool v) {
		update(enabled_, port_, v);
	}
	const std::string& getPort() const {
		return port_;
	}
	// Returns false (and changes nothing) if `port` is not a valid port number.
	bool setPort(std::string port) {
		trimStr(port);
		if (port.empty() || !isValidPort(port)) return false;
		update(enabled_, port, bindAll_);
		return true;
	}

	// Back to the defaults: off, 49160, loopback only.
	void reset() {
		update(false, DEFAULT_PORT, false);
	}

	virtual std::string getStatusText() const {
		switch (state.load()) {
			case State::Listening:
				return "Listening (" + std::to_string(received.load()) + " received, " + std::to_string(dropped.load()) + " dropped)";
			case State::BindFailed:
				return std::string("Bind failed: ") + errorText(bindError.load());
			default:
				return enabled_ ? "Starting" : "Off";
		}
	}

	json_t* toJson() const {
		json_t* j = json_object();
		json_object_set_new(j, "enabled", json_boolean(enabled_));
		json_object_set_new(j, "port", json_string(port_.c_str()));
		json_object_set_new(j, "bindAll", json_boolean(bindAll_));
		return j;
	}

	// Absent or malformed settings load as the defaults (input off).
	void fromJson(json_t* j) {
		bool enabled = false;
		std::string port = DEFAULT_PORT;
		bool bindAll = false;
		if (j && json_is_object(j)) {
			json_t* enabledJ = json_object_get(j, "enabled");
			if (enabledJ) enabled = json_boolean_value(enabledJ);
			json_t* portJ = json_object_get(j, "port");
			if (portJ && json_is_string(portJ)) {
				std::string p = json_string_value(portJ);
				if (!p.empty() && isValidPort(p)) port = p;
			}
			json_t* bindAllJ = json_object_get(j, "bindAll");
			if (bindAllJ) bindAll = json_boolean_value(bindAllJ);
		}
		update(enabled, port, bindAll);
	}

	// Called once per effective settings change, after the new values are stored.
	// The base starts the receiver thread on first use and wakes it; tests
	// override this to observe changes without opening sockets.
	virtual void onSettingsChanged() {
		{
			std::lock_guard<std::mutex> lock(wakeMutex);
			generation_++;
		}
		wake.notify_all();
		if (!thread.joinable() && enabled_ && !stopFlag) {
			thread = std::thread([this]() { run(); });
		}
	}

	void update(bool enabled, const std::string& port, bool bindAll) {
		if (enabled == enabled_ && port == port_ && bindAll == bindAll_) return;
		// Settings first, then the notification (which bumps the generation).
		port_ = port;
		portNum_ = (uint16_t)std::stoi(port);
		bindAll_ = bindAll;
		enabled_ = enabled;
		onSettingsChanged();
	}

	static const char* errorText(int e);

	// Receiver thread. Owns the socket: binds according to the current settings,
	// and rebinds whenever the generation changes.
	void run();

	// Returns a bound socket with a receive timeout, or INVALID (and BindFailed).
	Sock bindSocket(uint16_t port, bool bindAll);
	Sock fail(Sock sock);
};

} // namespace StoermelderPackOne
