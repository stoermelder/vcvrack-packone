#pragma once
#include <string>
#include <cctype>
#include <jansson.h>

extern "C" {
	#include <orca-c/base.h> // Usz, I32
	#include <orca-c/osc_out.h> // Oosc_dev + oosc_* UDP/OSC socket functions
}

namespace StoermelderPackOne {
namespace Ahab {

static inline void trimStr(std::string &s) {
	// left
	size_t i = 0;
	while (i < s.size() && std::isspace((unsigned char)s[i])) ++i;
	s.erase(0, i);
	// right
	if (!s.empty()) {
		size_t j = s.size() - 1;
		while (j != (size_t)-1 && std::isspace((unsigned char)s[j])) --j;
		s.erase(j + 1);
	}
}

static inline bool isValidPort(const std::string &s) {
	if (s.empty()) return true;
	for (char c : s) if (!std::isdigit((unsigned char)c)) return false;
	long p = 0;
	try { p = std::stol(s); } catch (...) { return false; }
	return (p >= 1 && p <= 65535);
}

static inline bool containsWhitespace(const std::string &s) {
	for (char c : s) if (std::isspace((unsigned char)c)) return true;
	return false;
}

// One transport class, instantiated twice (UDP + OSC). Each instance owns its
// own lazily-created Oosc_dev socket, so configuring both destinations no
// longer makes one send steal the other's socket.
// Sockets are created on first send — users rarely use either
// output, so we avoid opening a socket until it is actually needed.
//
// The two instances differ only in their default port and the JSON keys they
// persist under (udpAddress/udpPort vs oscAddress/oscPort), selected by Kind.
struct AhabOoscOutput {
	enum class Kind { UDP, OSC };

	Kind kind;
	Oosc_dev* dev_ = nullptr;   // bound to address_ / port_

	std::string address_ = "127.0.0.1";
	std::string port_ = "49161";

	AhabOoscOutput(Kind k) : kind(k) {
		port_ = (k == Kind::UDP) ? "49161" : "49162";
	}
	virtual ~AhabOoscOutput() {
		destroyDev();
	}

	virtual std::string getAddress() const {
		return address_;
	}
	virtual std::string getPort() const {
		return port_;
	}

	virtual void setDestination(const std::string& address, const std::string& port) {
		std::string newAddr = address;
		std::string newPort = port;
		trimStr(newAddr);
		trimStr(newPort);
		if (!isValidPort(newPort)) return;
		if (!newAddr.empty() && containsWhitespace(newAddr)) return;

		address_ = newAddr;
		port_ = newPort;
		destroyDev();   // only this socket depends on this destination
	}

	// UDP datagram (ORCA ';' operator).
	virtual void sendDatagram(const char* data, Usz size) {
		if (!dev_) {
			if (!address_.empty()) {
				if (!ensureDev(address_.c_str(), port_.c_str())) return;
			} else {
				if (!ensureDev("127.0.0.1", "49161")) return;
			}
		}
		oosc_send_datagram(dev_, data, size);
	}

	// OSC ints (ORCA '=' operator).
	virtual void sendInts(const char* osc_path, I32 const* vals, Usz count) {
		if (!dev_) {
			if (!address_.empty()) {
				if (!ensureDev(address_.c_str(), port_.c_str())) return;
			} else {
				if (!ensureDev("127.0.0.1", "49161")) return;
			}
		}
		oosc_send_int32s(dev_, osc_path, vals, count);
	}

	// Persistence: writes/reads the address / port keys (udpAddress/udpPort or
	// oscAddress/oscPort, chosen by Kind) into the module's "sim" sub-object,
	// keeping the stored JSON format unchanged.
	virtual void toJson(json_t* simJ) const {
		const char* addrKey = (kind == Kind::UDP) ? "udpAddress" : "oscAddress";
		const char* portKey = (kind == Kind::UDP) ? "udpPort" : "oscPort";
		json_object_set_new(simJ, addrKey, json_string(address_.c_str()));
		json_object_set_new(simJ, portKey, json_string(port_.c_str()));
	}

	virtual void fromJson(json_t* simJ) {
		const char* addrKey = (kind == Kind::UDP) ? "udpAddress" : "oscAddress";
		const char* portKey = (kind == Kind::UDP) ? "udpPort" : "oscPort";
		json_t* addrJ = json_object_get(simJ, addrKey);
		json_t* portJ = json_object_get(simJ, portKey);
		std::string addr;
		std::string port;
		if (addrJ && json_is_string(addrJ)) addr = json_string_value(addrJ);
		if (portJ && json_is_string(portJ)) port = json_string_value(portJ);
		if (!addr.empty() || !port.empty()) setDestination(addr, port);
	}

	bool ensureDev(const char* addr, const char* port) {
		if (dev_) return true;
		if (oosc_dev_create_udp(&dev_, addr, port) != Oosc_udp_create_error_ok) {
			dev_ = nullptr;
			return false;
		}
		address_ = addr ? addr : std::string();
		port_ = port ? port : std::string();
		return true;
	}

	void destroyDev() {
		if (dev_) {
			oosc_dev_destroy(dev_);
			dev_ = nullptr;
		}
	}
};

} // namespace Ahab
} // namespace StoermelderPackOne
