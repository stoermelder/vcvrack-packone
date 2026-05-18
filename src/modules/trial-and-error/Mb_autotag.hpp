#pragma once
#include "Mb.hpp"
#include <string>
#include <vector>
#include <cstdio>

namespace StoermelderPackOne {
namespace Mb {

struct AutoTagResult {
	int total = 0;
	std::map<std::string, int> perTag;
	std::map<std::string, std::set<plugin::Model*>> assignments;

	void apply() const {
		for (const auto& pair : assignments) {
			for (plugin::Model* model : pair.second) {
				customTagAdd(model, pair.first);
			}
		}
	}
};

struct AutoTagRule {
	std::string tagName;
	std::string description;
	std::vector<std::string> keywords;
	// Words that must NOT appear anywhere in the module name or description
	// (case-insensitive substring). Use to block known false positives,
	// e.g. "format" for the "Formant" rule.
	std::vector<std::string> blockwords;
	// Base score threshold for multi-word keywords. Single-word keywords
	// automatically get +0.05 on top, because multi-word queries already
	// provide AND-logic filtering that single-word queries lack.
	float minScore;

	AutoTagRule(std::string t, std::string desc, std::vector<std::string> kw, std::vector<std::string> bl = {}, float ms = 0.7f)
		: tagName(std::move(t)), description(std::move(desc)), keywords(std::move(kw)), blockwords(std::move(bl)), minScore(ms) {}
};

// Curated keyword → custom tag mapping.
// Keywords are searched via the fuzzy DB (case-insensitive, substring + Lev matching),
// so short canonical forms are usually sufficient. Multiple keywords use OR logic.
// Only covers concepts not already represented by an official Rack tag.
static const std::vector<AutoTagRule> AUTO_TAG_RULES = {
	// Audio routing & format
	{"Matrix Mixer",        "N×M routing matrix that sends any input to any combination of outputs with individual level control.",
	                        {"matrix mixer", "matrix routing", "crosspoint mixer", "routing matrix"}},
	{"Crossfader",          "Blends between two or more audio or CV signals with a single control.",
	                        {"crossfader"}},
	{"Sidechain",           "Routes a control signal through an external path to drive dynamics or modulation processing.",
	                        {"sidechain", "side chain"}},
	{"Feedback",            "Intentional audio or CV feedback network or matrix for self-oscillation and complex timbres.",
	                        {"feedback matrix", "feedback network", "feedback routing"}, {"visual"}},

	// Synthesis — oscillator techniques
	{"Wavetable",           "Oscillator that scans through stored single-cycle waveforms for timbral variation.",
	                        {"wavetable oscillator", "wavetable synth", "wavetable synthesis", "wave table"}},
	{"Wavefolder",          "Folds a signal back on itself when it exceeds a threshold, adding upper harmonics.",
	                        {"wavefold", "wave fold", "buchla fold"}},
	{"FM Synthesis",        "Uses one oscillator's output to modulate the frequency of another for complex spectra.",
	                        {"fm synthesis", "fm synth", "frequency modulation synthesis"}},
	{"Through-Zero FM",     "FM synthesis where the carrier frequency passes below zero Hz, enabling symmetric and rich modulation.",
	                        {"through-zero", "tzfm", "thru-zero"}},
	{"Phase Modulation",    "Modulates the phase of an oscillator rather than its frequency for related but distinct timbres.",
	                        {"phase modulation", "phase mod synth"}},
	{"Additive",            "Builds complex tones by summing multiple sine-wave partials at controlled amplitudes.",
	                        {"additive synthesis", "additive synth", "additive oscillator"}, {"additional"}},
	{"Formant",             "Shapes or generates resonant vocal-tract-like peaks (formants) in a spectrum.",
	                        {"formant oscillator", "formant filter"}, {"format"}},
	{"Karplus-Strong",      "Physical model of a plucked or struck string using a delay line and feedback filter.",
	                        {"karplus strong", "string synthesis"}, {"string utilities", "string helper"}},
	{"Spectral",            "Processes audio in the frequency domain via FFT-based analysis and resynthesis.",
	                        {"spectral", "fft-based"}, {"special"}},
	{"West Coast",          "Buchla-influenced synthesis emphasising waveshaping and complex FM over subtractive filtering.",
	                        {"west coast", "buchla"}},
	{"Complex Oscillator",  "Multi-output oscillator with integrated FM, waveshaping, or sync for immediate complex timbres.",
	                        {"complex oscillator"}},
	{"Sub-Oscillator",      "Derives a lower-octave pitch from an existing oscillator signal.",
	                        {"sub oscillator", "sub-oscillator"}},
	{"Organ",               "Emulates tonewheel, drawbar, or pipe organ tone generation.",
	                        {"tonewheel", "drawbar organ", "hammond", "leslie", "pipe organ", "electric organ"}},
	{"Macro Oscillator",    "Single module offering a wide palette of synthesis algorithms selectable by a mode control.",
	                        {"macro oscillator", "multi-algorithm oscillator"}},
	{"Drum Synthesis",      "Synthesises percussive sounds such as kicks, snares, and hi-hats through analog or digital modelling.",
	                        {"analog drum", "drum synthesis", "kick synthesis", "snare synthesis", "drum voice", "percussion synthesis", "808 style", "909 style"}},
	{"Modal Synthesis",     "Physical model based on a bank of resonant modes struck or excited by an impulse.",
	                        {"modal synthesis", "modal resonator", "mallet synthesis"}},
	{"PLL",                 "Phase-locked loop that locks an oscillator to an input frequency for sync and clock effects.",
	                        {"phase-locked loop", "pll", "syncable oscillator"}},

	// Filter types
	{"Ladder Filter",       "Classic Moog-style four-pole transistor ladder filter known for its warm, self-oscillating character.",
	                        {"ladder filter", "transistor ladder", "moog ladder"}},
	{"Comb Filter",         "Adds a short delayed copy of a signal to itself, creating a comb-shaped frequency response.",
	                        {"comb filter"}},
	{"Multimode Filter",    "State-variable or similar design offering simultaneous LP, BP, HP, and notch outputs.",
	                        {"multimode filter", "state variable filter", "svf"}},
	{"Resonator",           "Bank of tuned resonant filters modelling the acoustic resonance of physical objects.",
	                        {"resonator bank", "string resonator", "plate resonator"}},

	// Voice architecture
	{"Paraphonic",          "Multiple oscillators share a single filter and envelope, giving limited independent voice control.",
	                        {"paraphonic", "paraphony"}},
	{"Monophonic",          "Produces a single voice at a time, typically with last-note or low-note priority.",
	                        {"monophonic", "monophony"}},
	{"Stereo",              "Processes or generates two-channel left/right audio for stereo positioning and width.",
	                        {"stereo"}},
	{"Unison",              "Stacks multiple detuned voices on a single note for a thick, wide sound.",
	                        {"unison"}},
	{"Detune",              "Offsets pitch between voices or oscillators by small intervals to add width and movement.",
	                        {"detune"}},
	{"Voice Manager",       "Handles polyphonic voice allocation, stealing, and routing to downstream synth voices.",
	                        {"voice manager", "voice allocator", "poly voice", "voice stealing", "voice allocation"}},

	// Modulation & CV utilities
	{"Attenuverter",        "Scales and optionally inverts a CV signal between -1x and +1x of its input range.",
	                        {"attenuverter"}},
	{"Comparator",          "Outputs a gate when an input signal crosses a threshold or falls within a window.",
	                        {"comparator", "window comparator"}},
	{"Shift Register",      "Clocks values through a chain of stages, creating correlated or sequenced CV patterns.",
	                        {"shift register"}},
	{"Frequency Shifter",   "Translates all spectral components up or down by a fixed Hz offset using single-sideband modulation.",
	                        {"frequency shifter", "single sideband"}},
	{"Pitch Shifter",       "Transposes audio pitch up or down while preserving duration.",
	                        {"pitch shifter", "pitch transposer"}},
	{"Transpose",           "Shifts pitch by a fixed interval in semitones or octaves without altering timbre.",
	                        {"transpose", "octave shift", "semitone shift", "pitch transpose"}},
	{"Portamento",          "Slides pitch smoothly from one note to the next over a controllable glide time.",
	                        {"portamento", "pitch glide", "pitch slide", "glide time"}},
	{"Probability",         "Generates gates or selects events based on a user-defined probability per step.",
	                        {"probabilistic", "probability gate", "probability sequencer"}},
	{"Bernoulli",           "Routes or fires a trigger based on a Bernoulli (coin-flip) random trial.",
	                        {"bernoulli"}},
	{"Random Walk",         "Produces a CV that drifts by random increments, creating slow Brownian motion.",
	                        {"random walk", "brownian"}},
	{"Markov",              "Generates sequences by probabilistic state transitions in a Markov chain.",
	                        {"markov"}},
	{"XY Controller",       "Two-dimensional pad or joystick that outputs X and Y CV for expressive control.",
	                        {"xy pad", "joystick controller", "two-dimensional controller"}},
	{"Patch Memory",        "Stores and recalls complete or partial module states as named snapshots or scenes.",
	                        {"patch memory", "snapshot recall", "preset recall", "scene recall"}},

	// Rhythm & clock
	{"Euclidean",           "Distributes a set number of pulses as evenly as possible across a step count.",
	                        {"euclidean"}},
	{"Polyrhythm",          "Runs two or more rhythmic cycles of different lengths simultaneously.",
	                        {"polyrhythm", "polymeter"}},
	{"Clock Divider",       "Outputs sub-multiples of an incoming clock frequency.",
	                        {"clock divider", "clock division"}},
	{"Clock Multiplier",    "Outputs multiples of an incoming clock frequency via interpolation or PLL.",
	                        {"clock multiplier", "clock multiplication"}},
	{"Burst",               "Emits a rapid train of gates on a trigger, with controllable count and spacing.",
	                        {"burst generator", "burst mode", "gate repeat", "trig repeat"}},
	{"Swing",               "Delays every other beat or sub-beat by a percentage to create groove or shuffle feel.",
	                        {"swing timing", "groove timing", "shuffle timing", "humanize", "groove quantization"}},
	{"Ratchet",             "Subdivides a single gate into multiple faster pulses within the same step.",
	                        {"ratchet"}, {"socket", "racket"}},
	{"Turing Machine",      "Looping shift-register sequencer that probabilistically mutates its bit pattern over time.",
	                        {"turing machine"}},
	{"Cellular Automata",   "Generates rhythmic or pitch patterns by evolving a grid of cells according to simple rules.",
	                        {"cellular automata", "game of life"}},
	{"Step Sequencer",      "Advances through a series of programmable steps on each clock pulse to produce gates or CV.",
	                        {"step sequencer", "trigger sequencer", "gate sequencer"}},

	// Effects & processing
	{"Looper",              "Records audio into a buffer and plays it back in a continuous loop with optional overdub.",
	                        {"looper", "loop recorder", "loop playback", "phrase looper"}, {"jooper"}},
	{"Bitcrusher",          "Reduces bit depth and/or sample rate to produce digital lo-fi distortion.",
	                        {"bit crush", "bitcrush", "bit depth reduction", "decimator", "lo-fi", "lofi", "word length reduction"}},
	{"Tape",                "Emulates the warm saturation, flutter, and echo characteristics of analog tape.",
	                        {"tape delay", "tape echo", "tape saturation", "tape emulation"}},
	{"Saturation",          "Applies soft-clipping or harmonic distortion for analog warmth and overdrive.",
	                        {"saturation", "soft clipping", "analog warmth", "overdrive"}},
	{"Tube",                "Emulates the even-harmonic saturation and compression of vacuum tube circuits.",
	                        {"tube saturation", "tube emulation", "valve emulation", "tube distortion"}},
	{"Ping-Pong",           "Stereo delay that alternates echoes between left and right channels.",
	                        {"ping-pong", "ping pong"}},
	{"Spring Reverb",       "Simulates the distinctive metallic decay of a physical spring reverb tank.",
	                        {"spring reverb"}},
	{"Convolution",         "Applies an impulse response to impose a real acoustic space or cabinet character onto a signal.",
	                        {"convolution reverb", "impulse response", "ir reverb", "convolution processor"}},
	{"Tremolo",             "Modulates the amplitude of a signal periodically to create a rhythmic pulsing effect.",
	                        {"tremolo"}},
	{"Vibrato",             "Modulates the pitch of a signal periodically to create a wavering intonation effect.",
	                        {"vibrato"}},
	{"Cabinet Simulation",  "Emulates the frequency response of a guitar speaker cabinet and microphone placement.",
	                        {"cabinet simulation", "cabinet emulation", "speaker cabinet", "ir loader", "cab sim", "amp simulation", "amp sim", "guitar cab"}},
	{"Digital Glitch",      "Introduces stutter, buffer corruption, or digital artefacts as intentional creative effects.",
	                        {"glitch effect", "stutter effect", "buffer stutter", "digital artifact", "buffer repeat", "glitchy"}},

	// Dynamics
	{"Noise Gate",          "Silences a signal that falls below a threshold, typically to remove background noise.",
	                        {"noise gate", "noise suppression"}},
	{"Transient Shaper",    "Boosts or cuts the attack and sustain portions of a signal independently of overall level.",
	                        {"transient shaper", "transient designer", "attack shaper"}},

	// Harmony & pitch
	{"Chord",               "Generates or harmonises multiple pitches simultaneously to form chords.",
	                        {"chord generator", "chord mode", "chord voicing", "harmonizer", "chord player", "chord memory", "polychord"}},
	{"Microtonal",          "Supports tuning systems outside 12-TET, including just intonation and custom Scala scales.",
	                        {"microtonal", "microtuning", "just intonation", "xenharmonic", "scala tuning"}},

	// Generative & character
	{"Drone",               "Holds a continuous sustained pitch or texture, often with slow evolving modulation.",
	                        {"drone"}},
	{"Chaos",               "Produces unpredictable but deterministic CV from chaotic systems such as the Lorenz attractor.",
	                        {"chaos", "lorenz", "strange attractor"}},
	{"Generative",          "Autonomously creates evolving musical material without step-by-step programming.",
	                        {"generative", "evolving soundscape", "generative texture"}, {"generator", "generates", "generated"}},

	// Software & tools (VCV-specific, no direct hardware equivalent)
	{"Scripting",           "Allows users to write custom DSP or logic code in an embedded scripting language.",
	                        {"scripting", "user script", "programmable module", "programming language", "faust", "lua script", "js module", "prototype", "script language"}},
	{"OSC",                 "Sends or receives Open Sound Control messages over a network.",
	                        {"open sound control", "osc send", "osc receive", "osc bridge"}},
	{"CV Recorder",         "Records a stream of CV values over time and plays them back as automation.",
	                        {"cv recorder", "cv record", "cv automation", "cv sequence record"}},
	{"Poly Cable",          "Merges, splits, or routes polyphonic cables to manage multi-channel signal paths.",
	                        {"poly merge", "poly split", "polyphonic routing", "poly spread"}},
	{"Oscilloscope",        "Displays a waveform in the time domain for visual signal monitoring.",
	                        {"oscilloscope", "waveform display", "waveform viewer", "scope", "signal monitor"}},
	{"Spectrum Analyzer",   "Displays signal energy across the frequency spectrum in real time.",
	                        {"spectrum analyzer", "fft display", "frequency analyzer", "spectroscope"}},
	{"Plugin Host",         "Loads and runs third-party audio plugins (VST, AU) as a module within VCV Rack.",
	                        {"plugin host", "vst host", "au host", "audio plugin host"}},
	{"Resampling",          "Converts a signal to a different sample rate for pitch, timing, or quality effects.",
	                        {"resampling", "sample rate reduction", "sample rate modulation"}},
	{"Synchronization",     "Locks module timing to an external clock or DAW transport.",
	                        {"synchronization", "syncable", "clock sync", "tempo sync"}},
	{"Text",                "Displays or inputs text labels, CSV data, or other human-readable information.",
	                        {"text", "text display", "text editor", "csv file", "csv import"}, {"next", "test"}},
};


AutoTagResult customTagAuto(const std::vector<AutoTagRule>& rules = AUTO_TAG_RULES, const std::vector<Plugin*>& plugins = rack::plugin::plugins);

AutoTagResult customTagSearch(const std::string& query, const std::vector<Plugin*>& plugins = rack::plugin::plugins);


// Internal function that performs the actual network download.
// Exposed for testing - can be overridden in test to mock network behavior.
const std::string downloadMetamoduleYaml();

// Internal function that parses the downloaded YAML file.
// Exposed for testing - can be overridden in test to mock parsing behavior.
std::set<std::pair<std::string, std::string>> parseMetamoduleYaml(const std::string& tmpFile = downloadMetamoduleYaml());

// Assigns "MetaModule" tag to all modules from MetaModule-compatible plugins.
AutoTagResult customTagMetamodule(std::set<std::pair<std::string, std::string>> metamoduleModules = parseMetamoduleYaml(),
	const std::vector<Plugin*>& plugins = rack::plugin::plugins);

} // namespace Mb
} // namespace StoermelderPackOne
