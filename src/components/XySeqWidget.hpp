#pragma once
#include <rack.hpp>
#include "LedTextDisplay.hpp"
#include <random>


namespace StoermelderPackOne {

using namespace rack;

enum class XYSEQ_MODE {
	TRIG_FWD = 0,
	TRIG_REV = 1,
	TRIG_RANDOM_16 = 2,
	TRIG_RANDOM_8 = 3,
	TRIG_RANDOM_4 = 4,
	VOLT = 10,
	C4 = 11
};

enum class XYSEQ_INTERPOLATE {
	LINEAR = 0,
	CUBIC = 1
};

enum class XYSEQ_PRESET {
	CIRCLE,
	SPIRAL,
	SAW,
	SINE,
	EIGHT,
	ROSE
};

static const int XYSEQ_LENGTH = 128;
static const int XYSEQ_COUNT = 16;


struct XySeqItem {
	float x[XYSEQ_LENGTH];
	float y[XYSEQ_LENGTH];
	int length = 0;
};


template <int PORTS>
struct XySeqModule {
	/** [Stored to JSON] */
	XySeqItem seqData[PORTS][XYSEQ_COUNT];
	/** [Stored to JSON] */
	XYSEQ_MODE seqMode[PORTS];
	/** [Stored to JSON] */
	XYSEQ_INTERPOLATE seqInterpolate[PORTS];
	/** [Stored to JSON] */
	int seqSelected[PORTS];
	int seqEdit;
	int seqPreview = -1;

	int seqCopyPort = -1;
	int seqCopySeq = -1;

	dsp::SchmittTrigger seqTrigger[PORTS];

	virtual bool seqPortUsed(int i) { 
		return true; 
	}

	void seqInit() {
		for (int i = 0; i < PORTS; i++) {
			seqSelected[i] = 0;
			for (int j = 0; j < XYSEQ_COUNT; j++) {
				seqData[i][j].length = 0;
			}
		}
		seqEdit = -1;
	}

	void seqReset() {
		for (int i = 0; i < PORTS; i++) {
			seqSelected[i] = 0;
			seqMode[i] = XYSEQ_MODE::TRIG_FWD;
			seqInterpolate[i] = XYSEQ_INTERPOLATE::LINEAR;
		}
		seqCopyPort = -1;
		seqCopySeq = -1;
	}

	int seqLength(int port) {
		return seqData[port][seqSelected[port]].length;
	}

	void seqClear(int port) {
		seqData[port][seqSelected[port]].length = 0;
	}

	void seqProcess(engine::Input& input, int port) {
		switch (seqMode[port]) {
			case XYSEQ_MODE::TRIG_FWD: {
				if (seqTrigger[port].process(input.getVoltage())) {
					int t = seqSelected[port];
					do 
						seqSelected[port] = (seqSelected[port] + 1) % XYSEQ_COUNT;
					while (seqData[port][seqSelected[port]].length == 0 && seqSelected[port] != t);
				}
				break;
			}
			case XYSEQ_MODE::TRIG_REV: {
				if (seqTrigger[port].process(input.getVoltage())) {
					int t = seqSelected[port];
					do 
						seqSelected[port] = (seqSelected[port] - 1 + XYSEQ_COUNT) % XYSEQ_COUNT;
					while (seqData[port][seqSelected[port]].length == 0 && seqSelected[port] != t);
				}
				break;
			}
			case XYSEQ_MODE::TRIG_RANDOM_16:
				if (seqTrigger[port].process(input.getVoltage())) {
					seqSelected[port] = std::floor(rescale(random::uniform(), 0.f, 1.f, 0.f, 16.f));
				}
				break;
			case XYSEQ_MODE::TRIG_RANDOM_8:
				if (seqTrigger[port].process(input.getVoltage())) {
					seqSelected[port] = std::floor(rescale(random::uniform(), 0.f, 1.f, 0.f, 8.f));
				}
				break;
			case XYSEQ_MODE::TRIG_RANDOM_4:
				if (seqTrigger[port].process(input.getVoltage())) {
					seqSelected[port] = std::floor(rescale(random::uniform(), 0.f, 1.f, 0.f, 4.f));
				}
				break;
			case XYSEQ_MODE::C4: {
				int s = std::round(clamp(input.getVoltage() * 12.f, 0.f, XYSEQ_COUNT - 1.f));
				seqSelected[port] = s;
				break;
			}
			case XYSEQ_MODE::VOLT: {
				int s = std::floor(rescale(input.getVoltage(), 0.f, 10.f, 0, XYSEQ_COUNT - 1));
				seqSelected[port] = s;
				break;
			}
		}
	}

