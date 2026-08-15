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

/**
 * @brief Creates a MenuItem that when hovered, opens a submenu with several MenuItems identified by a vector (preserves insertion order).
 *
 * @param text The label text for the menu item
 * @param labels Vector of enum values to label strings for standard mode (insertion order preserved)
 * @param labelsPlugin Vector of enum values to label strings for plugin mode
 * @param getter Function returning the current selected enum value
 * @param setter Function called when an item is selected with the new enum value
 * @param showRightText Whether to show the current selection on the parent item (default: true)
 * @param disabled Whether the menu item is disabled (default: false)
 * @param alwaysConsume Whether to always consume the action event (default: false)
 * @return ui::MenuItem* The created menu item
 *
 * Example:
 *   menu->addChild(createMapSubmenuItem<QUALITY>("Mode",
 *       {
 *           { QUALITY::HIFI, "Hi-fi" },
 *           { QUALITY::MIDFI, "Mid-fi" },
 *           { QUALITY::LOFI, "Lo-fi" }
 *       },
 *       [=]() {
 *           return module->getMode();
 *       },
 *       [=](QUALITY mode) {
 *           module->setMode(mode);
 *       }
 *   ));
 */
template <typename TEnum, class TMenuItem = ui::MenuItem>
ui::MenuItem* createMapSubmenuItem(std::string text, std::vector<std::pair<TEnum, std::string>> labels, std::vector<std::pair<TEnum, std::string>> labelsPlugin, std::function<TEnum()> getter, std::function<void(TEnum val)> setter, bool showRightText = true, bool disabled = false, bool alwaysConsume = false) {
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
		std::vector<std::pair<TEnum, std::string>> labels;
		TEnum currIndex;
		bool currIndexInitialized = false;
		bool showRightText;
		bool alwaysConsume;

		void step() override {
			TEnum currIndex = getter();
			if (showRightText) {
				if (this->currIndex != currIndex || !this->currIndexInitialized) {
					std::string label;
					for (const auto& l : labels) {
						if (l.first == currIndex) {
							label = l.second;
							break;
						}
					}
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

/**
 * @brief Easy wrapper that controls a mapped label using getter/setter functions.
 *
 * @param text The label text for the menu item
 * @param labels Vector of enum values to label strings (insertion order preserved)
 * @param getter Function returning the current selected enum value
 * @param setter Function called when an item is selected with the new enum value
 * @param showRightText Whether to show the current selection on the parent item (default: true)
 * @param disabled Whether the menu item is disabled (default: false)
 * @param alwaysConsume Whether to always consume the action event (default: false)
 * @return ui::MenuItem* The created menu item
 *
 * Example:
 *   menu->addChild(createMapSubmenuItem("Mode",
 *       {
 *           { QUALITY::HIFI, "Hi-fi" },
 *           { QUALITY::MIDFI, "Mid-fi" },
 *           { QUALITY::LOFI, "Lo-fi" }
 *       },
 *       []() {  return module->mode; },
 *       [=](QUALITY mode) {  module->mode = mode; },
 *   ));
 */
template <typename TEnum, class TMenuItem = ui::MenuItem>
ui::MenuItem* createMapSubmenuItem(std::string text, std::vector<std::pair<TEnum, std::string>> labels, std::function<TEnum()> getter, std::function<void(TEnum val)> setter, bool showRightText = true, bool disabled = false, bool alwaysConsume = false) {
	return createMapSubmenuItem(text, labels, labels, getter, setter, showRightText, disabled, alwaysConsume);
}


/**
 * @brief Easy wrapper for createMapSubmenuItem() that controls a mapped label at a pointer address.
 *
 * @param text The label text for the menu item
 * @param labels Vector of enum values to label strings (insertion order preserved)
 * @param ptr Pointer to the enum value to control
 * @param showRightText Whether to show the current selection on the parent item (default: true)
 * @return ui::MenuItem* The created menu item
 *
 * Example:
 *   menu->addChild(createMapPtrSubmenuItem("Mode",
 *       {
 *           { QUALITY::HIFI, "Hi-fi" },
 *           { QUALITY::MIDFI, "Mid-fi" },
 *           { QUALITY::LOFI, "Lo-fi" }
 *       },
 *       &module->mode
 *   ));
 */
template <typename TEnum>
ui::MenuItem* createMapPtrSubmenuItem(std::string text, const std::vector<std::pair<TEnum, std::string>>& labels, TEnum* ptr, bool showRightText = true) {
	return createMapSubmenuItem<TEnum>(text, labels,
		[=]() { return *ptr; },
		[=](TEnum index) { *ptr = TEnum(index); },
		showRightText
	);
}

/**
 * @brief Easy wrapper for createMenuItem() to modify a property with a specific value.
 *
 * @param text The label text for the menu item
 * @param ptr Pointer to the value to modify
 * @param val The value to set when the menu item is clicked
 * @return ui::MenuItem* The created menu item
 *
 * Example:
 *   menu->addChild(createValuePtrMenuItem("Loop", &module->mode, MODE::LOOP));
 */
template <typename T>
ui::MenuItem* createValuePtrMenuItem(std::string text, T* ptr, T val) {
	return createMenuItem(text, CHECKMARK(*ptr == val), [=]() { *ptr = val; });
}

/**
 * @brief Easy wrapper for createMenuItem() to modify a property with a specific value and custom right text.
 *
 * @param text The label text for the menu item
 * @param rightText The text to display on the right side of the menu item
 * @param ptr Pointer to the value to modify
 * @param val The value to set when the menu item is clicked
 * @return ui::MenuItem* The created menu item
 *
 * Example:
 *   menu->addChild(createValuePtrMenuItem("Loop", RACK_MOD_SHIFT_NAME "+L", &module->mode, MODE::LOOP));
 */
template <typename T>
ui::MenuItem* createValuePtrMenuItem(std::string text, std::string rightText, T* ptr, T val) {
	return createMenuItem(text, string::f("%s %s", rightText, CHECKMARK(*ptr == val)), [=]() { *ptr = val; });
}

/**
 * @brief Like createValuePtrMenuItem() but for std::atomic<T> values.
 *
 * The checkmark reflects whether the current atomic value equals val; clicking the
 * item stores val into the atomic. Loads and stores use std::memory_order_seq_cst
 * (the default for std::atomic), which is the safest option for a cross-thread
 * UI/engine field.
 *
 * @param text The label text for the menu item
 * @param ptr Pointer to the std::atomic<T> value to modify
 * @param val The value to set when the menu item is clicked
 * @return ui::MenuItem* The created menu item
 *
 * Example:
 *   menu->addChild(createAtomicValuePtrMenuItem("Off", &module->setCvMode, SETCVMODE::OFF));
 *   menu->addChild(createAtomicValuePtrMenuItem("Trigger", &module->snapshotsUsed, 1));
 */
template <typename T>
ui::MenuItem* createAtomicValuePtrMenuItem(std::string text, std::atomic<T>* ptr, T val) {
	T _v = ptr->load(std::memory_order_relaxed);
	return createMenuItem(text, CHECKMARK(_v == val), [=]() { ptr->store(val, std::memory_order_relaxed); });
}


/**
 * @brief Append color controls directly to an existing menu.
 *
 * @param menu The menu to append controls to
 * @param colorPtr Pointer to the color value to control
 * @param presets Vector of color presets (default: empty)
 * @param includePicker Whether to include a color picker (default: true)
 * @param includeField Whether to include a hex field (default: false)
 * @param textSelected Optional pointer to track text field selection state (default: nullptr)
 *
 * Example:
 *   std::vector<std::pair<NVGcolor, std::string>> presets = {
 *       { LABEL_COLOR_YELLOW, "Yellow" },
 *       { LABEL_COLOR_RED, "Red" },
 *       { LABEL_COLOR_CYAN, "Cyan" }
 *   };
 *   Rack::appendColorSubmenuItems(menu, &module->slotColor[id], presets, true, true);
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

/**
 * @brief Create a color submenu item with presets, a picker and a hex field.
 *
 * @param text The label text for the menu item
 * @param colorPtr Pointer to the color value to control
 * @param presets Vector of color presets (default: empty)
 * @param includePicker Whether to include a color picker (default: true)
 * @param includeField Whether to include a hex field (default: false)
 * @param textSelected Optional pointer to track text field selection state (default: nullptr)
 * @return ui::MenuItem* The created menu item
 *
 * Example:
 *   menu->addChild(createColorSubmenuItem("Color", &module->defaultColor, {
 *       { LABEL_COLOR_YELLOW, "Yellow" },
 *       { LABEL_COLOR_RED, "Red" },
 *       { LABEL_COLOR_CYAN, "Cyan" }
 *   }));
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


/**
 * @brief Easy wrapper for creating a slider with custom getter/setter functions.
 *
 * @param getter Function returning the current float value
 * @param setter Function called when the slider value changes
 * @param minValue Minimum slider value (default: 0.0)
 * @param maxValue Maximum slider value (default: 1.0)
 * @param defaultValue Default slider value (default: 0.5)
 * @param label Label text for the slider (default: "Value")
 * @param unit Unit string displayed after the value (default: "")
 * @param displayMultiplier Multiplier for display values (default: 1.0)
 * @param width Slider width in pixels (default: 200.0)
 * @return ui::Slider* The created slider widget
 *
 * Example:
 *   menu->addChild(createSlider(
 *       []() { return module->outsideAlpha; },
 *       [=](float v) { module->outsideAlpha = v; },
 *       0.f, 1.f, 0.5f, "Opacity", "%", 100.f
 *   ));
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

/**
 * @brief Easy wrapper for creating a slider with custom getter/setter functions.
 *
 * @param getter Function returning the current float value
 * @param setter Function called when the slider value changes
 * @param minValue Minimum slider value (default: 0.0)
 * @param maxValue Maximum slider value (default: 1.0)
 * @param defaultValue Default slider value (default: 0.5)
 * @param label Label text for the slider (default: "Value")
 * @param unit Unit string displayed after the value (default: "")
 * @param displayMultiplier Multiplier for display values (default: 1.0)
 * @param width Slider width in pixels (default: 200.0)
 * @return ui::Slider* The created slider widget
 */
inline ui::Slider* createSlider(std::function<float()> getter, std::function<void(float)> setter, float minValue = 0.f, float maxValue = 1.f, float defaultValue = 0.5f, std::string label = "Value", std::string unit = "", float displayMultiplier = 1.f, float width = 200.0f) {
	return createSliderT<>(getter, setter, minValue, maxValue, defaultValue, label, unit, displayMultiplier, width);
}


/**
 * @brief Easy wrapper for creating a slider that controls a float pointer.
 *
 * @param valuePtr Pointer to the float value to control
 * @param minValue Minimum slider value (default: 0.0)
 * @param maxValue Maximum slider value (default: 1.0)
 * @param defaultValue Default slider value (default: 0.5)
 * @param label Label text for the slider (default: "Value")
 * @param unit Unit string displayed after the value (default: "")
 * @param displayMultiplier Multiplier for display values (default: 1.0)
 * @param width Slider width in pixels (default: 200.0)
 * @return ui::Slider* The created slider widget
 *
 * Example:
 *   menu->addChild(createPtrSlider(&module->value, 0.f, 1.f, 0.5f, "Opacity", "%", 100.f));
 */
template<typename BASE = ui::Slider>
inline BASE* createPtrSliderT(float* valuePtr, float minValue = 0.f, float maxValue = 1.f, float defaultValue = 0.5f, std::string label = "Value", std::string unit = "", float displayMultiplier = 1.f, float width = 200.0f) {
	return createSliderT<BASE>(
		[=]() { return *valuePtr; },
		[=](float v) { *valuePtr = v; },
		minValue, maxValue, defaultValue, label, unit, displayMultiplier, width
	);
}

/**
 * @brief Easy wrapper for creating a slider that controls a float pointer.
 *
 * @param valuePtr Pointer to the float value to control
 * @param minValue Minimum slider value (default: 0.0)
 * @param maxValue Maximum slider value (default: 1.0)
 * @param defaultValue Default slider value (default: 0.5)
 * @param label Label text for the slider (default: "Value")
 * @param unit Unit string displayed after the value (default: "")
 * @param displayMultiplier Multiplier for display values (default: 1.0)
 * @param width Slider width in pixels (default: 200.0)
 * @return ui::Slider* The created slider widget
 */
inline ui::Slider* createPtrSlider(float* valuePtr, float minValue = 0.f, float maxValue = 1.f, float defaultValue = 0.5f, std::string label = "Value", std::string unit = "", float displayMultiplier = 1.f, float width = 200.0f) {
	return createPtrSliderT<>(valuePtr, minValue, maxValue, defaultValue, label, unit, displayMultiplier, width);
}


/**
 * @brief Easy wrapper for creating a stepped slider with integer values.
 *
 * @param getter Function returning the current value
 * @param setter Function called when the slider value changes
 * @param minValue Minimum slider value (default: 0.0)
 * @param maxValue Maximum slider value (default: 1.0)
 * @param defaultValue Default slider value (default: 0.5)
 * @param label Label text for the slider (default: "Value")
 * @param unit Unit string displayed after the value (default: "")
 * @param str Optional function to convert value to display string (default: nullptr)
 * @param width Slider width in pixels (default: 140.0)
 * @return ui::Slider* The created slider widget
 *
 * Example:
 *   menu->addChild(createSteppedSlider<uint8_t>(
 *       [=]() { return module->cc; },
 *       [=](uint8_t v) { module->cc = v; },
 *       0, 127, 0,
 *       "CC", "", [=](float v) { return string::f("CC %d", std::round(v)); },
 *       140.f
 *   ));
 */
template<typename T>
inline ui::Slider* createSteppedSlider(std::function<T()> getter, std::function<void(T)> setter, 
		float minValue = 0.f, float maxValue = 1.f, float defaultValue = 0.5f, 
		std::string label = "Value", std::string unit = "", std::function<std::string(T)> str = nullptr,
		float width = 140.f) {
	struct SliderQuantity : Quantity {
		std::function<T()> get;
		std::function<void(T)> set;
		std::function<std::string(T)> str = nullptr;
		float minVal;
		float maxVal;
		float defaultVal;
		std::string lbl;
		std::string unt;
		float value;
	
		SliderQuantity(std::function<T()> g, std::function<void(T)> s, float minV, float maxV, float d, std::string l, std::string u, std::function<std::string(T)> s_)
				: get(g), set(s), str(s_), minVal(minV), maxVal(maxV), defaultVal(d), lbl(l), unt(u) {
			value = float(get());
		}

		void setValue(float value) override {
			this->value = math::clamp(value, minVal, maxVal);
			set(T(std::round(this->value)));
		}
		float getValue() override {
			return value;
		}
		float getDefaultValue() override {
			return defaultVal;
		}
		float getDisplayValue() override {
			return this->value;
		}
		void setDisplayValue(float displayValue) override {
		}
		std::string getDisplayValueString() override {
			return str ? str(T(std::round(value))) : string::f("%.0f", std::round(this->value));
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

	struct SliderWithQuantity : ui::Slider {
		SliderWithQuantity(std::function<T()> g, std::function<void(T)> s, float minV, float maxV, float d, std::string l, std::string u, std::function<std::string(float)> str, float w) {
			box.size.x = w;
			quantity = new SliderQuantity(g, s, minV, maxV, d, l, u, str);
		}
		~SliderWithQuantity() {
			delete quantity;
		}
	};

	return new SliderWithQuantity(getter, setter, minValue, maxValue, defaultValue, label, unit, str, width);
}


/**
 * @brief Helper to create a pre-configured TextField for menus or overlays.
 *
 * @param initial Initial text value (default: empty)
 * @param placeHolder Placeholder text shown when empty (default: empty)
 * @param width TextField width in pixels (default: 120.0)
 * @return ui::TextField* The created text field widget
 *
 * Example:
 *   // inside a menu lambda
 *   auto* tf = createTextField(module->sim->udpAddress(), 200.0f);
 *   m->addChild(tf);
 *   // retrieve value later via tf->text
 */
inline ui::TextField* createTextField(const std::string& initial = "", const std::string& placeHolder = "", float width = 120.0f) {
	ui::TextField* tf = new ui::TextField();
	tf->box.size.x = width;
	tf->text = initial;
	tf->placeholder = placeHolder;
	return tf;
}

/**
 * @brief True when `text` matches a live menu filter string (case-insensitive substring match).
 *
 * A null or empty filter matches everything. Intended for menu items that
 * hide themselves in step() while the user types into a filter text field.
 */
inline bool menuFilterMatches(const std::shared_ptr<std::string>& filter, const std::string& text) {
	if (!filter || filter->empty()) return true;
	return string::lowercase(text).find(string::lowercase(*filter)) != std::string::npos;
}

/**
 * @brief Helper to add menu items from a sorted list with a factory function, grouping them into submenus when exceeding a threshold.
 *
 * @param menu The menu to add items to
 * @param items Sorted vector of items (any type TData)
 * @param creator Factory function: ui::MenuItem* creator(const TData& item)
 * @param directThreshold Maximum number of items before grouping into submenus (default: 24)
 * @param groupSize Number of items per submenu when grouping (default: 16)
 * @param filter Optional live filter string; group submenu items hide themselves when no member's text matches (the items created by `creator` are expected to filter themselves)
 *
 * Example:
 *   std::vector<std::pair<std::string, int>> items = {...};
 *   Rack::addGroupedMenuItems<decltype(items)::value_type>(menu, items, [](const auto& item) -> ui::MenuItem* {
 *       ToggleTagItem* t = new ToggleTagItem;
 *       t->text = item.first;
 *       return t;
 *   });
 */
template<typename TData>
inline void addGroupedMenuItems(
	ui::Menu* menu,
	const std::vector<TData>& items,
	std::function<ui::MenuItem*(const TData&)> creator,
	size_t directThreshold = 24,
	size_t groupSize = 16,
	std::shared_ptr<std::string> filter = nullptr
) {
	if (items.empty()) return;

	if (items.size() <= directThreshold) {
		for (const auto& item : items) {
			menu->addChild(creator(item));
		}
	} else {
		// Group submenu item that hides itself while a live filter is set and
		// none of its member texts match it.
		struct FilteredGroupItem : ui::MenuItem {
			std::function<void(ui::Menu*)> createMenuFn;
			std::vector<std::string> texts;
			std::shared_ptr<std::string> filter;
			ui::Menu* createChildMenu() override {
				ui::Menu* subMenu = new ui::Menu;
				createMenuFn(subMenu);
				return subMenu;
			}
			void step() override {
				visible = std::any_of(texts.begin(), texts.end(), [this](const std::string& t) {
					return menuFilterMatches(filter, t);
				});
				MenuItem::step();
			}
		};

		size_t numGroups = (items.size() + groupSize - 1) / groupSize;
		size_t actualGroupSize = (items.size() + numGroups - 1) / numGroups;

		for (size_t i = 0; i < items.size(); i += actualGroupSize) {
			size_t end = std::min(i + actualGroupSize, items.size());
			// Item texts are only obtainable via creator(); the widgets built
			// here are temporary and deleted after their text is read.
			std::vector<std::string> texts;
			texts.reserve(end - i);
			for (size_t j = i; j < end; j++) {
				ui::MenuItem* tmp = creator(items[j]);
				texts.push_back(tmp->text);
				delete tmp;
			}
			char first = (char)std::toupper((unsigned char)string::lowercase(texts.front())[0]);
			char last = (char)std::toupper((unsigned char)string::lowercase(texts.back())[0]);
			std::string label = first == last
				? std::string(1, first)
				: std::string(1, first) + "-" + std::string(1, last);

			std::vector<TData> group(items.begin() + i, items.begin() + end);
			auto createMenuFn = [creator, group](ui::Menu* subMenu) {
				for (const auto& item : group) {
					subMenu->addChild(creator(item));
				}
			};
			if (filter) {
				FilteredGroupItem* groupItem = new FilteredGroupItem;
				groupItem->text = label;
				groupItem->rightText = RIGHT_ARROW;
				groupItem->createMenuFn = createMenuFn;
				groupItem->texts = std::move(texts);
				groupItem->filter = filter;
				menu->addChild(groupItem);
			}
			else {
				menu->addChild(createSubmenuItem(label, "", createMenuFn));
			}
		}
	}
}


/**
 * @brief Menu subclass that stays open after any item action.
 *
 * Overrides onAction to call e.unconsume(), which prevents the event from
 * reaching MenuOverlay and thus keeps the menu visible after each click.
 */
struct StickyMenu : ui::Menu {
	void onAction(const event::Action& e) override { e.unconsume(); }
};

/**
 * @brief Like createSubmenuItem but the child menu stays open on every click.
 *
 * @param text         Label text for the parent menu item
 * @param rightText    Right-side annotation (e.g. RIGHT_ARROW)
 * @param createMenuFn Callback that populates the child menu
 * @return ui::MenuItem* The created menu item
 *
 * Example:
 *   menu->addChild(createStickySubmenuItem("Options", RIGHT_ARROW, [=](ui::Menu* m) {
 *       m->addChild(createCheckMenuItem(...));
 *   }));
 */
inline ui::MenuItem* createStickySubmenuItem(const std::string& text, const std::string& rightText, std::function<void(ui::Menu*)> createMenuFn) {
	struct Item : ui::MenuItem {
		std::function<void(ui::Menu*)> createMenuFn;
		ui::Menu* createChildMenu() override {
			ui::Menu* menu = new StickyMenu;
			createMenuFn(menu);
			return menu;
		}
	};
	Item* item = new Item;
	item->text        = text;
	item->rightText   = rightText;
	item->createMenuFn = createMenuFn;
	return item;
}

/**
 * @brief Self-refreshing sticky MIDI menu for a midi::Port.
 *
 * Populates driver, device, and channel sections from the port on construction.
 * Calls e.unconsume() on every action so the menu stays open after each click.
 * Detects driver changes in step() and rebuilds the device list automatically.
 *
 * Use createStickyMidiMenuItem() rather than instantiating this directly.
 */
struct StickyMidiMenu : ui::Menu {
	midi::Port* port = nullptr;
	int lastDriverId = INT_MIN;
	bool enableChannelMenu = true;

	void onAction(const event::Action& e) override {
		e.unconsume();
	}

	void populate() {
		clearChildren();
		if (!port) return;
		lastDriverId = port->getDriverId();

		struct DriverItem : ui::MenuItem {
			midi::Port* port; int driverId;
			void step() override { rightText = CHECKMARK(driverId == port->getDriverId()); MenuItem::step(); }
			void onAction(const event::Action& e) override { port->setDriverId(driverId); e.unconsume(); }
		};
		struct DeviceItem : ui::MenuItem {
			midi::Port* port; int deviceId;
			void step() override { rightText = CHECKMARK(deviceId == port->getDeviceId()); MenuItem::step(); }
			void onAction(const event::Action& e) override { port->setDeviceId(deviceId); e.unconsume(); }
		};
		struct ChannelItem : ui::MenuItem {
			midi::Port* port; int channel;
			void step() override { rightText = CHECKMARK(channel == port->getChannel()); MenuItem::step(); }
			void onAction(const event::Action& e) override { port->setChannel(channel); e.unconsume(); }
		};
		struct ChannelSubmenuItem : ui::MenuItem {
			midi::Port* port;
			ui::Menu* createChildMenu() override {
				ui::Menu* menu = new StickyMenu;
				for (int ch : port->getChannels()) {
					ChannelItem* item = new ChannelItem;
					item->port = port; item->channel = ch;
					item->text = port->getChannelName(ch);
					menu->addChild(item);
				}
				return menu;
			}
		};

		addChild(createMenuLabel(string::translate("MidiDisplay.driver")));
		for (int driverId : midi::getDriverIds()) {
			DriverItem* item = new DriverItem;
			item->port = port; item->driverId = driverId;
			item->text = midi::getDriver(driverId)->getName();
			addChild(item);
		}

		addChild(new ui::MenuSeparator);
		addChild(createMenuLabel(string::translate("MidiDisplay.device")));
		{
			DeviceItem* item = new DeviceItem;
			item->port = port; item->deviceId = -1;
			item->text = "(" + string::translate("MidiDisplay.noDevice") + ")";
			addChild(item);
		}
		for (int deviceId : port->getDeviceIds()) {
			DeviceItem* item = new DeviceItem;
			item->port = port; item->deviceId = deviceId;
			item->text = port->getDeviceName(deviceId);
			addChild(item);
		}

		if (enableChannelMenu) {
			addChild(new ui::MenuSeparator);
			ChannelSubmenuItem* channelItem = new ChannelSubmenuItem;
			channelItem->text = string::translate("MidiDisplay.channel");
			channelItem->rightText = RIGHT_ARROW;
			channelItem->port = port;
			addChild(channelItem);
		}
	}

	void step() override {
		if (port && port->getDriverId() != lastDriverId)
			populate();
		ui::Menu::step();
	}
};

/**
 * @brief Creates a submenu item for a midi::Port that stays open on every click.
 *
 * Opens a StickyMidiMenu showing driver, device, and channel selection.
 * The device list refreshes automatically when the selected driver changes.
 *
 * @param text Label text for the menu item
 * @param port The midi::Port to configure
 * @return ui::MenuItem* The created menu item
 *
 * Example:
 *   menu->addChild(createStickyMidiMenuItem("MIDI Input",  &module->midiInput));
 *   menu->addChild(createStickyMidiMenuItem("MIDI Output", &module->midiOutput));
 */
inline ui::MenuItem* createStickyMidiMenuItem(const std::string& text, midi::Port* port, bool enableChannelMenu = true) {
	struct Item : ui::MenuItem {
		midi::Port* port;
		bool enableChannelMenu;
		ui::Menu* createChildMenu() override {
			StickyMidiMenu* menu = new StickyMidiMenu;
			menu->port = port;
			menu->enableChannelMenu = enableChannelMenu;
			menu->populate();
			return menu;
		}
	};
	Item* item = new Item;
	item->text = text;
	item->rightText = RIGHT_ARROW;
	item->port = port;
	item->enableChannelMenu = enableChannelMenu;
	return item;
}

} // namespace Rack
} // namespace StoermelderPackOne
