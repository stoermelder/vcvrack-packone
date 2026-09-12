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
| `test_context.hpp` | `TestContext`, `createModule`/`destroyModule`, `createWidget`/`destroyWidget`, `ModuleScaffold`, `makeProcessArgs`, `sampleRate` | |
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
| `ModuleScaffold` | 53 | The default lifetime pattern |
| Preset fuzzers | 51 | Standard for any module with `dataFromJson` |
| `Test::createWidget` | 47 | Mostly construction smoke tests |
| `TEST_MOCK_*` | 9 | Grows with `vcv` layer migration |
| `Test::Harness` | 7 | The default setup path for module tests (Intermix ×3 migrated) |
| `EventDriver` | 3 | New in Phase 2 Step 5; Tilt is the worked widget-test example |

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
Test::TestContext<> testContext;
```

It builds a minimal `rack::Context` — engine, `EventState`, `Scene` — sets `settings::headless`,
disables `ThreadVerifier`, and on first construction runs the suite's `testPluginInit()`.

Three things worth knowing:

**Ownership.** `rack::Context::~Context()` deletes `window`, `patch`, `scene`, `event`, `history`
and `engine` unconditionally. So `delete ctx` frees all of them; do not free them separately or you
get a double-free. This was ambiguous for a long time and is now pinned by reading Rack's source.

**`testPluginInit()` runs during static initialization**, before `main()`, because every test file
declares its `TestContext` at file scope. That is why `initPluginOnce()` installs a `NullFileAccess`
around the call itself rather than at a call site: no `TEST_CASE`-scoped guard could possibly exist
that early. Without it, `pluginSettings.readFromJson()` would read — and then overwrite with
defaults — the developer's real `plugin.json`.

It is also why a suite registers only models whose `.cpp` it `#include`s: a model global from
another translation unit is not constructed yet at that point. See `src/test/CONVERTING.md`.

**`ThreadVerifier` is disabled** (`thread::verifyEnabled = false`), so every `assert(verifier->...)`
in the plugin is inert under test. This is a known gap, not a design choice; re-enabling it with
harness-supplied phase identities is Phase 2 Step 3.

### The two-copies problem — solved by the build, not by a sync

Historically a test binary held **two copies** of each module's model global: one in
`plugin.dylib` (used by its `init()`), one in the test TU (from `#include`ing the module's
`.cpp`). Any expander check like `exp->model == modelMidiCatMem` compared the TU's copy against
the dylib's and never matched — presenting as a module logic bug rather than a harness problem.
A `SYNC_MODEL(modelFoo, "Foo")` macro papered over it by overwriting the TU's pointer after
`init()` ran, and `Test::requireModelSync()` existed to catch a *missing* `SYNC_MODEL`.

Test binaries now link the plugin's objects as a static archive rather than the dylib, so there
is exactly one copy of every module symbol and nothing to reconcile. **Both `SYNC_MODEL` and
`requireModelSync` are gone** (removed 2026-09-10); each suite's `testPluginInit()` registers the
models it needs directly. See `src/test/CONVERTING.md` for the layout and
`var/TestFramework_review.md` Q3–Q3o for why the archive works where the dylib could not.

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

### Background workers — `TaskWorker` and `ITaskWorker`

> **The harness does not schedule worker threads yet.** So how a module *declares* its worker
> decides whether it can be tested deterministically at all — that is what this section is about.
> Every module in the plugin now follows the rule below.

Two separate mechanisms, often confused because both run tasks off the engine thread:

| | `TaskWorker` (`utils/TaskWorker.hpp`) | `GuiTaskProcessor` (`utils/GuiTaskProcessor.hpp`) |
|---|---|---|
| Thread | One, permanent, started in the constructor | Conditional — only when `hasWindow()` is false |
| Selected by | Nothing; it always runs | `UiMode` (see above) |
| Drained by | The worker only | The widget's `step()`, **or** the worker |
| Test seam | `ITaskWorker` | `UiMode`, and a legacy `syncMode` flag |

