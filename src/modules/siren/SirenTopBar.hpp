#pragma once
#include <rack.hpp>
#include "SirenBrowserPane.hpp"

namespace StoermelderPackOne {
namespace Siren {


/**
 * @brief Source selection choice button for the Siren top bar
 * 
 * Provides a dropdown button for selecting the active root container
 * and accessing source management options.
 */
static constexpr float TOPBAR_SCALE = 0.6f;

struct SirenSourceButton : ui::ChoiceButton {
	SirenBrowserPane* pane = nullptr;

	void draw(const DrawArgs& args) override {
		BNDwidgetState state = BND_DEFAULT;
		if (APP->event->getHoveredWidget() == this) state = BND_HOVER;
		if (APP->event->getSelectedWidget() == this) state = BND_ACTIVE;
		nvgSave(args.vg);
		nvgScale(args.vg, TOPBAR_SCALE, TOPBAR_SCALE);
		bndChoiceButton(args.vg, 0, 0, box.size.x / TOPBAR_SCALE, box.size.y / TOPBAR_SCALE,
		                BND_CORNER_NONE, state, -1, text.c_str());
		nvgRestore(args.vg);
	}

	void step() override {
		if (!pane->rootContainers.empty() && pane->activeRootIdx >= 0 &&
			pane->activeRootIdx < (int)pane->rootContainers.size()) {
			text = pane->getRootDisplayName(pane->activeRootIdx);
		}
		else {
			text = "No source";
		}
		text = string::ellipsize(text, 40);
		ChoiceButton::step();
	}

	void onAction(const event::Action& e) override {
		ui::Menu* menu = createMenu();
		menu->box.pos   = getAbsoluteOffset(math::Vec(0, box.size.y));
		menu->box.size.x = box.size.x;

		// List of existing sources
		for (int i = 0; i < (int)pane->rootContainers.size(); i++) {
			std::string name = pane->getRootDisplayName(i);
			menu->addChild(createCheckMenuItem(name, "",
				[this, i]() { return i == pane->activeRootIdx; },
				[this, i]() { if (pane->onSelectRoot) pane->onSelectRoot(i); }
			));
		}

		menu->addChild(new ui::MenuSeparator);
		size_t i = menu->children.size();
		if (pane->activeDataSource) {
			pane->activeDataSource->appendSourceMenuItems(menu);
			if (i != menu->children.size()) menu->addChild(new ui::MenuSeparator);
		}
		
		menu->addChild(createMenuItem("Add root...", "", [this]() {
			if (pane->onAddRoot) pane->onAddRoot();
		}));
		menu->addChild(createMenuItem("Remove root", "", [this]() {
			if (!sirenSettings.removeActiveRoot(pane->activeDataSource)) return;
			pane->setRoots(sirenSettings.rootContainers, sirenSettings.activeRootIdx);
		}, pane->rootContainers.empty()));

		menu->addChild(new MenuSeparator);
		menu->addChild(createBoolPtrMenuItem("Resample on playback", "", &sirenSettings.resampleOnPlayback));
		menu->addChild(createBoolPtrMenuItem("Resample on drop", "", &sirenSettings.resampleOnDrop));
		// Speex resampler quality used during "resample on drop".
		menu->addChild(createSubmenuItem("Resample quality", "", [](ui::Menu* qMenu) {
			struct QPreset { int value; std::string label; std::string desc; };
			QPreset presets[] = {
				{ 1,  "Fast",    "Lowest CPU" },
				{ 6,  "Default", "Balanced quality and CPU"      },
				{ 10, "Best",    "Highest CPU"  },
			};
			for (const QPreset& p : presets) {
				qMenu->addChild(createCheckMenuItem(p.label, p.desc,
					[=]() { return sirenSettings.resampleQuality == p.value; },
					[=]() { sirenSettings.resampleQuality = p.value; }
				));
			}
		}));
		menu->addChild(createBoolPtrMenuItem("Convert to WAV on drop", "", &sirenSettings.convertToWavOnDrop));
		menu->addChild(createSubmenuItem("Folder for converted/trimmed files", "", [=](ui::Menu* subMenu) {
			subMenu->addChild(createCheckMenuItem("Same folder as source file", "",
				[=]() { return sirenSettings.customConvertDir.empty(); },
				[=]() { sirenSettings.customConvertDir = ""; }
			));
			subMenu->addChild(createCheckMenuItem(
				sirenSettings.customConvertDir.empty() ? "Custom folder..." : sirenSettings.customConvertDir, "",
				[=]() { return !sirenSettings.customConvertDir.empty(); },
				[]() {
					char* path = osdialog_file(OSDIALOG_OPEN_DIR, nullptr, nullptr, nullptr);
					if (!path) return;
					sirenSettings.customConvertDir = path;
					free(path);
				}
			));
		}));
	}
};


/**
 * @brief Search field widget for the Siren top bar
 * 
 * Provides a text input field for searching within the file browser.
 * Supports escape key to clear the search and trigger a rebuild.
 */
struct SirenSearchField : ui::TextField {
	SirenBrowserPane* pane = nullptr;

