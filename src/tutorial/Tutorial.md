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

	t.steps.push_back(Step(
		"Welcome",
		"A short introduction to what this module does."
	));

	t.steps.push_back(Step(
		"The matrix",
		"Each button assigns a port.",
		Target::params(MyModule::PARAM_MATRIX, MATRIX_COUNT)
	));

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

- `title`/`text` — plain strings, word-wrapped to the bubble width.
- `target` — see below. Omit it (or pass `Target::none()`) for a centered, untargeted step.
- `.prefer(Side::LEFT)` / `.withWidth(200.f)` — chainable overrides on the `Step` you just
  constructed, for the rare step that needs a non-default side or a wider bubble.
- `onEnter`/`onLeave` — fire exactly once per step change (including on close), useful for
  side effects like temporarily changing a module's display mode while its step is active.

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
  overlay is a normal child widget and cleans up the bubble in its destructor.

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
| `src/tutorial/Tutorial.hpp` | `Target`, `Step`, `Tutorial`, `start()`, `createTutorialMenuItem()`, `unresolvedSteps()` — the public API |
| `src/tutorial/TutorialOverlay.hpp` | `TutorialOverlay` (module child, dimming/cutout/events) and `TutorialBubble` (rack child, dialog widget) — included from `Tutorial.hpp`, never directly |
| `src/vcv/ui.hpp`/`.cpp` | `measureTextBox()`, the text-measurement seam the bubble uses to size itself |

Always `#include "tutorial/Tutorial.hpp"` — it pulls in `TutorialOverlay.hpp` at the point
where the latter's definitions need `Target`/`Step`/`Tutorial` to already exist.

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
