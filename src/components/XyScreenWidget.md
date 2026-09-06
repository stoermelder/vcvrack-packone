# XyScreenWidget: adding an XY screen to a new module

`XyScreenWidget.hpp` provides an XY pad display with two kinds of draggable
points:

- **Nodes** — static targets with position, radius, and amount. Arena's IN
  ports and TransitPad's snapshots are nodes.
- **Cursors** — the driven point(s) whose position comes from the module
  (CV, a motion sequence, a ParamHandle, or a plain param), not from stored
  state. Arena's MIX ports and TransitPad's Out point are cursors.

A screen always has exactly one node collection and one cursor collection.
If a module only needs one kind of point, that's fine — give the cursor
collection a count of 0, or (for a node-only screen) just don't add cursor
widgets.

This file walks through wiring up a new module `MyModule<COUNT>`, mirroring
how `Arena.cpp` and `TransitPad.cpp` do it. Read those two side by side with
this guide — they're the two real, working consumers, and TransitPad is the
simpler one (a single cursor) if you want a smaller example to start from.

## 1. Inherit the two bases

```cpp
template <uint8_t COUNT>
struct MyModule : Module, XyScreenModule<COUNT>, XyScreenCursor {
	typedef XyScreenModule<COUNT> Sc;   // convenience alias, used everywhere below
	...
};
```

`XyScreenModule<COUNT>` owns node storage (an `XyScreenNodes<COUNT> nodes`
member) and the node-side `sc*` API. `XyScreenCursor` is a pure interface —
you provide the storage. `COUNT` is your node count; there's no template
parameter for the cursor count, since `XyScreenCursor::cursorCount()` is a
virtual, not a compile-time constant.

## 2. Node params and accessors

Declare one param pair per node (`X_POS`/`Y_POS`), `configParam`'d with
`XyScreenParamQuantity` (not plain `ParamQuantity` — this is what lets a
`XyScreenDummyMapButton` detect whether the param has a CV-map handle
attached, so the module can prefer a live CV input over its own smoothing):

```cpp
enum ParamIds {
	ENUMS(NODE_X_POS, COUNT),
	ENUMS(NODE_Y_POS, COUNT),
	...
};

configParam<XyScreenParamQuantity>(NODE_X_POS + i, 0.f, 1.f, defaultX, "Node i x-pos");
configParam<XyScreenParamQuantity>(NODE_Y_POS + i, 0.f, 1.f, defaultY, "Node i y-pos");
```

Then implement the two accessors `XyScreenModule` needs to reach them
(`XyScreenNodes` can't resolve these itself — see the header's own comment
on why):

```cpp
engine::ParamQuantity* getNodePqX(uint8_t id) override { return paramQuantities[NODE_X_POS + id]; }
engine::ParamQuantity* getNodePqY(uint8_t id) override { return paramQuantities[NODE_Y_POS + id]; }
```

Radius and amount are already fully stored by `XyScreenNodes` — no params
needed, no accessors to implement, unless you want non-default behavior:

```cpp
float getNodeRadiusDefault(uint8_t id) override { return 1.f; }  // default 0.5f
float getNodeAmountDefault(uint8_t id) override { return 1.f; }  // default 1.f (this is already the base default)
```

