# stoermelder MIDI-BAY

MIDI-BAY is an 8×8 matrix patch bay. Each of the 64 buttons represents a single port anywhere in the current patch. Pressing two buttons creates or removes a cable between the corresponding ports. Eight independent scenes allow storing and recalling different cable configurations. All buttons — matrix cells and scene selectors — can be triggered from a MIDI controller, and MIDI-BAY can send LED feedback back to the controller.

### Port assignment

Before a button can be used, a port from the current patch must be assigned to it. Right-click any matrix button to open its context menu and select **Learn port**. The cursor changes to a crosshair — click on any input or output port in the patch to complete the assignment. The port label and direction are shown at the top of the context menu once a port is assigned.

To remove a port assignment select **Clear port** from the context menu. Any connections involving that button are removed from all scenes.

Hovering over a button shows a tooltip with the assigned port label and its current MIDI mapping.

### Creating and removing cables

Once two buttons have ports assigned, pressing them in sequence creates or removes a cable between those ports:

1. Press the first button — it starts blinking white to indicate it is selected and waiting for a second press.
2. Press a second button with a compatible port — MIDI-BAY creates a cable if none exists, or removes the existing cable if one is already present.
3. Pressing the same button again cancels the selection.

Only an output port and an input port can be connected. Two ports of the same direction cannot be connected and the second press is ignored.

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

MIDI-BAY accepts MIDI input to trigger both matrix buttons and scene buttons. Any assigned CC or note event with a non-zero value triggers the corresponding button, equivalent to pressing it with the mouse. Configure the MIDI input device in the **MIDI Input** submenu of the module context menu.

### MIDI learn

Each matrix button and each scene button can be mapped to a MIDI CC or note individually.

**Single button learn** — right-click the button and select **Learn MIDI**. The LED starts blinking magenta (matrix) or white (scene). Send any CC or note with a non-zero value from your controller to complete the mapping. The current mapping (e.g. _CC 91_ or _Note 36_) is shown in the **Learn MIDI** label once assigned. Pressing the blinking button again also cancels the learn without mapping.

Select **Clear MIDI** from the context menu to remove the mapping.

**Sequential learn** maps all 64 matrix buttons in order with a single workflow. Open the module context menu and enable **Sequential MIDI learn**. MIDI-BAY starts learning button 1; after each received message it automatically advances to the next button until all 64 are mapped or the option is disabled again. Scene buttons are not included in sequential learn — assign them individually or use a controller preset.

### MIDI feedback and controller presets

MIDI-BAY can send MIDI messages back to a controller to illuminate its LEDs in sync with the module's button states. Configure the output device in the **MIDI Output** submenu of the module context menu. When the output device changes, all LED states are refreshed automatically.

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
| **Generic (Note On)** | Any controller that accepts Note On for LED control. Velocity values 0–6 map to off through the various states. |

Each preset defines the MIDI message type, channel, and value sent for every LED state (off, output-dim, output, input-dim, input, pending, port-learn, MIDI-learn, scene-active, scene-dim).

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
        "off":         <spec>,
        "outDim":      <spec>,
        "out":         <spec>,
        "inDim":       <spec>,
        "in":          <spec>,
        "pending":     <spec>,
        "portLearn":   <spec>,
        "midiLearn":   <spec>,
        "sceneActive": <spec>,
        "sceneDim":    <spec>
    }
}
```

**`cells` and `scenes`** (optional) define the MIDI input mapping applied when **Apply note layout** is used. `type` is `1` for Note and `2` for CC. `numbers` lists the note or CC number for each button in row-major order (top-left to bottom-right for cells; top to bottom for scenes). Omit these keys if the preset is LED-only with no fixed layout.

Each **`<spec>`** object describes the MIDI message sent to the controller for one LED state:

| Field | Values | Default | Meaning |
|---|---|---|---|
| `type` | 0 = none, 1 = Note On, 2 = Note Off, 3 = CC | 0 | Message type; 0 disables feedback for this state |
| `channel` | 0–15 | 0 | MIDI channel (0 = channel 1) |
| `noteMode` | 0 = from-slot, 1 = fixed | 0 | `from-slot`: use the button's own MIDI mapping number; `fixed`: use the `note` field |
| `note` | 0–127 | 0 | Note/CC number; only used when `noteMode` is `fixed` |
| `value` | 0–127 | 0 | Velocity (Note On/Off) or CC value |

**LED states:**

| Key | Meaning |
|---|---|
| `off` | No port assigned |
| `outDim` | Output port assigned, no cable |
| `out` | Output port assigned, cable connected |
| `inDim` | Input port assigned, no cable |
| `in` | Input port assigned, cable connected |
| `pending` | First button pressed, waiting for second press |
| `portLearn` | Port-learn active for this button |
| `midiLearn` | MIDI-learn active for this button |
| `sceneActive` | Currently selected scene |
| `sceneDim` | Inactive scene that has stored connections |

#### Applying a preset's note layout

Presets that define a fixed button layout (Launchpad / S, Launchpad MK3, and Launchpad MK2) can automatically configure the MIDI input mappings to match: select **Apply note layout as MIDI input mappings**. This clears any existing MIDI input assignments and maps each cell and scene button to its corresponding note or CC number from the preset.


## Changelog

- v2.5.0
    - Initial release of MIDI-BAY