`GuiTaskProcessor` is already covered: set `UiMode::UiPresent` and its tasks drain from
`h.uiFrame()`, deterministically, on the production path. Nothing further is needed, and **new
tests should not set `syncMode`** — it suppresses *both* branches, so a test using it exercises
neither real path. It survives only in SpliceKit's scaffold and is scheduled for deletion.

`TaskWorker` is the gap. It has no `UiMode` equivalent, so a module that queues work through it
runs that work on a real background thread with no defined moment at which it has landed. Do not
sleep or spin waiting for it.

#### How to write a module so its worker is testable

Same principle as §7.1, and the same failure mode: get this right while *writing* the module. A
concrete `TaskWorker` member cannot be substituted later, and the mistake is invisible — the code
compiles, runs correctly in Rack, and its test silently exercises a real background thread.

**Take the worker as a constructor parameter, and default it to one worker per `Context`.**

```cpp
// One worker per Rack Context (one per plugin instance, and one per test binary),
// shared by all MyModule instances within it. The weak_ptr lets it be destroyed
// when the last module in that Context is removed.
//
// Only ever reached from the DEFAULT constructor — a module built with an
// injected worker never calls this, so a test constructs no thread at all.
//
// Function-local static, not a class static: the test build both links the dylib
// and #includes this .cpp, so a class static would exist twice and writes through
// one copy would be invisible through the other. Same rationale as SpliceKit's
// getInstances() (SpliceKit.cpp:682).
static std::shared_ptr<ITaskWorker> defaultWorker() {
    static std::mutex m;
    static std::map<Context*, std::weak_ptr<ITaskWorker>> workers;
    std::lock_guard<std::mutex> lock(m);
    auto& slot = workers[APP];
    if (auto w = slot.lock()) return w;     // lock() once — no expired()/lock() gap
    auto worker = std::make_shared<MpmcTaskWorker>("MyModule worker");
    slot = worker;
    return worker;
}

struct MyModule : Module {
    MyModule() : MyModule(defaultWorker()) {}
    explicit MyModule(std::shared_ptr<ITaskWorker> worker) {
        // ... config() etc ...
        this->worker = std::move(worker);          // before anything can queue
    }
    std::shared_ptr<ITaskWorker> worker;

    void process(const ProcessArgs& args) override {
        if (somethingHappened) worker->work([=]() { doTheWork(); });
    }
};
```

Five properties, each of which a simpler version loses:

- **No thread exists unless one is needed.** Because `defaultWorker()` is reached only from the
  default constructor, a test passing its own worker never reaches the map and no
  `MpmcTaskWorker` is ever constructed. An owned-member worker (`TaskWorker w;`) starts its thread
  in the constructor regardless, leaving every test carrying a parked thread per module.
- **The worker cannot be injected too late.** It is a constructor parameter, so there is no window
  between construction and injection in which a task could go to the wrong worker. Assign it before
  anything in the constructor can queue.
- **Thread count is bounded by Rack instances, not module instances.** Sixteen modules in one patch
  share one worker.
- **Contexts stay isolated.** Two plugin instances — or, for tests, two `TestContext`s — never
  share a worker, so one cannot serialise behind or observe the other's tasks.
- **The worker dies with the last module in its Context.** `weak_ptr` rather than a leaked
  singleton, so clearing the patch tears the thread down.

Three details in that function that are easy to get wrong:

- **`lock()` once, not `expired()` then `lock()`.** Testing `expired()` and then calling `lock()`
  separately leaves a window in which the last owner releases between the two calls, so `lock()`
  returns null after the code has already committed to the "still alive" branch.
- **The mutex is not optional here.** Rack constructs modules on the UI thread, so a bare pointer
  swap would *usually* be safe — but this mutates a `std::map`, and module construction is rare
  enough that the lock costs nothing measurable. Enforced beats assumed.