	Vec seqValue(int port, float pos) {
		XySeqItem* s = &seqData[port][seqSelected[port]];
		if (s->length == 0) return Vec(0.5f, 0.5f);
		int l = s->length - 1;

		switch (seqInterpolate[port]) {
			case XYSEQ_INTERPOLATE::LINEAR: {
				float mu1 = l * pos;
				float intf;
				float mu = std::modf(mu1, &intf);
				int i1 = int(intf);
				int i2 = std::min(int(intf) + 1, l);
				Vec a1 = Vec(s->x[i1], s->y[i1]);
				Vec a2 = Vec(s->x[i2], s->y[i2]);
				Vec d = a2.minus(a1).mult(mu).plus(a1);
				return d;
			}
			case XYSEQ_INTERPOLATE::CUBIC: {
				float mu1 = l * pos;
				float intf;
				float mu = std::modf(mu1, &intf);
				int i0 = std::max(0, int(intf));
				int i1 = int(intf);
				int i2 = std::min(int(intf) + 1, l);
				int i3 = std::min(int(intf) + 2, l);
				float mu2 = mu * mu;
				float x0 = -0.5f * s->x[i0] + 1.5f * s->x[i1] - 1.5f * s->x[i2] + 0.5f * s->x[i3];
				float x1 = s->x[i0] - 2.5f * s->x[i1] + 2.f * s->x[i2] - 0.5f * s->x[i3];
				float x2 = -0.5f * s->x[i0] + 0.5f * s->x[i2];
				float x3 = s->x[i1];
				float x = x0 * mu * mu2 + x1 * mu2 + x2 * mu + x3;
				float y0 = -0.5f * s->y[i0] + 1.5f * s->y[i1] - 1.5f * s->y[i2] + 0.5f * s->y[i3];
				float y1 = s->y[i0] - 2.5f * s->y[i1] + 2.f * s->y[i2] - 0.5f * s->y[i3];
				float y2 = -0.5f * s->y[i0] + 0.5f * s->y[i2];
				float y3 = s->y[i1];
				float y = y0 * mu * mu2 + y1 * mu2 + y2 * mu + y3;
				return Vec(x, y);
			}
			default: {
				return Vec(0, 0);
			}
		}
	}

	void seqRandomize(int port) {
		seqData[port][seqSelected[port]].length = 0;

		unsigned seed = std::chrono::system_clock::now().time_since_epoch().count();
		std::default_random_engine gen(seed);
		std::normal_distribution<float> d{0.f, 0.1f};
		dsp::ExponentialFilter filterX;
		dsp::ExponentialFilter filterY;
		filterX.setLambda(0.7f);
		filterY.setLambda(0.7f);

		// Random length
		int l = std::max(0, std::min(int(XYSEQ_LENGTH / 4 + d(gen) * XYSEQ_LENGTH / 4), XYSEQ_LENGTH - 1));

		// Set some start-value for the exponential filters
		filterX.out = 0.5f + d(gen);
		filterY.out = 0.5f + d(gen);
		int dirX = d(gen) >= 0.f ? 1 : -1;
		int dirY = d(gen) >= 0.f ? 1 : -1;
		float pX = 0.5f;
		float pY = 0.5f;
		for (int c = 0; c < l; c++) {
			// Reduce the number of direction changes, only when rand > 0
			if (d(gen) >= 0.5f) dirX = dirX == -1 ? 1 : -1;
			if (pX == 1.f) dirX = -1;
			if (pX == 0.f) dirX = 1;
			if (d(gen) >= 0.5f) dirY = dirY == -1 ? 1 : -1;
			if (pY == 1.f) dirY = -1;
			if (pY == 0.f) dirY = 1;
			float r;

			r = d(gen);
			pX = filterX.process(1.f, pX + dirX * abs(r));
			// Only range [0,1] is valid
			pX = clamp(pX, 0.f, 1.f);
			seqData[port][seqSelected[port]].x[c] = pX;

			r = d(gen);
			pY = filterY.process(1.f, pY + dirY * abs(r));
			// Only range [0,1] is valid
			pY = clamp(pY, 0.f, 1.f);
			seqData[port][seqSelected[port]].y[c] = pY;
		}
		seqData[port][seqSelected[port]].length = l;
	}

