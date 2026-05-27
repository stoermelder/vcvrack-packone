#pragma once
#include <rack.hpp>
#include "SirenDataSource.hpp"
#include "SirenFileSystem.hpp"
#include "SirenMetadata.hpp"
#include "../../utils/TaskWorker.hpp"

namespace StoermelderPackOne {
namespace Siren {

// Forward declarations
struct SirenBrowserPane;

// ─── source-selection choice button (method bodies defined after SirenBrowserPane) ──

struct SirenSourceButton : ui::ChoiceButton {
	SirenBrowserPane* pane = nullptr;
	void step() override;
	void onAction(const event::Action& e) override;
};

// ─── drag state shared between browser and preview panes ─────────────────────

struct SirenDragState {
	bool active = false;
	std::string dragPath;
};

// ─── layout constants ─────────────────────────────────────────────────────────

static constexpr float BROWSER_HEADER_H = 30.f;
static constexpr float BROWSER_TAG_H    = 72.f;

static std::string toTitleCase(const std::string& s) {
	std::string r = s;
	bool cap = true;
	for (char& c : r) {
		if (c == ' ' || c == '_' || c == '-') { cap = true; c = ' '; }
		else if (cap) { c = (char)::toupper(c); cap = false; }
		else c = (char)::tolower(c);
	}
	return r;
}

// ─── single row in the tree ───────────────────────────────────────────────────

struct SirenTreeRow : widget::OpaqueWidget {
	static constexpr float ROW_H   = 20.f;
	static constexpr float INDENT  = 14.f;

	DataSourceNode node;
	int indentLevel = 0;
	RootMetadata* metadata = nullptr;
	SirenBrowserPane* pane = nullptr;
	bool selected = false;

	// Favorite star button (file rows only)
	struct StarButton : widget::OpaqueWidget {
		SirenTreeRow* row = nullptr;
		void draw(const DrawArgs& args) override {
			if (!row || !row->metadata) return;
			bool fav = row->metadata->isFavorite(row->node.relativePath);
			nvgFontSize(args.vg, 12.f);
			nvgFillColor(args.vg, fav
				? nvgRGBf(1.f, 0.85f, 0.1f)
				: nvgRGBAf(1.f, 1.f, 1.f, 0.22f));
			nvgText(args.vg, 0.f, 12.f, fav ? "★" : "☆", nullptr);
		}
		void onButton(const event::Button& e) override {
			if (e.button == GLFW_MOUSE_BUTTON_LEFT && e.action == GLFW_PRESS) {
				if (row && row->metadata) {
					bool fav = row->metadata->isFavorite(row->node.relativePath);
					row->metadata->setFavorite(row->node.relativePath, !fav);
				}
				e.consume(this);
			}
		}
	};

	StarButton* starBtn = nullptr;

	void init(const DataSourceNode& n, int indent, RootMetadata* meta, SirenBrowserPane* p) {
		node = n;
		indentLevel = indent;
		metadata = meta;
		pane = p;
		box.size = Vec(0.f, ROW_H);

		if (!node.isDirectory) {
			starBtn = new StarButton;
			starBtn->row = this;
			starBtn->box.pos = Vec(0.f, 2.f);
			starBtn->box.size = Vec(14.f, 16.f);
			addChild(starBtn);
		}
	}

	void step() override {
		// Keep star button flush against the right edge
		if (starBtn)
			starBtn->box.pos.x = box.size.x - 16.f;
		widget::OpaqueWidget::step();
	}

