#pragma once
#include <rack.hpp>
#include "SirenBrowserPane.hpp"

namespace StoermelderPackOne {
namespace Siren {


// Appends the shared conversion/resampling settings items to `menu`.
// Called from both SirenWidget::appendContextMenu and SirenSourceButton::onAction.
//   patchStorageAvailable — pass (module != nullptr); disables the "Patch storage"
//   option in the module browser where there is no real module instance.
inline void appendConversionMenuItems(ui::Menu* menu, bool patchStorageAvailable = true) {
	menu->addChild(createBoolPtrMenuItem("Resample on playback", "", &sirenSettings.resampleOnPlayback));
	menu->addChild(createBoolPtrMenuItem("Resample on drop", "", &sirenSettings.resampleOnDrop));
	menu->addChild(createSubmenuItem("Resample quality", "", [](ui::Menu* qMenu) {
		struct QPreset { int value; std::string label; std::string desc; };
		QPreset presets[] = {
			{ 1, "Fast", "Lowest CPU" },
			{ 6, "Default", "Balanced quality and CPU" },
			{ 10, "Best", "Highest CPU" },
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
			[=]() { return sirenSettings.convertTarget == SirenSettings::CT_SOURCE; },
			[=]() { sirenSettings.convertTarget = SirenSettings::CT_SOURCE; }
		));
		subMenu->addChild(createCheckMenuItem(
			sirenSettings.customConvertDir.empty() ? "Custom folder..." : sirenSettings.customConvertDir, "",
			[=]() { return sirenSettings.convertTarget == SirenSettings::CT_CUSTOM; },
			[]() {
				char* path = osdialog_file(OSDIALOG_OPEN_DIR, nullptr, nullptr, nullptr);
				if (!path) return;
				sirenSettings.customConvertDir = path;
				sirenSettings.convertTarget = SirenSettings::CT_CUSTOM;
				free(path);
			}
		));
		// Patch storage: only available when there is a real module instance.
		// Disabled in the module browser where no patch storage directory exists.
		subMenu->addChild(createCheckMenuItem("Patch storage", "",
			[=]() { return sirenSettings.convertTarget == SirenSettings::CT_PATCH; },
			[=]() { sirenSettings.convertTarget = SirenSettings::CT_PATCH; },
			!patchStorageAvailable
		));
		subMenu->addChild(new ui::MenuSeparator);
		// "Always copy" forces a copy of the source file into the target folder
		// even when no conversion/trim/resample is needed. Disabled when the
		// target is the source folder — copying a file on top of itself is pointless.
		subMenu->addChild(createBoolMenuItem("Always copy", "",
			[=]() { return sirenSettings.alwaysCopy; },
			[=](bool v) { sirenSettings.alwaysCopy = v; },
			sirenSettings.convertTarget == SirenSettings::CT_SOURCE));
	}));
}


// Source selection choice button for the Siren top bar.
// Provides a dropdown button for selecting the active root container
// and accessing source management options.
static constexpr float TOPBAR_SCALE = 0.6f;

// Mixin for small top-bar icon buttons that show a hover tooltip with a fixed
// label (set via `tooltipText`).
struct SirenTooltipWidget : widget::OpaqueWidget {
	std::string tooltipText;
	ui::Tooltip* tooltip = nullptr;

	~SirenTooltipWidget() {
		setTooltip(nullptr);
	}

	void setTooltip(ui::Tooltip* t) {
		if (tooltip) { tooltip->requestDelete(); tooltip = nullptr; }
		if (t) { APP->scene->addChild(t); tooltip = t; }
	}

	void onEnter(const event::Enter& e) override {
		if (!tooltipText.empty()) {
			ui::Tooltip* t = new ui::Tooltip;
			t->text = tooltipText;
			setTooltip(t);
		}
		OpaqueWidget::onEnter(e);
	}

	void onLeave(const event::Leave& e) override {
		setTooltip(nullptr);
		OpaqueWidget::onLeave(e);
	}
};


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
		menu->box.pos = getAbsoluteOffset(math::Vec(0, box.size.y));
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
		if (pane->activeDs) {
			pane->activeDs->appendSourceMenuItems(menu);
			if (i != menu->children.size()) menu->addChild(new ui::MenuSeparator);
		}

