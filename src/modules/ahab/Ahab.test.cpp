#include "../../test/test_plugin.hpp"
#include "../../test/test_context.hpp"
#include "Ahab.test.hpp"

using namespace StoermelderPackOne;
using namespace StoermelderPackOne::Ahab;

SYNC_MODEL(modelAhab, "Ahab");
static Test::TestContext<> testContext;


// Test cases are split by target into the headers included below.
// - AhabModule.test.hpp: AhabModule core (clock, run/stop, reset, CV I/O, JSON, preset)
// - AhabMidi.test.hpp:   MIDI output (midiOutPort) + virtual MIDI driver (Ahab::Midi)
#include "Ahab.module.test.hpp"
#include "Ahab.midi.test.hpp"

// Test cases are split by target into the headers included below.
// - AhabSimOperators.test.hpp: vcvin/vcvout operators + E bang propagation
// - AhabSimField.test.hpp:     ORCA parsing, field sizing, fill/cut/move/paste/replace
// - AhabSimState.test.hpp:     undo/redo, reset, step, seed
// - AhabSimConfig.test.hpp:    UDP/OSC destinations + JSON serialization
// - AhabSimCallbacks.test.hpp: UI/DSP callbacks, event clearing, display buffer
#include "Ahab.SimOperators.test.hpp"
#include "Ahab.SimField.test.hpp"
#include "Ahab.SimState.test.hpp"
#include "Ahab.SimConfig.test.hpp"
#include "Ahab.SimCallbacks.test.hpp"