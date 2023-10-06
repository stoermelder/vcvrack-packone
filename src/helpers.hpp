#pragma once
#include "plugin.hpp"
#include "settings.hpp"

namespace StoermelderPackOne {
namespace Rack {

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
		bool showRightText;
		bool alwaysConsume;

		void step() override {
			TEnum currIndex = getter();
			if (showRightText) {
				if (this->currIndex != currIndex) {
					std::string label = labels[currIndex];
					this->rightText = label + "  " + RIGHT_ARROW;
					this->currIndex = currIndex;
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


} // namespace Rack
} // namespace StoermelderPackOne