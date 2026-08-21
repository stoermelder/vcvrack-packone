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

// Transport for ORCA ';' (raw UDP datagram) and '=' (OSC-over-UDP) output.
// The module owns one; tests inject a recording fake to assert event routing
// with no sockets. All send methods are DSP-thread-safe by contract.
struct AhabUdpOutput {
	virtual ~AhabUdpOutput() = default;

	virtual void setUdpDestination(const std::string& address, const std::string& port) = 0;
	virtual std::string getUdpAddress() const = 0;
	virtual std::string getUdpPort() const = 0;

	virtual void setOscDestination(const std::string& address, const std::string& port) = 0;
	virtual std::string getOscAddress() const = 0;
	virtual std::string getOscPort() const = 0;

	virtual void sendUdpDatagram(const char* data, Usz size) = 0;
	virtual void sendOscInts(const char* osc_path, I32 const* vals, Usz count) = 0;

	// Persistence. Writes/reads the four destination keys (udpAddress, udpPort,
	// oscAddress, oscPort) into/from the passed-in object — which the module
	// passes as the "sim" sub-object, keeping the stored JSON format unchanged.
	virtual void toJson(json_t* simJ) const = 0;
	virtual void fromJson(json_t* simJ) = 0;
};

// Real implementation over orca-c's Oosc_dev UDP socket. One socket is shared
// by both the UDP and OSC destinations (preserving the original behaviour).
struct AhabOoscUdpOutput : AhabUdpOutput {
	// UDP device and destinations
	Oosc_dev* udp_dev_ = nullptr;
	std::string udpAddress_ = "127.0.0.1";
	std::string udpPort_ = "49161";
	std::string oscAddress_ = "127.0.0.1";
	std::string oscPort_ = "49162";

	~AhabOoscUdpOutput() override {
		destroyUdpDev();
	}

	std::string getUdpAddress() const override { return udpAddress_; }
	std::string getUdpPort() const override { return udpPort_; }
	std::string getOscAddress() const override { return oscAddress_; }
	std::string getOscPort() const override { return oscPort_; }

	void setUdpDestination(const std::string& address, const std::string& port) override {
		std::string newAddr = address;
		std::string newPort = port;
		trimStr(newAddr);
		trimStr(newPort);
		if (!isValidPort(newPort)) return;
		if (!newAddr.empty() && containsWhitespace(newAddr)) return;

		udpAddress_ = newAddr;
		udpPort_ = newPort;
		destroyUdpDev();
	}

	void sendUdpDatagram(const char* data, Usz size) override {
		// If a configured destination exists, use it; otherwise try defaults
		if (!udp_dev_) {
			if (!udpAddress_.empty()) {
				if (!ensureUdpDev(udpAddress_.c_str(), udpPort_.c_str())) return;
			} else {
				if (!ensureUdpDev("127.0.0.1", "49161")) return;
			}
		}
		oosc_send_datagram(udp_dev_, data, size);
	}

	void setOscDestination(const std::string& address, const std::string& port) override {
		std::string newAddr = address;
		std::string newPort = port;
		trimStr(newAddr);
		trimStr(newPort);
		if (!isValidPort(newPort)) return;
		if (!newAddr.empty() && containsWhitespace(newAddr)) return;

		oscAddress_ = newAddr;
		oscPort_ = newPort;
		destroyUdpDev();
	}

	void sendOscInts(const char* osc_path, I32 const* vals, Usz count) override {
		// Ensure UDP device exists (respect configured destination or use defaults)
		if (!udp_dev_) {
			if (!oscAddress_.empty()) {
				if (!ensureUdpDev(oscAddress_.c_str(), oscPort_.c_str())) return;
			} else {
				if (!ensureUdpDev("127.0.0.1", "49161")) return;
			}
		}
		oosc_send_int32s(udp_dev_, osc_path, vals, count);
	}

	// Persistence. The passed object is the module's "sim" sub-object; the four
	// keys are added to it so the stored JSON format is unchanged.
	void toJson(json_t* simJ) const override {
		json_object_set_new(simJ, "udpAddress", json_string(udpAddress_.c_str()));
		json_object_set_new(simJ, "udpPort", json_string(udpPort_.c_str()));
		json_object_set_new(simJ, "oscAddress", json_string(oscAddress_.c_str()));
		json_object_set_new(simJ, "oscPort", json_string(oscPort_.c_str()));
	}

	void fromJson(json_t* simJ) override {
		// Restore UDP settings if present
		json_t* addrJ = json_object_get(simJ, "udpAddress");
		json_t* portJ = json_object_get(simJ, "udpPort");
		std::string addr;
		std::string port;
		if (addrJ && json_is_string(addrJ)) addr = json_string_value(addrJ);
		if (portJ && json_is_string(portJ)) port = json_string_value(portJ);
		if (!addr.empty() || !port.empty()) {
			setUdpDestination(addr, port);
		}
		// Restore OSC settings if present
		json_t* oscAddrJ = json_object_get(simJ, "oscAddress");
		json_t* oscPortJ = json_object_get(simJ, "oscPort");
		std::string oscAddr;
		std::string oscPort;
		if (oscAddrJ && json_is_string(oscAddrJ)) oscAddr = json_string_value(oscAddrJ);
		if (oscPortJ && json_is_string(oscPortJ)) oscPort = json_string_value(oscPortJ);
		if (!oscAddr.empty() || !oscPort.empty()) {
			setOscDestination(oscAddr, oscPort);
		}
	}

	// Ensure UDP device exists (tries to create with given address/port if missing).
	bool ensureUdpDev(const char* addr, const char* port) {
		if (udp_dev_) return true;
		if (oosc_dev_create_udp(&udp_dev_, addr, port) != Oosc_udp_create_error_ok) {
			udp_dev_ = nullptr;
			return false;
		}
		// store the destination
		udpAddress_ = addr ? addr : std::string();
		udpPort_ = port ? port : std::string();
		return true;
	}

	// Destroy UDP device if present.
	void destroyUdpDev() {
		if (udp_dev_) {
			oosc_dev_destroy(udp_dev_);
			udp_dev_ = nullptr;
		}
	}
};

} // namespace Ahab
} // namespace StoermelderPackOne
