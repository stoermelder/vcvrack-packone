#pragma once
#include "../../plugin.hpp"
#include "Mb_manifests.hpp"
#include <plugin.hpp>
#include <FuzzySearchDatabase.hpp>

namespace StoermelderPackOne {
namespace Mb {

// Model DB

// Shared fuzzy search database — initialized once by BrowserOverlay, re-initialized when searchDescriptions changes
extern fuzzysearch::Database<plugin::Model*> modelDb;
extern bool searchDescriptions;
extern bool sortBySearchScore;

void modelDbInit();
ModuleWidget* chooseModel(plugin::Model* model, bool hideBrowser = true);


// Model Widths (HP, determined from actual panel widget)

extern std::map<Model*, int> modelWidths;

void modelWidthSet(Model* model, int hp);
int modelWidthGet(Model* model); // returns -1 if unknown
void modelWidthScanAll();
void modelWidthsFromJson();
void modelWidthsToJson();


// Model Usage

struct ModelUsage {
	int usedCount = 0;
	int64_t usedTimestamp = -std::numeric_limits<int64_t>::infinity();
};
extern std::map<Model*, ModelUsage*> modelUsage;

void modelUsageTouch(Model* model);
void modelUsageReset();


// Favorite

enum class FavoriteMode {
	VCVRACK = 0,
	MB = 1,
	BOTH = 2
};
extern FavoriteMode favoriteMode;
extern bool favoriteHighlight;

extern std::set<Model*> favoriteModels;
bool isModelFavorite(Model* model);
void setModelFavorite(Model* model, bool favorite);
static void toggleModelFavorite(plugin::Model* model) {
	setModelFavorite(model, !isModelFavorite(model));
}


// Hidden

extern std::set<Model*> hiddenModels;
void toggleModelHidden(Model* model);
bool isModelHidden(plugin::Model* model);
void hiddenModelsReset();


// Custom Tags

extern std::map<std::string, std::set<Model*>> customTagModels;


void customTagAdd(Model* model, const std::string& tag);
void customTagRemove(Model* model, const std::string& tag);
bool isValidCustomTag(const std::string& tag);
bool customTagHas(Model* model, const std::string& tag, bool resolveKey = false);
void customTagDelete(const std::string& tag);
void customTagReset();
std::set<std::string> customTagsForModel(Model* model);
std::set<std::string> customTagsAll();


// Predefined Tags

// Tag modifications: predefined tags that are added/removed per model
extern std::map<Model*, std::set<int>> predefinedTagsAdded;
extern std::map<Model*, std::set<int>> predefinedTagsRemoved;

// Cache for effective tag IDs to avoid O(n*m) lookup during refresh
struct TagIdCache {
	std::set<int> tagIds;
	uint64_t version = 0;
};
extern std::map<Model*, TagIdCache> effectiveTagIdsCache;
extern uint64_t effectiveTagIdsVersion;

void effectiveTagIdsCacheInvalidate(Model* model);
void effectiveTagIdsCacheInvalidateAll();

void predefinedTagAdd(Model* model, int tagId);
void predefinedTagRemove(Model* model, int tagId);
bool predefinedTagHasAdded(Model* model, int tagId);
bool predefinedTagHasRemoved(Model* model, int tagId);
void predefinedTagDelete(int tagId);
void predefinedTagsReset();
std::set<int> getEffectiveTagIds(Model* model);
std::set<std::string> getEffectiveTagNames(Model* model);


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

