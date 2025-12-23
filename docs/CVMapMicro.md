# stoermelder µMAP

µMAP is a single slot of [CV-MAP](./CVMap.md) with attenuation and output port. Please refer to the manual of [CV-MAP](./CVMap.md) how this module works.

![µMAP Intro](./CVMapMicro-intro.png)

### _OFFSET_

Using the trimpod the input signal can be offset in the range 0% to 100%. Connecting a cable to the offset port disables the trimpod. It expects a voltage range 0..10V.

### _SCALE_

The input signal can be scaled using the trimpod from –200% to 200% while its default value is 100%. Connecting a cable to the **SCALE**-port disables the trimpod. It expects voltage –10V to +10V which is mapped to scaling –200% to 200%.

### _OUT_

µMAP has an _OUT_ port that outputs the CV of the currently selected mapped parameter value, similar to the function of [CV‑PAM](./CVPam.md). This port follows the voltage range selected for the **INPUT**-port and can be inverted by option on the context-menu (since v1.2.0).

### Tips

<a name="target-context"></a>
- After a parameter has been mapped the parameter's context menu is extended with some additional menu items allowing quick adjustments and centering its mapping, the µMAP module, to the center of the screen (since v1.9.0).

![µMAP context menu](./CVMapMicro-target.png)

- By default, parameter changes are not reported back to the plugin host when µMAP is used in a plugin version of VCV Rack. In v2.2.0, a context menu option was added to enable this behavior, which may increase CPU usage.

µMAP was added in v1.0.2 of PackOne.