	void draw(const DrawArgs& args) override {
		float w = box.size.x;
		float h = ROW_H;

		// Blendish widget state
		BNDwidgetState state = BND_DEFAULT;
		if (selected) state = BND_ACTIVE;
		else if (APP->event->getHoveredWidget() == this) state = BND_HOVER;

		// Background using blendish tool button styling (label drawn manually for indentation)
		bndToolButton(args.vg, 0, 0, w, h, BND_CORNER_NONE, state, -1, nullptr);

		float textX = 6.f + indentLevel * INDENT;
		NVGcolor textColor = bndGetTheme()->toolTheme.textColor;
		if (state == BND_ACTIVE) textColor = bndGetTheme()->toolTheme.textSelectedColor;

		nvgFontFaceId(args.vg, APP->window->uiFont->handle);

		if (node.isDirectory) {
			// Expand/collapse triangle
			nvgFontSize(args.vg, 9.f);
			nvgFillColor(args.vg, nvgRGBAf(textColor.r, textColor.g, textColor.b, 0.55f));
			nvgText(args.vg, textX, 13.f, node.childrenLoaded ? "▼" : "▶", nullptr);
			textX += 14.f;
			// Folder name
			nvgFontSize(args.vg, BND_LABEL_FONT_SIZE);
			nvgFillColor(args.vg, textColor);
			nvgScissor(args.vg, textX, 0.f, w - textX - 4.f, h);
			nvgText(args.vg, textX, 13.f, node.name.c_str(), nullptr);
			nvgResetScissor(args.vg);
		}
		else {
			const float starW   = 16.f;
			const float durW    = 44.f;
			float maxNameW = w - textX - durW - starW - 4.f;

			// File name
			nvgFontSize(args.vg, BND_LABEL_FONT_SIZE);
			nvgFillColor(args.vg, textColor);
			nvgScissor(args.vg, textX, 0.f, maxNameW, h);
			nvgText(args.vg, textX, 13.f, node.name.c_str(), nullptr);
			nvgResetScissor(args.vg);

			// Duration (right-aligned before star)
			if (node.durationSeconds > 0.f) {
				int mins = (int)(node.durationSeconds / 60.f);
				float secs = node.durationSeconds - mins * 60.f;
				std::string dur = rack::string::f("%02d:%05.2f", mins, secs);
				nvgFontSize(args.vg, 9.f);
				nvgFillColor(args.vg, nvgRGBAf(1.f, 1.f, 1.f, 0.32f));
				nvgText(args.vg, w - starW - durW - 1.f, 13.f, dur.c_str(), nullptr);
			}
			// Star drawn by StarButton child widget
		}
	}

	void onButton(const event::Button& e) override;

	// Path-drop drag
	void onDragStart(const event::DragStart& e) override;
	void onDragMove(const event::DragMove& e) override;
	void onDragEnd(const event::DragEnd& e) override;
};

// ─── browser pane ─────────────────────────────────────────────────────────────

struct SirenBrowserPane : widget::OpaqueWidget {
	// Callbacks set by the parent widget
	std::function<void(const std::string&)> onFileSelected;
	std::function<void()>    onAddRoot;
	std::function<void(int)> onRemoveRoot;
	std::function<void(int)> onSelectRoot;

	// Drag state (shared with preview pane via pointer in parent)
	SirenDragState* dragState = nullptr;
	std::string dragLabel;
	Vec dragPos;

	TaskWorker* worker = nullptr;

	// Root folders from settings
	std::vector<std::string> rootFolders;
	int activeRootIdx = -1;
	std::string selectedPath;
	bool favoritesOnly = false;
	std::set<std::string> tagFilter;

	// Hover tracking for tag chips
	std::string hoveredTag;

	// Active data source (owned; replaced on root change)
	DataSource* activeDataSource = nullptr;

	// Sub-widget
	SirenSourceButton* sourceBtn = nullptr;

	// Expanded tree state
	struct TreeEntry {
		DataSourceNode node;
		int indent    = 0;
		bool expanded = false;
	};
	std::vector<TreeEntry> rows;
	std::atomic<bool> loadPending{false};

	// Pending async result
	struct PendingResult {
		std::string parentPath;
		int insertIdx = -1;
		std::vector<DataSourceNode> nodes;
		int gen = -1;
		PendingResult() = default;
		PendingResult(std::string p, int idx, std::vector<DataSourceNode> n, int g)
			: parentPath(std::move(p)), insertIdx(idx), nodes(std::move(n)), gen(g) {}
	};
	std::atomic<int>  treeGeneration{0};
	PendingResult     pendingResult;
	std::atomic<bool> pendingReady{false};

	// Sub-widgets
	ui::ScrollWidget* scrollWidget  = nullptr;
	widget::Widget*   rowContainer  = nullptr;

	~SirenBrowserPane() override {
		delete activeDataSource;
	}

