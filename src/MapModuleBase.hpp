#pragma once
#include "plugin.hpp"
#include "settings.hpp"
#include "helpers/StripIdFixModule.hpp"
#include "components/ParamHandleIndicator.hpp"
#include "components/MenuLabelEx.hpp"
#include "components/SubMenuSlider.hpp"
#include "digital/ScaledMapParam.hpp"
#include <chrono>


// Abstract modules

namespace StoermelderPackOne {

template< int MAX_CHANNELS >
struct MapModuleBase : Module, StripIdFixModule {
	/** Number of maps */
	int mapLen = 0;
	/** The mapped param handle of each channel */
	ParamHandle paramHandles[MAX_CHANNELS];
	StoermelderPackOne::ParamHandleIndicator paramHandleIndicator[MAX_CHANNELS];

	/** Channel ID of the learning session */
	int learningId;
	/** Whether the param has been set during the learning session */
	bool learnedParam;

	/** [Stored to JSON] */
	bool textScrolling = true;

	NVGcolor mappingIndicatorColor = color::BLACK_TRANSPARENT;
	/** [Stored to JSON] */
	bool mappingIndicatorHidden = false;

	/** The smoothing processor (normalized between 0 and 1) of each channel */
	dsp::ExponentialFilter valueFilters[MAX_CHANNELS];

	dsp::ClockDivider indicatorDivider;

	MapModuleBase() {
		for (int id = 0; id < MAX_CHANNELS; id++) {
			paramHandleIndicator[id].color = mappingIndicatorColor;
			paramHandleIndicator[id].handle = &paramHandles[id];
			APP->engine->addParamHandle(&paramHandles[id]);
		}
		indicatorDivider.setDivision(2048);
	}

	~MapModuleBase() {
		for (int id = 0; id < MAX_CHANNELS; id++) {
			APP->engine->removeParamHandle(&paramHandles[id]);
		}
	}

	void onReset() override {
		learningId = -1;
		learnedParam = false;
		// Use NoLock because we're already in an Engine write-lock if Engine::resetModule().
		// We also might be in the constructor, which could cause problems, but when constructing, all ParamHandles will point to no Modules anyway.
		clearMaps_NoLock();
		mapLen = 0;
	}

	void process(const ProcessArgs& args) override {
		if (indicatorDivider.process()) {
			float t = indicatorDivider.getDivision() * args.sampleTime;
			for (int i = 0; i < MAX_CHANNELS; i++) {
				paramHandleIndicator[i].color = mappingIndicatorHidden ? color::BLACK_TRANSPARENT : mappingIndicatorColor;
				if (paramHandles[i].moduleId >= 0) {
					paramHandleIndicator[i].process(t, learningId == i);
				}
			}
		}
	}

	ParamQuantity* getParamQuantity(int id) {
		// Get Module
		Module* module = paramHandles[id].module;
		if (!module)
			return NULL;
		// Get ParamQuantity
		int paramId = paramHandles[id].paramId;
		ParamQuantity* paramQuantity = module->paramQuantities[paramId];
		if (!paramQuantity)
			return NULL;
		if (!paramQuantity->isBounded())
			return NULL;
		return paramQuantity;
	}

	virtual void clearMap(int id) {
		if (paramHandles[id].moduleId < 0) return;
		learningId = -1;
		APP->engine->updateParamHandle(&paramHandles[id], -1, 0, true);
		valueFilters[id].reset();
		updateMapLen();
	}

	void clearMaps_NoLock() {
		learningId = -1;
		for (int id = 0; id < MAX_CHANNELS; id++) {
			APP->engine->updateParamHandle_NoLock(&paramHandles[id], -1, 0, true);
			valueFilters[id].reset();
		}
		mapLen = 0;
	}

	virtual void updateMapLen() {
		// Find last nonempty map
		int id;
		for (id = MAX_CHANNELS - 1; id >= 0; id--) {
			if (paramHandles[id].moduleId >= 0)
				break;
		}
		mapLen = id + 1;
		// Add an empty "Mapping..." slot
		if (mapLen < MAX_CHANNELS)
			mapLen++;
	}

	virtual void commitLearn() {
		if (learningId < 0)
			return;
		if (!learnedParam)
			return;
		// Reset learned state
		learnedParam = false;
		// Find next incomplete map
		while (++learningId < MAX_CHANNELS) {
			if (paramHandles[learningId].moduleId < 0)
				return;
		}
		learningId = -1;
	}

