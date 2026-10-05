#pragma once
#include "MidiProcessor.hpp"
#include <string>

namespace StoermelderPackOne {
namespace MidiText {

/** The one-line text of a MIDI message, shared by MIDI-MON's log and MIDI-KIT's
 *  midi.toString(), so the two read alike. The text is built in two steps because
 *  MIDI-MON records on the dsp thread, which must not allocate: classify() only
 *  picks a Kind and two numbers (plain data), format() builds the string later. */
enum class Kind : uint8_t {
	NOTE_ON,			// channel, x = note, y = velocity
	NOTE_OFF,
	KEY_PRESSURE,
	CC,					// channel, x = cc, y = value
	CC_14BIT,
	RPN_RESET,			// channel
	RPN_VALUE,			// channel, x = param, y = value
	RPN_PARAM,			// channel, x = param
	NRPN_VALUE,
	NRPN_PARAM,
	PROGRAM_CHANGE,		// channel, x = program
	CHANNEL_PRESSURE,	// channel, x = value
	PITCH_BEND,			// channel, x = value
	SYSEX,				// x = data bytes
	SONG_POINTER,		// x = value
	SONG_SELECT,		// x = song
	CLOCK,
	START,
	CONTINUE,
	STOP,
	RESET,
	MTC_QUARTER_FRAME,	// x = piece, y = value
	TUNE_REQUEST,
	ACTIVE_SENSING,
	UNKNOWN,			// x = status byte
	EMPTY
};

struct Fields {
	Kind kind = Kind::EMPTY;
	uint8_t channel = 0;	// 0-based
	int32_t x = 0;
	int32_t y = 0;
};

/** The Fields of a decoded message. No allocation. Handles the kinds MessageEx knows. */
inline Fields classify(const MessageEx& m) {
	using T = MessageEx::Type;
	Fields f;
	f.channel = m.getChannel();
	switch (m.type) {
		case T::NOTE_ON:          f.kind = Kind::NOTE_ON; f.x = m.getNote(); f.y = m.getValue(); break;
		case T::NOTE_OFF:         f.kind = Kind::NOTE_OFF; f.x = m.getNote(); f.y = m.getValue(); break;
		case T::KEY_PRESSURE:     f.kind = Kind::KEY_PRESSURE; f.x = m.getNote(); f.y = m.getValue(); break;
		case T::CC:               f.kind = Kind::CC; f.x = m.getNote(); f.y = m.getValue(); break;
		case T::CC_14BIT:         f.kind = Kind::CC_14BIT; f.x = m.getNote(); f.y = m.getValue(); break;
		case T::RPN:
			if (m.getParamNumber() < 0) f.kind = Kind::RPN_RESET;
			else if (m.hasValue()) { f.kind = Kind::RPN_VALUE; f.x = m.getParamNumber(); f.y = m.getValue(); }
			else { f.kind = Kind::RPN_PARAM; f.x = m.getParamNumber(); }
			break;
		case T::NRPN:
			if (m.hasValue()) { f.kind = Kind::NRPN_VALUE; f.x = m.getParamNumber(); f.y = m.getValue(); }
			else { f.kind = Kind::NRPN_PARAM; f.x = m.getParamNumber(); }
			break;
		case T::PROGRAM_CHANGE:   f.kind = Kind::PROGRAM_CHANGE; f.x = m.getNote(); break;
		case T::CHANNEL_PRESSURE: f.kind = Kind::CHANNEL_PRESSURE; f.x = m.getNote(); break;
		case T::PITCH_BEND:       f.kind = Kind::PITCH_BEND; f.x = m.getValue(); break;
		case T::SYSEX: {
			// F0 <data> F7, but a script can build a message without the F7.
			int size = m.getSysExSize();
			f.kind = Kind::SYSEX;
			f.x = size - 1 - (size > 1 && m.getSysExByte(size - 1) == 0xf7 ? 1 : 0);
			break;
		}
		case T::SONG_POINTER:     f.kind = Kind::SONG_POINTER; f.x = m.getValue(); break;
		case T::SONG_SELECT:      f.kind = Kind::SONG_SELECT; f.x = m.getNote(); break;
		case T::CLOCK:            f.kind = Kind::CLOCK; break;
		case T::START:            f.kind = Kind::START; break;
		case T::CONTINUE:         f.kind = Kind::CONTINUE; break;
		case T::STOP:             f.kind = Kind::STOP; break;
		case T::RESET:            f.kind = Kind::RESET; break;
	}
	return f;
}

/** The Fields of a raw message that has not been through the decoder: also the system
 *  messages MessageEx has no type for (MTC quarter frame, tune request, active sensing),
 *  an unknown status and an empty message. A velocity-0 Note-On stays a Note-On. */
inline Fields classify(const rack::midi::Message& msg) {
	if (msg.getSize() < 1) return Fields();
	MessageEx m(msg);
	if (MessageEx::decodeBasic(msg, m)) return classify(m);
	Fields f;
	f.channel = msg.getChannel();
	switch (msg.bytes[0]) {
		case 0xf1: f.kind = Kind::MTC_QUARTER_FRAME; f.x = (msg.getNote() >> 4) & 7; f.y = msg.getNote() & 15; break;
		case 0xf6: f.kind = Kind::TUNE_REQUEST; break;
		case 0xfe: f.kind = Kind::ACTIVE_SENSING; break;
		default:   f.kind = Kind::UNKNOWN; f.x = msg.bytes[0]; break;
	}
	return f;
}

/** The bytes as lower-case hex pairs separated by a space: "43 10 4c". */
inline std::string hexBytes(const uint8_t* bytes, int count) {
	static const char* digits = "0123456789abcdef";
	std::string s;
	for (int i = 0; i < count; i++) {
		if (i > 0) s += ' ';
		s += digits[bytes[i] >> 4];
		s += digits[bytes[i] & 15];
	}
	return s;
}

/** The text of a message. Allocates. */
inline std::string format(const Fields& f) {
	const int ch = f.channel + 1;
	switch (f.kind) {
		case Kind::NOTE_ON:          return string::f("ch%02d note on  %i vel %i", ch, f.x, f.y);
		case Kind::NOTE_OFF:         return string::f("ch%02d note off %i vel %i", ch, f.x, f.y);
		case Kind::KEY_PRESSURE:     return string::f("ch%02d key-pressure %i vel %i", ch, f.x, f.y);
		case Kind::CC:               return string::f("ch%02d cc%i=%i", ch, f.x, f.y);
		case Kind::CC_14BIT:         return string::f("ch%02d 14-bit cc%i=%i", ch, f.x, f.y);
		case Kind::RPN_RESET:        return string::f("ch%02d rpn/nrpn reset", ch);
		case Kind::RPN_VALUE:        return string::f("ch%02d rpn param=%i value=%i", ch, f.x, f.y);
		case Kind::RPN_PARAM:
			switch (f.x) {
				case 0: return string::f("ch%02d rpn param=0 (Pitch Bend Sensitivity)", ch);
				case 1: return string::f("ch%02d rpn param=1 (Fine Tuning)", ch);
				case 2: return string::f("ch%02d rpn param=2 (Coarse Tuning)", ch);
				case 3: return string::f("ch%02d rpn param=3 (Tuning Program Select)", ch);
				case 4: return string::f("ch%02d rpn param=4 (Tuning Bank Select)", ch);
				default: return string::f("ch%02d rpn param=%i selected", ch, f.x);
			}
		case Kind::NRPN_VALUE:       return string::f("ch%02d nrpn param=%i value=%i", ch, f.x, f.y);
		case Kind::NRPN_PARAM:       return string::f("ch%02d nrpn param=%i selected", ch, f.x);
		case Kind::PROGRAM_CHANGE:   return string::f("ch%02d program=%i", ch, f.x);
		case Kind::CHANNEL_PRESSURE: return string::f("ch%02d channel-pressure=%i", ch, f.x);
		case Kind::PITCH_BEND:       return string::f("ch%02d pitchbend=%i", ch, f.x);
		case Kind::SYSEX:            return string::f("sysex (%i data bytes)", f.x);
		case Kind::SONG_POINTER:     return string::f("song pointer=%i", f.x);
		case Kind::SONG_SELECT:      return string::f("song select=%i", f.x);
		case Kind::CLOCK:            return "clock tick";
		case Kind::START:            return "start";
		case Kind::CONTINUE:         return "continue";
		case Kind::STOP:             return "stop";
		case Kind::RESET:            return "reset";
		case Kind::MTC_QUARTER_FRAME: return string::f("mtc quarter frame piece=%i value=%i", f.x, f.y);
		case Kind::TUNE_REQUEST:     return "tune request";
		case Kind::ACTIVE_SENSING:   return "active sensing";
		case Kind::UNKNOWN:          return string::f("unknown %02x", f.x);
		case Kind::EMPTY:            return "(empty)";
	}
	return "";
}

} // namespace MidiText
} // namespace StoermelderPackOne
