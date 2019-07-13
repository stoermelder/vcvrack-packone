# stoermelder 8FACE

8FACE is a module for storing, recalling and sequencing up to eight different presets of any module in Rack. It uses Rack's expander mechanism to attach to any module to its left and uses buttons and LEDs to manage each of its preset-slot.

An important advice: Loading presets of modules were not designed to be controlled by CV or modulated at audio rate. Please do not contact the developers of Rack or any modules when unexpected behaviour occurs or high CPU usage is noticable.

### Usage

Place 8FACE next on the right to the module you like to manage. The LED between the triangles begins to flash if a connection is established successfully. You can detach 8FACE and re-attach to another instance of the same module. When you place 8FACE next to a module and the LED turns red it means it had been configured for another model. In this case you can either check the model on the context menu or initialize 8FACE to its initial state.

### Write-Mode

Write-mode is used to save presets in 8FACE. You enter write mode by flipping the switch on the bottom to the "W"-position. To store a preset simply configure your module left of 8FACE and then short press a slot-button numbered 1 to 8. The LEDs next to the slot-button turn red when a slot is already in use. To clear a slot long-press a button.

### Read-Mode

Read-mode is enabled by default and can be selected by the switch on the bottom in "R"-position. In read-mode LEDs that are lit in green signal slots that are in use. A blue LED marks the preset currently applied to the module on the left. You can manually select a preset with a short-press on a slot-button.

### SLOT-port

The fun begins when you use the port labelled "SLOT" for selecting different slots by CV. Although there are eight slots available it is possible to use less slots for sequencing: You can adjust the number of useable slot by long-pressing a slot-button while in read-mode. LED turns off completely for slots that are currently disabled.

There are different modes available, configured by a context menu option:

- Seq Trigger:
A trigger on SLOT advances 8FACE to the next slot. Empty slots are part of the sequence but won't have any effect on the controlled module.
- Seq 0..10V:
You can select a specific slot by voltage. A voltage 0-1.25V selects slot 1, 1.25-2.5V selects slot 2, and so on if all eight slots are active. Keep in mind that adjusting the length of the sequence also adjusts the voltage range for selecting individual slots: A sequence with length 2 will select slot 1 with voltage 0-5V etc.
- Seq C4-G8: 
This mode follows the V/Oct-standard. C4 selects slot 1, C#4 selects slot 2 and so on.
- Clock:
This mode works similar to sample on hold. You first apply a clock signal on SLOT. Then you trigger any slot manually by button (resulting a yellow LED) which will be applied on the next clock trigger (blue LED).