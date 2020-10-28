## 1.x.x (in development)

- Module [FLOWER, SEEDS, OFFSPRING](./docs/Flower.md)
    - New modules, pattern-driven 16-step sequencer
- Module [MACRO](./docs/Macro.md)
    - New module, marco-knob for four parameter-mappings and two CV outputs
- Module [RAW](./docs/Raw.md)
    - New module, a digital effect based on the dynamics of bistable systems

### Fixes and Changes

- Module [CV-PAM](./docs/CVPam.md)
    - Fixed wrong channel count of the polyphonic output ports
- Modules [8FACE, 8FACEx2](./docs/EightFace.md)
    - Fixed hanging pingpong-mode when changing slots manually (#191, #203)
    - Added trigger-option "pseudo-random"
- Module [GRIP](./docs/Grip.md)
    - Fixed crash on locking more than 32 parameters (#176)
- Module [MEM](./docs/MidiCat.md#mem-expander)
    - Added support for MIDI-CAT's new slew-limiting and scaling options
- Module [µMAP](./docs/CVMapMicro.md)
    - Added input voltage display
- Module [MIDI-CAT](./docs/MidiCat.md)
    - Added context menu sliders for MIDI filtering/slew-limiting for CCs and notes (#79)
    - Added context menu sliders for scaling or transforming the MIDI-input and parameter-range (#169)
    - Added context menu options for precision/CPU-usage
    - Added context menu options on mapped parameters of target module for MIDI-CAT
    - Added skipping of current slot with SPACE-key while in mapping-mode
    - Added context menu option for clearing all mapping-slots
    - Added option for ignoring MIDI device settings on preset load (#185)
    - Fixed broken "Re-send MIDI feedback" option
    - Added context menu option for re-sending MIDI feedback periodically
- Module [MIDI-STEP](./docs/MidiStep.md)
    - Added option for Akai MPD218
- Module [MIRROR](./docs/Mirror.md)
    - Added syncing of module presets even if bound module has no parameters (#189)
    - Added hotkey for syncing module presets
- Module [STRIP](./docs/Strip.md)
    - Added hotkeys Shift+L (load), Shift+S (save as), Shift+X (cut)
    - Added context menu option "Load with replace" (#186)
    - Fixed crash on loading vcvss-files with missing modules
- Module [STROKE](./docs/Stroke.md)
    - Added command "Toggle engine pause"
    - Added command "Toggle lock modules"
    - Added LEDs for signaling a triggered hotkey
- Module [TRANSIT](./docs/Transit.md)
    - Added trigger-option "pseudo-random"
    - Added trigger-option "random walk"
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