	// Display circle center in scene coords, offset from the cursor.
	// Prefers upper-left; flips to the opposite side when more than 1/4 of the
	// circle would go outside the window. Up to 1/4 overhang is allowed.
	Vec displayCenter() const {
		float overlap = 12.f;          // cursor sits this many px inside the circle edge
		float maxOvhg = radius * 0.7f; // ca. 1/3 of diameter may go outside
		Vec sceneSize = APP->scene->box.size;

		// cx / cy are the circle center positions.
		// Left edge of circle = cx - radius; we allow it down to -maxOvhg.
		// Flip threshold: cx < radius - maxOvhg (i.e., more than maxOvhg clips left)
		float cx = mousePos.x - radius + overlap;
		if (cx < radius - maxOvhg)
			cx = mousePos.x + radius - overlap;
		cx = math::clamp(cx, radius - maxOvhg, sceneSize.x - radius + maxOvhg);

		float cy = mousePos.y - radius + overlap;
		if (cy < radius - maxOvhg)
			cy = mousePos.y + radius - overlap;
		cy = math::clamp(cy, radius - maxOvhg, sceneSize.y - radius + maxOvhg);

		return Vec(cx, cy);
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
	Widget* mbV06;
	Widget* mbV1;
	Widget* mbV2;

	MODE* mode;

	// Rack-space position captured when the browser is shown via right-click.
	// Used to place the new module at the right-click position rather than at
	// the current mouse position (which is inside the browser UI).
	math::Vec rackMousePosAtOpen = math::Vec(NAN, NAN);

	// Pending immediate drag: if non-null, the user is still holding the mouse
	// button after adding a module. Once they move far enough (in screen space)
	// the drag is transferred to the module widget; otherwise the module stays
	// at rackMousePosAtOpen when they release.
	ModuleWidget* pendingDragModule = nullptr;
	math::Vec pendingDragSceneAnchor = math::Vec(NAN, NAN);

	BrowserOverlay();
	~BrowserOverlay();

	void onShow(const event::Show& e) override;
	void step() override;
	void draw(const DrawArgs& args) override;
	void onButton(const event::Button& e) override;
};

struct DropdownChoiceContainer : widget::OpaqueWidget {
	ui::ScrollWidget* scroll;
	ui::SequentialLayout* layout;
	std::string filterText;
	std::string filterTextActive;
	std::map<widget::Widget*, std::string> itemTexts;
	widget::Widget* selectedItem = nullptr;

	DropdownChoiceContainer() {
		scroll = new ui::ScrollWidget;
		addChild(scroll);

		layout = new ui::SequentialLayout;
		layout->orientation = ui::SequentialLayout::HORIZONTAL_ORIENTATION;
		layout->margin = Vec(5, 5);
		layout->spacing = Vec(5, 5);
		layout->box.size.y = 1.f;
		scroll->container->addChild(layout);
	}

	void step() override {
		box.size.y = std::min(layout->box.size.y, parent->box.size.y - box.pos.y - 20.f);
		scroll->box.size = box.size;
		layout->box.size.x = box.size.x;
		widget::OpaqueWidget::step();
	}

	void draw(const widget::Widget::DrawArgs& args) override {
		nvgFontSize(args.vg, BND_LABEL_FONT_SIZE);
		bndMenuBackground(args.vg, 0, 0, box.size.x, box.size.y, 0);
		OpaqueWidget::draw(args);
	}

	void navigateSelection(int key) {
		std::vector<widget::Widget*> visible;
		for (widget::Widget* w : layout->children) {
			if (w->visible) visible.push_back(w);
		}
		if (visible.empty()) return;

		// Starting point: selected item > hovered item > first visible
		widget::Widget* current = nullptr;
		if (selectedItem) {
			for (widget::Widget* w : visible) {
				if (w == selectedItem) { current = w; break; }
			}
		}
		if (!current) {
			widget::Widget* hovered = APP->event->getHoveredWidget();
			while (hovered && !current) {
				for (widget::Widget* w : visible) {
					if (w == hovered) { current = hovered; break; }
				}
				hovered = hovered->parent;
			}
		}
		if (!current) current = visible[0];

		widget::Widget* next = current;

		switch (key) {
			case GLFW_KEY_RIGHT: {
				bool found = false;
				for (widget::Widget* w : visible) {
					if (found) { next = w; break; }
					if (w == current) found = true;
				}
				break;
			}
			case GLFW_KEY_LEFT: {
				widget::Widget* prev = nullptr;
				for (widget::Widget* w : visible) {
					if (w == current) { if (prev) next = prev; break; }
					prev = w;
				}
				break;
			}
			case GLFW_KEY_DOWN: {
				float currentCenterY = current->box.getCenter().y;
				float currentCenterX = current->box.getCenter().x;
				float nextRowY = std::numeric_limits<float>::max();
				for (widget::Widget* w : visible) {
					if (w->box.pos.y > currentCenterY && w->box.pos.y < nextRowY)
						nextRowY = w->box.pos.y;
				}
				if (nextRowY < std::numeric_limits<float>::max()) {
					float minDist = std::numeric_limits<float>::max();
					for (widget::Widget* w : visible) {
						if (std::abs(w->box.pos.y - nextRowY) < 2.f) {
							float dist = std::abs(w->box.getCenter().x - currentCenterX);
							if (dist < minDist) { minDist = dist; next = w; }
						}
					}
				}
				break;
			}
			case GLFW_KEY_UP: {
				float currentCenterX = current->box.getCenter().x;
				float prevRowY = -std::numeric_limits<float>::max();
				for (widget::Widget* w : visible) {
					if (w->box.pos.y < current->box.pos.y && w->box.pos.y > prevRowY)
						prevRowY = w->box.pos.y;
				}
				if (prevRowY > -std::numeric_limits<float>::max()) {
					float minDist = std::numeric_limits<float>::max();
					for (widget::Widget* w : visible) {
						if (std::abs(w->box.pos.y - prevRowY) < 2.f) {
							float dist = std::abs(w->box.getCenter().x - currentCenterX);
							if (dist < minDist) { minDist = dist; next = w; }
						}
					}
				}
				break;
			}
		}

		selectedItem = next;
		Rect r = next->box;
		r.pos = r.pos.plus(layout->box.pos);
		scroll->scrollTo(r);
	}

