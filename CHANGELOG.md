### 1.0.6 (in development)

- Module 8FACE
    - Using additional worker thread for applying presets to avoid engine deadlock on some modules (especially using parameter mapping)
- Module MIDI-CAT
    - New module, mapping module similar to VCV's MIDI-MAP with midi feedback and note mapping
- Module STRIP
    - Added button EX for excluding specific module parameter from randomization

### 1.0.5

- Module 8FACE
    - New module, preset sequencer for 8 presets of any module ([docs](./docs/EightFace.md))
- Module STRIP
    - Added "cut" for cut & paste in the context menu
- Module ReMOVE Lite
    - Added random automation-curves on "Randomize" of the module
    - Starting a recording generates an item in the Rack undo-history

### 1.0.4

- Module STRIP
    - New module, manage a group of modules in a patch, providing load, save as, disable and randomize ([docs](./docs/Strip.md))
- Module ReMOVE Lite
    - LEDs for RUN and RESET turn red when using PHASE-input
    - Added play mode "Sequence Loop"
    - Added record mode "Sample & Hold"
    - OUT-port can be used for monitoring while recording
    - OUT-port bypasses IN-port when selecting an empty sequence
    - Fixed bug when saving sequences with lots of constant values (compression-bug)

### 1.0.3

- Module INFIX
    - New module, insert for polyphonic cables ([docs](./docs/Infix.md))
- Module µMAP
    - Fixed bug causing "damaged" module panels (array out of bounds)

### 1.0.2

- Module ReMOVE Lite
    - New module, allows recording of parameter automation of knobs, switches or sliders ([docs](./docs/ReMove.md))
- Module BOLT
    - New module, polyphonic modulateable boolean functions ([docs](./docs/Bolt.md))
- Module µMAP
    - New module, a single slot version of CV-PAM with attenuverter and output port ([docs](./docs/CVMapMicro.md))
- Modules CV-MAP, CV-PAM
    - Added context menu option (on LED display) to locate and blink indicator for mapped slots
    - Added context menu option to disable text scrolling
    - Added context menu option linking the online manual
    - Improved panels
- Module ROTOR Model A
    - Reduced panel width
    
### 1.0.0

- Module ROTOR Model A
    - New module
- Module CV-MAP
    - Added text scrolling for longer module and parameter names
    - Added context menu option to allow manual target parameter changes (Locked/Unlocked)
- Module CV-PAM
    - Added text scrolling for longer module and parameter names

### 1.0.0-rc

- Initial public release