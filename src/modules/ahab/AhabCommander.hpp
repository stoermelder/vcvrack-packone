#pragma once
#include <cctype>
#include <cerrno>
#include <climits>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <string>
#include <midi.hpp>
#include "AhabSim.hpp"
#include "AhabUdpOutput.hpp"

namespace StoermelderPackOne {
namespace Ahab {

// Executes Orca-style text commands (`name:value;part;part`) on the DSP thread.
// A template on the module type so it can be included before AhabModule is
// defined: handler bodies are only instantiated where they are used, by which
// time the module is complete. Adding a command means adding one handler and
// one row in `rows`.
template <typename MODULE>
struct AhabCommander {
	static constexpr int MAX_PARTS = 4;

	// Allocation-free counterpart of Orca's `Param`: views into the caller's buffer.
	struct Args {
		const char* part[MAX_PARTS] = {};
		size_t partLen[MAX_PARTS] = {};
		int parts = 0;

		// False if the part is missing, empty or not a whole decimal integer.
		bool getInt(int i, int32_t& out) const {
			if (i < 0 || i >= parts || partLen[i] == 0) return false;
			char buf[16];
			if (partLen[i] >= sizeof(buf)) return false;
			memcpy(buf, part[i], partLen[i]);
			buf[partLen[i]] = '\0';
			char* end = nullptr;
			errno = 0;
			long v = strtol(buf, &end, 10);
			if (end == buf || *end != '\0' || errno == ERANGE || v < INT32_MIN || v > INT32_MAX) return false;
			out = (int32_t)v;
			return true;
		}
	};

	using Handler = std::function<void(MODULE&, const Args&)>;
	struct Row { const char* name; Handler fn; };

	// Registration order matters for two-letter aliases (first match wins).
	// A function-local table, as C++11 has no inline variables; the handlers
	// may be declared anywhere in the struct.
	static const Row* rows() {
		static const Row table[] = {
			{ "play", &AhabCommander::play },
			{ "stop", &AhabCommander::stop },
			{ "run", &AhabCommander::run },
			{ "frame", &AhabCommander::frame },
			{ "skip", &AhabCommander::skip },
			{ "rewind", &AhabCommander::rewind },
			{ "write", &AhabCommander::write },
			{ "cc", &AhabCommander::cc },
			{ "pg", &AhabCommander::pg },
			{ "udp", &AhabCommander::udp },
			{ "osc", &AhabCommander::osc },
			{ "ip", &AhabCommander::ip },
			{ nullptr, nullptr }
		};
		return table;
	}

	// Returns true if a command was recognised and its handler ran.
	static bool execute(MODULE& m, const char* text, size_t len) {
		const char* colon = (const char*)memchr(text, ':', len);
		size_t nameEnd = colon ? (size_t)(colon - text) : len;

		// Name: word characters only, lowercased. Longer than any command is unknown.
		char name[16];
		size_t n = 0;
		for (size_t i = 0; i < nameEnd; i++) {
			unsigned char c = (unsigned char)text[i];
			if (!isalnum(c) && c != '_') continue;
			if (n >= sizeof(name) - 1) return false;
			name[n++] = (char)tolower(c);
		}
		name[n] = '\0';
		if (n == 0) return false;

		Args args;
		if (colon) {
			const char* p = colon + 1;
			const char* end = text + len;
			while (args.parts < MAX_PARTS) {
				const char* sep = (const char*)memchr(p, ';', end - p);
				const char* partEnd = sep ? sep : end;
				args.part[args.parts] = p;
				args.partLen[args.parts] = (size_t)(partEnd - p);
				args.parts++;
				if (!sep) break;
				p = sep + 1;
			}
		}

		for (const Row* r = rows(); r->name; r++) {
			if (strcmp(r->name, name) == 0 || (n == 2 && strncmp(r->name, name, 2) == 0)) {
				r->fn(m, args);
				return true;
			}
		}
		return false;
	}

	// play / stop — same function the RUN button uses; idempotent.
	static void play(MODULE& m, const Args&) {
		m.setRunning(true);
	}
	static void stop(MODULE& m, const Args&) {
		m.setRunning(false);
	}

	// run — one simulation step, as the CLK button does.
	static void run(MODULE& m, const Args&) {
		m.sim->step();
	}

	// frame:N — jump the tick counter to N (negative values clamp to 0).
	static void frame(MODULE& m, const Args& a) {
		int32_t v;
		if (!a.getInt(0, v)) return;
		m.sim->setTickNumber(v < 0 ? 0 : (Usz)v);
		// Redraw the tick display while stopped.
		m.sim->notifyTick();
	}

	// Moves the tick counter by `delta`, saturating at 0.
	static void shiftTick(MODULE& m, int64_t delta) {
		Usz tick = m.sim->getTickNumber();
		if (delta < 0 && (Usz)(-delta) > tick) tick = 0;
		else tick = (Usz)((int64_t)tick + delta);
		m.sim->setTickNumber(tick);
		m.sim->notifyTick();
	}