- **Empty map entries are never erased.** `workers[APP]` inserts a slot for a `Context` that may
  later be destroyed. That is a bounded leak — one empty `weak_ptr` per Rack instance — and
  deliberately not worth fixing; SpliceKit makes the same call for the same reason
  (`SpliceKit.cpp:665`).

**The variant to pick.** Per-`Context` is the default. Two others are occasionally right:

| Variant | One worker per | Use when |
|---|---|---|
| Per-instance — `return std::make_shared<MpmcTaskWorker>(...)`, no static at all | module | Tasks are long enough that one instance must not delay another's |
| **Per-`Context`** (above) | **Rack instance** | **Default** |
| Process-global — a single `static weak_ptr` | process | Effectively never: strictly worse isolation than per-`Context`, with no advantage |

Per-instance is the simplest and has no shared state to guard, so prefer it whenever the extra
threads are affordable and head-of-line blocking would be a real risk.

Three rules for the task body itself, each of which has a counterexample in the tree:

1. **Keep the task body a named method, not a lambda body.** Write
   `worker->work([=]() { groupBypassWorker(val); })`, not fifteen lines inline. The named method is
   callable from a test directly, which is the fallback when there is no seam — and it stays
   readable at the enqueue site. Strip gets this right (`Strip.cpp:223`).
2. **The task must not assume it runs on the engine thread.** It does not, in production. If it
   needs engine state, capture it by value at enqueue time rather than reading it from `this` when
   the task runs. Anything it touches concurrently with `process()` needs to be atomic or queued.
3. **The task must be safe to run *late*, or not at all.** A queued task may run several DSP blocks
   after the event that queued it, and a fixed-size queue may drop it entirely —
   `GuiTaskProcessor::enqueue()` returns `false` when full (`GuiTaskProcessor.hpp:117`). If dropping
   it would corrupt state, the queue is the wrong mechanism.

**Constructor injection is the part to adopt everywhere; the sharing is a separate decision.** The
table above is about how many threads exist, and changing it never affects testability — a test
injects its own worker either way.

**A widget-side worker can be simpler.** Siren holds one on the *widget* (`Siren.cpp:785`) and
hands components a bare `ITaskWorker*` (`SirenBrowserPane.hpp:245`). That works because they are
constructed after the widget owns it and none is reached from `process()` — so there is no
construction-ordering hole for the constructor parameter to close.

#### Testing a module written this way

Pass a `SyncTaskWorker` and every task runs inline, to completion, before `work()` returns:

```cpp
// Construct directly, not through the dylib factory — see the note below.
auto* m = new MyModule(std::make_shared<StoermelderPackOne::SyncTaskWorker>());
```

Wrap that in a suite-local `createModule()` and bind `ModuleScaffold` to it, so every scaffolded
module gets the injected worker. MidiKit does exactly this (`MidiKit.test.hpp:64-79` — MidiKit
lives on the `midi-kit` branch, so its file references resolve there, not on `v2-dev`), and the
reason is worth stating: **`Test::createModule` / `Harness::addModule` go through the dylib's model
factory, which only knows the default constructor** — so a module created through them gets the
real async worker no matter what the test wants. Until the harness can construct with arguments, a
worker-injecting suite needs its own factory shadow.

For a component the test calls directly, no shadow is needed — construct the `SyncTaskWorker` and
pass it in:

```cpp
StoermelderPackOne::SyncTaskWorker worker;   // runs inline
SirenIndexTask task;
task.start(&worker, src, cancel);
REQUIRE(task.progress->done.load(std::memory_order_acquire));   // already done, no polling
```

See `SirenBackgroundTasks.test.cpp` for ten worked examples.

**When inline is the wrong answer.** `SyncTaskWorker` makes a fire-and-forget call and a blocking
one behave identically, which erases exactly the property some tests exist to check — MidiKit's
case is `loadScript()` (async) versus `closeState()` (blocks until `onUnload()` has run); under an
inline worker a test asserting teardown ordering passes against code that never waits. Those tests
need a real worker plus a barrier:

