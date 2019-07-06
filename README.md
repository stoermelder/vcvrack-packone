# stoermelder PackOne

<!-- Version and License Badges -->
![Version](https://img.shields.io/badge/version-1.0.4-green.svg?style=flat-square)
![Rack SDK](https://img.shields.io/badge/Rack--SDK-1.1.1-red.svg?style=flat-square)
![License](https://img.shields.io/badge/license-GPLv3-blue.svg?style=flat-square)
![Language](https://img.shields.io/badge/language-C++-yellow.svg?style=flat-square)

The PackOne plugin pack gives you a some utility modules for [VCV Rack](https://www.vcvrack.com).

![Intro image](./docs/intro.png)

- [CV-MAP](./docs/CVMap.md): control 32 knobs/sliders/switches of any module by CV even when the module has no CV input
- [µMAP](./docs/CVMapMicro.md): a single instance of CV-MAP's slots with attenuverters
- [CV-PAM](./docs/CVPam.md): generate CV voltage by observing 32 knobs/sliders/switches of any module
- [ReMOVE Lite](./docs/ReMove.md): a recorder for knob/slider/switch-automation
- [ROTOR Model A](./docs/RotorA.md): spread a carrier signal across 2-16 output channels using CV
- [BOLT](./docs/Bolt.md): polyphonic CV-modulateable boolean functions
- [INFIX](./docs/Infix.md): insert for polyphonic cables
- [STRIP](./docs/Strip.md): manage a group of modules in a patch, providing load, save as, disable and randomize

Release versions are published in the [VCV Library](https://vcvrack.com/plugins.html#packone).

Feel free to contact me or create a GitHub issue if you have any problems or questions!

## Building

Follow the [build instructions](https://vcvrack.com/manual/Building.html#building-rack-plugins) for VCV Rack.

## License

Copyright © 2019 Benjamin Dill

Licensed under the [GNU Public License, version 3](https://www.gnu.org/licenses/gpl-3.0.en.html).

The panel graphics in the `res` and `res-src` directories are licensed under CC BY-NC-ND 4.0. You may not create modified adaptations of these graphics.