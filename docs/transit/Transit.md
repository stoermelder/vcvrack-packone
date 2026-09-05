# stoermelder TRANSIT

- [Binding parameters](#binding-parameters)
- [Write-mode: saving snapshots](#write-mode-saving-snapshots)
- [Read-mode: morphing between snapshots](#read-mode-morphing-between-snapshots)
- [Auto-mode](#auto-mode)
- [Sequencing and selecting snapshots](#sequencing-and-selecting-snapshots)
- [OUT-port](#out-port)
- [Fade time](#fade-time)
- [+T expander](#t-expander)
- [Transit-Pad expander](#transit-pad-expander)
- [Tips](#tips)
- [Changelog](#changelog)

### Overview

TRANSIT is a utility module for morphing parameters of other modules from one "snapshot" to another utilizing Rack's parameter-mapping functionality. The module provides 12 snapshot-slots and can be extended with up to fifteen extender-modules called +T.

There is a nice [video overview](https://www.youtube.com/watch?v=qnjBrlkcYOw) from [Artem Leonov](https://artemleonov.bandcamp.com/) of _VCV Rack Ideas_ showcasing a preview-build of TRANSIT.

![TRANSIT intro](./Transit-intro.gif)

### Binding parameters

At least one parameter (knob, fader, switch...) must be bound to TRANSIT before the module can be used. It provides multiple ways for binding one or more parameters which need to be enabled on the contextual menu:

- **Bind module (left)** - Place any module in Rack to the left of TRANSIT. By selecting _Bind module (left)_ all parameters of the module get bound by TRANSIT.

![TRANSIT bind left](./Transit-bind-left.gif)

- **Bind module (select)** - Selecting _Bind module (select)_ turns the mouse pointer into a crosshair. After you click on the panel of a module all parameters of this module get bound by TRANSIT.

![TRANSIT bind select](./Transit-bind-select.gif)

- **Bind single parameter** - Selecting _Bind single parameter_ turns the mouse pointer into a crosshair. After you click on a parameter of any module this parameter gets bound by TRANSIT.

- **Bind multiple parameters** - This mode works the same way as _Bind single parameter_ except the binding mode is not disabled automatically and multiple parameters can be bound in a row.

![TRANSIT bind parameter](./Transit-bind-param.gif)

TRANSIT is designed to bind parameters from different modules at the same time and technically there is no limitation on the number of parameters which can be bound. Please note that the CPU usage for morphing between snapshots increases linearly with the number of bound parameters.

Parameters can be unbound at any point by unmapping the mapping indicator. Please note that values stored in snapshots won't be deleted for unbound parameters.

### Write-mode: saving snapshots

Write-mode is used to save snapshots in TRANSIT after some parameters have been bound: A snapshot consists of the values all bound parameters are currently set to. You enter write-mode by flipping the switch on the bottom to the _W_-position. To store a snapshot simply short-press one of the 12 snapshot-buttons and the LED on a snapshot-button is lit in red when a slot is in use. To clear a snapshot long-press the button. 

![TRANSIT write-mode](./Transit-write.gif)

There are also some options on the context menu of the snapshot-buttons:

- **Load** (also Shift+click) - Applies the snapshot to the parameters if the slot is used.
- **Clear** - Clears the snapshot.
- **Randomize and save** - Randomizes all bound parameters and saves a snapshot.
- **Copy** - Copies the snapshot to the clipboard.
- **Paste** - Pastes the snapshot which has been copied before.
- **Shift front** (added in v1.10.0) - Moves all snapshots one slot forward, beginning from the initiating slot. If the first slot is used it gets deleted.
- **Shift back** (added in v1.10.0) - Moves all snapshots one slot backward, beginning from the initiating slot. If the last slot is used it gets deleted, also the number of currently active slots is unaffected.

![TRANSIT write-mode](./Transit-write-context.gif)

A blinking white LED signals the snapshot that was last applied to the parameters. Please keep in mind that you can change bound parameters manually which will not be recognized by TRANSIT.

In write-mode any input on the _SEL_-port is ignored and sequencing is disabled.

### Read-mode: morphing between snapshots

Read-mode is the default operational mode of TRANSIT and is used to "load" or "apply" previously saved snapshots to the parameters. The interesting part of TRANSIT is its ability to "morph" the parameter values into the target snapshot: _FADE_ sets the amount of time it takes to reach the parameters' positions stored in the snapshot; this duration can also be controlled by CV (0–10 V). The CV input is always additive — it adds to the global _FADE_ knob when a slot uses the default fade time, and it adds to the per-slot fade time when a slot has its own fade setting. There is also a trimpot for setting the shape of the transition; in the middle position, the parameters are morphed linearly.

![TRANSIT morph](./Transit-morph.gif)

TRANSIT provides several precision-settings on the contextual menu which influence the CPU usage when morphing snapshots: Audio rate, Lower CPU (1/8), Lowest CPU (1/64, default in standalone Rack), Even lower CPU (1/256, default in plugin mode) and Crazy low CPU (1/1024).

### Auto-mode

Auto-mode (added in v1.10.0) stores snapshots automatically to the current slot right before moving on to the next slot. A typical workflow would look like this: Store a few snapshots using Write-mode as usual. Afterwards flip the switch to the middle "A"-position and start slow sequencing using the _SEL_-port. Imagine the first usable slot is active and TRANSIT will begin morphing into the next usable slot. Right before the transition starts the current state of the parameters is stored into the currently active slot, preserving all adjustments made in the meantime. In contrast, Read-mode would simply load the next slot and the snapshot stored in the current slot will stay unchanged, discarding all changes made to the parameters. Note: Empty slots will stay empty, even in Auto-mode.

### Sequencing and selecting snapshots

The fun begins when you use the port labelled _SEL_ for selecting snapshots by CV. Although there are 12 snapshot slots available it is possible to use less slots for sequencing: You can adjust the number of active slots (i.e. sequence length) by long-pressing a snapshot-button while in read-mode. The LEDs turn off completely for slots that are currently disabled.

You can change the range of usable snapshots by right‑clicking any snapshot button and choosing _Set as first_ or _Set as last_. The first usable snapshot becomes the new 'Snapshot 1' for sequencing, CV selection and Phase mode; the last usable snapshot sets the end of the sequence.

Modes for _SEL_ on the contextual menu. The _RESET_ input restarts the sequence based on the mode:

| Mode | Description | Reset behavior |
|------|-------------|------|
| **Trigger forward** | A trigger advances TRANSIT to the next snapshot. Empty slots are part of the sequence but won't have any effect on the parameters. | Resets to first usable snapshot |
| **Trigger reverse** | Same as **Trigger forward** but reverse direction. | Resets to first usable snapshot |
| **Trigger pingpong** | Same as **Trigger forward** but loops first forward then reverse. | Resets to first usable snapshot, direction to forward |
| **Trigger alternating** (added in v1.8.0) | Same as **Trigger forward** but progresses in the following manner (for 6 active snapshots): 1, 2, 1, 3, 1, 4, 1, 5, 1, 6, 1, 5, 1, 4, 1, 3, 1, 2, ... | Resets to first usable snapshot |
| **Trigger random** | Same as **Trigger forward** but chooses the next snapshot randomly. | Resets to first usable snapshot |
| **Trigger pseudo-random** (added in v1.8.0) | Same as **Trigger random** but never chooses a snapshot multiple times in a row (which happens on "random"). | Resets to first usable snapshot |
| **Trigger random walk** (added in v1.8.0) | Same as **Trigger forward** but chooses the next snapshot randomly from those adjacent to the currently active snapshot. | Resets to first usable snapshot |
| **Trigger shuffle** (added in v1.8.0) | Same as **Trigger forward** but works on a random permutation of the active snapshots: Every snapshot will be enabled once before the next permutation is randomly generated. | Re-initializes the shuffle order |
| **0..10V** | You can select a specific snapshot by voltage. A voltage of 0–0.833V selects the first usable slot, 0.833–1.666V selects the next one, and so on, if all 12 snapshot-slots are active. Keep in mind that adjusting the length of the sequence or the configured first/last slots also adjusts the voltage ranges: e.g. a sequence with length 2 will select the first usable slot on voltage 0–5V. | No effect |
| **C4** | This mode follows the V/Oct standard. C4 selects the first usable snapshot, C#4 selects the next one and so on. Channel 2 on the CV-input responds to triggers to re-trigger the currently selected snapshot. | No effect |
| **Arm** | This mode is a kind of "buffered trigger": First apply a clock signal on _SEL_. Then you "arm" any snapshot manually or by MIDI-mapping its button (resulting in a yellow LED) which will be activated on the next clock trigger (white LED). This mode allows manual snapshot activation synchronized to a clock. | Clears the armed snapshot |
| **Phase** (added in v1.9.0) | This mode behaves differently than the other modes: An input voltage of 0–10V scans continuously through the stored snapshots. A voltage of 0V sets the parameters to the first usable snapshot, and 10V sets the parameters to the last active snapshot (as set via "Set as last"); in between the parameters are interpolated according to the used snapshots. Slew-limiting can be applied additionally using the _Fade_-slider. | No effect |

![TRANSIT SEL-port](./Transit-sel.gif)

### _OUT_-port

TRANSIT brings an _OUT_-port for different purposes:

- **Envelope** - Outputs an envelope-like shape of the fading-curve starting at 0V and ending at 10V.

- **Gate** - Outputs a 10V gate while a fade is in progress.

- **Trigger snapshot change** - Outputs a 10V trigger signal every time a new snapshot-slot is selected.

- **Trigger fade start** - Outputs a 10V trigger at the start of every fade.

- **Trigger fade stop** - Outputs a 10V trigger at the end of every fade.

- **Poly** - Outputs a polyphonic signal combining all of the previous signals on the channels of the cable.

Note: These modes are unavailable if _SEL_-port operates in Phase-mode.

![TRANSIT OUT-port](./Transit-out.gif)

### Fade time

The fade duration follows an exponential curve: `t = 0.01 × 2^(f × 10)` seconds, where `f` is the sum of the knob (or per-slot) value and the CV contribution (`CV voltage / 10`). By default this sum is clamped to 0–1, giving a maximum of ~10.2 s. Enabling _Disable CV clamping_ on the context menu removes this ceiling and allows the CV to push the combined value above 1.0, resulting in much longer fades:

| Fader / Slot setting | Duration (0V CV) | +10V CV, clamping on | +10V CV, clamping off |
|-------------|------------------|------------------------|-------------------------|
| 0.0 | ~10 ms | ~10.2 s | ~10.2 s |
| 0.1 | ~20 ms | ~10.2 s | ~20.5 s |
| 0.2 | ~40 ms | ~10.2 s | ~41 s |
| 0.3 | ~80 ms | ~10.2 s | ~82 s |
| 0.4 | ~160 ms | ~10.2 s | ~164 s (~2.7 min) |
| 0.5 | ~320 ms | ~10.2 s | ~328 s (~5.5 min) |
| 0.6 | ~640 ms | ~10.2 s | ~655 s (~10.9 min) |
| 0.7 | ~1.3 s | ~10.2 s | ~1311 s (~21.8 min) |
| 0.8 | ~2.6 s | ~10.2 s | ~2621 s (~43.7 min) |
| 0.9 | ~5.1 s | ~10.2 s | ~5243 s (~87.4 min) |
| 1.0 | ~10.2 s | ~10.2 s | ~10486 s (~2.9 h) |

These durations are **independent of sample rate** — the fade engine integrates against real time in seconds. The _Precision_ setting on the context menu only affects CPU usage and integration coarseness for very short fades at low precision; it does not change the absolute duration.

### +T expander

TRANSIT provides 12 snapshot-slots and supports extending this number with +T expanders: The expander must be placed on the right side of TRANSIT. Up to fifteen instances of +T can be added to one instance of TRANSIT, providing 12 × 16 = 192 snapshot-slots in total.

Once placed next to TRANSIT the expander works and behaves the same way TRANSIT does and the setup is done analogously. +T itself provides no further options.

![+T expander](./Transit-t.gif)

### Transit-Pad expander

TRANSIT-PAD is a specialized expander for TRANSIT that provides a 2-dimensional XY-pad for morphing between snapshots: Snapshots are placed at arbitrary positions on the pad and a movable _Mix_ point crossfades them based on its distance to each snapshot. The expander is useful for creating smooth parameter "landscapes" — instead of stepping through snapshots in sequence, you move a single point in 2D space and any number of snapshots blend into the output simultaneously.

#### Setup and connection

Place TRANSIT-PAD on the right side of TRANSIT, just like a +T expander. As soon as it is connected TRANSIT switches to a dedicated XY-pad mode:

- The _SEL_-port and the _OUT_-port of TRANSIT become inactive. The _SEL_-mode is forced to _Off_ and _OUT_-mode is forced to _Off_ automatically.
- Only Read-mode of TRANSIT is supported. Auto-mode and Write-mode of the operating-mode switch are ignored while the pad is active.
- Any number of +T expanders can be chained between TRANSIT and TRANSIT-PAD (the pad is placed at the end of the chain, after all +T expanders). The snapshots stored on those +T expanders are reachable from the pad just like the snapshots on the host TRANSIT. Placing additional +T expanders or a second pad to the right of TRANSIT-PAD is not supported: the chain stops as soon as the pad is reached, and any expander placed after it is ignored.

Setup of TRANSIT itself is unchanged: bind parameters and save snapshots in Write-mode as described above. The pad itself does not store snapshots — it only assigns a 2D-position to snapshots that already exist on the host TRANSIT.

#### The XY-pad

The main element of the expander is the large square XY-display. It always shows two kinds of items:

- **Snapshot points** A–H. Each point is one snapshot of the host TRANSIT, placed at an arbitrary 2D position. The point is rendered with the color of the current snapshot-set and labeled with the snapshot's letter.
- **The _Mix_ point** (rendered as a `+`). The mix point's position is the "play head" of the pad. For every snapshot its weight is calculated as the distance from the mix point to the snapshot position, scaled by the snapshot's individual **radius**: snapshots inside their radius contribute with a weight that approaches 1.0 the closer the mix point is to them. Multiple snapshots can be active at the same time, and the parameters of all bound modules are blended together as a weighted average of the contributing snapshots.

Right-clicking a snapshot point opens its context menu with the following options:

- **Bind snapshot** binds this pad-position to the snapshot currently active on the host TRANSIT.
- **Unbind snapshot** clears the binding. An unbound snapshot point shows the label _No snapshot_ and does not contribute to the output.
- **Amount** slider — scales the snapshot's contribution. 0% silences the snapshot completely, 100% is the default.
- **Radius** slider — controls the radius of the area of influence. At 0% only the exact pixel-position contributes, at 100% the snapshot is active across the whole pad. The radius is visualized on the display as a filled circle around the point when the point is selected.

The number of snapshot points on the pad is configurable through the context menu of the display (right-click on the empty area): _Number of snapshots_ selects how many of A–H are active. The default is 4; the maximum is 8. Inactive points are not displayed and do not contribute to the output.

The mix point can be moved in several ways:

- **Mouse** — click and drag the `+`-marker on the pad.
- **CV inputs** _Mix x-pos_ and _Mix y-pos_ are bipolar 0..10V inputs: 0V places the mix at the center, +5V at the right/top edge, -5V at the left/bottom edge.
- **MIDI/CV-mapping** the two dummy map-buttons next to the pad. They expose the x- and y-coordinates of the mix point to VCV Rack's mapping system. There is no visual feedback on the buttons themselves, but mapping them lets you drive the mix point from any source — e.g. MIDI-CC from a controller or a CV-MAP output.
- **Motion-Sequences** — a recorded or generated trajectory that moves the mix point automatically. See the next section.

The mix point's value is smoothed internally to avoid clicks when the source jumps abruptly. After the source is disconnected the value slowly drifts back to the center of the pad.

#### Snapshot-sets

TRANSIT-PAD provides 8 snapshot-sets, each containing its own positions and weights for the 8 pad points. This way you can store complete layouts (e.g. _Drums_, _Bass_, _Pad_) and switch between them at any time.

The 8 buttons at the top of the module select the active set. The active set is indicated by a white LED above the button. Every set is assigned a fixed default color (cycling through green, magenta, blue, yellow, cyan, white, red, grey) and all snapshot points of the current set are rendered in that color. The color of a single set can be changed via the context menu of the set-button or via the context menu of any snapshot point on the pad (sub-menu _Color_ under _Current set_).

Each snapshot-set can be given a custom text label (e.g. _Drums_, _Bass_, _Pad_) to make the 8 buttons easier to tell apart at a glance. Right-click a set-button and choose _Label_ to enter a label.

A snapshot-set can also be selected by CV through the _Snapshot-set select CV_-input on the bottom-right of the module. The CV-mode is configured through the context menu of the display:

| Mode | Description |
|------|-------------|
| **Off** | The CV input is ignored; sets are selected only via the buttons on the module. |
| **Trigger forward** | A rising edge advances to the next snapshot-set, wrapping around after set 8. |
| **0..10V** | 0V selects set 1, 10V selects set 8, linearly interpolated in between. |
| **C4** | The set is selected by V/Oct: 0V = set 1, 1/12V per additional set. |

When the CV input is connected the buttons can still be used to manually override the active set, but the CV input takes over as soon as it carries a new value.

#### Motion-Sequences

The mix point can be animated by a _Motion-Sequence_: a path of up to 128 (x, y)-waypoints that are interpolated over time and drive the mix point automatically. Up to 16 motion sequences can be stored and any one of them can be active at a time. The currently selected sequence is shown on the two-digit LED-display below the pad.

Right-click on the LED-display to open the sequence's context menu:

- **Slot** selects one of the 16 sequences. The first 4 are populated with reasonable default paths on initialization.
- **Interpolation** selects _Linear_ or _Cubic_ interpolation between the waypoints. Cubic provides smoother but less precise motion.
- **Trigger mode** selects how the playback of the sequence is advanced. The mode is also reflected in the behaviour of the _Mix sequence select_ CV input:
  - _Trigger forward_ / _Trigger reverse_ — triggers step through the sequences.
  - _Trigger random 1-16_ / _1-8_ / _1-4_ — triggers select a random sequence from the available 16 / 8 / 4.
  - _0..10V_ — the sequence is selected by voltage (0V = sequence 1, 10V = sequence 16).
  - _C4-D#5_ — the sequence is selected by V/Oct across two octaves.

Clicking on the LED-display with the left mouse-button enters _SEQ-EDIT_ mode for the currently selected sequence: the display dims slightly and a red record-cursor appears. Click anywhere on the empty area of the pad to start recording, then drag the mouse to draw a path. Released waypoints are stored at ~65ms intervals, producing a smooth, hand-drawn motion of the mix point. The recorded path is drawn as a light line on the display for reference. Click on the LED-display again to exit SEQ-EDIT mode and resume normal pad operation.

Right-click on the pad while in SEQ-EDIT mode opens the full sequence-editor menu:

- **Clear** — remove all waypoints of the current sequence.
- **Flip horizontally** / **Flip vertically** — mirror the recorded path.
- **Rotate 45 degrees** / **Rotate 90 degrees** — rotate the path around the center of the pad.
- **Random motion** — replace the sequence with a randomly generated, low-pass-filtered path of variable length.
- **Preset** — replace the sequence with one of six built-in shapes: _Circle_, _Spiral_, _Saw_, _Sine_, _Eight_ and _Rose_. The _Scale x_ / _Scale y_ sliders stretch the shape horizontally/vertically, the _Parameter_ slider changes the shape's character (e.g. number of spiral arms or petals of the rose).
- **Copy** / **Paste** — copy the current sequence to/from any other sequence slot.

The _Mix sequence phase_ CV input controls the playback position of the currently selected sequence: 0V holds the mix point at the start of the path, 10V plays back the entire path from beginning to end. Intermediate values interpolate along the path. When this input is not connected, the mix point simply rests at the center of the pad.

The trigger mode and interpolation mode are global to all 16 sequences (TRANSIT-PAD has a single port, so the settings apply to the whole bank). The _Copy_ / _Paste_ actions in the sequence-editor menu only transfer the recorded waypoint data between sequences; they do not affect the modes.

#### Binding snapshots to pad positions

The first 4 snapshot points (A–D) are pre-bound to TRANSIT snapshots 1–4 (slot indices 0–3) on every snapshot-set, so a freshly-added TRANSIT-PAD is ready to use out of the box — the positions can simply be dragged to where you want the snapshots to live. Snapshot points E–H start out unbound and have to be assigned manually.

To bind or rebind a pad point to a different snapshot:

1. In Read-mode on TRANSIT, short-press a snapshot-button to load the snapshot you want to assign.
2. On the pad, right-click the point (A–H) and choose _Bind snapshot_.

The point is now linked to the chosen snapshot. The point only contributes to the output if the bound TRANSIT-snapshot is actually stored (i.e. its slot is _used_); binding an empty slot leaves the point dark. If the bound TRANSIT-snapshot has a custom label, that label is shown when hovering over the pad point.

To unbind, right-click a point and choose _Unbind snapshot_ — the point returns to its _No snapshot_ state and does not contribute to the output. The same menu also has an _Unbind snapshot_ entry, and the snapshot's own TRANSIT-slot is not affected.

In addition to the context-menu binding, a snapshot can be assigned to a pad point by **dragging the LED button** of the snapshot on TRANSIT (or on a +T expander) and **dropping it onto the pad**. This works on any of the snapshot buttons in the chain.

#### Visualize mode

A "visualize" mode is available to make the pad's snapshot bindings easier to understand. While the mode is active, a colored spline is drawn from the outer ring of every snapshot point on the pad to the corresponding LED button on the host TRANSIT (or the +T expander hosting the snapshot). The splines use the color of the current snapshot-set, and the line for each snapshot ends in a small dot on the destination button.

The mode is toggled with the **Space** key (no modifier). Press Space while hovering over the TRANSIT-PAD module and the splines appear. Press Space again to hide them. The mode is purely visual — it has no effect on the audio.

#### Context menu

Right-clicking on the empty area of the XY-display opens the following menu:

- **Initialize** — reset the entire module to factory defaults. All snapshot-set positions, motion-sequences, colors and bindings are cleared.
- **Randomize x-pos & y-pos** / **Randomize x-pos** / **Randomize y-pos** — randomly distribute the active snapshot points on the pad. Useful as a starting point for generative patches.
- **Randomize amount** — randomize the _Amount_ slider of each snapshot point.
- **Randomize radius** — randomize the _Radius_ slider of each snapshot point.
- **Number of snapshots** — select 1..8 active snapshot points.
- **Snapshot-set CV mode** — select _Off_, _Trigger forward_, _0..10V_ or _C4_ for the snapshot-set CV input.
- **Lock pad** — toggle a lock that prevents accidental edits: while locked, snapshot points and the _Mix_ point cannot be dragged to a new position, and snapshot buttons dragged from TRANSIT (or a +T expander) onto the pad no longer rebind. Dropping is still allowed to highlight a target (so the user can see where a drop would have landed), but the binding is rejected. The right-click _Bind snapshot_ and _Unbind snapshot_ entries on snapshot points are also disabled.


### Tips

- TRANSIT is designed to morph parameter-snapshots, while stoermelder [8FACE](../eightface/EightFace.md) and [8FACE mk2](../eightface/EightFaceMk2.md) are designed to apply different presets onto modules. Morphing between presets of modules is not possible for technical reasons.

- If you set the _OUT_-port to _Trigger fade stop_ and patch _OUT_ into _SEL_, TRANSIT will endlessly fade through snapshots.

- Each snapshot can be named with a custom text label. This label is shown while hovering above the snapshot button if parameter tooltips are enabled (added in v1.9.0).

- You can set a different first and/or last usable snapshot by right‑clicking a snapshot button and choosing _Set as first_ or _Set as last_. This makes it possible to use a contiguous subset of snapshot-slots for sequencing and CV selection.

- Parameter changes are not reported back to the plugin-host by default if TRANSIT is used in a plugin-version of VCV Rack. In v2.1.0 a context menu option has been added to enable this behavior - it might cause higher CPU usage of the plugin.

## Changelog

- v1.7.0
    - Initial release of TRANSIT and +T
- v1.7.1
    - Fixed wrong snapshot-count when using +T expander after loading a patch
- v1.8.0
    - Fixed hanging pingpong-mode when changing slots manually
    - Added trigger-options "pseudo-random", "random walk", "alternating", "shuffle"
    - Fixed broken snapshots on save after mapped modules have been deleted (#205)
- v1.9.0
    - Added "Phase"-mode for CV-input which scans continuously through snapshots (#182)
    - Added context menu option "Locate and indicate" for bound parameters
    - Added context menu option for custom text labels
    - Improved performance of +T expanders
- v1.10.0
    - Added context menu option for unbinding all bound parameters of a module (#268)
    - Added "Auto"-mode besides "Read" and "Write" (#269)
    - Added "Shift front" and "Shift back" context menu options (#274)
- v2.0.0
    - Added retrigger-function for CV-input channel 2 in C4 mode (#330)
    - Fixed premature end of processing and not reaching stored snapshot state (#329)
    - Fixed broken Auto/Write-modes if CV-port is set to "Phase" (#282)
    - Fixed broken reset-behavior for "Trigger forward", "Trigger reverse" and "Trigger pingpong" (#347)
    - Added missing reset-handling for "Trigger alternating" and "Trigger shuffle"
    - Added missing reset-handling for "Trigger random", "Trigger pseudo-random" and "Trigger random walk"
    - Allow disabling of "long press" for changing the number of active slots (#354)
    - Increased maximum number of expanders to 15 (#381)
    - Added fade setting per slot
    - Improved handling on mapped switches (skipping all immediate values)
    - Added context menu option to clean invalid bound parameters up (#383)
- v2.1.0
    - Added context menu option to report parameter updates to plugin-host (only in plugin-version of Rack)
- v2.2.0
    - Improved robustness for expander +T (e.g. crashes when using module-presets) (#412)
- v2.3.0
    - Added alternative parameter binding by selection box
    - Added custom color LED setting per slot
    - Added option to set the first usable snapshot (instead of starting at 1) (#265)
    - Added option to disable fade CV input clamping, allowing for more extreme fade times
- v2.5.0
    - Fade CV input is now additive to per-slot fade time (previously CV was only additive to the global _FADE_ knob)
    - Added Output-mode "Tipsy" for sending the snapshot text label (for modules with Tipsy-support like [TTY](https://library.vcvrack.com/StochasticTelegraph/TTY))
- v2.6.0
    - Fixed "Bind parameters by selection" when spanning multiple modules
- v2.x.0
    - Added new expander TRANSIT-PAD for 2-dimensional morphing between snapshots through an XY-pad with up to 8 snapshot-sets and 16 motion-sequences
