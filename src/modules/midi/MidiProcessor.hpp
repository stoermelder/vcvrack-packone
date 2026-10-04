#pragma once
#include <rack.hpp>
#include <vector>
#include <functional>
#include <algorithm>
#include <memory>
#include <cassert>
#include "MidiCInputQueue.hpp"

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

    // The leading bytes of the message, copied. A rack::midi::Message owns a
    // heap vector that its default constructor allocates, so holding one here
    // cost an allocation per decoded event on the audio thread.
    uint8_t bytes[3] = {0, 0, 0};
    int size = 0;
    // The full message, for SysEx bytes only. Not owned: valid for as long as
    // the Message this was constructed from, which is the duration of the notify.
    const rack::midi::Message* source = nullptr;
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

// The decoder: turns a stream of raw messages into semantic events (NRPN/RPN/
// 14-bit CC assembly) for its handlers. Knows nothing about where messages come from.
struct MidiDecoder {
    // Public members so other modules/tests can inspect state when necessary
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

    // Configuration, not decode state: reset() leaves these alone, as it drops
    // stream state after a discontinuity and not the owner's settings. Bit c is
    // MIDI channel c. Set, per kind, for channels whose device sends 7-bit data
    // entry (CC 6 without CC 38): CC 6 then fires an event of its own, with the
    // coarse value (msb << 7). Other modules never set them.
    uint16_t msbDataEntryNrpnMask = 0;
    uint16_t msbDataEntryRpnMask = 0;

    struct DecodeOnly {};

    MidiDecoder() {
        reset();
    }

    void reset();

    // Decodes one message and notifies handlers. MidiProcessorT::process() is
    // just the pump that feeds this; call it directly to decode messages
    // obtained some other way, without this processor touching a queue at all.
    void processMessage(const rack::midi::Message& msg);

    void processCc(const rack::midi::Message& msg);

    // Replaces both MSB data entry masks. Same thread as processMessage().
    void setMsbDataEntry(uint16_t nrpnMask, uint16_t rpnMask) {
        msbDataEntryNrpnMask = nrpnMask;
        msbDataEntryRpnMask = rpnMask;
    }

    // Whether CC 6 on `ch` fires an event: the mask of the kind armed on that
    // channel (RPN if one is, else NRPN).
    bool isMsbDataEntry(uint8_t ch) const;

    // Whether `msg` (a CC) currently participates in an extended message; see
    // MessageEx::isComponent. Pure query -- it must be called BEFORE processCc()
    // updates the state, so it reports the state as of the message's arrival.
    bool isComponentCc(const rack::midi::Message& msg) const;

    // Notifies the armed RPN and/or NRPN of `ch` with the data entry `value`.
    void notifyDataEntry(const rack::midi::Message& msg, uint8_t ch, int16_t value);

    void notify(const MessageEx& m);
    void subscribe(MidiProcessorHandler* handler);
    void unsubscribe(MidiProcessorHandler* handler);
};


struct MidiProcessor : MidiDecoder {
    // Queue owned by this processor, allocated only when none is injected --
    // so an injecting consumer carries no unused queue. Null when injecting.
    std::unique_ptr<rack::midi::InputQueue> ownedInput;
    // The queue actually pumped: ownedInput, or the injected one. Null for a
    // decode-only processor.
    rack::midi::InputQueue* input;

    // Reusable scratch MIDI message for the audio thread. `midi::Message`
    // heap-allocates its internal byte vector on construction, so creating one
    // per call inside `process()`/`processBypass()` would be a per-pump
    // malloc/free.
    rack::midi::Message scratchMidiMessage;

    // Default: own the input queue, pumping it in process()/processBypass().
    //
    // Injected: the CALLER owns the queue and must keep it alive for at least as
    // long as this processor. Where both are members of the same module, declare
    // the queue first so destruction order guarantees it. Lets a consumer that
    // already owns a MIDI port (with its own widget binding and JSON) reuse the
    // decoding without transplanting ownership.
    explicit MidiProcessor(rack::midi::InputQueue* injected = nullptr)
        : ownedInput(injected ? nullptr : new rack::midi::InputQueue())
        , input(injected ? injected : ownedInput.get()) {}

    // No queue at all: only processMessage() and the state calls may be used.
    // process(), processBypass() and getInput() assert that there is a queue.
    explicit MidiProcessor(DecodeOnly) : input(nullptr) {}

    rack::midi::InputQueue& getInput() {
        assert(input);
        return *input;
    }

    void processBypass(int64_t frame) {
        assert(input);
        rack::midi::Message& msg = scratchMidiMessage;
        while (input->tryPop(&msg, frame)) {}
    }

    void process(int64_t frame) {
        assert(input);
        rack::midi::Message& msg = scratchMidiMessage;
        while (input->tryPop(&msg, frame)) {
            processMessage(msg);
        }
    }
};

// The decoder over a MidiCInputQueue: the audio thread takes no lock and
// allocates nothing, and decodes straight out of the queue's slots instead of
// copying each message.
struct MidiCProcessor : MidiDecoder {
    // Queue owned by this processor, allocated only when none is injected.
    std::unique_ptr<MidiCInputQueue<>> ownedInput;
    // The queue actually pumped: ownedInput, or the injected one. Null for a
    // decode-only processor.
    MidiCInputQueue<>* input;

    // Injected: the CALLER owns the queue and must keep it alive for at least as
    // long as this processor.
    explicit MidiCProcessor(MidiCInputQueue<>* injected = nullptr)
        : ownedInput(injected ? nullptr : new MidiCInputQueue<>())
        , input(injected ? injected : ownedInput.get()) {}

    // No queue at all: only processMessage() and the state calls may be used.
    // process(), processBypass() and getInput() assert that there is a queue.
    explicit MidiCProcessor(DecodeOnly) : input(nullptr) {}

    MidiCInputQueue<>& getInput() {
        assert(input);
        return *input;
    }

    // Empties the queue of what is due at `frame`, without decoding.
    void processBypass(int64_t frame) {
        assert(input);
        while (input->peek(frame)) input->pop();
    }

    // Decodes what is due at `frame`, in frame order. Handlers copy what they
    // keep before the message is popped.
    void process(int64_t frame) {
        assert(input);
        while (const rack::midi::Message* msg = input->peek(frame)) {
            processMessage(*msg);
            input->pop();
        }
    }
};

} // namespace StoermelderPackOne