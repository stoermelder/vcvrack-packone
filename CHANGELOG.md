## 1.10.x (in development)

- Module [DIRT](./docs/Dirt.md)
    - New module, crosstalk and noise for polyphonic cables
- Modules [FLOWER, SEEDS, OFFSPRING](./docs/Flower.md)
    - New modules, pattern-driven 16-step sequencer
- Module [PRISMA](./docs/Prisma.md)
    - New module, a wave multiplier/phase shifter inspired by A-137-2
- Module [STRIP-BLOCK](./docs/Strip.md)
    - New module, a companion module for STRIP for blocking STRIP's expander-mechanism

## 1.9.0

- Modules [8FACE mk2, +8](./docs/EightFaceMk2.md)
    - New modules, evolution of 8FACE and 8FACEx2 (#63 #76 #144 #154 #157 #158 #160 #162)
- Module [CV-MAP CTX](./docs/CVMap.md#ctx-expander)
    - New expander-module for CV-MAP, helper for mapping parameters by context menu (#256)
- Module [MIDI-CAT CTX](./docs/MidiCat.md#ctx-expander)
    - New expander-module for MIDI-CAT, helper for mapping parameters by context menu (#232, #250)
- Module [MIDI-PLUG](./docs/MidiPlug.md)
    - New module, a virtual MIDI merger and splitter
    - MIDI "Loopback" driver for routing outgoing MIDI messages back into Rack (enabled on the context menu)
- Module [ORBIT](./docs/Orbit.md)
    - New module, a polyphonic stereo field spreader
- Module [STRIP-BAY](./docs/Strip.md#stoermelder-strip-bay)
    - New module, a companion module for STRIP for keeping input/output connections while replacing strips
- Module [ME](./docs/Me.md)
    - New experimental module for "mouse enhacements", provides a screen overlay for parameters changes

### Fixes and Changes

- Modules [8FACE, 8FACEx2](./docs/EightFace.md)
    - Load preset in Arm-mode even when the same slot was selected before (#212)
    - Improved thread-handling for crashes when used with specific modules (#76)
    - Added an option for auto-loading the last active preset
    - Added "Off" as SLOT mode (#249)
    - Fixed broken "Autoload first preset" (#29)
- Module [CV-MAP](./docs/CVMap.md)
    - Added context menu sliders for slew and scaling and transforming the input and parameter-range ([manual](./docs/CVMap.md#slew-limiting-and-input-scaling)) (#243)
    - Added arbitrary channel routings to allow every input channel assigned to any mapping slot ([manual](./docs/CVMap.md#channel-routing))
    - Added context menu on the input ports for custom labeling the channels ([manual](./docs/CVMap.md#input-labels)) (#256)
    - Fixed wrong slot behavior when toggling input to 0V (#221)
- Module [GLUE](./docs/Glue.md)
    - Added option to consolidate all GLUE modules into the current one ([manual](./docs/Glue.md#consolidate))
- Module [HIVE](./docs/Hive.md)
    - Fixed hanging ratchets on missing or stopped clock trigger (#216)
    - Added new ratcheting modes ("Twos", "Threes", "Power of Two") ([manual](./docs/Maze.md#ratchet))
- Module [MACRO](./docs/Macro.md)
    - Added context menu options on mapped parameters of target module for MACRO
- Module [MAZE](./docs/Maze.md)
    - Fixed hanging ratchets on missing or stopped clock trigger (#216)
    - Added new ratcheting modes ("Twos", "Threes", "Power of Two") ([manual](./docs/Maze.md#ratchet))
- Module [MB](./docs/Mb.md)
    - Added option to hide the "brands" section of the V1-browser (#223)
    - Added option to search module descriptions (https://github.com/stoermelder/vcvrack-packtau/pull/9)
- Module [µMAP](./docs/CVMapMicro.md)
    - Added context menu options on mapped parameters of target module for µMAP ([manual](./docs/CVMapMicro.md#target-context))
    - Fixed wrong behavior when toggling input to 0V (#221)
- Module [MIDI-CAT](./docs/MidiCat.md)
    - Added support for MIDI 14-bit CC ([manual](./docs/MidiCat.md#14-bit-cc))
    - Added toggle-modes for MIDI CC mappings ([manual](./docs/MidiCat.md#toggle-cc)) (#225)
    - Added an overlay showing current parameter changes on the bottom of the screen ([manual](./docs/MidiCat.md#overlay))
    - Fixed crash when binding modules with more than 128 parameters (#234)
    - Added option for clearing mapping slots on preset load (#259)
- Module [MIDI-MON](./docs/MidiMon.md)
    - Added support for more message types (program change, song select, song pointer)
    - Added context menu option for clearing the log
- Module [MIDI-STEP](./docs/MidiStep.md)
    - Fixed relative modes for Behringer X-Touch (#240)
    - Fixed duplicate mappings of the same CC (#240)
- Module [RAW](./docs/Raw.md)
    - Added basic limiting to prevent rare instabilities (#214)
- Module [SAIL](./docs/Sail.md)
    - Added an overlay showing current parameter changes on the bottom of the screen
    - Block adjustments on switch-parameters to avoid undefined behavior
- Module [SPIN](./docs/Spin.md)
    - Improved transition between scrolling and parameter adjustments on hovering (#260)
- Module [STRIP](./docs/Strip.md)
    - Added context menu option "Load and replace" to preset-submenu (#215)
    - Added support for sub-folders in preset-submenu (#230)
- Module [STROKE](./docs/Stroke.md)
    - Allow mapping mouse buttons 0/1/2 (left/right/middle) in use with modifiers
    - Fixed not working mappings caused by Num Lock state (#220)
    - Fixed not working mappings caused by use of numpad keys (#220)
    - Added view-commands using smooth transitions (#139)
    - Added "Add module" command ([manual](./docs/Stroke.md#add-module))
    - Added "Send hotkey to module" command ([manual](./docs/Stroke.md#module-send-hotkey))
    - Added scroll-commands ([manual](./docs/Stroke.md#view-scroll)) (#252)
    - Added tooltips for mapped commands
- Module [TRANSIT](./docs/Transit.md)
    - Added "Phase"-mode for CV-input which scans continously through snapshots ([manual](./docs/Transit.md#phase)) (#182)
    - Added context menu option "Locate and indicate" for bound parameters
    - Added context menu option for custom text labels
    - Improved performance of +T expanders

## 1.8.0

- Module [HIVE](./docs/Hive.md)
    - New module, 4 channel sequencer running on a 2-dimensional hexagonal grid
- Module [MACRO](./docs/Macro.md)
    - New module, marco-knob for four parameter-mappings and two CV outputs
- Module [MB](./docs/Mb.md)
    - New module, experimental replacement for Rack's module browser, formerly available in Stoermelder's PackTau
- Module [MIDI-MON](./docs/MidiMon.md)
    - New module, input-monitor for MIDI messages
- Module [RAW](./docs/Raw.md)
    - New module, a digital effect based on the dynamics of bistable systems

### Fixes and Changes

- Module [ARENA](./docs/Arena.md)
    - Fixed noise on OUT-ports (#190)
- Module [CV-PAM](./docs/CVPam.md)
    - Fixed wrong channel count of the polyphonic output ports
- Modules [8FACE, 8FACEx2](./docs/EightFace.md)
    - Fixed hanging pingpong-mode when changing slots manually (#191, #203)
    - Added trigger-options "pseudo-random", "random walk", "alternating", "shuffle" ([manual](./docs/EightFace.md#trigger-modes))
- Module [GRIP](./docs/Grip.md)
    - Fixed crash on locking more than 32 parameters (#176)
- Module [INTERMIX](./docs/Intermix.md)
    - Added support for polyphony (#199)
- Module [MEM](./docs/MidiCat.md#mem-expander)
    - Added support for MIDI-CAT's new slew-limiting and scaling options ([manual](./docs/MidiCat.md#slew-limiting-and-input-scaling))
    - Added scanning for next or previous modules with stored mapping ([manual](./docs/MidiCat.md#mem-scan)) (#200)
- Module [µMAP](./docs/CVMapMicro.md)
    - Added input voltage display
- Module [MIDI-CAT](./docs/MidiCat.md)
    - Added context menu sliders for MIDI filtering/slew-limiting for CCs and notes ([manual](./docs/MidiCat.md#slew-limiting)) (#79)
    - Added context menu sliders for scaling or transforming the MIDI-input and parameter-range ([manual](./docs/MidiCat.md#input-scaling)) (#169)
    - Added context menu options for precision/CPU-usage ([manual](./docs/MidiCat.md#precision))
    - Added context menu options on mapped parameters of target module for MIDI-CAT ([manual](./docs/MidiCat.md#target-context))
    - Added skipping of current slot with SPACE-key while in mapping-mode
    - Added context menu option for clearing all mapping-slots
    - Added option for ignoring MIDI device settings on preset load (#185)
    - Fixed broken "Re-send MIDI feedback" option
    - Added context menu option for re-sending MIDI feedback periodically ([manual](./docs/MidiCat.md#feedback-periodically))
    - Added note-mode "Toggle + Velocity" ([manual](./docs/MidiCat.md#toggle-velocity))
- Module [MIDI-STEP](./docs/MidiStep.md)
    - Added option for Akai MPD218 ([manual](./docs/MidiStep.md#akai-mpd218))
- Module [MIRROR](./docs/Mirror.md)
    - Added syncing of module presets even if bound module has no parameters (#189)
    - Added hotkey for syncing module presets
- Module [STRIP](./docs/Strip.md)
    - Added hotkeys Shift+L (load), Shift+S (save as), Shift+X (cut)
    - Added context menu option "Load with replace" ([manual](./docs/Strip.md#load-and-replace)) (#186)
    - Fixed crash on loading vcvss-files with missing modules
    - Added context menu option for custom presets, listing all .vcvss-files in folder presets/Strip ([manual](./docs/Strip.md#preset)) (#198)
- Module [STROKE](./docs/Stroke.md)
    - Added commands "Toggle engine pause", "Toggle lock modules"
    - Added command "Toggle busboard"
    - Added LEDs for signaling an activated hotkey
    - Allow loading presets (#187)
    - Improved behavior of command "Cable opacity" across restarts of Rack (#197)
- Module [TRANSIT](./docs/Transit.md)
    - Fixed hanging pingpong-mode when changing slots manually
    - Added trigger-options "pseudo-random", "random walk", "alternating", "shuffle" ([manual](./docs/Transit.md#sequencing-and-selecting-snapshots))
    - Fixed broken snapshots on save after mapped modules have been deleted (#205)
- Module [X4](./docs/X4.md)
    - Fixed advancing to the lower button after the upper button has been mapped
    - Fixed wrong tooltip of lower mapping button

## 1.7.1

### Fixes and Changes

- Module [TRANSIT](./docs/Transit.md)
    - Fixed worng snapshot-count when using +T expander after loading a patch

## 1.7.0

- Module [MEM](./docs/MidiCat.md#mem-expander)
    - New expander-module for MIDI-CAT, storage-unit for MIDI mapping-presets with MIDI-CAT
- Module [SPIN](./docs/Spin.md)
    - New module, converts mouse-wheel or middle mouse-button events into triggers
- Module [STROKE](./docs/Stroke.md)
    - New module, converts used-defined hotkeys into triggers or gates, also provides some special commands for Rack's enviroment
- Module [TRANSIT](./docs/Transit.md)
    - New module, parameter-morpher and sequencer for up to 96 snapshots
- Module [+T](./docs/Transit.md#stoermelder-t-expander)
    - New module, expander for TRANSIT
- Module [X4](./docs/X4.md)
    - New module, dual multiple for parameter-mapping

### Fixes and Changes

- Module [ARENA](./docs/Arena.md)
    - Fixed wrong calculation of output levels (#147, #113)
- Module [CV-MAP](./docs/CVMap.md)
    - Don't capture mouse scrolling if mapping slots are locked (#137)
    - Blink mapping indicator of currently selected mapping slot
- Module [CV-PAM](./docs/CVPam.md)
    - Don't capture mouse scrolling if mapping slots are locked (#137)
    - Blink mapping indicator of currently selected mapping slot
- Module [GLUE](./docs/Glue.md)
    - Implemented support for labels within STRIP, please be aware to include GLUE within your strip-file (#115)
    - Added options for changing text coloring (#136)
- Module [GRIP](./docs/Grip.md)
    - Implemented support for parameter-mappings within STRIP (#151)
- Module [GOTO](./docs/Goto.md)
    - Added support for number pad keys (#134)
- Module [µMAP](./docs/CVMapMicro.md)
    - Fixed meaningless tooltip on Map-button
    - Blink mapping indicator when activating the mapping button
- Module [MIDI-CAT](./docs/MidiCat.md)
    - Don't capture mouse scrolling if mapping slots are locked (#137)
    - Blink mapping indicator of currently selected mapping slot
    - Added option for automatic mapping of all parameters of a module on the left side or by module-select
    - Mapping is aborted using ESC-key while hovering the mouse over the module
    - Added option for sending MIDI "note on, velocity 0" on feedback for note off (#130)
- Module [MIRROR](./docs/Mirror.md)
    - Implemented support for parameter-mappings within STRIP
- Module [ReMOVE Lite](./docs/ReMove.md)
    - Blink mapping indicator when activating the mapping screen
- Module [STRIP](./docs/Strip.md)
    - Added option to randomize only parameters without the module's internal state (#135) 

## 1.6.3

### Fixes and Changes

- Module [GLUE](./docs/Glue.md)
    - Fixed crash on loading patches with empty labels

## 1.6.2

### Fixes and Changes

- Module [MIDI-STEP](./docs/MidiStep.md)
    - Fixed port numbering

## 1.6.1

### Fixes and Changes

- Module [GLUE](./docs/Glue.md)
    - Fixed invalid initialization on new instances
- Module [MIDI-CAT](./docs/MidiCat.md)
    - Added option for re-sending MIDI feedback values
- Module [ROTOR mod A](./docs/RotorA.md)
    - Indicate inactive output channels in blue

## 1.6.0

- Module [AFFIX](./docs/Affix.md), [µAFFIX](./docs/Affix.md)
    - New modules, inserts for polyphonic cables for adding offsets in volt, semitones or ocatves
- Module [GLUE](./docs/Glue.md)
    - New module, label maker for your modules!
- Module [GOTO](./docs/Goto.md)
    - New module, utility for jumping directly to 10 locations in your patch by hotkey or using MIDI
- Module [GRIP](./docs/Grip.md)
    - New module, lock for module parameters
- Module [MIRROR](./docs/Mirror.md)
    - New module, utility for synchronizing module parameters
- Module [POLY-PILE](./docs/Pile.md)
    - New module, polyphonic version of PILE

### Fixes and Changes

- Module [MAZE](./docs/Maze.md)
    - Added option for disabling normalization to the yellow input ports (#95)
    - Added independent ratcheting settings for each sequencer-playhead (#94)
- Module [MIDI-STEP](./docs/MidiStep.md)
    - Added option for polyphonic output for all channels on port 1
    - Increased number of CCs to 16 although 9-16 can only be used in polyphonic mode
- Module [PILE](./docs/Pile.md)
    - Removed slew-limiting after preset-load
- Module [ROTOR mod A](./docs/RotorA.md)
    - Allow bipolar carrier signal / remove clamping on 0..10V
    - Added offset for output channel number (#121)
- Module [SAIL](./docs/Sail.md)
    - Rewritten how the target values are applied onto the parameters (#106). You can't use IN and INC/DEC the same time anymore, just use two instances of the  module.

## 1.5.0

- Module [MIDI-STEP](./docs/MidiStep.md)
    - New module, utility for relative modes of endless knobs on your MIDI controller such as Arturia Beatstep
- Module [PILE](./docs/Pile.md)
    - New module, translate increment triggers or decrement triggers into an absolute voltage, especially useful with MIDI-STEP
- Module [SAIL](./docs/Sail.md)
    - New module, control any parameter currently hovered by mouse with CV, especially useful with MIDI-CC or MIDI-STEP

### Fixes and Changes

- Module [CV-MAP](./docs/CVMap.md)
    - Added option for hiding parameter indicator squares
    - Added option for locking mapping slots to prevent changes by accident (#89)
- Module [CV-PAM](./docs/CVPam.md)
    - Added option for hiding parameter indicator squares
    - Added option for locking mapping slots to prevent changes by accident (#89)
- Module [MIDI-CAT](./docs/MidiCat.md)
    - Added option for hiding parameter indicator squares
    - Added option for locking mapping slots to prevent changes by accident (#89)
    - Fixed broken toggle-mode for MIDI note-mapping
- Module [INFIX](./docs/Infix.md)
    - Added Leds for used channels on polyphonic cables
- Module [INTERMIX](./docs/Intermix.md)
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

- Module [INTERMIX](./docs/Intermix.md)
    - New module, precision adder 8x8 switch matrix with support for 8 scenes
- Module [ARENA](./docs/Arena.md)
    - Added missing bipolar-mode for X/Y-inputs of the mix-channels
- Module [CV-MAP](./docs/CVMap.md)
    - Added option to disable audio rate processing for lower cpu usage
- Module [CV-PAM](./docs/CVPam.md)
    - Added option to disable audio rate processing for lower cpu usage
- Module [MIDI-CAT](./docs/MidiCat.md)
    - Fixed wrong handling of CC-mappings in Direct-mode on parameter changes made within Rack
    - Added option to enter custom labels for mapping slots (#75)

## 1.3.0

- Module [ARENA](./docs/Arena.md)
    - New module, 2-dimensional XY-Mixer for 8 sources with various modulation targets and graphical interface
- Module [MAZE](./docs/Maze.md)
    - New module, 4 channel sequencer running on a 2-dimensional grid
- Module [8FACE](./docs/EightFace.md)
    - Revised panel design with combined LED and buttons
- Module [8FACEx2](./docs/EightFace.md)
    - New module, 8FACE with sixteen preset slots
- Module [µINFIX](./docs/Infix.md)
    - New module, 8 port variant of INFIX
- Module [µMAP](./docs/CVMapMicro.md)
    - Fixed meaningless tooltip on Map-button
- Module [MIDI-CAT](./docs/MidiCat.md)
    - Fixed velocity-handling on note messages if in toggle-mode (does not need vel 127 anymore)
- Module [STRIP](./docs/Strip.md)
    - Added utilization for Rack's undo-history on cutting strips and pasting/loading-strips (#11)
    - Added utilization for Rack's undo-history on enable/disable or randomize if triggered manually (#11)
- Module [ReMOVE Lite](./docs/ReMove.md)
    - Added option to start playback automatically after recording 
    - Added SMTH-parameter for linear smoothing especially on jumps at sequence end and sequence change (#14)
- All stoermelder-modules are now shipped with updated panels and minor layout fixes

## 1.2.0

- Module [4ROUNDS](./docs/FourRounds.md)
    - New module, randomizer for up to 16 input signals to create 15 output signals
- Module [SIPO](./docs/Sipo.md)
    - New module, serial-in parallel-out shift register with polyphonic output and CV controls for skipping and incrementing on sampled values
- Module [8FACE](./docs/EightFace.md)
    - Added option to switch between left and right side controlled module (#50)
    - Follow voltage standards for Rack (ignore SLOT for 1ms after trigger on RESET)
- Module [µMAP](./docs/CVMapMicro.md)
    - Added option to invert output (#46)
    - Selected voltage range (-5V..5V or 0V..10V) is also used for OUT-port
- Module [ReMOVE Lite](./docs/ReMove.md)
    - Value is set to the mapped parameter when using IN-port for recording (#48)
    - Added EOC-mode for OUT-port (#47)
    - Follow voltage standards for Rack (ignore SEQ# for 1ms after trigger on RESET)
- Module [STRIP](./docs/Strip.md)
    - Changed LEDs to triangle shape
    - Load and save dialogs default to "patches" folder of the current user (#41)
- All stoermelder-modules are now shipped with dark mounting-screws, improved jack-ports and handy trimpots

## 1.1.0

- Module [MIDI-CAT](./docs/MidiCat.md)
    - New module, mapping module similar to VCV's MIDI-MAP with midi feedback, note mapping and cc pickup-mode
- Module [8FACE](./docs/EightFace.md)
    - Using additional worker thread for applying presets to avoid engine deadlock on some modules (especially using parameter mapping)
    - Added trigger modes "reverse", "pingpong" and "random" for SLOT-port
    - Renamed "Clock"-mode to "Arm" for SLOT-port
    - Added option to autoload first preset on load of 8FACE presets
    - Fixed unusable SLOT-modes "0..10V" and "C4..G4"
- Module [µMAP](./docs/CVMapMicro.md)
    - Added ventilation holes on the panel to prevent overheating
- Module [ReMOVE Lite](./docs/ReMove.md)
    - Added playmode "sequence random" which walks randomly through all sequences
- Module [STRIP](./docs/Strip.md)
    - Added button INC/EXC for including or excluding specific module parameter from randomization
- Modules [CV-MAP](./docs/CVMap.md), [CV-PAM](./docs/CVPam.md), [ReMOVE Lite](./docs/ReMove.md), [µMAP](./docs/CVMapMicro.md)
    - Fixed crash of Rack if deleting the module while in mapping mode

## 1.0.5

- Module [8FACE](./docs/EightFace.md)
    - New module, preset sequencer for 8 presets of any module
- Module [STRIP](./docs/Strip.md)
    - Added "cut" for cut & paste in the context menu
- Module [ReMOVE Lite](./docs/ReMove.md)
    - Added random automation-curves on "Randomize" of the module
    - Starting a recording generates an item in the Rack undo-history

## 1.0.4

- Module [STRIP](./docs/Strip.md)
    - New module, manage a group of modules in a patch, providing load, save as, disable and randomize
- Module [ReMOVE Lite](./docs/ReMove.md)
    - LEDs for RUN and RESET turn red when using PHASE-input
    - Added play mode "Sequence Loop"
    - Added record mode "Sample & Hold"
    - OUT-port can be used for monitoring while recording
    - OUT-port bypasses IN-port when selecting an empty sequence
    - Fixed bug when saving sequences with lots of constant values (compression-bug)

## 1.0.3

- Module [INFIX](./docs/Infix.md)
    - New module, insert for polyphonic cables
- Module [µMAP](./docs/CVMapMicro.md)
    - Fixed bug causing "damaged" module panels (array out of bounds)

## 1.0.2

- Module [ReMOVE Lite](./docs/ReMove.md)
    - New module, allows recording of parameter automation of knobs, switches or sliders
- Module [BOLT](./docs/Bolt.md)
    - New module, polyphonic modulateable boolean functions
- Module [µMAP](./docs/CVMapMicro.md)
    - New module, a single slot version of [CV-MAP](./docs/CVMap.md) with attenuverter and output port
- Modules [CV-MAP](./docs/CVMap.md), [CV-PAM](./docs/CVPam.md)
    - Added context menu option (on LED display) to locate and blink indicator for mapped slots
    - Added context menu option to disable text scrolling
    - Added context menu option linking the online manual
    - Improved panels
- Module [ROTOR Model A](./docs/RotorA.md)
    - Reduced panel width
    
## 1.0.0

- Module [ROTOR Model A](./docs/RotorA.md)
    - New module
- Module [CV-MAP](./docs/CVMap.md)
    - Added text scrolling for longer module and parameter names
    - Added context menu option to allow manual target parameter changes (Locked/Unlocked)
- Module [CV-PAM](./docs/CVPam.md)
    - Added text scrolling for longer module and parameter names

## 1.0.0-rc

- Initial public release