	void onSelectKey(const SelectKeyEvent& e) override {
		if (e.action == GLFW_PRESS || e.action == GLFW_REPEAT) {
			switch (e.key) {
				case GLFW_KEY_DOWN:
				case GLFW_KEY_UP:
				case GLFW_KEY_LEFT:
				case GLFW_KEY_RIGHT:
					navigateSelection(e.key);
					e.consume(this);
					return;
				case GLFW_KEY_BACKSPACE:
					filterText.clear();
					updateList();
					e.consume(this);
					return;
				default:
					break;
			}
		}
		if (e.action == GLFW_PRESS && e.isKeyCommand(GLFW_KEY_ENTER)) {
			widget::Widget* target = selectedItem;
			if (!target) {
				for (widget::Widget* w : layout->children) {
					if (w->isVisible()) { target = w; break; }
				}
			}
			if (target) {
				Button* b = dynamic_cast<Button*>(target);
				if (b) {
					ActionEvent _e;
					b->onAction(_e);
					e.consume(this);
				}
			}
		}
	}

	void onSelectText(const SelectTextEvent& e) override {
		std::u32string s32(1, char32_t(e.codepoint));
		std::string s8 = string::UTF32toUTF8(s32);
		filterText += s8;
		updateList();
		e.consume(this);
	}

	void updateList() {
		selectedItem = nullptr;
		std::string lowerFilter = string::lowercase(filterText);
		for (auto& pair : itemTexts) {
			widget::Widget* child = pair.first;
			std::string& itemText = pair.second;
			std::string lowerItem = string::lowercase(itemText);
			child->visible = string::startsWith(lowerItem, lowerFilter);
		}
	}
};

template <typename T>
struct DropdownChoiceItem : ui::Button {
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

		DropdownChoiceContainer* container = getAncestorOfType<DropdownChoiceContainer>();
		if (container && container->selectedItem == this) {
			nvgBeginPath(args.vg);
			nvgRect(args.vg, -2.f, -2.f, box.size.x + 4.f, box.size.y + 4.f);
			nvgStrokeWidth(args.vg, 1.5f);
			nvgStrokeColor(args.vg, nvgRGBAf(1.f, 1.f, 1.f, 0.9f));
			nvgStroke(args.vg);
		}
	}
};

template <typename TBrowser, typename TContainer = DropdownChoiceContainer>
static void openLayoutMenu(widget::Widget* button, std::vector<widget::Widget*> items) {
	static_assert(std::is_base_of<widget::Widget, TBrowser>::value, "TBrowser must be a widget type");

	auto browser = APP->scene->getFirstDescendantOfType<TBrowser>();
	Vec browserPos = browser->getAbsoluteOffset(Vec(0, 0));

	// Create menu container
	TContainer* container = new TContainer;
	float menuX = browserPos.x + browser->box.size.x * 0.15f;
	float menuY = button->getAbsoluteOffset(Vec(0, button->box.size.y)).y + 2.f;
	container->box.pos = Vec(menuX, menuY);
	container->box.size.x = browser->box.size.x * 0.7f;

	// Add items to layout
	for (widget::Widget* item : items) {
		container->layout->addChild(item);
		// Cache text for filtering
		if (auto* choiceItem = dynamic_cast<DropdownChoiceItem<TBrowser>*>(item)) {
			container->itemTexts[item] = choiceItem->rawText;
		}
	}

	ui::MenuOverlay* overlay = new ui::MenuOverlay;
	APP->scene->addChild(overlay);
	overlay->addChild(container);
}


json_t* moduleBrowserToJson(bool includeUsageData = true);
void moduleBrowserFromJson(json_t* rootJ);


} // namespace Mb
} // namespace StoermelderPackOne