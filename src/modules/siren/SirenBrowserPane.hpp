#pragma once
#include <rack.hpp>
#include "Siren.hpp"
#include "SirenDataSource.hpp"
#include "SirenFileSystem.hpp"
#include "SirenMetadata.hpp"
#include "SirenDropHandler.hpp"
#include "SirenTagClassifierApi.hpp"
#include "../../utils/TaskWorker.hpp"
#include "../../ui/AutoTagDialog.hpp"


namespace StoermelderPackOne {
namespace Siren {

// Forward declarations
struct SirenBrowserPane;

static constexpr float BROWSER_TAG_H = 96.f;

// ─── single row in the tree ───────────────────────────────────────────────────

struct SirenTreeRow : widget::OpaqueWidget {
	static constexpr float ROW_H   = 20.f;
	static constexpr float INDENT  = 14.f;

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

	DataSourceNode node;
	int indentLevel = 0;
	RootMetadata* metadata = nullptr;
	SirenBrowserPane* pane = nullptr;
	bool selected = false;

	StarButton* starBtn = nullptr;

	void init(const DataSourceNode& n, int indent, RootMetadata* meta, SirenBrowserPane* p) {
		node = n;
		indentLevel = indent;
		metadata = meta;
		pane = p;
		box.size = Vec(0.f, ROW_H);

		if (!node.isContainer) {
			starBtn = new StarButton;
			starBtn->row = this;
			starBtn->box.pos = Vec(0.f, 2.f);
			starBtn->box.size = Vec(14.f, 16.f);
			addChild(starBtn);
		}
	}

	void step() override {
		// Keep star button flush against the right edge
		if (starBtn) {
			starBtn->box.pos.x = box.size.x - 16.f;
		}
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

		if (node.isContainer) {
			// Expand/collapse triangle
			nvgFontSize(args.vg, 9.f);
			nvgFillColor(args.vg, nvgRGBAf(textColor.r, textColor.g, textColor.b, 0.55f));
			nvgText(args.vg, textX, 13.f, node.childrenLoaded ? "▼" : "▶", nullptr);
			textX += 14.f;
			// Container name
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
		OpaqueWidget::draw(args);
	}

	void onButton(const event::Button& e) override;

	// Path-drop drag
	void onDragStart(const event::DragStart& e) override;
	void onDragEnd(const event::DragEnd& e) override;
};

struct SirenBrowserPane : widget::OpaqueWidget {
	// Callbacks set by the parent widget
	std::function<void(const std::string&, bool /*startPlay*/)> onFileSelected;
	std::function<void()>    onAddRoot;
	std::function<void(int)> onRemoveRoot;
	std::function<void(int)> onSelectRoot;

	// Drop handler (shared with preview pane via pointer in parent)
	SirenDropHandler* dropHandler = nullptr;

	TaskWorker* worker = nullptr;

	// Root containers from settings
	std::vector<std::string> rootContainers;
	int activeRootIdx = -1;
	std::string selectedPath;
	bool favoritesOnly = false;
	std::set<std::string> tagFilter;

	// Hover tracking for tag chips
	std::string hoveredTag;
	std::string searchQuery;

	// Active data source (owned; replaced on root change)
	DataSource* activeDataSource = nullptr;

	// Expanded tree state
	struct TreeEntry {
		DataSourceNode node;
		int indent    = 0;
		bool expanded = false;
	};
	std::vector<TreeEntry> rows;
	std::atomic<bool> loadPending{false};

	// When non-empty: after the next async load completes, select the first child of this path
	std::string pendingSelectFirstOfPath;

	// Deferred rebuild — set from event handlers; consumed by step() to avoid
	// deleting widgets while handleButton/handleKey is still on the call stack.
	bool rebuildDirty = false;
	bool scrollAfterRebuild = false;

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

		scrollWidget = new ui::ScrollWidget;
		scrollWidget->box.pos = Vec(0.f, 0.f);
		addChild(scrollWidget);

		rowContainer = new widget::Widget;
		scrollWidget->container->addChild(rowContainer);
	}

	void setSize(Vec size) {
		box.size = size;
		scrollWidget->box.size = Vec(size.x, size.y - BROWSER_TAG_H);
		rowContainer->box.size.x = size.x;
	}

