#pragma once
#include <rack.hpp>
#include "SirenSettings.hpp"
#include "SirenDataSource.hpp"
#include "SirenFileSystem.hpp"
#include "SirenMetadata.hpp"
#include "SirenDropHandler.hpp"
#include "SirenTagClassifierApi.hpp"
#include "SirenBpmDetector.hpp"
#include "SirenBackgroundTasks.hpp"
#include "SirenDummyPreview.hpp"
#include "../../utils/TaskWorker.hpp"
#include "../../ui/AutoTagDialog.hpp"


namespace StoermelderPackOne {
namespace Siren {

// Forward declarations
struct SirenBrowserPane;
struct SirenTagBar;

static constexpr float BROWSER_TAG_H = 58.f;

// single row in the tree
struct SirenTreeRow : widget::OpaqueWidget {
	static constexpr float ROW_H = 12.f;
	static constexpr float INDENT = 8.f;

	// Favorite star button (file rows only)
	struct StarButton : widget::OpaqueWidget {
		SirenTreeRow* row = nullptr;
		void draw(const DrawArgs& args) override {
			MetadataStore* meta = row ? row->metadata() : nullptr;
			if (!meta) return;
			bool fav = meta->isFavorite(row->node.relativePath);
			nvgFontSize(args.vg, 7.f);
			nvgFillColor(args.vg, fav
				? nvgRGBf(1.f, 0.85f, 0.1f)
				: nvgRGBAf(1.f, 1.f, 1.f, 0.22f));
			nvgText(args.vg, 0.f, 7.f, fav ? "★" : "☆", nullptr);
		}
		void onButton(const event::Button& e) override {
			if (e.button == GLFW_MOUSE_BUTTON_LEFT && e.action == GLFW_PRESS) {
				if (MetadataStore* meta = row ? row->metadata() : nullptr) {
					bool fav = meta->isFavorite(row->node.relativePath);
					meta->setFavorite(row->node.relativePath, !fav);
				}
				e.consume(this);
			}
		}
	};

	DataSourceNode node;
	int indentLevel = 0;
	SirenBrowserPane* pane = nullptr;
	bool selected = false;
	bool expanded = false;

	StarButton* starBtn = nullptr;
	ui::Tooltip* tooltip = nullptr;

	~SirenTreeRow() override {
		setTooltip(nullptr);
	}

	void setTooltip(ui::Tooltip* t) {
		if (tooltip) { tooltip->requestDelete(); tooltip = nullptr; }
		if (t) { APP->scene->addChild(t); tooltip = t; }
	}

	// Metadata is owned by the active DataSource — derive it through `pane`
	// rather than caching a copy that could fall out of sync when the source changes.
	// Defined out-of-line below since SirenBrowserPane is only forward-declared here.
	MetadataStore* metadata() const;

	void onEnter(const event::Enter& e) override {
		if (!node.isContainer) {
			std::string text = node.name;
			if (node.durationSeconds > 0.f) {
				int mins = (int)(node.durationSeconds / 60.f);
				float secs = node.durationSeconds - mins * 60.f;
				text += rack::string::f("\n%02d:%05.2f", mins, secs);
			}
			if (MetadataStore* meta = metadata()) {
				auto tags = meta->getTags(node.relativePath);
				if (!tags.empty()) {
					text += "\n";
					for (const std::string& tag : tags) {
						text += "  " + tag;
					}
				}
			}
			ui::Tooltip* t = new ui::Tooltip;
			t->text = std::move(text);
			setTooltip(t);
		}
		OpaqueWidget::onEnter(e);
	}

	void onLeave(const event::Leave& e) override {
		setTooltip(nullptr);
		OpaqueWidget::onLeave(e);
	}

	void init(const DataSourceNode& n, int indent, SirenBrowserPane* p) {
		node = n;
		indentLevel = indent;
		pane = p;
		box.size = Vec(0.f, ROW_H);

		if (!node.isContainer) {
			starBtn = new StarButton;
			starBtn->row = this;
			starBtn->box.pos = Vec(0.f, 1.f);
			starBtn->box.size = Vec(8.f, 10.f);
			addChild(starBtn);
		}
	}

	void step() override {
		if (starBtn) {
			starBtn->box.pos.x = box.size.x - 10.f;
		}
		widget::OpaqueWidget::step();
	}

