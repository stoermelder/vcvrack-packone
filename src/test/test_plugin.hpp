#define CATCH_CONFIG_PREFIX_MESSAGES
#define CATCH_CONFIG_THREAD_SAFE_ASSERTIONS
#define CATCH_CONFIG_FAST_COMPILE
#include "catch_amalgamated.hpp"


// Avoid redefinition issues: replace Catch2's function-like DEPRECATED with an object-like one
#ifdef DEPRECATED
#  undef DEPRECATED
#endif
#ifndef DEPRECATED
#  if defined(__GNUC__) || defined(__clang__)
#    define DEPRECATED __attribute__((deprecated))
#  elif defined(_MSC_VER)
#    define DEPRECATED __declspec(deprecated)
#  else
#    define DEPRECATED
#  endif
#endif

#include "../plugin.hpp"


// The tests mock the vcv access layer (src/vcv/*.hpp) through its `*Access` pointers, which
// exist only in a DEBUGPLUGIN build — a release build resolves each access statically and
// has no seam at all. This #error covers the test TU; the sentinel below covers the dylib it
// links against.
#ifndef DEBUGPLUGIN
#error "The test suite requires DEBUGPLUGIN: Build with `make DEBUGPLUGIN=1 test` (or testrun / test-one NAME=<Module>)."
#endif

// Referencing the sentinel (defined in vcv/build.cpp) makes a release plugin.dylib fail the
// link on one named symbol, instead of a pile of undefined `vcv::*Access` — or, for a test
// that touches no mock, linking fine and silently running against the real Rack API.
//
// Declared here rather than in a header: any header opening namespace
// StoermelderPackOne::vcv makes the unqualified `history::Action` in utils/vcv_files.hpp
// resolve to vcv::history instead of rack::history. Must match the definition in build.cpp.
//
// Guarded: this header has no include guard of its own (its Catch2 macros depend on
// inclusion order) and is pulled in by both the .test.cpp and its .test.hpp fragments.
#ifndef _TEST_DEBUGPLUGIN_SENTINEL
#define _TEST_DEBUGPLUGIN_SENTINEL
namespace StoermelderPackOne {
	namespace vcv {
		void assertDebugPluginBuild();
	} 
}
namespace {
	// Must be a *call* from a static initializer: an unused pointer holding the address gets
	// discarded before the linker sees a relocation, leaving the sentinel inert.
	struct TestDebugPluginSentinel {
		TestDebugPluginSentinel() { StoermelderPackOne::vcv::assertDebugPluginBuild(); }
	};
	TestDebugPluginSentinel p1TestDebugPluginSentinel;
}
#endif


// We will call several deprecated functions in the tests, thus disable warnings here
#if defined(__clang__)
	#pragma clang diagnostic push
	#pragma clang diagnostic ignored "-Wdeprecated-declarations"
#elif defined(__GNUC__) || defined(__GNUG__)
	#pragma GCC diagnostic push
	#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#endif