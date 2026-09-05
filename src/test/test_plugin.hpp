#define CATCH_CONFIG_PREFIX_MESSAGES
#define CATCH_CONFIG_THREAD_SAFE_ASSERTIONS
#define CATCH_CONFIG_FAST_COMPILE
#include "catch_amalgamated.hpp"


// Avoid redefinition issues: replace Catch2's function-like DEPRECATED with an object-like one.
//
// Catch2 v3.12.0 (catch_amalgamated.hpp) defines `DEPRECATED(msg)` as a function-like macro
// expanding to [[deprecated(msg)]]. Rack's common.hpp separately defines an object-like
// `DEPRECATED` expanding to __attribute__((deprecated)) (no message). Both are unconditional
// #defines with no #ifndef guard on either side, so whichever header is included second wins
// outright — there is no warning, just silently wrong behaviour for the other's callers. This
// block re-asserts Rack's object-like form after Catch2's has taken effect. Re-check this
// comment (and whether it's still needed) on the next Catch2 upgrade — a future version may
// namespace or guard its macro differently.
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


// The test harness deliberately reaches into a handful of Rack APIs marked PRIVATE/DEPRECATED
// (rack::engine::Engine's constructor, Scene's constructor, addModule_NoLock/removeModule_NoLock
// — see test_context.hpp) because there is no public alternative for driving a module outside
// the real engine. Wrap just those call sites in TEST_SUPPRESS_DEPRECATED_BEGIN/END rather than
// suppressing -Wdeprecated-declarations for the rest of the translation unit: the module source
// under test is #include'd into the same TU after this header, and a blanket, never-popped
// `#pragma ... push` here would silently hide legitimate deprecation warnings in that module code
// too — the exact failure mode this scoping avoids.
#if defined(__clang__)
	#define TEST_SUPPRESS_DEPRECATED_BEGIN \
		_Pragma("clang diagnostic push") \
		_Pragma("clang diagnostic ignored \"-Wdeprecated-declarations\"")
	#define TEST_SUPPRESS_DEPRECATED_END _Pragma("clang diagnostic pop")
#elif defined(__GNUC__) || defined(__GNUG__)
	#define TEST_SUPPRESS_DEPRECATED_BEGIN \
		_Pragma("GCC diagnostic push") \
		_Pragma("GCC diagnostic ignored \"-Wdeprecated-declarations\"")
	#define TEST_SUPPRESS_DEPRECATED_END _Pragma("GCC diagnostic pop")
#else
	#define TEST_SUPPRESS_DEPRECATED_BEGIN
	#define TEST_SUPPRESS_DEPRECATED_END
#endif