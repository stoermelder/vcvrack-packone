# Module tutorial framework

A small, reusable UI framework for step-by-step, in-panel module tutorials. A tutorial is a
list of steps; each step shows a dialog ("bubble") with a title, body text, and Back / Next /
Close controls. A step may name one widget on the module to highlight — the module panel dims
except for a cutout around it, and the bubble points at it with an arrow. A step without a
target dims the whole panel and centers the bubble on it.

The tutorial belongs to the module: it lives as long as the `ModuleWidget` that opened it, and
each module instance has its own, independent of any other. It scales and moves with the rack
like the panel does.

Pilot integration: **SPLICE-KIT** (`src/modules/splicekit/SpliceKit.tutorial.hpp`).

## Quick start

```cpp
#include "../../tutorial/Tutorial.hpp"

using namespace StoermelderPackOne::Tutorial;

inline Tutorial myModuleTutorial() {
	Tutorial t;
	t.title = "MY MODULE";

	addStep(t, "Welcome", "A short introduction to what this module does.");

	addStep(t, "The matrix", "Each button assigns a port.",
		Target::params(MyModule::PARAM_MATRIX, MATRIX_COUNT));

	return t;
}
```

Add a context menu entry in the module widget:

```cpp
void appendContextMenu(Menu* menu) override {
	menu->addChild(new MenuSeparator);
	menu->addChild(createTutorialMenuItem(this, myModuleTutorial));
	...
}
```

`createTutorialMenuItem` calls the factory fresh each time the entry is clicked (not once up
front), so a tutorial that reads live module state stays current. `start(mw, tutorial)` is the
lower-level entry point if you need to open a tutorial from somewhere other than a menu item;
calling it again on the same `ModuleWidget` replaces whatever tutorial is already open.

## Writing steps

```cpp
struct Step {
	std::string title;
	std::string text;
	Target target;                 // Target::none() -> centered on the module
	Side side = Side::AUTO;        // placement preference for this step only
	float width = 0.f;             // bubble width override, module px; 0 = Style default
	std::function<void()> onEnter; // optional
	std::function<void()> onLeave;
};
```

- `title`/`text` — plain strings, word-wrapped to the bubble width. `text` may contain literal
  `\n`/`\n\n` for a manual line break or paragraph gap; the bubble measures and draws it the
  same way nanovg's own `nvgTextBox` would.
- `target` — see below. Omit it (or pass `Target::none()`) for a centered, untargeted step.
- `.prefer(Side::LEFT)` / `.withWidth(200.f)` — chainable overrides on the `Step` you just
  constructed, for the rare step that needs a non-default side or a wider bubble.
- `onEnter`/`onLeave` — fire exactly once per step change (including on close), useful for
  side effects like temporarily changing a module's display mode, or driving a real
  gesture (see "Live demos" below) while its step is active.

### `addStep()`: building a step without the boilerplate

```cpp
inline void addStep(Tutorial& t, std::string title, std::string text, Target target = Target::none(),
	std::function<void(Step&)> configure = nullptr);
```

Builds a `Step` and appends it to `t`, collapsing the
`{ Step step(title, text, target); step.onEnter = ...; t.steps.push_back(std::move(step)); }`
pattern a step otherwise needs even just to set `onEnter`. A step with no extra configuration
(just title/text/target, like `"Welcome"` in the quick-start example above) can omit `configure`
entirely; one that needs `onEnter`/`onLeave`/`.prefer()`/`.withWidth()` sets them inside it:

```cpp
addStep(t, "Assign a port", "...", Target::param(PARAM_MATRIX + 0), [mw](Step& step) {
	step.onEnter = [mw]() { module->enablePortLearn(0, mw); };
	step.onLeave = [mw]() { module->disablePortLearn(); };
});
```

Anything captured by value in `configure`'s own lambda (like `preview` in the menu-preview
example under "Live demos" below, a `shared_ptr` that needs to be visible to both `onEnter` and
`onLeave`) is declared inside `configure` itself, not passed in separately — `configure` runs
once, immediately, during `addStep()`, so a local declared at its top is already in scope for
both closures it goes on to build.

### Whole-tutorial lifecycle: `onOpen`/`onClose`

```cpp
struct Tutorial {
	std::string title;
	std::vector<Step> steps;
	std::function<void()> onOpen;  // optional
	std::function<void()> onClose;
};
```