	void seqPreset(int port, XYSEQ_PRESET preset, float x, float y, int parameter) {
		auto _x = [x](float v) { return (v - 0.5f) * x + 0.5f; };
		auto _y = [y](float v) { return (v - 0.5f) * y + 0.5f; };
		
 		switch (preset) {
			case XYSEQ_PRESET::CIRCLE: {
				seqData[port][seqSelected[port]].length = 0;
				int l = XYSEQ_LENGTH / 4;
				float p = 2.f * M_PI / (l - 1);
				for (int i = 0; i < l; i++) {
					seqData[port][seqSelected[port]].x[i] = _x(sin(i * p) / 2.f + 0.5f);
					seqData[port][seqSelected[port]].y[i] = _y(cos(i * p) / 2.f + 0.5f);
				}
				seqData[port][seqSelected[port]].length = l;
				break;
			}
			case XYSEQ_PRESET::SPIRAL: {
				auto _s = [](float v, float s) { return (v - 0.5f) * s + 0.5f; };
				seqData[port][seqSelected[port]].length = 0;
				int l = XYSEQ_LENGTH;
				float p = parameter * 2.f * M_PI / (l - 1);
				for (int i = 0; i < l; i++) {
					seqData[port][seqSelected[port]].x[i] = _x(_s(sin(i * p) / 2.f + 0.5f, 1.f / l * i));
					seqData[port][seqSelected[port]].y[i] = _y(_s(cos(i * p) / 2.f + 0.5f, 1.f / l * i));
				}
				seqData[port][seqSelected[port]].length = l;
				break;
			}
			case XYSEQ_PRESET::SAW: {
				seqData[port][seqSelected[port]].length = 0;
				seqData[port][seqSelected[port]].x[0] = _x(0.f);
				seqData[port][seqSelected[port]].y[0] = _y(1.f);
				int c = parameter;
				for (int i = 0; i < c; i++) {
					seqData[port][seqSelected[port]].x[i + 1] = _x(1.f / (c + 1) * (i + 1));
					seqData[port][seqSelected[port]].y[i + 1] = _y(i % 2);
				}
				seqData[port][seqSelected[port]].x[c + 1] = _x(1.f);
				seqData[port][seqSelected[port]].y[c + 1] = _y(0.f);
				seqData[port][seqSelected[port]].length = c + 2;
				break;
			}
			case XYSEQ_PRESET::SINE: {
				seqData[port][seqSelected[port]].length = 0;
				int l = XYSEQ_LENGTH;
				float p = parameter * 2.f * M_PI / (l - 1);
				for (int i = 0; i < l; i++) {
					seqData[port][seqSelected[port]].x[i] = _x(1.f / l * i);
					seqData[port][seqSelected[port]].y[i] = _y(sin(i * p) / 2.f + 0.5f);
				}
				seqData[port][seqSelected[port]].length = l;
				break;
			}
			case XYSEQ_PRESET::EIGHT: {
				auto _s = [](float v, float s) { return v / s + 0.5f; };
				seqData[port][seqSelected[port]].length = 0;
				int l = XYSEQ_LENGTH / 2.f;
				float p = 2.f * M_PI / (l - 1);
				float o = - M_PI / 2.f;
				for (int i = 0; i < l; i++) {
					seqData[port][seqSelected[port]].x[i] = _x(_s(std::cos(i * p + o), 2.f));
					seqData[port][seqSelected[port]].y[i] = _y(_s(std::cos(i * p + o) * std::sin(i * p + o), 1.f));
				}
				seqData[port][seqSelected[port]].length = l;
				break;
			}
			case XYSEQ_PRESET::ROSE: {
				auto _s = [](float v) { return v / 2.f + 0.5f; };
				seqData[port][seqSelected[port]].length = 0;
				int l = XYSEQ_LENGTH;
				float p = (parameter % 2 == 1 ? 2.f : 1.f) * 2.f * M_PI / (l - 1);
				for (int i = 0; i < l; i++) {
					seqData[port][seqSelected[port]].x[i] = _x(_s(std::cos(parameter / 2.f * i * p) * std::cos(i * p)));
					seqData[port][seqSelected[port]].y[i] = _y(_s(std::cos(parameter / 2.f * i * p) * std::sin(i * p)));
				}
				seqData[port][seqSelected[port]].length = l;
				break;
			}
		}
	}

	void seqRotate(int port, float angle) {
		for (int i = 0; i < seqData[port][seqSelected[port]].length; i++) {
			Vec p = Vec(seqData[port][seqSelected[port]].x[i], seqData[port][seqSelected[port]].y[i]);
			p = p.plus(Vec(-0.5f, -0.5f));
			p = p.rotate(angle);
			p = p.minus(Vec(-0.5f, -0.5f));
			seqData[port][seqSelected[port]].x[i] = std::max(0.f, std::min(p.x, 1.f));
			seqData[port][seqSelected[port]].y[i] = std::max(0.f, std::min(p.y, 1.f));
		}
	}

	void seqFlipHorizontally(int port) {
		for (int i = 0; i < seqData[port][seqSelected[port]].length; i++) {
			seqData[port][seqSelected[port]].y[i] = 1.f - seqData[port][seqSelected[port]].y[i];
		}
	}

	void seqFlipVertically(int port) {
		for (int i = 0; i < seqData[port][seqSelected[port]].length; i++) {
			seqData[port][seqSelected[port]].x[i] = 1.f - seqData[port][seqSelected[port]].x[i];
		}
	}

	void seqCopy(int port) {
		seqCopyPort = port;
		seqCopySeq = seqSelected[port];
	}

	void seqPaste(int port) {
		if (seqCopyPort >= 0) {
			seqData[port][seqSelected[port]].length = 0;
			for (int i = 0; i < seqData[seqCopyPort][seqCopySeq].length; i++) {
				seqData[port][seqSelected[port]].x[i] = seqData[seqCopyPort][seqCopySeq].x[i];
				seqData[port][seqSelected[port]].y[i] = seqData[seqCopyPort][seqCopySeq].y[i];
			}
			seqData[port][seqSelected[port]].length = seqData[seqCopyPort][seqCopySeq].length;
		}
	}

	void dataToJson(json_t* dataJ, int port) {
		json_object_set_new(dataJ, "seqSelected", json_integer(seqSelected[port]));
		json_object_set_new(dataJ, "seqMode", json_integer((int)seqMode[port]));
		json_object_set_new(dataJ, "seqInterpolate", json_integer((int)seqInterpolate[port]));

		json_t* seqDataJ = json_array();
		for (int j = 0; j < XYSEQ_COUNT; j++) {
			XySeqItem* s = &seqData[port][j];
			json_t* seqItemJ = json_object();
			json_t* xJ = json_array();
			json_t* yJ = json_array();
			for (int k = 0; k < s->length; k++) {
				json_array_append_new(xJ, json_real(s->x[k]));
				json_array_append_new(yJ, json_real(s->y[k]));
			}
			json_object_set_new(seqItemJ, "x", xJ);
			json_object_set_new(seqItemJ, "y", yJ);
			json_array_append_new(seqDataJ, seqItemJ);
		}
		json_object_set_new(dataJ, "seqData", seqDataJ);
	}

