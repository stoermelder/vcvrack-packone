# stoermelder SPLICE-KIT

SPLICE-KIT is an 8×8 matrix patch bay. Each of the 64 buttons represents a single port anywhere in the current patch. Pressing two buttons creates or removes a cable between the corresponding ports. Eight independent scenes allow storing and recalling different cable configurations. All buttons — matrix cells and scene selectors — can be triggered from a MIDI controller, and SPLICE-KIT can send LED feedback back to the controller.

### Port assignment

Before a button can be used, a port from the current patch must be assigned to it. Right-click any matrix button to open its context menu and select **Learn port**. The cursor changes to a crosshair — click on any input or output port in the patch to complete the assignment. The port label and direction are shown at the top of the context menu once a port is assigned.

As a faster alternative, drag a cable from any port in the patch and drop it directly onto a matrix button to assign that port — no modal learn step required. The dragged cable itself is discarded; no patch cable is created by this gesture. If the button already had a port assigned, it is replaced.

To remove a port assignment select **Clear port** from the context menu. Any connections involving that button are removed from all scenes.

Hovering over a button shows a tooltip with the assigned port label and its current MIDI mapping.

### Creating and removing cables

Once two buttons have ports assigned, pressing them in sequence creates or removes a cable between those ports:

1. Press the first button — it starts blinking white to indicate it is selected and waiting for a second press.
2. Press a second button with a compatible port — SPLICE-KIT creates a cable if none exists, or removes the existing cable if one is already present.
3. Pressing the same button again cancels the selection.

Only an output port and an input port can be connected. Two ports of the same direction cannot be connected and the second press is ignored.

### Cross-instance patching

When multiple SPLICE-KIT modules are present in the same patch, buttons from different instances can be connected to each other. The workflow is the same two-press sequence:

1. Press a button on any SPLICE-KIT instance — it starts blinking white as the initiator.
2. Press a button on a **different** SPLICE-KIT instance — the initiating module creates a cable between the two ports.

Cross-instance cables are **not part of the scene system**. They exist as ordinary patch cables and are not stored in or affected by scene changes on either module. The feature can be disabled per-instance via **Cross-instance patching** in the module context menu.

### Scenes

The row of eight buttons at the bottom of the module selects the active scene. Each scene maintains its own independent cable state. Switching scenes reconciles the patch: cables that belong to the previous scene are removed and cables stored in the new scene are created.

All eight scenes share the same port assignments. Scenes store only the connection topology, not the port assignments themselves.

Right-clicking a scene button opens its context menu:

| Item | Action |
|---|---|
| **Clear** | Remove all connections stored in this scene |
| **Copy** | Copy this scene's connection topology to the clipboard |
| **Paste** | Apply the clipboard topology to this scene |
| **Learn MIDI** | Map a MIDI CC or note to this scene button |
| **Clear MIDI** | Remove the MIDI mapping from this scene button |
| **Randomize** | Generate a random valid connection topology for this scene (only available on the currently active scene) |

**Randomize** on the currently active scene's context menu pairs every assigned output port with a random assigned input port, one-to-one, respecting the same output→input direction rule as manual patching. It only changes that scene's connections — port assignments and other scenes are untouched.

Right-click the module and choose **Randomize** (or press `Ctrl+R`/`Cmd+R`) to instead clear every existing port assignment and label, then reassign as many matrix cells as possible to a distinct random port from anywhere in the patch — no two cells are ever assigned the same port. If the patch has fewer available ports than 64, the surplus cells are simply left unassigned rather than duplicating a port. This is a full reshuffle of the patch bay itself, independent of scenes — use the scene button's **Randomize** instead if you just want a new random cable topology between the ports you've already assigned.

### Scene link

Multiple SPLICE-KIT instances can be linked so that one follows another's scene selection automatically. Open the module context menu of the instance that should follow, then choose **Scene link master** and select the instance to follow (or **None** to unlink). Whenever the master's scene changes, the follower switches to the same scene index shortly after.

A follower with no master configured (**None**, the default) behaves exactly like a standalone instance. If the configured master is deleted from the patch, the follower automatically reverts to **None**. Scene link only tracks *which* scene is active — it has no effect on cross-instance patching, cable topology, or port assignments, which remain entirely independent per instance.

Scene link is strictly two-level: a master cannot itself follow another instance, and an instance already following a master cannot be picked as someone else's master. Chains of linked instances (and the loops they would create) are not possible.

While following a master, a follower's own scene buttons are inactive — pressing one (or triggering it via MIDI) has no effect, since its active scene is driven entirely by the master.

### Drag gestures

As a faster alternative to the two-press workflow, connections can be created or removed by dragging directly from one cell to another:

