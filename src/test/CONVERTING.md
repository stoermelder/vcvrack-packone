# Converting a test suite to the archive-linked format

How to move one test suite onto the static-archive build and split it into the three-file layout.
Mechanical: no test case needs rewriting.

Background and measurements: `var/TestFramework_review.md`, Q3–Q3n. Framework usage:
`src/test/FRAMEWORK.md`.

## Why

A dylib-linked test binary contains **two copies** of the module under test: the test `.cpp`
`#include`s the module's `.cpp`, and `plugin.dylib` is on the link line as well. Linking a *dylib*
demotes implicitly-inline class members to image-local, so the dylib's copies are unreachable and
cannot merge. The same objects in a *static archive* keep `weak external` linkage and coalesce with
the test TU's copies into one definition.

That removes `SYNC_MODEL`'s reason to exist for the converted suite, the stale-`plugin.dylib`
trap, and the class of bug where a file-scope `static` in a module `.cpp` silently exists twice.

## Target layout

Three files per suite:

| File | Holds |
|---|---|
| `<Name>.test.hpp` | includes, `using`, `TestContext`, shared helpers |
| `<Name>.test.module.hpp` | the `TEST_CASE`s — not a standalone header |
| `<Name>.test.cpp` | includes the two above, plus `testPluginInit()` |

Suites with several topics use one `.test.<topic>.hpp` per topic instead of a single
`.test.module.hpp` (see `Arena`, `SpliceKit`, `Siren`). A suite that already has topic files only
needs step 3.

## Steps

### 1. `<Name>.test.hpp` — the preamble

Everything above the first `TEST_CASE` moves here, plus `#pragma once`.

```cpp
#pragma once

// Shared preamble for the SAIL test suite.
// Included by Sail.test.cpp, which pulls the test cases in from Sail.test.module.hpp
// inside their own namespace.

#include "../../test/framework.hpp"
#include "Sail.cpp"

using namespace StoermelderPackOne::Sail;

Test::TestContext<> testContext;
```

Keep `#include "<Name>.cpp"` **above** the `TestContext` declaration. `TestContext`'s constructor
runs during static initialization and calls `testPluginInit()`, which reads the model globals;
same-translation-unit initialization is ordered by declaration, so the include must come first.
Below it, the globals are still null and the binary segfaults in `Plugin::addModel`.

### 2. `<Name>.test.module.hpp` — the test cases

Everything from the first `TEST_CASE` onward, moved verbatim. No includes, no guard: it is only
ever pulled in by the `.cpp`, inside a namespace.

```cpp
// SAIL test cases. Included by Sail.test.cpp inside namespace __module.
// Not a standalone header: Sail.test.hpp supplies everything these cases use.

TEST_CASE("Construction and initialization", "[Sail]") {
	...
```

### 3. `<Name>.test.cpp` — entry point

```cpp
#include "Sail.test.hpp"

namespace __module {
	#include "Sail.test.module.hpp"
}

// This binary's plugin entry point: registers just the models this suite needs.
Plugin* pluginInstance;

void testPluginInit(rack::Plugin* p) {
	pluginInstance = p;
	p->addModel(modelSail);
}
```

**Register exactly the models the suite constructs or compares** — nothing more. Most suites need
one; the expander suites (`Intermix`+`IntermixGate`, `MidiCat`+`MidiCatMem`, `EightFace`+`EightFaceX2`,
`Affix`+`AffixMicro`, `Transit`+`TransitEx`, …) need two.

The name `testPluginInit` matters: it must **not** be `init()`. `init()` is what the plugin's own
`plugin.cpp` defines, and that file gets linked in from the archive — a same-name function would
collide, and worse, the real `init()` registers all ~70 models, most of which are null during
static initialization.

### 4. Verify

No build-file change: `plugin-test.mk` builds every `*.test.cpp` this way.

```
make testrun-one NAME=Sail
```

Optionally confirm the point of the exercise — no dylib, one copy:

```
otool -L build/test/Sail.test | grep -c plugin.dylib          # 0
nm build/test/Sail.test | c++filt | grep -c 'SailModule::process('   # 1
```

## Notes

- **ASan coverage narrows.** The plugin's objects are built without `-fsanitize`, so an
  archive-linked binary instruments only its own TUs — which still includes the module `.cpp`,
  since that is `#include`d. Prebuilt code from *other* modules is uninstrumented.
- **Suites that `#include` no module `.cpp`** (`MidiProcessor`, `MidiTrackingProcessor`, the
  `Siren*` helpers, `Mb_autotag`) have no duplication to remove; they still need an entry point if
  they declare a `TestContext` (an empty one — see the end state below).
- **Register only models whose `.cpp` the suite `#include`s.** A model global from another archive
  member is not yet constructed when `testPluginInit()` runs; only same-TU definitions are ordered.

## The end state — reached 2026-09-09

Every suite is migrated, so the transitional machinery is gone:

- **`TEST_PLUGIN_INIT_CUSTOM` and the default `testPluginInit()` are removed.**
  `test_context.hpp` now only *declares* the entry point, so a suite that forgets one gets a
  link error naming it. The 60 now-inert `#define TEST_PLUGIN_INIT_CUSTOM` lines were swept from
  the suites in the same pass; nothing to `#define` any more.
- **`plugin-test.mk` has one pattern rule again.** `TEST_NODYLIB_NAMES`, the
  `foreach`/`eval` rule generator and the dylib-linked rule are all gone; every binary links
  `$(TEST_PLUGIN_ARCHIVE)`. `test:` no longer depends on `$(TARGET)` — test binaries never link
  the dylib, and module-source staleness propagates through the archive instead.

Suites that need no model at all use an **empty** entry point:

```cpp
// No models needed: this suite tests a helper, not a module.
Plugin* pluginInstance;

void testPluginInit(rack::Plugin* p) {
	pluginInstance = p;
}
```

Two details worth knowing, both settled during the migration:

- **8 suites use `TestContext` but need no model** (`SirenAudio`, `SirenBackgroundTasks`,
  `SirenBpmDetector`, `SirenFileSystem`, `GuiTaskProcessor`, `MpmcTaskWorker`, `TaskWorker`,
  `EventDispatchSpike`) and 4 more use no `TestContext` at all (`MidiProcessor`,
  `MidiTrackingProcessor`, `ClockDividerEx`, `ClockMultiplier`, `selection`). The first group needs
  the empty entry point above; the second needs nothing.
- **Do not register a model the suite merely names.** `Mb_autotag` used to declare a sync for
  `modelMb` without `#include`ing `Mb.cpp`; registering that global would read it from another
  archive member, where it is still null during static initialization.

## `SYNC_MODEL` — removed 2026-09-10

`SYNC_MODEL` and `Test::requireModelSync()` existed only to reconcile the *two* copies of a model
global a dylib-linked binary had. With the archive there is one copy, and the sync loop became a
self-assignment: `pluginInstance->getModel(slug)` returns whatever `testPluginInit()` registered,
which *is* this TU's `modelFoo`.

Both are gone — 47 `SYNC_MODEL` statements, 7 `requireModelSync` calls across 44 files, plus the
registry, the macro and the sync loop in `test_context.hpp`.

Verified on the paths that actually depended on it: all ten expander suites, whose module code
compares model pointers (`exp->model == modelMidiCatMem` and friends), pass —
`MidiCatMem`, `MidiCatClk`, `MidiCatFine`, `IntermixEnv`, `IntermixFade`, `IntermixGate`,
`EightFace`, `TransitEx`, `Affix`, `Infix`.
