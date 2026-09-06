# Test Framework — Developer Manual

Reference for `src/test/`: what each piece is, when to reach for it, and the traps that have
actually cost time. Written to be read top to bottom once, then dipped into.

---

## 1. Orientation

### The shape of a test binary

One `.cpp` per test binary. `plugin-test.mk` globs `src/**/*.test.cpp`, compiles each into its own
executable under `build/test/`, and links it against the plugin dylib. A binary may pull in
`.test.hpp` fragments (3 suites do: SpliceKit, Ahab, Siren), but it is still **one translation
unit** — that assumption is load-bearing in a few places, noted where it matters.

A test binary therefore contains, in the same process:

- the plugin dylib (linked),
- the module source under test (`#include`d directly, so `static` internals are reachable),
- Catch2,
- a fake `rack::Context` built by `Test::TestContext`.

There is no Rack application, no window, no GL context, and no audio thread. What exists is enough
of Rack to construct a module and a widget and to call their methods.

### The headers

Include `framework.hpp` and you get all of it, in the one order that works:

```cpp
#include "../../test/framework.hpp"
```

| Header | Contents | Notes |
|---|---|---|
| `test_plugin.hpp` | Catch2 config, `DEPRECATED` collision fix, `DEBUGPLUGIN` sentinel, `TEST_SUPPRESS_DEPRECATED_*` | **Must be first.** No include guard — see §7 |
| `test_mock.hpp` | `mock::Guard<Base>`, `TEST_MOCK_*` macros, `NullFileAccess`, `MockFileAccess` | |
| `test_context.hpp` | `TestContext`, `SYNC_MODEL`, `createModule`/`destroyModule`, `createWidget`/`destroyWidget`, `ModuleScaffold`, `makeProcessArgs`, `sampleRate` | |
| `test_json.hpp` | The three preset fuzzers | |
| `test_traversal.hpp` | `Test::traversal` — shared widget-tree walk | |
| `test_harness.hpp` | `Test::Harness`, `SceneLayout`, `UiMode`; pulls in `test_events.hpp` | |
| `test_events.hpp` | `Test::EventDriver` | Reached as `h.events()` |

Never include the individual headers directly in a test file. The order in `framework.hpp` is not
stylistic (§7).

### Adoption, as of this writing

74 test files. Useful for judging what is idiomatic versus what is new:

| Facility | Files | Read as |
|---|---|---|
| `SYNC_MODEL` | 57 | Near-universal; omit only if nothing compares model pointers |
| `ModuleScaffold` | 53 | The default lifetime pattern |
| Preset fuzzers | 51 | Standard for any module with `dataFromJson` |
| `Test::createWidget` | 47 | Mostly construction smoke tests |
| `TEST_MOCK_*` | 9 | Grows with `vcv` layer migration |
| `Test::Harness` | 7 | The default setup path for module tests (Intermix ×3 migrated) |
| `EventDriver` | 2 | New in Phase 2 Step 5 |

---

## 2. Build and run

```bash
make DEBUGPLUGIN=1 -j8 test        # build plugin + all test binaries
make testrun DEBUGPLUGIN=1         # build and run everything, 8 binaries in parallel
make testrun-one NAME=Stroke DEBUGPLUGIN=1
```

Variables:

| Variable | Default | Purpose |
|---|---|---|
| `DEBUGPLUGIN` | *(unset)* | **Effectively mandatory** — see below |
| `JOBS` | `8` | Concurrent test binaries in `testrun` |
| `FILTER` | *(unset)* | Catch2 filter for `testrun-one`, e.g. `FILTER='[event]'` |
| `SANITIZER` | `address` | `address`, `undefined`, or `thread` |
| `SUCCESS` | *(unset)* | `SUCCESS=1` prints passing assertions |

### DEBUGPLUGIN is not optional

The `vcv::*Access` seam only exists in a `DEBUGPLUGIN` build; a release build resolves each access
statically and there is no pointer to swap. Two guards enforce this:

- `test_plugin.hpp` `#error`s if the **test TU** lacks `DEBUGPLUGIN`.
- A link-time sentinel (`vcv::assertDebugPluginBuild()`, called from a static initializer) fails
  the link if the **dylib** lacks it.

