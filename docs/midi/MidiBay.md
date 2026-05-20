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
| **Launchpad MK3 (Programmer mode)** | Novation Launchpad X / Mini MK3 in Programmer mode. Uses hardware flash (channel 1) for pending state and hardware pulse (channel 2) for learn states, synced to MIDI clock. Grid cells use Note On; top-row scene buttons use CC 91–98. |
| **Launchpad MK2 (Live mode)** | Novation Launchpad MK2 / S in Live/Session mode. Static colors only. Grid cells use Note On. |
| **APC Mini** | Akai APC Mini (original). LED colors via Note On velocity. |
| **Generic (Note On)** | Any controller that accepts Note On for LED control. Velocity values 0–6 map to off through the various states. |

Each preset defines the MIDI message type, channel, and value sent for every LED state (off, output-dim, output, input-dim, input, pending, port-learn, MIDI-learn).

#### Applying a preset's note layout

Presets that define a fixed button layout (Launchpad MK3 and MK2) can automatically configure the MIDI input mappings to match: select **Apply note layout as MIDI input mappings** at the bottom of the **MIDI Feedback** submenu. This clears any existing MIDI input assignments and maps each cell and scene button to its corresponding note or CC number from the preset. For the Launchpad MK3 preset this maps all 64 grid pads to their note numbers and all 8 top-row scene buttons to CC 91–98 in a single operation.

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

Press **Space** while hovering over the module to toggle the port map view. In this mode:

- All patch cables are temporarily hidden.
- A spline is drawn from each matrix cell to its assigned port on the target module. Output-port connections are shown in red-orange; input-port connections in sky blue.

Press **Space** again to return to the normal patch view with cables restored.

## Changelog

- v2.5.0
    - Initial release of MIDI-BAY