	virtual void enableLearn(int id) {
		if (learningId != id) {
			learningId = id;
			learnedParam = false;
		}
	}

	virtual void disableLearn(int id) {
		if (learningId == id) {
			learningId = -1;
		}
	}

	virtual void learnParam(int id, int64_t moduleId, int paramId) {
		APP->engine->updateParamHandle(&paramHandles[id], moduleId, paramId, true);
		learnedParam = true;
		commitLearn();
		updateMapLen();
	}

 
	json_t* dataToJson() override {
		json_t* rootJ = json_object();
		json_object_set_new(rootJ, "textScrolling", json_boolean(textScrolling));
		json_object_set_new(rootJ, "mappingIndicatorHidden", json_boolean(mappingIndicatorHidden));

		json_t* mapsJ = json_array();
		for (int id = 0; id < mapLen; id++) {
			json_t* mapJ = json_object();
			json_object_set_new(mapJ, "moduleId", json_integer(paramHandles[id].moduleId));
			json_object_set_new(mapJ, "paramId", json_integer(paramHandles[id].paramId));
			dataToJsonMap(mapJ, id);
			json_array_append_new(mapsJ, mapJ);
		}
		json_object_set_new(rootJ, "maps", mapsJ);

		return rootJ;
	}

	void dataFromJson(json_t* rootJ) override {
		// Use NoLock because we're already in an Engine write-lock.
		clearMaps_NoLock();

		json_t* textScrollingJ = json_object_get(rootJ, "textScrolling");
		textScrolling = json_boolean_value(textScrollingJ);
		json_t* mappingIndicatorHiddenJ = json_object_get(rootJ, "mappingIndicatorHidden");
		mappingIndicatorHidden = json_boolean_value(mappingIndicatorHiddenJ);

		json_t* mapsJ = json_object_get(rootJ, "maps");
		if (mapsJ) {
			json_t* mapJ;
			size_t mapIndex;
			json_array_foreach(mapsJ, mapIndex, mapJ) {
				json_t* moduleIdJ = json_object_get(mapJ, "moduleId");
				json_t* paramIdJ = json_object_get(mapJ, "paramId");
				if (!(moduleIdJ && paramIdJ))
					continue;
				if (mapIndex >= MAX_CHANNELS)
					continue;
				int64_t moduleId = json_integer_value(moduleIdJ);
				int paramId = json_integer_value(paramIdJ);
				moduleId = idFix(moduleId);
				APP->engine->updateParamHandle_NoLock(&paramHandles[mapIndex], moduleId, paramId, false);
				dataFromJsonMap(mapJ, mapIndex);
			}
		}
		updateMapLen();
		idFixClearMap();
	}

	virtual void dataToJsonMap(json_t* mapJ, int index) {}
	virtual void dataFromJsonMap(json_t* mapJ, int index) {}
};

template< int MAX_CHANNELS >
struct CVMapModuleBase : MapModuleBase<MAX_CHANNELS> {
	bool bipolarInput = false;

	/** Track last values */
	float lastValue[MAX_CHANNELS];
	/** [Saved to JSON] Allow manual changes of target parameters */
	bool lockParameterChanges = true;

	CVMapModuleBase() {
		this->mappingIndicatorColor = nvgRGB(0xff, 0x40, 0xff);
	}

	void process(const Module::ProcessArgs &args) override {
		MapModuleBase<MAX_CHANNELS>::process(args);
	}

	json_t* dataToJson() override {
		json_t* rootJ = MapModuleBase<MAX_CHANNELS>::dataToJson();
		json_object_set_new(rootJ, "lockParameterChanges", json_boolean(lockParameterChanges));
		json_object_set_new(rootJ, "bipolarInput", json_boolean(bipolarInput));

		return rootJ;
	}

	void dataFromJson(json_t* rootJ) override {
		MapModuleBase<MAX_CHANNELS>::dataFromJson(rootJ);

		json_t* lockParameterChangesJ = json_object_get(rootJ, "lockParameterChanges");
		lockParameterChanges = json_boolean_value(lockParameterChangesJ);

		json_t* bipolarInputJ = json_object_get(rootJ, "bipolarInput");
		bipolarInput = json_boolean_value(bipolarInputJ);
	}
};


// Widgets


template< int MAX_CHANNELS, typename MODULE >
struct MapModuleChoice : LedDisplayChoice {
	MODULE* module = NULL;
	bool processEvents = true;
	int id;