	void draw(const DrawArgs& args) override {
		float w = box.size.x;
		float h = ROW_H;

		BNDwidgetState state = BND_DEFAULT;
		if (selected) {
			state = BND_ACTIVE;
		}
		else if (APP->event->getHoveredWidget() == this) {
			state = BND_HOVER;
		}

		if (state != BND_DEFAULT) {
			nvgBeginPath(args.vg);
			nvgRect(args.vg, 0, 0, w, h);
			if (state == BND_ACTIVE) {
				nvgFillColor(args.vg, bndGetTheme()->toolTheme.innerSelectedColor);
			}
			else {
				NVGcolor c = bndGetTheme()->toolTheme.innerColor;
				c.a *= 0.5f;
				nvgFillColor(args.vg, c);
			}
			nvgFill(args.vg);
		}

		float textX = 6.f + indentLevel * INDENT;
		NVGcolor textColor = bndGetTheme()->toolTheme.textColor;
		if (state == BND_ACTIVE) textColor = bndGetTheme()->toolTheme.textSelectedColor;

		nvgFontFaceId(args.vg, APP->window->uiFont->handle);

		if (node.isContainer) {
			nvgFontSize(args.vg, 6.f);
			nvgFillColor(args.vg, nvgRGBAf(textColor.r, textColor.g, textColor.b, 0.55f));
			nvgText(args.vg, textX, 8.f, expanded ? "▼" : "▶", nullptr);
			textX += 8.f;
			nvgFontSize(args.vg, 7.7f);
			nvgFillColor(args.vg, textColor);
			nvgSave(args.vg);
			nvgIntersectScissor(args.vg, textX, 0.f, w - textX - 4.f, h);
			nvgText(args.vg, textX, 8.f, node.name.c_str(), nullptr);
			nvgRestore(args.vg);
		}
		else {
			const float starW = 10.f;
			const float durW = 26.f;
			float maxNameW = w - textX - durW - starW - 4.f;

			nvgFontSize(args.vg, 7.7f);
			nvgFillColor(args.vg, textColor);
			nvgSave(args.vg);
			nvgIntersectScissor(args.vg, textX, 0.f, maxNameW, h);
			nvgText(args.vg, textX, 8.f, node.name.c_str(), nullptr);
			nvgRestore(args.vg);

			if (node.durationSeconds > 0.f) {
				int mins = (int)(node.durationSeconds / 60.f);
				float secs = node.durationSeconds - mins * 60.f;
				std::string dur = rack::string::f("%02d:%05.2f", mins, secs);
				std::shared_ptr<Font> font = APP->window->loadFont(asset::system("res/fonts/ShareTechMono-Regular.ttf"));
				nvgFontFaceId(args.vg, font->handle);
				nvgFontSize(args.vg, 6.f);
				nvgFillColor(args.vg, nvgRGBAf(1.f, 1.f, 1.f, 0.42f));
				nvgText(args.vg, w - starW - durW - 1.f, 8.f, dur.c_str(), nullptr);
			}
		}
		OpaqueWidget::draw(args);
	}

	void onButton(const event::Button& e) override;
	void onDragStart(const event::DragStart& e) override;
	void onDragEnd(const event::DragEnd& e) override;
};

// Placeholder row shown below an expanded container while its children are
// being fetched asynchronously.
struct SirenLoadingRow : widget::OpaqueWidget {
	int indentLevel = 0;

	void draw(const DrawArgs& args) override {
		float textX = 6.f + indentLevel * SirenTreeRow::INDENT;
		NVGcolor textColor = bndGetTheme()->toolTheme.textColor;
		nvgFontFaceId(args.vg, APP->window->uiFont->handle);
		nvgFontSize(args.vg, 7.7f);
		nvgFillColor(args.vg, nvgRGBAf(textColor.r, textColor.g, textColor.b, 0.45f));
		nvgText(args.vg, textX, 8.f, "Loading\xe2\x80\xa6", nullptr);
		OpaqueWidget::draw(args);
	}
};

// ScrollWidget with a narrower vertical scrollbar
struct SirenScrollWidget : ui::ScrollWidget {
	static constexpr float SCROLLBAR_W = 6.f;
	void step() override {
		ui::ScrollWidget::step();
		if (verticalScrollbar) {
			verticalScrollbar->box.size.x = SCROLLBAR_W;
			verticalScrollbar->box.pos.x = box.size.x - SCROLLBAR_W;
		}
	}
};

// SirenBrowserPane (tag bar added after SirenTagBar is defined)
struct SirenBrowserPane : widget::OpaqueWidget {
	std::function<void(const DataSourceNode&, bool)> onFileSelected;
	std::function<void()> onAddRoot;
	std::function<void(int)> onSelectRoot;

	// Fired right before the active DataSource is destroyed (root switch/removal),
	// so owners holding raw DataSource*/MetadataStore* derived from it (e.g. the
	// preview pane) can drop those references before they dangle.
	std::function<void()> onActiveSourceChanging;

	SirenDropHandler* dropHandler = nullptr;
	TaskWorker* worker = nullptr;

	std::vector<RootContainer> rootContainers;
	int activeRootIdx = -1;
	std::string selectedPath;
	bool favoritesOnly = false;

	// Set by SirenWidget when constructed without a module (module browser
	// thumbnail) — draws a static mock file tree since no roots are loaded.
	bool dummyPreview = false;

	std::string searchQuery;

	// Set by SirenTopBar so setSearchQuery() can update the displayed text field.
	std::function<void(const std::string&)> setSearchFieldText;

	void setSearchQuery(const std::string& query) {
		searchQuery = query;
		if (setSearchFieldText) setSearchFieldText(query);
		requestRebuild();
	}

