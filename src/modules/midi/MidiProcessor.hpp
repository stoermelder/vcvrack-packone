#pragma once
#include <rack.hpp>
#include <vector>
#include <functional>
#include <algorithm>
#include <memory>

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

    // True when this CC is a component of an extended message: an NRPN/RPN
    // parameter select (CC 98/99/100/101), data entry for a currently active
    // parameter (CC 6/38), or either half of an already-tracked 14-bit pair.
    // Consumers that act on the assembled CC_14BIT/RPN/NRPN event should skip
    // these, or they see the same information twice. Only meaningful on Type::CC.
    //
    // Note the first MSB of a 14-bit pair reads false: until an MSB has been
    // stored the decoder cannot know a pair is coming, so one raw CC per pair
    // escapes at stream start.
    //
    // Also note CC 0-31 are simultaneously 14-bit MSBs and, for CC 6, Data
    // Entry MSB -- the spec's ranges overlap and processCc() tracks both. So
    // once CC 6 has been seen, CC 38 reads true as a tracked 14-bit LSB even
    // with no RPN/NRPN parameter armed.
    bool isComponent = false;

    MessageEx(const rack::midi::Message& msg);

    uint8_t getChannel() const;
    uint8_t getNote() const;
    int16_t getValue() const;
    int16_t getParamNumber() const;

    // RPN/NRPN are notified twice: once when the parameter is selected (number
    // only) and again when data entry supplies a value. False distinguishes the
    // former, whose getValue() is a placeholder rather than a reading.
    bool hasValue() const;
    int getSysExSize() const;
    unsigned char getSysExByte(int i) const;
    std::vector<unsigned char> getSysExBytes() const;
};

struct MidiProcessorHandler {
    virtual bool processMidi(const MessageEx& msg) { return false; }
};

struct MidiProcessor {
    // Public members so other modules/tests can inspect state when necessary

    // Queue owned by this processor, allocated only when none is injected --
    // so an injecting consumer carries no unused queue. Null when injecting.
    std::unique_ptr<rack::midi::InputQueue> ownedInput;
    // The queue actually pumped: ownedInput, or the injected one. Never null.
    rack::midi::InputQueue* input;

    std::vector<MidiProcessorHandler*> handlers;
    // All carry -1 as "not set". int16_t rather than int8_t so the sentinel and
    // the 0-127 data range never share a signed narrow type, and so the 14-bit
    // combinations below don't mix widths.
    int16_t ccNrpnParam[16];
    int16_t ccRpnParam[16];
    int16_t cc14bitMsb[16][32];
    int16_t ccDataEntryMsb[16];
    int16_t pendingRpnMsb[16];
    int16_t pendingNrpnMsb[16];

    // Default: own the input queue, pumping it in process()/processBypass().
    //
    // Injected: the CALLER owns the queue and must keep it alive for at least as
    // long as this processor. Where both are members of the same module, declare
    // the queue first so destruction order guarantees it. Lets a consumer that
    // already owns a MIDI port (with its own widget binding and JSON) reuse the
    // decoding without transplanting ownership.
    explicit MidiProcessor(rack::midi::InputQueue* injected = nullptr);

    rack::midi::InputQueue& getInput();

    void reset();

    void processBypass(int64_t frame);
    void process(int64_t frame);

    // Decodes one message and notifies handlers. process() is just the pump that
    // feeds this; call it directly to decode messages obtained some other way,
    // without this processor touching a queue at all.
    void processMessage(const rack::midi::Message& msg);

    void processCc(const rack::midi::Message& msg);

    // Whether `msg` (a CC) currently participates in an extended message; see
    // MessageEx::isComponent. Pure query -- it must be called BEFORE processCc()
    // updates the state, so it reports the state as of the message's arrival.
    bool isComponentCc(const rack::midi::Message& msg) const;

    void notify(const MessageEx& m);
    void subscribe(MidiProcessorHandler* handler);
    void unsubscribe(MidiProcessorHandler* handler);
};

} // namespace StoermelderPackOne