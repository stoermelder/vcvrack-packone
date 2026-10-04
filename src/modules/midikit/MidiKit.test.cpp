#include "MidiKit.test.hpp"
#include <fstream>
#include <sstream>

using namespace StoermelderPackOne::MidiScript;

// Catch2 macros reference Catch::X unqualified, so inside a per-header
// namespace they would resolve to <ns>::Catch. Alias the real namespace so
// every TEST_CASE/REQUIRE/GENERATE in these headers keeps working.

namespace __module {
	namespace Catch = ::Catch;
	#include "MidiKit.test.module.hpp"
}
namespace __tipsy {
	namespace Catch = ::Catch;
	#include "MidiKit.test.tipsy.hpp"
}
namespace __api_messages {
	namespace Catch = ::Catch;
	#include "MidiKit.test.api.messages.hpp"
}
namespace __api_io {
	namespace Catch = ::Catch;
	#include "MidiKit.test.api.io.hpp"
}
namespace __api_menu {
	namespace Catch = ::Catch;
	#include "MidiKit.test.api.menu.hpp"
}
namespace __config {
	namespace Catch = ::Catch;
	#include "MidiKit.test.config.hpp"
}
namespace __lifecycle {
	namespace Catch = ::Catch;
	#include "MidiKit.test.lifecycle.hpp"
}
namespace __robustness {
	namespace Catch = ::Catch;
	#include "MidiKit.test.robustness.hpp"
}
namespace __engines {
	namespace Catch = ::Catch;
	#include "MidiKit.test.engines.hpp"
}
namespace __output {
	namespace Catch = ::Catch;
	#include "MidiKit.test.output.hpp"
}
namespace __input {
	namespace Catch = ::Catch;
	#include "MidiKit.test.input.hpp"
}
namespace __timing {
	namespace Catch = ::Catch;
	#include "MidiKit.test.timing.hpp"
}
namespace __cancel {
	namespace Catch = ::Catch;
	#include "MidiKit.test.cancel.hpp"
}
namespace __log {
	namespace Catch = ::Catch;
	#include "MidiKit.test.log.hpp"
}
namespace __widget {
	namespace Catch = ::Catch;
	#include "MidiKit.test.widget.hpp"
}
namespace __variant {
	namespace Catch = ::Catch;
	#include "MidiKit.test.variant.hpp"
}
namespace __presets {
	namespace Catch = ::Catch;
	#include "MidiKit.test.presets.hpp"
}
namespace __broadcast {
	namespace Catch = ::Catch;
	#include "MidiKit.test.broadcast.hpp"
}
namespace __editor {
	namespace Catch = ::Catch;
	#include "MidiKit.test.editor.hpp"
}
namespace __harness {
	namespace Catch = ::Catch;
	#include "MidiKit.test.harness.hpp"
}

void testPluginInit(rack::Plugin* p) {
	pluginInstance = p;
	p->addModel(modelMidiKit);
	p->addModel(modelMidiKitMicro);
}