		bool indexing = pane->indexTask.running();
		menu->addChild(createMenuItem("Index metadata for all files", "", [this]() {
			pane->startIndexing();
		}, !pane->activeDs || indexing));
		if (pane->indexTask.running()) {
			menu->addChild(createMenuItem("Cancel indexing", "", [this]() {
				pane->cancelActiveSourceTasks();
			}));
		}

		bool classifying = pane->classifyTask.running();
		menu->addChild(createMenuItem("Run tag classifier on all files", "", [this]() {
			pane->startTagClassificationAll();
		}, !pane->activeDs || classifying));
		if (classifying) {
			menu->addChild(createMenuItem("Cancel tag classification", "", [this]() {
				pane->cancelActiveSourceTasks();
			}));
		}

		menu->addChild(new ui::MenuSeparator);
		menu->addChild(createMenuItem("Add root...", "", [this]() {
			if (pane->onAddRoot) pane->onAddRoot();
		}));
		menu->addChild(createMenuItem("Remove current root", "", [this]() {
			if (pane->onRemoveRoot) pane->onRemoveRoot();
		}, pane->rootContainers.empty()));

		menu->addChild(new MenuSeparator);
		appendConversionMenuItems(menu);
	}
};


// Search field widget for the Siren top bar.
// Provides a text input field for searching within the file browser.
// Supports escape key to clear the search and trigger a rebuild.
struct SirenSearchField : ui::TextField {
	SirenBrowserPane* pane = nullptr;
	ui::Tooltip* tooltip = nullptr;

	SirenSearchField() {
		placeholder = "Search...";
	}

	~SirenSearchField() {
		setTooltip(nullptr);
	}

	void setTooltip(ui::Tooltip* t) {
		if (tooltip) { tooltip->requestDelete(); tooltip = nullptr; }
		if (t) { APP->scene->addChild(t); tooltip = t; }
	}

	void onEnter(const event::Enter& e) override {
		ui::Tooltip* t = new ui::Tooltip;
		t->text =
			"Search by filename\n"
			"Supports filter expressions:\n"
			"  bpm:120, bpm:<120, bpm:>=120\n"
			"  length:30, length:<1m, length:>=30s";
		setTooltip(t);
		ui::TextField::onEnter(e);
	}

	void onLeave(const event::Leave& e) override {
		setTooltip(nullptr);
		ui::TextField::onLeave(e);
	}

	void draw(const DrawArgs& args) override {
		BNDwidgetState state;
		if (APP->event->getSelectedWidget() == this) state = BND_ACTIVE;
		else if (APP->event->getHoveredWidget() == this) state = BND_HOVER;
		else state = BND_DEFAULT;
		int begin = std::min(cursor, selection);
		int end = std::max(cursor, selection);
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

	int getTextPosition(math::Vec mousePos) override {
		// draw() renders at TOPBAR_SCALE, so the text (and its glyph positions)
		// are scaled relative to box.size — undo that here, matching draw()'s
		// effective coordinate space, or the cursor lands at the wrong glyph.
		return bndTextFieldTextPosition(APP->window->vg, 0.0, 0.0,
			box.size.x / TOPBAR_SCALE, box.size.y / TOPBAR_SCALE, -1, text.c_str(),
			mousePos.x / TOPBAR_SCALE, mousePos.y / TOPBAR_SCALE);
	}

	void onChange(const event::Change& e) override {
		if (pane) pane->setSearchQuery(rack::string::trim(text));
		ui::TextField::onChange(e);
	}

	void onSelectKey(const event::SelectKey& e) override {
		if (e.action == GLFW_PRESS && e.key == GLFW_KEY_ESCAPE) {
			if (pane) pane->setSearchQuery("");
			e.consume(this);
			return;
		}
		ui::TextField::onSelectKey(e);
	}

	void onDoubleClick(const DoubleClickEvent& e) override {
		if (pane) pane->setSearchQuery("");
	}
};


// Previous/next arrow buttons flanking the source selection button — cycle
// through rootContainers with wraparound.
struct SirenSourceArrowButton : SirenTooltipWidget {
	SirenBrowserPane* pane = nullptr;
	bool next = false;  // false = previous ("◀"), true = next ("▶")

