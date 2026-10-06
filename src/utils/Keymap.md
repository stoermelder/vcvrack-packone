# Keymap

A shared keyboard-mapping layer: a module registers its actions with default keys, the user can
rebind them in a file, and widgets attach behaviour to the actions. Used by Ahab and Mb.

Files: [Keymap.hpp](Keymap.hpp), [Keymap.cpp](Keymap.cpp), tests in [Keymap.test.cpp](Keymap.test.cpp).
Examples: [AhabKeymap.hpp](../modules/ahab/AhabKeymap.hpp), [MbKeymap.hpp](../modules/trial-and-error/MbKeymap.hpp).

GUI thread only. Never reachable from `process()`.

## The three pieces

| Piece | Lives | Holds |
| --- | --- | --- |
| `Keymap` | one per module slug, shared by every instance (`Keymaps::open("Mb")`) | the vocabulary: action ids, labels, contexts, default and current keys |
| `KeymapHandlers` | one per widget that reacts to keys | what each action does |
| `KeyCombo` | value type | one key plus modifiers, e.g. `Ctrl+Shift+Z` |

The split matters: the `Keymap` is process-wide, so it must never hold anything that captures a
widget. Behaviour goes into a `KeymapHandlers` owned by the widget, which is destroyed with it.

## Wiring a module

1. Register the vocabulary once, idempotently, in a header next to the module:

   ```cpp
   inline std::shared_ptr<Keymap> registerActions() {
       auto km = Keymaps::open("Mb");
       km->registerAction("browser.close", "Close browser", "Browser", "Escape", GLFW_REPEAT);
       km->registerAction("browser.clear.always", "Clear filters (any search)", "Browser"); // unbound
       km->save();
       return km;
   }
   ```

   Arguments: id, label, context, default key (omit for "unbound by default"), trigger
   (`GLFW_PRESS` by default, `GLFW_REPEAT` if holding the key should keep firing).
   `registerAlias(id, combo)` adds a second default key to an existing action.

2. In the widget, create the handlers and attach behaviour:

   ```cpp
   std::shared_ptr<Keymap> keymap = registerActions();
   KeymapHandlers handlers{keymap, {"Browser", "Navigation"}};

   Ctor() {
       handlers.on("browser.close", [this]{ overlay->hide(); });
   }

   void onSelectKey(const SelectKeyEvent& e) override {
       if (handlers.dispatch(e.key, e.mods, e.action)) { e.consume(this); return; }
       TextField::onSelectKey(e);
   }
   ```

3. Show bindings in menus with `keymap->shortcutText(id)`, never a hardcoded string.

## Action ids and contexts

* An **id** is what the file and the handlers refer to. It is permanent once released: renaming
  it orphans the user's binding. Dotted, lower case, e.g. `browser.v2.nav.up`. If an action only
  exists in one UI variant, say so in the id (`v2`): the file shows nothing but ids and comments.
* A **context** is a label ("Browser", "Navigation", "Module", "Side view"). It appears in the
  comment above each binding in the file, and it can restrict lookups (see below).
* Give an action that behaves differently with and without a precondition **two ids**, e.g.
  `browser.clear` (only with an empty search) and `browser.clear.always` (unbound by default). The
  precondition lives in the handler, not in the key.

## dispatch(): how a key finds its handler

`dispatch(key, mods, action, keyName)` does two things in order. `keyName` is optional: pass the
event's `e.keyName` and letter bindings (A-Z) follow the key *labelled* so on the current layout
(Ctrl+Z is the "Z" key on QWERTZ, not the US-position one), and a key labelled with punctuation
never matches a letter (the ";" key at the US-Z position on Dvorak is not Z). Only without it, or
when it is multi-byte (a non-Latin layout), matching falls back to the physical GLFW key code.
Non-letters always match by key code, except `+`: GLFW has no key code for it, so a `+` binding
matches the numpad plus, a key labelled "+" (QWERTZ, Nordic) and, on US layouts, Shift+= (the
Shift is implied, so `Ctrl++` fires on Ctrl+Shift+=). Use `KPAdd` for the numpad key alone.

1. **Resolve the key to one action id** with `Keymap::lookup()`. Only actions in the handlers'
   `contexts` are considered (all if none were given). The **first registered** match wins.
   `GLFW_RELEASE` and Rack's `RACK_HELD` never match, and `GLFW_REPEAT` only matches actions
   registered with `GLFW_REPEAT`.
2. **Run a handler for that id.** Handlers guarded by a scope run first, in registration order;
   unscoped handlers are the fallback. The first handler that returns true stops the walk.

It returns true if a handler ran (or an exclusive scope swallowed the key). The caller then
consumes the event, otherwise it passes the key on, typically to the base class.

`on(id, fn)` always handles. `onTry(id, fn)` may decline by returning false, which lets the next
handler try, and finally the caller. Use `onTry` for "only when the search is empty" style guards.

## Scopes and contexts: which one?

They answer different questions, and they work at different steps of `dispatch()`.

| | Scope | Context filter |
| --- | --- | --- |
| Question | "Given this action, which handler runs *right now*?" | "Which actions can this widget be reached through *at all*?" |
| Acts at | step 2, choosing among handlers of **one id** | step 1, choosing which **ids** a key can resolve to |
| Decided by | a predicate evaluated per key press | fixed when the `KeymapHandlers` is created |
| Set up with | `handlers.scope(pred)` | `KeymapHandlers{keymap, {"Browser", ...}}` |

