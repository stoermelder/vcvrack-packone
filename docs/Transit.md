# stoermelder TRANSIT

TRANSIT is an utility module for morphing other module's parameters between one "snapshot" to another utilizing Rack's parameter-mapping functionality. The module provides 12 snapshot-slots and can be extended with up to seven extenders-modules called +T.

There is a nice [video overview](https://www.youtube.com/watch?v=qnjBrlkcYOw) from [Artem Leonov](https://artemleonov.bandcamp.com/) of _VCV Rack Ideas_ showcasing a preview-build of TRANSIT.

## Binding parameters

At least one parameter (knob, fader, switch...) must be bound to TRANSIT before the module can be used. It provides multiple ways for binding one or more parameters which need to be enabled on the contextual menu:

- **Bind module (left)**  
  Place any module in Rack to the left of TRANSIT. By selecting _Bind module (left)_ all parameters of the module get bound by TRANSIT.
- **Bind module (select)**
  Selecting _Bind module (select)_ turns the mouse pointer into a crosshair. After you click on the panel of a module all parameters of this module get bound by TRANSIT.
- **Bind single parameter**  
  Selecting _Bind single parameter_ turns the mouse pointer into a crosshair. After you click on a parameter of any module this parameter gets bound by TRANSIT.
- **Bind multiple parameters**
  This mode works the same way as _Bind single parameter_ except the mapping mode is not disabled automatically and multiple parameters can be bound in a row.

TRANSIT is designed to bind parameters from different modules the same time and technically there is no limitation on the number of parameters which can be bound. Please note that the CPU usage for morphing between snapshots increases linearly by the number of bound parameters.  
Parameters can be unbound at any point by unmapping the mapping indicator. Please note that values stored in scenes won't be deleted for unbound parameters.

## Saving snapshots in write-mode 

Write-mode is used to save snapshots in TRANSIT after some parameters have been bound: A snapshot consists of the values all bound parameters are currently set to. You enter write-mode by flipping the switch on the bottom to the _W_-position. To store a snapshot simply short press one of the 12 snapshot-buttons and the LED on a snapshot-button is lit in red when a slot is in use. To clear a snapshot long-press the button. There are also some options on the context menu:

- **Load**  
  Applies the snapshot to the parameters if the slot is used.
- **Clear**  
  Clears the snapshot.
- **Randomize**  
  Creates a randomized snapshot for currently bound parameters.
- **Copy**  
  Copies the snapshot to the clipboard.
- **Paste**  
  Pastes the snapshot which has been copied before.

A blinking white LED signals the snapshot applied at last on the parameters. Please keep in mind that you can change bound parameters manually which will not be recognized by TRANSIT.  
In write-mode any input on the SEL-port is ignored and sequencing is disabled.

## Morphing between snapshots

Read-mode is the default operational mode of TRANSIT and is used to "load" or "apply" previously saved snapshots on the bound parameters. The interesting part of TRANSIT is its ability to "morph" the parameter values into the target snapshot: _FADE_ sets the amount of time it takes to reach the parameters' positions stored in the snapshot, this duration can also be controlled by CV (0-10V). There is also a trimpot for setting the shape of the transition, in the middle position the parameters are morphed linearly.

## Sequencing and selecting snapshots

The fun begins when you use the port labelled _SEL_ for selecting snapshots by CV. Although there are 12 snapshot slots available it is possible to use less slots for sequencing: You can adjust the number of active slots by long-pressing a snapshot-button while in read-mode. The LEDs turns off completely for slots that are currently disabled.

- **Trigger forward**  
  A trigger advances TRANSIT to the next snapshot. Empty slots are part of the sequence but won't have any effect on the bound parameters. A trigger on _RESET_ restarts the sequence on snapshot 1.
- **Trigger reverse**  
  Same as "Trigger forward", but reverse direction. 
- **Trigger pingpong**  
  Same as "Trigger forward", but loops first forward then reverse.
- **Trigger random**  
  Same as "Trigger forward", but chooses the next snapshot randomly.
- **0..10V**  
  You can select a specific snapshot by voltage. A voltage 0-0.833V selects slot 1, 0.833-0.166V selects slot 2, and so on if all 12 snapshot-slots are active. Keep in mind that adjusting the length of the sequence also adjusts the voltage range for selecting individual slots: A sequence with length 2 will select slot 1 on voltage 0-5V etc.
- **C4**  
  This mode follows the V/Oct-standard. C4 selects snapshot 1, C#4 selects snapshot 2 and so on.
- **Arm**  
  This mode is a kind of "buffered trigger": First apply a clock signal on _SEL_. Then you "arm" any snapshot manually or by MIDI-mapping by its button (resulting in a yellow LED) which will be activated on the next clock trigger (white LED). This mode allows you manual snapshot changes synchronized to a clock.

TRANSIT was added in v1.7.0 of PackOne.

# T+ expander

TRANSIT provides 12 snapshot-slots and supports extending this number with +T expanders: The expander must be placed on the right of TRANSIT. Up to seven instances of +T are supported for one instance of TRANSIT, providing in sum 12 * 8 = 96 snapshot-slots.  
Once placed next to TRANSIT the expander works and behaves the same way TRANSIT does and the setup works the same way. +T itself provides no further options.

+T was added in v1.7.0 of PackOne.