Unlike a `Step`'s `onEnter`/`onLeave`, these fire exactly once each for the tutorial as a
whole — `onOpen` right before the first step's own `onEnter`, `onClose` after the last-active
step's `onLeave`, regardless of which step the tutorial happened to be on when it closed (Esc,
clicking past the last step, the module being deleted, or `start()` replacing an already-open
tutorial with a new one all count as closing). Use them to bracket state that spans every step,
most commonly a snapshot/restore pair so nothing any step's `onEnter` does survives the
tutorial — see `withModuleSnapshot()` below.

### `withModuleSnapshot()`: snapshot/restore for any module

```cpp
inline void withModuleSnapshot(app::ModuleWidget* mw, Tutorial& tutorial, bool resetOnOpen = false);
```

Wires `onOpen`/`onClose` to snapshot the module's JSON on open and restore it on close, via
`engine::Module`'s own generic `toJson()`/`fromJson()` (which call the module's `dataToJson()`/
`dataFromJson()` internally) — every module already has this, so a tutorial gets a full
snapshot/restore for free rather than hand-writing one. Every step is then free to call real,
mutating module functions; nothing has to be undone step-by-step, because the whole module
reverts the moment the tutorial closes, from whichever step it was on. Call it last, after
setting up the tutorial's steps and any `onOpen`/`onClose` of your own — it composes with
whatever is already there rather than replacing it:

```cpp
inline Tutorial myModuleTutorial(app::ModuleWidget* mw) {
	Tutorial t;
	t.title = "MY MODULE";
	t.steps.push_back(/* ... */);
	withModuleSnapshot(mw, t, /* resetOnOpen */ true);
	return t;
}
```

`resetOnOpen` fires the module's `onReset()` (the same "Initialize" the context menu uses)
right after snapshotting, so every step starts from the same defined state regardless of what
the user had already set up.

`toJson()`/`fromJson()` only restore module-internal state (params, `dataToJson()`'s own
fields) — cables and other patch-level state aren't part of a module's own JSON. A module whose
tutorial changes live cables (or anything else `dataToJson()` doesn't cover) needs its own extra
fixup that runs against the just-restored state; set it as `tutorial.onClose` *before* calling
`withModuleSnapshot()` and it runs after the restore. SPLICE-KIT
(`src/modules/splicekit/SpliceKit.tutorial.hpp`) shows the pattern — its `reconcileAfterRestore()`
captures what's live now (the demo's stray cables) and diffs it against the connection bitmask
`fromJson()` just restored, fixing up exactly the strays.

### Live demos

A step doesn't have to just describe a gesture — `onEnter`/`onLeave` can drive the module's own
public functions for real, so the step shows the gesture instead of only narrating it. SPLICE-KIT
does this throughout:
- **A real, waiting gesture.** The "Assign a port" step calls the module's own
  `enablePortLearn()` — the exact function a real right-click → Learn uses — so the module is
  genuinely waiting for the user to click a port. Nothing is faked; learn mode alone changes
  nothing until the user acts.
- **An illustrated click or key-press gesture.** All three of "Patch", "Scenes", and "Port map
  view" use `CursorDemoWidget` (the framework's own reusable piece for this — see below), and the
  first two both spawn a throwaway VCV Fundamental "8vert" right next to the module
  (`vcv::addModule()` — see "Spawning a throwaway module" below), removed again on `onLeave`:
  "Patch" assigns two free cells to its first input/output pair via `assignPort()`, then shows a
  fake cursor (never the real one) walking between them and clicking each in turn, with
  `patchClick()` (this file) firing on each click to arm/toggle the connection the same way the
  module's own `triggerCell()` would; "Scenes" does the same assignment, connects the pair on the
  scene the module was already on (so the neighbouring scene it cycles to has a real, visible
  difference instead of switching between two empty scenes), then cycles the cursor between the
  two scene buttons, calling `sceneStore.switchTo()` on each click; "Port map view" uses a
  keyLabel stop to pop up a "SPACE" key cap instead of a click, illustrating the keyboard
  shortcut it describes.
- **The real context menu, previewed.** The "Context menu" and "Cell menu" steps show the
  module's actual menus via `openMenuPreview()` — see below.