	void init(TaskWorker* tw) {
		worker = tw;

		sourceBtn = new SirenSourceButton;
		sourceBtn->box.pos  = Vec(0.f, 0.f);
		sourceBtn->box.size = Vec(100.f, BROWSER_HEADER_H);
		sourceBtn->pane = this;
		addChild(sourceBtn);

		scrollWidget = new ui::ScrollWidget;
		scrollWidget->box.pos = Vec(0.f, BROWSER_HEADER_H);
		addChild(scrollWidget);

		rowContainer = new widget::Widget;
		scrollWidget->container->addChild(rowContainer);
	}

	void setSize(Vec size) {
		box.size = size;
		if (sourceBtn) sourceBtn->box.size.x = size.x;
		scrollWidget->box.size = Vec(size.x, size.y - BROWSER_HEADER_H - BROWSER_TAG_H);
		rowContainer->box.size.x = size.x;
	}

	void setRoots(const std::vector<std::string>& roots, int activeIdx) {
		rootFolders   = roots;
		activeRootIdx = activeIdx;
		if (activeRootIdx >= 0 && activeRootIdx < (int)rootFolders.size())
			loadRoot(rootFolders[activeRootIdx]);
	}

	void loadRoot(const std::string& root) {
		delete activeDataSource;
		activeDataSource = new FileSystemDataSource(root);

		rows.clear();
		rebuildRowWidgets();
		pendingReady.store(false, std::memory_order_relaxed);
		int gen = ++treeGeneration;
		if (!worker) return;
		loadPending = true;
		activeDataSource->loadChildrenAsync(root, *worker, [this, root, gen](std::vector<DataSourceNode> nodes) {
			pendingResult = PendingResult(root, 0, std::move(nodes), gen);
			pendingReady.store(true, std::memory_order_release);
		});
	}

	void step() override {
		if (pendingReady.load(std::memory_order_acquire)) {
			pendingReady.store(false, std::memory_order_relaxed);
			if (pendingResult.gen == treeGeneration.load(std::memory_order_relaxed)) {
				loadPending = false;
				if (pendingResult.insertIdx == 0) {
					rows.clear();
					for (auto& n : pendingResult.nodes) {
						TreeEntry e; e.node = n; e.indent = 0;
						rows.push_back(e);
					}
				}
				else {
					int idx = pendingResult.insertIdx;
					std::vector<TreeEntry> newRows;
					for (auto& n : pendingResult.nodes) {
						TreeEntry e;
						e.node   = n;
						e.indent = rows[idx - 1].indent + 1;
						newRows.push_back(e);
					}
					rows.insert(rows.begin() + idx, newRows.begin(), newRows.end());
				}
				rebuildRowWidgets();
			}
		}
		OpaqueWidget::step();
	}

	void rebuildRowWidgets() {
		while (!rowContainer->children.empty())
			rowContainer->removeChild(rowContainer->children.front());

		RootMetadata* meta = activeDataSource ? activeDataSource->getMetadata() : nullptr;

		float y = 0.f;
		for (int i = 0; i < (int)rows.size(); i++) {
			const TreeEntry& entry = rows[i];
			const DataSourceNode& n = entry.node;

			if (favoritesOnly && !n.isDirectory && meta && !meta->isFavorite(n.relativePath))
				continue;

			if (!tagFilter.empty() && !n.isDirectory && meta) {
				auto tags = meta->getTags(n.relativePath);
				bool hasAll = true;
				for (const std::string& t : tagFilter)
					if (std::find(tags.begin(), tags.end(), t) == tags.end()) { hasAll = false; break; }
				if (!hasAll) continue;
			}

			SirenTreeRow* row = new SirenTreeRow;
			row->init(n, entry.indent, meta, this);
			row->selected     = (n.fullPath == selectedPath);
			row->box.pos      = Vec(0.f, y);
			row->box.size     = Vec(box.size.x, SirenTreeRow::ROW_H);
			rowContainer->addChild(row);
			y += SirenTreeRow::ROW_H;
		}
		rowContainer->box.size.y = y;
		scrollWidget->container->box.size = rowContainer->box.size;
	}

