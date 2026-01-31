#pragma once
#include <rack.hpp>
#include <settings.hpp>
#include <vector>
#include "components/MenuColorLabel.hpp"
#include "components/MenuColorPicker.hpp"
#include "components/MenuColorField.hpp"

namespace StoermelderPackOne {
namespace Rack {

using namespace rack;

/** Creates a MenuItem that when hovered, opens a submenu with several MenuItems identified by a map.
Example:
	menu->addChild(createMapSubmenuItem<QUALITY>("Mode",
		{
			{ QUALITY::HIFI, "Hi-fi" },
			{ QUALITY::MIDFI, "Mid-fi" },
			{ QUALITY::LOFI, "Lo-fi" }
		},
		[=]() {
			return module->getMode();
		},
		[=](QUALITY mode) {
			module->setMode(mode);
		}
	));
*/
template <typename TEnum, class TMenuItem = ui::MenuItem>
ui::MenuItem* createMapSubmenuItem(std::string text, std::map<TEnum, std::string> labels, std::map<TEnum, std::string> labelsPlugin, std::function<TEnum()> getter, std::function<void(TEnum val)> setter, bool showRightText = true, bool disabled = false, bool alwaysConsume = false) {
	struct IndexItem : ui::MenuItem {
		std::function<TEnum()> getter;
		std::function<void(TEnum)> setter;
		TEnum index;
		bool alwaysConsume;

		void step() override {
			TEnum currIndex = getter();
			this->rightText = CHECKMARK(currIndex == index);
			MenuItem::step();
		}
		void onAction(const event::Action& e) override {
			setter(index);
			if (alwaysConsume)
				e.consume(this);
		}
	};

	struct Item : TMenuItem {
		std::function<TEnum()> getter;
		std::function<void(TEnum)> setter;
		std::map<TEnum, std::string> labels;
		TEnum currIndex;
		bool currIndexInitialized = false;
		bool showRightText;
		bool alwaysConsume;

		void step() override {
			TEnum currIndex = getter();
			if (showRightText) {
				if (this->currIndex != currIndex || !this->currIndexInitialized) {
					std::string label = labels[currIndex];
					this->rightText = label + "  " + RIGHT_ARROW;
					this->currIndex = currIndex;
					this->currIndexInitialized = true;
				}
			}
			else {
				this->rightText = RIGHT_ARROW;
			}
			TMenuItem::step();
		}
		ui::Menu* createChildMenu() override {
			ui::Menu* menu = new ui::Menu;
			for (const auto& i : labels) {
				IndexItem* item = createMenuItem<IndexItem>(i.second);
				item->getter = getter;
				item->setter = setter;
				item->index = i.first;
				item->alwaysConsume = alwaysConsume;
				menu->addChild(item);
			}
			return menu;
		}
	};

	Item* item = createMenuItem<Item>(text);
	item->getter = getter;
	item->setter = setter;
	item->labels = settings::isPlugin ? labelsPlugin : labels;
	item->showRightText = showRightText;
	item->disabled = disabled;
	item->alwaysConsume = alwaysConsume;
	return item;
}

/** Easy wrapper that controls a mapped label at a pointer address.
Example:
	menu->addChild(createMapSubmenuItem("Mode",
		{
			{ QUALITY::HIFI, "Hi-fi" },
			{ QUALITY::MIDFI, "Mid-fi" },
			{ QUALITY::LOFI, "Lo-fi" }
		},
		[]() {  return module->mode; },
		[=](QUALITY mode) {  module->mode = mode; },
	));
*/
template <typename TEnum, class TMenuItem = ui::MenuItem>
ui::MenuItem* createMapSubmenuItem(std::string text, std::map<TEnum, std::string> labels, std::function<TEnum()> getter, std::function<void(TEnum val)> setter, bool showRightText = true, bool disabled = false, bool alwaysConsume = false) {
	return createMapSubmenuItem(text, labels, labels, getter, setter, showRightText, disabled, alwaysConsume);
}


/** Easy wrapper for createMapPtrSubmenuItem() that controls a mapped label at a pointer address.
Example:
	menu->addChild(createMapPtrSubmenuItem("Mode",
		{
			{ QUALITY::HIFI, "Hi-fi" },
			{ QUALITY::MIDFI, "Mid-fi" },
			{ QUALITY::LOFI, "Lo-fi" }
		},
		&module->mode
	));
*/
template <typename TEnum>
ui::MenuItem* createMapPtrSubmenuItem(std::string text, std::map<TEnum, std::string> labels, TEnum* ptr, bool showRightText = true) {
	return createMapSubmenuItem<TEnum>(text, labels,
		[=]() { return *ptr; },
		[=](TEnum index) { *ptr = TEnum(index); },
		showRightText
	);
}

/** Easy wrapper for createMenuItem() to modify a property with a specific value.
Example:
	menu->addChild(createValuePtrMenuItem("Loop", &module->mode, MODE::LOOP));
*/
template <typename T>
ui::MenuItem* createValuePtrMenuItem(std::string text, T* ptr, T val) {
	return createMenuItem(text, CHECKMARK(*ptr == val), [=]() { *ptr = val; });
}

/** Easy wrapper for createMenuItem() to modify a property with a specific value.
Example:
	menu->addChild(createValuePtrMenuItem("Loop", RACK_MOD_SHIFT_NAME "+L", &module->mode, MODE::LOOP));
*/
template <typename T>
ui::MenuItem* createValuePtrMenuItem(std::string text, std::string rightText, T* ptr, T val) {
	return createMenuItem(text, string::f("%s %s", rightText, CHECKMARK(*ptr == val)), [=]() { *ptr = val; });
}


/** Append color controls (label, optional picker, presets and hex field) to an existing menu.
 *  This allows callers to insert the color controls directly into an existing Menu rather than creating a submenu.
 *
 *  Example:
 *    // Append the color controls directly into an existing menu (no extra submenu)
 *    std::vector<std::pair<NVGcolor, std::string>> presets = {
 *      { LABEL_COLOR_YELLOW, "Yellow" },
 *      { LABEL_COLOR_RED, "Red" },
 *      { LABEL_COLOR_CYAN, "Cyan" }
 *    };
 *    // Insert a color picker, presets and a hex field into 'menu' for 'module->slotColor[id]'
 *    Rack::appendColorSubmenuItems(menu, &module->slotColor[id], presets, true, true);
 */
inline void appendColorSubmenuItems(ui::Menu* menu, NVGcolor* colorPtr, const std::vector<std::pair<NVGcolor, std::string>>& presets = {}, bool includePicker = true, bool includeField = false, bool* textSelected = nullptr) {
	struct AppendColorItem : ui::MenuItem {
		NVGcolor color;
		NVGcolor* colorPtr;
		void onAction(const event::Action& e) override {
			*colorPtr = color;
			e.unconsume();
		}
		void step() override {
			rightText = color::toHexString(*colorPtr) == color::toHexString(color) ? "✔" : "";
			MenuItem::step();
		}
	};

	menu->addChild(construct<MenuColorLabel>(&MenuColorLabel::fillColor, colorPtr));
	if (includePicker) {
		menu->addChild(new MenuSeparator);
		menu->addChild(construct<MenuColorPicker>(&MenuColorPicker::color, colorPtr));
	}
	if (!presets.empty()) {
		menu->addChild(new MenuSeparator);
		for (auto &p : presets) {
			menu->addChild(construct<AppendColorItem>(&MenuItem::text, p.second.c_str(), &AppendColorItem::colorPtr, colorPtr, &AppendColorItem::color, p.first));
		}
	}
	if (includeField) {
		menu->addChild(construct<MenuColorField>(&MenuColorField::color, colorPtr, &MenuColorField::textSelected, textSelected));
	}
}

/** Create a color submenu for a color pointer with presets, a picker and a hex field.
Example:
	menu->addChild(createColorSubmenuItem("Color", &module->defaultColor, {
		{ LABEL_COLOR_YELLOW, "Yellow" },
		{ LABEL_COLOR_RED, "Red" },
		{ LABEL_COLOR_CYAN, "Cyan" }
	}));
*/
inline ui::MenuItem* createColorSubmenuItem(std::string text, NVGcolor* colorPtr, std::vector<std::pair<NVGcolor, std::string>> presets = {}, bool includePicker = true, bool includeField = false, bool* textSelected = nullptr) {
	struct Item : ui::MenuItem {
		NVGcolor* colorPtr;
		std::vector<std::pair<NVGcolor, std::string>> presets;
		bool includePicker;
		bool includeField;
		bool* textSelected;
		ui::Menu* createChildMenu() override {
			ui::Menu* menu = new ui::Menu;
			appendColorSubmenuItems(menu, colorPtr, presets, includePicker, includeField, textSelected);
			return menu;
		}
	};

	Item* item = createMenuItem<Item>(text);
	item->colorPtr = colorPtr;
	item->presets = presets;
	item->includePicker = includePicker;
	item->includeField = includeField;
	item->textSelected = textSelected;
	item->rightText = RIGHT_ARROW;
	return item;
}


/** Easy wrapper for creating a slider that controls a float value via getter/setter functions.
Example:
	menu->addChild(createSlider(
		[]() { return module->outsideAlpha; },
		[=](float v) { module->outsideAlpha = v; },
		0.f, 1.f, 0.5f, "Opacity", "%", 100.f
	));
*/
template<typename BASE = ui::Slider>
inline BASE* createSliderT(std::function<float()> getter, std::function<void(float)> setter, float minValue = 0.f, float maxValue = 1.f, float defaultValue = 0.5f, std::string label = "Value", std::string unit = "", float displayMultiplier = 1.f, float width = 200.0f) {
	struct SliderQuantity : Quantity {
		std::function<float()> get;
		std::function<void(float)> set;
		float minVal;
		float maxVal;
		float defaultVal;
		std::string lbl;
		std::string unt;
		float multiplier;

		SliderQuantity(std::function<float()> g, std::function<void(float)> s, float minV, float maxV, float d, std::string l, std::string u, float m) : get(g), set(s), minVal(minV), maxVal(maxV), defaultVal(d), lbl(l), unt(u), multiplier(m) {}
		void setValue(float value) override {
			set(math::clamp(value, minVal, maxVal));
		}
		float getValue() override {
			return get();
		}
		float getDefaultValue() override {
			return defaultVal;
		}
		float getDisplayValue() override {
			return getValue() * multiplier;
		}
		void setDisplayValue(float displayValue) override {
			setValue(displayValue / multiplier);
		}
		std::string getLabel() override {
			return lbl;
		}
		std::string getUnit() override {
			return unt;
		}
		float getMinValue() override {
			return minVal;
		}
		float getMaxValue() override {
			return maxVal;
		}
	};

	struct SliderWithQuantity : BASE {
		SliderWithQuantity(std::function<float()> g, std::function<void(float)> s, float minV, float maxV, float d, std::string l, std::string u, float m, float w) {
			this->box.size.x = w;
			this->quantity = new SliderQuantity(g, s, minV, maxV, d, l, u, m);
		}
		~SliderWithQuantity() {
			delete this->quantity;
		}
	};

	return new SliderWithQuantity(getter, setter, minValue, maxValue, defaultValue, label, unit, displayMultiplier, width);
}

inline ui::Slider* createSlider(std::function<float()> getter, std::function<void(float)> setter, float minValue = 0.f, float maxValue = 1.f, float defaultValue = 0.5f, std::string label = "Value", std::string unit = "", float displayMultiplier = 1.f, float width = 200.0f) {
	return createSliderT<>(getter, setter, minValue, maxValue, defaultValue, label, unit, displayMultiplier, width);
}


/** Easy wrapper for creating a float slider that controls a float pointer.
Example:
	menu->addChild(createPtrSlider(&module->value, 0.f, 1.f, 0.5f, "Opacity", "%", 100.f));
*/
template<typename BASE = ui::Slider>
inline BASE* createPtrSliderT(float* valuePtr, float minValue = 0.f, float maxValue = 1.f, float defaultValue = 0.5f, std::string label = "Value", std::string unit = "", float displayMultiplier = 1.f, float width = 200.0f) {
	return createSliderT<BASE>(
		[=]() { return *valuePtr; },
		[=](float v) { *valuePtr = v; },
		minValue, maxValue, defaultValue, label, unit, displayMultiplier, width
	);
}

inline ui::Slider* createPtrSlider(float* valuePtr, float minValue = 0.f, float maxValue = 1.f, float defaultValue = 0.5f, std::string label = "Value", std::string unit = "", float displayMultiplier = 1.f, float width = 200.0f) {
	return createPtrSliderT<>(valuePtr, minValue, maxValue, defaultValue, label, unit, displayMultiplier, width);
}

/** Helper to create a pre-configured TextField for menus or overlays.
Example:
	// inside a menu lambda
	auto* tf = createTextField(module->sim->udpAddress(), 200.0f);
	m->addChild(tf);
	// retrieve value later via tf->text
*/
inline ui::TextField* createTextField(const std::string& initial = "", const std::string& placeHolder = "", float width = 120.0f) {
	ui::TextField* tf = new ui::TextField();
	tf->box.size.x = width;
	tf->text = initial;
	tf->placeholder = placeHolder;
	return tf;
}

} // namespace Rack
} // namespace StoermelderPackOne