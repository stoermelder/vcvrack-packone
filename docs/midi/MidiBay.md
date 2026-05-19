# stoermelder MIDI-BAY

MIDI-BAY is an 8×8 matrix patch bay. Each of the 64 buttons represents a single port anywhere in the current patch. Pressing two buttons creates or removes a cable between the corresponding ports. Eight independent scenes allow storing and recalling different cable configurations.

### Port assignment

Before a button can be used, a port from the current patch must be assigned to it. Right-click any matrix button to open its context menu and select **Learn port**. The cursor changes to a crosshair — click on any input or output port in the patch to complete the assignment. The port label and direction are shown at the top of the context menu once a port is assigned.

To remove a port assignment select **Clear port** from the context menu. Any connections involving that button are removed from all scenes.

### Creating and removing cables

Once two buttons have ports assigned, pressing them in sequence creates or removes a cable between those ports:

1. Press the first button — it starts blinking white to indicate it is selected and waiting for a second press.
2. Press a second button with a compatible port — MIDI-BAY creates a cable if none exists, or removes the existing cable if one is already present.
3. Pressing the same button again cancels the selection.

Only an output port and an input port can be connected. Two ports of the same direction cannot be connected and the second press is ignored.

### Scenes

The row of eight buttons at the bottom of the module selects the active scene. Each scene maintains its own independent cable state. Switching scenes reconciles the patch: cables that belong to the previous scene are removed and cables stored in the new scene are created.

All eight scenes share the same port assignments. Scenes store only the connection topology, not the port assignments themselves.

### MIDI triggering

MIDI-BAY accepts MIDI input to trigger matrix buttons. Any assigned CC or note event with a non-zero value triggers the corresponding button, equivalent to pressing it with the mouse. The MIDI input device is configured in the **MIDI Input** submenu of the module context menu.

### MIDI learn

Each button can be mapped to a MIDI CC or note individually. Right-click the button and select **Learn MIDI** to start learning for that button — the LED starts blinking magenta. Send any CC or note from your controller to complete the mapping. The current mapping (e.g. _CC 14_ or _Note 36_) is shown in the **Learn MIDI** menu item label once assigned. Select **Clear MIDI** to remove the mapping.

**Sequential learn** maps all 64 buttons in order with a single workflow. Open the module context menu and enable **Sequential MIDI learn**. MIDI-BAY starts learning button 1. After each received message it automatically advances to the next button until all 64 buttons have been mapped or the option is disabled again.

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

## Changelog

- v2.5.0
    - Initial release of MIDI-BAY
