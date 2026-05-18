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
    - Added extended scene CV modes: Ping-pong, Alternate, Random, Random (no repeat), Random walk, Shuffle
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
    - Changed hotkey to toggle hidden modules to `Shift`+`Space` (because of Spotlight on Mac)
    - Fixed broken button of "Favorites" category
- Module [ME](./docs/trial-and-error/Me.md)
    - Added screen magnifier loupe
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

## 1.10.0

### New modules

- Module [INTERMIX-FADE](./docs/intermix/Intermix.md#fade-expander)
    - Expander for INTERMIX, helper for setting individual fade values
- Module [INTERMIX-ENV](./docs/intermix/Intermix.md#env-expander)
    - Expander for INTERMIX, outputs envelopes for a seleced input-column
- Module [INTERMIX-GATE](./docs/intermix/Intermix.md#gate-expander)
    - Expander for INTERMIX, outputs a gate-signal for each active row (#228)

### Fixes and Changes

- Modules [8FACE, 8FACEx2](./docs/eightface/EightFace.md)
    - Added "Auto"-mode besides "Read" and "Write" ([manual](./docs/eightface/EightFace.md#auto-mode)) (#251)
    - Added "Shift front" and "Shift back" context menu options (#275)
- Module [INTERMIX](./docs/intermix/Intermix.md)
    - Added context menu option "Scene lock" to prevent accidental changes
- Module [MIDI-CAT](./docs/midicat/MidiCat.md)
    - Fixed broken multi-mapping for note-messages (#271)
- Module [STROKE](./docs/stroke/Stroke.md)
    - Improved behavior of parameter copy/paste commands (#273)
- Module [TRANSIT](./docs/transit/Transit.md)
    - Added context menu option for unbinding all bound parameters of a module (#268)
    - Added "Auto"-mode besides "Read" and "Write" ([manual](./docs/transit/Transit.md#auto-mode)) (#269)
    - Added "Shift front" and "Shift back" context menu options (#274)

## 1.9.0

### New modules

- Modules [8FACE mk2, +8](./docs/eightface/EightFaceMk2.md)
    - Evolution of 8FACE and 8FACEx2 (#63 #76 #144 #154 #157 #158 #160 #162)
- Module [CV-MAP CTX](./docs/cvmap/CVMap.md#ctx-expander)
    - Expander for CV-MAP, helper for mapping parameters by context menu (#256)
- Module [MIDI-CAT CTX](./docs/midicat/MidiCat.md#ctx-expander)
    - Expander for MIDI-CAT, helper for mapping parameters by context menu (#232, #250)
- Module [MIDI-PLUG](./docs/midi/MidiPlug.md)
    - A virtual MIDI merger and splitter
    - MIDI "Loopback" driver for routing outgoing MIDI messages back into Rack (enabled on the context menu)
- Module [ORBIT](./docs/orbit/Orbit.md)
    - A polyphonic stereo field spreader
- Module [STRIP-BAY](./docs/strip/Strip.md#stoermelder-strip-bay)
    - A companion module for STRIP for keeping input/output connections while replacing strips
- Module [ME](./docs/trial-and-error/Me.md)
    - Experimental module for "mouse enhacements", provides a screen overlay for parameters changes

### Fixes and Changes

- Modules [8FACE, 8FACEx2](./docs/eightface/EightFace.md)
    - Load preset in Arm-mode even when the same slot was selected before (#212)
    - Improved thread-handling for crashes when used with specific modules (#76)
    - Added an option for auto-loading the last active preset
    - Added "Off" as SLOT mode (#249)
    - Fixed broken "Autoload first preset" (#29)
- Module [CV-MAP](./docs/cvmap/CVMap.md)
    - Added context menu sliders for slew and scaling and transforming the input and parameter-range ([manual](./docs/cvmap/CVMap.md#slew-limiting-and-input-scaling)) (#243)
    - Added arbitrary channel routings to allow every input channel assigned to any mapping slot ([manual](./docs/cvmap/CVMap.md#channel-routing))
    - Added context menu on the input ports for custom labeling the channels ([manual](./docs/cvmap/CVMap.md#input-labels)) (#256)
    - Fixed wrong slot behavior when toggling input to 0V (#221)
- Module [GLUE](./docs/glue/Glue.md)
    - Added option to consolidate all GLUE modules into the current one ([manual](./docs/glue/Glue.md#consolidate))
- Module [HIVE](./docs/maze/Hive.md)
    - Fixed hanging ratchets on missing or stopped clock trigger (#216)
    - Added new ratcheting modes ("Twos", "Threes", "Power of Two") ([manual](./docs/maze/Maze.md#ratchet))
- Module [MACRO](./docs/cvmap/Macro.md)
    - Added context menu options on mapped parameters of target module for MACRO
- Module [MAZE](./docs/maze/Maze.md)
    - Fixed hanging ratchets on missing or stopped clock trigger (#216)
    - Added new ratcheting modes ("Twos", "Threes", "Power of Two") ([manual](./docs/maze/Maze.md#ratchet))
- Module [MB](./docs/trial-and-error/Mb.md)
    - Added option to hide the "brands" section of the V1-browser (#223)
    - Added option to search module descriptions (https://github.com/stoermelder/vcvrack-packtau/pull/9)
- Module [µMAP](./docs/cvmap/CVMapMicro.md)
    - Added context menu options on mapped parameters of target module for µMAP ([manual](./docs/cvmap/CVMapMicro.md#target-context))
    - Fixed wrong behavior when toggling input to 0V (#221)
- Module [MIDI-CAT](./docs/midicat/MidiCat.md)
    - Added support for MIDI 14-bit CC ([manual](./docs/midicat/MidiCat.md#14-bit-cc))
    - Added toggle-modes for MIDI CC mappings ([manual](./docs/midicat/MidiCat.md#toggle-cc)) (#225)
    - Added an overlay showing current parameter changes on the bottom of the screen ([manual](./docs/midicat/MidiCat.md#overlay))
    - Fixed crash when binding modules with more than 128 parameters (#234)
    - Added option for clearing mapping slots on preset load (#259)
- Module [MIDI-MON](./docs/midi/MidiMon.md)
    - Added support for more message types (program change, song select, song pointer)
    - Added context menu option for clearing the log
- Module [MIDI-STEP](./docs/midi/MidiStep.md)
    - Fixed relative modes for Behringer X-Touch (#240)
    - Fixed duplicate mappings of the same CC (#240)
- Module [RAW](./docs/raw/Raw.md)
    - Added basic limiting to prevent rare instabilities (#214)
- Module [SAIL](./docs/sail/Sail.md)
    - Added an overlay showing current parameter changes on the bottom of the screen
    - Block adjustments on switch-parameters to avoid undefined behavior
- Module [SPIN](./docs/spin/Spin.md)
    - Improved transition between scrolling and parameter adjustments on hovering (#260)
- Module [STRIP](./docs/strip/Strip.md)
    - Added context menu option "Load and replace" to preset-submenu (#215)
    - Added support for sub-folders in preset-submenu (#230)
- Module [STROKE](./docs/stroke/Stroke.md)
    - Allow mapping mouse buttons 0/1/2 (left/right/middle) in use with modifiers
    - Fixed not working mappings caused by Num Lock state (#220)
    - Fixed not working mappings caused by use of numpad keys (#220)
    - Added view-commands using smooth transitions (#139)
    - Added "Add module" command ([manual](./docs/stroke/Stroke.md#add-module))
    - Added "Send hotkey to module" command ([manual](./docs/stroke/Stroke.md#module-send-hotkey))
    - Added scroll-commands ([manual](./docs/stroke/Stroke.md#view-scroll)) (#252)
    - Added tooltips for mapped commands
- Module [TRANSIT](./docs/transit/Transit.md)
    - Added "Phase"-mode for CV-input which scans continously through snapshots ([manual](./docs/transit/Transit.md#phase)) (#182)
    - Added context menu option "Locate and indicate" for bound parameters
    - Added context menu option for custom text labels
    - Improved performance of +T expanders

## 1.8.0

- Module [HIVE](./docs/maze/Hive.md)
    - New module, 4 channel sequencer running on a 2-dimensional hexagonal grid
- Module [MACRO](./docs/cvmap/Macro.md)
    - New module, marco-knob for four parameter-mappings and two CV outputs
- Module [MB](./docs/trial-and-error/Mb.md)
    - New module, experimental replacement for Rack's module browser, formerly available in Stoermelder's PackTau
- Module [MIDI-MON](./docs/midi/MidiMon.md)
    - New module, input-monitor for MIDI messages
- Module [RAW](./docs/raw/Raw.md)
    - New module, a digital effect based on the dynamics of bistable systems

### Fixes and Changes

- Module [ARENA](./docs/arena/Arena.md)
    - Fixed noise on OUT-ports (#190)
- Module [CV-PAM](./docs/cvmap/CVPam.md)
    - Fixed wrong channel count of the polyphonic output ports
- Modules [8FACE, 8FACEx2](./docs/eightface/EightFace.md)
    - Fixed hanging pingpong-mode when changing slots manually (#191, #203)
    - Added trigger-options "pseudo-random", "random walk", "alternating", "shuffle" ([manual](./docs/eightface/EightFace.md#trigger-modes))
- Module [GRIP](./docs/cvmap/Grip.md)
    - Fixed crash on locking more than 32 parameters (#176)
- Module [INTERMIX](./docs/intermix/Intermix.md)
    - Added support for polyphony (#199)
- Module [µMAP](./docs/cvmap/CVMapMicro.md)
    - Added input voltage display
- Module [MIDI-CAT](./docs/midicat/MidiCat.md)
    - Added context menu sliders for MIDI filtering/slew-limiting for CCs and notes ([manual](./docs/midicat/MidiCat.md#slew-limiting)) (#79)
    - Added context menu sliders for scaling or transforming the MIDI-input and parameter-range ([manual](./docs/midicat/MidiCat.md#input-scaling)) (#169)
    - Added context menu options for precision/CPU-usage ([manual](./docs/midicat/MidiCat.md#precision))
    - Added context menu options on mapped parameters of target module for MIDI-CAT ([manual](./docs/midicat/MidiCat.md#target-context))
    - Added skipping of current slot with SPACE-key while in mapping-mode
    - Added context menu option for clearing all mapping-slots
    - Added option for ignoring MIDI device settings on preset load (#185)
    - Fixed broken "Re-send MIDI feedback" option
    - Added context menu option for re-sending MIDI feedback periodically ([manual](./docs/midicat/MidiCat.md#feedback-periodically))
    - Added note-mode "Toggle + Velocity" ([manual](./docs/midicat/MidiCat.md#toggle-velocity))
- Module [MIDI-CAT MEM](./docs/midicat/MidiCat.md#mem-expander)
    - Added support for MIDI-CAT's new slew-limiting and scaling options ([manual](./docs/midicat/MidiCat.md#slew-limiting-and-input-scaling))
    - Added scanning for next or previous modules with stored mapping ([manual](./docs/midicat/MidiCat.md#mem-scan)) (#200)
- Module [MIDI-STEP](./docs/midi/MidiStep.md)
    - Added option for Akai MPD218 ([manual](./docs/midi/MidiStep.md#akai-mpd218))
- Module [MIRROR](./docs/cvmap/Mirror.md)
    - Added syncing of module presets even if bound module has no parameters (#189)
    - Added hotkey for syncing module presets
- Module [STRIP](./docs/strip/Strip.md)
    - Added hotkeys Shift+L (load), Shift+S (save as), Shift+X (cut)
    - Added context menu option "Load with replace" ([manual](./docs/strip/Strip.md#load-and-replace)) (#186)
    - Fixed crash on loading vcvss-files with missing modules
    - Added context menu option for custom presets, listing all .vcvss-files in folder presets/Strip ([manual](./docs/strip/Strip.md#preset)) (#198)
- Module [STROKE](./docs/stroke/Stroke.md)
    - Added commands "Toggle engine pause", "Toggle lock modules"
    - Added command "Toggle busboard"
    - Added LEDs for signaling an activated hotkey
    - Allow loading presets (#187)
    - Improved behavior of command "Cable opacity" across restarts of Rack (#197)
- Module [TRANSIT](./docs/transit/Transit.md)
    - Fixed hanging pingpong-mode when changing slots manually
    - Added trigger-options "pseudo-random", "random walk", "alternating", "shuffle" ([manual](./docs/transit/Transit.md#sequencing-and-selecting-snapshots))
    - Fixed broken snapshots on save after mapped modules have been deleted (#205)
- Module [X4](./docs/cvmap/X4.md)
    - Fixed advancing to the lower button after the upper button has been mapped
    - Fixed wrong tooltip of lower mapping button

## 1.7.1

### Fixes and Changes

- Module [TRANSIT](./docs/transit/Transit.md)
    - Fixed wrong snapshot-count when using +T expander after loading a patch

## 1.7.0

- Module [MIDI-CAT MEM](./docs/midicat/MidiCat.md#mem-expander)
    - New expander-module for MIDI-CAT, storage-unit for MIDI mapping-presets with MIDI-CAT
- Module [SPIN](./docs/spin/Spin.md)
    - New module, converts mouse-wheel or middle mouse-button events into triggers
- Module [STROKE](./docs/stroke/Stroke.md)
    - New module, converts used-defined hotkeys into triggers or gates, also provides some special commands for Rack's enviroment
- Module [TRANSIT](./docs/transit/Transit.md)
    - New module, parameter-morpher and sequencer for up to 96 snapshots
- Module [+T](./docs/transit/Transit.md#t-expander)
    - New module, expander for TRANSIT
- Module [X4](./docs/cvmap/X4.md)
    - New module, dual multiple for parameter-mapping

### Fixes and Changes

- Module [ARENA](./docs/arena/Arena.md)
    - Fixed wrong calculation of output levels (#147, #113)
- Module [CV-MAP](./docs/cvmap/CVMap.md)
    - Don't capture mouse scrolling if mapping slots are locked (#137)
    - Blink mapping indicator of currently selected mapping slot
- Module [CV-PAM](./docs/cvmap/CVPam.md)
    - Don't capture mouse scrolling if mapping slots are locked (#137)
    - Blink mapping indicator of currently selected mapping slot
- Module [GLUE](./docs/glue/Glue.md)
    - Implemented support for labels within STRIP, please be aware to include GLUE within your strip-file (#115)
    - Added options for changing text coloring (#136)
- Module [GRIP](./docs/cvmap/Grip.md)
    - Implemented support for parameter-mappings within STRIP (#151)
- Module [GOTO](./docs/goto/Goto.md)
    - Added support for number pad keys (#134)
- Module [µMAP](./docs/cvmap/CVMapMicro.md)
    - Fixed meaningless tooltip on Map-button
    - Blink mapping indicator when activating the mapping button
- Module [MIDI-CAT](./docs/midicat/MidiCat.md)
    - Don't capture mouse scrolling if mapping slots are locked (#137)
    - Blink mapping indicator of currently selected mapping slot
    - Added option for automatic mapping of all parameters of a module on the left side or by module-select
    - Mapping is aborted using ESC-key while hovering the mouse over the module
    - Added option for sending MIDI "note on, velocity 0" on feedback for note off (#130)
- Module [MIRROR](./docs/cvmap/Mirror.md)
    - Implemented support for parameter-mappings within STRIP
- Module [ReMOVE Lite](./docs/cvmap/ReMove.md)
    - Blink mapping indicator when activating the mapping screen
- Module [STRIP](./docs/strip/Strip.md)
    - Added option to randomize only parameters without the module's internal state (#135) 

## 1.6.3

### Fixes and Changes

- Module [GLUE](./docs/glue/Glue.md)
    - Fixed crash on loading patches with empty labels

## 1.6.2

### Fixes and Changes

- Module [MIDI-STEP](./docs/midi/MidiStep.md)
    - Fixed port numbering

## 1.6.1

### Fixes and Changes

- Module [GLUE](./docs/glue/Glue.md)
    - Fixed invalid initialization on new instances
- Module [MIDI-CAT](./docs/midicat/MidiCat.md)
    - Added option for re-sending MIDI feedback values
- Module [ROTOR mod A](./docs/rotor/RotorA.md)
    - Indicate inactive output channels in blue

## 1.6.0

- Module [AFFIX](./docs/affix/Affix.md), [µAFFIX](./docs/affix/Affix.md)
    - New modules, inserts for polyphonic cables for adding offsets in volt, semitones or ocatves
- Module [GLUE](./docs/glue/Glue.md)
    - New module, label maker for your modules!
- Module [GOTO](./docs/goto/Goto.md)
    - New module, utility for jumping directly to 10 locations in your patch by hotkey or using MIDI
- Module [GRIP](./docs/cvmap/Grip.md)
    - New module, lock for module parameters
- Module [MIRROR](./docs/cvmap/Mirror.md)
    - New module, utility for synchronizing module parameters
- Module [POLY-PILE](./docs/pile/Pile.md)
    - New module, polyphonic version of PILE

### Fixes and Changes

- Module [MAZE](./docs/maze/Maze.md)
    - Added option for disabling normalization to the yellow input ports (#95)
    - Added independent ratcheting settings for each sequencer-playhead (#94)
- Module [MIDI-STEP](./docs/midi/MidiStep.md)
    - Added option for polyphonic output for all channels on port 1
    - Increased number of CCs to 16 although 9-16 can only be used in polyphonic mode
- Module [PILE](./docs/pile/Pile.md)
    - Removed slew-limiting after preset-load
- Module [ROTOR mod A](./docs/rotor/RotorA.md)
    - Allow bipolar carrier signal / remove clamping on 0..10V
    - Added offset for output channel number (#121)
- Module [SAIL](./docs/sail/Sail.md)
    - Rewritten how the target values are applied onto the parameters (#106). You can't use IN and INC/DEC the same time anymore, just use two instances of the  module.

## 1.5.0

- Module [MIDI-STEP](./docs/midi/MidiStep.md)
    - New module, utility for relative modes of endless knobs on your MIDI controller such as Arturia Beatstep
- Module [PILE](./docs/pile/Pile.md)
    - New module, translate increment triggers or decrement triggers into an absolute voltage, especially useful with MIDI-STEP
- Module [SAIL](./docs/sail/Sail.md)
    - New module, control any parameter currently hovered by mouse with CV, especially useful with MIDI-CC or MIDI-STEP

### Fixes and Changes

- Module [CV-MAP](./docs/cvmap/CVMap.md)
    - Added option for hiding parameter indicator squares
    - Added option for locking mapping slots to prevent changes by accident (#89)
- Module [CV-PAM](./docs/cvmap/CVPam.md)
    - Added option for hiding parameter indicator squares
    - Added option for locking mapping slots to prevent changes by accident (#89)
- Module [MIDI-CAT](./docs/midicat/MidiCat.md)
    - Added option for hiding parameter indicator squares
    - Added option for locking mapping slots to prevent changes by accident (#89)
    - Fixed broken toggle-mode for MIDI note-mapping
- Module [INFIX](./docs/infix/Infix.md)
    - Added Leds for used channels on polyphonic cables
- Module [INTERMIX](./docs/intermix/Intermix.md)
    - Added matrix mapping parameters on rows and columns for use with midi-mapping
    - Added option for excluding attenuverters from scenes
    - Added ability to copy scenes
    - Added ability to reset scenes
    - Added option for disabling the SCENE-port
    - Added option for changing the number of active scenes
    - Fixed broken fading if either fade-in or fade-out is set to zero
- Added dark panels for all modules (#15)
- Added globals settings, esp. for dark panels als default

## 1.4.0

- Module [INTERMIX](./docs/intermix/Intermix.md)
    - New module, precision adder 8x8 switch matrix with support for 8 scenes
- Module [ARENA](./docs/arena/Arena.md)
    - Added missing bipolar-mode for X/Y-inputs of the mix-channels
- Module [CV-MAP](./docs/cvmap/CVMap.md)
    - Added option to disable audio rate processing for lower cpu usage
- Module [CV-PAM](./docs/cvmap/CVPam.md)
    - Added option to disable audio rate processing for lower cpu usage
- Module [MIDI-CAT](./docs/midicat/MidiCat.md)
    - Fixed wrong handling of CC-mappings in Direct-mode on parameter changes made within Rack
    - Added option to enter custom labels for mapping slots (#75)

## 1.3.0

- Module [ARENA](./docs/arena/Arena.md)
    - New module, 2-dimensional XY-Mixer for 8 sources with various modulation targets and graphical interface
- Module [MAZE](./docs/maze/Maze.md)
    - New module, 4 channel sequencer running on a 2-dimensional grid
- Module [8FACE](./docs/eightface/EightFace.md)
    - Revised panel design with combined LED and buttons
- Module [8FACEx2](./docs/eightface/EightFace.md)
    - New module, 8FACE with sixteen preset slots
- Module [µINFIX](./docs/infix/Infix.md)
    - New module, 8 port variant of INFIX
- Module [µMAP](./docs/cvmap/CVMapMicro.md)
    - Fixed meaningless tooltip on Map-button
- Module [MIDI-CAT](./docs/midicat/MidiCat.md)
    - Fixed velocity-handling on note messages if in toggle-mode (does not need vel 127 anymore)
- Module [STRIP](./docs/strip/Strip.md)
    - Added utilization for Rack's undo-history on cutting strips and pasting/loading-strips (#11)
    - Added utilization for Rack's undo-history on enable/disable or randomize if triggered manually (#11)
- Module [ReMOVE Lite](./docs/cvmap/ReMove.md)
    - Added option to start playback automatically after recording 
    - Added SMTH-parameter for linear smoothing especially on jumps at sequence end and sequence change (#14)
- All stoermelder-modules are now shipped with updated panels and minor layout fixes

## 1.2.0

- Module [4ROUNDS](./docs/fourrounds/FourRounds.md)
    - New module, randomizer for up to 16 input signals to create 15 output signals
- Module [SIPO](./docs/sipo/Sipo.md)
    - New module, serial-in parallel-out shift register with polyphonic output and CV controls for skipping and incrementing on sampled values
- Module [8FACE](./docs/eightface/EightFace.md)
    - Added option to switch between left and right side controlled module (#50)
    - Follow voltage standards for Rack (ignore SLOT for 1ms after trigger on RESET)
- Module [µMAP](./docs/cvmap/CVMapMicro.md)
    - Added option to invert output (#46)
    - Selected voltage range (-5V..5V or 0V..10V) is also used for OUT-port
- Module [ReMOVE Lite](./docs/cvmap/ReMove.md)
    - Value is set to the mapped parameter when using IN-port for recording (#48)
    - Added EOC-mode for OUT-port (#47)
    - Follow voltage standards for Rack (ignore SEQ# for 1ms after trigger on RESET)
- Module [STRIP](./docs/strip/Strip.md)
    - Changed LEDs to triangle shape
    - Load and save dialogs default to "patches" folder of the current user (#41)
- All stoermelder-modules are now shipped with dark mounting-screws, improved jack-ports and handy trimpots

## 1.1.0

- Module [MIDI-CAT](./docs/midicat/MidiCat.md)
    - New module, mapping module similar to VCV's MIDI-MAP with midi feedback, note mapping and cc pickup-mode
- Module [8FACE](./docs/eightface/EightFace.md)
    - Using additional worker thread for applying presets to avoid engine deadlock on some modules (especially using parameter mapping)
    - Added trigger modes "reverse", "pingpong" and "random" for SLOT-port
    - Renamed "Clock"-mode to "Arm" for SLOT-port
    - Added option to autoload first preset on load of 8FACE presets
    - Fixed unusable SLOT-modes "0..10V" and "C4..G4"
- Module [µMAP](./docs/cvmap/CVMapMicro.md)
    - Added ventilation holes on the panel to prevent overheating
- Module [ReMOVE Lite](./docs/cvmap/ReMove.md)
    - Added playmode "sequence random" which walks randomly through all sequences
- Module [STRIP](./docs/strip/Strip.md)
    - Added button INC/EXC for including or excluding specific module parameter from randomization
- Modules [CV-MAP](./docs/cvmap/CVMap.md), [CV-PAM](./docs/cvmap/CVPam.md), [ReMOVE Lite](./docs/cvmap/ReMove.md), [µMAP](./docs/cvmap/CVMapMicro.md)
    - Fixed crash of Rack if deleting the module while in mapping mode

## 1.0.5

- Module [8FACE](./docs/eightface/EightFace.md)
    - New module, preset sequencer for 8 presets of any module
- Module [STRIP](./docs/strip/Strip.md)
    - Added "cut" for cut & paste in the context menu
- Module [ReMOVE Lite](./docs/cvmap/ReMove.md)
    - Added random automation-curves on "Randomize" of the module
    - Starting a recording generates an item in the Rack undo-history

## 1.0.4

- Module [STRIP](./docs/strip/Strip.md)
    - New module, manage a group of modules in a patch, providing load, save as, disable and randomize
- Module [ReMOVE Lite](./docs/cvmap/ReMove.md)
    - LEDs for RUN and RESET turn red when using PHASE-input
    - Added play mode "Sequence Loop"
    - Added record mode "Sample & Hold"
    - OUT-port can be used for monitoring while recording
    - OUT-port bypasses IN-port when selecting an empty sequence
    - Fixed bug when saving sequences with lots of constant values (compression-bug)

## 1.0.3

- Module [INFIX](./docs/infix/Infix.md)
    - New module, insert for polyphonic cables
- Module [µMAP](./docs/cvmap/CVMapMicro.md)
    - Fixed bug causing "damaged" module panels (array out of bounds)

## 1.0.2

- Module [ReMOVE Lite](./docs/cvmap/ReMove.md)
    - New module, allows recording of parameter automation of knobs, switches or sliders
- Module [BOLT](./docs/bolt/Bolt.md)
    - New module, polyphonic modulateable boolean functions
- Module [µMAP](./docs/cvmap/CVMapMicro.md)
    - New module, a single slot version of [CV-MAP](./docs/cvmap/CVMap.md) with attenuverter and output port
- Modules [CV-MAP](./docs/cvmap/CVMap.md), [CV-PAM](./docs/cvmap/CVPam.md)
    - Added context menu option (on LED display) to locate and blink indicator for mapped slots
    - Added context menu option to disable text scrolling
    - Added context menu option linking the online manual
    - Improved panels
- Module [ROTOR Model A](./docs/rotor/RotorA.md)
    - Reduced panel width
    
## 1.0.0

- Module [ROTOR Model A](./docs/rotor/RotorA.md)
    - New module
- Module [CV-MAP](./docs/cvmap/CVMap.md)
    - Added text scrolling for longer module and parameter names
    - Added context menu option to allow manual target parameter changes (Locked/Unlocked)
- Module [CV-PAM](./docs/cvmap/CVPam.md)
    - Added text scrolling for longer module and parameter names

## 1.0.0-rc

- Initial public release