	// Merges a "bpm:..." or "length:..." filter token into the current search
	// query, replacing any existing filter token for the same field while
	// leaving other text/filters untouched.
	void setFilter(const std::string& filterToken) {
		SearchFilter newFilter;
		bool isFilter = parseSearchFilter(rack::string::lowercase(filterToken), newFilter);

		std::istringstream iss(searchQuery);
		std::string token;
		std::vector<std::string> tokens;
		while (iss >> token) {
			SearchFilter f;
			if (isFilter && parseSearchFilter(rack::string::lowercase(token), f) && f.field == newFilter.field) continue;
			tokens.push_back(token);
		}
		tokens.push_back(filterToken);

		std::string newQuery;
		for (size_t i = 0; i < tokens.size(); i++) {
			if (i > 0) newQuery += " ";
			newQuery += tokens[i];
		}
		setSearchQuery(newQuery);
	}

	// Held via shared_ptr so worker tasks that captured a copy (indexing, tag
	// classification) keep the source and its MetadataStore alive even if the
	// active root is switched while they're running.
	std::shared_ptr<DataSource> activeDs;

	// Signaled (then replaced) whenever the active source changes, so in-flight
	// indexing/tag-classification tasks for the old source stop promptly instead
	// of running to completion against a source that's no longer displayed. Each
	// generation gets its own flag object: a task captures a copy of the shared_ptr
	// for its generation, which only ever transitions false -> true once.
	std::shared_ptr<std::atomic<bool>> activeDsCancel = std::make_shared<std::atomic<bool>>(false);

	void cancelActiveSourceTasks() {
		activeDsCancel->store(true, std::memory_order_relaxed);
		activeDsCancel = std::make_shared<std::atomic<bool>>(false);
	}

	struct TreeEntry {
		DataSourceNode node;
		int indent = 0;
		bool expanded = false;
		// True while this container's children have been requested but not yet
		// arrived — rebuildRowWidgets() shows a "Loading…" placeholder row below it.
		bool childrenLoading = false;
	};
	std::vector<TreeEntry> rows;
	std::atomic<bool> loadPending{false};

	// Tag filter: include set requires the tag to be present, exclude set
	// requires the tag to be absent. Both can be active simultaneously.
	std::set<std::string> tagFilter;
	std::set<std::string> tagExcludeFilter;

	std::string pendingSelectFirstOfPath;
	std::string pendingRevealPath;

	bool rebuildDirty = false;
	bool scrollAfterRebuild = false;

	struct PendingResult {
		std::string parentPath;
		int insertIdx = -1;
		std::vector<DataSourceNode> nodes;
		int gen = -1;
		PendingResult() = default;
		PendingResult(std::string p, int idx, std::vector<DataSourceNode> n, int g)
			: parentPath(std::move(p)), insertIdx(idx), nodes(std::move(n)), gen(g) {}
	};
	std::atomic<int> treeGeneration{0};
	dsp::RingBuffer<PendingResult, 16> pendingQueue;

	SirenScrollWidget* scrollWidget = nullptr;
	widget::Widget* rowContainer = nullptr;
	SirenTagBar* tagBar = nullptr;

	// Background scans (indexing, tag classification). Each owns its own progress
	// tracking and is cleared by step() once it observes completion.
	SirenIndexTask indexTask;
	SirenClassifyTask classifyTask;

	// Defined after SirenTagBar
	void init(TaskWorker* tw);
	void setSize(Vec size);

	// Status message for the preview pane's generic background-task overlay,
	// e.g. "Loading…" while the tree is (re)loading, or "Indexing… N files"
	// while a metadata scan is running. Empty if this pane has nothing to
	// report. Covers all of this pane's background tasks, so callers don't
	// need to know which one (if any) is active.
	std::string statusMessage() const {
		if (loadPending) return "Loading\xe2\x80\xa6";

		std::string msg = classifyTask.statusMessage();
		if (!msg.empty()) return msg;
		return indexTask.statusMessage();
	}

	// Recursively scans every supported file below the active root, reading audio
	// header info (length, sample rate, bit depth, channels) and detecting BPM
	// from filenames, storing the results in metadata. Does not decode PCM data
	// or build waveform caches. Runs on the worker thread; progress is reported
	// via indexTask for draw() to display.
	void startIndexing() {
		if (!worker || !activeDs) return;
		indexTask.start(worker, activeDs, activeDsCancel);
	}

	std::string getRootDisplayName(int idx) const {
		if (idx < 0 || idx >= (int)rootContainers.size()) return "";
		return rootContainers[idx].name;
	}

	void setRoots(const std::vector<RootContainer>& roots, int activeIdx) {
		// Roots arrive pre-sorted from sirenSettings (sorted on load and on every
		// mutation). activeIdx is already an index into that sorted order.
		rootContainers = roots;
		activeRootIdx = activeIdx;
		if (activeRootIdx >= 0 && activeRootIdx < (int)rootContainers.size()) {
			loadRoot(rootContainers[activeRootIdx]);
		}
		else {
			if (onActiveSourceChanging) onActiveSourceChanging();
			cancelActiveSourceTasks();
			activeDs = nullptr;
			activeRootIdx = -1;
			selectedPath.clear();
			rows.clear();
			loadPending = false;
			++treeGeneration;
			rebuildRowWidgets();
		}
	}

