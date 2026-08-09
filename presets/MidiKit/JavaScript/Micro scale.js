/**
 * @target stoermelder MIDI-KIT
 * @engine QuickJs@v1
 * @author stoermelder
 * @description Retunes notes to a micro scale (Scala-style), expressing the residual tuning as per-channel pitch bend
 */

// Micro scale processor for MIDI-KIT
//
// A port of the classic "micro tuning" processor (see the Blokas MIDIHub
// Micro Scale pipe): every incoming note is mapped onto a scale defined as
// cents offsets per degree, its exact frequency computed, then re-expressed
// as the nearest 12-EDO note number plus a pitch bend carrying the residual
// cents. This is how Scala .scl files are actually played back - each key
// position is retuned rather than "snapped" like the Scale quantiser.
//
// Because pitch bend is channel-global in MIDI, every note is dispatched to
// its own output channel (round-robin over config.channels), so each voice
// carries an independent bend - exactly why the MIDIHub pipe needs its 16
// "Use Ch" switches. Enable one channel per simultaneous voice your synth
// has, or use fewer and accept note stealing.
//
// The tuning is defined by a Scala .scl file pasted into config.scl below -
// the same text you would load with "LOAD filename.scl" in the Scala
// program. onLoad parses it into a list of cents offsets per octave degree
// (first entry always 0 = the tonic at baseNote); ratios like "5/4" are
// converted with ratioToCents(numer, denom) = 1200 * log2(numer / denom).
// See https://www.huygens-fokker.org/scala/ for the format.
//
// Note-Off handling is the part worth reading, the same trap as the Scale
// quantiser: the note actually sent may be a *different* number than the one
// played (it is the nearest semitone to the tuned frequency), so the Note-Off
// that arrives later must be redirected to the channel and note number the
// Note-On actually used. state.queueOfNote is a per-note FIFO of output
// channels so even a unison (same note played twice) releases its voices in
// press order.
//
// onUnload releases every still-sounding note when the script is replaced,
// the module is reset, or the module is removed - without it a note held at
// that moment would hang forever, since nothing else remembers which channels
// are down once this script's state is gone.


// Configuration - change these values as needed
let config = {
    // Paste the contents of a Scala .scl file between the backticks below -
    // exactly as saved by the Scala program or downloaded from the Scala
    // archive (https://www.huygens-fokker.org/scala/). The format is:
    //   ! optional comment lines
    //   <description>
    //   <number of notes per octave>
    //   <note 1> ... <note n>
    // Each note is a frequency ratio like "5/4", or a cents value written
    // with a decimal point like "386.314" (a bare integer is a ratio). The
    // tonic 1/1 is implicit, and the last note is the octave (2/1 or 1200.0)
    // which the scale repeats every octave.
    scl: `! Micro scale.scl
!
5-limit just intonation, major scale
 7
!
 9/8
 5/4
 4/3
 3/2
 5/3
 15/8
 2/1`,

    // Base MIDI note the scale is anchored to, and its frequency in Hz.
    // baseNote 60 @ 261.625565 Hz is middle C in A440 tuning.
    baseNote: 60,
    baseFreq: 261.625565,

    // Pitch bend depth of the receiving synth, in semitones. This must match
    // the synth's configured bend range or every detune will be off by a
    // factor. 2 semitones resolves the residual cents of any scale.
    bendDepth: 2,

    // Output channels to dispatch notes to, round-robin. Each simultaneous
    // note needs its own channel (pitch bend is channel-global), so enable as
    // many as your synth has voices.
    channels: [1, 2, 3, 4, 5, 6, 7, 8],

    // Always send a pitch bend on every Note-On, even when unchanged. Off
    // relies on the receiver remembering the last bend per channel (smaller
    // stream); turn on only if the receiving device plays wrong notes.
    alwaysSendBend: false,

    // Only process this input channel; 0 = every channel
    channel: 0
};

// Internal state, indexed by 1-based output channel.
// noteOfChannel[c]     = incoming note currently sounding on output channel c (-1 = free).
// sentNoteOfChannel[c] = the note number actually sent there (may differ from the incoming one).
// bendOfChannel[c]     = the last pitch wheel value sent there, so unchanged bends can be skipped.
// queueOfNote[n]       = FIFO of output channels note n was dispatched to, so a Note-Off releases
//                        the exact channel(s) and a unison (same note twice) releases in press order.
let state = {
    noteOfChannel: [],
    sentNoteOfChannel: [],
    bendOfChannel: [],
    queueOfNote: [],
    cursor: -1
};

