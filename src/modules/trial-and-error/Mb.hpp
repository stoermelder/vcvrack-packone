#pragma once
#include "../../plugin.hpp"
#include "../strip/SelectionPreview.hpp"
#include <plugin.hpp>
#include <FuzzySearchDatabase.hpp>
#include <helpers.hpp>

namespace StoermelderPackOne {
namespace Mb {

// Usage data

struct ModelUsage {
	int usedCount = 0;
	int64_t usedTimestamp = -std::numeric_limits<int64_t>::infinity();
};

void modelUsageTouch(Model* model);
void modelUsageReset();

// Globals

json_t* moduleBrowserToJson(bool includeUsageData = true);
void moduleBrowserFromJson(json_t* rootJ);

extern std::set<Model*> favoriteModels;
extern std::set<Model*> hiddenModels;
extern std::map<Model*, ModelUsage*> modelUsage;
extern std::map<std::string, std::set<Model*>> customTagModels;

inline std::set<std::string> customTagsAll() {
	std::set<std::string> result;
	for (auto& pair : customTagModels)
		result.insert(pair.first);
	return result;
}

// Tag modifications: predefined tags that are added/removed per model
extern std::map<Model*, std::set<int>> predefinedTagsAdded;
extern std::map<Model*, std::set<int>> predefinedTagsRemoved;

void customTagAdd(Model* model, const std::string& tag);
void customTagRemove(Model* model, const std::string& tag);
bool customTagHas(Model* model, const std::string& tag, bool resolveKey = false);
void customTagDelete(const std::string& tag);
std::set<std::string> customTagsForModel(Model* model);
std::set<std::string> customTagsAll();

// Predefined tag modifications
void predefinedTagAdd(Model* model, int tagId);
void predefinedTagRemove(Model* model, int tagId);
bool predefinedTagHasAdded(Model* model, int tagId);
bool predefinedTagHasRemoved(Model* model, int tagId);
void predefinedTagDelete(int tagId);
std::set<int> getEffectiveTagIds(Model* model);
std::set<std::string> getEffectiveTagNames(Model* model);

// Favorite mode handling
enum class FavoriteMode {
	VCVRACK = 0,
	MB = 1,
	BOTH = 2
};

extern FavoriteMode favoriteMode;

bool isModelFavorite(Model* model);
void setModelFavorite(Model* model, bool favorite);


// Shared fuzzy search database — initialized once by BrowserOverlay, re-initialized when searchDescriptions changes
extern fuzzysearch::Database<plugin::Model*> modelDb;
extern bool searchDescriptions;
extern bool sortBySearchScore;
extern bool favoriteHighlight;
void modelDbInit();


// Magnifier overlay for module preview zoom

struct MagnifierOverlay : widget::TransparentWidget {
	widget::FramebufferWidget* fb = NULL;
	Vec sourceAbsPos;
	Vec sourceSize;
	Vec mousePos;
	float radius = 120.f;
	float magnification = 3.f;
	bool initialized = false;
	bool enabled = true;

	// Display circle center in scene coords, offset upper-left from the cursor
	Vec displayCenter() const {
		float gap = -12.f;
		return mousePos + Vec(-radius - gap, -radius - gap);
	}

	void step() override {
		Vec dc = displayCenter();
		box.pos = dc - Vec(radius + 4, radius + 4);
		box.size = Vec((radius + 4) * 2, (radius + 4) * 2);
		widget::TransparentWidget::step();
	}

	void draw(const DrawArgs& args) override {
		if (!enabled || !initialized || !fb) return;
		NVGLUframebuffer* framebuf = fb->getFramebuffer();
		if (!framebuf || framebuf->image < 0) return;

		// Circle center in overlay-local coords
		Vec center = displayCenter() - box.pos;

		// Image pattern: zoom the module texture centered on mousePos,
		// but place the result at the offset displayCenter.
		// A scene point p maps to: displayCenter + (p - mousePos) * magnification
		// so the module top-left (sourceAbsPos) maps to (in local coords):
		//   center + (sourceAbsPos - mousePos) * magnification
		float ox = center.x + (sourceAbsPos.x - mousePos.x) * magnification;
		float oy = center.y + (sourceAbsPos.y - mousePos.y) * magnification;
		float ex = sourceSize.x * magnification;
		float ey = sourceSize.y * magnification;

		NVGpaint imgPaint = nvgImagePattern(args.vg, ox, oy, ex, ey, 0.f, framebuf->image, 1.f);

		// Clip the circle fill to the zoomed texture rectangle [ox,oy,ex,ey].
		// Outside that rect the image pattern would clamp to edge pixels (solid
		// panel color). The circle rim is drawn after restore so it stays full.
		nvgSave(args.vg);
		nvgIntersectScissor(args.vg, ox, oy, ex, ey);

		nvgBeginPath(args.vg);
		nvgCircle(args.vg, center.x, center.y, radius);
		nvgFillPaint(args.vg, imgPaint);
		nvgFill(args.vg);

		// Edge vignette
		NVGpaint vignette = nvgRadialGradient(args.vg, center.x, center.y, radius * 0.7f, radius,
			nvgRGBAf(0, 0, 0, 0), nvgRGBAf(0, 0, 0, 0.35f));
		nvgBeginPath(args.vg);
		nvgCircle(args.vg, center.x, center.y, radius);
		nvgFillPaint(args.vg, vignette);
		nvgFill(args.vg);

		nvgRestore(args.vg);

		// Rim drawn outside the scissor so it's always a full circle
		nvgBeginPath(args.vg);
		nvgCircle(args.vg, center.x, center.y, radius);
		nvgStrokeWidth(args.vg, 2.f);
		nvgStrokeColor(args.vg, nvgRGBAf(0.2f, 0.2f, 0.2f, 0.8f));
		nvgStroke(args.vg);
	}
};


// Browser overlay

enum class MODE {
	V06,
	V1,
	V2
};

struct BrowserOverlay : widget::OpaqueWidget {
	Widget* mbWidgetBackup;