	void dataFromJson(json_t* dataJ, int port) {
		seqSelected[port] = json_integer_value(json_object_get(dataJ, "seqSelected"));
		seqMode[port] = (XYSEQ_MODE)json_integer_value(json_object_get(dataJ, "seqMode"));
		seqInterpolate[port] = (XYSEQ_INTERPOLATE)json_integer_value(json_object_get(dataJ, "seqInterpolate"));

		json_t* seqDataJ = json_object_get(dataJ, "seqData");
		json_t* seqItemJ;
		size_t seqItemIndex;
		json_array_foreach(seqDataJ, seqItemIndex, seqItemJ) {
			json_t* xsJ = json_object_get(seqItemJ, "x");
			json_t* ysJ = json_object_get(seqItemJ, "y");
			json_t* xJ;
			size_t xIndex;
			json_array_foreach(xsJ, xIndex, xJ) {
				seqData[port][seqItemIndex].x[xIndex] = json_real_value(xJ);
			}
			json_t* yJ;
			size_t yIndex;
			json_array_foreach(ysJ, yIndex, yJ) {
				seqData[port][seqItemIndex].y[yIndex] = json_real_value(yJ);
			}
			seqData[port][seqItemIndex].length = yIndex;
		}
	}
};


template <typename MODULE>
struct XySeqChangeAction : history::ModuleAction {
	int portId;
	int seqId;
	int oldSeqLength, newSeqLength;
	float oldSeqX[XYSEQ_LENGTH], oldSeqY[XYSEQ_LENGTH];
	float newSeqX[XYSEQ_LENGTH], newSeqY[XYSEQ_LENGTH];

	void setOld(MODULE* m, int portId, int seqId) {
		name = m->model->plugin->brand + " " + m->model->name + " seq";
		this->moduleId = m->id;
		this->portId = portId;
		this->seqId = seqId;
		oldSeqLength = m->seqData[portId][seqId].length;
		for (int i = 0; i < oldSeqLength; i++) {
			oldSeqX[i] = m->seqData[portId][seqId].x[i];
			oldSeqY[i] = m->seqData[portId][seqId].y[i];
		}
	}

	void setNew(MODULE* m) {
		newSeqLength = m->seqData[portId][seqId].length;
		for (int i = 0; i < newSeqLength; i++) {
			newSeqX[i] = m->seqData[portId][seqId].x[i];
			newSeqY[i] = m->seqData[portId][seqId].y[i];
		}
	}

	void undo() override {
		app::ModuleWidget* mw = APP->scene->rack->getModule(moduleId);
		assert(mw);
		MODULE* m = dynamic_cast<MODULE*>(mw->module);
		m->seqData[portId][seqId].length = 0;
		for (int i = 0; i < oldSeqLength; i++) {
			m->seqData[portId][seqId].x[i] = oldSeqX[i];
			m->seqData[portId][seqId].y[i] = oldSeqY[i];
		}
		m->seqData[portId][seqId].length = oldSeqLength;
	}

	void redo() override {
		app::ModuleWidget* mw = APP->scene->rack->getModule(moduleId);
		assert(mw);
		MODULE* m = dynamic_cast<MODULE*>(mw->module);
		m->seqData[portId][seqId].length = 0;
		for (int i = 0; i < newSeqLength; i++) {
			m->seqData[portId][seqId].x[i] = newSeqX[i];
			m->seqData[portId][seqId].y[i] = newSeqY[i];
		}
		m->seqData[portId][seqId].length = newSeqLength;
	}
};

template <typename MODULE>
struct XySeqInterpolateMenuItem : MenuItem {
	MODULE* module;
	int id;
	const std::map<XYSEQ_INTERPOLATE, std::string> labels = {
		{ XYSEQ_INTERPOLATE::LINEAR, "Linear" },
		{ XYSEQ_INTERPOLATE::CUBIC, "Cubic" }
	};

	XySeqInterpolateMenuItem(MODULE* module, int id) {
		this->module = module;
		this->id = id;
		text = "Interpolation";
		rightText = labels.at(module->seqInterpolate[id]) + "  " + RIGHT_ARROW;
	}

	Menu* createChildMenu() override {
		Menu* menu = new Menu;
		for (const auto& i : labels) {
			menu->addChild(StoermelderPackOne::Rack::createValuePtrMenuItem(i.second, &module->seqInterpolate[id], i.first));
		}
		return menu;
	}
};

template <typename MODULE>
struct XySeqSlotMenuItem : MenuItem {
	MODULE* module;
	int id;

	XySeqSlotMenuItem(MODULE* module, int id) {
		this->module = module;
		this->id = id;
		text = "Slot";
		rightText = string::f("%02u", module->seqSelected[id] + 1) + "  " + RIGHT_ARROW;
	}
	
	Menu* createChildMenu() override {
		Menu* menu = new Menu;
		for (int i = 0; i < XYSEQ_COUNT; i++) {
			menu->addChild(StoermelderPackOne::Rack::createValuePtrMenuItem(string::f("%02u", i + 1), &module->seqSelected[id], i));
		}
		return menu;
	}
};

template <typename MODULE>
struct XySeqTriggerMenuItem : MenuItem {
	MODULE* module;
	int id;
	const std::map<XYSEQ_MODE, std::string> labels = {
		{ XYSEQ_MODE::TRIG_FWD, "Trigger forward" },
		{ XYSEQ_MODE::TRIG_REV, "Trigger reverse" },
		{ XYSEQ_MODE::TRIG_RANDOM_16, "Trigger random 1-16" },
		{ XYSEQ_MODE::TRIG_RANDOM_8, "Trigger random 1-8" },
		{ XYSEQ_MODE::TRIG_RANDOM_4, "Trigger random 1-4" },
		{ XYSEQ_MODE::VOLT, "0..10V" },
		{ XYSEQ_MODE::C4, "C4-D#5" }
	};