- **Left-drag** cell A → cell B — toggle the connection between those two ports (same effect as pressing A then B in sequence).
- **Shift + left-drag** cell A → cell B — move cell A's assignment to cell B: port, label, color, and all scene connections are transferred. Any previous assignment on cell B is discarded. MIDI mappings are **not** moved — each cell keeps its own mapping because it corresponds to a fixed physical button on the controller.
- **Left-drag on scene buttons** — creates a copy of the scene.

While dragging, the source shows a faint border so it remains identifiable.

### LED colors

| Color | Pattern | Meaning |
|---|---|---|
| Off | — | No port assigned |
| Red-orange (dim) | Steady | Output port assigned, no cable connected |
| Red-orange (bright) | Steady | Output port assigned, cable connected |
| Sky blue (dim) | Steady | Input port assigned, no cable connected |
| Sky blue (bright) | Steady | Input port assigned, cable connected |
| White | Blinking | First button pressed, waiting for second press |
| Sky blue | Blinking | Port learn active for this button |
| Yellow-green | Blinking | MIDI learn active for this button |

Each LED uses a single base color channel in isolation. Mixing channels is avoided because VCV Rack's sky blue base color already contains a large green component that would otherwise produce teal or white when combined.

Scene button LEDs are white at full brightness for the active scene and dim white when a scene contains stored connections.

### Port map view

Press `Space` while hovering over the module to toggle the port map view. In this mode:

- All patch cables are temporarily hidden.
- A spline is drawn from each matrix cell to its assigned port on the target module. Output-port connections are shown in red-orange; input-port connections in sky blue.

Press `Space` again to return to the normal patch view with cables restored.

### MIDI triggering

SPLICE-KIT accepts MIDI input to trigger both matrix buttons and scene buttons. Any assigned CC or note event with a non-zero value triggers the corresponding button, equivalent to pressing it with the mouse. Configure the MIDI input device in the **MIDI Input** submenu of the module context menu.

### MIDI learn

Each matrix button and each scene button can be mapped to a MIDI CC or note individually.

**Single button learn** — right-click the button and select **Learn MIDI**. The LED starts blinking magenta (matrix) or white (scene). Send any CC or note with a non-zero value from your controller to complete the mapping. The current mapping (e.g. _CC 91_ or _Note 36_) is shown in the **Learn MIDI** label once assigned. Pressing the blinking button again also cancels the learn without mapping.

Select **Clear MIDI** from the context menu to remove the mapping.

**Sequential learn** maps all 64 matrix buttons in order with a single workflow. Open the module context menu and enable **Sequential MIDI learn**. SPLICE-KIT starts learning button 1; after each received message it automatically advances to the next button until all 64 are mapped or the option is disabled again. Scene buttons are not included in sequential learn — assign them individually or use a controller preset.

### MIDI feedback and controller presets

SPLICE-KIT can send MIDI messages back to a controller to illuminate its LEDs in sync with the module's button states. Configure the output device in the **MIDI Output** submenu of the module context menu. When the output device changes, all LED states are refreshed automatically.

#### Controller presets

Open the **MIDI Feedback** submenu to select a preset:

| Preset | Description |
|---|---|
| **Off** | No feedback sent |
| **Launchpad / S (X-Y mode)** | Novation Launchpad (original) and Launchpad S in X-Y (default) layout. Bi-colour LEDs driven by Note On velocity. Grid cells use notes 0–119 (row×16+col); right-side scene launch buttons use notes 8, 24, 40, 56, 72, 88, 104, 120. |
| **Launchpad X / MK3 (Programmer mode)** | Novation Launchpad X / Mini MK3 in Programmer mode. Uses hardware flash (channel 1) for pending state and hardware pulse (channel 2) for learn states, synced to MIDI clock. Grid cells use Note On; top-row scene buttons use CC 91–98. |
| **Launchpad MK2 (Session mode)** | Novation Launchpad MK2 / S in Session mode. Grid cells use Note On; top-row scene buttons use CC 104–111. Uses hardware flash (channel 2) for pending state and hardware pulse (channel 3) for learn states, synced to MIDI clock. |
| **APC Mini** | Akai APC Mini (original). LED colors via Note On velocity. |
| **APC Mini MK2** | Akai APC Mini MK2. RGB LED palette via Note On velocity; MIDI channel encodes behavior (solid/pulse/blink). Grid cells use notes 0–63 (top-left to bottom-right); Scene Launch buttons 1–8 use notes 112–119. |
| **Ableton Push 2** | Ableton Push 2 in User mode. 8×8 pad grid uses Note On (notes 36–99, bottom-left to top-right); the eight buttons below the display (CC 20–27) serve as scene selectors. MIDI channel encodes animation: 0=static, 6–10=pulse, 11–15=blink. Color palette indices: 0=off, 127=red, 125=blue, 126=green, 122=white, 124=dark gray. |
| **Generic (Note On)** | Any controller that accepts Note On for LED control. Velocity values 0–6 map to off through the various states. |

