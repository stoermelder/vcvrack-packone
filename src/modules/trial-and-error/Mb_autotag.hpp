#pragma once
#include <string>
#include <vector>
#include <network.hpp>
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
	{"Unison",              {"detune"}},     

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


// Downloads and parses https://metamodule.info/dl/plugins.yml.
// Returns a set of (pluginSlug, moduleSlug) pairs for every MetaModule-compatible module.
// The YAML structure uses indentation to distinguish plugin-level VCVSlug (8 spaces)
// from module-level VCVSlug (16 spaces).
std::set<std::pair<std::string, std::string>> getMetamoduleModules() {
	std::set<std::pair<std::string, std::string>> result;

	std::string tmpFile = rack::system::getTempDirectory() + "/metamodule-plugins.yml";
	if (!rack::network::requestDownload("https://metamodule.info/dl/plugins.yml", tmpFile))
		return result;

	FILE* file = fopen(tmpFile.c_str(), "r");
	if (!file) return result;

	const std::string prefix = "VCVSlug: ";
	std::string currentPlugin;
	char buf[512];

	while (fgets(buf, sizeof(buf), file)) {
		std::string line(buf);
		// Count leading spaces to determine nesting level
		size_t indent = 0;
		while (indent < line.size() && line[indent] == ' ') indent++;
		std::string trimmed = line.substr(indent);
		// Strip trailing whitespace/newline
		while (!trimmed.empty() && (trimmed.back() == '\n' || trimmed.back() == '\r' || trimmed.back() == ' '))
			trimmed.pop_back();

		if (trimmed.find(prefix) != 0) continue;
		std::string slug = trimmed.substr(prefix.size());

		if (indent == 8) {
			currentPlugin = slug;
		} 
		else if (indent == 16 && !currentPlugin.empty()) {
			result.insert({currentPlugin, slug});
		}
	}

	fclose(file);
	std::remove(tmpFile.c_str());
	return result;
}


AutoTagResult customTagAuto() {
	// Build a dedicated DB using name + description (always include description
	// regardless of the global searchDescriptions setting).
	fuzzysearch::Database<plugin::Model*> db;
	db.setWeights({0.9f, 1.f});
	db.setThreshold(0.7f);
	for (plugin::Plugin* p : rack::plugin::plugins) {
		for (plugin::Model* model : p->models) {
			db.addEntry(model, {model->name, model->description});
		}
	}

	AutoTagResult result;
	for (const AutoTagRule& rule : AUTO_TAG_RULES) {
		std::set<plugin::Model*> matches;
		for (const std::string& kw : rule.keywords) {
			bool multiWord = kw.find(' ') != std::string::npos;
			float threshold = multiWord ? rule.minScore : rule.minScore + 0.05f;
			for (const auto& r : db.search(kw)) {
				if (r.score >= threshold)
					matches.insert(r.key);
			}
		}
		for (plugin::Model* model : matches) {
			if (!rule.blockwords.empty()) {
				std::string text = model->name + " " + model->description;
				std::transform(text.begin(), text.end(), text.begin(), ::tolower);
				bool blocked = false;
				for (const std::string& bw : rule.blockwords) {
					std::string lbw = bw;
					std::transform(lbw.begin(), lbw.end(), lbw.begin(), ::tolower);
					if (text.find(lbw) != std::string::npos) { blocked = true; break; }
				}
				if (blocked) continue;
			}
			if (!customTagHas(model, rule.tagName, true)) {
				result.assignments[rule.tagName].insert(model);
				result.total++;
				result.perTag[rule.tagName]++;
			}
		}
	}
	return result;
}


AutoTagResult customTagSearch(const std::string& query) {
	fuzzysearch::Database<plugin::Model*> db;
	db.setWeights({0.9f, 1.f});
	db.setThreshold(0.7f);
	for (plugin::Plugin* p : rack::plugin::plugins) {
		for (plugin::Model* model : p->models) {
			db.addEntry(model, {model->name, model->description});
		}
	}

	AutoTagResult result;
	for (const auto& r : db.search(query)) {
		plugin::Model* model = r.key;
		if (!customTagHas(model, query)) {
			result.assignments[query].insert(model);
			result.total++;
			result.perTag[query]++;
		}
	}
	return result;
}


AutoTagResult customTagMetamodule() {
	std::set<std::pair<std::string, std::string>> metamoduleModules = getMetamoduleModules();

	AutoTagResult result;
	for (plugin::Plugin* p : rack::plugin::plugins) {
		for (plugin::Model* model : p->models) {
			if (metamoduleModules.count({p->slug, model->slug}) && !customTagHas(model, "MetaModule", true)) {
				result.assignments["MetaModule"].insert(model);
				result.total++;
				result.perTag["MetaModule"]++;
			}
		}
	}
	return result;
}

} // namespace Mb
} // namespace StoermelderPackOne
