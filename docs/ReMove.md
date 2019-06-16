# stoermelder ReMOVE Lite

ReMove Lite is a utility module for recording and replaying movements of any parameter on any module in the rack. It supports up to 8 recorded sequences, various  sampling rates, different recording-modes, a phase-input for directly controlling the playback and some more settings.

![ReMove Intro](./ReMove-intro.gif)

### Mapping of parameters

To map the module to a parameter activate the mapping mode by mouse press inside the display on the top. While showing "Mapping..." click on any parameter of any module in your rack to bind the module. You can unbind the parameter using the context menu of the display. Also, you can "locate" the module and mapped parameter if you got lost inside you rack.

### Precision and number of sequences

The module has storage of 96000 samples. At full sampling rate this corresponds to 2 seconds of recording at audio samplerate of 48kHz. Such high precision is not needed for parameter automation, so ReMOVE Lite allows at most an 8th of the audio sampling rate for recording. The lowest setting at 2048th sample gives you ~23 samples per second at audio samplerate of 48kHz and could still be ok for slowly changing parameters or low accuracy.
Be careful using higher rates: The recorded sequences are stored to the patch-file and these can get quite huge if several modules are used.

ReMOVE Lite can be configured to store 1 to 8 sequences. The maximum length for each sequence is evenly diveded, so you get 1/8 of the available recording time when using 8 sequences. The available time is shown in the context menu-option and in the display as soon as a recording starts. Changing the number of sequences resets all recordings.

Both precision setting and number of sequences are found on the context menu.

### Recording-Modes

There are three recording modes available, changed by the context menu option:

- Touch-Mode (Default):
Triggering the red REC button by mouse or through REC-port arms the recording. Actual recording starts on first mouse click ("touch") on the mapped parameter and holds on as long the button is pressed. On release of the button the recording stops.
- Move-Mode:
As with Touch-Mode the recording is armed. Actual recording starts on the first change of the mapped parameter, which happens not necessarily on the mouse down event. Releasing the mouse button ends the recording and the stored automation will be trimmed on the end to the last change of value. This way the sequence starts on first change and ends on the last one.
- Manual-Mode:
This mode starts the recording as soon as the red REC-button is pressed. Manual-mode is especially useful when triggering using the REC-input.

Recording is only possible when a parameter is mapped, even when using the IN-port.

### Play-Modes

Some modes for playback have been implemented:

- Loop: the playback loops through the sequence.
- Oneshot: the sequence is played once and must be retriggered by RESET.
- Ping Pong: the sequence loops, first played forward and then backward.

You can use the PHASE-input if you want a different playback speed or a completely different playback pattern.

### SEQ#-input

The SEQ#-input allows you to select the sequence by CV. There are three different modes available:

- 0..10V: The range is splitted evenly by 8. 0-1.25V selects sequence 1, 1.25-2.5V sequence 2 and so on.
- C4-G4: Keyboard mode, C4 triggers sequence 1, G4 triggers sequence 8.
- Trigger: When a trigger is received the module advances to the next sequence.

### PHASE-input

The input labeled PHASE accepts 0-10V and allows controlling the playhead directly: The voltages from 0 to 10V are mapped to the the length of the sequence. Using an LFO's unipolar saw output or a clock with phase output like [ZZC's Clock-module](https://zzc-cv.github.io/en/clock-manipulation/clock) the playback can be synced to sequencers. Obviously multiple instances can also be synchronized this way.

The ports RUN and RESET as well as their buttons are disabled and can't be used as long a cable is connected to PHASE.

![ReMove PHASE-input](./ReMove-phase.png)

### RESET- and RUN-ports

Same behaviour as most sequencers: RUN must be high for playback, a trigger on RESET restarts the currently selected playback mode from the beginning. Disabled when recording or when PHASE is connected.

![ReMove IN-input](./ReMove-reset.png)

### REC-input

REC-input is used for starting and stopping recordings by CV trigger. Be aware that a trigger with record modes Touch and Move only arms the recording.

### REC-output

The REC-output sends a trigger everytime a recording starts or stops.

### IN-input

The input labled IN accepts 0..10V or -5..5V (configuration option is found in the context menu) and can be used to record parameter movements from any external CV source. All parameter movements are ignored duing a recording when a cable is connected.

![ReMove IN-input](./ReMove-in.png)

### OUT-output

The OUT-port outputs voltage for the recorded sequence. It can be configured for 0..10V or -5..5V.

### Bonus-Features

- When duplicating the module all recorded sequences are also duplicated.
- The module can be re-mapped to another parameter after a sequence has been recorded.
- When changing the sampling rate of the module all recorded data will prevail and the playback-speed will be higher or slower.
- A simple compression is implemented to reduce the size of the patchfile.