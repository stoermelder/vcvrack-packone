### 1.3.0

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

### 1.2.0

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

### 1.1.0

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

### 1.0.5

- Module [8FACE](./docs/EightFace.md)
    - New module, preset sequencer for 8 presets of any module
- Module [STRIP](./docs/Strip.md)
    - Added "cut" for cut & paste in the context menu
- Module [ReMOVE Lite](./docs/ReMove.md)
    - Added random automation-curves on "Randomize" of the module
    - Starting a recording generates an item in the Rack undo-history

### 1.0.4

- Module [STRIP](./docs/Strip.md)
    - New module, manage a group of modules in a patch, providing load, save as, disable and randomize
- Module [ReMOVE Lite](./docs/ReMove.md)
    - LEDs for RUN and RESET turn red when using PHASE-input
    - Added play mode "Sequence Loop"
    - Added record mode "Sample & Hold"
    - OUT-port can be used for monitoring while recording
    - OUT-port bypasses IN-port when selecting an empty sequence
    - Fixed bug when saving sequences with lots of constant values (compression-bug)

### 1.0.3

- Module [INFIX](./docs/Infix.md)
    - New module, insert for polyphonic cables
- Module [µMAP](./docs/CVMapMicro.md)
    - Fixed bug causing "damaged" module panels (array out of bounds)

### 1.0.2

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
    
### 1.0.0

- Module [ROTOR Model A](./docs/RotorA.md)
    - New module
- Module [CV-MAP](./docs/CVMap.md)
    - Added text scrolling for longer module and parameter names
    - Added context menu option to allow manual target parameter changes (Locked/Unlocked)
- Module [CV-PAM](./docs/CVPam.md)
    - Added text scrolling for longer module and parameter names

### 1.0.0-rc

- Initial public release