	XySeqTriggerMenuItem(MODULE* module, int id) {
		this->module = module;
		this->id = id;
		text = "Trigger mode";
		rightText = labels.at(module->seqMode[id]) + "  " + RIGHT_ARROW;
	}

	Menu* createChildMenu() override {
		Menu* menu = new Menu;
		for (const auto& i : labels) {
			menu->addChild(StoermelderPackOne::Rack::createValuePtrMenuItem(i.second, &module->seqMode[id], i.first));
		}
		return menu;
	}
};

template <typename MODULE>
ui::MenuItem* XySeqPresetMenuItem(MODULE* module) {
	struct XySeqPresetMenuItem_ : MenuItem {
		MODULE* module;

		float x = 1.0f;
		float y = 1.0f;
		int parameter = 6;

		XySeqPresetMenuItem_(MODULE* module) {
			this->module = module;
			text = "Preset";
			rightText = RIGHT_ARROW;
		}

		struct XSlider : ui::Slider {
			struct XQuantity : Quantity {
				XySeqPresetMenuItem_* item;

				XQuantity(XySeqPresetMenuItem_* item) {
					this->item = item;
				}
				void setValue(float value) override {
					item->x = math::clamp(value, 0.f, 1.f);
				}
				float getValue() override {
					return item->x;
				}
				float getDefaultValue() override {
					return 0.5;
				}
				float getDisplayValue() override {
					return getValue() * 100;
				}
				void setDisplayValue(float displayValue) override {
					setValue(displayValue / 100);
				}
				std::string getLabel() override {
					return "Scale x";
				}
				std::string getUnit() override {
					return "%";
				}
			};

			XSlider(XySeqPresetMenuItem_* item) {
				quantity = new XQuantity(item);
			}
			~XSlider() {
				delete quantity;
			}
		};

		struct YSlider : ui::Slider {
			struct YQuantity : Quantity {
				XySeqPresetMenuItem_* item;

				YQuantity(XySeqPresetMenuItem_* item) {
					this->item = item;
				}
				void setValue(float value) override {
					item->y = math::clamp(value, 0.f, 1.f);
				}
				float getValue() override {
					return item->y;
				}
				float getDefaultValue() override {
					return 0.5;
				}
				float getDisplayValue() override {
					return getValue() * 100;
				}
				void setDisplayValue(float displayValue) override {
					setValue(displayValue / 100);
				}
				std::string getLabel() override {
					return "Scale y";
				}
				std::string getUnit() override {
					return "%";
				}
			};

			YSlider(XySeqPresetMenuItem_* item) {
				quantity = new YQuantity(item);
			}
			~YSlider() {
				delete quantity;
			}
		};

		struct ParameterSlider : ui::Slider {
			struct ParameterQuantity : Quantity {
				XySeqPresetMenuItem_* item;
				float v = -1.f;

				ParameterQuantity(XySeqPresetMenuItem_* item) {
					this->item = item;
				}
				void setValue(float value) override {
					v = clamp(value, 2.f, 12.f);
					item->parameter = int(v);
				}
				float getValue() override {
					if (v < 0.f) v = item->parameter;
					return v;
				}
				float getDefaultValue() override {
					return 6.f;
				}
				float getMinValue() override {
					return 2.f;
				}
				float getMaxValue() override {
					return 12.f;
				}
				float getDisplayValue() override {
					return getValue();
				}
				std::string getDisplayValueString() override {
					int i = int(getValue());
					return string::f("%i", i);
				}
				void setDisplayValue(float displayValue) override {
					setValue(displayValue);
				}
				std::string getLabel() override {
					return "Parameter";
				}
				std::string getUnit() override {
					return "";
				}
			};

			ParameterSlider(XySeqPresetMenuItem_* item) {
				quantity = new ParameterQuantity(item);
			}
			~ParameterSlider() {
				delete quantity;
			}
			void onDragMove(const event::DragMove& e) override {
				if (quantity) {
					quantity->moveScaledValue(0.002f * e.mouseDelta.x);
				}
			}
		};

		Menu* createChildMenu() override {
			Menu* menu = new Menu;

			auto h = [=](XYSEQ_PRESET preset) {
				XySeqChangeAction<MODULE>* h = new XySeqChangeAction<MODULE>;
				h->setOld(module, module->seqEdit, module->seqSelected[module->seqEdit]);
				h->name += " preset";
				module->seqPreset(module->seqEdit, preset, this->x, this->y, this->parameter);
				h->setNew(module);
				APP->history->push(h);
			};

			menu->addChild(createMenuItem("Circle", "", [=] { h(XYSEQ_PRESET::CIRCLE); }));
			menu->addChild(createMenuItem("Spiral", "", [=] { h(XYSEQ_PRESET::SPIRAL); }));
			menu->addChild(createMenuItem("Saw", "", [=] { h(XYSEQ_PRESET::SAW); }));
			menu->addChild(createMenuItem("Sine", "", [=] { h(XYSEQ_PRESET::SINE); }));
			menu->addChild(createMenuItem("Eight", "", [=] { h(XYSEQ_PRESET::EIGHT); }));
			menu->addChild(createMenuItem("Rose", "", [=] { h(XYSEQ_PRESET::ROSE); }));

			XSlider* xSlider = new XSlider(this);
			xSlider->box.size.x = 120.0f;
			menu->addChild(xSlider);

			YSlider* ySlider = new YSlider(this);
			ySlider->box.size.x = 120.0f;
			menu->addChild(ySlider);

			ParameterSlider* parameterSlider = new ParameterSlider(this);
			parameterSlider->box.size.x = 120.0f;
			menu->addChild(parameterSlider);

			return menu;
		}
	};

	return new XySeqPresetMenuItem_(module);
}


template <typename MODULE>
struct XySeqEditDragWidget : OpaqueWidget {
	const float radius = 8.f;
	const float fontsize = 13.0f;