Keep demo-only structs (illustrative overlays, snapshot wrappers) in the tutorial file itself
rather than adding them to the module — the tutorial is meant to observe and drive the module
through its existing public surface, not grow new surface of its own. Reusable pieces (the
menu-preview machinery, the whole-tutorial snapshot) belong in the framework instead, once more
than one module would otherwise duplicate them — see `TutorialMenuPreview.hpp` and
`withModuleSnapshot()` above.

### Spawning a throwaway module

A step demoing a cable needs a real second module with a real port to patch against. Don't reuse
whatever happens to already be in the patch (it might not exist, or — worse — it might be a
module the user is actively working on, and rewiring it mid-tutorial is a real, saveable, visible
change to their patch) and don't add a plugin-internal module just for this. Instead spawn one
through the same `ModuleAccess` seam the rest of the codebase uses for module lifecycle
(`src/vcv/modules.hpp`), and remove it again on `onLeave`:

```cpp
int64_t demoModuleId = vcv::addModule({"Fundamental", "8vert"}, mw->box.pos.plus(Vec(mw->box.size.x, 0.f)));
// ... use vcv::getModuleWidget(demoModuleId)->getPorts() to find a port to assignPort()/patch to ...
vcv::removeModule(demoModuleId); // onLeave
```

VCV's own Fundamental plugin ships with Rack by default, so `Fundamental`/`8vert` (8 independent
in/out pairs — index 0 always gives a real input and output) is available in essentially every
install without adding a dependency of this plugin's own. `addModule()` returns `-1` if the model
isn't found (Fundamental genuinely missing, or — the case that matters for this plugin's own test
suite, which only loads its own dylib — no other plugin loaded at all); treat that the same as
every other "the patch doesn't have what the demo needs" no-op elsewhere in this file, rather than
asserting or crashing. `addModule()` positions the new module with `setModulePosForce()`, so it
pushes whatever's already there aside instead of overlapping it, same as dragging a module in
from the browser would.

### Menu previews

```cpp
struct MenuPreview {
	widget::Widget* host; // owns the menu + highlight + blocker; child of APP->scene
	ui::Menu* menu;
};
inline MenuPreview openMenuPreview(app::ModuleWidget* mw, std::function<void()> build,
	std::function<std::vector<widget::Widget*>(ui::Menu*)> pickItems = nullptr);
inline void closeMenuPreview(MenuPreview& preview);
inline widget::Widget* findMenuItemByLabel(ui::Menu* menu, const std::string& label);
inline std::vector<widget::Widget*> findMenuItemsByLabel(ui::Menu* menu, const std::vector<std::string>& labels);
```

Shows a module's real menu — built by the module's own real menu code, so there's nothing to
hand-duplicate — as a read-only illustration next to `mw`, instead of actually opening it as a
live, clickable menu:

```cpp
step.onEnter = [mw, preview]() {
	*preview = openMenuPreview(mw, [mw]() { mw->createContextMenu(); });
};
step.onLeave = [preview]() { closeMenuPreview(*preview); };
```

`build` is anything that ends by opening a real menu via `createMenu()`, directly or
transitively — `mw->createContextMenu()`, or a module's own per-item menu function (SPLICE-KIT's
cell menu, extracted into a free function precisely so both the real right-click handler and its
tutorial preview call the same code — see `openSpliceKitCellMenu()` in `SpliceKit.cpp`).

Pass `pickItems` to highlight one or more entries instead of the whole menu — a single ring is
drawn around the union of every listed entry's row (they don't need to be adjacent; the ring
just spans the gap between them), not one ring per item. It's called with the already-built,
already-repositioned `ui::Menu*`, so it can use `findMenuItemByLabel()`/`findMenuItemsByLabel()`
for the common case (exact match against a real `MenuItem`'s `text`) or walk `menu->children`
directly for anything more specific (a submenu item, a checkbox found by its checked state).
Returning an empty vector (nothing found) falls back to highlighting the whole menu.

`createMenu()` (what every Rack menu is built on) wraps the `Menu` in a `ui::MenuOverlay`: a
modal widget that captures every click and every key anywhere on screen while it's open, and
self-deletes on the first one. That's correct for a real menu, but wrong for a preview — while a
`MenuOverlay` is open, the tutorial's own Back/Next/Esc are completely unreachable (`MenuOverlay`
is a child of `APP->scene` added after everything else, and events are dispatched to the topmost
widget first), so an earlier design that kept the real `MenuOverlay` and only blocked clicks
reaching its items needed the user's first click or Esc to close the menu and only the second to
actually navigate. `openMenuPreview()` instead detaches the `Menu` from its `MenuOverlay` right
after `build()` returns, deletes the `MenuOverlay`, and re-hosts the `Menu` under a plain,
non-modal container (`MenuPreviewHost`) added directly to `APP->scene` — so clicks/keys that
miss the menu's own (inert) area pass straight through to the tutorial bubble underneath.

