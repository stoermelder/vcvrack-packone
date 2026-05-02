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
	db.setWeights({1.0f, 0.9f});
	db.setThreshold(0.6f);
	for (plugin::Plugin* p : rack::plugin::plugins) {
		for (plugin::Model* model : p->models) {
			db.addEntry(model, {model->name, model->description});
		}
	}

	AutoTagResult result;
	for (const AutoTagRule& rule : AUTO_TAG_RULES) {
		std::set<plugin::Model*> matches;
		for (const std::string& kw : rule.keywords) {
			for (const auto& r : db.search(kw)) {
				matches.insert(r.key);
			}
		}
		for (plugin::Model* model : matches) {
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
	db.setWeights({1.0f, 0.9f});
	db.setThreshold(0.6f);
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