If your node count can be smaller than `COUNT` at runtime (a "number of
nodes used" setting), override `nodeCountActive()`:

```cpp
uint8_t nodeCountActive() override { return nodesUsed; }
```

## 3. Cursor storage and accessors

You own cursor storage yourself — a plain param pair (single cursor) or an
array (multiple cursors), plus whatever UI-shadow/filter state your drag and
CV smoothing need:

```cpp
enum ParamIds {
	...
	CURSOR_X_POS, CURSOR_Y_POS,   // or ENUMS(..., CURSOR_COUNT) for several
};

float cursorUiX, cursorInX;
dsp::ExponentialFilter cursorXfilter;
// ...and the Y equivalents
```

Implement the interface:

```cpp
uint8_t cursorCount() const override { return 1; }               // or CURSOR_COUNT
uint8_t cursorCountActive() const override { return cursorCount(); }  // override if it varies at runtime

// The param-backed position — what process() actually drives (CV, a
// motion sequence, a ParamHandle) and what the widget draws. This must
// read the param, not the UI shadow variable — see §6.
float getCursorXFinal(uint8_t id) const override {
	return paramQuantities[CURSOR_X_POS]->getParam()->getValue();
}
float getCursorYFinal(uint8_t id) const override {
	return paramQuantities[CURSOR_Y_POS]->getParam()->getValue();
}

void setCursorXyImmediate(uint8_t id, float x, float y) override {
	if (id >= cursorCount()) return;   // silent no-op on a bad id — see §6
	paramQuantities[CURSOR_X_POS]->getParam()->setValue(x);
	cursorXfilter.out = cursorUiX = x;
	paramQuantities[CURSOR_Y_POS]->getParam()->setValue(y);
	cursorYfilter.out = cursorUiY = y;
}
void setCursorXyFiltered(uint8_t id, float x, float y) override {
	if (id >= cursorCount()) return;
	cursorUiX = x;
	cursorUiY = y;
}
```

`setCursorXyImmediate` is a full write (used by drag-end, undo/redo, and
your own `initExtra()` — see §4); `setCursorXyFiltered` is the live-drag
path, cheap and unfiltered, matching what `nodes.setXyFiltered` does for
nodes.

If the cursor needs to influence node-facing drawing (the connector lines
between a cursor and nearby nodes), override:

```cpp
float getCursorToNodeDistance(uint8_t cursorId, uint8_t nodeId) override {
	return dist[cursorId][nodeId];   // whatever your process() already computes
}
```

## 4. Init and reset

Wire `initNodes()` (which internally calls `initExtra()` then `resetNodes()`)
into your module's own init/reset path, and use `initExtra()` to set up
cursor storage — it's the natural place, since `initNodes()`'s own reset
logic only knows about nodes:

```cpp
void initExtra() override {
	setCursorXyImmediate(0, paramQuantities[CURSOR_X_POS]->getDefaultValue(),
	                         paramQuantities[CURSOR_Y_POS]->getDefaultValue());
	cursorXfilter.setTau(0.05f);
	cursorYfilter.setTau(0.05f);
}

void onReset() override {
	Sc::selection = XyScreenSelection();
	init();
	Module::onReset();
}

// Both real consumers call initExtra() once directly and once again inside
// Sc::initNodes() (which calls it as part of node param/filter setup) — the
// second call is redundant but harmless, since initExtra() only resets
// cursor position/filters and is idempotent. Follow the same pattern rather
// than trying to dedupe it; it's what the constructor also calls.
void init() {
	initExtra();
	Sc::initNodes();
	Seq::seqInit();   // if you also use XySeqModule
}
```

## 5. process()

Drive node and cursor positions the same way Arena/TransitPad do: read a
`ParamQuantity`'s `hasHandle` flag to prefer a live CV-mapped value over
your own filtered UI position, and otherwise fall through to
`Sc::nodes.getXFiltered`/`getYFiltered` (nodes) or your own filter (cursor):

```cpp
XyScreenParamQuantity* px = reinterpret_cast<XyScreenParamQuantity*>(paramQuantities[NODE_X_POS + j]);
float x = px->hasHandle ? px->getParam()->getValue() : Sc::nodes.getXFiltered(j, args.sampleTime);
```

Note that only `getNodeXFinal`/`getNodeYFinal`/`getNodeRadiusFinal`/
`getNodeAmountFinal`/`getNodeRadiusDefault`/`getNodeAmountDefault` are
virtuals on `XyScreenModule` — those exist because `XyScreenDummyModule`
(the module-browser preview) needs to override them. Everything else on
`XyScreenNodes` — filtered reads, immediate/filtered writes, randomize-all —
has no reason to be virtual and is called directly as `nodes.<method>(...)`,
not through a forwarding method on the module.

Whatever ends up driving the cursor position each frame (CV input, a motion
sequence, nothing) should end with `params[CURSOR_X_POS].setValue(...)` —
the param is the single source of truth the widget reads back through
`getCursorXFinal`.

## 6. Two things that will bite you if skipped

**Read the param, not the UI shadow, for `getCursorXFinal`/`getCursorYFinal`.**
The UI shadow variable (`cursorUiX` above) is written only by mouse drag and
`setCursorXyImmediate`/`Filtered` — not by CV, a motion sequence, or a
ParamHandle. If the cursor's "what does the widget draw" accessor reads the
shadow instead of the param, the on-screen cursor freezes under CV/sequence
control and the drag-smoothing ease-in disappears. This exact bug shipped
once during the type→interface refactor (see `var/XyScreenNodes_refactor_plan.md`)
and was only caught by manual testing — there's no automated coverage of
the drag-widget draw path, so verify this by eye (connect CV to the cursor
inputs, confirm the on-screen point tracks) rather than trusting a green
test suite.

**Bound every cursor setter against `cursorCount()`.** `XyScreenNodes`
guards its own arrays internally; your cursor storage doesn't get that for
free. Guard `setCursorXyImmediate`/`Filtered` yourself (`if (id >=
cursorCount()) return;`), as a silent no-op — that's the convention the rest
of this codebase uses for bad indices, not an assert.

## 7. Persistence

Nodes persist through `XyScreenNodes` directly — no per-id branching needed:

```cpp
json_t* dataToJson() override {
	json_t* rootJ = json_object();
	json_t* nodesJ = json_array();
	for (uint8_t i = 0; i < COUNT; i++) {
		json_t* nodeJ = json_object();
		Sc::nodes.dataToJson(nodeJ, i);   // writes "radius" + "amount"
		json_array_append_new(nodesJ, nodeJ);
	}
	json_object_set_new(rootJ, "nodes", nodesJ);
	return rootJ;
}

void dataFromJson(json_t* rootJ) override {
	json_t* nodesJ = json_object_get(rootJ, "nodes");
	if (nodesJ) {
		// Bound the loop to the array actually present: hand-edited or
		// corrupted patches may claim more nodes than COUNT holds.
		size_t n = std::min((size_t)COUNT, json_array_size(nodesJ));
		for (size_t i = 0; i < n; i++) {
			json_t* nodeJ = json_array_get(nodesJ, i);
			Sc::nodes.dataFromJson(nodeJ, i);   // reads "radius" + "amount", re-applies position
		}
	}
}
```

Cursors have **no** persistence method — `XyScreenCursor` deliberately
doesn't declare one, because a cursor's position isn't independently stored
state; it's derived from CV/sequence/param each frame, and radius/amount were
never meaningful for it. Don't add `dataToJson`/`dataFromJson` calls for the
cursor. If you find yourself wanting to persist something cursor-shaped,
that's very likely module-owned state (like TransitPad's `XySeqModule`
per-cursor sequence data) — persist it directly under your own JSON key, not
through this component.