The highlight ring reuses `HighlightStyle` (same file as `Target`/`Step`/`Tutorial`) — the same
accent color/pulse/corner-radius `TutorialOverlay`'s own module-panel ring uses, so a menu
preview reads as part of the same tutorial. `HighlightStyle`'s numbers are module px, meant for
drawing inside the rack's zoomed coordinate space (like `TutorialOverlay`'s ring, a child of the
module panel); a menu preview draws under `APP->scene` instead — true screen px, no ambient
zoom — so it scales them by `mw->getAbsoluteZoom()` itself before drawing, or the ring would
read thinner or thicker than the panel's own depending on how far the rack is zoomed.

Lives in its own file, `TutorialMenuPreview.hpp` (included from `Tutorial.hpp`, same pattern as
`TutorialOverlay.hpp` — never include it directly) since it's a large, self-contained piece not
every tutorial needs to read to understand `Target`/`Step`/`Tutorial` themselves.

### Cursor demos

```cpp
struct CursorDemoStop {
	std::function<math::Vec(app::ModuleWidget*)> target; // module-local coordinates, resolved fresh each frame
	std::function<void()> onClick; // optional; fires once, when the cursor "clicks" (or key "presses") here
	std::string keyLabel; // non-empty -> illustrate a keyboard shortcut instead of a click
};
inline CursorDemoStop cursorDemoParamStop(int paramId, std::function<void()> onClick = nullptr);
inline CursorDemoStop cursorDemoKeyStop(std::string keyLabel, std::function<void()> onClick = nullptr);

struct CursorDemoWidget : widget::TransparentWidget {
	app::ModuleWidget* mw;
	std::vector<CursorDemoStop> stops;
};
```

An illustrative fake cursor (never the real one) that walks between a fixed sequence of points
on the panel, pausing on each with a click-ripple animation, looping back to the first stop
after the last. Use it for a step that wants to show "click A, then click B" (or a longer
sequence) as an animated gesture — SPLICE-KIT's "Patch" and "Scenes" steps are the pilot:

```cpp
step.onEnter = [mw]() {
	auto* module = dynamic_cast<MyModule*>(mw->module);
	auto* cursor = addStepWidget<CursorDemoWidget>(mw); // module-local addStepWidget()/removeStepWidget() helper
	cursor->mw = mw;
	cursor->stops = {
		cursorDemoParamStop(PARAM_A),
		cursorDemoParamStop(PARAM_B, [module]() { module->toggleConnection(A, B); }),
	};
};
step.onLeave = [mw]() { removeStepWidget<CursorDemoWidget>(mw); };
```

Each stop's `target` is resolved fresh every frame (not cached), so it keeps working across a
panel relayout, same as `Target`'s own resolvers; `cursorDemoParamStop()` covers the common case
of a `ParamWidget`'s own center via `mw->getParam(paramId)`. `onClick`, if given, fires once, at
the moment the animation "clicks" that stop — the right place to drive whatever real module
function the click is illustrating (`toggleConnection()`, `switchTo()`, …), the same way a menu
preview's `build` drives the module's real menu code.

A single-stop sequence still animates (click, hold, repeat) rather than sitting static, so it's
also usable for "here's the one thing to click" with no second point; an empty `stops` draws
nothing.

