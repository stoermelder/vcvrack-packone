# stoermelder INTERMIX

- [Basic Usage](#basic-usage)
- [Matrix and Pads](#matrix-and-pads)
- [Input Modes](#input-modes)
- [Scenes](#scenes)
- [Expanders](#expanders)
- [Settings](#settings)
- [Tips](#tips)
- [Changelog](#changelog)

### Overview

INTERMIX is a precision adder matrix featuring 8 inputs and 8 outputs. The module allows you to mix any combination of inputs and route them to any output using a grid of toggle pads. INTERMIX supports scene-based preset storage with smooth fading between scenes and multiple input modes including constant voltages.

- 8x8 signal routing matrix with toggle pads
- 8 distinct scenes for storing configurations
- Smooth fading between scenes with adjustable fade-in and fade-out times
- Multiple input modes (direct, linear fade, constant voltages)
- Output attenuverters for each of the 8 outputs
- Optional visualization of input voltage on the pads
- Full polyphony support
- Very low CPU usage

### Basic Usage

INTERMIX works as a matrix mixer where each output is a sum of selected inputs. To route a signal:

1. Locate the intersection of the desired input (column) and output (row)
2. Click the pad at that intersection to toggle it on (yellow) or off
3. The output will now include the selected input signal
4. Adjust the output attenuverter knob to scale the signal

Each output can mix any number of inputs simultaneously, and each input can be routed to multiple outputs.

### Matrix and Pads

The center grid displays the 8x8 routing matrix with pads representing each input-to-output connection:

- **Yellow pads** - Signal is routed (active)
- **Dark pads** - Signal is not routed (inactive)
- **Red row button** (far right) - Disables the entire output row
- **Pad brightness** - Configure this in the context menu to adjust visibility

When _Visualize input on pads_ is enabled in the context menu, the brightness of each pad reflects the current voltage level of the corresponding input, making it easy to see signal activity.

### Input Modes

Each of the 8 inputs can be configured with a different mode by right-clicking on the input display. Available input modes are:

**Mixing Modes**
- **Off** - Input signal is ignored
- **Direct** - Input signal is passed directly to the matrix (default mode)
- **Linear fade** - Input signal fades smoothly when scenes are changed (uses FADE-expander settings if available)

**Constant Voltage Modes**

INTERMIX can generate constant voltages offset from zero, useful for adding musical intervals or bias voltages:

- **Subtract modes** - Generate negative voltages from -12 cents to -1 cent
- **Add modes** - Generate positive voltages from +1 cent to +12 cents

These constant voltages can be mixed and routed just like input signals, enabling precise pitch shifting or voltage offset applications.

### Scenes

INTERMIX provides 8 scenes for storing different matrix configurations, output settings, and input modes:

1. Configure the matrix pads, input modes, and output settings as desired
2. Short-press one of the 8 scene buttons on the left (numbered 1-8)
3. The scene LED turns yellow, indicating that scene is now active
4. All current settings are stored in that scene

Loading scenes in read-mode:
1. Short-press any scene button to load that scene
2. The LED lights yellow for the active scene
3. The scene may fade smoothly into place (if FADE is configured)

Long-press a scene button to clear it and remove all stored settings.

The attenuverter knob can be included in scenes using the _Include attenuverters in scenes_ context menu option.

**Scene Lock**
Use the context menu option _Scene lock_ to prevent accidental changes to scenes while keeping all buttons fully functional - useful during live performance. When locked, all scene buttons and output buttons remain active, but matrix pads cannot be edited. This is useful when performing with INTERMIX to avoid unintended changes. Scene lock prevents accidental matrix changes 

**Scene Configuration**
The behavior of scenes can be customized via context menu:

- **Include input-mode in scenes** - When enabled, each scene stores its own input modes. When disabled, all scenes share the same input modes.

- **Include attenuverters in scenes** - When enabled, each scene stores its own output attenuverter settings. When disabled, all scenes share the same attenuverter values.

- **Port _SCENE_-mode** - Select how the _SCENE_ input port behaves:
  - **Off** - Port is disabled
  - **Trigger** - Trigger input advances to the next scene (default)
  - **0..10V** - Voltage 0-1.25V selects scene 1, 1.25-2.5V selects scene 2, etc.
  - **C4-G4** - V/Oct standard: C4 selects scene 1, C#4 selects scene 2, etc.
  - **Arm** - Buffered trigger mode: arm a scene with a trigger, load it on next clock

INTERMIX can smoothly crossfade between scenes using the two fade trimpots:

- **Fade in** (left trimpot) - Time in seconds for the new scene to fade in
- **Fade out** (right trimpot) - Time in seconds for the previous scene to fade out

The maximum fade time can be set in the context menu (4s, 15s, or 60s). These settings apply to all scene changes and can be included in scenes if "Include input-mode in scenes" is enabled. For more advanced fade control per pad, use the INTERMIX-FADE expander.

### Fade Input Port

The _Scene selection_ input port allows triggering scene changes via CV. The behavior depends on the _Port SCENE-mode_ setting in the context menu.

## Expanders

INTERMIX supports three expander modules that extend its functionality. Expanders must be placed on the right side of INTERMIX (or chained after other expanders).

### INTERMIX-GATE Expander

The INTERMIX-GATE expander outputs a 10V gate signal whenever at least one pad in a row is active. This is useful for generating gate signals that correspond to which outputs are currently active.

![INTERMIX-GATE expander](./Intermix-gate.gif)

### INTERMIX-ENV Expander

The INTERMIX-ENV expander outputs envelope or gate signals for each input column. When fading is enabled, it outputs envelope shapes following the fade curves; otherwise it outputs simple gate signals.

![INTERMIX-ENV expander](./Intermix-env.gif)

- **Fade-aware** - If _FADE_ is enabled and an input uses "Linear fade" mode, outputs an envelope shape
- **Column selection** - The expander tracks the currently selected column

### INTERMIX-FADE Expander

The INTERMIX-FADE expander provides individual fade-in and fade-out control for each pad of a specific input column. This allows precise timing control for each input when using linear fade mode.

![INTERMIX-FADE expander](./Intermix-fade.gif)

1. Select which input column the expander should control (shown on the expander display)
2. For each pad in that column, set custom fade-in and fade-out times
3. Supports fade-in only, fade-out only, or simultaneous fade-in and fade-out

The maximum fade time can be set in the context menu (4s, 15s, or 60s). The default is 15s.

**Note:** When the INTERMIX-FADE expander is detached or reconfigured for a different input column, all fade settings return to the default values from the main module.

### Settings

- **Limit output to -10..10V** - When enabled, output voltages are clamped to -10V..+10V range (default: on)
- **Channels** - Select the number of polyphonic channels to process (1-16)
- **Pad brightness** - Adjust the brightness of the matrix pads (default: 75%)

### Tips

- The context menu option _Viualize input on pads_ displays input voltage levels on the pads, making it easy to see signal activity

- Each input can be set to constant voltage modes to add musical intervals (semitones in ±12 cent steps)

- Polyphony is supported: use the _Channels_ option in the context menu to process up to 16 channels

- Attenuverters can be excluded from scene storage if you want to keep them consistent across scenes

## Changelog

- v1.4.0
    - Initial release
- v1.5.0
    - Added matrix mapping parameters for rows and columns to support MIDI mapping
    - Added option to exclude attenuverters from scenes
    - Added ability to copy scenes
    - Added ability to reset scenes
    - Added option to disable the SCENE port
    - Added option to change the number of active scenes
    - Fixed broken fading when either fade-in or fade-out is set to zero
- v1.8.0
    - Added support for polyphony (#199)
- v1.10.0
    - Added context menu option "Scene lock" to prevent accidental changes
    - Added expander INTERMIX-FADE
    - Added expander INTERMIX-ENV
    - Added expander INTERMIX-GATE
- v2.4.0
    - Added fade length setting (4s, 15s, 60s) (#432)