#pragma once
#include <rack.hpp>
#include "SirenBrowserPane.hpp"
#include "SirenFileSystem.hpp"

namespace StoermelderPackOne {
namespace Siren {


/**
 * @brief Source selection choice button for the Siren top bar
 * 
 * Provides a dropdown button for selecting the active root container
 * and accessing source management options.
 */
struct SirenSourceButton : ui::ChoiceButton {
	SirenBrowserPane* pane = nullptr;

	void step() {
		if (!pane->rootContainers.empty() && pane->activeRootIdx >= 0 &&
			pane->activeRootIdx < (int)pane->rootContainers.size()) {
			ghc::filesystem::path p(pane->rootContainers[pane->activeRootIdx]);
			text = p.filename().string();
			if (text.empty()) text = p.string();
		}
		else {
			text = "No source";
		}
		text = string::ellipsize(text, 40);
		ChoiceButton::step();
	}

	void onAction(const event::Action& e) {
		ui::Menu* menu = createMenu();
		menu->box.pos   = getAbsoluteOffset(math::Vec(0, box.size.y));
		menu->box.size.x = box.size.x;

		// List of existing sources
		for (int i = 0; i < (int)pane->rootContainers.size(); i++) {
			ghc::filesystem::path p(pane->rootContainers[i]);
			std::string name = p.filename().string();
			if (name.empty()) name = p.string();
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
			if (pane->onRemoveRoot) pane->onRemoveRoot(pane->activeRootIdx);
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
			if (pane) {
				pane->searchQuery.clear();
				pane->requestRebuild();
			}
			e.consume(this);
			return;
		}
		ui::TextField::onSelectKey(e);
	}
};


struct SirenTopBar : widget::OpaqueWidget {
	SirenBrowserPane* pane = nullptr;
	float zoom = 0.6f;
	float contentX = 8.f;
	float contentY = 8.f;
	float totalW = 480.1f; // browserW + previewW at zoom 0.6f
	float topBarH = 18.f;   // 30.f * zoom

	SirenTopBar() {
		// Size is set based on zoom-scaled logical dimensions
		box.size = rack::math::Vec(totalW, topBarH);
	}

	void init() {
		clearChildren();

		// Calculate logical dimensions (before zoom)
		// These must match the original SirenWidget constructor values
		const float logW  = totalW / zoom;  // 800 logical pixels
		const float logH  = topBarH / zoom; // 30 logical pixels
		const float btnH  = 22.f;
		const float btnW  = 150.f / zoom;   // 250 logical pixels
		const float mrgX  = 5.f;
		const float mrgY  = (logH - btnH) * 0.5f;

		SirenSourceButton* srcBtn = new SirenSourceButton;
		srcBtn->box.size = rack::math::Vec(btnW, btnH);
		srcBtn->pane = pane;

		SirenSearchField* searchField = new SirenSearchField;
		searchField->box.size = rack::math::Vec(300.f, btnH);  // 300 logical (not scaled)
		searchField->pane = pane;

		ui::SequentialLayout* layout = new ui::SequentialLayout;
		layout->box.pos  = rack::math::Vec(0.f, 0.f);
		layout->box.size = rack::math::Vec(logW, logH);
		layout->margin   = rack::math::Vec(mrgX, mrgY);
		layout->spacing  = rack::math::Vec(mrgX, 0.f);
		layout->addChild(srcBtn);
		layout->addChild(searchField);

		widget::ZoomWidget* topBarZw = new widget::ZoomWidget;
		topBarZw->box.pos  = rack::math::Vec(contentX, contentY);   // relative to parent
		topBarZw->box.size = rack::math::Vec(totalW, topBarH);      // display size (scaled)
		topBarZw->setZoom(zoom);
		topBarZw->addChild(layout);
		addChild(topBarZw);
	}
};

} // namespace Siren
} // namespace StoermelderPackOne