A stop can illustrate a keyboard shortcut instead of a click: set `keyLabel` (or use
`cursorDemoKeyStop()`, the equivalent of `cursorDemoParamStop()` for this case — anchored to the
panel's center, the usual spot for a global/hover shortcut with no one control it "belongs" to)
and the widget draws a popping-in key cap there instead of the cursor dot/ripple; its MOVE phase
collapses to instant, since a keyboard shortcut has no "walking there" the way a click does.
`onClick` still fires at the same point in the cycle, now meaning "the key was pressed" — SPLICE-
KIT's "Port map view" step ([SPACE] to toggle the port-map overlay) is the pilot:

```cpp
cursor->stops = { cursorDemoKeyStop("SPACE") };
```

A sequence can mix click stops and key-press stops freely (walk to a button, click it, then show
the key that does the same thing) — they're two ways to illustrate the same "something happens at
this stop" cycle, not two different widgets.

`addStepWidget<W>()`/`removeStepWidget<W>()` — add a widget as the last child before `mw`'s
tutorial overlay (so its `drawLayer(1)` content sits under the overlay's dim/cutout paint) and
find-and-remove it again — are SPLICE-KIT-local helpers today (`SpliceKit.tutorial.hpp`), not yet
promoted into the framework; do that once a second module needs them (`CursorDemoWidget` itself
doesn't require them — any child of `mw` painting on `drawLayer(1)` works, they're just the
established way to add/remove one for the duration of a step).

Lives in its own file, `TutorialCursorDemo.hpp` (included from `Tutorial.hpp` — never include it
directly), same pattern as `TutorialMenuPreview.hpp`.

### Targets

A `Target` is a resolver run fresh on every frame — no widget pointer is cached, so it keeps
working across theme rebuilds, and it's safe to write a tutorial as a plain, module-agnostic
factory function (see the SPLICE-KIT pilot) rather than one bound to a specific
`ModuleWidget*`.

| Constructor | Resolves via |
|---|---|
| `Target::none()` | nothing — centered step |
| `Target::param(int id)` | `mw->getParam(id)` |
| `Target::params(int firstId, int count)` | union of `mw->getParam(firstId … firstId+count−1)` — button rows, matrices |
| `Target::input(int id)` / `Target::output(int id)` | `mw->getInput(id)` / `mw->getOutput(id)` |
| `Target::light(int firstLightId)` | first `app::ModuleLightWidget` descendant with that `firstLightId` |
| `Target::widget<W>()` | first descendant of type `W` (depth-first) |
| `Target::widget(std::function<widget::Widget*(app::ModuleWidget*)>)` | arbitrary lookup |
| `Target::rect(math::Rect mm)` | a panel rect in millimetres, for printed areas with no widget |

If a target fails to resolve (e.g. a bad param id), the step falls back to centered and logs a
`WARN` once. Call `unresolvedSteps(mw, tutorial)` in your module's own tests to catch a typo in
a target id before it ships — it returns the indices of every step whose target didn't
resolve on a real (or test) `ModuleWidget`.

## Behavior

- **Blocking, not interactive** (v1): the panel is inert while a tutorial is open. Left clicks
  on the panel don't reach its controls; the module can still be dragged and its context menu
  still opens on right-click. Hover-scroll still scrolls/zooms the rack. ←/→/Enter/Esc
  navigate; other keys pass through to Rack.
- Cables and plugs still draw over the dimmed panel — the bubble draws above them (and above
  every module's own LEDs), so it's always readable regardless of what's underneath.
- The bubble may extend past the module's edges (e.g. a narrow module with a wide bubble) and
  stays clickable there.
- Moving the module, scrolling, or zooming the rack keeps the bubble correctly placed; the
  chosen side has hysteresis so it doesn't flip during a scroll.
- Deleting the module, undo/redo, and closing a patch with a tutorial open are all safe — the
  overlay is a normal child widget and cleans up the bubble in its destructor. The destructor
  does **not** fire the active step's `onLeave` or the tutorial's `onClose` on its own — see
  "onLeave/onClose and teardown" below for why, and what that means if you write a caller that
  deletes an overlay directly instead of through `close()`.

### `onLeave`/`onClose` and teardown

`fireLeave()`/`fireClose()` (what actually invokes a step's `onLeave` and the tutorial's
`onClose`) only ever run from `close()`, `next()`/`back()` (for the step being left), and
`forceClose()` — never automatically from `~TutorialOverlay()`. This matters if you're writing
code that deletes an already-open `TutorialOverlay` directly rather than through `close()` (the
built-in case is `start()` replacing a tutorial that's already open on `mw`): call
`overlay->forceClose()` yourself, before `removeChild()`/`delete`, or the old tutorial's
`onLeave`/`onClose` won't run at all.

This was an unconditional destructor call earlier, specifically to cover that `start()` case —
but it turned out unsafe for a different, more common teardown path: closing a patch or removing
the module deletes `mw` via `ModuleWidget::~ModuleWidget()` → `clearChildren()`, which iterates
`mw`'s children and deletes each one in place, `TutorialOverlay` included. A step whose `onLeave`
mutates `mw`'s own children — the common shape for a step that adds a temporary demo widget and
removes it again on the way out — would then reenter that same in-progress iteration from inside
the destructor, corrupting it. Since `mw` itself is being destroyed in that case, there's nothing
useful to restore anyway (the module and everything a step's `onEnter` touched on it go away
together), so the fix is simply: the destructor never fires anything, and any caller that
deletes an overlay outside of `close()` — while `mw` is otherwise still fully intact — is
responsible for calling `forceClose()` first if it wants that tutorial's cleanup to run.

## Placement

For a targeted step, the bubble tries RIGHT, then falls back through LEFT/BOTTOM/TOP (or the
step's preferred side first) until it finds one that fits the visible rack viewport without
overlapping the target. If nothing fits (e.g. zoomed in so far the target fills the screen),
it falls back to the corner of the viewport farthest from the target, with no arrow. This
logic is pure (`src/tutorial/TutorialPlacement.hpp`, no VCV Rack dependency) and unit-tested
on its own.

Sizing/spacing constants (bubble width, fonts, padding, arrow size, …) live in one `Style`
struct in the same header — tune there rather than hardcoding values elsewhere.

## Files

| File | Content |
|---|---|
| `src/tutorial/TutorialPlacement.hpp` | `Side`, `Style`, `PlacementInput`/`Placement`, `place()` — pure, only `math::` |
| `src/tutorial/Tutorial.hpp` | `Target`, `Step`, `Tutorial`, `HighlightStyle`, `start()`, `createTutorialMenuItem()`, `withModuleSnapshot()`, `unresolvedSteps()` — the public API |
| `src/tutorial/TutorialOverlay.hpp` | `TutorialOverlay` (module child, dimming/cutout/events) and `TutorialBubble` (rack child, dialog widget) — included from `Tutorial.hpp`, never directly |
| `src/tutorial/TutorialMenuPreview.hpp` | `MenuPreview`, `openMenuPreview()`, `closeMenuPreview()`, `findMenuItemByLabel()`/`findMenuItemsByLabel()` — included from `Tutorial.hpp`, never directly |
| `src/tutorial/TutorialCursorDemo.hpp` | `CursorDemoStop`, `cursorDemoParamStop()`, `cursorDemoKeyStop()`, `CursorDemoWidget` — included from `Tutorial.hpp`, never directly |
| `src/vcv/ui.hpp`/`.cpp` | `measureTextBox()`, the text-measurement seam the bubble uses to size itself |

Always `#include "tutorial/Tutorial.hpp"` — it pulls in `TutorialOverlay.hpp`,
`TutorialMenuPreview.hpp`, and `TutorialCursorDemo.hpp` at the points where their definitions
need `Target`/`Step`/`Tutorial`/`HighlightStyle` to already exist.

Header-only, same pattern as `src/ui/InfoWindow.hpp`.

## METAMODULE

The whole framework compiles out under `METAMODULE`. `createTutorialMenuItem` degrades to a
no-op stub (returns `nullptr`) so call sites don't need `#ifndef` guards of their own.

## Not yet implemented

- **First-load / "show once" tutorials.** The plan for this framework includes a
  `startOnFirstLoad(mw, key, isNewModule, factory)` entry point plus a persisted
  `pluginSettings.tutorialsSeen` set, so a tutorial can open automatically the first time a
  module is added to a patch (with a "don't show again" option), the way SPLICE-KIT's info
  dialog does today via `hasDataLoaded`. This is designed but not built yet — for now, every
  tutorial is opened explicitly from its context menu entry.
- Interactive steps, multiple simultaneously highlighted widgets, and step transitions are
  out of scope for v1 and not planned unless a module needs them.

See `var/Tutorial_framework_plan.md` for the full design rationale (how Rack's layer/event
model constrains the widget hierarchy, the placement algorithm's decision order, etc.) if
you're extending the framework itself rather than just writing a tutorial with it.