	MODULE* module;
	NVGcolor color = color::RED;
	int id = -1;
	int seq = -1;

	int index;
	math::Vec dragPos;
	XySeqChangeAction<MODULE>* dragChange;
	std::chrono::time_point<std::chrono::system_clock> timer;
	bool timerClear;

	XySeqEditDragWidget() {
		box.size = Vec(2 * radius, 2 * radius);
	}

	void init(int id, int seq) {
		this->id = id;
		this->seq = seq;
		index = 0;

		if (id >= 0) {
			if (module->seqData[id][seq].length == 0) {
				box.pos.x = parent->box.size.x / 2.f - radius;
				box.pos.y = parent->box.size.y / 2.f - radius;
			}
			else {
				box.pos.x = (parent->box.size.x - box.size.x) * module->seqData[id][seq].x[0];
				box.pos.y = (parent->box.size.y - box.size.y) * module->seqData[id][seq].y[0];
			}
		}
	}

	void clear() {
		index = 0;

		XySeqChangeAction<MODULE>* h = new XySeqChangeAction<MODULE>;
		h->setOld(module, id, seq);
		h->name += " clear";

		module->seqData[id][seq].length = 0;

		h->setNew(module);
		APP->history->push(h);
	}

	void drawLayer(const Widget::DrawArgs& args, int layer) override {
		if (!module) return;

		if (layer == 1 && id >= 0) {
			Vec c = Vec(box.size.x / 2.f, box.size.y / 2.f);

			nvgGlobalCompositeOperation(args.vg, NVG_LIGHTER);

			// Draw circle
			nvgBeginPath(args.vg);
			nvgCircle(args.vg, c.x, c.y, radius);
			nvgStrokeColor(args.vg, color);
			nvgStrokeWidth(args.vg, 1.f);
			nvgStroke(args.vg);
			nvgFillColor(args.vg, color::mult(color, 0.5f));
			nvgFill(args.vg);

			// Draw label
			std::shared_ptr<Font> font = APP->window->loadFont(asset::system("res/fonts/ShareTechMono-Regular.ttf"));
			nvgFontSize(args.vg, fontsize);
			nvgFontFaceId(args.vg, font->handle);
			nvgFillColor(args.vg, color);
			nvgTextBox(args.vg, c.x - 3.f, c.y + 4.f, 120, string::f("%i", id + 1).c_str(), NULL);
		}
		OpaqueWidget::drawLayer(args, layer);
	}

	void onHover(const event::Hover& e) override {
		math::Vec c = box.size.div(2);
		float dist = e.pos.minus(c).norm();
		if (dist <= c.x) {
			OpaqueWidget::onHover(e);
		}
	}

	void onButton(const event::Button& e) override {
		math::Vec c = box.size.div(2);
		float dist = e.pos.minus(c).norm();
		if (dist <= c.x) {
			OpaqueWidget::onButton(e);
			if (e.action == GLFW_PRESS && e.button == GLFW_MOUSE_BUTTON_LEFT) {
				e.consume(this);
			}
		}
		else {
			OpaqueWidget::onButton(e);
		}
	}

	void onDragStart(const event::DragStart& e) override {
		if (e.button != GLFW_MOUSE_BUTTON_LEFT)
			return;

		dragPos = APP->scene->rack->getMousePos().minus(box.pos);
		timerClear = true;
		module->seqData[id][seq].length = 0;

		// history
		dragChange = new XySeqChangeAction<MODULE>;
		dragChange->setOld(module, module->seqEdit, module->seqSelected[module->seqEdit]);
		dragChange->name += " drag";
	}

	void onDragEnd(const event::DragEnd& e) override {
		dragChange->setNew(module);
		APP->history->push(dragChange);
		dragChange = NULL;
	}

	void onDragMove(const event::DragMove& e) override {
		if (e.button != GLFW_MOUSE_BUTTON_LEFT)
			return;

		math::Vec pos = APP->scene->rack->getMousePos().minus(dragPos);
		pos.x = std::max(0.f, std::min(pos.x, parent->box.size.x - box.size.x));
		pos.y = std::max(0.f, std::min(pos.y, parent->box.size.y - box.size.y));
		box.pos = pos;

		auto now = std::chrono::system_clock::now();
		if (timerClear || now - timer > std::chrono::milliseconds{65}) {
			if (index < XYSEQ_LENGTH) {
				float x = pos.x / (parent->box.size.x - box.size.x);
				float y = pos.y / (parent->box.size.y - box.size.y);

				module->seqData[id][seq].x[index] = x;
				module->seqData[id][seq].y[index] = y;
				module->seqData[id][seq].length = index + 1;
				index++;
			}
			timer = now;
			timerClear = false;
		}
		OpaqueWidget::onDragMove(e);
	}
};


template <typename MODULE>
struct XySeqEditWidget : OpaqueWidget {
	MODULE* module;
	XySeqEditDragWidget<MODULE>* recWidget;
	int mixParamIdX;
	int mixParamIdY;
	int lastSeqId = -1;
	int lastSeqSelected = -1;

