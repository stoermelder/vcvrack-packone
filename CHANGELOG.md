## 2.5.0

### Changes and Fixes

- Module [MB](./docs/trial-and-error/Mb.md)
    - Added width (HP) filter and sorting to *v2 mod* browser
    - Fixed *v2 mod* scroll position when reopening the browser overlay
- Module [MIDI-MON](./docs/midi/MidiMon.md)
    - Fixed SysEx message logging (trailing garbage bytes)
    - Fixed saving of SysEx data logging setting
- Module [PANIC ROOM](./docs/panicroom/PanicRoom.md)
    - Added options to limit the number of allowed modules and cables
- Module [TRANSIT](./docs/transit/Transit.md)
    - Fade CV input is now additive to per-slot fade time (previously CV was only additive to the global _FADE_ knob)
    - Added Output-mode "Tipsy" for sending the snapshot text label (for modules with Tipsy-support like [TTY](https://library.vcvrack.com/StochasticTelegraph/TTY))

## 2.4.1

### Changes and Fixes

- [8FACE mk2](./docs/eightface/EightFaceMk2.md)
    - Fixed crash on patch autosave and on preset-loading 

## 2.4.0

### New modules

- Module [MIDI-ESX](./docs/midiesx/MidiEsx.md)
    - MIDI converter for your Expert Sleepers hardware setup (ES-5, ES-8, ES-9 etc.)

### Changes and Fixes

- Modules [8FACE, 8FACEx2](./docs/eightface/EightFace.md), [8FACE mk2](./docs/eightface/EightFaceMk2.md)
    - Fixed broken processing in VCV Rack-plugin on closed plugin-window (#424)
- [8FACE mk2](./docs/eightface/EightFaceMk2.md)
    - Added an option to draw module outlines only when selected, which is the new default
- Module [AHAB](./docs/ahab/Ahab.md)
    - Added gate length as a parameter for operator `>` in note mode (#420)
    - Added reset input for tick counter (#429)
    - Added "pending bang" operator `+` (#427)
    - Fixed crash on operator `<` when using whithout maximum value set (#425)
- Modules [INTERMIX](./docs/intermix/Intermix.md)
    - Added reset input for resetting scene selection and direction (#433)
    - Added scene CV modes: Ping-pong, Alternate, Random, Random (no repeat), Random walk, Shuffle
- Modules [INTERMIX](./docs/intermix/Intermix.md), [INTERMIX-FADE](./docs/intermix/Intermix.md#intermix-fade-expander)
    - Added fade length setting (4s, 15s, 60s) (#432)
- Module [MB](./docs/trial-and-error/Mb.md)
    - Added custom tags for user-defined module grouping
        - with not-so-smart auto-tagging using a curated list of tags
        - with auto-tagging for available modules on MetaModule
    - Added *v2 mod* browser variant
        - with improved drop-down menus, also with keyboard-filtering
        - with keyboard-navigation using cursor-keys
        - with hotkeys `Ctrl/Cmd`+`1`/`2`/`3` for Brand/Tags/Custom Tag drop-down menus
        - with keyboard-navigation and filtering within the drop-down menus
    - Added ability to add/remove predefined tags (only within MB)
    - Added fuzzy search (similar to the default module browser)
        - with setting for the fuzzy search threshold
    - Added an option to select "Favorite" handling (Legacy MB / Built-in VCV Rack)
    - Added an option to highlight favorites
    - Added model magnifier loupe option
    - Added integration into Rack's menu bar
    - Added `Shift`+*Click* to add a module without closing the browser
    - Added option to apply VCV Library whitelisting
    - Added option to show deprecated module models (now hidden by default) (#440)
    - Changed hotkey to toggle hidden modules to `Shift`+`Space` (because of Spotlight on Mac)
    - Fixed broken button of "Favorites" category
- Module [ME](./docs/trial-and-error/Me.md)
    - Added screen magnifier loupe
- Module [MIDI-MON](./docs/midi/MidiMon.md)
    - Added option to display engine frame instead of timestamp
- Module [POLY-PILE](./docs/pile/Pile.md)
    - Added support for polyphonic reset triggers (#431)

## 2.3.1

### Changes and Fixes

- Module [STRIP](./docs/strip/Strip.md)
    - Fixed changed behavior on excluded/included parameters for randomization introduced in v2.3.0

## 2.3.0

### New modules

- Module [AHAB](./docs/ahab/Ahab.md)
    - A live programming environment for ORCA, an esoteric programming language designed to quickly create procedural sequencers

### Changes and Fixes

- Module [8FACE mk2](./docs/eightface/EightFaceMk2.md)
    - Added performance warning for large module presets (#396)
    - Fixed broken unbinding of modules (memory leak) (#396)
- Module [GLUE](./docs/glue/Glue.md)
    - Added option to add labels to cables (#247)
- Module [MIDI-CAT](./docs/midicat/MidiCat.md)
    - Added hotkey Ctrl/Cmd+Shift+R for resetting input-mode _Pickup (snap)_
    - Added hotkey Ctrl/Cmd+Shift+I for temporarily activating input-mode _Direct_ while held
    - Added hotkey Ctrl/Cmd+Shift+F for re-sending MIDI feedback
- Module [MIDI-KEY](./docs/midi/MidiKey.md)
    - Fixed occasional crash in browser preview
- Module [SAIL](./docs/sail/Sail.md)
    - Fixed occasional crash (#358)
- Module [STROKE](./docs/stroke/Stroke.md)
    - Fixed broken _Zoom out_ command
    - Fixed broken Toggle-commands on different UI Scale (#415)
- Module [TRANSIT](./docs/transit/Transit.md)
    - Added alternative parameter binding by selection box
    - Added custom color LED setting per slot
    - Added option to set the first usable snapshot (instead of starting at 1) (#265)
    - Added option to disable fade CV input clamping, allowing for more extreme fade times

## 2.2.0

Modules [8FACE, 8FACEx2](./docs/eightface/EightFace.md) and [8FACE mk2](./docs/eightface/EightFaceMk2.md) are now considered stable again. A new _Safe-mode_ has been added, which loads presets into modules according to the supported way. This provides maximum stability. However, it may lock up the audio processing and cause stutters, pops, or other audible artifacts. This behavior is not caused by CPU overload but by fundamental design constraints in VCV Rack.  
The old behavior can be restored using _Unsafe fast_-mode, which will load presets more quickly but may lead to crashes or other issues.

### New modules

- Module [MIDI-CAT FINE](./docs/midicat/MidiCat.md#fine-expander)
    - New expander for MIDI-CAT, allows fine-tuning of MIDI CC parameter-mappings
- Module [PANIC ROOM](./docs/panicroom/PanicRoom.md)
    - Restricts your modular space within Rack, making it impossible to patch outside of a defined area

### Changes and Fixes

- Modules [8FACE, 8FACEx2](./docs/eightface/EightFace.md)
    - Added stability mode setting and _Safe-mode_, which is the new default setting
- Modules [8FACEx2](./docs/eightface/EightFace.md)
    - Fixed broken function on some modules (only 8FACEx2)
- Module [8FACE mk2](./docs/eightface/EightFaceMk2.md)
    - Added stability mode setting and _Safe-mode_, which is the new default setting
    - Improved robustness for expander +8
- Module [ARENA](./docs/arena/Arena.md)
    - Fixed broken loading of presets and loading from saved patches
- Modules [CV-MAP](./docs/cvmap/CVMap.md)
    - Added mapping functions _Map module (left)_ and _Map module (select)_
    - Added color setting for mapping indicators
- Modules [CV-MAP](./docs/cvmap/CVMap.md), [MACRO](./docs/cvmap/Macro.md), [µMAP](./docs/cvmap/CVMapMicro.md), [MIDI-CAT](./docs/midicat/MidiCat.md), [MIRROR](./docs/cvmap/Mirror.md), [ReMOVE Lite](./docs/cvmap/ReMove.md), [X4](./docs/cvmap/X4.md)
    - Added context menu option to report parameter updates to plugin-host (only in plugin-version of Rack)
- Module [DIRT](./docs/dirt/Dirt.md)
    - Added new defects _Pitch_, _Crush_ and _Dropout_
- Module [GOTO](./docs/goto/Goto.md)
     - Jump-points can be multiple modules/a selection instead of a single module
- Module [MIDI-CAT](./docs/midicat/MidiCat.md)
    - Added input-modes _Snapped_ and _Snapped (short/long)_ for CC and Notes for use with snapped parameters (e.g. "Steps" on VCV SEQ3)
    - Added handling for MIDI System Reset message for resetting input-mode _Pickup (snap)_
    - Added color setting for mapping indicators
    - Fixed mistaken copy-over of 14-bit CC flag from previous mapping slot on CCs >= 32
    - Fixed missing parameter updates after manual adjustment in some situations
    - Fixed broken MIDI learning for CC 31
    - Fixed broken _Locate and indicate_ mode
- Module [MIDI-CAT MEM](./docs/midicat/MidiCat.md#mem-expander)
    - Added module restriction list
    - Fixed broken MIDI feedback when loading stored mappings
- Module [STRIP](./docs/strip/Strip.md)
    - Remember and recall file-dialog folder locations for vcvss and vcvs files
- Module [STROKE](./docs/stroke/Stroke.md)
    - Fixed cables' _Toggle visibility_ command (cable plugs still visible)
- Module [TRANSIT](./docs/transit/Transit.md)
    - Improved robustness for expander +T (e.g. crashes when using module-presets) (#412)

## 2.1.0

### Changes and Fixes

- Modules [CV-MAP](./docs/cvmap/CVMap.md), [µMAP](./docs/cvmap/CVMapMicro.md), [MIDI-CAT](./docs/midicat/MidiCat.md), [ReMOVE Lite](./docs/cvmap/ReMove.md), [X4](./docs/cvmap/X4.md)
    - Improved mapping logic to allow more parameters to be mapped (e.g. SurgeXTRack) (#410)
- Module [MIDI-KEY](./docs/midi/MidiKey.md)
    - Fixed crash on mapped special keys like ENTER
- Module [TRANSIT](./docs/transit/Transit.md)
    - Added context menu option to report parameter updates to plugin-host (only in plugin-version of Rack) 

## 2.0.0

[Selected modules](./MetaModule.md) are available on the [4ms MetaModule](https://4mscompany.com/p.php?p=990).

Modules [8FACE, 8FACEx2](./docs/eightface/EightFace.md) and [8FACE mk2](./docs/eightface/EightFaceMk2.md) are now considered experimental, because loading of presets into any module cannot be considered stable and issue-free in every case.

### New modules

- Module [MIDI-CAT XL](./docs/midicat/MidiCat.md)
    - A wider panel version of MIDI-CAT with identical features and driver selection moved to the context menu

### Changes and Fixes

- Module [8FACE mk2](./docs/eightface/EightFaceMk2.md)
    - Added "Auto"-mode besides "Read" and "Write" ([manual](./docs/eightface/EightFaceMk2.md#auto-mode)) (#276)
    - Added option to bind currently selected modules
- Module [AFFIX](./docs/affix/Affix.md), [µAFFIX](./docs/affix/Affix.md)
    - Fixed knob reset on double-click in Semitone/Octave-mode (#387)
    - Fixed wrong output voltage in Semitone/Octave-mode after loading (#403)
    - Don't use Rack's parameter smoothing in Semitone/Octave-mode (broken since Rack 2.3.0)
- Module [ARENA](./docs/arena/Arena.md)
    - Fixed behavior of attenuvertors for X/Y/MOD-inputs (#394)
    - Fixed coarse parameter updates on screen interaction because of display refresh rate (#210)
- Module [GOTO](./docs/goto/Goto.md)
    - Implemented smooth transition for "top left" jump destination (#388)
- Module [MB](./docs/trial-and-error/Mb.md)
    - Fixed usage in multiple plugin-instances
- Module [ME](./docs/trial-and-error/Me.md)
    - Fixed usage in multiple plugin-instances
- Module [MIDI-CAT](./docs/midicat/MidiCat.md)
    - Implemented response curves (logarithmic/exponential) (#258)
    - Fixed MIDI-feedback for snapped parameters (#374)
    - Implemented experimental LED binding for proper MIDI feedback on push-buttons (e.g. on VCV SEQ3 or Impromptu GATE-SEQ-64) (#401)
    - Implemented alternative parameter binding by selection box
- Module [MIDI-CAT CTX](./docs/midicat/MidiCat.md#ctx-expander)
    - Fixed broken button-handling when triggered by Parameter-mapping
- Module [MIDI-CAT MEM](./docs/midicat/MidiCat.md#mem-expander)
    - Fixed broken button-handling when triggered by Parameter-mapping (#356)
    - Added trigger-inputs for Prev and Next
- Module [MIDI-MON](./docs/midi/MidiMon.md)
    - Added support for CC 14-bit/RPN/NRPN messages
    - Reduced CPU usage
- Module [MIDI-STEP](./docs/midi/MidiStep.md)
    - Added option for Hercules DJControl Starlight (#361)
- Module [STRIP](./docs/strip/Strip.md)
    - Ask to view unavailable modules on the VCV Library when loading a strip (#18)
    - Fixed missing module-id mapping when adding using STRIP or STRIP++ (#402)
    - Added option to remove items from included/excluded parameters list
- Module [STRIP++](./docs/strip/StripPp.md)
    - Ask to view unavailable modules on the VCV Library when loading a selection
    - Fixed usage in multiple plugin-instances
- Module [STRIP-BAY](./docs/strip/Strip.md#stoermelder-strip-bay)
    - Fixed crash on stackable input-cables (#405)
- Module [STROKE](./docs/stroke/Stroke.md)
    - Added command "Minimize window"
- Module [TRANSIT](./docs/transit/Transit.md)
    - Added fade setting per slot
    - Improved handling on mapped switches (skipping all immediate values)
    - Added context menu option to clean invalid parameters (#383)

## 2.0.beta4

### Fixes and Changes

- Added panel option to follow Rack's dark panel setting
- Modules [8FACE, 8FACEx2](./docs/eightface/EightFace.md)
    - Allow disabling of "long-press" for changing the number of active slots (#354)
- Module [8FACE mk2](./docs/eightface/EightFaceMk2.md)
    - Allow disabling of "long-press" for changing the number of active slots (#354)
    - Added HSL color picker for bound modules' box
    - Fixed broken module-id mapping when adding using STRIP or STRIP++
    - Fixed crash while exceding 0..10V in Volt-mode (#377)
    - Increased maximum number of expanders to 15
    - Added missing reset-handling for "Trigger random", "Trigger pseudo-random" and "Trigger random walk"
- Module [GLUE](./docs/glue/Glue.md)
    - Added HSL color picker
- Module [GRIP](./docs/cvmap/Grip.md)
    - Fixed broken parameter locking (#360)
- Module [GOTO](./docs/goto/Goto.md)
    - Fixed broken zoom behavior when jumping by buttons on the panel
    - Improved smooth transition speed on long distances (#376)
- Module [MB](./docs/trial-and-error/Mb.md)
    - Fixed crash on exiting Rack's after adding MB (#352)
    - Fixed wrong hotkey modifier on Mac (Ctrl instead of Cmd) on Space-key
    - Added missing template loading after adding a module (#369)
- Module [ROTOR mod A](./docs/rotor/RotorA.md)
    - Fixed occasional crashes (#365)
- Module [SAIL](./docs/sail/Sail.md)
    - Fixed occasional crash (#358)
- Module [STRIP](./docs/strip/Strip.md)
    - Fixed crash in rare cases (Surge-modules) (#366)
    - Fixed wrong hotkey modifier on Mac (Ctrl instead of Cmd) on Cmd+Shift+L
- Module [STRIP++](./docs/strip/StripPp.md)
    - Fixed wrong hotkey modifier on Mac (Ctrl instead of Cmd)
- Module [STROKE](./docs/stroke/Stroke.md)
    - Added commands "Zoom to specific module" and "Zoom to specific module (smooth)" (#357)
    - Fixed wrong hotkey modifier on Mac (Ctrl instead of Cmd)
    - Fixed broken "Zoom to module" and "Zoom toggle" commands (#382)
- Module [SPIN](./docs/spin/Spin.md)
    - Fixed middle mouse button handling in Rack v2 (#372)
- Module [TRANSIT](./docs/transit/Transit.md)
    - Allow disabling of "long-press" for changing the number of active snapshots (#354)
    - Increased maximum number of expanders to 15 (#381)
    - Added missing reset-handling for "Trigger random", "Trigger pseudo-random" and "Trigger random walk"

## 2.0.beta3

### New modules

- Module [MIDI-CAT CLK](./docs/midicat/MidiCat.md#clk-expander)
    - New expander for MIDI-CAT, allows trigger-quantization for mapped parameters (#299)

### Fixes and Changes

- Modules [8FACE, 8FACEx2](./docs/eightface/EightFace.md)
    - Fixed broken reset-behavior for "Trigger forward", "Trigger reverse" and "Trigger pingpong" (#347)
    - Added missing reset-handling for "Trigger alternating" and "Trigger shuffle"
- Module [8FACE mk2](./docs/eightface/EightFaceMk2.md)
    - Added "Bind module (select multiple)" option (#291)
    - Fixed broken reset-behavior for "Trigger forward", "Trigger reverse" and "Trigger pingpong" (#347)
    - Added missing reset-handling for "Trigger alternating" and "Trigger shuffle"
- Module [DIRT](./docs/dirt/Dirt.md)
    - Added crackle for polyphonic cables
    - Added switches for noise-types on front panel
- Module [GLUE](./docs/glue/Glue.md)
    - Changed "Add label" hotkey to Ctrl+G (#305)
    - Added hotkey Ctrl+Shift+G for "Lock"
- Module [HIVE](./docs/maze/Hive.md)
    - Fixed broken reset-behavior
- Module [MAZE](./docs/maze/Maze.md)
    - Fixed broken reset-behavior
- Module [MIDI-CAT](./docs/midicat/MidiCat.md)
    - Fixed pickup of parameters with snapping (#308)
- Module [MIDI-KEY](./docs/midi/MidiKey.md)
    - Added options for slot-specific key modifiers (#344)
    - Added options for sending hotkeys to a specific module
- Module [MIDI-MON](./docs/midi/MidiMon.md)
    - Added support for SysEx messages
- Module [ORBIT](./docs/orbit/Orbit.md)
    - Added output level control (#286)
- Module [STRIP++](./docs/strip/StripPp.md)
    - Added selection perview before actual inserting modules
    - Added "Recent selection" context menu option
- Module [STRIP](./docs/strip/Strip.md)
    - Remember last used folder for strips and selections on dialogs (#307)
    - "randomizeEnabled" of parameters is respected when randomizing (#349)
    - Fixed high CPU usage in High/Low-mode for bypass
- Module [STROKE](./docs/stroke/Stroke.md)
    - Added commands "Add random module", "Save module preset" and "Save module default preset" (#345)
- Module [TRANSIT](./docs/transit/Transit.md)
    - Fixed broken Auto/Write-modes if CV-port is set to "Phase" (#282)
    - Fixed broken reset-behavior for "Trigger forward", "Trigger reverse" and "Trigger pingpong" (#347)
    - Added missing reset-handling for "Trigger alternating" and "Trigger shuffle"

## 2.0.beta2

### New modules

- Module [DIRT](./docs/dirt/Dirt.md)
    - New module, crosstalk and noise for polyphonic cables
- Module [MIDI-KEY](./docs/midi/MidiKey.md)
    - New module, utility for generating keyboard events from MIDI CC or note messages (#245)
- Module [STRIP++](./docs/strip/StripPp.md)
    - New module, utility for pasting and importing Rack selections while preserving parameter mappings and [GLUE](./docs/glue/Glue.md) labels

### Fixes and Changes

- Module [GOTO](./docs/goto/Goto.md)
    - Fixed crash on patch-loading inside Rack VST (and probably other plugin formats) (#342)
- Module [MIDI-PLUG](./docs/midi/MidiPlug.md)
    - Removed MIDI "Loopback" driver as a loopback driver is available officially since Rack 2.2.0
- Module [STRIP](./docs/strip/Strip.md)
    - Fixed crash on disabling a group of modules (#341)

## 2.0.beta1

- Changed license to GPL-3.0-or-later

### Fixes and Changes

- Modules [8FACE, 8FACEx2](./docs/eightface/EightFace.md)
    - Added retrigger-function for CV-input channel 2 in C4 mode (#330)
    - Fixed unconnected modules after patch reload (#338)
- Module [8FACE mk2](./docs/eightface/EightFaceMk2.md)
    - Added retrigger-function for CV-input channel 2 in C4 mode (#330)
- Module [ARENA](./docs/arena/Arena.md)
    - Fixed broken behavior of "Radius" sliders of "In"-ports
    - Fixed broken patch-restore of "Radius" sliders (#331)
- Module [GOTO](./docs/goto/Goto.md)
    - Added "top left" as a modules reference point for jump destination
    - Removed setting "Center module" as the disabled state did not work correctly
- Module [TRANSIT](./docs/transit/Transit.md)
    - Added retrigger-function for CV-input channel 2 in C4 mode (#330)
    - Fixed premature end of processing and not reaching stored snapshot state (#329)


---

The changelog of releases for VCV Rack v1 can be found [here](./CHANGELOG-v1.md).