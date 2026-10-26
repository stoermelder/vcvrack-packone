#pragma once
#include <cctype>
#include <cerrno>
#include <climits>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <algorithm>
#include <functional>
#include <string>
#include <midi.hpp>
#include "AhabSim.hpp"
#include "AhabEditorState.hpp"
#include "AhabUdpOutput.hpp"
#include "../../utils/CommandLine.hpp"

namespace StoermelderPackOne {
namespace Ahab {

// Executes Orca-style text commands (`name:value;part;part`) on the DSP thread.
// A template on the module type so it can be included before AhabModule is
// defined: handler bodies are only instantiated where they are used, by which
// time the module is complete. Adding a command means adding one handler and
// one row in `rows`.
template <typename MODULE>
struct AhabCommander {
	using Table = CommandTable<MODULE>;
	using Args = typename Table::Args;
	using Row = typename Table::Row;

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
			// `wr` is taken by write.
			{ "writes", &AhabCommander::writes, true },
			{ "cc", &AhabCommander::cc },
			{ "pg", &AhabCommander::pg },
			{ "bpm", &AhabCommander::bpm },
			{ "apm", &AhabCommander::apm },
			{ "udp", &AhabCommander::udp },
			{ "osc", &AhabCommander::osc },
			{ "ip", &AhabCommander::ip },
			{ "select", &AhabCommander::select },
			{ "find", &AhabCommander::find },
			{ "erase", &AhabCommander::erase },
			// `co` is ORCA's `color`, so copy has no two-letter alias.
			{ "copy", &AhabCommander::copy, true },
			{ "paste", &AhabCommander::paste },
			{ nullptr, nullptr }
		};
		return table;
	}

