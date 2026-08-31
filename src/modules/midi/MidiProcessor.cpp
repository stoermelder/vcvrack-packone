#include "MidiProcessor.hpp"
#include <algorithm>

namespace StoermelderPackOne {

MessageEx::MessageEx(const rack::midi::Message& msg) {
	this->msg = msg;
	this->frame = msg.frame;
}

uint8_t MessageEx::getChannel() const {
	return msg.getChannel();
}
uint8_t MessageEx::getNote() const {
	return msg.getNote();
}

int16_t MessageEx::getValue() const {
	switch (type) {
		case Type::PITCH_BEND:
		case Type::CC_14BIT:
		case Type::RPN:
		case Type::NRPN:
		case Type::SONG_POINTER:
			return extraValue;
		default:
			return msg.getValue();
	}
}
int16_t MessageEx::getParamNumber() const {
	return paramNumber;
}

bool MessageEx::hasValue() const {
	return extraValue >= 0;
}

int MessageEx::getSysExSize() const {
	return type == Type::SYSEX ? msg.getSize() : 0;
}

unsigned char MessageEx::getSysExByte(int i) const {
	return type == Type::SYSEX ? msg.bytes[i] : 0;
}

std::vector<unsigned char> MessageEx::getSysExBytes() const {
	return type == Type::SYSEX ? msg.bytes : std::vector<unsigned char>();
}

// ownedInput is initialized first (members initialize in declaration order), so
// its get() below is valid. Allocated only when nothing is injected.
MidiProcessor::MidiProcessor(rack::midi::InputQueue* injected)
	: ownedInput(injected ? nullptr : new rack::midi::InputQueue())
	, input(injected ? injected : ownedInput.get()) {
	reset();
}

void MidiProcessor::reset() {
	for (int i = 0; i < 16; ++i) {
		ccNrpnParam[i] = -1;
		ccRpnParam[i] = -1;
		ccDataEntryMsb[i] = -1;
		pendingRpnMsb[i] = -1;
		pendingNrpnMsb[i] = -1;
		for (int j = 0; j < 32; ++j) cc14bitMsb[i][j] = -1;
	}
}

rack::midi::InputQueue& MidiProcessor::getInput() {
	return *input;
}


void MidiProcessor::processBypass(int64_t frame) {
	// Reuse the member scratch message so the audio thread never heap-allocates
	// a `midi::Message` per pump.
	rack::midi::Message& msg = scratchMidiMessage;
	while (input->tryPop(&msg, frame)) {
		(void)0;
	}
}

void MidiProcessor::process(int64_t frame) {
	rack::midi::Message& msg = scratchMidiMessage;
	while (input->tryPop(&msg, frame)) {
		processMessage(msg);
	}
}

void MidiProcessor::processMessage(const rack::midi::Message& msg) {
	uint8_t status = msg.getStatus();
	MessageEx m = MessageEx(msg);
	switch (status) {
		case 0x9:   // note on
			m.type = MessageEx::Type::NOTE_ON;
			notify(m);
			break;
		case 0x8:   // note off
			m.type = MessageEx::Type::NOTE_OFF;
			notify(m);
			break;
		case 0xa:   // key pressure
			m.type = MessageEx::Type::KEY_PRESSURE;
			notify(m);
			break;
		case 0xb:   // cc
			m.type = MessageEx::Type::CC;
			// Must be queried before processCc() below, which mutates the state
			// it reads: for CC 6/38 the question is whether a parameter was
			// active when this message arrived, not after it landed. Do not
			// reorder these -- the raw CC deliberately notifies before its
			// assembled counterpart.
			m.isComponent = isComponentCc(msg);
			notify(m);
			processCc(msg); // extended CC handling
			break;
		case 0xc:   // program change
			m.type = MessageEx::Type::PROGRAM_CHANGE;
			notify(m);
			break;
		case 0xd:   // channel pressure
			m.type = MessageEx::Type::CHANNEL_PRESSURE;
			notify(m);
			break;
		case 0xe:   // pitch wheel
			m.type = MessageEx::Type::PITCH_BEND;
			m.extraValue = ((uint16_t)msg.getValue() << 7) | msg.getNote();
			notify(m);
			break;
		case 0xf: { // system
			uint8_t sys = msg.getChannel();
			switch (sys) {
				case 0x0: // sysex
					m.type = MessageEx::Type::SYSEX;
					notify(m);
					break;
				case 0x2: // song pointer
					m.type = MessageEx::Type::SONG_POINTER;
					m.extraValue = ((uint16_t)msg.getValue() << 7) | msg.getNote();
					notify(m);
					break;
				case 0x3: // song select
					m.type = MessageEx::Type::SONG_SELECT;
					notify(m);
					break;
				case 0x8: // timing clock
					m.type = MessageEx::Type::CLOCK;
					notify(m);
					break;
				case 0xa: // start
					m.type = MessageEx::Type::START;
					notify(m);
					break;
				case 0xb: // continue
					m.type = MessageEx::Type::CONTINUE;
					notify(m); 
					break;
				case 0xc: // stop
					m.type = MessageEx::Type::STOP;
					notify(m);
					break;
				case 0xf: // reset
					m.type = MessageEx::Type::RESET;
					notify(m);
					break;
				default:
					break;
			}
			break;
		}
		default:
			break;
	}
}

bool MidiProcessor::isComponentCc(const rack::midi::Message& msg) const {
	uint8_t ch = msg.getChannel();
	uint8_t cc = msg.getNote();

	// Parameter select is always a component, whether or not it completes a pair.
	if (cc == 98 || cc == 99 || cc == 100 || cc == 101) return true;
	// Data entry only counts while a parameter is actually armed; otherwise
	// CC 6/38 are ordinary controllers.
	if ((cc == 6 || cc == 38) && (ccNrpnParam[ch] >= 0 || ccRpnParam[ch] >= 0)) return true;
	// 14-bit halves count only once the pair is being tracked -- see the note on
	// MessageEx::isComponent about the first MSB.
	if (cc < 32) return cc14bitMsb[ch][cc] >= 0;
	if (cc < 64) return cc14bitMsb[ch][cc - 32] >= 0;
	return false;
}

void MidiProcessor::processCc(const rack::midi::Message& msg) {
	uint8_t ch = msg.getChannel();
	uint8_t cc = msg.getNote();
	uint8_t value = msg.bytes[2];

	// RPN selection: CC 101 (MSB) sets pending MSB, CC 100 (LSB) completes selection
	if (cc == 101) {
		pendingRpnMsb[ch] = value;
		// don't return; record and continue
	} 
	else if (cc == 100) {
		if (pendingRpnMsb[ch] >= 0) {
			int16_t rpnMsb = pendingRpnMsb[ch];
			int16_t rpnLsb = value;
			int16_t rpnNumber = int16_t(rpnMsb) * 128 + rpnLsb;

			MessageEx m = MessageEx(msg);
			// RPN reset: 127/127 resets both RPN and NRPN
			if (rpnMsb == 127 && rpnLsb == 127) {
				// reset
				ccNrpnParam[ch] = ccRpnParam[ch] = -1;
				pendingRpnMsb[ch] = -1;
				pendingNrpnMsb[ch] = -1;
				ccDataEntryMsb[ch] = -1;

				// notify reset as RPN with param -1
				m.type = MessageEx::Type::RPN;
				m.paramNumber = -1;
				notify(m);
			} 
			else {
				ccRpnParam[ch] = rpnNumber;
				ccNrpnParam[ch] = -1;
				pendingRpnMsb[ch] = -1;
				ccDataEntryMsb[ch] = -1;

				m.type = MessageEx::Type::RPN;
				m.paramNumber = rpnNumber;
				notify(m);
			}
		}
	}

	// NRPN selection: CC 99 (MSB) sets pending MSB, CC 98 (LSB) completes selection
	else if (cc == 99) {
		pendingNrpnMsb[ch] = value;
	} 
	else if (cc == 98) {
		if (pendingNrpnMsb[ch] >= 0) {
			int16_t nrpnMsb = pendingNrpnMsb[ch];
			int16_t nrpnLsb = value;
			int16_t number = int16_t(nrpnMsb) * 128 + nrpnLsb;

			ccNrpnParam[ch] = number;
			ccRpnParam[ch] = -1;
			pendingNrpnMsb[ch] = -1;
			ccDataEntryMsb[ch] = -1;

			MessageEx m = MessageEx(msg);
			m.type = MessageEx::Type::NRPN;
			m.paramNumber = number;
			notify(m);
		}
	}

	// RPN/NRPN data entry
	if (cc == 6 && (ccNrpnParam[ch] >= 0 || ccRpnParam[ch] >= 0)) {
		// Store MSB for potential LSBs (CC 38) that may follow; do not clear immediately
		ccDataEntryMsb[ch] = value;
	} 
	else if (cc == 38 && (ccNrpnParam[ch] >= 0 || ccRpnParam[ch] >= 0)) {
		int16_t finalValue;
		if (ccDataEntryMsb[ch] >= 0) {
			finalValue = int16_t(ccDataEntryMsb[ch]) * 128 + value;
		}
		else {
			finalValue = value; // LSB-only
		}

		MessageEx m = MessageEx(msg);
		if (ccRpnParam[ch] >= 0) {
			m.type = MessageEx::Type::RPN;
			m.paramNumber = ccRpnParam[ch];
			m.extraValue = finalValue;
			notify(m);
		}
		if (ccNrpnParam[ch] >= 0) {
			m.type = MessageEx::Type::NRPN;
			m.paramNumber = ccNrpnParam[ch];
			m.extraValue = finalValue;
			notify(m);
		}
	}

	// 14-bit CC (CC 0-31 for MSB, CC 32-63 for LSB)
	if (cc < 32) {
		// CC 0-31: Store as MSB for potential 14-bit CC
		// This is not according to standard, but to avoid spurious 14-bit CC messages
		// after a MIDI reset, we ignore MSBs with value = 0.
		if (value > 0 || cc14bitMsb[ch][cc] != -1) cc14bitMsb[ch][cc] = value;
	} 
	else if (32 <= cc && cc < 64) {
		// CC 32-63: LSB for 14-bit CC
		uint8_t msbCc = cc - 32;
		if (cc14bitMsb[ch][msbCc] >= 0) {
			int16_t value14bit = int16_t(cc14bitMsb[ch][msbCc]) * 128 + value;
			MessageEx m = MessageEx(msg);
			m.type = MessageEx::Type::CC_14BIT;
			m.paramNumber = msbCc;
			m.extraValue = value14bit;
			notify(m);
		}
	}
}

void MidiProcessor::notify(const MessageEx& m) {
	for (auto& handler : handlers) {
		bool b = handler->processMidi(m);
		if (b) break;
	}
}

void MidiProcessor::subscribe(MidiProcessorHandler* handler) {
	handlers.push_back(handler);
}

void MidiProcessor::unsubscribe(MidiProcessorHandler* handler) {
	auto it = std::find(handlers.begin(), handlers.end(), handler);
	if (it != handlers.end()) {
		handlers.erase(it);
	}
}

} // namespace StoermelderPackOne