	std::chrono::time_point<std::chrono::system_clock> hscrollUpdate = std::chrono::system_clock::now();
	int hscrollCharOffset = 0;

	MapModuleChoice() {
		box.size = mm2px(Vec(0, 7.5));
		textOffset = Vec(6, 14.7);
		color = nvgRGB(0xf0, 0xf0, 0xf0);
	}

	~MapModuleChoice() {
		if (module && module->learningId == id) {
			glfwSetCursor(APP->window->win, NULL);
		}
	}

	void setModule(MODULE* module) {
		this->module = module;
	}

	void onButton(const event::Button& e) override {
		e.stopPropagating();
		if (!module) return;
		if (module->locked) return;

		if (e.action == GLFW_PRESS && e.button == GLFW_MOUSE_BUTTON_LEFT) {
			e.consume(this);
		}

		if (e.action == GLFW_PRESS && e.button == GLFW_MOUSE_BUTTON_RIGHT) {
			e.consume(this);

			if (module->paramHandles[id].moduleId >= 0) {
				createContextMenu();
			} 
			else {
				module->clearMap(id);
			}
		}
	}

	void createContextMenu() {
		struct UnmapItem : MenuItem {
			MODULE* module;
			int id;
			void onAction(const event::Action& e) override {
				module->clearMap(id);
			}
		};

		struct IndicateItem : MenuItem {
			MODULE* module;
			int id;
			void onAction(const event::Action& e) override {
				ParamHandle* paramHandle = &module->paramHandles[id];
				ModuleWidget* mw = APP->scene->rack->getModule(paramHandle->moduleId);
				module->paramHandleIndicator[id].indicate(mw);
			}
		};

		ui::Menu* menu = createMenu();
		menu->addChild(createMenuLabel("Parameter \"" + getParamName() + "\""));
		menu->addChild(construct<IndicateItem>(&MenuItem::text, "Locate and indicate", &IndicateItem::module, module, &IndicateItem::id, id));
		menu->addChild(construct<UnmapItem>(&MenuItem::text, "Unmap", &UnmapItem::module, module, &UnmapItem::id, id));
		appendContextMenu(menu);
	}

	virtual void appendContextMenu(Menu* menu) { }

	void onSelect(const event::Select& e) override {
		if (!module) return;
		if (module->locked) return;

		ScrollWidget *scroll = getAncestorOfType<ScrollWidget>();
		scroll->scrollTo(box);

		// Reset touchedParam, unstable API
		APP->scene->rack->touchedParam = NULL;
		module->enableLearn(id);

		GLFWcursor* cursor = glfwCreateStandardCursor(GLFW_CROSSHAIR_CURSOR);
		glfwSetCursor(APP->window->win, cursor);
	}

	void onDeselect(const event::Deselect& e) override {
		if (!module) return;
		if (!processEvents) return;

		// Check if a ParamWidget was touched, unstable API
		ParamWidget *touchedParam = APP->scene->rack->touchedParam;
		if (touchedParam && touchedParam->getParamQuantity()->module != module) {
			APP->scene->rack->touchedParam = NULL;
			int64_t moduleId = touchedParam->getParamQuantity()->module->id;
			int paramId = touchedParam->getParamQuantity()->paramId;
			module->learnParam(id, moduleId, paramId);
			hscrollCharOffset = 0;
		} 
		else {
			module->disableLearn(id);
		}
		glfwSetCursor(APP->window->win, NULL);
	}

