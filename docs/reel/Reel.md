# stoermelder REEL

REEL is a patch-memory for a group of modules. It captures the full state (parameters, settings) of all bound modules together with the cables between them, storing everything in named snapshot slots. Each slot is a complete picture of a sub-patch at one point in time.

### Binding modules

At least one module must be bound before REEL can be used. Binding options are available in the right-click context menu of the REEL panel:

- **Bind module (left expander)** — Binds the module that is placed directly to the left of REEL.
- **Bind module (select one)** — Turns the mouse pointer into a crosshair. Click on any module panel to bind it.
- **Bind modules (select multiple)** — Same as _select one_ but keeps binding successive modules until you click on empty rack space.
- **Bind modules (current selection)** — Binds all modules that are currently selected (highlighted in the rack).

There is no limit on the number of bound modules. Keep in mind that loading snapshots with many complex modules can increase CPU usage briefly.

Bound modules are listed in the context menu under **Bound modules**. From there each module can be zoomed to on the rack or unbound individually. Unbinding a module does not delete any saved snapshots; the stored state for that module simply has no effect when a slot is loaded.

All bound modules are highlighted with a colored bounding box on the rack. See [Module outlines](#module-outlines) below.

Additionally you can load a preset of REEL into an existing patch and recreate all bound modules by an option in the context menu. This enables you to craft a patch using different building-blocks, which can easily change preset using the snapshots in REEL.

### Saving snapshots

To save the current state of all bound modules into a slot, right-click any slot row and choose **Save current state**. REEL captures:

1. The complete preset (all parameters and settings) of every bound module.
2. Every cable that connects two bound modules to each other.

Cables from bound modules to non-bound modules are not captured — only the internal wiring of the group.

Slots that contain a snapshot show a filled triangle (▶) on the left side of the row.

### Loading snapshots

Click the ▶ icon on the left side of any used slot row to load it. REEL will:

1. Restore the parameters of each bound module from the stored preset.
2. Remove any cables that currently exist between bound modules.
3. Recreate the cables that were saved with the slot.

All changes are pushed to Rack's undo history as a single action, so they can be reversed with Ctrl+Z / Cmd+Z.

The active slot is highlighted. If you change module parameters or cables manually after loading a slot, REEL does not track those changes — to update the slot use **Save current state** again.

### Naming slots

Click anywhere on the label area (the right portion of a slot row, to the right of the ▶ column) to enter inline label editing. The slot switches to edit mode: type to enter a name, use the arrow keys to move the cursor, Backspace/Delete to erase characters. Press Enter to confirm or Escape to discard changes.

Slots with no label display _Snapshot_ as a placeholder. Labels that are too wide for the row scroll horizontally automatically.

### Cable capture

REEL only captures cables where **both** endpoints (output port and input port) belong to bound modules. Cables that connect a bound module to an unbound module are ignored.

When loading a slot the following happens in order:

1. Module presets are restored.
2. All current cables between bound modules are removed.
3. The cables stored in the slot are recreated with their original colors.

This means loading a slot completely replaces the internal wiring of the group — any cables between bound modules will be lost. Cables to modules outside the group are unaffected.

### Slot operations

Right-click any slot row to open its context menu:

| Option | Description |
|--------|-------------|
| **Save current state** | Captures the current state of all bound modules and their internal cables into this slot. |
| **Load** | Loads this slot (same as clicking the ▶ icon). Disabled when the slot is empty. |
| **Clear** | Deletes the snapshot stored in this slot. |
| **Copy** | Marks this slot as the copy source. |
| **Paste** | Copies the previously marked slot into this slot. |

REEL always provides a trailing empty slot at the bottom of the list. As soon as you save into the trailing slot, a new empty slot appears below it. There is no upper limit; the list grows as needed.

### Module outlines

All bound modules are decorated with a colored bounding box on the rack. This makes it easy to see at a glance which modules belong to a REEL group.

- **Show module outlines** (context menu, Shift+B) — Toggles the bounding boxes on and off.
- **Outline color** (context menu) — Opens a color picker to change the box color.

### Tips

- REEL is well-suited for capturing different routing configurations of a self-contained sub-patch — for example, different effect chains, different mixdown states, or different performance setups within the same patch.

- Because cable colors are preserved, you can use cable color as a visual hint to differentiate the routing states across slots.

- Unbinding a module does not erase its stored states in existing slots. If you rebind the same module later (and its module ID is the same, which is the case as long as you have not deleted the module), existing slots will restore its state again.

- REEL cannot morph between snapshots — it switches states instantly. For parameter morphing, see stoermelder [TRANSIT](../transit/Transit.md).

- Large or complex modules (samplers, convolution reverbs, etc.) can produce large preset data. A warning is shown when binding if the stored preset size exceeds ~400 KB.

- The entire slot list, including all stored snapshots, is saved with the patch. Loading the patch restores the REEL state exactly.

## Changelog

- v2.4.0
    - Initial release of REEL