	void draw(const DrawArgs& args) override {
		if (!pane) return;
		BNDwidgetState state = APP->event->getHoveredWidget() == this ? BND_HOVER : BND_DEFAULT;
		nvgSave(args.vg);
		nvgScale(args.vg, TOPBAR_SCALE, TOPBAR_SCALE);
		float lw = box.size.x / TOPBAR_SCALE;
		float lh = box.size.y / TOPBAR_SCALE;
		bndToolButton(args.vg, 0, 0, lw, lh, BND_CORNER_NONE, state, -1, nullptr);

		nvgFontSize(args.vg, 14.f);
		nvgFontFaceId(args.vg, APP->window->uiFont->handle);
		nvgTextAlign(args.vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
		nvgFillColor(args.vg, nvgRGBAf(1.f, 1.f, 1.f, 0.55f));
		nvgText(args.vg, lw * 0.5f, lh * 0.5f, next ? "\xe2\x96\xb6" : "\xe2\x97\x80", nullptr);
		nvgTextAlign(args.vg, NVG_ALIGN_LEFT | NVG_ALIGN_BASELINE);
		nvgRestore(args.vg);
	}

	void onButton(const event::Button& e) override {
		if (e.button == GLFW_MOUSE_BUTTON_LEFT && e.action == GLFW_PRESS) {
			if (pane && !pane->rootContainers.empty()) {
				int n = (int)pane->rootContainers.size();
				int idx = pane->activeRootIdx;
				if (idx < 0) idx = 0;
				idx = next ? (idx + 1) % n : (idx - 1 + n) % n;
				if (pane->onSelectRoot) pane->onSelectRoot(idx);
			}
			e.consume(this);
		}
	}
};


struct SirenFavButton : SirenTooltipWidget {
	SirenBrowserPane* pane = nullptr;

	SirenFavButton() {
		tooltipText = "Show favorites only";
	}

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
		const float gapW = 10.f;
		const float btnH = 22.f * TOPBAR_SCALE;        // physical button height
		const float btnY = (box.size.y - btnH) * 0.5f; // centred in top bar
		const float starW = btnH;                        // square star button
		const float arrowW = btnH;                       // square prev/next arrow buttons
		const float gap = 2.f;
		// source button leaves room for the star and the prev/next arrows flanking it
		const float srcW = browserW - starW - 2.f * arrowW - 3.f * gap;

		SirenSourceArrowButton* prevButton = new SirenSourceArrowButton;
		prevButton->box.pos = Vec(0.f, btnY);
		prevButton->box.size = Vec(arrowW, btnH);
		prevButton->pane = pane;
		prevButton->next = false;
		prevButton->tooltipText = "Previous source";
		addChild(prevButton);

		SirenSourceButton* sourceButton = new SirenSourceButton;
		sourceButton->box.pos = Vec(arrowW + gap, btnY);
		sourceButton->box.size = Vec(srcW, btnH);
		sourceButton->pane = pane;
		addChild(sourceButton);

		SirenSourceArrowButton* nextButton = new SirenSourceArrowButton;
		nextButton->box.pos = Vec(arrowW + gap + srcW + gap, btnY);
		nextButton->box.size = Vec(arrowW, btnH);
		nextButton->pane = pane;
		nextButton->next = true;
		nextButton->tooltipText = "Next source";
		addChild(nextButton);

		SirenFavButton* favButton = new SirenFavButton;
		favButton->box.pos = Vec(arrowW + gap + srcW + gap + arrowW + gap, btnY);
		favButton->box.size = Vec(starW, btnH);
		favButton->pane = pane;
		addChild(favButton);

		SirenSearchField* searchField = new SirenSearchField;
		searchField->box.pos = Vec(browserW + gapW, btnY);
		searchField->box.size = Vec(box.size.x - browserW - gapW, btnH);
		searchField->pane = pane;
		addChild(searchField);

		pane->setSearchFieldText = [searchField](const std::string& text) {
			searchField->setText(text);
		};
	}
};

} // namespace Siren
} // namespace StoermelderPackOne