	void step() override {
		if (!module)
			return;

		// Set bgColor and selected state
		if (module->learningId == id) {
			bgColor = color;
			bgColor.a = 0.15;
			if (APP->event->getSelectedWidget() != this)
				APP->event->setSelectedWidget(this);
		} 
		else {
			bgColor = nvgRGBA(0, 0, 0, 0);
			if (APP->event->getSelectedWidget() == this)
				APP->event->setSelectedWidget(NULL);
		}

		// Set text
		if (module->paramHandles[id].moduleId >= 0 && module->learningId != id) {
			std::string prefix = "";
			std::string label = getSlotLabel();
			if (label == "") {
				prefix = getSlotPrefix();
				label = getParamName();
				if (label == "") {
					module->clearMap(id);
					return;
				}
			}

			size_t hscrollMaxLength = ceil(box.size.x / 6.2f);
			if (module->textScrolling && label.length() + prefix.length() > hscrollMaxLength) {
				// Scroll the parameter-name horizontically
				text = prefix + label.substr(hscrollCharOffset > (int)label.length() ? 0 : hscrollCharOffset);
				auto now = std::chrono::system_clock::now();
				if (now - hscrollUpdate > std::chrono::milliseconds{100}) {
					hscrollCharOffset = (hscrollCharOffset + 1) % (label.length() + hscrollMaxLength);
					hscrollUpdate = now;
				}
			} 
			else {
				text = prefix + label;
			}
		} 
		else {
			if (module->learningId == id) {
				text = getSlotPrefix() + "Mapping...";
			} else {
				text = getSlotPrefix() + "Unmapped";
			}
		}

		// Set text color
		if (module->paramHandles[id].moduleId >= 0 || module->learningId == id) {
			color.a = 1.0;
		} 
		else {
			color.a = 0.5;
		}
	}

	virtual std::string getSlotLabel() {
		return "";
	}

	virtual std::string getSlotPrefix() {
		return MAX_CHANNELS > 1 ? string::f("%02d ", id + 1) : "";
	}

	ParamQuantity* getParamQuantity() {
		if (!module)
			return NULL;
		if (id >= module->mapLen)
			return NULL;
		ParamHandle* paramHandle = &module->paramHandles[id];
		if (paramHandle->moduleId < 0)
			return NULL;
		ModuleWidget *mw = APP->scene->rack->getModule(paramHandle->moduleId);
		if (!mw)
			return NULL;
		// Get the Module from the ModuleWidget instead of the ParamHandle.
		// I think this is more elegant since this method is called in the app world instead of the engine world.
		Module* m = mw->module;
		if (!m)
			return NULL;
		int paramId = paramHandle->paramId;
		if (paramId >= (int) m->params.size())
			return NULL;
		ParamQuantity* paramQuantity = m->paramQuantities[paramId];
		return paramQuantity;
	}

	std::string getParamName() {
		if (!module)
			return "";
		if (id >= module->mapLen)
			return "";
		ParamHandle* paramHandle = &module->paramHandles[id];
		if (paramHandle->moduleId < 0)
			return "";
		ModuleWidget *mw = APP->scene->rack->getModule(paramHandle->moduleId);
		if (!mw)
			return "";
		// Get the Module from the ModuleWidget instead of the ParamHandle.
		// I think this is more elegant since this method is called in the app world instead of the engine world.
		Module* m = mw->module;
		if (!m)
			return "";
		int paramId = paramHandle->paramId;
		if (paramId >= (int) m->params.size())
			return "";
		ParamQuantity* paramQuantity = m->paramQuantities[paramId];
		std::string s;
		s += mw->model->name;
		s += " ";
		s += paramQuantity->name;
		return s;
	}

	void drawLayer(const DrawArgs& args, int layer) override {
		if (layer == 1) {
			if (bgColor.a > 0.0) {
				nvgScissor(args.vg, RECT_ARGS(args.clipBox));
				nvgBeginPath(args.vg);
				nvgRect(args.vg, 0, 0, box.size.x, box.size.y);
				nvgFillColor(args.vg, bgColor);
				nvgFill(args.vg);
				nvgResetScissor(args.vg);
			}

			std::shared_ptr<window::Font> font = APP->window->loadFont(fontPath);
			if (font && font->handle >= 0) {
				Rect r = Rect(textOffset.x, 0.f, box.size.x - textOffset.x * 2, box.size.y).intersect(args.clipBox);
				nvgScissor(args.vg, RECT_ARGS(r));
				nvgFillColor(args.vg, color);
				nvgFontFaceId(args.vg, font->handle);
				nvgTextLetterSpacing(args.vg, 0.0);
				nvgFontSize(args.vg, 12);
				nvgText(args.vg, textOffset.x, textOffset.y, text.c_str(), NULL);
				nvgResetScissor(args.vg);
			}
		}
	}
};

template< int MAX_CHANNELS, typename MODULE, typename CHOICE = MapModuleChoice<MAX_CHANNELS, MODULE> >
struct MapModuleDisplay : LedDisplay {
	MODULE* module;
	ScrollWidget* scroll;
	CHOICE* choices[MAX_CHANNELS];
	LedDisplaySeparator *separators[MAX_CHANNELS];