	// Returns true if a command was recognised and its handler ran.
	static bool execute(MODULE& m, const char* text, size_t len) {
		return Table::execute(rows(), m, text, len);
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
	// Without coordinates (`write:text`) it writes at the cursor; with only one of
	// them the command is rejected. A space becomes '.', as in orca-c's loaders,
	// since only '!'..'~' are valid glyphs; text with any other non-printable
	// character is rejected as a whole. Clipped like a UI paste.
	static void write(MODULE& m, const Args& a) {
		Glyph cells[MAX_TEXT];
		size_t len;
		if (!toGlyphs(a, cells, len)) return;
		Usz y, x;
		if (a.parts == 1) {
			AhabEditorState s = m.editor.load();
			y = s.cursor_y;
			x = s.cursor_x;
		}
		else {
			int32_t xi, yi;
			if (!a.getInt(1, xi) || !a.getInt(2, yi) || xi < 0 || yi < 0) return;
			y = (Usz)yi;
			x = (Usz)xi;
		}
		if (m.sim->pasteCells(cells, 1, len, y, x)) m.sim->notifyTick();
	}

	// writes:text — writes `text` into the selection, row by row from its top left
	// corner; at the right edge of the selection the text wraps to the next row's
	// left edge. Text longer than the selection is cut off, cells the text does not
	// reach stay untouched. Characters are handled as in `write`.
	static void writes(MODULE& m, const Args& a) {
		Glyph cells[MAX_TEXT];
		size_t len;
		if (!toGlyphs(a, cells, len)) return;
		AhabEditorState s = m.editor.load();
		if (s.sel_w == 0 || s.sel_h == 0) return;
		bool changed = false;
		size_t pos = 0;
		for (Usz row = 0; row < s.sel_h && pos < len; row++) {
			size_t n = std::min(len - pos, (size_t)s.sel_w);
			changed |= m.sim->pasteCells(cells + pos, 1, n, s.sel_y + row, s.sel_x);
			pos += n;
		}
		if (changed) m.sim->notifyTick();
	}

	// select:x;y;w;h — cursor to column x, row y and a w×h selection from there
	// (w and h default to 1). Clamped to the field like the UI does.
	static void select(MODULE& m, const Args& a) {
		int32_t x, y, w = 1, h = 1;
		if (!a.getInt(0, x) || !a.getInt(1, y) || x < 0 || y < 0) return;
		if (a.parts > 2 && !a.getInt(2, w)) return;
		if (a.parts > 3 && !a.getInt(3, h)) return;
		if (w < 1 || h < 1) return;
		selectRect(m, (Usz)y, (Usz)x, (Usz)h, (Usz)w);
	}

	// find:text — selects the first match in row-major order. The text is one row
	// (spaces count as '.'), so the match is a 1×len selection. No match: no-op.
	static void find(MODULE& m, const Args& a) {
		Glyph needle[MAX_TEXT];
		size_t len;
		if (!toGlyphs(a, needle, len)) return;
		Usz fh = m.sim->getFieldHeight();
		Usz fw = m.sim->getFieldWidth();
		if (len > fw) return;
		Glyph const* buf = m.sim->getFieldBuffer();
		for (Usz y = 0; y < fh; y++) {
			for (Usz x = 0; x + len <= fw; x++) {
				if (memcmp(buf + y * fw + x, needle, len * sizeof(Glyph)) == 0) {
					selectRect(m, y, x, 1, (Usz)len);
					return;
				}
			}
		}
	}

	// erase — clears the selection. Not undoable, like all remote edits.
	static void erase(MODULE& m, const Args&) {
		AhabEditorState s = m.editor.load();
		m.sim->cutRect(s.sel_y, s.sel_x, s.sel_h, s.sel_w);
		m.sim->notifyTick();
	}

	// copy — the selection into the module's own clipboard, never the OS clipboard.
	static void copy(MODULE& m, const Args&) {
		AhabEditorState s = m.editor.load();
		m.clipboard.copyFrom(m.sim->getFieldBuffer(), m.sim->getFieldHeight(), m.sim->getFieldWidth(),
			s.sel_y, s.sel_x, s.sel_h, s.sel_w);
	}

	// paste — the module clipboard at the selection's top left, clipped to the field; the
	// selection is resized to the pasted block. No-op if empty.
	static void paste(MODULE& m, const Args&) {
		AhabEditorState s = m.editor.load();
		AhabClipboard& c = m.clipboard;
		if (!m.sim->pasteCells(c.cells, c.height, c.width, s.sel_y, s.sel_x)) return;
		m.sim->notifyTick();
		Usz fh = m.sim->getFieldHeight(), fw = m.sim->getFieldWidth();
		Usz h = std::min(c.height, fh - s.sel_y), w = std::min(c.width, fw - s.sel_x);
		m.editor.update([&](AhabEditorState& st) { st.setSelection(s.sel_y, s.sel_x, h, w, fh, fw); });
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

	// bpm:N — sets the tempo knob (30..300) and cancels any apm ramp. No effect on
	// the tempo while CLK is patched, as for the knob itself.
	static void bpm(MODULE& m, const Args& a) {
		int32_t v;
		if (!a.getInt(0, v)) return;
		m.bpmTarget = 0;
		m.params[MODULE::BPM_PARAM].setValue((float)clampValue(v, 30, 300));
	}

	// apm:N — ramps the tempo knob towards N (30..300) by 1 BPM per step.
	static void apm(MODULE& m, const Args& a) {
		int32_t v;
		if (!a.getInt(0, v)) return;
		m.bpmTarget = clampValue(v, 30, 300);
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
	// First part as glyphs. A space becomes '.'; empty, too long or non-printable
	// text is rejected.
	static bool toGlyphs(const Args& a, Glyph* out, size_t& len) {
		if (a.parts < 1) return false;
		len = a.partLen[0];
		if (len == 0 || len > MAX_TEXT) return false;
		for (size_t i = 0; i < len; i++) {
			char c = a.part[0][i];
			if (c == ' ') c = '.';
			if (c < '!' || c > '~') return false;
			out[i] = (Glyph)c;
		}
		return true;
	}

	// Cursor to (y, x) and a selection from there, clamped to the field.
	static void selectRect(MODULE& m, Usz y, Usz x, Usz h, Usz w) {
		Usz fh = m.sim->getFieldHeight();
		Usz fw = m.sim->getFieldWidth();
		m.editor.update([&](AhabEditorState& s) {
			s.setCursor(y, x, fh, fw);
			s.setSelection(y, x, h, w, fh, fw);
		});
		// Wakes the display, so a stopped simulation still shows the new selection.
		m.sim->notifyTick();
	}

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