	SirenSearchField() {
		placeholder = "Search...";
	}

	void draw(const DrawArgs& args) override {
		BNDwidgetState state;
		if (APP->event->getSelectedWidget() == this) state = BND_ACTIVE;
		else if (APP->event->getHoveredWidget() == this) state = BND_HOVER;
		else state = BND_DEFAULT;
		int begin = std::min(cursor, selection);
		int end   = std::max(cursor, selection);
		bool showPlaceholder = text.empty() && state != BND_ACTIVE;
		const char* displayText = showPlaceholder ? placeholder.c_str() : text.c_str();
		int b = showPlaceholder ? 0 : begin;
		int e = showPlaceholder ? 0 : end;
		nvgSave(args.vg);
		if (showPlaceholder) nvgGlobalAlpha(args.vg, 0.4f);
		nvgScale(args.vg, TOPBAR_SCALE, TOPBAR_SCALE);
		bndTextField(args.vg, 0, 0, box.size.x / TOPBAR_SCALE, box.size.y / TOPBAR_SCALE,
		             BND_CORNER_NONE, state, -1, displayText, b, e);
		nvgRestore(args.vg);
	}

	void onChange(const event::Change& e) override {
		if (pane) {
			pane->searchQuery = rack::string::trim(text);
			pane->requestRebuild();
		}
		ui::TextField::onChange(e);
	}

	void onSelectKey(const event::SelectKey& e) override {
		if (e.action == GLFW_PRESS && e.key == GLFW_KEY_ESCAPE) {
			setText("");
			pane->searchQuery.clear();
			pane->requestRebuild();
			e.consume(this);
			return;
		}
		ui::TextField::onSelectKey(e);
	}

	void onDoubleClick(const DoubleClickEvent& e) override {
		setText("");
		pane->searchQuery.clear();
		pane->requestRebuild();
	}
};


struct SirenFavButton : widget::OpaqueWidget {
	SirenBrowserPane* pane = nullptr;

	void draw(const DrawArgs& args) override {
		if (!pane) return;
		bool active = pane->favoritesOnly;
		BNDwidgetState state = active ? BND_ACTIVE
		                     : (APP->event->getHoveredWidget() == this ? BND_HOVER : BND_DEFAULT);
		nvgSave(args.vg);
		nvgScale(args.vg, TOPBAR_SCALE, TOPBAR_SCALE);
		float lw = box.size.x / TOPBAR_SCALE;
		float lh = box.size.y / TOPBAR_SCALE;
		bndToolButton(args.vg, 0, 0, lw, lh, BND_CORNER_NONE, state, -1, nullptr);
		nvgFontSize(args.vg, 14.f);
		nvgFontFaceId(args.vg, APP->window->uiFont->handle);
		nvgTextAlign(args.vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
		nvgFillColor(args.vg, active
			? nvgRGBf(1.f, 0.85f, 0.1f)
			: nvgRGBAf(1.f, 1.f, 1.f, 0.55f));
		nvgText(args.vg, lw * 0.5f, lh * 0.5f, active ? "★" : "☆", nullptr);
		nvgTextAlign(args.vg, NVG_ALIGN_LEFT | NVG_ALIGN_BASELINE);
		nvgRestore(args.vg);
	}

	void onButton(const event::Button& e) override {
		if (e.button == GLFW_MOUSE_BUTTON_LEFT && e.action == GLFW_PRESS) {
			if (pane) {
				pane->favoritesOnly = !pane->favoritesOnly;
				pane->requestRebuild();
			}
			e.consume(this);
		}
	}
};


struct SirenTopBar : widget::OpaqueWidget {
	SirenBrowserPane* pane = nullptr;

	void init() {
		const float browserW = 188.0f;
		const float gapW     = 10.f;
		const float btnH     = 22.f * TOPBAR_SCALE;        // physical button height
		const float btnY     = (box.size.y - btnH) * 0.5f; // centred in top bar
		const float starW    = btnH;                        // square star button
		const float srcW     = browserW - starW - 2.f;     // source button leaves room for star

		SirenSourceButton* sourceButton = new SirenSourceButton;
		sourceButton->box.pos  = Vec(0.f, btnY);
		sourceButton->box.size = Vec(srcW, btnH);
		sourceButton->pane = pane;
		addChild(sourceButton);

		SirenFavButton* favButton = new SirenFavButton;
		favButton->box.pos  = Vec(srcW + 2.f, btnY);
		favButton->box.size = Vec(starW, btnH);
		favButton->pane = pane;
		addChild(favButton);

		SirenSearchField* searchField = new SirenSearchField;
		searchField->box.pos  = Vec(browserW + gapW, btnY);
		searchField->box.size = Vec(box.size.x - browserW - gapW, btnH);
		searchField->pane = pane;
		addChild(searchField);
	}
};

} // namespace Siren
} // namespace StoermelderPackOne