```cpp
// A real background worker, for tests that must distinguish async from blocking dispatch.
static std::shared_ptr<ITaskWorker> asyncWorker() {
    return std::make_shared<MpmcTaskWorker>("MyModule test worker");
}

// Push a sentinel and wait for it. The queue is FIFO, so once the sentinel runs,
// every earlier task has finished — the only way to know an async call has landed.
// Bounded: an unbounded spin turns a stalled worker into a silent 100%-CPU hang.
barrier(worker);            // see MidiKit.test.hpp:107 for a complete implementation
```

Two details that implementation gets right and a naive barrier does not: `work()` returns `false`
when the queue is momentarily full, so the sentinel push must retry; and the sentinel flag must be
a `shared_ptr`, not a stack reference, or a timeout leaves the worker writing into a dead frame.

#### Writing the test so it survives the harness gaining support

- **Never let a worker body run from inside `process()` in a test.** The planned harness runs
  worker tasks in a third phase (`workerDrain()`), never inline on the engine thread — that is the
  whole point, since a task like Strip's calls `APP->engine->bypassModule()`, which exclusively
  locks the engine. A test that reaches the body through `dspStep()` today is asserting a sequence
  the harness will deliberately stop producing.
- **Keep the queue-side and the task-side assertions separate.** Assert what `process()` *queued*
  in one place and what the task *did* in another, rather than one assertion spanning both. When
  `workerDrain()` and `pendingWorkerTasks()` arrive, the split is already where it needs to be.
- **Do not assert on wall-clock ordering between the engine thread and a worker.** There is no
  guaranteed ordering now, and the harness will define one — deterministically, and probably not
  the one a sleep happens to produce today.

#### The trap in `SyncTaskWorker`

It runs the task **on the calling thread, inside `work()`**. For a component test that is the point.
For a module whose `process()` queues the task it is wrong twice over: the task runs on the engine
thread it was queued to escape, and the queued-but-not-yet-run window — where torn reads and lost
edges live — never exists for the test to observe. `SirenBackgroundTasks.test.cpp:54` records the
same limitation from the other side ("a real worker can't [be observed mid-flight]").

`isWorkerThread()` has the matching sharp edge: `SyncTaskWorker` returns `true` unconditionally
(`TaskWorker.hpp:171`), because every thread is "the worker thread" when tasks run inline. A module
asserting "this only runs on the worker" therefore always passes under it. That assertion is not
meaningfully tested until the harness supplies a worker that can answer `false`.

### Expanders

```cpp
h.connectExpander(transit, ex);              // transit ← → ex, both sides
h.connectChain(transit, ex1, ex2);           // left-to-right, one link at a time
h.disconnectExpander(transit, Test::Harness::SIDE_RIGHT);
```

Use these rather than assigning `rightExpander.module` / `leftExpander.module` by hand. Hand-wiring
silently skips two things Rack does:

- **`onExpanderChange` is never dispatched.** Rack assigns the neighbour through
  `Module::setExpanderModule()`, which fires the event when the pointer actually changes. **11
  modules in this plugin override it**, and several do real work there — MidiCat and Transit call
  `notifyModuleListeners()` (which is what sets `moduleChangedFlag`); `IntermixBase` unpublishes its
  expander message and resets its outputs on a left-side change. A hand-wired test reaches the
  steady state a module settles into *after* a change, but never exercises the change itself.
- **`Expander::moduleId` is left at `-1`.** Rack maintains it alongside `module`, and
  `setExpanderModule` does *not* touch it — the engine assigns it separately. Strip walks chains by
  `moduleId`, so a hand-wired neighbour reads as connected-but-unidentifiable.

Connections do not step. Whether a module needs a `dspStep()` afterwards — and how many — is the
module's business, so it stays at the call site where it can be asserted.

> **The harness never touches `moduleChangedFlag`.** That is the plugin's own
> `ModuleChangeListener` signal, and a module reaches it *through*
> `onExpanderChange → notifyModuleListeners()`. Setting it from the harness would mask a module
> that forgot to notify. A test that needs the flag without a real notification should set it
> itself, and say why. There is a `TEST_CASE` in `test_harness.test.cpp` pinning this.