	void expandRow(int rowIdx) {
		if (rowIdx < 0 || rowIdx >= (int)rows.size()) return;
		TreeEntry& entry = rows[rowIdx];
		if (!entry.node.isDirectory) return;

		if (entry.expanded) {
			int childIndent = entry.indent + 1;
			int end = rowIdx + 1;
			while (end < (int)rows.size() && rows[end].indent >= childIndent) end++;
			rows.erase(rows.begin() + rowIdx + 1, rows.begin() + end);
			entry.expanded = false;
			rebuildRowWidgets();
			return;
		}

		entry.expanded = true;
		entry.node.childrenLoaded = false;
		rebuildRowWidgets();

		std::string path = entry.node.fullPath;
		int insertIdx    = rowIdx + 1;

		if (worker && activeDataSource) {
			int gen = treeGeneration.load(std::memory_order_relaxed);
			activeDataSource->loadChildrenAsync(path, *worker, [this, insertIdx, gen](std::vector<DataSourceNode> nodes) {
				pendingResult = PendingResult("", insertIdx, std::move(nodes), gen);
				pendingReady.store(true, std::memory_order_release);
			});
		}
	}

	int findRowIdx(SirenTreeRow* row) {
		int idx = 0;
		for (Widget* child : rowContainer->children) {
			if (dynamic_cast<SirenTreeRow*>(child) == row) return idx;
			idx++;
		}
		return -1;
	}

	int findTreeIdx(const std::string& fullPath) {
		for (int i = 0; i < (int)rows.size(); i++)
			if (rows[i].node.fullPath == fullPath) return i;
		return -1;
	}

	// Compute tag chip rects (shared by draw and hit-testing).
	// Keys are the original tag strings; display labels are title-cased separately.
	std::vector<std::pair<std::string, Rect>> tagChips() {
		std::vector<std::pair<std::string, Rect>> chips;
		const float chipH    = 18.f;
		const float rowStride = chipH + 6.f;
		const float startY   = box.size.y - BROWSER_TAG_H + 8.f;
		float x = 6.f;
		int   curRow = 0;

		RootMetadata* meta = activeDataSource ? activeDataSource->getMetadata() : nullptr;

		// Measure a label string using the blendish font/size
		auto measureChip = [](const std::string& label) -> float {
			if (!APP || !APP->window) return label.size() * 8.f + 20.f;
			NVGcontext* vg = APP->window->vg;
			nvgFontSize(vg, BND_LABEL_FONT_SIZE);
			nvgFontFaceId(vg, APP->window->uiFont->handle);
			float bounds[4];
			nvgTextBounds(vg, 0.f, 0.f, label.c_str(), nullptr, bounds);
			return bounds[2] - bounds[0] + 20.f;
		};

		// "Fav" chip is always first
		float favW = measureChip("Fav");
		chips.push_back({"fav", Rect(Vec(x, startY), Vec(favW, chipH))});
		x += favW + 4.f;

		if (meta) {
			auto all = meta->allTags();
			std::vector<std::string> sorted(all.begin(), all.end());
			std::sort(sorted.begin(), sorted.end());
			for (const std::string& tag : sorted) {
				std::string label = toTitleCase(tag);
				float tw = measureChip(label);
				if (x + tw > box.size.x - 4.f) {
					curRow++;
					if (curRow > 1) break;
					x = 6.f;
				}
				float chipY = startY + curRow * rowStride;
				chips.push_back({tag, Rect(Vec(x, chipY), Vec(tw, chipH))});
				x += tw + 4.f;
			}
		}
		return chips;
	}

	void onHover(const event::Hover& e) override {
		hoveredTag.clear();
		if (e.pos.y >= box.size.y - BROWSER_TAG_H) {
			for (auto& chip : tagChips()) {
				if (chip.second.contains(e.pos)) { hoveredTag = chip.first; break; }
			}
		}
		OpaqueWidget::onHover(e);
	}

	void onLeave(const event::Leave& e) override {
		hoveredTag.clear();
		OpaqueWidget::onLeave(e);
	}

