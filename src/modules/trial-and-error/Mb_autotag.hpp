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
	std::vector<std::string> keywords;
	// Words that must NOT appear anywhere in the module name or description
	// (case-insensitive substring). Use to block known false positives,
	// e.g. "format" for the "Formant" rule.
	std::vector<std::string> blockwords;
	// Base score threshold for multi-word keywords. Single-word keywords
	// automatically get +0.05 on top, because multi-word queries already
	// provide AND-logic filtering that single-word queries lack.
	float minScore;

	AutoTagRule(std::string t, std::vector<std::string> kw, std::vector<std::string> bl = {}, float ms = 0.7f)
		: tagName(std::move(t)), keywords(std::move(kw)), blockwords(std::move(bl)), minScore(ms) {}
};

// Curated keyword → custom tag mapping.
// Keywords are searched via the fuzzy DB (case-insensitive, substring + Lev matching),
// so short canonical forms are usually sufficient. Multiple keywords use OR logic.
// Only covers concepts not already represented by an official Rack tag.
static const std::vector<AutoTagRule> AUTO_TAG_RULES = {
	// Audio routing & format
	{"Matrix Mixer",        {"matrix mixer", "matrix routing"}},
	{"Crossfader",          {"crossfader"}},
	{"Sidechain",           {"sidechain", "side chain"}},
	{"Feedback",            {"feedback matrix", "feedback network", "feedback routing"}, {"visual"}},

	// Synthesis — oscillator techniques
	{"Wavetable",           {"wavetable oscillator", "wavetable synth"}},
	{"Wavefolder",          {"wavefold"}},
	{"FM Synthesis",        {"fm synthesis", "fm synth", "frequency modulation synthesis"}},
	{"Through-Zero FM",     {"through-zero", "tzfm", "thru-zero"}},
	{"Phase Modulation",    {"phase modulation", "phase mod synth"}},
	{"Additive",            {"additive synthesis", "additive synth", "additive oscillator"}, {"additional"}},
	{"Formant",             {"formant oscillator", "formant filter"}, {"format"}},
	{"Karplus-Strong",      {"karplus strong", "string synthesis"}, {"string utilities", "string helper"}},
	{"Spectral",            {"spectral", "fft-based"}, {"special"}},
	{"West Coast",          {"west coast", "buchla"}},
	{"Complex Oscillator",  {"complex oscillator"}},
	{"Sub-Oscillator",      {"sub oscillator", "sub-oscillator"}},
	{"Organ",               {"tonewheel", "drawbar organ", "hammond"}},
	{"Macro Oscillator",    {"macro oscillator", "multi-algorithm oscillator"}},
	{"Drum Synthesis",      {"analog drum", "drum synthesis", "drum voice", "kick synthesis"}},
	{"Modal Synthesis",     {"modal synthesis", "modal resonator", "mallet synthesis"}},
	{"PPL",                 {"phase-locked loop", "pll", "syncable oscillator"}},

	// Filter types
	{"Ladder Filter",       {"ladder filter", "transistor ladder", "moog ladder"}},
	{"Comb Filter",         {"comb filter"}},
	{"Multimode Filter",    {"multimode filter", "state variable filter", "svf"}},
	{"Resonator",           {"resonator bank", "string resonator", "plate resonator"}},

	// Voice architecture
	{"Paraphonic",          {"paraphonic", "paraphony", "quadraphonic"}},
	{"Monophonic",          {"monophonic", "monophony"}},
	{"Stereo",              {"stereo"}},
	{"Unison",              {"unison"}},     
	{"Detune",              {"detune"}},     

	// Modulation & CV utilities
	{"Attenuverter",        {"attenuverter"}},
	{"Comparator",          {"comparator", "window comparator"}},
	{"Shift Register",      {"shift register"}},
	{"Frequency Shifter",   {"frequency shifter", "single sideband"}},
	{"Pitch Shifter",       {"pitch shifter", "pitch transposer"}},
	{"Probability",         {"probabilistic", "probability gate", "probability sequencer"}},
	{"Bernoulli",           {"bernoulli"}},
	{"Random Walk",         {"random walk", "brownian"}},
	{"Markov",              {"markov"}},
	{"XY Controller",       {"xy pad", "joystick controller", "two-dimensional controller"}},
	{"Patch Memory",        {"patch memory", "snapshot recall", "preset recall", "scene recall"}},

	// Rhythm & clock
	{"Euclidean",           {"euclidean"}},
	{"Polyrhythm",          {"polyrhythm", "polymeter"}},
	{"Clock Divider",       {"clock divider", "clock division"}},
	{"Clock Multiplier",    {"clock multiplier", "clock multiplication"}},
	{"Burst",               {"burst generator", "burst mode"}},
	{"Swing",               {"swing timing", "groove timing", "shuffle timing"}},
	{"Ratchet",             {"ratchet"}, {"socket", "racket"}},
	{"Turing Machine",      {"turing machine"}},
	{"Cellular Automata",   {"cellular automata", "game of life"}},
	{"Step Sequencer",      {"step sequencer", "trigger sequencer", "gate sequencer"}},

	// Effects & processing
	{"Looper",              {"looper", "loop recorder", "loop playback"}, {"jooper"}},
	{"Bitcrusher",          {"bit crush", "bitcrush", "sample rate reduc", "decimator"}},
	{"Tape",                {"tape delay", "tape echo", "tape saturation", "tape emulation"}},
	{"Saturation",          {"saturation", "soft clipping", "analog warmth", "overdrive"}},
	{"Tube",                {"tube saturation", "tube emulation", "valve emulation", "tube distortion"}},
	{"Ping-Pong",           {"ping-pong", "ping pong"}},
	{"Spring Reverb",       {"spring reverb"}},
	{"Tremolo",             {"tremolo"}},
	{"Vibrato",             {"vibrato"}},
	{"Cabinet Simulation",  {"cabinet simulation", "cabinet emulation", "speaker cabinet", "ir loader", "cab sim"}},
	{"Digital Glitch",      {"glitch effect", "stutter effect", "buffer stutter", "digital artifact"}},

	// Dynamics
	{"Noise Gate",          {"noise gate", "noise suppression"}},
	{"Transient Shaper",    {"transient shaper", "transient designer", "attack shaper"}},

	// Harmony & pitch
	{"Chord",               {"chord generator", "chord mode", "chord voicing", "harmonizer"}},
	{"Microtonal",          {"microtonal", "microtuning", "just intonation", "xenharmonic", "scala tuning"}},

	// Generative & character
	{"Drone",               {"drone"}},
	{"Chaos",               {"chaos", "lorenz", "strange attractor"}},
	{"Generative",          {"generative", "evolving soundscape", "generative texture"}, {"generator", "generates", "generated"}},

	// Software & tools (VCV-specific, no direct hardware equivalent)
	{"Scripting",           {"scripting", "user script", "programmable module", "programming language", "faust", "lua script", "js module", "prototype", "script language"}},
	{"OSC",                 {"open sound control", "osc send", "osc receive", "osc bridge"}},
	{"CV Recorder",         {"cv recorder", "cv record", "cv automation", "cv sequence record"}},
	{"Poly Cable",          {"poly merge", "poly split", "polyphonic routing", "poly spread"}},
	{"Oscilloscope",        {"oscilloscope", "waveform display", "waveform viewer"}},
	{"Spectrum Analyzer",   {"spectrum analyzer", "fft display", "frequency analyzer", "spectroscope"}},
	{"Plugin Host",         {"plugin host", "vst host", "au host", "audio plugin host"}},
	{"Resampling",          {"resampling", "sample rate reduction", "sample rate modulation"}},
	{"Synchronization",     {"synchronization", "syncable", "clock sync", "tempo sync"}},
	{"Text",                {"text", "csv"}, {"next", "test"}},
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