// The parsed scale, filled in onLoad from config.scl: cents offsets per
// octave degree, scale[0] = 0 (the tonic at baseNote). The tonic is implicit
// and the octave entry (1200 cents) is not stored - the scale repeats.
let scale = [0];

// Converts a Scala frequency ratio to cents: 1200 * log2(numer / denom).
function ratioToCents(numer, denom) {
    return 1200 * Math.log2(numer / denom);
};

// Converts one .scl note line to cents. Per the Scala convention a value
// containing a slash is a frequency ratio, one containing a decimal point is
// already a cents value, and a bare integer is a ratio. Returns null when
// the line is not a note.
function parseSclNote(s) {
    let m = s.match(/^(\d+(?:\.\d+)?)\s*\/\s*(\d+(?:\.\d+)?)$/);
    if (m) {
        let denom = parseFloat(m[2]);
        if (denom === 0) return null;
        return ratioToCents(parseFloat(m[1]), denom);
    }
    if (s.indexOf('.') >= 0) return parseFloat(s);
    let v = parseFloat(s);
    if (isFinite(v) && v > 0) return ratioToCents(v, 1);
    return null;
};

// Parses a pasted Scala .scl file into the cents-per-degree list. Lines
// starting with "!" are comments. In the standard layout the first line is a
// description, the second the note count and the following lines the notes;
// a paste that omits the description line is tolerated (the count is first).
// The tonic (0 cents) and the octave (1200 cents) entries are dropped - the
// tonic is implicit at scale[0] and the octave is implied by the repetition.
function parseScl(content) {
    let lines = [];
    for (let raw of String(content).split(/\r?\n/)) {
        let s = raw.trim();
        if (s.length === 0) continue;
        if (s[0] === '!') continue;
        lines.push(s);
    }

    let start = 0;
    if (lines.length >= 2 && /^\d+$/.test(lines[1])) {
        start = 2;                    // lines[0] = description, lines[1] = count
    } else if (lines.length >= 1 && /^\d+$/.test(lines[0])) {
        start = 1;                    // no description; lines[0] = count
    }
    // else: no count line found - treat every line as a note.

    let scale = [0];
    for (let i = start; i < lines.length; i++) {
        let cents = parseSclNote(lines[i]);
        if (cents === null) continue;
        if (cents > 0 && cents < 1200) scale.push(cents);
    }
    return scale;
};

// The tuned pitch of a MIDI note relative to baseNote, in cents.
function noteToCents(note) {
    let rel = note - config.baseNote;
    let size = scale.length;
    let oct = Math.floor(rel / size);
    let deg = ((rel % size) + size) % size;
    return oct * 1200 + scale[deg];
};

// The pitch wheel value for a deviation from the sent note, in semitones.
function bendToPitchWheel(semis) {
    let pw = 8192 + Math.round(semis / config.bendDepth * 8192);
    if (pw < 0) pw = 0;
    if (pw > 16383) pw = 16383;
    return pw;
};

// Sends a pitch wheel on output channel ch. Skipped when the value is
// unchanged and alwaysSendBend is off - the receiver still remembers the last
// bend on that channel, the data-size optimisation the MIDIHub pipe has.
function sendBend(ch, pw) {
    if (!config.alwaysSendBend && pw === state.bendOfChannel[ch]) return;
    let bend = midi.create();
    midi.setPitchWheel(bend, ch, pw);
    midiOut.send(bend);
    state.bendOfChannel[ch] = pw;
};

rack.onLoad = function() {
    scale = parseScl(config.scl);

    for (let c = 1; c <= 16; c++) {
        state.noteOfChannel[c] = -1;
        state.sentNoteOfChannel[c] = -1;
        state.bendOfChannel[c] = 8192;
    }
    for (let n = 0; n < 128; n++) {
        state.queueOfNote[n] = [];
    }
    rack.log("Micro scale initialized");
    rack.log("Scale degrees: ", scale.length - 1, " per octave (parsed from config.scl)");
    if (scale.length < 2) rack.log("WARNING: no scale notes parsed - check the pasted .scl in config.scl");
    rack.log("Base: ", config.baseNote, " @ ", number.toString(config.baseFreq), " Hz");
    rack.log("Bend depth: ", number.toString(config.bendDepth), " st");
};

