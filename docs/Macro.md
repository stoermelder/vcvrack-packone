# stoermelder MACRO

MACRO is an utility module for controlling four different parameters at once. It has also two CV output ports controlled the same way and it provides built-in slew-limiting and scaling options, adjustable for every parameter and every port.

![MACRO intro](./Macro-intro.png)

## Mapping parameters

You can bind up to four parameters of any module in your patch. You do this by activating a mapping button by click. While mapping is active the led is lit in red and turns green after you click onto a knob or fader of the module you like to control, also a small pink mapping indicator is shown next to the parameter.

## Slew-limiting and scaling

Each mapping slot has its own setting for slew-limiting of the input value which applies an exponential filter. Small values for _Slew_ are smoothing incoming values and larger values  give an overall steady movement of the mapped parameters or CV outputs on fast input changes.

Each mapping slot and CV output has also two sliders (_Low_ and _High_) for scaling input values which allows you to adjust the range and how the mapped parameter is affected. By setting the two sliders accordingly almost any linear transformation is possible, even inverting a parameter. For convenience some presets are provided and the current scaling transformation is shown on the context menu.

![MACRO context](./Macro-scaling.gif)

## IN-port

The IN-port is optional in use and MACRO can be used leaving it unconnected. In this case the input value is taken from the big knob's current position. If voltage is applied to IN the knob acts as an attenuator, also the voltage range can be switch from unipolar (0..10V) to bipolar (-5..5V) on the context menu.

## Additional features

Mapping parameters can result in quite high CPU usage. As controlling parameters at audio rate is not needed in most of the cases you can switch four different performance settings using _Precision_ option on the context menu.

MACRO was added in v1.8.0 of PackOne.