	void loadRoot(const RootContainer& root) {
		if (onActiveSourceChanging) onActiveSourceChanging();
		cancelActiveSourceTasks();
		activeDs = createDataSource(root);
		rows.clear();
		rebuildRowWidgets();
		int gen = ++treeGeneration;
		if (!worker) return;
		loadPending = true;
		activeDs->loadChildrenAsync(activeDs->rootId(), *worker, [this, gen](std::vector<DataSourceNode> nodes) {
			if (!pendingQueue.full()) {
				pendingQueue.push(PendingResult("", 0, std::move(nodes), gen));
			}
		});
	}

	void step() override {
		while (!pendingQueue.empty()) {
			PendingResult result = pendingQueue.shift();
			if (result.gen != treeGeneration.load(std::memory_order_relaxed)) continue;
			loadPending = false;
			if (result.parentPath.empty()) {
				rows.clear();
				for (auto& n : result.nodes) {
					TreeEntry e; e.node = n; e.indent = 0;
					rows.push_back(e);
				}
			}
			else {
				int parentIdx = findTreeIdx(result.parentPath);
				if (parentIdx >= 0) {
					rows[parentIdx].childrenLoading = false;
					std::vector<TreeEntry> newRows;
					for (auto& n : result.nodes) {
						TreeEntry e;
						e.node = n;
						e.indent = rows[parentIdx].indent + 1;
						newRows.push_back(e);
					}
					rows.insert(rows.begin() + parentIdx + 1, newRows.begin(), newRows.end());
				}
			}
			rebuildRowWidgets();

			if (!pendingSelectFirstOfPath.empty()) {
				std::string parentId = pendingSelectFirstOfPath;
				pendingSelectFirstOfPath.clear();
				auto vr = visibleRowWidgets();
				for (int i = 0; i < (int)vr.size() - 1; i++) {
					if (vr[i]->node.relativePath == parentId) {
						selectPath(vr[i + 1]->node);
						break;
					}
				}
			}
			if (!pendingRevealPath.empty()) {
				advanceRevealPath();
			}
			break;
		}
		if (dropHandler) dropHandler->step();

		if (indexTask.step()) requestRebuild();
		classifyTask.step();

		if (rebuildDirty && !(dropHandler && dropHandler->active)) {
			rebuildDirty = false;
			bool doScroll = scrollAfterRebuild;
			scrollAfterRebuild = false;
			rebuildRowWidgets();
			if (doScroll) scrollToSelected();
		}

		OpaqueWidget::step();
	}

	void requestRebuild() { rebuildDirty = true; }

	void rebuildRowWidgets();  // Defined after SirenTagBar

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
			entry.childrenLoading = false;
			requestRebuild();
			return;
		}

		entry.expanded = true;
		requestRebuild();

		std::string id = entry.node.relativePath;

