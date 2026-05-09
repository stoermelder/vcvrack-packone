# stoermelder PackOne

<!-- Version and License Badges -->
![Version](https://img.shields.io/badge/Version-2.3.1-green.svg?style=flat-square)
![Rack](https://img.shields.io/badge/VCV_Rack-v2-red.svg?style=flat-square)
![MetaModule](https://img.shields.io/badge/MetaModule-v2-orange.svg?style=flat-square)
![License](https://img.shields.io/badge/License-GPLv3+-blue.svg?style=flat-square)
![Language](https://img.shields.io/badge/Language-C++-yellow.svg?style=flat-square)

**The PackOne plugin gives you some modules for [VCV Rack](https://www.vcvrack.com). Stable versions are released in the [VCV Library](https://library.vcvrack.com/?brand=stoermelder). [Selected modules](./MetaModule.md) are also available on the [4ms MetaModule](https://4mscompany.com/p.php?p=990)!**

A [Development build](https://github.com/stoermelder/vcvrack-packone/releases/tag/Nightly) of the latest commit is also available for all platforms. Please review the [changelog](./CHANGELOG.md) for this plugin.

If you like my modules consider donating to https://paypal.me/stoermelder, but don't feel obligated to do so. Thank you for your support!

## The modules of PackOne

- [4ROUNDS](./docs/fourrounds/FourRounds.md): randomizer for up to 16 input signals to create 15 output signals
- [8FACE, 8FACEx2](./docs/eightface/EightFace.md): preset sequencer for eight or sixteen presets of any module working as an universal expander
- [8FACE mk2, +8](./docs/eightface/EightFaceMk2.md): evolution and replacement for 8FACE and 8FACEx2
- [AFFIX](./docs/affix/Affix.md), [µAFFIX](./docs/affix/Affix.md): inserts for polyphonic cables for adding offsets in volt, semitones or octaves
- [AHAB](./docs/ahab/Ahab.md): A live programming environment for ORCA, an esoteric programming language designed to quickly create procedural sequencers
- [ARENA](./docs/arena/Arena.md): 2-dimensional XY-Mixer for 8 sources with various modulation targets and fun graphical interface
- [BOLT](./docs/bolt/Bolt.md): polyphonic CV-modulateable boolean functions
- [CV-MAP](./docs/cvmap/CVMap.md): control 32 knobs/sliders/switches of any module by CV even when the module has no CV input
- [CV-MAP CTX](./docs/cvmap/CVMap.md#ctx-expander): expander-module for CV-MAP, helper for mapping parameters by context menu
- [CV-PAM](./docs/cvmap/CVPam.md): generate CV voltage by observing 32 knobs/sliders/switches of any module
- [DIRT](./docs/dirt/Dirt.md): a module for noise, crackles and crosstalk on polyphonic cables
- [GLUE](./docs/glue/Glue.md): label maker for your modules!
- [GOTO](./docs/goto/Goto.md): utility for jumping directly to 10 locations in your patch by hotkey or using MIDI
- [GRIP](./docs/cvmap/Grip.md): lock for module parameters
- [HIVE](./docs/maze/Hive.md): 4 channel sequencer running on a 2-dimensional hexagonal grid, similar to [MAZE](./docs/maze/Maze.md)
- [INFIX](./docs/infix/Infix.md), [µINFIX](./docs/infix/Infix.md): insert for polyphonic cables
- [INTERMIX](./docs/intermix/Intermix.md): precision adder 8x8 advanced switch matrix with support for 8 scenes
- [INTERMIX-FADE](./docs/intermix/Intermix.md#fade-expander): expander-module for INTERMIX, helper for setting individual fade values
- [INTERMIX-ENV](./docs/intermix/Intermix.md#env-expander): expander-module for INTERMIX, outputs envelopes for a seleced input-column
- [INTERMIX-GATE](./docs/intermix/Intermix.md#gate-expander): expander-module for INTERMIX, outputs a gate-signal for each active row (#228)
- [MACRO](./docs/cvmap/Macro.md): marco-knob for four parameter-mappings and two CV outputs
- [MAZE](./docs/maze/Maze.md): 4 channel sequencer running on a 2-dimensional grid
- [MB](./docs/trial-and-error/Mb.md): experimental replacement for Rack's module browser, formerly available in [stoermelder's PackTau](https://github.com/stoermelder/vcvrack-packtau)
- [ME](./docs/trial-and-error/Me.md): experimental module for "mouse enhacements", provides a screen overlay for parameters changes
- [µMAP](./docs/cvmap/CVMapMicro.md): a single instance of CV-MAP's slots with attenuverters
- [MIDI-CAT](./docs/midicat/MidiCat.md): map parameters to midi controllers similar to MIDI-MAP with midi feedback and note mapping
- [MIDI-CAT CLK](./docs/midicat/MidiCat.md#clk-expander): expander for MIDI-CAT, allows trigger-quantization for mapped parameters (#299)
- [MIDI-CAT CTX](./docs/midicat/MidiCat.md#ctx-expander): expander-module for MIDI-CAT, helper for mapping parameters by context menu
- [MIDI-CAT FINE](./docs/midicat/MidiCat.md#fine-expander): expander for MIDI-CAT, allows fine-tuning of MIDI CC parameter-mappings
- [MIDI-CAT MEM](./docs/midicat/MidiCat.md#mem-expander): storage-expander for MIDI mapping-presets with MIDI-CAT
- [MIDI-CAT XL](./docs/midicat/MidiCat.md): a wider panel version of MIDI-CAT with identical features and driver selection moved to the context menu
- [MIDI-KEY](./docs/midi/MidiKey.md): utility for generating keyboard events from MIDI CC or Note messages
- [MIDI-MON](./docs/midi/MidiMon.md): input-monitor for MIDI messages
- [MIDI-PLUG](./docs/midi/MidiPlug.md): a virtual MIDI merger and splitter
- [MIDI-STEP](./docs/midi/MidiStep.md): utility for relative modes of endless knobs on your MIDI controller such as Arturia Beatstep
- [MIRROR](./docs/cvmap/Mirror.md): utility for synchronizing module parameters
- [ORBIT](./docs/orbit/Orbit.md): a polyphonic stereo field spreader
- [PILE, POLY-PILE](./docs/pile/Pile.md): utility which translates increment triggers or decrement triggers in an absolute voltage, especially useful with MIDI-STEP
- [RAW](./docs/raw/Raw.md): a digital effect based on the dynamics of bistable systems
- [ReMOVE Lite](./docs/cvmap/ReMove.md): a recorder for knob/slider/switch-automation
- [ROTOR Model A](./docs/rotor/RotorA.md): spread a carrier signal across 2-16 output channels using CV
- [SAIL](./docs/sail/Sail.md): control any parameter currently hovered by mouse with CV, especially useful with MIDI-CC
- [SIPO](./docs/sipo/Sipo.md): serial-in parallel-out shift register with polyphonic output and CV controls
- [SPIN](./docs/spin/Spin.md): utility for converting mouse-wheel or middle mouse-button events into triggers
- [STROKE](./docs/stroke/Stroke.md): utility which converts used-defined hotkeys into triggers or gates, also provides some special commands for Rack's enviroment
- [STRIP](./docs/strip/Strip.md): manage a group of modules in a patch, providing load, save as, disable and randomize
- [STRIP-BAY](./docs/strip/Strip.md#stoermelder-strip-bay): a companion module for STRIP for keeping input/output connections while replacing strips
- [STRIP++](./docs/strip/StripPp.md): utility for pasting and importing Rack selections while preserving parameter mappings and [GLUE](./docs/glue/Glue.md) labels
- [TRANSIT](./docs/transit/Transit.md): parameter-morpher and sequencer for up to 96 snapshots
- [X4](./docs/cvmap/X4.md): dual multiple for parameter-mapping

![Intro image](./docs/intro.png)

Feel free to contact me or create a GitHub issue if you have any problems or questions!  

## Special thanks

- [Lexandra Maxine](https://github.com/xandramax) for contributing [HIVE](./docs/maze/Hive.md)
- [Artem Leonov](https://artemleonov.bandcamp.com/) of [VCV Rack Ideas](https://www.youtube.com/channel/UCc0cXlzQdOwQSiyW30NQ7Sg) for his endless ideas, feature requests and testing, also for his great video tutorials
- [Omri Cohen](https://omricohencomposer.bandcamp.com/) for his fabulous video tutorials and fun live streams
- Andrew Belt for creating and developing VCV Rack
- 4ms Company for creating and developing MetaModule
- Hundredrabbits for creating and developing ORCA
- All others who contributed ideas or made donations for the development of PackOne!

## Building

Follow the [build instructions](https://vcvrack.com/manual/Building#Building-Rack-plugins) for VCV Rack or the [build instructions](https://github.com/4ms/metamodule-plugin-sdk?tab=readme-ov-file#path-to-the-sdk) for MetaModule.

## License

All **source code** is copyright © 2026 Benjamin Dill and is distributed under the [GNU General Public License, version v3.0](./LICENSE.txt) or any later version (SPDX: GPL-3.0-or-later).

All **files** and **graphics** in the `res` and `res-src` directories are distributed under [CC BY-NC-ND 4.0](https://creativecommons.org/licenses/by-nc-nd/4.0/) (SPDX: CC-BY-NC-ND-4.0). You may not distribute modified adaptations of these graphics.