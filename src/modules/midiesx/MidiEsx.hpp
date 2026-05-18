#pragma once
#include <rack.hpp>

namespace StoermelderPackOne {
namespace MidiEsx {

struct MidiEsxMessageHandler {
    virtual void onMessage(int portIndex, const rack::midi::Message& message) = 0;
};

void init();

void subscribe(int portGroup, MidiEsxMessageHandler* handler);

void unsubscribe(int portGroup, MidiEsxMessageHandler* handler);

} // namespace MidiEsx
} // namespace StoermelderPackOne