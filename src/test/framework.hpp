#pragma once

// Umbrella header: the pieces nearly every test file needs, in the one order that works
// (test_plugin.hpp has no include guard of its own — its Catch2 macro setup is
// order-dependent and must come first in every test TU).
//
//   test_plugin.hpp    — Catch2 config, DEPRECATED redefinition, DEBUGPLUGIN sentinel,
//                         TEST_SUPPRESS_DEPRECATED_BEGIN/END
//   test_mock.hpp      — Test::mock::Guard<Base>, NullFileAccess and the pass-through
//                         MockFileAccess; pulls in vcv/api.hpp, but declaring a Guard<Base>
//                         costs nothing in a file that never instantiates one.
//                         Before test_context.hpp: initPluginOnce() guards init() with a
//                         NullFileAccess, so TestContext depends on this header (it includes
//                         it directly too — this order is documentation, not the mechanism)
//   test_context.hpp   — TestContext, initPluginOnce, createModule/destroyModule,
//                         createWidget/destroyWidget, SimpleEngine, ModuleScaffold,
//                         SYNC_MODEL/requireModelSync
//   test_json.hpp      — testPresetNullGuards/TypeConfusion/OversizedArrays; templates, so
//                         harmless to include even in files that never call them
//   test_harness.hpp   — Test::Harness: the deterministic DSP/UI scheduler, scene layout,
//                         and module/widget lifetime. Supersedes SimpleEngine for new tests
#include "test_plugin.hpp"
#include "test_mock.hpp"
#include "test_context.hpp"
#include "test_json.hpp"
#include "test_harness.hpp"