	void setRoots(const std::vector<std::string>& roots, int activeIdx) {
		rootContainers   = roots;
		activeRootIdx = activeIdx;
		if (activeRootIdx >= 0 && activeRootIdx < (int)rootContainers.size()) {
			loadRoot(rootContainers[activeRootIdx]);
		}
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

				// Auto-select first child when navigating right into a folder
				if (!pendingSelectFirstOfPath.empty()) {
					std::string parentPath = pendingSelectFirstOfPath;
					pendingSelectFirstOfPath.clear();
					auto vr = visibleRowWidgets();
					for (int i = 0; i < (int)vr.size() - 1; i++) {
						if (vr[i]->node.fullPath == parentPath) {
							selectPath(vr[i + 1]->node);
							break;
						}
					}
				}
			}
		}
		if (dropHandler) dropHandler->step();

		// Flush deferred rebuilds requested by event handlers
		if (rebuildDirty) {
			rebuildDirty = false;
			bool doScroll = scrollAfterRebuild;
			scrollAfterRebuild = false;
			rebuildRowWidgets();
			if (doScroll) scrollToSelected();
		}

		OpaqueWidget::step();
	}

	void requestRebuild() {
		rebuildDirty = true;
	}

	void rebuildRowWidgets() {
		rowContainer->clearChildren();
		RootMetadata* meta = activeDataSource ? activeDataSource->getMetadata() : nullptr;

		float y = 0.f;
		for (int i = 0; i < (int)rows.size(); i++) {
			const TreeEntry& entry = rows[i];
			const DataSourceNode& n = entry.node;

			// When searching, hide containers and filter by name
			if (!searchQuery.empty()) {
				if (n.isContainer) continue;
				if (rack::string::lowercase(n.name).find(rack::string::lowercase(searchQuery)) == std::string::npos)
					continue;
			}

			if (n.isContainer) {
				if ((favoritesOnly || !tagFilter.empty()) && !containerHasMatchingDescendant(i, meta))
					continue;
			}
			else {
				if (favoritesOnly && meta && !meta->isFavorite(n.relativePath))
					continue;

				if (!tagFilter.empty() && meta) {
					auto tags = meta->getTags(n.relativePath);
					bool hasAll = true;
					for (const std::string& t : tagFilter)
						if (std::find(tags.begin(), tags.end(), t) == tags.end()) { hasAll = false; break; }
					if (!hasAll) continue;
				}
			}

			SirenTreeRow* row = new SirenTreeRow;
			row->init(n, entry.indent, meta, this);
			row->selected     = (n.fullPath == selectedPath);
			row->box.pos      = Vec(0.f, y);
			row->box.size     = Vec(box.size.x - 12.f, SirenTreeRow::ROW_H);
			rowContainer->addChild(row);
			y += SirenTreeRow::ROW_H;
		}
		rowContainer->box.size.y = y;
		scrollWidget->container->box.size = rowContainer->box.size;
	}

