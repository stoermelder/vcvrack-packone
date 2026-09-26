#pragma once
#include <rack.hpp>
#include <map>


namespace StoermelderPackOne {

using namespace rack;

static const int XYSEQ_LENGTH = 128;


struct XySeqItem {
	float x[XYSEQ_LENGTH];
	float y[XYSEQ_LENGTH];
	int length = 0;
};


/** Describes a single generator-specific parameter for the dynamic preset UI. */
struct XySeqGeneratorParam {
	std::string label;
	std::string unit;
	float minValue;
	float maxValue;
	float defaultValue;
};

/**
 * Base class for all XY-sequence shape generators. Each generator owns its
 * own set of extra parameters (beyond the always-present Scale X/Y) so the
 * preset menu can build sliders for exactly what the selected shape needs.
 */
struct XySeqGenerator {
	virtual ~XySeqGenerator() {}
	virtual std::string getName() = 0;
	virtual std::vector<XySeqGeneratorParam> getParams() { return {}; }
	/** `params` has exactly getParams().size() entries, already clamped to their [min,max]. */
	virtual void generate(XySeqItem& item, float x, float y, const std::vector<float>& params) = 0;
	/** Formats a parameter's current value for display, e.g. Lissajous' "2:3" ratio. */
	virtual std::string getParamValueString(int idx, float v) {
		return string::f("%i", int(v));
	}

protected:
	static float scaleX(float v, float x) { return (v - 0.5f) * x + 0.5f; }
	static float scaleY(float v, float y) { return (v - 0.5f) * y + 0.5f; }
	/**
	 * Stretches the raw points of `item` so their bounding box spans [0,1] on
	 * each axis, then applies the Scale X/Y. An axis with zero extent (e.g. a
	 * straight line) is centered instead.
	 */
	static void fit(XySeqItem& item, float x, float y) {
		float minX = INFINITY, maxX = -INFINITY, minY = INFINITY, maxY = -INFINITY;
		for (int i = 0; i < item.length; i++) {
			minX = std::min(minX, item.x[i]);
			maxX = std::max(maxX, item.x[i]);
			minY = std::min(minY, item.y[i]);
			maxY = std::max(maxY, item.y[i]);
		}
		auto norm = [](float v, float lo, float hi) { return hi - lo > 1e-6f ? (v - lo) / (hi - lo) : 0.5f; };
		for (int i = 0; i < item.length; i++) {
			item.x[i] = scaleX(norm(item.x[i], minX, maxX), x);
			item.y[i] = scaleY(norm(item.y[i], minY, maxY), y);
		}
	}
};

struct XySeqGeneratorCircle : XySeqGenerator {
	std::string getName() override { return "Circle"; }
	void generate(XySeqItem& item, float x, float y, const std::vector<float>& params) override {
		item.length = 0;
		int l = XYSEQ_LENGTH / 4;
		float p = 2.f * M_PI / (l - 1);
		for (int i = 0; i < l; i++) {
			item.x[i] = scaleX(sin(i * p) / 2.f + 0.5f, x);
			item.y[i] = scaleY(cos(i * p) / 2.f + 0.5f, y);
		}
		item.length = l;
	}
};

struct XySeqGeneratorEllipse : XySeqGenerator {
	std::string getName() override { return "Ellipse"; }
	std::vector<XySeqGeneratorParam> getParams() override {
		return {{"Angle", "°", 0.f, 180.f, 0.f}};
	}
	void generate(XySeqItem& item, float x, float y, const std::vector<float>& params) override {
		item.length = 0;
		int l = XYSEQ_LENGTH / 4;
		float p = 2.f * M_PI / (l - 1);
		float angle = params[0] * M_PI / 180.f;
		for (int i = 0; i < l; i++) {
			Vec v = Vec(std::sin(i * p) * 0.5f, std::cos(i * p) * 0.25f);
			v = v.rotate(angle);
			item.x[i] = scaleX(v.x + 0.5f, x);
			item.y[i] = scaleY(v.y + 0.5f, y);
		}
		item.length = l;
	}
};

struct XySeqGeneratorPolygon : XySeqGenerator {
	std::string getName() override { return "Polygon"; }
	std::vector<XySeqGeneratorParam> getParams() override {
		return {{"Sides", "", 2.f, 12.f, 4.f}};
	}
	void generate(XySeqItem& item, float x, float y, const std::vector<float>& params) override {
		item.length = 0;
		int n = int(params[0]);
		// Regular polygon with a flat bottom edge (y grows downwards), so 4 sides
		// give an axis-aligned square and 2 sides collapse into a horizontal line.
		float o = M_PI / 2.f - M_PI / n;
		float p = 2.f * M_PI / n;
		for (int i = 0; i <= n; i++) {
			item.x[i] = std::cos(i * p + o);
			item.y[i] = std::sin(i * p + o);
		}
		item.length = n + 1;
		fit(item, x, y);
	}
};

struct XySeqGeneratorStar : XySeqGenerator {
	std::string getName() override { return "Star"; }
	std::vector<XySeqGeneratorParam> getParams() override {
		return {{"Points", "", 3.f, 12.f, 5.f}};
	}
	void generate(XySeqItem& item, float x, float y, const std::vector<float>& params) override {
		item.length = 0;
		int points = int(params[0]);
		int l = 2 * points + 1;
		float p = M_PI / points;
		float o = -M_PI / 2.f;
		for (int i = 0; i < l; i++) {
			float r = (i % 2 == 0) ? 0.5f : 0.2f;
			item.x[i] = r * std::cos(i * p + o);
			item.y[i] = r * std::sin(i * p + o);
		}
		item.length = l;
		fit(item, x, y);
	}
};

struct XySeqGeneratorSpiral : XySeqGenerator {
	std::string getName() override { return "Spiral"; }
	std::vector<XySeqGeneratorParam> getParams() override {
		return {{"Windings", "", 2.f, 12.f, 6.f}};
	}
	void generate(XySeqItem& item, float x, float y, const std::vector<float>& params) override {
		auto _s = [](float v, float s) { return (v - 0.5f) * s + 0.5f; };
		item.length = 0;
		int l = XYSEQ_LENGTH;
		float p = params[0] * 2.f * M_PI / (l - 1);
		for (int i = 0; i < l; i++) {
			item.x[i] = _s(sin(i * p) / 2.f + 0.5f, 1.f / l * i);
			item.y[i] = _s(cos(i * p) / 2.f + 0.5f, 1.f / l * i);
		}
		item.length = l;
		fit(item, x, y);
	}
};

struct XySeqGeneratorSaw : XySeqGenerator {
	std::string getName() override { return "Saw"; }
	std::vector<XySeqGeneratorParam> getParams() override {
		return {{"Segments", "", 2.f, 12.f, 6.f}};
	}
	void generate(XySeqItem& item, float x, float y, const std::vector<float>& params) override {
		item.length = 0;
		item.x[0] = scaleX(0.f, x);
		item.y[0] = scaleY(1.f, y);
		int c = int(params[0]);
		for (int i = 0; i < c; i++) {
			item.x[i + 1] = scaleX(1.f / (c + 1) * (i + 1), x);
			item.y[i + 1] = scaleY(i % 2, y);
		}
		item.x[c + 1] = scaleX(1.f, x);
		item.y[c + 1] = scaleY(0.f, y);
		item.length = c + 2;
	}
};

struct XySeqGeneratorZigzag : XySeqGenerator {
	std::string getName() override { return "Zigzag"; }
	std::vector<XySeqGeneratorParam> getParams() override {
		return {{"Peaks", "", 2.f, 12.f, 6.f}};
	}
	void generate(XySeqItem& item, float x, float y, const std::vector<float>& params) override {
		item.length = 0;
		int c = int(params[0]);
		// Two anti-phase triangle waves joined at the left and right midline
		// into one closed path, which draws a row of c diamonds.
		int i = 0;
		item.x[i] = scaleX(0.f, x);
		item.y[i++] = scaleY(0.5f, y);
		for (int k = 0; k < c; k++) {
			item.x[i] = scaleX((k + 0.5f) / c, x);
			item.y[i++] = scaleY(k % 2 == 0 ? 0.f : 1.f, y);
		}
		item.x[i] = scaleX(1.f, x);
		item.y[i++] = scaleY(0.5f, y);
		for (int k = c - 1; k >= 0; k--) {
			item.x[i] = scaleX((k + 0.5f) / c, x);
			item.y[i++] = scaleY(k % 2 == 0 ? 1.f : 0.f, y);
		}
		item.x[i] = scaleX(0.f, x);
		item.y[i++] = scaleY(0.5f, y);
		item.length = i;
	}
};

struct XySeqGeneratorSine : XySeqGenerator {
	std::string getName() override { return "Sine"; }
	std::vector<XySeqGeneratorParam> getParams() override {
		return {{"Cycles", "", 2.f, 12.f, 6.f}};
	}
	void generate(XySeqItem& item, float x, float y, const std::vector<float>& params) override {
		item.length = 0;
		int l = XYSEQ_LENGTH;
		float p = params[0] * 2.f * M_PI / (l - 1);
		for (int i = 0; i < l; i++) {
			item.x[i] = 1.f / l * i;
			item.y[i] = sin(i * p) / 2.f + 0.5f;
		}
		item.length = l;
		fit(item, x, y);
	}
};

struct XySeqGeneratorEight : XySeqGenerator {
	std::string getName() override { return "Eight"; }
	void generate(XySeqItem& item, float x, float y, const std::vector<float>& params) override {
		auto _s = [](float v, float s) { return v / s + 0.5f; };
		item.length = 0;
		int l = XYSEQ_LENGTH / 2.f;
		float p = 2.f * M_PI / (l - 1);
		float o = -M_PI / 2.f;
		for (int i = 0; i < l; i++) {
			item.x[i] = scaleX(_s(std::cos(i * p + o), 2.f), x);
			item.y[i] = scaleY(_s(std::cos(i * p + o) * std::sin(i * p + o), 1.f), y);
		}
		item.length = l;
	}
};

struct XySeqGeneratorRose : XySeqGenerator {
	std::string getName() override { return "Rose"; }
	std::vector<XySeqGeneratorParam> getParams() override {
		return {{"Petals", "", 2.f, 12.f, 6.f}};
	}
	void generate(XySeqItem& item, float x, float y, const std::vector<float>& params) override {
		auto _s = [](float v) { return v / 2.f + 0.5f; };
		item.length = 0;
		int l = XYSEQ_LENGTH;
		int parameter = int(params[0]);
		float p = (parameter % 2 == 1 ? 2.f : 1.f) * 2.f * M_PI / (l - 1);
		for (int i = 0; i < l; i++) {
			item.x[i] = _s(std::cos(parameter / 2.f * i * p) * std::cos(i * p));
			item.y[i] = _s(std::cos(parameter / 2.f * i * p) * std::sin(i * p));
		}
		item.length = l;
		fit(item, x, y);
	}
};

struct XySeqGeneratorSpiro : XySeqGenerator {
	std::string getName() override { return "Spiro"; }
	std::vector<XySeqGeneratorParam> getParams() override {
		return {{"Loops", "", 3.f, 12.f, 4.f}};
	}
	void generate(XySeqItem& item, float x, float y, const std::vector<float>& params) override {
		item.length = 0;
		int l = XYSEQ_LENGTH;
		int k = int(params[0]);
		// Epitrochoid: a circle of radius r = R/k rolling around the outside
		// of a fixed circle of radius R. With an integer k the curve closes
		// after a single turn (t: 0..2pi) and bulges into k outer lobes. A pen
		// offset larger than r turns each inward cusp between two lobes into
		// a small loop.
		float R = 1.f;
		float r = R / k;
		float d = r * 1.4f;
		float ratio = (R + r) / r;
		float p = 2.f * M_PI / (l - 1);
		for (int i = 0; i < l; i++) {
			float t = i * p;
			item.x[i] = (R + r) * std::cos(t) - d * std::cos(ratio * t);
			item.y[i] = (R + r) * std::sin(t) - d * std::sin(ratio * t);
		}
		item.length = l;
		fit(item, x, y);
	}
};

struct XySeqGeneratorLissajous : XySeqGenerator {
	std::string getName() override { return "Lissajous"; }
	std::vector<XySeqGeneratorParam> getParams() override {
		return {{"Ratio", "", 2.f, 12.f, 2.f}};
	}
	void generate(XySeqItem& item, float x, float y, const std::vector<float>& params) override {
		item.length = 0;
		int l = XYSEQ_LENGTH;
		float a = params[0];
		float b = a + 1.f;
		float p = 2.f * M_PI / (l - 1);
		for (int i = 0; i < l; i++) {
			item.x[i] = scaleX(std::sin(a * i * p) / 2.f + 0.5f, x);
			item.y[i] = scaleY(std::sin(b * i * p + M_PI / 2.f) / 2.f + 0.5f, y);
		}
		item.length = l;
	}
	std::string getParamValueString(int idx, float v) override {
		int a = int(v);
		return string::f("%i:%i", a, a + 1);
	}
};


/** Registry of all available shape generators, in menu order. Owns the instances. */
struct XySeqGenerators {
	std::vector<XySeqGenerator*> list;