		if (worker && activeDs) {
			entry.childrenLoading = true;
			int gen = treeGeneration.load(std::memory_order_relaxed);
			activeDs->loadChildrenAsync(id, *worker, [this, id, gen](std::vector<DataSourceNode> nodes) {
				if (!pendingQueue.full())
					pendingQueue.push(PendingResult(id, -1, std::move(nodes), gen));
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

	bool sampleMatchesFilter(const SampleMetadata& sm) const {
		if (favoritesOnly && !sm.favorite) return false;
		if (!tagFilter.empty()) {
			for (const std::string& t : tagFilter) {
				if (std::find(sm.tags.begin(), sm.tags.end(), t) == sm.tags.end()) return false;
			}
		}
		if (!tagExcludeFilter.empty()) {
			for (const std::string& t : tagExcludeFilter) {
				if (std::find(sm.tags.begin(), sm.tags.end(), t) != sm.tags.end()) return false;
			}
		}
		return true;
	}

	int findTreeIdx(const std::string& id) {
		for (int i = 0; i < (int)rows.size(); i++) {
			if (rows[i].node.relativePath == id) return i;
		}
		return -1;
	}

	std::vector<SirenTreeRow*> visibleRowWidgets() {
		std::vector<SirenTreeRow*> result;
		for (Widget* w : rowContainer->children) {
			if (auto* r = dynamic_cast<SirenTreeRow*>(w)) {
				result.push_back(r);
			}
		}
		return result;
	}

	void scrollToSelected() {
		for (Widget* w : rowContainer->children) {
			auto* r = dynamic_cast<SirenTreeRow*>(w);
			if (!r || !r->selected) continue;
			float rowTop = r->box.pos.y;
			float rowBot = rowTop + r->box.size.y;
			float viewH = scrollWidget->box.size.y;
			if (rowTop < scrollWidget->offset.y) {
				scrollWidget->offset.y = rowTop;
			}
			else if (rowBot > scrollWidget->offset.y + viewH) {
				scrollWidget->offset.y = rowBot - viewH;
			}
			break;
		}
	}

	void selectPath(const DataSourceNode& node, bool startPlay = false) {
		selectedPath = node.relativePath;
		requestRebuild();
		scrollAfterRebuild = true;
		if (!node.isContainer && onFileSelected) {
			onFileSelected(node, startPlay);
		}
	}

	// Marks `node` as the selected row without invoking onFileSelected, used when
	// the file is already loaded elsewhere (e.g. restoring selection on patch load).
	void markSelected(const DataSourceNode& node) {
		selectedPath = node.relativePath;
		requestRebuild();
		scrollAfterRebuild = true;
	}

	// Expands the tree along `relativePath` (loading folders asynchronously as
	// needed) and marks the target file as selected once its row becomes visible.
	void revealPath(const std::string& relativePath) {
		pendingRevealPath = relativePath;
		advanceRevealPath();
	}

	void advanceRevealPath() {
		if (pendingRevealPath.empty()) return;

		int targetIdx = findTreeIdx(pendingRevealPath);
		if (targetIdx >= 0) {
			pendingRevealPath.clear();
			markSelected(rows[targetIdx].node);
			return;
		}

		// Find the deepest already-loaded ancestor directory of the target path.
		int bestIdx = -1;
		size_t bestLen = 0;
		for (int i = 0; i < (int)rows.size(); i++) {
			const std::string& rp = rows[i].node.relativePath;
			if (rp.size() > bestLen && rp.size() < pendingRevealPath.size()
					&& pendingRevealPath.compare(0, rp.size(), rp) == 0
					&& pendingRevealPath[rp.size()] == '/') {
				bestIdx = i;
				bestLen = rp.size();
			}
		}

		if (bestIdx < 0) {
			// Nothing to expand yet: only give up once async loading has settled.
			if (!loadPending) pendingRevealPath.clear();
			return;
		}
		if (!rows[bestIdx].expanded) {
			expandRow(bestIdx);
		}
	}

	void startTagClassification(const DataSourceNode& node) {
		if (!worker || !activeDs) return;
		if (classifyTask.running()) return;
		TagClassifier::registerKeywords(starterTagKeywords());
		// Captured by the worker lambda so this DataSource stays alive even if
		// the active root is switched mid-scan.
		std::shared_ptr<DataSource> ds = activeDs;
		MetadataStore* meta = ds->getMetadata();

		classifyTask.onApply = [this, meta, ds](const std::map<std::string, std::set<std::string>>& filtered) {
			for (const auto& pair : filtered) {
				for (const std::string& r : pair.second) {
					meta->addTag(r, pair.first);
				}
			}
			if (ds) ds->saveMetadata();
			requestRebuild();
		};

		classifyTask.start(worker, ds, meta, node.relativePath, node.isContainer, node.name,
			[this](const DataSourceNode& n, bool play) { selectPath(n, play); });
	}

	bool navigateKey(int key) {
		auto vr = visibleRowWidgets();
		if (vr.empty()) return false;

		int selIdx = -1;
		for (int i = 0; i < (int)vr.size(); i++) {
			if (vr[i]->selected) { selIdx = i; break; }
		}

		if (key == GLFW_KEY_UP) {
			selectPath(vr[(selIdx <= 0) ? 0 : selIdx - 1]->node);
			return true;
		}
		if (key == GLFW_KEY_DOWN) {
			selectPath(vr[(selIdx < 0) ? 0 : std::min(selIdx + 1, (int)vr.size() - 1)]->node);
			return true;
		}
		if (key == GLFW_KEY_RIGHT) {
			if (selIdx < 0) return true;
			SirenTreeRow* row = vr[selIdx];
			if (!row->node.isContainer) return true;
			int treeIdx = findTreeIdx(row->node.relativePath);
			if (treeIdx < 0) return true;
			if (!rows[treeIdx].expanded) {
				pendingSelectFirstOfPath = row->node.relativePath;
				expandRow(treeIdx);
			}
			else if (selIdx + 1 < (int)vr.size()) {
				selectPath(vr[selIdx + 1]->node);
			}
			return true;
		}
		if (key == GLFW_KEY_LEFT) {
			if (selIdx < 0) return true;
			SirenTreeRow* row = vr[selIdx];
			if (row->node.isContainer) {
				int treeIdx = findTreeIdx(row->node.relativePath);
				if (treeIdx >= 0 && rows[treeIdx].expanded) {
					expandRow(treeIdx);
					return true;
				}
			}
			int treeIdx = findTreeIdx(row->node.relativePath);
			if (treeIdx < 0) return true;
			int parentIndent = rows[treeIdx].indent - 1;
			if (parentIndent < 0) return true;
			for (int i = treeIdx - 1; i >= 0; i--) {
				if (rows[i].indent == parentIndent && rows[i].node.isContainer) {
					expandRow(i);
					selectedPath = rows[i].node.relativePath;
					requestRebuild();
					scrollAfterRebuild = true;
					return true;
				}
			}
			return true;
		}
		return false;
	}

	void onSelectKey(const SelectKeyEvent& e) override {
		if (e.action == GLFW_PRESS || e.action == GLFW_REPEAT) {
			if (e.key == GLFW_KEY_SPACE) {
				ModuleWidget* mw = getAncestorOfType<ModuleWidget>();
				mw->onSelectKey(e);
				return;
			}
			if (e.key == GLFW_KEY_UP || e.key == GLFW_KEY_DOWN ||
					e.key == GLFW_KEY_LEFT || e.key == GLFW_KEY_RIGHT) {
				if (navigateKey(e.key)) { e.consume(this); return; }
			}
		}
		OpaqueWidget::onSelectKey(e);
	}

	void draw(const DrawArgs& args) override {
		float w = box.size.x;
		float h = box.size.y;

		nvgBeginPath(args.vg);
		nvgRect(args.vg, 0, 0, w, h);
		nvgFillColor(args.vg, nvgRGBf(0.12f, 0.12f, 0.09f));
		nvgFill(args.vg);

		OpaqueWidget::draw(args);

		if (dummyPreview && rows.empty()) {
			dummyview::drawDummyTree(args.vg, w);
		}
	}
};


// Single tag chip widget
struct SirenTagChip : widget::OpaqueWidget {
	static constexpr float CHIP_H = 10.f;

	SirenBrowserPane* pane = nullptr;
	std::string tag;

	// Must be called from within a draw context so devicePxRatio is valid.
	static float measure(NVGcontext* vg, const std::string& label) {
		nvgFontSize(vg, 8.f);
		nvgFontFaceId(vg, APP->window->uiFont->handle);
		float bounds[4];
		nvgTextBounds(vg, 0.f, 0.f, label.c_str(), nullptr, bounds);
		return bounds[2] - bounds[0] + 10.f;
	}

	void draw(const DrawArgs& args) override {
		bool included = pane && pane->tagFilter.count(tag) > 0;
		bool excluded = pane && pane->tagExcludeFilter.count(tag) > 0;
		bool hovered = APP->event->getHoveredWidget() == this;

		NVGcolor bgColor;
		NVGcolor fgColor;
		if (included) {
			bgColor = bndGetTheme()->toolTheme.innerSelectedColor;
			fgColor = bndGetTheme()->toolTheme.textSelectedColor;
		}
		else if (excluded) {
			// Visual cue: red-tinted, dimmer — clearly distinct from "included".
			bgColor = nvgRGBAf(0.55f, 0.18f, 0.18f, 0.55f);
			fgColor = nvgRGBAf(1.f, 0.85f, 0.85f, 0.95f);
		}
		else if (hovered) {
			bgColor = bndGetTheme()->toolTheme.itemColor;
			bgColor.a *= 0.7f;
			fgColor = bndGetTheme()->toolTheme.innerColor;
		}
		else {
			bgColor = bndGetTheme()->toolTheme.innerColor;
			bgColor.a *= 0.4f;
			fgColor = bndGetTheme()->toolTheme.textColor;
		}

		nvgBeginPath(args.vg);
		nvgRoundedRect(args.vg, 0, 0, box.size.x, box.size.y, 2.f);
		nvgFillColor(args.vg, bgColor);
		nvgFill(args.vg);

		// Strike-through for excluded chips to make the state unambiguous.
		if (excluded) {
			nvgBeginPath(args.vg);
			nvgMoveTo(args.vg, 2.f, box.size.y * 0.5f);
			nvgLineTo(args.vg, box.size.x - 2.f, box.size.y * 0.5f);
			nvgStrokeColor(args.vg, fgColor);
			nvgStrokeWidth(args.vg, 0.9f);
			nvgStroke(args.vg);
		}

		nvgFontSize(args.vg, 8.f);
		nvgFontFaceId(args.vg, APP->window->uiFont->handle);
		nvgTextAlign(args.vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
		nvgFillColor(args.vg, fgColor);
		nvgText(args.vg, box.size.x * 0.5f, box.size.y * 0.5f, tag.c_str(), nullptr);
		nvgTextAlign(args.vg, NVG_ALIGN_LEFT | NVG_ALIGN_BASELINE);
	}

	void onButton(const event::Button& e) override {
		if (e.button == GLFW_MOUSE_BUTTON_LEFT && e.action == GLFW_PRESS) {
			// Click cycles: clear → include → exclude → clear
			if (pane->tagFilter.count(tag)) {
				pane->tagFilter.erase(tag);
				pane->tagExcludeFilter.insert(tag);
			}
			else if (pane->tagExcludeFilter.count(tag)) {
				pane->tagExcludeFilter.erase(tag);
			}
			else {
				pane->tagFilter.insert(tag);
			}
			pane->requestRebuild();
			e.consume(this);
			return;
		}
		OpaqueWidget::onButton(e);
	}
};


// Tag chip container (scrollable content inside SirenTagBar)
struct SirenTagContainer : widget::OpaqueWidget {
	SirenBrowserPane* pane = nullptr;
	widget::Widget* scrollContainer = nullptr;  // scrollWidget->container; updated after layout
	bool layoutDirty = true;

	static constexpr float CHIP_H = SirenTagChip::CHIP_H;
	static constexpr float ROW_STRIDE = CHIP_H + 2.f;
	static constexpr float PAD_X = 4.f;
	static constexpr float PAD_Y = 4.f;

	// Mark for rebuild; actual chip creation is deferred to draw() so NVG is ready.
	void layout() { layoutDirty = true; }

	void draw(const DrawArgs& args) override {
		if (layoutDirty) {
			layoutDirty = false;
			rebuildChips(args.vg);
		}
		OpaqueWidget::draw(args);
	}

	void rebuildChips(NVGcontext* vg) {
		clearChildren();
		if (!pane || !pane->activeDs) { box.size.y = PAD_Y * 2.f; return; }
		MetadataStore* meta = pane->activeDs->getMetadata();
		if (!meta) { box.size.y = PAD_Y * 2.f; return; }

		auto all = meta->allTags();
		std::vector<std::string> sorted(all.begin(), all.end());
		std::sort(sorted.begin(), sorted.end());

		float x = PAD_X, y = PAD_Y;
		for (const std::string& t : sorted) {
			float tw = SirenTagChip::measure(vg, t);
			if (x + tw > box.size.x - PAD_X) { y += ROW_STRIDE; x = PAD_X; }

			SirenTagChip* chip = new SirenTagChip;
			chip->pane = pane;
			chip->tag = t;
			chip->box.pos = Vec(x, y);
			chip->box.size = Vec(tw, CHIP_H);
			addChild(chip);

			x += tw + 4.f;
		}
		box.size.y = y + CHIP_H + PAD_Y;
		if (scrollContainer) scrollContainer->box.size = box.size;
	}

	void onButton(const event::Button& e) override {
		if (e.button == GLFW_MOUSE_BUTTON_RIGHT && e.action == GLFW_PRESS) {
			Menu* menu = createMenu();
			menu->addChild(createMenuItem("Clear tag filters", "", [this]() {
				pane->tagFilter.clear();
				pane->tagExcludeFilter.clear();
				pane->requestRebuild();
			}));
			menu->addChild(new MenuSeparator);
			menu->addChild(createMenuItem("Clear included tag filter", "", [this]() {
				pane->tagFilter.clear();
				pane->requestRebuild();
			}));
			menu->addChild(createMenuItem("Clear excluded tag filter", "", [this]() {
				pane->tagExcludeFilter.clear();
				pane->requestRebuild();
			}));

			e.consume(this);
			return;
		}
		OpaqueWidget::onButton(e);
	}
};


// Tag bar: separator + scrollable tag chip area
struct SirenTagBar : widget::OpaqueWidget {
	SirenScrollWidget* scrollWidget = nullptr;
	SirenTagContainer* tagContainer = nullptr;

	void init(SirenBrowserPane* pane) {
		scrollWidget = new SirenScrollWidget;
		scrollWidget->box.pos = Vec(0.f, 1.f);  // 1 px below separator
		scrollWidget->box.size = Vec(box.size.x, box.size.y - 1.f);
		addChild(scrollWidget);

		tagContainer = new SirenTagContainer;
		tagContainer->pane = pane;
		tagContainer->scrollContainer = scrollWidget->container;
		tagContainer->box.pos = Vec(0.f, 0.f);
		tagContainer->box.size = Vec(box.size.x - SirenScrollWidget::SCROLLBAR_W, 0.f);
		scrollWidget->container->addChild(tagContainer);

		layout();
	}

	// Mark chip layout dirty; actual rebuild happens in draw() once NVG is ready.
	void layout() {
		if (!tagContainer) return;
		tagContainer->box.size.x = box.size.x - SirenScrollWidget::SCROLLBAR_W;
		if (tagContainer->box.size.y <= 0.f) {
			tagContainer->box.size.y = SirenTagContainer::CHIP_H + SirenTagContainer::PAD_Y * 2.f;
		}
		tagContainer->layout();
	}

	void draw(const DrawArgs& args) override {
		float w = box.size.x;
		float h = box.size.y;

		// Background
		nvgBeginPath(args.vg);
		nvgRect(args.vg, 0, 0, w, h);
		nvgFillColor(args.vg, nvgRGBf(0.09f, 0.09f, 0.07f));
		nvgFill(args.vg);

		// Separator line at top
		nvgBeginPath(args.vg);
		nvgMoveTo(args.vg, 0, 0.5f);
		nvgLineTo(args.vg, w, 0.5f);
		nvgStrokeColor(args.vg, nvgRGBAf(1.f, 1.f, 1.f, 0.09f));
		nvgStrokeWidth(args.vg, 0.5f);
		nvgStroke(args.vg);

		OpaqueWidget::draw(args);
	}
};


// SirenBrowserPane deferred method implementations

inline void SirenBrowserPane::init(TaskWorker* tw) {
	worker = tw;

	scrollWidget = new SirenScrollWidget;
	scrollWidget->box.pos = Vec(0.f, 0.f);
	addChild(scrollWidget);

	rowContainer = new widget::Widget;
	scrollWidget->container->addChild(rowContainer);

	tagBar = new SirenTagBar;
	tagBar->init(this);
	addChild(tagBar);
}

inline void SirenBrowserPane::setSize(Vec size) {
	box.size = size;
	scrollWidget->box.size = Vec(size.x, size.y - BROWSER_TAG_H);
	rowContainer->box.size.x = size.x;

	tagBar->box.pos = Vec(0.f, size.y - BROWSER_TAG_H);
	tagBar->box.size = Vec(size.x, BROWSER_TAG_H);
	tagBar->scrollWidget->box.size = Vec(size.x, BROWSER_TAG_H - 1.f);
	tagBar->layout();
}

inline MetadataStore* SirenTreeRow::metadata() const {
	return (pane && pane->activeDs) ? pane->activeDs->getMetadata() : nullptr;
}

inline void SirenBrowserPane::rebuildRowWidgets() {
	rowContainer->clearChildren();
	MetadataStore* meta = activeDs ? activeDs->getMetadata() : nullptr;

	// Pre-compute which directories have at least one matching descendant in O(N).
	// Container rows then do an O(log D) set lookup instead of an O(N) scan each.
	const bool filterActive = favoritesOnly || !tagFilter.empty() || !tagExcludeFilter.empty();
	std::set<std::string> matchingDirs;
	if (filterActive && meta) {
		for (const auto& pair : meta->samples) {
			if (!sampleMatchesFilter(pair.second)) continue;
			const std::string& filePath = pair.first;
			size_t pos = 0;
			while ((pos = filePath.find('/', pos)) != std::string::npos) {
				matchingDirs.insert(filePath.substr(0, pos));
				++pos;
			}
		}
	}

	SearchQuery sq = parseSearchQuery(searchQuery);
	float y = 0.f;
	for (int i = 0; i < (int)rows.size(); i++) {
		const TreeEntry& entry = rows[i];
		const DataSourceNode& n = entry.node;

		if (!sq.empty() && activeDs) {
			if (!activeDs->matchesSearch(n.relativePath, n.isContainer, sq)) {
				continue;
			}
		}

		if (n.isContainer) {
			if (filterActive && matchingDirs.count(n.relativePath) == 0) continue;
		}
		else {
			if (filterActive && meta) {
				auto it = meta->samples.find(n.relativePath);
				if (it == meta->samples.end() || !sampleMatchesFilter(it->second)) continue;
			}
		}

		SirenTreeRow* row = new SirenTreeRow;
		row->init(n, entry.indent, this);
		row->selected = (n.relativePath == selectedPath);
		row->expanded = entry.expanded;
		row->box.pos = Vec(0.f, y);
		row->box.size = Vec(box.size.x - SirenScrollWidget::SCROLLBAR_W, SirenTreeRow::ROW_H);
		rowContainer->addChild(row);
		y += SirenTreeRow::ROW_H;

		if (n.isContainer && entry.expanded && entry.childrenLoading) {
			SirenLoadingRow* loadingRow = new SirenLoadingRow;
			loadingRow->indentLevel = entry.indent + 1;
			loadingRow->box.pos = Vec(0.f, y);
			loadingRow->box.size = Vec(box.size.x - SirenScrollWidget::SCROLLBAR_W, SirenTreeRow::ROW_H);
			rowContainer->addChild(loadingRow);
			y += SirenTreeRow::ROW_H;
		}
	}
	rowContainer->box.size.y = y;
	scrollWidget->container->box.size = rowContainer->box.size;

	if (tagBar) tagBar->layout();
}


// SirenTreeRow out-of-line method implementations

inline void SirenTreeRow::onButton(const event::Button& e) {
	if (starBtn && starBtn->box.contains(e.pos)) {
		starBtn->onButton(e);
		return;
	}
	if (e.button == GLFW_MOUSE_BUTTON_LEFT) {
		if (e.action == GLFW_PRESS) {
			if (node.isContainer) {
				int treeIdx = pane->findTreeIdx(node.relativePath);
				if (treeIdx >= 0) pane->expandRow(treeIdx);
			}
			else {
				bool shift = (e.mods & GLFW_MOD_SHIFT) != 0;
				pane->selectPath(node, shift);
			}
		}
		if (e.action == GLFW_RELEASE) {
			Widget* w = getAncestorOfType<ModuleWidget>();
			APP->event->setSelectedWidget(w);
		}
		e.consume(this);
	}
	if (e.button == GLFW_MOUSE_BUTTON_RIGHT && e.action == GLFW_PRESS) {
		if (pane->activeDs) {
			ui::Menu* menu = createMenu();
			menu->addChild(createMenuLabel(pane->activeDs->getDisplayName(node.relativePath)));
			menu->addChild(new ui::MenuSeparator);
			pane->activeDs->appendNodeMenuItems(menu, node, [this]() {
				pane->rebuildRowWidgets();
			});

			menu->addChild(new ui::MenuSeparator);
			menu->addChild(createMenuItem("Suggest tags", "", [this]() {
				pane->startTagClassification(node);
			}));

			menu->addChild(createMenuItem("Clear tags", "", [this]() {
				DataSource* ds = pane->activeDs.get();
				if (!ds) return;
				MetadataStore* meta = ds->getMetadata();
				if (!meta) return;
				if (node.isContainer) {
					const std::string prefix = node.relativePath + "/";
					for (auto& pair : meta->samples) {
						if (pair.first.compare(0, prefix.size(), prefix) == 0) {
							meta->clearTags(pair.first);
						}
					}
				}
				else {
					meta->clearTags(node.relativePath);
				}
				ds->saveMetadata();
				pane->requestRebuild();
			}));

			e.consume(this);
		}
	}
}

inline void SirenTreeRow::onDragStart(const event::DragStart& e) {
	if (node.isContainer) return;
	if (pane && pane->dropHandler) {
		pane->dropHandler->startDrag(node.relativePath, node.name);
	}
	e.consume(this);
}

inline void SirenTreeRow::onDragEnd(const event::DragEnd& e) {
	if (!pane || !pane->dropHandler || !pane->dropHandler->active) return;
	pane->dropHandler->endDrag(APP->scene->mousePos, pane->worker);
}

} // namespace Siren
} // namespace StoermelderPackOne
