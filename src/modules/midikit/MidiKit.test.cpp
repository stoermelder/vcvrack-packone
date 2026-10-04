#include "MidiKit.test.hpp"
#include <fstream>
#include <sstream>

using namespace StoermelderPackOne::MidiScript;

// Catch2 macros reference Catch::X unqualified, so inside a per-header
// namespace they would resolve to <ns>::Catch. Alias the real namespace so
// every TEST_CASE/REQUIRE/GENERATE in these headers keeps working.

namespace __tipsy {
	namespace Catch = ::Catch;
	#include "MidiKit.test.module.hpp"
}
namespace __engine {
	namespace Catch = ::Catch;
	#include "MidiKit.test.engine.hpp"
	#include "MidiKit.test.engine.messages.hpp"
	#include "MidiKit.test.engine.extended.hpp"
	#include "MidiKit.test.engine.io.hpp"
	#include "MidiKit.test.engine.lifecycle.hpp"
	#include "MidiKit.test.engine.config.hpp"
	#include "MidiKit.test.engine.callbacks.hpp"
	#include "MidiKit.test.engine.robustness.hpp"
}
namespace __minilua {
	namespace Catch = ::Catch;
	#include "MidiKit.test.minilua.hpp"
}
namespace __quickjs {
	namespace Catch = ::Catch;
	#include "MidiKit.test.quickjs.hpp"
}
namespace __cc {
	namespace Catch = ::Catch;
	#include "MidiKit.test.cc.hpp"
}
namespace __examples {
	namespace Catch = ::Catch;
	#include "MidiKit.test.examples.hpp"
}
namespace __tipsy {
	namespace Catch = ::Catch;
	#include "MidiKit.test.tipsy.hpp"
}
namespace __ports {
	namespace Catch = ::Catch;
	#include "MidiKit.test.ports.hpp"
}
namespace __timing {
	namespace Catch = ::Catch;
	#include "MidiKit.test.timing.hpp"
}
namespace __cancel {
	namespace Catch = ::Catch;
	#include "MidiKit.test.cancel.hpp"
}
namespace __messagesbus {
	namespace Catch = ::Catch;
	#include "MidiKit.test.broadcast.bus.hpp"
}
namespace __swap {
	namespace Catch = ::Catch;
	#include "MidiKit.test.swap.hpp"
}
namespace __log {
	namespace Catch = ::Catch;
	#include "MidiKit.test.log.hpp"
}
namespace __editor {
	namespace Catch = ::Catch;
	#include "MidiKit.test.editor.hpp"
}
namespace __harness {
	namespace Catch = ::Catch;
	#include "MidiKit.test.harness.hpp"
}

// The examples header's OutEvent StringMaker specialization has to live at
// global scope — the namespace alias above makes `namespace Catch { ... }`
// inside the per-header namespace illegal, and it must be in the real ::Catch
// anyway.
namespace Catch {
	template<> struct StringMaker<__examples::OutEvent> {
		static std::string convert(__examples::OutEvent const& e) {
			std::ostringstream os;
			os << "0x" << std::hex << (int)e.status << std::dec
			   << " ch=" << (int)e.channel
			   << " n=" << (int)e.note
			   << " v=" << (int)e.value
			   << " t=" << e.ticks;
			return os.str();
		}
	};
}

void testPluginInit(rack::Plugin* p) {
	pluginInstance = p;
	p->addModel(modelMidiKit);
	p->addModel(modelMidiKitMicro);
}