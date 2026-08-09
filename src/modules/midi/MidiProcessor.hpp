#pragma once
#include <rack.hpp>
#include <vector>
#include <functional>
#include <algorithm>

namespace StoermelderPackOne {

struct MessageEx {
    enum class Type {
        NOTE_ON,
        NOTE_OFF,
        KEY_PRESSURE,
        CC,
        CC_14BIT,
        RPN,
        NRPN,
        PROGRAM_CHANGE,
        CHANNEL_PRESSURE,
        PITCH_BEND,
        SYSEX,
        SONG_POINTER,
        SONG_SELECT,
        CLOCK,
        START,
        CONTINUE,
        STOP,
        RESET
    };

    rack::midi::Message msg;
    Type type = Type::RESET;
    int64_t frame = 0;
    int16_t paramNumber = -1;
    int16_t extraValue = -1;

    MessageEx(const rack::midi::Message& msg);

    uint8_t getChannel() const;
    uint8_t getNote() const;
    int16_t getValue() const;
    int16_t getParamNumber() const;
    int getSysExSize() const;
    unsigned char getSysExByte(int i) const;
    std::vector<unsigned char> getSysExBytes() const;
};

struct MidiProcessorHandler {
    virtual bool processMidi(const MessageEx& msg) { return false; }
};

struct MidiProcessor {
    // Public members so other modules/tests can inspect state when necessary
    rack::midi::InputQueue midiInput;
    std::vector<MidiProcessorHandler*> handlers;
    int16_t ccNrpnParam[16];
    int16_t ccRpnParam[16];
    int8_t cc14bitMsb[16][32];
    int8_t ccDataEntryMsb[16];
    int8_t pendingRpnMsb[16];
    int8_t pendingNrpnMsb[16];

    MidiProcessor();

    rack::midi::InputQueue& getInput();

    void reset();

    void processBypass(int64_t frame);
    void process(int64_t frame);
    void processCc(const rack::midi::Message& msg);

    void notify(const MessageEx& m);
    void subscribe(MidiProcessorHandler* handler);
    void unsubscribe(MidiProcessorHandler* handler);
};

} // namespace StoermelderPackOne