## 8. Widget wiring

```cpp
template <typename MODULE>
struct MyNodeDragWidget : XyScreenNodeDragWidget<MODULE> {
	typedef XyScreenNodeDragWidget<MODULE> B;
	std::string getItemName() override { return string::f("Node %i", B::id + 1); }
	void appendContextMenu(Menu* menu) override { /* per-node menu items */ }
};

template <typename MODULE>
struct MyCursorDragWidget : XyScreenCursorDragWidget<MODULE> {
	typedef XyScreenCursorDragWidget<MODULE> B;
	std::string getItemName() override { return "Cursor"; }
};

template <typename MODULE>
struct MyXyScreenWidget : XyScreenWidget<MODULE> {
	MyXyScreenWidget(MODULE* module) : XyScreenWidget<MODULE>(module) {
		uint8_t nodeN = module ? module->nodeCount() : COUNT;      // fallback count for the module-browser preview
		this->template createNodeWidgets<MyNodeDragWidget<MODULE>>(module, nodeN);
		uint8_t cursorN = module ? module->cursorCount() : 1;
		this->template createCursorWidgets<MyCursorDragWidget<MODULE>>(module, cursorN);
	}
};
```

Then in the `ModuleWidget` constructor:

```cpp
MyXyScreenWidget<MODULE>* screenWidget = new MyXyScreenWidget<MODULE>(module);
screenWidget->box.pos = Vec(x, y);
screenWidget->box.size = Vec(w, h);
addChild(screenWidget);
```

When `module` is `nullptr` (module-browser preview), `createNodeWidgets`/
`createCursorWidgets` fall back to `XyScreenDummyModule` automatically — you
don't need to do anything extra for that path, just make sure the fallback
counts above are sensible.

If a param needs a small on-screen handle indicator too (so users can see
which XY params are CV-mapped), use `XyScreenDummyMapButton` as its widget,
same as `IN_X_POS`/`MIX_X_POS` etc. in Arena:

```cpp
addParam(createParamCentered<XyScreenDummyMapButton>(Vec(x, y), module, MODULE::NODE_X_POS + i));
```

## 9. Selection

`selection` (an `XyScreenSelection`) lives on `XyScreenModule` and is shared
between nodes and cursors — at most one item is selected at a time, and its
`kind` field says which. You generally don't touch `selection` directly
except in `onReset()` (see §4); the drag widgets manage it via
`setSelected()`/`isSelected()` on click.

If you have your own clickable indicator outside the screen widget (Arena's
`ClickableLight` next to each port), drive it the same way:

```cpp
bool alreadySelected = kind == XyScreenSelection::Kind::NODE
	? module->selection.isNode(id) : module->selection.isCursor(id);
module->selection = alreadySelected ? XyScreenSelection() : XyScreenSelection(kind, id);
```
