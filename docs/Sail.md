# stoermelder SAIL

SAIL is an utility module. It allows you to control any module parameter which is currently hovered by the mouse pointer with CV.

![SAIL intro](./Sail-intro.png)

This module is especially helpful if used together with VCV MIDI-CC: You can map a specific MIDI CC and change any parameter currently hovered with the same control on your MIDI device. The indicator-LED on top of the module signals whenever a parameter is currently hovered.

### IN-port

There are two modes available on the context menu for the IN-port:  
In "Differential" mode the delta changes in voltage are determined and applied on the current value of the hovered parameter. If the SHIFT-key is held or the MOD-port is high (>=1V) the parameter control precision is increased by factor 10.  
In "Absolute" mode the input voltage (range 0..10V) is directly mapped onto the parameter's range. 

![SAIL OUT-port](./Sail-absolute.gif)

### INC/DEC-ports

SAIL supports triggers for incrementing and decrementing the current value of the hovered parameter, STEP sets the change on every trigger in percent of the full range. If INC/DEC is connected the IN-port will be ignored. These ports are useful when connected to [MIDI-STEP](./MidiStep.md) which outputs triggers for endless rotary knobs on MIDI controllers.

### SLEW

The SLEW-parameter and input-port can be used to smoothen changes applied to the hovered parameter and make some less abrupt.

![SAIL slew](./Sail-slew.gif)

### OUT-port

Additionally the module can convert the current value of the hovered parameter to voltage on the OUT-port. In "Reduced" mode only parameter changes made by SAIL itself will update the output voltage, in "Full" mode all changes (by mouse or MIDI-mapping) will update the output voltage.

![SAIL OUT-port](./Sail-out.gif)

### Bonus tips

* You can use multiple instances of SAIL with different STEP-sizes for the INC/DEC-ports.
* You can use multiple instances of SAIL, one with FINE permanently set to a high voltage and patched to different CC of VCV MIDI-CC. This way one MIDI controller knob can be used for coarse control and one knob for fine control.

SAIL was added in v1.5.0 of PackOne.