Two things worth knowing before reading a green expander test as proof of anything, both measured
on Transit rather than reasoned about:

- **Notification is often redundant.** Transit *and* TransitEx both call
  `notifyModuleListeners("Transit")` from their own `onExpanderChange`, and the harness notifies
  both sides — so deleting either call alone keeps the suite green. Only removing both fails it.
- **A first-step rescan can hide the flag entirely.** Transit's gate is
  `moduleChangedFlag || ctrlMode != BASE::ctrlMode`; on a fresh module the second term is already
  true, so a test that connects before its first `dspStep()` would pass with no notification at
  all. Step to a settled state first if the flag is what you mean to test.

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

**Find the inner widget by type, don't reconstruct its position.** A `ModuleWidget`'s interesting
children are constructor locals with no accessor, so the tempting alternative is to re-derive where
the panel put them from its layout constants — a second copy of the layout, free to drift until the
click lands on the wrong widget and the test asserts a no-op:

```cpp
auto* lane = h.events().find<TiltEdgeWidget<MODULE, EDGE::TOP>>(mw);   // REQUIREs a match
h.events().dragBy(lane, Vec(0, 50), 10);
auto lanes = h.events().findAll<InnerWidget>(mw);      // several of a kind, topmost first
```

`find<T>()` fails the test when nothing matches, rather than returning a nullptr that segfaults
three lines later. Where a widget maps a position to a logical index itself (a grid's `cellAt()`, a
lane's `slotAt()`), derive the click point and then *assert it against that function* — the position
is then correct by construction and a layout change moves the click with the widget. `Tilt.test.hpp`'s
`tiltCellCenter()`/`tiltSlotCenter()` are the worked example.

`handleButton()`/`handleHover()` are reproduced line for line from `Rack/src/widget/event.cpp`
except for two `APP->window` reads: `isCursorLocked()` (always false in a test) and `getMods()`
(routed through the `vcv::ui::getWindowMods()` seam). Everything else — the whole recursion — is
Rack's real code, so geometry, ordering and bookkeeping are genuinely under test.

### Modifier keys

A widget that gates behaviour on held modifiers reads `vcv::ui::getWindowMods()`, and a test sets
it with `setMods()`:

```cpp
h.events().setMods(RACK_MOD_CTRL);
h.events().scroll(mw, Vec(0, 1));     // ctrl+scroll, as the widget sees it
h.events().clearMods();
```

This matters because Rack's `HoverScrollEvent` carries **no** `mods` field, which is exactly why
Rack itself polls the window for scroll modifiers — so for an `onHoverScroll()` handler there is no
way to pass mods through the event, and before the seam carried them those branches (Spin's
mods-gated scroll, Mb's ctrl+zoom) were unreachable under test.

The value lives in the installed `vcv::UiAccess` (`UiAccess::testMods`), not on the driver, so the
mods a widget sees and the mods the synthesised `RACK_HELD` key repeats carry are one value and
cannot disagree. It is on the base interface rather than a mock so that a suite installing its own
`UiAccess` over the harness's — Strip, MidiMon and MidiCat all do — still honours it. Mods persist
until changed, like a real held key; `reset()` deliberately leaves them alone.

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

### `getMousePos()` tracks synthetic input automatically

16 widgets across 11 modules in this plugin (Tilt's grid and edge lanes, Maze, Hive, Glue's label,
Siren's waveform canvas, XySeq/XyScreen, Strip, MidiCat, Mb) drive their drags off
`APP->scene->rack->getMousePos()` rather than `e.mouseDelta`, because that is the only way to get a
position in a stable coordinate space across a drag. Rack maintains that field in
`RackWidget::onHover()/onDragHover()` as an event descends through the rack — but the rack is a
descendant of `rackScroll`, which `SceneLayout` neutralises, so it is never written under a harness.

`EventDriver::syncRackMousePos()` closes that, on every `button()` and `hover()`. It matters because
the gap was silent in the direction that *passes*: a drag over such a widget dispatched perfectly,
fired every `DragMove`, and moved nothing — so a test could assert dispatch happened while the
widget's own state never changed. Both handlers are public and only record `e.pos` before recursing,
so calling one directly reproduces the recursion without making `rackScroll` live.

Positions are converted into the rack's own space, so a widget comparing `getMousePos()` against a
`ModuleWidget` box (`SirenDropHandler`, Glue's `ModuleLabelWidget`, Maze and Hive) sees both in one
space. The press syncs *before* dispatch, since a handler capturing a drag origin reads the field
during `onButton`. Opt out with `trackRackMousePos = false`; inspect it with `rackMousePos()`.

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

### 7.1 What a new module must route through the layer

This is the part to get right while *writing* the module, not while writing its test: a call that
goes straight to `APP->` cannot be observed or faked later, and the mistake is invisible — the code
compiles, runs correctly in Rack, and its test silently exercises the real thing.

**Route these. Always.** A test cannot obtain them otherwise:

| In a new module, write | Not | Because a test has no |
|---|---|---|
| `vcv::fs::` — `read`/`write`/`exists`, `isFile`/`isDirectory`/`getEntries`/`getFileSize`, `rename`/`copy`/`remove`/`removeRecursively`, `createDirectory`/`createDirectories`, `getTempDirectory`/`getUserDirectory`/`openDirectory` | `fopen`/`fread`/`fwrite`, `rack::system::*` | filesystem it may touch |
| `vcv::fs::getTime()` | `system::getTime()` | clock it can control |
| `vcv::ui::message/openDialog/saveDialog/openDirectoryDialog` | `osdialog_*` | user to answer a dialog |
| `vcv::ui::getClipboard/setClipboard/openBrowser` | `glfwGetClipboardString`, `system::openBrowser` | clipboard or browser |
| `vcv::nw::requestJson/requestDownload` | `rack::network::*` | network |
| `vcv::history::push` | `APP->history->push` | undo stack it can inspect |
| `vcv::getModuleWidget/getModule/addModule/removeModule/applyPreset/toJson` | `APP->scene->rack->addModule(...)` etc. | patch to add modules to |
| `vcv::findCable/addCableToPort/removeCable/getCompleteCables/hasCable` | `APP->scene->rack->addCable(...)` etc. | cables |
| `vcv::scene::select/deselect/deselectAll/isSelected/getSelectedModuleIds` | `APP->scene->rack` selection | selection state |
| `vcv::ui::hasWindow()` | `APP->window != nullptr` | window (it is always null) |
| `vcv::engine::getFrame()` | `APP->engine->getFrame()` | way to advance the engine's own counter |

**Leave these raw.** A test already has the real thing, so a mock would only be a second
definition free to drift from Rack:

- **The rest of `APP->engine->`** — `getSampleRate()`, `getSampleTime()`, `getModule()`,
  `bypassModule()`, and every `ParamHandle` call. `Test::Harness` drives the *real* engine, so
  these already answer correctly; §5 and `Harness`'s mapping helpers cover them.
- **The widget tree** — `APP->scene->rack->...` beyond the module/cable operations above,
  `APP->scene->rackScroll->...`, `APP->event->...`. `Harness` gives a live laid-out scene and
  `EventDriver` dispatches through it (§6).
- **Drawing** — `APP->window->loadFont()`, `->uiFont`, `->vg`, `->pixelRatio`. Not testable at all
  (§6, *What is not testable*); `Window`/`NVGcontext` cannot be constructed headless.
- Logging macros (`INFO`/`WARN`), `rack::APP_NAME`/`APP_VERSION`, `asset::user(...)`.

Note `vcv::fs::` also wraps the pure path helpers — `join`, `getDirectory`, `getFilename`,
`getStem`, `getExtension` — even though they are just string manipulation. They are in the layer
so a mock can redirect a whole path root (`fs.hpp:34`), so prefer them over `system::` for
consistency within a module that already uses the layer.

**The rule, if the table does not cover your case:**

> Route it when a test **cannot otherwise obtain what the call returns**. Where a test can already
> have the real thing, use the real thing.

Wrapping the widget tree in a proxy is *possible* — it was prototyped, and a mock returning real
widgets satisfies even `dynamic_cast` plus follow-on calls — but it buys no testability and
creates a Rack type definition that can drift.

Two practical notes:

- `#include "../../vcv/api.hpp"` pulls in the whole layer in one line.
- **`EngineAccess` has no `TEST_MOCK_*` macro**, and should not be mocked by hand: `Test::Harness`
  installs its own for its lifetime, pointing `getFrame()` at the harness's DSP clock. Just use
  `vcv::engine::getFrame()` in the module and step the harness.

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

Seven macros, one per test-mockable slot (`EngineAccess` is the eighth interface but has no macro —
`Test::Harness` owns it). Each hardcodes its own member name, `Base` type and global — there is no
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

**`APP->scene->rack` is process-wide too, and production code adds to it.** Any code path that
adds a module the way a real click does — `Mb`'s `chooseModel()`, `Stroke`'s and `Mirror`'s
add-module actions, `vcv::addModule()` — calls `APP->scene->rack->addModule()` directly, which the
harness does not own. Left behind, it is worse than a leak: the next `TEST_CASE` doing the same
thing searches for a free grid position among modules nothing tore down, and Rack's
`eachNearestGridPos()`/`setModulePosNearest()` (`RackWidget.cpp`) **hangs** rather than fails when
it collides with a stale module at the same position. `Harness::sweepAddedModules()` handles this
automatically on teardown: it snapshots the rack's `ModuleWidget`s at construction and removes
anything added since that the harness does not own, so a widget test can drive a real
add-module click and simply not think about it. A fixture that parents a widget into the rack
itself and cleans it up in its own destructor (`EightFaceMk2.test.dispatch.hpp`'s
`DispatchFixture`) still works — its destructor runs before its `Harness` member's, so the sweep
finds nothing left to do.

**Catch2 runs all `TEST_CASE`s in one process.** Assert on *deltas*, not absolute counts, for
anything process-wide (scene children, registries) — an earlier case may have left something behind.

**A known pre-existing flake.** `Siren`'s background-task suite occasionally fails or aborts under
parallel load and passes in isolation. It is a real `TaskWorker`/teardown race, documented since
Phase 1, and not a regression from whatever you just changed. Confirm with
`make testrun-one NAME=<the failing binary>` before investigating. It is also the reason §5's
worker guidance says not to let a real worker thread run against test state: this is what it looks
like when one does.

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

- [ ] A `testPluginInit()` registering the models the suite needs (see `CONVERTING.md`)
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
| `test_context.hpp` | 357 | Context, module/widget lifetime |
| `test_json.hpp` | 299 | Preset fuzzers |
| `test_traversal.hpp` | 150 | Shared widget-tree walk |
| `test_events.hpp` | 442 | `EventDriver` |
| `test_harness.hpp` | 867 | `Harness`, `SceneLayout`, `UiMode` |
| `test_harness.test.cpp` | 912 | The harness's own tests |
| `test_events.test.cpp` | 704 | The driver's own tests |
| `catch_amalgamated.{hpp,cpp}` | 30k | Catch2 v3.12.0, compiled once and shared |

Not part of the framework, but needed when testing a module with background work (§5, workers):

| File | Role |
|---|---|
| `utils/TaskWorker.hpp` | `TaskWorker`, the `ITaskWorker` seam, and `SyncTaskWorker` |
| `utils/GuiTaskProcessor.hpp` | The GUI task queue `UiMode` selects the drain path of |

Framework changes belong with a test in `test_harness.test.cpp` or `test_events.test.cpp`. Both
exist because the framework is what every other test rests on, and a silent regression there is
worse than a module bug.
