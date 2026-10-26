#include "UdpInput.hpp"

#ifdef _WIN32
	#include <winsock2.h>
	#include <ws2tcpip.h>
#else
	#include <arpa/inet.h>
	#include <netinet/in.h>
	#include <sys/socket.h>
	#include <sys/time.h>
	#include <unistd.h>
	#include <cerrno>
#endif

namespace StoermelderPackOne {

#ifdef _WIN32
static void closeSocket(UdpInput::Sock s) { ::closesocket((SOCKET)s); }
static int lastError() { return WSAGetLastError(); }
#else
static void closeSocket(UdpInput::Sock s) { ::close((int)s); }
static int lastError() { return errno; }
#endif

const char* UdpInput::errorText(int e) {
#ifdef _WIN32
	return e == WSAEADDRINUSE ? "port in use" : "socket error";
#else
	return e == EADDRINUSE ? "port in use" : strerror(e);
#endif
}

void UdpInput::run() {
	Sock sock = INVALID;
	uint32_t applied = generation_ - 1;   // force a first apply
	char buf[sizeof(Datagram::data) + 1];

	while (!stopFlag) {
		uint32_t gen = generation_;
		if (gen != applied) {
			applied = gen;
			if (sock != INVALID) {
				closeSocket(sock);
				sock = INVALID;
			}
			// Invalidate everything queued so far before reporting a new state.
			state = State::Off;
			activeSession_ = gen;
			received = 0;
			dropped = 0;
			if (enabled_) sock = bindSocket(portNum_, bindAll_);
			if (sock != INVALID) state = State::Listening;
			continue;
		}

		if (sock == INVALID) {
			// Nothing to listen on: sleep until the settings change.
			std::unique_lock<std::mutex> lock(wakeMutex);
			wake.wait(lock, [&]() { return stopFlag || generation_ != applied; });
			continue;
		}

		int n = (int)::recv(sock, buf, sizeof(buf), 0);
		if (n <= 0) continue;   // timeout or transient error
		// Longer than the payload limit: dropped, never truncated.
		if ((size_t)n > sizeof(Datagram::data)) { dropped++; continue; }
		// The message ends at the first line break or NUL, so a terminator (also a C
		// string's) and anything after it are not part of the command.
		int end = 0;
		while (end < n && buf[end] != '\n' && buf[end] != '\r' && buf[end] != '\0') end++;
		n = end;
		if (n == 0) continue;
		if (queue.full()) { dropped++; continue; }
		Datagram d;
		d.len = (uint16_t)n;
		memcpy(d.data, buf, n);
		d.session = applied;
		queue.push(d);
		received++;
	}

	if (sock != INVALID) closeSocket(sock);
	state = State::Off;
}

UdpInput::Sock UdpInput::bindSocket(uint16_t port, bool bindAll) {
	Sock sock = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
	if (sock == INVALID) return fail(sock);

	sockaddr_in addr;
	memset(&addr, 0, sizeof(addr));
	addr.sin_family = AF_INET;
	addr.sin_port = htons(port);
	addr.sin_addr.s_addr = htonl(bindAll ? INADDR_ANY : INADDR_LOOPBACK);
	// No SO_REUSEADDR/SO_REUSEPORT: a second listener must fail visibly
	// instead of silently splitting datagrams.
	if (::bind(sock, (sockaddr*)&addr, sizeof(addr)) != 0) return fail(sock);

	// Receive timeout so the thread notices settings changes and stop().
#ifdef _WIN32
	DWORD tv = POLL_MS;
	setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, (const char*)&tv, sizeof(tv));
#else
	timeval tv;
	tv.tv_sec = 0;
	tv.tv_usec = POLL_MS * 1000;
	setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
#endif
	return sock;
}

UdpInput::Sock UdpInput::fail(Sock sock) {
	bindError = lastError();
	if (sock != INVALID) closeSocket(sock);
	state = State::BindFailed;
	return INVALID;
}

} // namespace StoermelderPackOne
