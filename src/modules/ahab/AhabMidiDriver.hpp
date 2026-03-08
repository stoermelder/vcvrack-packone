#pragma once
#include <midi.hpp>

namespace StoermelderPackOne {
namespace Ahab {
namespace Midi {
    
// Initialize the virtual AHAB MIDI driver (registers the driver with Rack)
void init();

// Whether the driver has been successfully registered
bool isLoaded();

// Number of virtual input ports provided by the driver
int numPorts();

// Reset virtual ports by sending All Notes Off / All Sound Off / Reset Controllers
// to all channels so connected inputs are put into a known state.
void reset(int deviceId);

// Inject a MIDI message into the virtual port with id `deviceId` (0-based)
void sendToPort(int deviceId, const rack::midi::Message& m);

} // namespace Midi
} // namespace Ahab
} // namespace StoermelderPackOne
