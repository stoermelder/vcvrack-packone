#include "MidiKit.test.hpp"
#include <fstream>
#include <sstream>

using namespace StoermelderPackOne::MidiScript;

// Catch2 macros reference Catch::X unqualified, so inside a per-header
// namespace they would resolve to <ns>::Catch. Alias the real namespace so
// every TEST_CASE/REQUIRE/GENERATE in these headers keeps working.

namespace __tipsy {
	namespace Catch = ::Catch;
	#include "MidiKit.module.test.hpp"
}
namespace __engine {
	namespace Catch = ::Catch;
	#include "MidiKit.engine.test.hpp"
}
namespace __minilua {
	namespace Catch = ::Catch;
	#include "MidiKit.minilua.test.hpp"
}
namespace __quickjs {
	namespace Catch = ::Catch;
	#include "MidiKit.quickjs.test.hpp"
}
namespace __cc {
	namespace Catch = ::Catch;
	#include "MidiKit.cc.test.hpp"
}
namespace __examples {
	namespace Catch = ::Catch;
	#include "MidiKit.examples.test.hpp"
}
namespace __tipsy {
	namespace Catch = ::Catch;
	#include "MidiKit.tipsy.test.hpp"
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