	~MapModuleDisplay() {
		for (int id = 0; id < MAX_CHANNELS; id++) {
			choices[id]->processEvents = false;
		}
	}

	void setModule(MODULE* module) {
		this->module = module;

		scroll = new ScrollWidget;
		scroll->box.size.x = box.size.x;
		scroll->box.size.y = box.size.y - scroll->box.pos.y;
		addChild(scroll);

		LedDisplaySeparator* separator = createWidget<LedDisplaySeparator>(scroll->box.pos);
		separator->box.size.x = box.size.x;
		addChild(separator);
		separators[0] = separator;

		Vec pos;
		for (int id = 0; id < MAX_CHANNELS; id++) {
			if (id > 0) {
				LedDisplaySeparator* separator = createWidget<LedDisplaySeparator>(pos);
				separator->box.size.x = box.size.x;
				scroll->container->addChild(separator);
				separators[id] = separator;
			}

			CHOICE* choice = createWidget<CHOICE>(pos);
			choice->box.size.x = box.size.x;
			choice->id = id;
			choice->setModule(module);
			scroll->container->addChild(choice);
			choices[id] = choice;

			pos = choice->box.getBottomLeft();
		}
	}

	void draw(const DrawArgs& args) override {
		LedDisplay::draw(args);
		if (module && module->locked) {
			float stroke = 2.f;
			nvgBeginPath(args.vg);
			nvgRoundedRect(args.vg, stroke / 2, stroke / 2, box.size.x - stroke, box.size.y - stroke, 5.0);
			nvgStrokeWidth(args.vg, stroke);
			nvgStrokeColor(args.vg, color::mult(color::WHITE, 0.5f));
			nvgStroke(args.vg);
		}
	}

	void onHoverScroll(const event::HoverScroll& e) override {
		if (module && module->locked) {
			e.stopPropagating();
		}
		LedDisplay::onHoverScroll(e);
	}
};


template<typename SCALE = ScaledMapParam<float>>
struct MapSlewSlider : ui::Slider {
	struct SlewQuantity : Quantity {
		const float SLEW_MIN = 0.f;
		const float SLEW_MAX = 5.f;
		SCALE* p;
		void setValue(float value) override {
			value = clamp(value, SLEW_MIN, SLEW_MAX);
			p->setSlew(value);
		}
		float getValue() override {
			return p->getSlew();
		}
		float getDefaultValue() override {
			return 0.f;
		}
		std::string getLabel() override {
			return "Slew-limiting";
		}
		int getDisplayPrecision() override {
			return 2;
		}
		float getMaxValue() override {
			return SLEW_MAX;
		}
		float getMinValue() override {
			return SLEW_MIN;
		}
	}; // struct SlewQuantity

