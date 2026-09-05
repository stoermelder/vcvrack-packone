#pragma once
#include <rack.hpp>
#include <cmath>
#include "SirenAudio.hpp"


namespace StoermelderPackOne {
namespace Siren {
namespace dummyview {

// Static placeholder content shown by SirenBrowserPane / SirenPreviewPane when
// the widget is constructed without a module (the module browser thumbnail).
// Purely cosmetic — no interaction, no real data behind it.

struct DummyTreeEntry {
	int indent;
	std::string name;
	bool isDir;
	bool expanded;
	float durationSeconds;
};

inline const std::vector<DummyTreeEntry>& dummyTreeEntries() {
	static const std::vector<DummyTreeEntry> entries = {
		{0, "Kicks", true, true, 0.f},
		{1, "Kick_808.wav", false, false, 1.20f},
		{1, "Kick_Tight.wav", false, false, 0.85f},
		{0, "Snares", true, false, 0.f},
		{0, "Hats", true, false, 0.f},
		{0, "Loop_Anthem.wav", false, false, 4.00f},
	};
	return entries;
}

// Draws a static mock file tree, mimicking SirenTreeRow::draw() without
// depending on any live row widgets.
inline void drawDummyTree(NVGcontext* vg, float w) {
	static constexpr float ROW_H = 12.f;
	static constexpr float INDENT = 8.f;

	std::shared_ptr<Font> monoFont = APP->window->loadFont(asset::system("res/fonts/ShareTechMono-Regular.ttf"));
	NVGcolor textColor = bndGetTheme()->toolTheme.textColor;

	float y = 0.f;
	for (const DummyTreeEntry& e : dummyTreeEntries()) {
		float textX = 6.f + e.indent * INDENT;
		nvgFontFaceId(vg, APP->window->uiFont->handle);
		if (e.isDir) {
			nvgFontSize(vg, 6.f);
			nvgFillColor(vg, nvgRGBAf(textColor.r, textColor.g, textColor.b, 0.55f));
			nvgText(vg, textX, y + 8.f, e.expanded ? "\xe2\x96\xbc" : "\xe2\x96\xb6", nullptr);
			textX += 8.f;
		}
		nvgFontSize(vg, 7.7f);
		nvgFillColor(vg, e.isDir ? textColor : nvgRGBAf(textColor.r, textColor.g, textColor.b, 0.7f));
		nvgText(vg, textX, y + 8.f, e.name.c_str(), nullptr);

		if (!e.isDir && e.durationSeconds > 0.f) {
			int mins = (int)(e.durationSeconds / 60.f);
			float secs = e.durationSeconds - mins * 60.f;
			std::string dur = rack::string::f("%02d:%05.2f", mins, secs);
			nvgFontFaceId(vg, monoFont->handle);
			nvgFontSize(vg, 6.f);
			nvgFillColor(vg, nvgRGBAf(1.f, 1.f, 1.f, 0.42f));
			nvgText(vg, w - 10.f - 26.f, y + 8.f, dur.c_str(), nullptr);
		}
		y += ROW_H;
	}
}

// Static metadata for the preview pane's mock file.
struct DummyPreviewInfo {
	std::string displayName = "Sample.wav";
	float durationSeconds = 8.5f;
	int sampleRate = 44100;
	int bitDepth = 16;
	int channels = 2;
	float bpm = 120.f;
};

inline const DummyPreviewInfo& dummyPreviewInfo() {
	static const DummyPreviewInfo info;
	return info;
}

// Builds a synthetic waveform: a handful of decaying transients, loosely
// resembling a drum loop, for the preview pane's waveform canvas.
inline AudioWaveformCache buildDummyWaveformCache() {
	AudioWaveformCache cache;
	const int pw = 300;
	cache.sampleCount = pw;
	cache.samples.assign(2, std::vector<float>(pw));
	for (int i = 0; i < pw; i++) {
		float t = (float)i / pw;
		float env = 0.f;
		for (int k = 0; k < 8; k++) {
			float beatPos = (float)k / 8.f;
			float d = t - beatPos;
			if (d >= 0.f) env = std::max(env, std::exp(-d * 40.f));
		}
		cache.samples[0][i] = std::sin(t * 220.f) * env;
		cache.samples[1][i] = std::sin(t * 220.f + 0.4f) * env * 0.9f;
	}
	return cache;
}

} // namespace dummyview
} // namespace Siren
} // namespace StoermelderPackOne