The sentinel exists because the failure it prevents is silent: without it, a test that touches no
mock would link fine and run against the real Rack API — including the real filesystem.

> **The trap that keeps recurring.** `make testrun` does not imply `DEBUGPLUGIN=1`. A stale
> non-`DEBUGPLUGIN` dylib from an earlier plain `make` produces failures scattered across unrelated
> suites, because weak module symbols resolve into the stale copy. If several unrelated suites fail
> at once, rebuild with `DEBUGPLUGIN=1` before debugging anything. Header dependencies *are*
> tracked, so there is no need to `rm build/test/*`.

### Switching sanitizer

`SANITIZER` is not encoded in binary paths, so changing it does not by itself trigger a rebuild:

```bash
rm -rf build/test && make DEBUGPLUGIN=1 SANITIZER=thread -j8 test
```

TSan is the prerequisite for concurrency testing (Phase 2 Step 7, not yet built).

---

## 3. The context: `TestContext`

Every test file declares exactly one, at file scope:

```cpp
SYNC_MODEL(modelStroke, "Stroke");
Test::TestContext<> testContext;
```

It builds a minimal `rack::Context` — engine, `EventState`, `Scene` — sets `settings::headless`,
disables `ThreadVerifier`, and on first construction runs the plugin's `init()`.

Three things worth knowing:

**Ownership.** `rack::Context::~Context()` deletes `window`, `patch`, `scene`, `event`, `history`
and `engine` unconditionally. So `delete ctx` frees all of them; do not free them separately or you
get a double-free. This was ambiguous for a long time and is now pinned by reading Rack's source.

**`init()` runs during static initialization**, before `main()`, because ~60 test files declare
their `TestContext` at file scope. That is why `initPluginOnce()` installs a `NullFileAccess` around
the `init()` call itself rather than at a call site: no `TEST_CASE`-scoped guard could possibly
exist that early. Without it, `pluginSettings.readFromJson()` would read — and then overwrite with
defaults — the developer's real `plugin.json`.

**`ThreadVerifier` is disabled** (`thread::verifyEnabled = false`), so every `assert(verifier->...)`
in the plugin is inert under test. This is a known gap, not a design choice; re-enabling it with
harness-supplied phase identities is Phase 2 Step 3.

### `SYNC_MODEL` — the two-copies problem

