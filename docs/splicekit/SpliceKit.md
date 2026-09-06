# stoermelder SPLICE-KIT

- [Port assignment](#port-assignment)
- [Creating and removing cables](#creating-and-removing-cables)
- [Cross-instance patching](#cross-instance-patching)
- [Scenes](#scenes)
- [Scene link](#scene-link)
- [Drag gestures](#drag-gestures)
- [LED colors](#led-colors)
- [Port map view](#port-map-view)
- [MIDI triggering](#midi-triggering)
- [MIDI learn](#midi-learn)
- [MIDI feedback and controller presets](#midi-feedback-and-controller-presets)
- [Caveats](#caveats)
- [Changelog](#changelog)

### Overview

SPLICE-KIT is an 8×8 matrix patch bay. Each of the 64 buttons represents a single port anywhere in the current patch, and pressing two buttons creates or removes a cable between them — turning cable-heavy patching into something you can play from a grid controller.

- **64-button matrix**, each cell assigned to any input or output port in the patch, with fast per-port drag-to-assign and sequential learn for setting up many buttons at once.
- **Eight independent scenes** store separate cable topologies over the same port assignments, so a single button layout can drive several completely different patches.
- **Cross-instance patching** lets buttons on different SPLICE-KIT modules connect to each other directly, and **scene link** lets one instance follow another's scene selection.
- **Full MIDI control** — every matrix and scene button can be triggered by a MIDI note or CC, with single or sequential learn.
- **MIDI feedback** drives LEDs back on the controller, with built-in presets for popular grid controllers (Launchpad, APC Mini, Push 2, and more) plus a documented JSON format for custom ones.
- **Port map view** overlays lines from each cell to its assigned port, for tracing the patch bay's wiring at a glance.

SPLICE-KIT has no CV inputs or outputs of its own by design — it only creates and removes cables between ports that already exist elsewhere in the patch, rather than sitting inline in the signal path.

### Port assignment

Before a button can be used, a port from the current patch must be assigned to it. Right-click any matrix button to open its context menu and, under **Module port**, select **Learn**. The cursor changes to a crosshair — click on any input or output port in the patch to complete the assignment. The port label and direction are shown at the top of the context menu once a port is assigned.

As a faster alternative, drag a cable from any port in the patch and drop it directly onto a matrix button to assign that port — no modal learn step required. The dragged cable itself is discarded; no patch cable is created by this gesture. If the button already had a port assigned, it is replaced.

To assign many buttons in one pass, choose **Module port → Start sequential learn...** (or **Sequential port learn** in the module context menu). Each port you click is assigned to the next button in turn, starting from the button you right-clicked and continuing to button 64. Selecting the option again ends the run early.

To remove a port assignment select **Module port → Clear**. Any connections involving that button are removed from all scenes.

Hovering over a button shows a tooltip with the assigned port label and its current MIDI mapping.

#### Labels and colors

The text field at the top of a matrix button's context menu sets a custom label for that button, shown in its tooltip and in overlay messages instead of the port name. Press `Enter` to confirm. Clearing the field restores the port name.

The **Color** submenu overrides the button's LED color set. **Auto** (the default) picks the color by port direction — red for outputs, blue for inputs. A label and an explicit color both follow the port when a button is moved, and are dropped when its port is cleared or reassigned.

### Creating and removing cables

Once two buttons have ports assigned, pressing them in sequence creates or removes a cable between those ports:

1. Press the first button — it starts blinking white to indicate it is selected and waiting for a second press.
2. Press a second button with a compatible port — SPLICE-KIT creates a cable if none exists, or removes the existing cable if one is already present.
3. Pressing the same button again cancels the selection.

Only an output port and an input port can be connected. Two ports of the same direction cannot be connected and the second press is ignored.

While a button is pending, every button already connected to it blinks slowly in its own color, so the existing connections of the selected port are visible at a glance.

A button's context menu also lists **Remove cable**, which removes one selected connection of that button in the current scene, and **Remove all cables**, which removes all of them. Both are unavailable when the button has no connections.

#### Button mode

**Button mode** in the module context menu controls how a press is interpreted:

- **Toggle** (default) — the first press selects a button and it stays selected until a second press, whether or not you keep it held. This is the two-press workflow described above.
- **Momentary** — releasing the first button cancels the selection, so the second button must be pressed while the first is still held. This suits controllers whose pads send a note-off on release.

#### Overlay messages

Each action posts a short on-screen message naming the ports involved (for example _Cable created_ with both port names). Turn these off with **Show overlay messages** in the module context menu.

### Cross-instance patching

When multiple SPLICE-KIT modules are present in the same patch, buttons from different instances can be connected to each other. The workflow is the same two-press sequence:

1. Press a button on any SPLICE-KIT instance — it starts blinking white as the initiator.
2. Press a button on a **different** SPLICE-KIT instance — the initiating module creates a cable between the two ports.

Cross-instance cables are **not part of the scene system**. They exist as ordinary patch cables and are not stored in or affected by scene changes on either module. The feature is **enabled by default** and can be disabled per-instance via **Cross-instance patching** in the module context menu; disabling it on either instance stops that instance from initiating, responding to, or highlighting cross-instance gestures.

### Scenes

The row of eight buttons at the bottom of the module selects the active scene. Each scene maintains its own independent cable state. Switching scenes reconciles the patch: cables that belong to the previous scene are removed and cables stored in the new scene are created.

All eight scenes share the same port assignments. Scenes store only the connection topology, not the port assignments themselves.

Only cables where **both** endpoints are assigned to a matrix button are stored in a scene. A cable with just one end on a SPLICE-KIT button — or neither — is left alone by scene switching and is not part of any scene's state. A stored cable is captured the same way whether it was created with SPLICE-KIT's button patching or by dragging an ordinary cable by hand between the same two ports; SPLICE-KIT reads the current cable state of assigned ports rather than tracking how each cable came to exist.

Right-clicking a scene button opens its context menu:

| Item | Action |
|---|---|
| **Clear** | Remove all connections stored in this scene |
| **Copy** | Copy this scene's connection topology to the clipboard |
| **Paste** | Apply the clipboard topology to this scene |
| **Learn MIDI** | Map a MIDI CC or note to this scene button |
| **Clear MIDI** | Remove the MIDI mapping from this scene button |
| **Randomize** | Generate a random valid connection topology for this scene (only available on the currently active scene) |

**Randomize** on the currently active scene's context menu opens a submenu with two modes. Both respect the same output→input direction rule as manual patching, and both only change that scene's connections — port assignments and other scenes are untouched.

- **Sparse** pairs every assigned output port with a random assigned input port, one-to-one. If one side has more assigned ports than the other, the surplus ports on the larger side are left unconnected.
- **Full** also pairs outputs and inputs one-to-one, but reuses ports on the shorter side once it runs out, so every assigned port ends up with at least one connection. Ports on the shorter side may end up fanned out to several partners.

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
- **Left-drag** scene A → scene B — copy scene A's connections onto scene B, replacing them.

While dragging, the source shows a faint border so it remains identifiable.

### LED colors

| Color | Pattern | Meaning |
|---|---|---|
| Off | — | No port assigned |
| Red (dim) | Steady | Output port assigned, no cable connected |
| Red (bright) | Steady | Output port assigned, cable connected |
| Blue (dim) | Steady | Input port assigned, no cable connected |
| Blue (bright) | Steady | Input port assigned, cable connected |
| White | Blinking | First button pressed, waiting for second press |
| Button's own color | Slow blink | Connected to the currently pending button |
| Grey | Blinking | Port learn active for this button |
| Pale blue | Blinking | MIDI learn active for this button |

Red and blue are the defaults for output and input ports; the **Color** submenu can set any button to red, blue, orange, or green instead.

Scene button LEDs are white only: full brightness for the active scene, dim when an inactive scene contains stored connections, off when it is empty, and blinking while MIDI learn is active.

### Port map view

Press `Space` while hovering over the module to toggle the port map view. In this mode:

- All patch cables are temporarily hidden.
- A spline is drawn from each matrix cell to its assigned port on the target module, in that button's LED color.
- Hovering a button dims every unrelated spline and draws an arc to each button it is connected to in the current scene.

Press `Space` again to return to the normal patch view with cables restored. The same toggle is available as **Visualize** in the module context menu.

### MIDI triggering

SPLICE-KIT accepts MIDI input to trigger both matrix buttons and scene buttons. Any assigned CC or note event with a non-zero value triggers the corresponding button, equivalent to pressing it with the mouse. Configure the MIDI input device in the **MIDI Input** submenu of the module context menu.

**Copying a scene from the controller** — with **MIDI scene-copy gesture** enabled in the module context menu (off by default), holding one mapped scene button and pressing a second one copies the first scene's connections onto the second, the same as dragging one scene button onto another. The gesture is recognised when a second scene activation arrives before the first button's release (note-off or CC 0). While the option is off, the second activation simply selects that scene.

> **Note:** the gesture relies on your controller sending a release message. A controller or mapping that only sends presses will make *every second scene selection* copy the previous scene over the one you select, overwriting its stored connections — this is why the option is off by default. Only enable it if your controller sends note-off / CC 0 on release, and verify in a scratch patch first — the copy cannot be undone.

### MIDI learn

Each matrix button and each scene button can be mapped to a MIDI CC or note individually.

**Single button learn** — right-click the button and select **MIDI → Learn** (**Learn MIDI** on scene buttons). The LED starts blinking — pale blue on matrix buttons, white on scene buttons. Send any CC or note with a non-zero value from your controller to complete the mapping. The current mapping (e.g. _CC 91_ or _Note 36_) is shown in the menu label once assigned. Pressing the blinking button again cancels the learn without mapping.

Select **MIDI → Clear** (**Clear MIDI** on scene buttons) to remove the mapping.

**Sequential learn** maps a run of matrix buttons in one pass. Choose **MIDI → Start sequential learn...** from a button's context menu to begin at that button, or **Sequential MIDI learn** from the module context menu. After each received message SPLICE-KIT advances to the next button, continuing to button 64 or until the option is disabled again. Scene buttons are not included — assign them individually or use a controller preset.

### MIDI feedback and controller presets

SPLICE-KIT can send MIDI messages back to a controller to illuminate its LEDs in sync with the module's button states. Configure the output device in the **MIDI Output** submenu of the module context menu. When the output device changes, all LED states are refreshed automatically.

#### Controller presets

Open the **MIDI Preset** submenu to select a preset:

| Preset | Description |
|---|---|
| **No preset** | No feedback sent |
| **Launchpad / S (X-Y mode)** ⚠️ | Novation Launchpad (original) and Launchpad S in X-Y (default) layout. Bi-colour LEDs driven by Note On velocity. Grid cells use notes 0–119 (row×16+col); right-side scene launch buttons use notes 8, 24, 40, 56, 72, 88, 104, 120. |
| **Launchpad X / MK3 (Programmer mode)** | Novation Launchpad X / Mini MK3 in Programmer mode. Uses hardware flash (channel 1) for pending state and hardware pulse (channel 2) for learn states, synced to MIDI clock. Grid cells use Note On; top-row scene buttons use CC 91–98. |
| **Launchpad MK2 (Session mode)** ⚠️ | Novation Launchpad MK2 / S in Session mode. Grid cells use Note On; top-row scene buttons use CC 104–111. Uses hardware flash (channel 2) for pending state and hardware pulse (channel 3) for learn states, synced to MIDI clock. |
| **APC Mini** ⚠️ | Akai APC Mini (original). LED colors via Note On velocity. |
| **APC Mini MK2** ⚠️ | Akai APC Mini MK2. RGB LED palette via Note On velocity; MIDI channel encodes behavior (solid/pulse/blink). Grid cells use notes 0–63 (top-left to bottom-right); Scene Launch buttons 1–8 use notes 112–119. |
| **Ableton Push 2** ⚠️ | Ableton Push 2 in User mode. 8×8 pad grid uses Note On (notes 36–99, bottom-left to top-right); the eight buttons below the display (CC 20–27) serve as scene selectors. MIDI channel encodes animation: 0=static, 6–10=pulse, 11–15=blink. Color palette indices (partial reference, see hover text for the full set): 0=off, 127=red, 125=blue, 126=green, 122=white, 124=dark gray. |
| **Generic (Note On)** | Any controller that accepts Note On for LED control. Velocity values 0–6 map to off through the various states. |

⚠️ marks presets not yet verified against real hardware — the menu label itself shows "[untested]". They follow the manufacturer's documented MIDI implementation but have not been confirmed to work correctly on the physical controller. If you have the hardware and can confirm or fix one, please report back so the tag can be removed.

Each preset defines the MIDI message type, channel, and value sent for every LED state (off, four color sets each with dim/active/connected variants, pending, port-learn, MIDI-learn, scene-active, scene-dim). Hover a preset entry to see additional notes about its layout and hardware, where available.

The built-in presets are plain files on disk (`presets/SpliceKit/*.ctrl.json` inside the plugin folder). They can be edited directly, and new files dropped into that folder appear in the **MIDI Preset** submenu the next time VCV Rack starts — no reinstall needed. See [Custom preset JSON format](#custom-preset-json-format) below for the file format.

#### Loading and saving presets

Use **Load preset from file...** to read a JSON file from disk and activate it as the current feedback configuration. Use **Save preset to file...** to write the currently active preset back to disk as a JSON file; this is also how to export a built-in preset as a starting template for customisation.

#### Custom preset JSON format

A preset file is a UTF-8 JSON object:

```json
{
    "name": "My controller",
    "description": "Optional notes shown when hovering the preset in the menu.",
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

**`name`** is shown in the **MIDI Preset** submenu and used as the default filename when saving. **`description`** is optional; if present, it is shown when hovering the preset entry. Use `\n\n` to separate it into multiple paragraphs — long single-paragraph text is hard to read once wrapped.

**`cells` and `scenes`** (optional) define the MIDI input mapping applied when **Apply input mappings from preset** is used. `type` is `1` for Note and `2` for CC. `numbers` lists the note or CC number for each button in row-major order (top-left to bottom-right for cells; top to bottom for scenes). Omit these keys if the preset is LED-only with no fixed layout.

Each **`<spec>`** object describes the MIDI message sent to the controller for one LED state:

| Field | Values | Default | Meaning |
|---|---|---|---|
| `type` | 0 = none, 1 = Note On, 2 = Note Off, 3 = CC, 4 = from-slot | 0 | Message type; 0 disables feedback for this state. `from-slot` sends Note On for note-mapped buttons and CC for CC-mapped buttons — useful for controllers like the Push 2 that mix both message types |
| `channel` | 0–15 | 0 | MIDI channel (0 = channel 1) |
| `noteMode` | 0 = from-slot, 1 = fixed | 0 | `from-slot`: use the button's own MIDI mapping number; `fixed`: use the `note` field |
| `note` | 0–127 | 0 | Note/CC number; only used when `noteMode` is `fixed` |
| `value` | 0–127 | 0 | Velocity (Note On/Off) or CC value |

**LED states:**

SPLICE-KIT uses four color sets (0–3): set 0 = red, set 1 = blue, set 2 = orange, set 3 = green. Buttons use set 0 for output ports and set 1 for input ports unless overridden in the **Color** submenu. Each set has a dim state (port assigned, no cable), an active state (cable connected), and a connected state (port is linked to the currently pending button).

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

Presets that define a fixed button layout (Launchpad / S, Launchpad MK3, Launchpad MK2, APC Mini, APC Mini MK2, and Ableton Push 2) can automatically configure the MIDI input mappings to match: select **Apply input mappings from preset**. This clears any existing MIDI input assignments and maps each cell and scene button to its corresponding note or CC number from the preset.

### Caveats

**Creating or removing a cable briefly pauses audio for the whole patch.** This is not a SPLICE-KIT quirk — it's how VCV Rack's engine works for any cable change, whether made by SPLICE-KIT, another module, or dragging a cable by hand. Adding or removing a cable requires exclusive access to the patch graph, which means audio processing for *every* module stops for an instant while the change is made, then resumes. For a single cable this pause is far too short to hear.

The difference with SPLICE-KIT is that many of its features can change several cables in rapid succession rather than one at a time:

- **Switching scenes** removes the old scene's cables and creates the new scene's cables, one by one.
- **Randomize** (module-level) can reassign all 64 buttons and rewire connections between them.
- **Randomize** (per-scene, Full or Sparse) can create up to dozens of cables in one go.
- **Sequential learn**, **scene copy/paste**, and MIDI-triggered scene switching can also touch many cables quickly.

Each of these cables is still added or removed one at a time behind the scenes — there is no single pause covering the whole batch. With only a handful of cables this is inaudible, but a large scene switch or a full Randomize can add up to a short burst of brief pauses in immediate succession, which may be audible as a click, a stutter, or a moment of timing jitter, especially at small buffer sizes.

**What this means in practice:**

- Rewiring a patch while it's silent, being set up, or between songs is unaffected — it's the same as editing cables by hand.
- Switching scenes or randomizing **while audio is live and being listened to** (rehearsing, recording, or performing) carries a small but real risk of an audible glitch, roughly in proportion to how many cables change at once. A scene that only changes one or two cables is safer than one that rewires most of the grid.
- MIDI-triggered scene switches happen off the audio thread already, so SPLICE-KIT itself never freezes waiting on this — but the momentary engine pause for each cable change still happens and can still be heard.
- If you plan to switch scenes or trigger Randomize live, it's worth testing that specific transition beforehand at your actual buffer size to confirm it's clean, rather than discovering an audible click during a performance.

## Changelog

- v2.6.0
    - Initial release of SPLICE-KIT