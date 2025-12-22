# stoermelder CV‑PAM

CV‑PAM is the counterpart of [CV‑MAP](./CVMap.md). While CV‑MAP takes CV input and automates any knob, switch or slider, CV‑PAM does the opposite: you attach one of its 32 slots to a parameter of any module and it generates CV output from the parameter’s movements. The use‑cases for CV‑PAM are therefore not as varied as those for [CV-MAP](./CVMap.md).

![CV-PAM intro](./CVPam-intro.png)

Using CV‑PAM in combination with CP‑MAP lets you synchronize parameter changes across multiple instances of the same module (or different modules). For an all‑in‑one solution you can also use [MIRROR](./Mirror.md) (since v1.6.0).

![CV-PAM sync](./CVPam-sync.gif)

### Additional features

- Text scrolling of the mapping slots can be disabled  (since v1.0.2).

- **CPU usage** – Mapping many parameters can increase CPU usage. If audio‑rate automation is not required, disable _Audio rate processing_ from the context menu. This updates the mapped parameter only every 32nd audio sample, reducing CPU usage to roughly 1/32 (since v1.4.0).
- **Pink mapping indicators** – If the pink indicators are distracting, disable them from the context menu (since v1.5.0).
- **Lock mapping slots** – Prevent accidental changes by enabling the _Lock mapping slots_ option, which locks the mapping‑slot widgets (since v1.5.0).
- **Blinking indicator** – A blinking indicator shows the bound parameter of the currently selected mapping slot (since v1.7.0).
- **Scrolling interference** – Hovering over CV‑PAM’s list widget interrupts Rack view mouse scrolling. Enabling *Lock mapping slots* suppresses all mouse scrolling events while hovered (since v1.7.0).

## Changelog

CV‑PAM was introduced in v1.0 of PackOne.