	void onButton(const event::Button& e) override {
		if (e.button == GLFW_MOUSE_BUTTON_LEFT && e.action == GLFW_PRESS) {
			// Header: favorites star (far-right of header bar)
			if (e.pos.y < BROWSER_HEADER_H && e.pos.x > box.size.x - 24.f) {
				favoritesOnly = !favoritesOnly;
				rebuildRowWidgets();
				e.consume(this);
				return;
			}
			// Tag chips
			if (e.pos.y >= box.size.y - BROWSER_TAG_H) {
				for (auto& chip : tagChips()) {
					const std::string& tag = chip.first;
					const Rect& rect       = chip.second;
					if (rect.contains(e.pos)) {
						if (tag == "fav") {
							favoritesOnly = !favoritesOnly;
						} else {
							if (tagFilter.count(tag)) tagFilter.erase(tag);
							else tagFilter.insert(tag);
						}
						rebuildRowWidgets();
						e.consume(this);
						return;
					}
				}
			}
		}
		OpaqueWidget::onButton(e);
	}

	void draw(const DrawArgs& args) override {
		float w = box.size.x;
		float h = box.size.y;

		// Pane background
		nvgBeginPath(args.vg);
		nvgRect(args.vg, 0, 0, w, h);
		nvgFillColor(args.vg, nvgRGBf(0.12f, 0.12f, 0.09f));
		nvgFill(args.vg);

		// ── Header ────────────────────────────────────────────────────────────
		// (source button is a child widget, drawn by OpaqueWidget::draw below)

		// Favorites star toggle (drawn in the header, right of the source button)
		nvgFontFaceId(args.vg, APP->window->uiFont->handle);
		nvgFontSize(args.vg, 12.f);
		nvgFillColor(args.vg, favoritesOnly
			? nvgRGBf(1.f, 0.85f, 0.1f)
			: nvgRGBAf(1.f, 1.f, 1.f, 0.28f));
		nvgText(args.vg, w - 14.f, BROWSER_HEADER_H * 0.67f, favoritesOnly ? "★" : "☆", nullptr);

		// Separator
		nvgBeginPath(args.vg);
		nvgMoveTo(args.vg, 0, BROWSER_HEADER_H - 0.5f);
		nvgLineTo(args.vg, w, BROWSER_HEADER_H - 0.5f);
		nvgStrokeColor(args.vg, nvgRGBAf(1.f, 1.f, 1.f, 0.09f));
		nvgStrokeWidth(args.vg, 0.5f);
		nvgStroke(args.vg);

		// ── Loading indicator ─────────────────────────────────────────────────
		if (loadPending) {
			nvgFontSize(args.vg, 9.f);
			nvgFillColor(args.vg, nvgRGBAf(1.f, 1.f, 1.f, 0.30f));
			nvgText(args.vg, 8.f, BROWSER_HEADER_H + 14.f, "Loading...", nullptr);
		}

		OpaqueWidget::draw(args);

		// ── Tag chips (drawn over scroll content) ─────────────────────────────
		const float tagAreaY = h - BROWSER_TAG_H;

		nvgBeginPath(args.vg);
		nvgRect(args.vg, 0, tagAreaY, w, BROWSER_TAG_H);
		nvgFillColor(args.vg, nvgRGBf(0.09f, 0.09f, 0.07f));
		nvgFill(args.vg);

		nvgBeginPath(args.vg);
		nvgMoveTo(args.vg, 0, tagAreaY + 0.5f);
		nvgLineTo(args.vg, w, tagAreaY + 0.5f);
		nvgStrokeColor(args.vg, nvgRGBAf(1.f, 1.f, 1.f, 0.09f));
		nvgStrokeWidth(args.vg, 0.5f);
		nvgStroke(args.vg);

		// Tag chips: blendish tool buttons with title-case labels
		for (auto& chip : tagChips()) {
			const std::string& tag = chip.first;
			const Rect& rect       = chip.second;
			bool active = (tag == "fav") ? favoritesOnly : (tagFilter.count(tag) > 0);

			BNDwidgetState chipState = BND_DEFAULT;
			if (active) chipState = BND_ACTIVE;
			else if (hoveredTag == tag) chipState = BND_HOVER;

			std::string label = (tag == "fav") ? "Fav" : toTitleCase(tag);

			bndToolButton(args.vg, rect.pos.x, rect.pos.y, rect.size.x, rect.size.y,
			              BND_CORNER_NONE, chipState, -1, label.c_str());
		}

		// Drag floating label
		if (dragState && dragState->active && !dragLabel.empty()) {
			nvgBeginPath(args.vg);
			nvgRoundedRect(args.vg, dragPos.x - box.pos.x, dragPos.y - box.pos.y, 140.f, 18.f, 3.f);
			nvgFillColor(args.vg, nvgRGBAf(0.f, 0.f, 0.f, 0.7f));
			nvgFill(args.vg);
			nvgFontSize(args.vg, 10.f);
			nvgFillColor(args.vg, nvgRGBf(1.f, 0.85f, 0.1f));
			nvgText(args.vg, dragPos.x - box.pos.x + 4.f, dragPos.y - box.pos.y + 12.f,
				dragLabel.c_str(), nullptr);
		}
	}
};

// ─── SirenTreeRow method bodies (need full SirenBrowserPane) ─────────────────

inline void SirenTreeRow::onButton(const event::Button& e) {
	if (e.button == GLFW_MOUSE_BUTTON_LEFT && e.action == GLFW_PRESS) {
		if (node.isDirectory) {
			int treeIdx = pane->findTreeIdx(node.fullPath);
			if (treeIdx >= 0) pane->expandRow(treeIdx);
		}
		else {
			pane->selectedPath = node.fullPath;
			pane->rebuildRowWidgets();
			if (pane->onFileSelected) pane->onFileSelected(node.fullPath);
		}
		e.consume(this);
	}
	if (e.button == GLFW_MOUSE_BUTTON_RIGHT && e.action == GLFW_PRESS) {
		if (pane->activeDataSource) {
			ui::Menu* menu = createMenu();
			pane->activeDataSource->appendNodeMenuItems(menu, node);
			e.consume(this);
		}
	}
}

inline void SirenTreeRow::onDragStart(const event::DragStart& e) {
	if (node.isDirectory) return;
	if (pane && pane->dragState) {
		pane->dragState->active  = true;
		pane->dragState->dragPath = node.fullPath;
		pane->dragLabel = node.name;
	}
}

inline void SirenTreeRow::onDragMove(const event::DragMove& e) {
	if (pane && pane->dragState && pane->dragState->active)
		pane->dragPos = APP->scene->mousePos;
}

inline void SirenTreeRow::onDragEnd(const event::DragEnd& e) {
	if (!pane || !pane->dragState || !pane->dragState->active) return;
	pane->dragState->active = false;
	pane->dragLabel.clear();

	Vec pos = APP->scene->mousePos;
	Widget::PathDropEvent pd(std::vector<std::string>{pane->dragState->dragPath});
	pd.pos = pos;
	APP->scene->onPathDrop(pd);
	pane->dragState->dragPath.clear();
}

// ─── SirenSourceButton method bodies ─────────────────────────────────────────

inline void SirenSourceButton::step() {
	box.size.x = parent ? parent->box.size.x : 100.f;

	if (!pane->rootFolders.empty() && pane->activeRootIdx >= 0 &&
	    pane->activeRootIdx < (int)pane->rootFolders.size()) {
		ghc::filesystem::path p(pane->rootFolders[pane->activeRootIdx]);
		text = p.filename().string();
		if (text.empty()) text = p.string();
	}
	else {
		text = "No source";
	}
	text = string::ellipsize(text, 40);
	ChoiceButton::step();
}

inline void SirenSourceButton::onAction(const event::Action& e) {
	ui::Menu* menu = createMenu();
	menu->box.pos   = getAbsoluteOffset(math::Vec(0, box.size.y));
	menu->box.size.x = box.size.x;

	// List of existing sources
	for (int i = 0; i < (int)pane->rootFolders.size(); i++) {
		ghc::filesystem::path p(pane->rootFolders[i]);
		std::string name = p.filename().string();
		if (name.empty()) name = p.string();
		menu->addChild(createCheckMenuItem(name, "",
			[this, i]() { return i == pane->activeRootIdx; },
			[this, i]() { if (pane->onSelectRoot) pane->onSelectRoot(i); }
		));
	}

	menu->addChild(new ui::MenuSeparator);
	menu->addChild(createMenuItem("Add folder...", "", [this]() {
		if (pane->onAddRoot) pane->onAddRoot();
	}));
	menu->addChild(createMenuItem("Remove source", "", [this]() {
		if (pane->onRemoveRoot) pane->onRemoveRoot(pane->activeRootIdx);
	}, pane->rootFolders.empty()));
}

} // namespace Siren
} // namespace StoermelderPackOne