	void expandRow(int rowIdx) {
		if (rowIdx < 0 || rowIdx >= (int)rows.size()) return;
		TreeEntry& entry = rows[rowIdx];
		if (!entry.node.isContainer) return;

		if (entry.expanded) {
			int childIndent = entry.indent + 1;
			int end = rowIdx + 1;
			while (end < (int)rows.size() && rows[end].indent >= childIndent) end++;
			rows.erase(rows.begin() + rowIdx + 1, rows.begin() + end);
			entry.expanded = false;
			requestRebuild();
			return;
		}

		entry.expanded = true;
		entry.node.childrenLoaded = false;
		requestRebuild();

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

	// Returns true if the container at rows[rowIdx] has at least one descendant
	// file that passes the current favoritesOnly / tagFilter.
	//
	// Only files with metadata entries (tags or favorites) can ever match either
	// filter, so scanning meta->samples is both necessary and sufficient — no
	// filesystem scan or row-tree walk needed.
	bool containerHasMatchingDescendant(int rowIdx, RootMetadata* meta) const {
		if (!meta) return false;
		const std::string dirPrefix = rows[rowIdx].node.relativePath + "/";

		for (const auto& pair : meta->samples) {
			if (pair.first.compare(0, dirPrefix.size(), dirPrefix) != 0) continue;
			const SampleMetadata& sm = pair.second;

			if (favoritesOnly && !sm.favorite) continue;

			if (!tagFilter.empty()) {
				bool hasAll = true;
				for (const std::string& t : tagFilter)
					if (std::find(sm.tags.begin(), sm.tags.end(), t) == sm.tags.end()) { hasAll = false; break; }
				if (!hasAll) continue;
			}

			return true;
		}
		return false;
	}

	int findTreeIdx(const std::string& fullPath) {
		for (int i = 0; i < (int)rows.size(); i++)
			if (rows[i].node.fullPath == fullPath) return i;
		return -1;
	}

	// ── Keyboard navigation ───────────────────────────────────────────────────

	std::vector<SirenTreeRow*> visibleRowWidgets() {
		std::vector<SirenTreeRow*> result;
		for (Widget* w : rowContainer->children)
			if (auto* r = dynamic_cast<SirenTreeRow*>(w))
				result.push_back(r);
		return result;
	}

	void scrollToSelected() {
		for (Widget* w : rowContainer->children) {
			auto* r = dynamic_cast<SirenTreeRow*>(w);
			if (!r || !r->selected) continue;
			float rowTop = r->box.pos.y;
			float rowBot = rowTop + r->box.size.y;
			float viewH  = scrollWidget->box.size.y;
			if (rowTop < scrollWidget->offset.y)
				scrollWidget->offset.y = rowTop;
			else if (rowBot > scrollWidget->offset.y + viewH)
				scrollWidget->offset.y = rowBot - viewH;
			break;
		}
	}

	// Select a row by node, schedule rebuild+scroll, and fire onFileSelected for files.
	// Uses node.fullPath for internal tree state, node.relativePath for the external callback.
	void selectPath(const DataSourceNode& node, bool startPlay = false) {
		selectedPath = node.fullPath;
		requestRebuild();
		scrollAfterRebuild = true;
		if (!node.isContainer && onFileSelected) {
			onFileSelected(node.relativePath, startPlay);
		}
	}

	// Returns true if the key was handled.
	bool navigateKey(int key) {
		auto vr = visibleRowWidgets();
		if (vr.empty()) return false;

		// Find index of currently selected row in the visible list
		int selIdx = -1;
		for (int i = 0; i < (int)vr.size(); i++) {
			if (vr[i]->selected) { selIdx = i; break; }
		}

		if (key == GLFW_KEY_UP) {
			int target = (selIdx <= 0) ? 0 : selIdx - 1;
			selectPath(vr[target]->node);
			return true;
		}

		if (key == GLFW_KEY_DOWN) {
			int target = (selIdx < 0) ? 0 : std::min(selIdx + 1, (int)vr.size() - 1);
			selectPath(vr[target]->node);
			return true;
		}

		if (key == GLFW_KEY_RIGHT) {
			if (selIdx < 0) return true;
			SirenTreeRow* row = vr[selIdx];
			if (!row->node.isContainer) return true;

			int treeIdx = findTreeIdx(row->node.fullPath);
			if (treeIdx < 0) return true;

			if (!rows[treeIdx].expanded) {
				// Expand and schedule auto-select of first child once loaded
				pendingSelectFirstOfPath = row->node.fullPath;
				expandRow(treeIdx);
			}
			else {
				// Already expanded — move into first visible child
				if (selIdx + 1 < (int)vr.size()) {
					selectPath(vr[selIdx + 1]->node);
				}
			}
			return true;
		}

		if (key == GLFW_KEY_LEFT) {
			if (selIdx < 0) return true;
			SirenTreeRow* row = vr[selIdx];

			// If this is an expanded container, collapse it
			if (row->node.isContainer) {
				int treeIdx = findTreeIdx(row->node.fullPath);
				if (treeIdx >= 0 && rows[treeIdx].expanded) {
					expandRow(treeIdx);  // toggles — collapses it
					return true;
				}
			}

			// Move to parent container: find nearest ancestor in rows
			int treeIdx = findTreeIdx(row->node.fullPath);
			if (treeIdx < 0) return true;
			int parentIndent = rows[treeIdx].indent - 1;
			if (parentIndent < 0) return true;  // already at root level

			for (int i = treeIdx - 1; i >= 0; i--) {
				if (rows[i].indent == parentIndent && rows[i].node.isContainer) {
					// Collapse parent and select it
					expandRow(i);
					selectedPath = rows[i].node.fullPath;
					requestRebuild();
					scrollAfterRebuild = true;
					return true;
				}
			}
			return true;
		}

		return false;
	}

	// Compute tag chip rects (shared by draw and hit-testing).
	// Keys are the original tag strings; display labels are title-cased separately.
	std::vector<std::pair<std::string, Rect>> tagChips() {
		std::vector<std::pair<std::string, Rect>> chips;
		const float chipH    = 20.f;
		const float rowStride = chipH + 2.f;
		const float startY   = box.size.y - BROWSER_TAG_H + 8.f;
		float x = 4.f;
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
		float favW = measureChip("Favorite");
		chips.push_back({"fav", Rect(Vec(x, startY), Vec(favW, chipH))});
		x += favW + 4.f;

		if (meta) {
			auto all = meta->allTags();
			std::vector<std::string> sorted(all.begin(), all.end());
			std::sort(sorted.begin(), sorted.end());
			for (const std::string& tag : sorted) {
				const std::string& label = tag;
				float tw = measureChip(label);
				if (x + tw > box.size.x - 4.f) {
					curRow++;
					if (curRow > 3) break;
					x = 4.f;
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
						requestRebuild();
						e.consume(this);
						return;
					}
				}
			}
		}
		OpaqueWidget::onButton(e);
	}