Each preset defines the MIDI message type, channel, and value sent for every LED state (off, four color sets each with dim/active/connected variants, pending, port-learn, MIDI-learn, scene-active, scene-dim).

#### Loading and saving presets

Use **Load preset from file...** to read a JSON file from disk and activate it as the current feedback configuration. The loaded preset appears in the submenu below the built-in entries. Use **Save preset to file...** to write the currently active preset back to disk as a JSON file; this is also how to export a built-in preset as a starting template for customisation.

#### Custom preset JSON format

A preset file is a UTF-8 JSON object with three top-level keys:

```json
{
    "name": "My controller",
    "cells":  { "type": <1|2>, "numbers": [ <64 values> ] },
    "scenes": { "type": <1|2>, "numbers": [ <8 values>  ] },
    "specs": {
        "off":          <spec>,
        "color0dim":    <spec>,
        "color0":       <spec>,
        "color1dim":    <spec>,
        "color1":       <spec>,
        "color2dim":    <spec>,
        "color2":       <spec>,
        "color3dim":    <spec>,
        "color3":       <spec>,
        "pending":      <spec>,
        "portLearn":    <spec>,
        "midiLearn":    <spec>,
        "sceneActive":  <spec>,
        "sceneDim":     <spec>,
        "connected0":   <spec>,
        "connected1":   <spec>,
        "connected2":   <spec>,
        "connected3":   <spec>
    }
}
```

**`cells` and `scenes`** (optional) define the MIDI input mapping applied when **Apply note layout** is used. `type` is `1` for Note and `2` for CC. `numbers` lists the note or CC number for each button in row-major order (top-left to bottom-right for cells; top to bottom for scenes). Omit these keys if the preset is LED-only with no fixed layout.

Each **`<spec>`** object describes the MIDI message sent to the controller for one LED state:

| Field | Values | Default | Meaning |
|---|---|---|---|
| `type` | 0 = none, 1 = Note On, 2 = Note Off, 3 = CC, 4 = from-slot | 0 | Message type; 0 disables feedback for this state. `from-slot` sends Note On for note-mapped buttons and CC for CC-mapped buttons — useful for controllers like the Push 2 that mix both message types |
| `channel` | 0–15 | 0 | MIDI channel (0 = channel 1) |
| `noteMode` | 0 = from-slot, 1 = fixed | 0 | `from-slot`: use the button's own MIDI mapping number; `fixed`: use the `note` field |
| `note` | 0–127 | 0 | Note/CC number; only used when `noteMode` is `fixed` |
| `value` | 0–127 | 0 | Velocity (Note On/Off) or CC value |

**LED states:**

SPLICE-KIT uses four color sets (0–3). The defaults are: set 0 = red (output ports), set 1 = blue (input ports), set 2 = green, set 3 = white/neutral. Each set has a dim state (port assigned, no cable), an active state (cable connected), and a connected state (port is linked to the currently pending button).

| Key | Meaning |
|---|---|
| `off` | No port assigned |
| `color0dim` | Color-set 0, port assigned, no cable |
| `color0` | Color-set 0, port assigned, cable connected |
| `color1dim` | Color-set 1, port assigned, no cable |
| `color1` | Color-set 1, port assigned, cable connected |
| `color2dim` | Color-set 2, port assigned, no cable |
| `color2` | Color-set 2, port assigned, cable connected |
| `color3dim` | Color-set 3, port assigned, no cable |
| `color3` | Color-set 3, port assigned, cable connected |
| `pending` | First button pressed, waiting for second press |
| `portLearn` | Port-learn active for this button |
| `midiLearn` | MIDI-learn active for this button |
| `sceneActive` | Currently selected scene |
| `sceneDim` | Inactive scene that has stored connections |
| `connected0` | Port already connected to the pending button, color-set 0 |
| `connected1` | Port already connected to the pending button, color-set 1 |
| `connected2` | Port already connected to the pending button, color-set 2 |
| `connected3` | Port already connected to the pending button, color-set 3 |

#### Applying a preset's note layout

Presets that define a fixed button layout (Launchpad / S, Launchpad MK3, Launchpad MK2, APC Mini, APC Mini MK2, and Ableton Push 2) can automatically configure the MIDI input mappings to match: select **Apply note layout as MIDI input mappings**. This clears any existing MIDI input assignments and maps each cell and scene button to its corresponding note or CC number from the preset.


## Changelog

- v2.5.0
    - Initial release of SPLICE-KIT