A test binary has **two copies** of each module's model global: one in the dylib (used by `init()`),
one in the test TU (from `#include`ing the module's `.cpp`). Without a sync, any expander check like
`exp->model == modelMidiCatMem` compares the TU's stale copy against the dylib's and never matches —
presenting as a module logic bug rather than a harness problem.

```cpp
SYNC_MODEL(modelStroke, "Stroke");   // file scope, BEFORE the TestContext declaration
```

`SYNC_MODEL` only records an *intent*; the sync loop in `TestContext`'s constructor runs
unconditionally, so a present `SYNC_MODEL` cannot fail quietly. The bug it cannot catch is a
**missing** one — a module whose expander code compares against a global nobody registered. For
that, call `Test::requireModelSync(model, slug)` right where the peer being compared is constructed
(6 files do):

```cpp
auto* exp = /* ... construct expander peer ... */;
Test::requireModelSync(modelMidiCatMem, "MidiCatMem");
REQUIRE(exp->model == modelMidiCatMem);
```

---

## 4. Module lifetime

### `ModuleScaffold` — the default

```cpp
Test::ModuleScaffold<MyModule> mods;
MyModule* m    = mods.create("MySlug");
MyModule* peer = mods.create("MySlug");   // destroyed before m; no explicit teardown
```

Use it rather than bare `createModule`/`destroyModule`. The reason is specific: a failing `REQUIRE`
throws, so a trailing `destroyModule()` never runs. For a self-contained module that only leaks
memory — but any module with process-wide registration (a static instance registry, a listener list)
leaves an entry pointing at freed memory, and **every later test in the binary sees it**. One real
failure then cascades into several fake ones. Observed in SpliceKit: 2 real failures reported as 4.

Modules are destroyed in reverse creation order, and `destroyModule()` fires `onRemove()` — which is
where a well-behaved module drops its shared-state entries.

A suite whose modules need setup applied at construction can pass a factory:

```cpp
Test::ModuleScaffold<SpliceKitModule> mods{[]{ return createWithSyncMode(); }};
MyModule* m = mods.create();
```

**When a bare `destroyModule` is still correct** (8 sites in the suite, each deliberate):
destruction is itself the behaviour under test (stale-master cleanup, registry cleanup after an
instance dies), or the create/destroy pair sits in a scope where no `REQUIRE` can run between them
(a `DEFER` block, an RAII wrapper, a lambda returning a value to the caller's assertion). Add a
comment saying which.

### Stepping a module by hand

```cpp
for (int i = 0; i < 512; i++)
    m->process(Test::makeProcessArgs(i));
```

`makeProcessArgs` defaults its sample rate to `Test::sampleRate()`, which reads
`APP->engine->getSampleRate()` — the same source `createModule()` uses for its `onSampleRateChange`.
That single source of truth is why construction and stepping cannot disagree, even in a test that
changes the engine rate. Pass an explicit rate only when a *mismatch* is the point (`MidiEsx` does
this deliberately, testing that `process()` rejects a non-48kHz rate).

---

## 5. `Test::Harness` — the DSP/UI scheduler

The recommended path for anything that involves a widget, a UI frame, or the interaction between
the two threads.

```cpp
Test::Harness h;
MyModule* m  = h.addModule<MyModule>("MySlug");
MyWidget* mw = h.addWidget<MyWidget>(m);    // positioned in rack coords, added to the scene

h.dspSteps(512);              // 512 process() calls, no UI frame between them
h.uiFrame();                  // one widget step() pass
h.run(Test::seconds(0.1));    // interleave both at their true relative rates
```

Everything runs on the calling thread. The harness *interleaves*; it does not thread. That is
deliberate — a test that fails intermittently is worse than no test, and most of the interesting
bugs are scheduling bugs, fully reproducible from one thread once the schedule is under the test's
control.

### Why the rate ratio matters

At 44.1kHz and 60Hz there are **735 `process()` calls between consecutive `step()` calls**. Any
module that assumes otherwise — an edge flag the UI is supposed to observe, a fixed-size queue the
UI is supposed to drain — is broken in a way no older test in this suite could express.

```cpp
h.setSampleRate(44100);
h.setFrameRate(60);
REQUIRE(h.stepsPerFrame() == Catch::Approx(735.0));
```

`run()` carries the fractional remainder in `dspStepDebt`, so a non-integer ratio (44100/59.94)
neither drifts nor rounds away across repeated calls.

### `UiPresent` vs `UiAbsent`

```cpp
Test::Harness h;                              // UiPresent by default
Test::Harness h2{Test::UiMode::UiAbsent};
h.setUiMode(Test::UiMode::UiAbsent);          // model the editor being closed
```

This decides what `vcv::ui::hasWindow()` answers, which is how `GuiTaskProcessor` chooses between
draining from the widget's `step()` (window present — the production path) and starting a private
worker thread (no window).

The history is worth knowing, because it corrected an obvious-looking plan. `APP->window` is null in
every test binary and cannot be made non-null, so before this every module under test took the
*worker* branch — the suite tested only the less common path of the plugin's most safety-critical
component, and SpliceKit's suite set `syncMode = true` to stop that worker racing the test thread,
so it tested *neither* real path.

The fix that did **not** work was passing a dummy non-null window to `GuiTaskProcessor::process()`:
that parameter was a *default argument*, evaluated at the call site, and real modules call
`taskProcessorUi.process()` with no argument from inside their own `process()`. The harness's
pointer never won. Routing the question through the existing `vcv::UiAccess` seam did work, and the
parameter has since been deleted. General lesson, recorded because it recurs: **for test injection,
extend a `vcv::*Access` seam rather than adding a test-only field or parameter.**

### Lifetime and hooks

Modules and widgets added through the harness are destroyed in reverse order when it goes out of
scope, including when Catch2 unwinds through a failed `REQUIRE` — same guarantee as
`ModuleScaffold`. Widgets go first, since a `ModuleWidget` points at its module.

`onUiFrame(fn)` registers extra per-frame work, for a module whose real widget cannot be constructed
in a test or whose `step()` the test wants to stand in for.

`addBrowserWidget<T>("Slug")` builds the `module == nullptr` widget the module browser creates — a
classic crash source, and worth a smoke test.

---

## 6. UI testing

### `SceneLayout` — the precondition, not an option

`Harness` owns one. It exists because `rack::widget::Widget`'s default box is
`Rect(Vec(), Vec(INFINITY, INFINITY))`, the scene's real size is only ever assigned by
`Window::step()`, and `Scene::step()` dereferences `APP->window` on its first line. Neither runs
headless, so straight out of `TestContext` the scene *and all five of its children* have infinite
boxes and `box.contains(pos)` is true at every position.

Hit-testing is then meaningless in both directions: a widget added last is hit at *any* position,
and one added earlier is unreachable at *every* position because a full-size overlay in front of it
consumes first. That presents exactly as "dispatch doesn't work headless" — and is almost certainly
what produced the long-standing claim in `Stroke.test.cpp` that it doesn't. **The failure was in
layout, not in `EventState`.**

`SceneLayout` gives the scene a finite box, hides its children, **and zeroes their boxes**. The last
part is not redundant: `Scene::onHover()` calls `menuBar->show()` whenever
`mousePos.y < menuBar->box.size.y`, and an un-sized menu bar's height is `INFINITY` — so the first
hover reaching the scene un-hides a full-size menu bar that then consumes everything after it.
Zeroing the boxes makes a re-shown child harmless. It restores everything on destruction.

### `Test::EventDriver` — dispatch through Rack

Reached as `h.events()`. Prefer it over hand-built event structs: a hand-built event tests the
handler and nothing around it — hit-testing, coordinate translation, z-order, consumption ordering
and `EventState` bookkeeping are all skipped, and that is where widget bugs live.

```cpp
h.events().click(mw);                                  // or click(Vec) in scene coordinates
h.events().rightClick(mw);
h.events().hover(mw);                                  // Enter/Leave via setHoveredWidget
h.events().dragBy(knob, Vec(0, -50), /* steps */ 10);  // Button → DragStart → DragMove → DragEnd
h.events().keyPress(GLFW_KEY_A, GLFW_MOD_SHIFT);       // SelectKey first, then HoverKey
h.events().scroll(mw, Vec(0, -1));
h.events().type("abc");
REQUIRE(h.events().consumedBy() == expectedChild);
```

`handleButton()`/`handleHover()` are reproduced line for line from `Rack/src/widget/event.cpp`
except for two `APP->window` reads: `isCursorLocked()` (always false in a test) and `getMods()`
(replaced by an explicit `heldKeyMods`). Everything else — the whole recursion — is Rack's real
code, so geometry, ordering and bookkeeping are genuinely under test.

Double-click timing reads `vcv::fs::getTime()`, so it is deterministic under a `FileAccess` mock:

```cpp
struct ScriptedClock : Test::mock::MockFileAccess {
    double now = 1000.0;
    double getTime() override { return now; }
};
struct { TEST_MOCK_FS(ScriptedClock); } mock;

mock.fs.now = 100.0;  h.events().click(probe);
mock.fs.now = 100.1;  h.events().click(probe);
REQUIRE(probe->doubleClickCount == 1);
```

### Four gotchas that will bite

**1. `consumedBy()` is never `nullptr` for a missed left-click.** `rack::app::Scene` is itself an
`OpaqueWidget`, so an unmatched click is consumed *by the scene*. Use `missed()`:

```cpp
REQUIRE(h.events().missed());                  // correct
REQUIRE(h.events().consumedBy() == nullptr);   // never true for a left-click
```

**2. A left press on a `ModuleWidget` is refused.** Rack's own `ModuleWidget::onDragStart`
dereferences `APP->window->fbDirtyOnSubpixelChange()` for a subpixel-redraw hack, inside an
`if (button == LEFT)`. No seam fixes this — it is Rack reading Rack's window. The driver `FAIL`s
with an explanation rather than segfaulting; ask `EventDriver::canLeftDrag(w)` first. Right-click,
hover, scroll, keys, and dragging a ModuleWidget's *children* (knobs, ports — their own handlers)
all work normally.

**3. Widgets parented to `APP->scene->rack` need `h.exposeRackWidgets()`.** The rack is a descendant
of `rackScroll`, which `SceneLayout` neutralises — so a helper a module widget adds there (Stroke's
`KeyContainer`, Glue's `LabelContainer`) is invisible to every event. Call `exposeRackWidgets()`
*after* adding the widget whose constructor creates the helper; it moves them to the scene and
restores them on teardown, before widget destruction, so `~StrokeWidget` still finds its container.

Leaving `rackScroll` live instead does not work: `RackScrollWidget::onHoverScroll` dereferences
`APP->window` unconditionally, and a live viewport also consumes the position events tests use to
assert a miss.

**4. Positions are scene coordinates, and widget placement is implicit.** `placeWidget()` packs
widgets left to right from a non-zero origin so hit-testing can tell them apart. Read `mw->box` or
use the helpers; never hardcode:

```cpp
EventDriver::centerOf(w)          // scene-space centre
EventDriver::pointIn(w, local)    // a widget-local point in scene space
EventDriver::sceneOrigin(w)       // accumulates through nested parents
```

### `Test::traversal` — the shared spine

`hitTest()`, `hitPath()`, `visitAll()`, `walk()` (with pruning), `isTraversable()`. The spatial walk
of a widget tree, matching `Widget::recursePositionEvent`'s rules exactly: reverse insertion order
(last added is topmost), invisible subtrees skipped, box-contains filtering, local-coordinate
translation.

It knows nothing about events, deliberately. A future `DrawDriver` needs the same
notion of which children get visited and with what transform; writing that twice is how the two end
up disagreeing. `EventDriver` uses it for geometry queries only — "what is under this point" —
which is a different question from "who consumed the event", and only real dispatch answers the
latter.

### What is not testable

**Drawing.** `NVGcontext` is opaque and its only constructor requires a live GL context. It cannot
be subclassed, stubbed at the type level, or faked with a cast. A seam does not help, because
`draw()` takes its context as a *parameter* — it never asks the window for one.

**Real concurrency.**

---

## 7. Mocking the `vcv` access layer

Production code routes filesystem, dialog, module, scene, cable, history and network operations
through `StoermelderPackOne::vcv`. Each is a virtual interface behind a global pointer, swappable in
a `DEBUGPLUGIN` build.

```cpp
struct MockUi : vcv::UiAccess {
    std::string lastSaveName;
    std::string saveDialog(const std::string& filters, const std::string& dir,
                           const std::string& filename) override {
        lastSaveName = filename;
        return "/tmp/out.log";      // "" would mean the user cancelled
    }
};

TEST_CASE("export uses the right filename") {
    struct { TEST_MOCK_UI(MockUi); } mock;      // installs for this scope, restores after
    // ...
    REQUIRE(mock.ui.lastSaveName == "MidiMon.log");
}
```

Seven macros, one per slot. Each hardcodes its own member name, `Base` type and global — there is no
regular naming convention to infer them from (the `FileAccess` global is `fileAccess`, not
`fsAccess`), and a wrong Type/slot pairing fails to compile:

| Macro | Member | Interface |
|---|---|---|
| `TEST_MOCK_MODULES(T)` | `.modules` | `vcv::ModuleAccess` |
| `TEST_MOCK_SCENE(T)` | `.scene` | `vcv::SceneAccess` |
| `TEST_MOCK_CABLES(T)` | `.cables` | `vcv::CableAccess` |
| `TEST_MOCK_UI(T)` | `.ui` | `vcv::UiAccess` |
| `TEST_MOCK_FS(T)` | `.fs` | `vcv::FileAccess` |
| `TEST_MOCK_HISTORY(T)` | `.history` | `vcv::HistoryAccess` |
| `TEST_MOCK_NW(T)` | `.nw` | `vcv::NwAccess` |

Two base mocks are provided: `mock::MockFileAccess` forwards everything to the real
`rack::system` (inherit and override just what you care about), and `mock::NullFileAccess` denies
all I/O (used internally to fence off `init()`).

`mock::Guard<Base>` is the underlying single-slot RAII installer if you need one directly. It saves
the slot's **previous** pointer and restores that — not `nullptr` — so nested mocks work and a slot
someone else installed survives.

---

## 8. Preset fuzzing

Three helpers, applied by 51 files. They have found real bugs and are the cheapest coverage in the
framework:

```cpp
auto module = mods.create("MySlug");
json_t* rootJ = module->dataToJson();
REQUIRE(rootJ != nullptr);

Test::testPresetNullGuards(module, rootJ);       // every property → json_null()
Test::testPresetTypeConfusion(module, rootJ);    // every value → wrong type
Test::testPresetOversizedArrays(module, rootJ);  // every array grown by 64

json_decref(rootJ);
```

What each catches:

- **Null guards** — `json_object_get()` returning NULL for a missing key, then dereferenced;
  `if (dataJ)` passing for `json_null` and then iterated; a nested read whose parent was null.
- **Type confusion** — `json_string_value()` returns NULL for a non-string, which is UB assigned to
  a `std::string`. Descends into arrays, because `"label": 42` is exactly the shape of real patch
  corruption.
- **Oversized arrays** — unbounded `for (s = 0; s < json_array_size(setsJ); ++s)` writing past a
  fixed `[SETS]` member when loading a hand-edited patch.

Each mutation is annotated with `CATCH_INFO` naming the JSON path, and Catch2's own
`FatalConditionHandler` surfaces that context on a crash — so an unguarded loader gives a one-line
diagnosis rather than a bare `SIGSEGV`.

> No manual per-`SECTION` module reset is needed. **Catch2 re-runs the whole `TEST_CASE` body once
> per `SECTION`**, so the module declared in that body is constructed fresh for each one
> automatically. A design note once claimed otherwise and a factory-overload API was built and then
> reverted; the single `T* module` form is correct.

---

## 9. Traps and invariants

Collected because each has cost real time.

**Include order is load-bearing.** `test_plugin.hpp` has no include guard, and must come first in
every test TU. Catch2 v3.12.0 defines `DEPRECATED(msg)` as function-like; Rack's `common.hpp`
defines it object-like. Both are unconditional `#define`s with no guard on either side, so whichever
lands second wins silently. `test_plugin.hpp` re-asserts Rack's form afterwards. Re-check this on
the next Catch2 upgrade. Just include `framework.hpp`.

**Never suppress deprecation warnings TU-wide.** Use `TEST_SUPPRESS_DEPRECATED_BEGIN`/`END` around
individual call sites. A blanket never-popped `#pragma push` here also hides legitimate warnings in
the module source, which is `#include`d into the same TU. Also: `#pragma push`/`pop` is a flat stack
across parse order, **not scoped to `{}` blocks** — an `if`/`else` needs its own pair per branch,
or the first branch's `END` closes the suppression before the second branch's call runs.

**Header statics are per-TU, not per-binary.** Every namespace-scope helper in these headers is
`inline`, and counters live in function-local statics behind accessors (`testContextCount()`,
`modelSyncRegistry()`). `#pragma once` does not protect against this. Today each binary is one TU so
it would work anyway — but the moment two `.cpp` files link into one binary, a header-scope `static`
counter silently becomes two counters and the plugin initialises twice.

**One `TestContext` per binary, at file scope.** A second `TestContext<CustomScene>` after a
`TestContext<>` would `dynamic_cast` to `nullptr`; there is a `REQUIRE` for that now.

**`EventState` is process-wide.** A widget left hovered, dragged or selected by one part of a test
changes how the next part dispatches — a selected widget sees `SelectKey` before anything sees
`HoverKey`. Call `h.events().reset()` between phases. Any widget added to the scene by hand must be
passed to `APP->event->finalizeWidget()` before deletion, or it dangles into every later
`TEST_CASE`. `Test::destroyWidget()` and `unregisterModule()` both do this for you.

**Catch2 runs all `TEST_CASE`s in one process.** Assert on *deltas*, not absolute counts, for
anything process-wide (scene children, registries) — an earlier case may have left something behind.

**A known pre-existing flake.** `Siren`'s background-task suite occasionally fails or aborts under
parallel load and passes in isolation. It is a real `TaskWorker`/teardown race, documented since
Phase 1, and not a regression from whatever you just changed. Confirm with
`make testrun-one NAME=<the failing binary>` before investigating.

**Rack code that cannot run headless.** These dereference `APP->window` where no seam reaches:

| Rack code | Consequence |
|---|---|
| `ModuleWidget::onDragStart`/`onDragEnd` | Left-press on any `ModuleWidget` segfaults |
| `Scene::onHover` | Un-hides the menu bar; `SceneLayout` zeroes boxes to compensate |
| `RackScrollWidget::onHoverScroll` | The rack viewport cannot be left live |
| `EventState::handleButton`/`handleHover` | Re-implemented in `EventDriver` |
| `Window`'s constructor | `APP->window` can never be made non-null |

When a headless widget test segfaults on a null dereference, suspect Rack's handler before the
plugin's.

---

## 10. Writing a new test — the skeleton

```cpp
#include "../../test/framework.hpp"
#include "MyModule.cpp"

using namespace rack;
using namespace StoermelderPackOne;

SYNC_MODEL(modelMyModule, "MyModule");
Test::TestContext<> testContext;


TEST_CASE("Construction", "[MyModule]") {
    Test::ModuleScaffold<MyModule> mods;
    MyModule* m = mods.create("MyModule");
    REQUIRE(m != nullptr);
}

TEST_CASE("Preset round-trip is guarded", "[MyModule][JSON]") {
    Test::ModuleScaffold<MyModule> mods;
    auto module = mods.create("MyModule");

    json_t* rootJ = module->dataToJson();
    REQUIRE(rootJ != nullptr);
    Test::testPresetNullGuards(module, rootJ);
    Test::testPresetTypeConfusion(module, rootJ);
    Test::testPresetOversizedArrays(module, rootJ);
    json_decref(rootJ);
}

TEST_CASE("The UI observes what the DSP thread produced", "[MyModule][ui]") {
    Test::Harness h;
    auto* m  = h.addModule<MyModule>("MyModule");
    auto* mw = h.addWidget<MyModuleWidget>(m);

    h.run(Test::seconds(0.1));      // real 735:1 interleaving

    h.events().rightClick(mw);
    REQUIRE_FALSE(h.events().missed());
}
```

Checklist before committing:

- [ ] `SYNC_MODEL` at file scope if anything compares model pointers
- [ ] `ModuleScaffold` or `Harness` owns every module — no bare `destroyModule` without a comment
- [ ] The three preset fuzzers, if the module has `dataFromJson`
- [ ] Built and run with `DEBUGPLUGIN=1`
- [ ] Passes in a full parallel `make testrun`, not just in isolation
- [ ] New framework behaviour has a test in `src/test/*.test.cpp`

---

## 11. Files

| File | Lines | Role |
|---|---|---|
| `framework.hpp` | 33 | Umbrella — the only header a test should include |
| `test_plugin.hpp` | 89 | Catch2 config, macro collisions, build sentinels |
| `test_mock.hpp` | 103 | `vcv` access mocking |
| `test_context.hpp` | 347 | Context, module/widget lifetime |
| `test_json.hpp` | 299 | Preset fuzzers |
| `test_traversal.hpp` | 151 | Shared widget-tree walk |
| `test_events.hpp` | 442 | `EventDriver` |
| `test_harness.hpp` | 515 | `Harness`, `SceneLayout`, `UiMode` |
| `test_harness.test.cpp` | 520 | The harness's own tests |
| `test_events.test.cpp` | 704 | The driver's own tests |
| `catch_amalgamated.{hpp,cpp}` | 30k | Catch2 v3.12.0, compiled once and shared |

Framework changes belong with a test in `test_harness.test.cpp` or `test_events.test.cpp`. Both
exist because the framework is what every other test rests on, and a silent regression there is
worse than a module bug.