	// skip:N — tick += N
	static void skip(MODULE& m, const Args& a) {
		int32_t v;
		if (a.getInt(0, v)) shiftTick(m, v);
	}

	// rewind:N — tick -= N, saturating at 0
	static void rewind(MODULE& m, const Args& a) {
		int32_t v;
		if (a.getInt(0, v)) shiftTick(m, -(int64_t)v);
	}

	// write:text;x;y — writes `text` as a row of glyphs at column x, row y.
	// Coordinates are required (there is no cursor without a UI). A space
	// becomes '.', as in orca-c's loaders, since only '!'..'~' are valid glyphs;
	// text with any other non-printable character is rejected as a whole.
	// Clipped like a UI paste.
	static void write(MODULE& m, const Args& a) {
		int32_t x, y;
		if (!a.getInt(1, x) || !a.getInt(2, y) || x < 0 || y < 0) return;
		size_t len = a.partLen[0];
		if (len == 0 || len > MAX_TEXT) return;
		Glyph cells[MAX_TEXT];
		for (size_t i = 0; i < len; i++) {
			char c = a.part[0][i];
			if (c == ' ') c = '.';
			if (c < '!' || c > '~') return;
			cells[i] = (Glyph)c;
		}
		if (m.sim->pasteCells(cells, 1, len, (Usz)y, (Usz)x)) m.sim->notifyTick();
	}

	// cc:N — offset added to the control number of CC operators (0..127).
	static void cc(MODULE& m, const Args& a) {
		int32_t v;
		if (a.getInt(0, v)) m.midiCcOffset = (uint16_t)clampValue(v, 0, 127);
	}

	// pg:channel;bank;sub;program — bank select MSB (CC 0), LSB (CC 32), then
	// program change. The channel is required (0..15); each of bank, sub and
	// program is sent only if present and numeric (values clamp to 0..127).
	static void pg(MODULE& m, const Args& a) {
		int32_t ch;
		if (!a.getInt(0, ch)) return;
		uint8_t channel = (uint8_t)clampValue(ch, 0, 15);
		int32_t v;
		if (a.getInt(1, v)) sendControl(m, channel, 0, (uint8_t)clampValue(v, 0, 127));
		if (a.getInt(2, v)) sendControl(m, channel, 32, (uint8_t)clampValue(v, 0, 127));
		if (a.getInt(3, v)) {
			rack::midi::Message msg;
			msg.setSize(2);
			msg.bytes[0] = (uint8_t)(0xC0 | channel);
			msg.bytes[1] = (uint8_t)clampValue(v, 0, 127);
			m.sendMidiMessage(msg);
		}
	}

	// udp:out;in — UDP output port (address kept) and UDP input port. Either part
	// may be omitted; a part that isn't a valid port number is ignored. The input
	// rebinds itself on its receiver thread.
	static void udp(MODULE& m, const Args& a) {
		int32_t v;
		if (a.getInt(0, v) && isPortNumber(v)) {
			m.udpOutput->setDestination(m.udpOutput->getAddress(), std::to_string(v));
		}
		if (a.getInt(1, v) && isPortNumber(v)) {
			m.udpInput->setPort(std::to_string(v));
		}
	}

	// osc:port — OSC output port (address kept).
	static void osc(MODULE& m, const Args& a) {
		int32_t v;
		if (a.getInt(0, v) && isPortNumber(v)) {
			m.oscOutput->setDestination(m.oscOutput->getAddress(), std::to_string(v));
		}
	}

	// ip:addr — address of both outputs (ports kept). Empty or whitespace-containing
	// addresses are rejected.
	static void ip(MODULE& m, const Args& a) {
		if (a.parts < 1) return;
		std::string addr(a.part[0], a.partLen[0]);
		trimStr(addr);
		if (addr.empty() || containsWhitespace(addr)) return;
		m.udpOutput->setDestination(addr, m.udpOutput->getPort());
		m.oscOutput->setDestination(addr, m.oscOutput->getPort());
	}

private:
	static bool isPortNumber(int32_t v) {
		return v >= 1 && v <= 65535;
	}

	static int32_t clampValue(int32_t v, int32_t lo, int32_t hi) {
		return v < lo ? lo : (v > hi ? hi : v);
	}

	static void sendControl(MODULE& m, uint8_t channel, uint8_t control, uint8_t value) {
		rack::midi::Message msg;
		msg.setSize(3);
		msg.bytes[0] = (uint8_t)(0xB0 | channel);
		msg.bytes[1] = control;
		msg.bytes[2] = value;
		m.sendMidiMessage(msg);
	}

	static constexpr size_t MAX_TEXT = 256;
};

} // namespace Ahab
} // namespace StoermelderPackOne