	bool visibleBefore = false;
	Widget* mbActive;

	Widget* mbV06;
	Widget* mbV1;
	Widget* mbV2;
	Widget* mbSelection;

	MODE* mode;

	BrowserOverlay();
	~BrowserOverlay();

	void step() override;
	void draw(const DrawArgs& args) override;
	void onButton(const event::Button& e) override;
};


template <typename T>
static void openLayoutMenu(widget::Widget* button, std::vector<widget::Widget*> items) {
	// Container that draws a menu background and holds the layout
	struct MenuContainer : widget::OpaqueWidget {
		ui::ScrollWidget* scroll;
		ui::SequentialLayout* layout;

		MenuContainer() {
			scroll = new ui::ScrollWidget;
			addChild(scroll);

			// Create horizontal sequential layout inside container
			layout = new ui::SequentialLayout;
			layout->orientation = ui::SequentialLayout::HORIZONTAL_ORIENTATION;
			//layout->alignment = ui::SequentialLayout::CENTER_ALIGNMENT;
			layout->margin = Vec(5, 5);
			layout->spacing = Vec(5, 5);
			layout->box.size.y = 1.f;
			scroll->container->addChild(layout);
		}

		void step() override {
			box.size.y = std::min(layout->box.size.y, parent->box.size.y - box.pos.y - 20.f);
			scroll->box.size = box.size;
			layout->box.size.x = box.size.x;
			OpaqueWidget::step();
		}

		void draw(const widget::Widget::DrawArgs& args) override {
			//nvgFontFaceId(ctx, bnd_font);
        	nvgFontSize(args.vg, BND_LABEL_FONT_SIZE);
			bndMenuBackground(args.vg, 0, 0, box.size.x, box.size.y, 0);
			OpaqueWidget::draw(args);
		}
	};

	auto browser = APP->scene->getFirstDescendantOfType<T>();
	Vec browserPos = browser->getAbsoluteOffset(Vec(0, 0));

	// Create menu container
	MenuContainer* container = new MenuContainer;
	float menuX = browserPos.x + browser->box.size.x * 0.15f;
	float menuY = button->getAbsoluteOffset(Vec(0, button->box.size.y)).y + 2.f;
	container->box.pos = Vec(menuX, menuY);
	container->box.size.x = browser->box.size.x * 0.7f;

	// Add items to layout
	for (widget::Widget* item : items) {
		container->layout->addChild(item);
	}

	ui::MenuOverlay* overlay = new ui::MenuOverlay;
	APP->scene->addChild(overlay);
	overlay->addChild(container);
}

template <typename T>
struct ChoiceFilterItem : ui::Button {
	T* browser;
	bool disabled = false;
	std::string rawText;
	bool selected = false;

	void setRawText(std::string s) {
		rawText = s;
		text = s;

		NVGcontext* vg = APP->window->vg;
		nvgFontSize(vg, BND_LABEL_FONT_SIZE);
		nvgFontFaceId(vg, APP->window->uiFont->handle);
		float bounds[4];
		nvgTextBounds(vg, 0.f, 0.f, rawText.c_str(), NULL, bounds);
		box.size.x = bounds[2] - bounds[0] + 30.f;
		box.size.y = bounds[3] - bounds[1] + 8.f;
	}

	void onDragDrop(const DragDropEvent& e) override {
		if (!disabled) Button::onDragDrop(e);
	}

	void draw(const DrawArgs& args) override {
		text = string::f("%s %s", rawText, selected ? CHECKMARK(true) : "");

		BNDwidgetState state = BND_DEFAULT;
		if (!disabled) {
			if (APP->event->getHoveredWidget() == this) state = BND_HOVER;
			if (APP->event->getDraggedWidget() == this) state = BND_ACTIVE;
		}
		if (disabled) nvgSave(args.vg);
		if (disabled) nvgGlobalAlpha(args.vg, 0.35f);
		bndToolButton(args.vg, 0.0, 0.0, box.size.x, box.size.y, BND_CORNER_NONE, state, -1, text.c_str());
		if (disabled) nvgRestore(args.vg);
	}
};


} // namespace Mb
} // namespace StoermelderPackOne