### Use a scope when the same action means something different depending on state

The action and the key are the same, only the current situation changes. Example: in the Mb
search field, `browser.v2.layout.brand` (Ctrl+1) opens the Brand dropdown, but when a dropdown is
already open the same key closes it or switches to another one. Two handlers, one id:

```cpp
handlers.on(id, [this]{ openDropdown(); });                  // unscoped: the fallback
auto whenOpen = handlers.scope([this]{ return dropdownOpen(); });
whenOpen.on(id, [this]{ closeOrSwitch(); });                 // scoped: runs first while open
```

Also right for "this whole set of handlers is only live while X":

```cpp
auto browse = handlers.scope([this]{ return !dropdownOpen(); });
browse.on("browser.close", ...);   // suspended while a dropdown owns the keyboard
```

Scoped handlers always beat unscoped ones, whatever the registration order, so a scope can
override a default without the two having to be registered in the right order.

**Exclusive scopes** (`scope(pred, /*exclusive=*/true)`) go further: while the predicate holds, the
scope owns the keyboard. Keys bound to other actions never fall through to unscoped handlers, and
even an unbound key is reported as handled. Use it for a modal picker that must swallow
everything. Do **not** use it when the scope has to let some keys through to a child, such as
arrows and Enter for a dropdown: those are not keymap actions, so an exclusive scope would swallow
them.

A predicate cannot see the key. It answers "is this handler eligible now", not "is it this key".

### Use a context filter when two actions share a key in different contexts

Two *different actions* legitimately default to the same key, and each belongs to a different
widget. Example in Mb: `modelbox.favorite.toggle` (a hovered module box) and
`browser.v2.sideview.focus` (the side-view catcher) are both Ctrl+F.

Without a filter, `lookup()` reports only the first one registered, so the other could never be
reached, and its behaviour would depend on registration order. With contexts, each widget only
looks among its own actions:

```cpp
KeymapHandlers box{keymap, {"Module"}};        // module box:      Ctrl+F -> modelbox.favorite.toggle
KeymapHandlers catcher{keymap, {"Side view"}}; // side-view catcher: Ctrl+F -> browser.v2.sideview.focus
```

Give the contexts a meaning per *widget*, not per topic: every action a widget's handlers use must
be in a context that widget lists. Plain lookups (`keymap->lookup(key, mods, action)`) without
contexts still see everything and are first-match-wins.

Contexts are also how the file is documented (they appear in the comments), so keep them
user-facing: "Navigation", not "SearchFieldStuff".

### They combine, and neither fixes event routing

A context filter stops two actions shadowing each other; a scope chooses between handlers. Neither
decides **which widget receives the key first**, since that is Rack's event routing (widget tree,
hover position, selected widget). When two widgets can both see a key, resolve it in the widgets:
in Mb, the side-view catcher is the topmost scene child and would see Ctrl+F before a module box
under the cursor, so its focus handler sits in a scope `!overModelBox()`.

### Quick decision guide

| Situation | Use |
| --- | --- |
| One action, behaviour depends on UI state | scope |
| A set of handlers must pause while something is open | scope |
| A modal state must swallow every key | exclusive scope |
| Same key, different actions, different widgets | context filter |
| One action, only valid under a data precondition (empty text) | `onTry` returning false |
| Same behaviour without the precondition, on request | a second, unbound action id |
| A widget must react to a key before another widget gets it | widget event routing, not the keymap |

## The file

`<Rack user folder>/Stoermelder-P1/keymaps/<slug>.jsonc`, created on first use. It is JSON with
`//` line comments (jansson can't parse them, so `stripLineComments()` removes them before
parsing):

```jsonc
{
  "slug": "Mb",
  "version": 1,
  "bindings": {
    // Close browser (Browser)
    "browser.close": "Escape",

    // Hidden toggle (Browser)
    "browser.hidden.toggle": ["Shift+Space", "Ctrl+Space"],

    // Clear filters (any search) (Browser)
    "browser.clear.always": null
  }
}
```

* A binding is a string, an array of strings (several keys), or `null` (unbound).
* Key names: `Ctrl`, `Shift`, `Alt` in any order and case; `Cmd`/`Command`/`Super` are read as
  `Ctrl`. Files always use `Ctrl` so they stay portable. Menus show the platform's name.
* The comment above each binding (`label (context)`) is written by `save()` and regenerated on every
  write. User-written comments do not survive a save.
* Actions missing from the file take their default and cause a rewrite. Ids in the file that no
  module registers are kept and written back untouched, so a downgrade doesn't lose bindings.
* A file that fails to parse is never overwritten: defaults stay in memory, the file stays
  recoverable by hand.
* There is no in-app rebinding menu. `Keymaps::reload(slug)` re-reads the file.

## Testing

* `Keymaps::resetForTest()` drops the process-wide registry. Call it at the start and end of any
  test that rebinds, or the binding leaks into the next test case.
* Tests install a mock filesystem, so nothing touches the real user folder.
* Send keys to the widget that owns the handlers rather than through `EventDriver::key()` when the
  key might go unconsumed: it would fall through to Rack's `Scene::onHoverKey`, which
  dereferences `APP->window` (null headless).