	XySeqGenerators() {
		list.push_back(new XySeqGeneratorCircle);
		list.push_back(new XySeqGeneratorEllipse);
		list.push_back(new XySeqGeneratorPolygon);
		list.push_back(new XySeqGeneratorStar);
		list.push_back(new XySeqGeneratorSpiral);
		list.push_back(new XySeqGeneratorSaw);
		list.push_back(new XySeqGeneratorZigzag);
		list.push_back(new XySeqGeneratorSine);
		list.push_back(new XySeqGeneratorEight);
		list.push_back(new XySeqGeneratorRose);
		list.push_back(new XySeqGeneratorLissajous);
		list.push_back(new XySeqGeneratorSpiro);
	}
	~XySeqGenerators() {
		for (auto* g : list) delete g;
	}

	static XySeqGenerators& get() {
		static XySeqGenerators instance;
		return instance;
	}
};


/**
 * Builds the "Preset" submenu item: pick a shape generator, tweak Scale X/Y
 * plus whatever extra parameters that generator declares, then Apply.
 * The submenu stays open across clicks and rebuilds its parameter sliders
 * whenever the selected generator changes.
 *
 * `onApply` receives the already-generated points so the caller only has to
 * copy them into its own sequence storage and record undo history,
 * keeping this helper independent of any particular module type.
 */
inline ui::MenuItem* xySeqPresetMenuItem(std::function<void(const XySeqItem&)> onApply) {
	struct XySeqPresetMenuItem : ui::MenuItem {
		std::function<void(const XySeqItem&)> onApply;

		float x = 1.0f;
		float y = 1.0f;
		XySeqGenerator* generator = XySeqGenerators::get().list[0];
		/** Current value per generator, keyed by generator so switching shapes keeps each shape's own settings. */
		std::map<XySeqGenerator*, std::vector<float>> paramValues;

		XySeqPresetMenuItem() {
			text = "Preset";
			rightText = RIGHT_ARROW;
		}

		std::vector<float>& paramsFor(XySeqGenerator* g) {
			auto it = paramValues.find(g);
			if (it != paramValues.end()) return it->second;
			std::vector<float> v;
			for (auto& p : g->getParams()) v.push_back(p.defaultValue);
			return paramValues.emplace(g, std::move(v)).first->second;
		}

		static ui::Slider* createParamSlider(XySeqPresetMenuItem* item, XySeqGenerator* generator, int idx, XySeqGeneratorParam desc) {
			struct ParamSlider : ui::Slider {
				struct ParamQuantity : Quantity {
					std::function<void(float)> setValueFn;
					std::function<float()> getValueFn;
					XySeqGenerator* generator;
					int idx;
					XySeqGeneratorParam desc;

					void setValue(float value) override { setValueFn(clamp(value, desc.minValue, desc.maxValue)); }
					float getValue() override { return getValueFn(); }
					float getMinValue() override { return desc.minValue; }
					float getMaxValue() override { return desc.maxValue; }
					float getDefaultValue() override { return desc.defaultValue; }
					std::string getDisplayValueString() override { return generator->getParamValueString(idx, getValue()); }
					std::string getLabel() override { return desc.label; }
					std::string getUnit() override { return desc.unit; }
				};

				ParamSlider(XySeqPresetMenuItem* item, XySeqGenerator* generator, int idx, XySeqGeneratorParam desc) {
					ParamQuantity* q = new ParamQuantity;
					q->setValueFn = [item, generator, idx](float v) { item->paramsFor(generator)[idx] = v; };
					q->getValueFn = [item, generator, idx] { return item->paramsFor(generator)[idx]; };
					q->generator = generator;
					q->idx = idx;
					q->desc = desc;
					quantity = q;
					box.size.x = 120.0f;
				}
				~ParamSlider() {
					delete quantity;
				}
			};
			return new ParamSlider(item, generator, idx, desc);
		}

		struct GeneratorItem : ui::MenuItem {
			XySeqPresetMenuItem* item;
			XySeqGenerator* generator;

			void step() override {
				rightText = (item->generator == generator) ? CHECKMARK_STRING : "";
				MenuItem::step();
			}
			void onAction(const event::Action& e) override {
				item->generator = generator;
				e.unconsume();
			}
		};

		struct PresetMenu : ui::Menu {
			XySeqPresetMenuItem* item;
			XySeqGenerator* builtFor = nullptr;

			void step() override {
				if (item->generator != builtFor) populate();
				ui::Menu::step();
			}

			void populate() {
				clearChildren();
				builtFor = item->generator;

				for (XySeqGenerator* g : XySeqGenerators::get().list) {
					GeneratorItem* gi = new GeneratorItem;
					gi->item = item;
					gi->generator = g;
					gi->text = g->getName();
					addChild(gi);
				}

				addChild(new MenuSeparator);

				addChild(StoermelderPackOne::Rack::createPtrSlider(&item->x, 0.f, 1.f, 0.5f, "Scale x", "%", 100.f, 120.0f));
				addChild(StoermelderPackOne::Rack::createPtrSlider(&item->y, 0.f, 1.f, 0.5f, "Scale y", "%", 100.f, 120.0f));

				std::vector<XySeqGeneratorParam> params = builtFor->getParams();
				item->paramsFor(builtFor);
				for (size_t i = 0; i < params.size(); i++) {
					addChild(createParamSlider(item, builtFor, int(i), params[i]));
				}

				addChild(new MenuSeparator);

				addChild(createMenuItem("Apply", "", [=] {
					XySeqItem seqItem;
					item->generator->generate(seqItem, item->x, item->y, item->paramsFor(item->generator));
					item->onApply(seqItem);
				}));
			}
		};

		Menu* createChildMenu() override {
			PresetMenu* menu = new PresetMenu;
			menu->item = this;
			return menu;
		}
	};

	XySeqPresetMenuItem* item = new XySeqPresetMenuItem;
	item->onApply = onApply;
	return item;
}

} // namespace StoermelderPackOne