	void onSelectKey(const SelectKeyEvent& e) override {
		if (e.action == GLFW_PRESS || e.action == GLFW_REPEAT) {
			if (e.key == GLFW_KEY_SPACE) {
				ModuleWidget* mw = getAncestorOfType<ModuleWidget>();
				mw->onSelectKey(e);
				return;
			}
			if (e.key == GLFW_KEY_UP || e.key == GLFW_KEY_DOWN || e.key == GLFW_KEY_LEFT || e.key == GLFW_KEY_RIGHT) {
				if (navigateKey(e.key)) {
					e.consume(this);
					return;
				}
			}
		}
		OpaqueWidget::onSelectKey(e);
	}

	void draw(const DrawArgs& args) override {
		float w = box.size.x;
		float h = box.size.y;

		// Pane background
		nvgBeginPath(args.vg);
		nvgRect(args.vg, 0, 0, w, h);
		nvgFillColor(args.vg, nvgRGBf(0.12f, 0.12f, 0.09f));
		nvgFill(args.vg);

		// ── Loading indicator ─────────────────────────────────────────────────
		if (loadPending) {
			nvgFontSize(args.vg, 9.f);
			nvgFillColor(args.vg, nvgRGBAf(1.f, 1.f, 1.f, 0.30f));
			nvgText(args.vg, 8.f, 14.f, "Loading...", nullptr);
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

			std::string label = (tag == "fav") ? "Favorite" : tag;

			bndToolButton(args.vg, rect.pos.x, rect.pos.y, rect.size.x, rect.size.y,
			              BND_CORNER_NONE, chipState, -1, label.c_str());
		}
	}

