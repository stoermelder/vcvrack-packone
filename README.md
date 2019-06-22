# stoermelder PackOne

<!-- Version and License Badges -->
![Rack](https://img.shields.io/badge/Rack-1.0.0-red.svg?style=flat-square)
![Version](https://img.shields.io/badge/version-1.0.0-green.svg?style=flat-square)
![License](https://img.shields.io/badge/license-GPLv3-blue.svg?style=flat-square)
![Language](https://img.shields.io/badge/language-C++-yellow.svg?style=flat-square)

The PackOne plugin pack gives you a some utility modules for VCV Rack v1:

![Intro image](./docs/intro.png)

- [CV-MAP](./docs/CVMap.md): control 32 knobs/sliders/switches of any module by CV even when the module has no CV input
- [µMAP](./docs/CVMapMicro.md): single instance of CV-MAPs slots with attenuverters
- CV-PAM: generate CV voltage by observing 32 knobs/sliders/switches of any module
- [ReMOVE Lite](./docs/ReMove.md): a recorder for knob/slider/switch-automation
- [ROTOR Model A](./docs/RotorA.md): spread a carrier signal across 2-16 output channels using CV
- [BOLT](./docs/Bolt.md): polyphonic CV-modulateable boolean functions

All releases with odd numbering (1.0.1, 1.0.3) are preview-builds. Releases published in the [VCV Library](https://vcvrack.com/plugins.html#packone) are even numbered.

Feel free to contact me or create a GitHub issue if you have any problems or questions!

## Building

Follow the build instructions for [VCV Rack](https://vcvrack.com/manual/Building.html#building-rack-plugins).

## License

Copyright © 2019 Benjamin Dill

Licensed under the [GNU Public License, version 3](https://www.gnu.org/licenses/gpl-3.0.en.html).

The panel graphics in the `res` and `res-src` directories are licensed under CC BY-NC-ND 4.0. You may not create modified adaptations of these graphics.