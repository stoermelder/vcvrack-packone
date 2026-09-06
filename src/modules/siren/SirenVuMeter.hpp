#pragma once
#include <rack.hpp>


namespace StoermelderPackOne {
namespace Siren {

struct SirenVuMeter : TransparentWidget {
	// Pointers to module-owned atomics; null-safe (draws all dim when nullptr)
	std::atomic<float>* levelL = nullptr;
	std::atomic<float>* levelR = nullptr;

	static constexpr int NUM_SEGS = 14;
	static constexpr float SEG_H = 4.f;
	static constexpr float SEG_GAP = 1.f;
	static constexpr float BAR_W = 7.f;
	static constexpr float BAR_GAP = 2.f;

	// Returns core and dim colors for a segment index
	static void segColors(int seg, NVGcolor& core, NVGcolor& dim) {
		if (seg >= NUM_SEGS - 2) {
			core = nvgRGBf(1.f, 0.10f, 0.10f);
			dim = nvgRGBAf(0.20f, 0.02f, 0.02f, 1.f);
		}
		else if (seg >= NUM_SEGS - 5) {
			core = nvgRGBf(1.f, 0.82f, 0.f);
			dim = nvgRGBAf(0.20f, 0.16f, 0.f, 1.f);
		}
		else {
			core = nvgRGBf(0.10f, 1.f, 0.25f);
			dim = nvgRGBAf(0.02f, 0.16f, 0.04f, 1.f);
		}
	}

	// Always-visible dark bezels (off state)
	void draw(const DrawArgs& args) override {
		float xL = (box.size.x - 2.f * BAR_W - BAR_GAP) * 0.5f;
		float xR = xL + BAR_W + BAR_GAP;

		for (int seg = 0; seg < NUM_SEGS; seg++) {
			float y = box.size.y - (seg + 1) * (SEG_H + SEG_GAP);
			NVGcolor core, dim;
			segColors(seg, core, dim);

			nvgBeginPath(args.vg);
			nvgRoundedRect(args.vg, xL, y, BAR_W, SEG_H, 1.f);
			nvgFillColor(args.vg, dim);
			nvgFill(args.vg);

			nvgBeginPath(args.vg);
			nvgRoundedRect(args.vg, xR, y, BAR_W, SEG_H, 1.f);
			nvgFillColor(args.vg, dim);
			nvgFill(args.vg);
		}
	}

	// Active LEDs on layer 1: bright core + halo using Rack's blend formula
	void drawLayer(const DrawArgs& args, int layer) override {
		if (layer != 1) return;

		float dbL = levelL ? levelL->load(std::memory_order_relaxed) : -100.f;
		float dbR = levelR ? levelR->load(std::memory_order_relaxed) : -100.f;

		float xL = (box.size.x - 2.f * BAR_W - BAR_GAP) * 0.5f;
		float xR = xL + BAR_W + BAR_GAP;

		// Rack's LED blend: lightColor * (1 - dst) + dst  — prevents over-saturation
		nvgGlobalCompositeBlendFunc(args.vg, NVG_ONE_MINUS_DST_COLOR, NVG_ONE);

		for (int seg = 0; seg < NUM_SEGS; seg++) {
			float segDb = -60.f + seg * (60.f / (NUM_SEGS - 1));
			float y = box.size.y - (seg + 1) * (SEG_H + SEG_GAP);

			NVGcolor core, dim;
			segColors(seg, core, dim);

			auto drawLed = [&](float x, bool active) {
				if (!active) return;

				// Bright LED face
				nvgBeginPath(args.vg);
				nvgRoundedRect(args.vg, x, y, BAR_W, SEG_H, 1.f);
				nvgFillColor(args.vg, core);
				nvgFill(args.vg);

				// Halo — skip in framebuffer (screenshots / module browser)
				if (!args.fb) {
					const float haloB = rack::settings::haloBrightness;
					if (haloB > 0.f) {
						float cx = x + BAR_W * 0.5f;
						float cy = y + SEG_H * 0.5f;
						float r = std::min(BAR_W, SEG_H) * 0.5f;
						float or_ = r + std::min(r * 4.f, 15.f);

						NVGcolor icol = rack::color::mult(core, haloB);
						NVGcolor ocol = nvgRGBA(0, 0, 0, 0);
						NVGpaint paint = nvgRadialGradient(args.vg, cx, cy, r, or_, icol, ocol);

						nvgBeginPath(args.vg);
						nvgRect(args.vg, cx - or_, cy - or_, 2.f * or_, 2.f * or_);
						nvgFillPaint(args.vg, paint);
						nvgFill(args.vg);
					}
				}
			};

			drawLed(xL, dbL >= segDb);
			drawLed(xR, dbR >= segDb);
		}
	}
};

// Out-of-class definitions required by C++11 ODR for static constexpr float members
constexpr int SirenVuMeter::NUM_SEGS;
constexpr float SirenVuMeter::SEG_H;
constexpr float SirenVuMeter::SEG_GAP;
constexpr float SirenVuMeter::BAR_W;
constexpr float SirenVuMeter::BAR_GAP;


} // namespace Siren
} // namespace StoermelderPackOne