	void startTagClassification(const DataSourceNode& node) {
		if (!worker || !activeDataSource) return;
		DataSource*   ds    = activeDataSource;
		RootMetadata* meta  = ds->getMetadata();
		std::string   fp    = node.fullPath;
		std::string   rel   = node.relativePath;
		bool          isDir = node.isContainer;
		std::string   name  = node.name;

		ui::MenuOverlay* loadingOverlay = new ui::MenuOverlay;
		loadingOverlay->bgColor = nvgRGBAf(0.f, 0.f, 0.f, 0.5f);
		APP->scene->addChild(loadingOverlay);

		// Payload type carried by the per-tag sample labels inside the "Suggest tags"
		// dialog. Held at namespace scope so the worker thread, the label builder, and
		// the apply callback can all spell the type identically.
		using DataSourceNodeId = std::string;

		auto buildLabel = [this, ds](const std::string& tag, const DataSourceNodeId& fileId) -> widget::Widget* {
			struct SampleLabel : ui::MenuItem {
				DataSource* ds;
				SirenBrowserPane* pane;
				DataSourceNodeId fileId;
				std::string groupTag;
				void onAction(const event::Action& e) override {
					pane->onFileSelected(fileId, true);
					e.unconsume();  // keep dialog open
				}
				void onButton(const ButtonEvent& e) override {
					if (e.action == GLFW_PRESS && e.button == GLFW_MOUSE_BUTTON_RIGHT) {
						Menu* menu = createMenu();
						menu->addChild(createMenuLabel(ds->getDisplayName(fileId)));
						// Find the owning dialog so we can mutate its groups vector
						auto* dlg = getAncestorOfType<ui::TagConfirmDialog<DataSourceNodeId>>();
						SampleLabel* self = this;
						menu->addChild(createMenuItem("Remove from group", "", [self, dlg]() {
							if (!dlg) { self->requestDelete(); return; }
							for (auto& g : dlg->groups) {
								if (g.tag == self->groupTag) {
									g.payloads.erase(self->fileId);
									break;
								}
							}
							dlg->updateSummary();
							self->requestDelete();
						}));
						e.consume(this);
						return;
					}
					MenuItem::onButton(e);
				}
			};

			SampleLabel* item = new SampleLabel;
			item->text     = ds->getDisplayName(fileId);
			item->ds	   = ds;
			item->pane     = this;
			item->fileId   = fileId;
			item->groupTag = tag;
			return item;
		};

		auto applyFn = [this, meta, ds](const std::map<std::string, std::set<DataSourceNodeId>>& filtered) {
			for (const auto& pair : filtered)
				for (const std::string& r : pair.second)
					meta->addTag(r, pair.first);
			if (ds) ds->saveMetadata();
			requestRebuild();
		};

		auto summaryFn = [isDir, name](int sel, int items) -> std::string {
			if (isDir)
				return rack::string::f("%d tag%s across %d file%s",
					sel, sel == 1 ? "" : "s", items, items == 1 ? "" : "s");
			return rack::string::f("%d tag%s for %s",
				sel, sel == 1 ? "" : "s", name.c_str());
		};

		std::string header = isDir ? "Suggest tags — " + name : "Suggest tags";
		using AsyncDlg = StoermelderPackOne::ui::AsyncTagConfirmDialog<DataSourceNodeId>;
		AsyncDlg* asyncWidget = new AsyncDlg(loadingOverlay, buildLabel, applyFn, header, summaryFn);
		APP->scene->addChild(asyncWidget);

		worker->work([asyncWidget, fp, rel, isDir, ds]() {
			std::vector<DataSourceNodeId> files;
			if (isDir) {
				std::function<void(const std::string&)> collect = [&](const std::string& path) {
					for (const auto& child : ds->loadChildrenSync(path)) {
						if (child.isContainer) collect(child.fullPath);
						else files.push_back(child.relativePath);
					}
				};
				collect(fp);
			}
			else {
				files = {rel};
			}

			std::map<std::string, std::set<DataSourceNodeId>> tagToRels;
			for (const auto& f : files) {
				auto stream = ds->openAudioStream(f);
				if (!stream) continue;
				auto suggestions = TagClassifier::classify(*stream, 5);
				for (const auto& s : suggestions)
					if (s.score >= 0.5f)
						tagToRels[s.name].insert(f);
			}

			using GroupVec = std::vector<StoermelderPackOne::ui::TagGroup<DataSourceNodeId>>;
			auto groups = std::make_shared<GroupVec>();
			for (auto& pair : tagToRels)
				groups->push_back({pair.first, pair.second});
			asyncWidget->result = groups;
		});
	}
};

inline void SirenTreeRow::onButton(const event::Button& e) {
	// Manually handle star button, because of OpaqueWidget
	if (starBtn && starBtn->box.contains(e.pos)) {
		starBtn->onButton(e);
		return;
	}

	if (e.button == GLFW_MOUSE_BUTTON_LEFT) {
		if (e.action == GLFW_PRESS) {
			if (node.isContainer) {
				int treeIdx = pane->findTreeIdx(node.fullPath);
				if (treeIdx >= 0) pane->expandRow(treeIdx);
			}
			else {
				bool shift = (e.mods & GLFW_MOD_SHIFT) != 0;
				pane->selectPath(node, shift);
			}
		}
		if (e.action == GLFW_RELEASE) {
			// Set the selected widget for key events
			Widget* w = getAncestorOfType<ModuleWidget>();
			APP->event->setSelectedWidget(w);
		}
		e.consume(this);
	}
	if (e.button == GLFW_MOUSE_BUTTON_RIGHT && e.action == GLFW_PRESS) {
		if (pane->activeDataSource) {
			ui::Menu* menu = createMenu();
			SirenBrowserPane* p = pane;
			pane->activeDataSource->appendNodeMenuItems(menu, node, [p]() {
				p->rebuildRowWidgets();
			});

			DataSourceNode nodeCopy = node;
			menu->addChild(new ui::MenuSeparator);
			menu->addChild(createMenuItem("Suggest tags", "", [p, nodeCopy]() {
				p->startTagClassification(nodeCopy);
			}));

			e.consume(this);
		}
	}
}

inline void SirenTreeRow::onDragStart(const event::DragStart& e) {
	if (node.isContainer) return;
	if (pane && pane->dropHandler)
		pane->dropHandler->startDrag(node.relativePath);
}

inline void SirenTreeRow::onDragEnd(const event::DragEnd& e) {
	if (!pane || !pane->dropHandler || !pane->dropHandler->active) return;
	pane->dropHandler->endDrag(APP->scene->mousePos, pane->worker);
}

} // namespace Siren
} // namespace StoermelderPackOne