	MapSlewSlider(SCALE* p) {
		box.size.x = 220.0f;
		quantity = construct<SlewQuantity>(&SlewQuantity::p, p);
	}
	~MapSlewSlider() {
		delete quantity;
	}
}; // struct MapSlewSlider


template<typename SCALE = ScaledMapParam<float>>
struct MapScalingInputLabel : MenuLabelEx {
	SCALE* p;
	void step() override {
		float min = std::min(p->getMin(), p->getMax());
		float max = std::max(p->getMin(), p->getMax());

		float g1 = rescale(0.f, min, max, p->limitMin, p->limitMax);
		g1 = clamp(g1, p->limitMin, p->limitMax);
		float g2 = rescale(1.f, min, max, p->limitMin, p->limitMax);
		g2 = clamp(g2, p->limitMin, p->limitMax);

		rightText = string::f("[%.1f%, %.1f%]", g1 * 100.f, g2 * 100.f);
	}
}; // struct MapScalingInputLabel

template<typename SCALE = ScaledMapParam<float>>
struct MapScalingOutputLabel : MenuLabelEx {
	SCALE* p;
	void step() override {
		float min = p->getMin();
		float max = p->getMax();

		float f1 = rescale(p->limitMin, p->limitMin, p->limitMax, min, max);
		f1 = clamp(f1, 0.f, 1.f) * 100.f;
		float f2 = rescale(p->limitMax, p->limitMin, p->limitMax, min, max);
		f2 = clamp(f2, 0.f, 1.f) * 100.f;

		rightText = string::f("[%.1f%, %.1f%]", f1, f2);
	}
}; // struct MapScalingOutputLabel

template<typename SCALE = ScaledMapParam<float>>
struct MapScalingOutputLabelUnit : MenuLabelEx {
	SCALE* p;
	void step() override {
		float min = p->getMin();
		float max = p->getMax();

		float f1 = rescale(p->limitMin, p->limitMin, p->limitMax, min, max);
		f1 = clamp(f1, 0.f, 1.f);
		float f2 = rescale(p->limitMax, p->limitMin, p->limitMax, min, max);
		f2 = clamp(f2, 0.f, 1.f);

		ParamQuantity* pq = p->paramQuantity;
		min = rescale(f1, 0.f, 1.f, pq->getMinValue(), pq->getMaxValue());
		max = rescale(f2, 0.f, 1.f, pq->getMinValue(), pq->getMaxValue());

		rightText = string::f("[%.1fV, %.1fV]", min, max);
	}
}; // struct MapScalingOutputLabelUnit


template<typename SCALE = ScaledMapParam<float>>
struct MapMinSlider : SubMenuSlider {
	struct MinQuantity : Quantity {
		SCALE* p;
		void setValue(float value) override {
			value = clamp(value, -1.f, 2.f);
			p->setMin(value);
		}
		float getValue() override {
			return p->getMin();
		}
		float getDefaultValue() override {
			return 0.f;
		}
		float getMinValue() override {
			return -1.f;
		}
		float getMaxValue() override {
			return 2.f;
		}
		float getDisplayValue() override {
			return getValue() * 100;
		}
		void setDisplayValue(float displayValue) override {
			setValue(displayValue / 100);
		}
		std::string getLabel() override {
			return "Low";
		}
		std::string getUnit() override {
			return "%";
		}
		int getDisplayPrecision() override {
			return 3;
		}
	}; // struct MinQuantity

	MapMinSlider(SCALE* p) {
		box.size.x = 220.0f;
		quantity = construct<MinQuantity>(&MinQuantity::p, p);
	}
	~MapMinSlider() {
		delete quantity;
	}
}; // struct MapMinSlider


template<typename SCALE = ScaledMapParam<float>>
struct MapMaxSlider : SubMenuSlider {
	struct MaxQuantity : Quantity {
		SCALE* p;
		void setValue(float value) override {
			value = clamp(value, -1.f, 2.f);
			p->setMax(value);
		}
		float getValue() override {
			return p->getMax();
		}
		float getDefaultValue() override {
			return 1.f;
		}
		float getMinValue() override {
			return -1.f;
		}
		float getMaxValue() override {
			return 2.f;
		}
		float getDisplayValue() override {
			return getValue() * 100;
		}
		void setDisplayValue(float displayValue) override {
			setValue(displayValue / 100);
		}
		std::string getLabel() override {
			return "High";
		}
		std::string getUnit() override {
			return "%";
		}
		int getDisplayPrecision() override {
			return 3;
		}
	}; // struct MaxQuantity

	MapMaxSlider(SCALE* p) {
		box.size.x = 220.0f;
		quantity = construct<MaxQuantity>(&MaxQuantity::p, p);
	}
	~MapMaxSlider() {
		delete quantity;
	}
}; // struct MapMaxSlider


template<typename SCALE = ScaledMapParam<float>>
struct MapPresetMenuItem : MenuItem {
	SCALE* p;
	MapPresetMenuItem() {
		rightText = RIGHT_ARROW;
	}

	Menu* createChildMenu() override {
		struct PresetItem : MenuItem {
			SCALE* p;
			float min, max;
			void onAction(const event::Action& e) override {
				p->setMin(min);
				p->setMax(max);
			}
		};

		Menu* menu = new Menu;
		menu->addChild(construct<PresetItem>(&MenuItem::text, "Default", &PresetItem::p, p, &PresetItem::min, 0.f, &PresetItem::max, 1.f));
		menu->addChild(construct<PresetItem>(&MenuItem::text, "Inverted", &PresetItem::p, p, &PresetItem::min, 1.f, &PresetItem::max, 0.f));
		return menu;
	}
}; // struct MapPresetMenuItem

} // namespace StoermelderPackOne