	XySeqEditWidget(MODULE* module, int mixParamIdX, int mixParamIdY) {
		this->module = module;
		this->mixParamIdX = mixParamIdX;
		this->mixParamIdY = mixParamIdY;

		recWidget = new XySeqEditDragWidget<MODULE>;
		recWidget->module = module;
		addChild(recWidget);
	}

	void step() override {
		OpaqueWidget::step();
		if (!module) return;

		int seqId = module->seqEdit;
		int seqSelected = module->seqSelected[module->seqEdit];

		if (module->seqEdit >= 0) {
			if (lastSeqId != seqId || lastSeqSelected != seqSelected)
				recWidget->init(seqId, seqSelected);
		}
		else {
			recWidget->init(-1, -1);
		}
		lastSeqId = seqId;
		lastSeqSelected = seqSelected;
	}

	void drawLayer(const DrawArgs& args, int layer) override {
		if (!module)
			return;

		if (layer == 1) {
			if (module->seqEdit >= 0) {
				// Dim the display but don't darken it completely
				float b = std::max(0.4f, settings::rackBrightness);
				nvgGlobalTint(args.vg, nvgRGBAf(b, b, b, 1.f));

				NVGcolor c = color::mult(color::WHITE, 0.7f);
				float stroke = 1.f;
				
				// Draw outer border
				nvgBeginPath(args.vg);
				nvgRect(args.vg, 0.f, 0.f, box.size.x, box.size.y);
				nvgStrokeWidth(args.vg, stroke);
				nvgStrokeColor(args.vg, c);
				nvgStroke(args.vg);

				// Draw "EDIT" text
				std::shared_ptr<Font> font = APP->window->loadFont(asset::system("res/fonts/ShareTechMono-Regular.ttf"));
				nvgFontSize(args.vg, 22);
				nvgFontFaceId(args.vg, font->handle);
				nvgTextLetterSpacing(args.vg, -2.2);
				nvgFillColor(args.vg, c);
				nvgTextBox(args.vg, box.size.x - 78.f, box.size.y - 6.f, 120, "SEQ-EDIT", NULL);

				OpaqueWidget::drawLayer(args, layer);

				// Draw raw automation line
				XySeqItem* s = &module->seqData[lastSeqId][lastSeqSelected];
				if (s->length > 1) {
					float sizeX = box.size.x - recWidget->box.size.x;
					float sizeY = box.size.y - recWidget->box.size.y;
					nvgBeginPath(args.vg);
					for (int i = 0; i < s->length; i++) {
						float x = recWidget->box.size.x / 2.f + sizeX * s->x[i];
						float y = recWidget->box.size.y / 2.f + sizeY * s->y[i];
						if (i == 0)
							nvgMoveTo(args.vg, x, y);
						else
							nvgLineTo(args.vg, x, y);
					}

					nvgStrokeColor(args.vg, nvgRGB(0xd8, 0xd8, 0xd8));
					nvgLineCap(args.vg, NVG_ROUND);
					nvgMiterLimit(args.vg, 2.0);
					nvgStrokeWidth(args.vg, 1.0);
					nvgGlobalCompositeOperation(args.vg, NVG_LIGHTER);
					nvgStroke(args.vg);
				}
			}

			if (module->seqEdit < 0 && module->seqPreview >= 0) {
				// Draw interpolated automation line if selected
				float sizeX = box.size.x - recWidget->box.size.x;
				float sizeY = box.size.y - recWidget->box.size.y;
				nvgBeginPath(args.vg);
				int segments = module->seqLength(module->seqPreview) * 5;
				float seg1 = 1.f / segments;
				for (int i = 0; i < segments; i++) {
					Vec p = module->seqValue(module->seqPreview, seg1 * i);
					float x = recWidget->box.size.x / 2.f + sizeX * p.x;
					float y = recWidget->box.size.y / 2.f + sizeY * p.y;
					if (i == 0)
						nvgMoveTo(args.vg, x, y);
					else
						nvgLineTo(args.vg, x, y);
				}

				nvgStrokeColor(args.vg, color::mult(color::RED, 0.7f));
				nvgLineCap(args.vg, NVG_ROUND);
				nvgMiterLimit(args.vg, 2.0);
				nvgStrokeWidth(args.vg, 1.0);
				nvgGlobalCompositeOperation(args.vg, NVG_LIGHTER);
				nvgStroke(args.vg);
			}
		}
	}

	void onButton(const event::Button& e) override {
		if (lastSeqId >= 0) {
			Widget::onButton(e);
			if (e.action == GLFW_PRESS && e.button == GLFW_MOUSE_BUTTON_LEFT && !e.isConsumed()) {
				recWidget->box.pos.x = e.pos.x - recWidget->radius;
				recWidget->box.pos.y = e.pos.y - recWidget->radius;
				recWidget->clear();
				e.consume(this);
			}
			if (e.button == GLFW_PRESS && e.button == GLFW_MOUSE_BUTTON_RIGHT && !e.isConsumed()) {
				createContextMenu();
				e.consume(this);
			}
		}
	}

