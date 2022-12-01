# stoermelder 8FACE and 8FACEx2

8FACE is a module for storing, recalling and sequencing up to eight different presets of any module in Rack. It uses Rack's expander mechanism to attach to any module to its side and uses buttons and LEDs to manage each of its preset-slot. 8FACEx2 offers sixteen preset slots.

**Disclaimer: Loading presets of modules was not designed to be controlled by CV or modulated at audio rate. Please do not contact the developers of Rack or any modules when unexpected behaviour (i.e. crashes) occurs or high CPU usage is noticeable.**

![8FACE Intro](./EightFace-intro.gif)

### Amazing tutorial video from [Omri Cohen](https://omricohencomposer.bandcamp.com/)

<a href="https://www.youtube.com/watch?v=S2j6W2nvuC8" target="_blank"><img src="https://img.youtube.com/vi/S2j6W2nvuC8/0.jpg" style="width:100%" /></a>

## Usage

Place 8FACE on the right side next to the module you like to manage. The triangle shaped LED begins to flash if a connection is established successfully. You can detach 8FACE and re-attach to another instance of the same module. When you place 8FACE next to a module and the LED turns red it means it had been configured for another model. In this case you can either check the model on the context menu or initialize 8FACE to its initial state. Since v1.2.0 you can place 8FACE on the left side of a module after changing the according setting on the context menu.

## Write-mode

Write-mode is used to save presets in 8FACE. You enter write mode by flipping the switch on the bottom to the "W"-position. To store a preset simply configure your module next to 8FACE and then short press a slot-button numbered 1 to 8. The LEDs next to the slot-button turn red when a slot is already in use. To clear a slot long-press a button. In write-mode any input on the SLOT-port is ignored and sequencing is disabled.

## Read-mode

Read-mode is enabled by default and can be selected by the switch on the bottom in "R"-position. LEDs lit in bright green signal slots in use, dim green slots are active but empty. A blue LED marks a slot which preset is currently applied to the module on the side. You can manually apply a preset with a short-press.

## Auto-mode

Auto-mode (added in v1.10.0) stores presets automatically to the current slot right before moving on to the next slot. A typical workflow would look like this: Store a few presets using Write-mode as usual. Afterwards flip the switch to the middle "A"-position and start slow sequencing using the _SLOT_-port. Imagine slot 1 is active and slot 2 will be loaded next. Right before moving to slot 2 the current state of the module is stored into slot 1 preserving all adjustments made in the meantime. In contrast, Read-mode would simply load slot 2 and the preset stored in slot 1 will stay unchanged, discarding all changes made to the module. Note: Empty slots will stay empty, even in Auto-mode.

## SLOT-port

The fun begins when you use the port labelled "SLOT" for selecting preset slots by CV. Although there are eight slots available it is possible to use less slots for sequencing: You can adjust the number of useable slots (i.e. sequence length) by long-pressing a slot-button while in read-mode. The LED turns off completely for slots that are currently disabled.

![8FACE sequencing](./EightFace-context.png)

<a name="trigger-modes"></a>
There are different modes for SLOT-port available, configured by context menu option:

- **Trigger forward**: A trigger on SLOT advances 8FACE to the next slot. Empty slots are part of the sequence but won't have any effect on the controlled module.

![8FACE sequencing](./EightFace-trig.gif)

- **Trigger reverse** (added in v1.1.0): Same as "Trigger forward" but in reverse direction.

- **Trigger pingpong** (added in v1.1.0): Same as "Trigger forward" but loops first forward then reverse.

- **Trigger alternating** (added in v1.8.0): Same as "Trigger forward" but progresses in the following manner (for 6 active slots): 1, 2, 1, 3, 1, 4, 1, 5, 1, 6, 1, 5, 1, 4, 1, 3, 1, 2, ...

- **Trigger random** (added in v1.1.0): Same as "Trigger forward", but chooses the next preset randomly.

- **Trigger pseudo-random** (added in v1.8.0): Same as "Trigger random" but never chooses a slot multiple times in a row (which happens on "random").

- **Trigger random walk** (added in v1.8.0): Same as "Trigger forward" but chooses the next slot randomly right next to the currently active slot.

- **Trigger shuffle** (added in v1.8.0): Same as "Trigger forward" but works on a random permutation of the active slots: Every slot will be enabled once before the next permutation is randomly generated.

- **0..10V**: You can select a specific slot by voltage. A voltage 0-1.25V selects slot 1, 1.25-2.5V selects slot 2, and so on if all eight slots are active. Keep in mind that adjusting the length of the sequence also adjusts the voltage range for selecting individual slots: A sequence with length 2 will select slot 1 on voltage 0-5V etc.

- **C4**: This mode follows the V/Oct-standard. C4 selects slot 1, C#4 selects slot 2 and so on. Channel 2 on the SLOT-input acts on triggers to re-trigger the currently selected snapshot.

- **Arm** (renamed from "Clock" in v1.1.0): This mode is a kind of buffered trigger: First apply a clock signal on SLOT. Then you "arm" any slot manually or by MIDI-mapping by its button (resulting in a yellow LED) which will be activated on the next clock trigger (blue LED). This mode allows you manual preset changes synchronized to a clock.

![8FACE arm mode](./EightFace-clock.gif)

With the option "Autoload first preset" on the context menu you can autoload the first preset slot when a preset of 8FACE itself is loaded. This is useful when changing presets of 8FACE with another instance of 8FACE to aquire even more preset slots. The option "Autoload last active preset" works the same way.

## Changelog

- v1.0.5
    - Initial release of 8FACE
- v1.1.0
    - Using additional worker thread for applying presets to avoid engine deadlock on some modules (especially using parameter mapping)
    - Added trigger modes "reverse", "pingpong" and "random" for SLOT-port
    - Renamed "Clock"-mode to "Arm" for SLOT-port
    - Added option to autoload first preset on load of 8FACE presets
    - Fixed unusable SLOT-modes "0..10V" and "C4..G4"
- v1.2.0
    - Added option to switch between left and right side controlled module (#50)
    - Follow voltage standards for Rack (ignore SLOT for 1ms after trigger on RESET)
- v1.3.0
    - Intial release of 8FACEx2
    - Revised panel design with combined LED and buttons
- v1.8.0
    - Fixed hanging pingpong-mode when changing slots manually (#191, #203)
    - Added trigger-options "pseudo-random", "random walk", "alternating", "shuffle" ([manual](./EightFace.md#trigger-modes))
- v1.9.0
    - Load preset in Arm-mode even when the same slot was selected before (#212)
    - Improved thread-handling for crashes when used with specific modules (#76)
    - Added an option for auto-loading the last active preset
    - Added "Off" as SLOT mode (#249)
    - Fixed broken "Autoload first preset" (#29)
- v1.10.0
    - Added "Auto"-mode besides "Read" and "Write" ([manual](./EightFace.md#auto-mode)) (#251)
    - Added "Shift front" and "Shift back" context menu options (#275)
- v2.0.0
    - Added retrigger-function for CV-input channel 2 in C4 mode (#330)
    - Fixed unconnected modules after patch reload (#338)
    - Fixed broken reset-behavior for "Trigger forward", "Trigger reverse" and "Trigger pingpong" (#347)
    - Added missing reset-handling for "Trigger alternating" and "Trigger shuffle"