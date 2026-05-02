#pragma once
#include <string>
#include <vector>

namespace StoermelderPackOne {
namespace Mb {

struct AutoTagRule {
	std::string tagName;
	std::vector<std::string> keywords;
};

// Curated keyword → custom tag mapping.
// Keywords are searched via the fuzzy DB (case-insensitive, substring + Lev matching),
// so short canonical forms are usually sufficient. Multiple keywords use OR logic.
// Only covers concepts not already represented by an official Rack tag.
static const std::vector<AutoTagRule> AUTO_TAG_RULES = {
	// Audio format & routing
	{"Stereo",              {"stereo"}},
	{"Matrix Mixer",        {"matrix mixer", "matrix routing"}},
	{"Crossfader",          {"crossfader"}},
	{"Voice Allocator",     {"voice allocator", "voice steal", "voice assign"}},
	{"Sidechain",           {"sidechain", "side chain"}},

	// Synthesis — core techniques
	{"Wavetable",           {"wavetable"}},
	{"Wavefolder",          {"wavefold"}},
	{"FM Synthesis",        {"fm synthesis", "fm synth", "frequency modulation synthesis"}},
	{"Through-Zero FM",     {"through-zero", "tzfm", "thru-zero"}},
	{"Phase Modulation",    {"phase modulation", "phase mod synth"}},
	{"Additive",            {"additive synthesis"}},
	{"Formant",             {"formant"}},
	{"Karplus-Strong",      {"karplus"}},
	{"Spectral",            {"spectral", "fft-based", "fft based"}},
	{"Convolution",         {"convolution"}},
	{"West Coast",          {"west coast"}},
	{"Complex Oscillator",  {"complex oscillator"}},
	{"Sub-Oscillator",      {"sub oscillator", "sub-oscillator"}},
	{"Organ",               {"tonewheel", "drawbar organ", "hammond"}},

	// Filter types
	{"Ladder Filter",       {"ladder filter", "transistor ladder"}},
	{"Comb Filter",         {"comb filter"}},

	// Modulation & CV utilities
	{"Attenuverter",        {"attenuverter"}},
	{"Comparator",          {"comparator"}},
	{"Shift Register",      {"shift register"}},
	{"Frequency Shifter",   {"frequency shifter"}},
	{"Probability",         {"probabilistic", "probability gate", "probability sequencer"}},
	{"Bernoulli",           {"bernoulli"}},
	{"Random Walk",         {"random walk", "brownian"}},
	{"Markov",              {"markov"}},

	// Rhythm & clock
	{"Euclidean",           {"euclidean"}},
	{"Polyrhythm",          {"polyrhythm", "polymeter"}},
	{"Clock Divider",       {"clock divider", "clock division"}},
	{"Clock Multiplier",    {"clock multiplier", "clock multiplication"}},
	{"Burst",               {"burst generator", "burst mode"}},
	{"Swing",               {"swing", "groove"}},
	{"Ratchet",             {"ratchet"}},
	{"Turing Machine",      {"turing machine"}},
	{"Cellular Automata",   {"cellular automata", "game of life"}},

	// Effects & processing
	{"Looper",              {"looper", "loop recorder", "loop playback"}},
	{"Bitcrusher",          {"bit crush", "bitcrush"}},
	{"Tape",                {"tape delay", "tape echo", "tape saturation", "tape emulation"}},
	{"Saturation",          {"saturation", "soft clipping", "analog warmth"}},
	{"Convolution Reverb",  {"convolution reverb", "impulse response reverb"}},
	{"Spectral Freeze",     {"spectral freeze", "freeze effect"}},
	{"Ping-Pong",           {"ping-pong", "ping pong"}},
	{"Tremolo",             {"tremolo"}},
	{"Vibrato",             {"vibrato"}},
	{"Cabinet Simulation",  {"cabinet simulation", "cabinet emulation", "speaker cabinet"}},
	{"Paraphonic",          {"paraphonic"}},

	// Harmony & pitch
	{"Chord",               {"chord generator", "chord mode", "chord voicing", "harmonizer"}},
	{"Microtonal",          {"microtonal", "microtuning", "just intonation", "xenharmonic", "scala tuning"}},

	// Generative & character
	{"Drone",               {"drone"}},
	{"Chaos",               {"chaos", "lorenz", "strange attractor"}},
	{"Generative",          {"generative"}},
	{"Morphing",            {"morphing", "morph between"}},
	{"Physical Model",      {"resonator", "waveguide"}},
};

} // namespace Mb
} // namespace StoermelderPackOne