	void createContextMenu() {
		ui::Menu* menu = createMenu();

		auto h = [=](const char* suffix, std::function<void()> action) {
			XySeqChangeAction<MODULE>* h = new XySeqChangeAction<MODULE>;
			h->setOld(module, module->seqEdit, module->seqSelected[module->seqEdit]);
			h->name += " " + std::string(suffix);
			action();
			h->setNew(module);
			APP->history->push(h);
		};

		menu->addChild(createMenuLabel("Motion-Sequence"));
		menu->addChild(new XySeqSlotMenuItem<MODULE>(module, module->seqEdit));
		menu->addChild(new XySeqInterpolateMenuItem<MODULE>(module, module->seqEdit));
		menu->addChild(new XySeqTriggerMenuItem<MODULE>(module, module->seqEdit));
		menu->addChild(construct<MenuSeparator>());
		menu->addChild(createMenuItem("Clear", "", [=] { h("clear", [=] { module->seqClear(module->seqEdit); }); }));
		menu->addChild(createMenuItem("Flip horizontally", "", [=] { h("flip horizontally", [=] { module->seqFlipHorizontally(module->seqEdit); }); }));
		menu->addChild(createMenuItem("Flip vertically", "", [=] { h("flip vertically", [=] { module->seqFlipVertically(module->seqEdit); }); }));
		menu->addChild(createMenuItem("Rotate 45 degrees", "", [=] { h("rotate", [=] { module->seqRotate(module->seqEdit, M_PI / 4.f); }); }));
		menu->addChild(createMenuItem("Rotate 90 degrees", "", [=] { h("rotate", [=] { module->seqRotate(module->seqEdit, M_PI / 2.f); }); }));
		menu->addChild(construct<MenuSeparator>());
		menu->addChild(createMenuItem("Random motion", "", [=] { h("randomize", [=] { module->seqRandomize(module->seqEdit); }); }));
		menu->addChild(XySeqPresetMenuItem(module));
		menu->addChild(construct<MenuSeparator>());
		menu->addChild(createMenuItem("Copy", "", [=] { XySeqEditWidget::module->seqCopy(module->seqEdit); }));
		menu->addChild(createMenuItem("Paste", "", [=] { h("paste", [=] { module->seqPaste(module->seqEdit); }); }));
	}
};


template <typename MODULE>
struct XySeqLedDisplay : StoermelderLedDisplay {
	MODULE* module;
	int id;

	XySeqLedDisplay() {
		box.size = Vec(16.9f, 13.2f);
	}

	void step() override {
		if (module) {
			text = module->seqPortUsed(id) ? "" : string::f("%02d", module->seqSelected[id] + 1);
			color = module->seqEdit == id ? color::RED : nvgRGB(0xf0, 0xf0, 0xf0);
		}
		else {
			text = "00";
		}
		StoermelderLedDisplay::step();
	}

	void onButton(const event::Button& e) override {
		if (module->seqPortUsed(id)) return;
		if (e.action == GLFW_PRESS && e.button == GLFW_MOUSE_BUTTON_RIGHT) {
			createContextMenu();
			e.consume(this);
		}
		if (e.action == GLFW_PRESS && e.button == GLFW_MOUSE_BUTTON_LEFT) {
			if (module->seqEdit == id) {
				module->seqEdit = -1;
			}
			else {
				module->seqEdit = id;
			}
			e.consume(this);
		}
		StoermelderLedDisplay::onButton(e);
	}

	void draw(const DrawArgs& args) override {
		StoermelderLedDisplay::draw(args);
		if (module && module->seqEdit == id) {
			drawRedHalo(args);
		}
	}

	void drawRedHalo(const DrawArgs& args) {
		float radiusX = box.size.x / 2.0;
		float radiusY = box.size.x / 2.0;
		float oradiusX = 2 * radiusX;
		float oradiusY = 2 * radiusY;
		nvgBeginPath(args.vg);
		nvgRect(args.vg, radiusX - oradiusX, radiusY - oradiusY, 2 * oradiusX, 2 * oradiusY);

		NVGpaint paint;
		NVGcolor icol = color::mult(color, 0.65f);
		NVGcolor ocol = nvgRGB(0, 0, 0);

		paint = nvgRadialGradient(args.vg, radiusX, radiusY, radiusX, oradiusY, icol, ocol);
		nvgFillPaint(args.vg, paint);
		nvgGlobalCompositeOperation(args.vg, NVG_LIGHTER);
		nvgFill(args.vg);
	}

	void createContextMenu() {
		ui::Menu* menu = createMenu();
		menu->addChild(createMenuLabel(getPortName()));
		menu->addChild(new MenuSeparator());
		menu->addChild(createMenuLabel("Motion-Sequence"));
		menu->addChild(new XySeqSlotMenuItem<MODULE>(module, id));
		menu->addChild(new XySeqInterpolateMenuItem<MODULE>(module, id));
		menu->addChild(new XySeqTriggerMenuItem<MODULE>(module, id));
	}

	virtual std::string getPortName() { return string::f("Port %i", id + 1); }
	virtual void appendContextMenu(Menu* menu) {}
};


} // namespace StoermelderPackOne