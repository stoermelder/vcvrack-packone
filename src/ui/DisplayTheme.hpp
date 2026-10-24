#pragma once
#include <rack.hpp>

namespace StoermelderPackOne {

// Colors for "screen" style displays that are drawn by hand (nanovg) instead of
// through Rack's Blendish theme. Dark displays stay as they are in the "dark" and
// "hcdark" UI themes; in the "light" UI theme the display turns light and every
// color that is drawn on top of it flips to stay readable.
//
// Foreground helpers are named by role, not by color: use ink() where the code
// used to draw white-on-dark, scrim() where it used black-on-dark, and tone()
// or gold() for accent colors.
struct DisplayTheme {
	static bool isLight() {
		return rack::settings::uiTheme == "light";
	}

	// Neutral foreground: white on a dark display, black on a light one.
	static NVGcolor ink(float alpha = 1.f) {
		return isLight() ? nvgRGBAf(0.f, 0.f, 0.f, alpha) : nvgRGBAf(1.f, 1.f, 1.f, alpha);
	}

	// Translucent backdrop for text laid over content: black on a dark display,
	// white on a light one.
	static NVGcolor scrim(float alpha) {
		return isLight() ? nvgRGBAf(1.f, 1.f, 1.f, alpha) : nvgRGBAf(0.f, 0.f, 0.f, alpha);
	}

	// Display background, top and bottom of the vertical gradient.
	static NVGcolor backgroundTop() {
		return isLight() ? nvgRGB(0xd6, 0xd6, 0xd1) : nvgRGB(0x22, 0x22, 0x22);
	}
	static NVGcolor backgroundBottom() {
		return isLight() ? nvgRGB(0xc2, 0xc2, 0xbc) : nvgRGB(0x12, 0x12, 0x12);
	}

	// Flat fills of panes that sit on the display: the raised one (top bar, source
	// list) and the recessed one (footer).
	static NVGcolor pane() {
		return isLight() ? nvgRGB(0xca, 0xca, 0xc5) : nvgRGB(0x1f, 0x1f, 0x17);
	}
	static NVGcolor paneRecessed() {
		return isLight() ? nvgRGB(0xbc, 0xbc, 0xb6) : nvgRGB(0x17, 0x17, 0x12);
	}

	// Edge darkening of the display; much weaker on a light display.
	static NVGcolor vignette(float darkAlpha) {
		return nvgRGBAf(0.f, 0.f, 0.f, isLight() ? darkAlpha * 0.25f : darkAlpha);
	}

	// Accent colors are authored for a dark display. On a light display they are
	// darkened to keep their hue and gain contrast.
	static NVGcolor tone(NVGcolor c) {
		if (!isLight()) return c;
		const float k = 0.55f;
		return nvgRGBAf(c.r * k, c.g * k, c.b * k, c.a);
	}

	// Highlight/selection yellow.
	static NVGcolor gold(float alpha = 1.f) {
		return isLight() ? nvgRGBAf(0.78f, 0.50f, 0.f, alpha) : nvgRGBAf(1.f, 0.85f, 0.1f, alpha);
	}
};

} // namespace StoermelderPackOne