rack.onUnload = function() {
    for (let c = 1; c <= 16; c++) {
        if (state.noteOfChannel[c] >= 0) {
            let off = midi.create();
            midi.setNoteOff(off, c, state.sentNoteOfChannel[c]);
            midiOut.send(off);
        }
    }
};

function matchesChannel(ch) {
    return config.channel === 0 || ch === config.channel;
};

// Finds the output channel for a new note: the next free channel in
// round-robin order, or the next channel when all are busy (note stealing).
function allocateChannel() {
    let n = config.channels.length;
    for (let i = 1; i <= n; i++) {
        let idx = (state.cursor + i) % n;
        let ch = config.channels[idx];
        if (state.noteOfChannel[ch] < 0) {
            state.cursor = idx;
            return ch;
        }
    }
    state.cursor = (state.cursor + 1) % n;
    return config.channels[state.cursor];
};

// Removes `ch` from the FIFO of an incoming note (used when a channel is
// stolen, so the displaced note's later Note-Off doesn't release the thief).
function removeFromQueue(note, ch) {
    let q = state.queueOfNote[note];
    for (let i = 0; i < q.length; i++) {
        if (q[i] === ch) {
            q.splice(i, 1);
            return;
        }
    }
};

// Context menu - right-click the module to change these settings live.
// Each menu mirrors a `config` value above; onChange applies the choice.
let CHANNEL_LABELS = ["All"];
for (let c = 1; c <= 16; c++) CHANNEL_LABELS[CHANNEL_LABELS.length] = String(c);

rack.registerContextMenu({
    type: "options",
    label: "Input channel",
    options: CHANNEL_LABELS,
    onGetValue: function() {
        return config.channel;
    },
    onChange: function(idx) {
        config.channel = idx;
        rack.log("Input channel: ", CHANNEL_LABELS[idx]);
    }
});

rack.registerContextMenu({
    type: "boolean",
    label: "Always send pitch bend",
    onGetValue: function() {
        return config.alwaysSendBend;
    },
    onChange: function(checked) {
        config.alwaysSendBend = checked;
    }
});

midi.onMessage = function(midiPort, msg) {
    let ch = midi.getChannel(msg);

    if (!matchesChannel(ch)) {
        midiOut.send(msg);
        return;
    }

    let vel = midi.isNoteOn(msg) ? midi.getValue(msg) : 0;
    let isOn = midi.isNoteOn(msg) && vel > 0;
    // Velocity 0 is the running-status spelling of a Note-Off.
    let isOff = midi.isNoteOff(msg) || (midi.isNoteOn(msg) && vel === 0);

    if (isOn) {
        let note = midi.getNote(msg);

        // Tuned frequency of the note, then back to the nearest 12-EDO note
        // number plus the residual bend in semitones.
        let cents = noteToCents(note);
        let freq = config.baseFreq * Math.pow(2, cents / 1200);
        let semis = 12 * Math.log2(freq / config.baseFreq) + config.baseNote;
        let outNote = Math.round(semis);
        if (outNote < 0) outNote = 0;
        if (outNote > 127) outNote = 127;
        let bendSemis = semis - outNote;
        let pw = bendToPitchWheel(bendSemis);

        let outCh = allocateChannel();

        // If we stole a channel, drop the note it was holding so the stale
        // Note-Off for it doesn't release the new voice.
        let displaced = state.noteOfChannel[outCh];
        if (displaced >= 0) {
            removeFromQueue(displaced, outCh);
        }

        state.noteOfChannel[outCh] = note;
        state.sentNoteOfChannel[outCh] = outNote;
        state.queueOfNote[note].push(outCh);

        sendBend(outCh, pw);

        let on = midi.create();
        midi.setNoteOn(on, outCh, outNote, vel);
        midiOut.send(on);

        return;
    }

    if (isOff) {
        let note = midi.getNote(msg);
        let q = state.queueOfNote[note];
        // Release the oldest channel this note was dispatched to, skipping any
        // stale entry left by a stolen channel. One Note-Off releases one
        // voice, so a unison needs two Note-Offs like the keyboard sent.
        while (q.length > 0) {
            let outCh = q.shift();
            if (state.noteOfChannel[outCh] === note) {
                let off = midi.create();
                midi.setNoteOff(off, outCh, state.sentNoteOfChannel[outCh]);
                midiOut.send(off);
                state.noteOfChannel[outCh] = -1;
                state.sentNoteOfChannel[outCh] = -1;
                return;
            }
        }
        return;
    }

    // Everything else (CC, program change, realtime, ...) passes through.
    midiOut.send(msg);
};
