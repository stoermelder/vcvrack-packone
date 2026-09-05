#include "../../test/framework.hpp"
#include "Ahab.test.hpp"

using namespace StoermelderPackOne;
using namespace StoermelderPackOne::Ahab;

SYNC_MODEL(modelAhab, "Ahab");
static Test::TestContext<> testContext;


// Test cases are split by target into the headers included below.
// - AhabModule.test.hpp: AhabModule core (clock, run/stop, reset, CV I/O, preset)
// - Ahab.midi.test.hpp:  MIDI output (midiOutPort) + virtual MIDI driver (Ahab::Midi)
// - Ahab.json.test.hpp:  JSON serialization/deserialization (module, sim, AhabOoscOutput)
// - Ahab.state.test.hpp: headless cursor/selection clamping math (no widget)
#include "Ahab.test.module.hpp"
#include "Ahab.test.midi.hpp"
#include "Ahab.test.json.hpp"
#include "Ahab.test.state.hpp"

// Test cases are split by target into the headers included below.
// - AhabSimOperators.test.hpp: vcvin/vcvout operators + E bang propagation + UDP/OSC output (incl. destination config)
// - AhabSimField.test.hpp:     ORCA parsing, field sizing, fill/cut/move/paste/replace
// - AhabSimState.test.hpp:     undo/redo, reset, step, seed
// - AhabSimCallbacks.test.hpp: UI/DSP callbacks, event clearing, display buffer
#include "Ahab.test.SimOperators.hpp"
#include "Ahab.test.SimField.hpp"
#include "Ahab.test.SimState.hpp"
#include "Ahab.test.SimCallbacks.hpp"

// Headless pure-data tests (no sim, no widget), included last:
// - Ahab.test.generator.hpp: AhabPatternBuffer + randomizer generation
#include "Ahab.test.generator.hpp"
