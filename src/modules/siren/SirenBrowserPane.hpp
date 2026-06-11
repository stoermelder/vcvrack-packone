#pragma once
#include <rack.hpp>
#include "Siren.hpp"
#include "SirenDataSource.hpp"
#include "SirenFileSystem.hpp"
#include "SirenMetadata.hpp"
#include "SirenDropHandler.hpp"
#include "SirenTagClassifierApi.hpp"
#include "SirenBpmDetector.hpp"
#include "../../utils/TaskWorker.hpp"
#include "../../ui/AutoTagDialog.hpp"


namespace StoermelderPackOne {
namespace Siren {

// Forward declarations
struct SirenBrowserPane;
struct SirenTagBar;

static constexpr float BROWSER_TAG_H = 58.f;

// ─── single row in the tree ───────────────────────────────────────────────────

struct SirenTreeRow : widget::OpaqueWidget {
	static constexpr float ROW_H   = 12.f;
	static constexpr float INDENT  = 8.f;

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
		if (selected) state = BND_ACTIVE;
		else if (APP->event->getHoveredWidget() == this) state = BND_HOVER;

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
			const float starW   = 10.f;
			const float durW    = 26.f;
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

// ─── ScrollWidget with a narrower vertical scrollbar ─────────────────────────

struct SirenScrollWidget : ui::ScrollWidget {
	static constexpr float SCROLLBAR_W = 6.f;
	void step() override {
		ui::ScrollWidget::step();
		if (verticalScrollbar) {
			verticalScrollbar->box.size.x = SCROLLBAR_W;
			verticalScrollbar->box.pos.x  = box.size.x - SCROLLBAR_W;
		}
	}
};

// ─── Indexing progress ─────────────────────────────────────────────────────────
// Shared between the worker thread (writes) and SirenBrowserPane::draw (reads).
// Held via shared_ptr so the worker task can safely outlive the pane if the
// module is removed mid-scan.
struct IndexProgress {
	std::atomic<int>  processed{0};
	std::atomic<bool> done{false};
};

// ─── SirenBrowserPane (tag bar added after SirenTagBar is defined) ────────────

struct SirenBrowserPane : widget::OpaqueWidget {
	std::function<void(const DataSourceNode&, bool)> onFileSelected;
	std::function<void()>    onAddRoot;
	std::function<void(int)> onSelectRoot;

	// Fired right before the active DataSource is destroyed (root switch/removal),
	// so owners holding raw DataSource*/MetadataStore* derived from it (e.g. the
	// preview pane) can drop those references before they dangle.
	std::function<void()> onActiveSourceChanging;

	SirenDropHandler* dropHandler = nullptr;
	TaskWorker* worker = nullptr;

	std::vector<std::string> rootContainers;
	int activeRootIdx = -1;
	std::string selectedPath;
	bool favoritesOnly = false;

	std::string searchQuery;

	// Set by SirenTopBar so setSearchQuery() can update the displayed text field.
	std::function<void(const std::string&)> setSearchFieldText;

	void setSearchQuery(const std::string& query) {
		searchQuery = query;
		if (setSearchFieldText) setSearchFieldText(query);
		requestRebuild();
	}

	// Merges a "bpm:..." filter token into the current search query, replacing
	// any existing bpm filter token while leaving other text/filters untouched.
	void setBpmFilter(const std::string& filterToken) {
		std::istringstream iss(searchQuery);
		std::string token;
		std::vector<std::string> tokens;
		while (iss >> token) {
			SearchFilter f;
			if (parseSearchFilter(rack::string::lowercase(token), f) && f.field == SearchFilterField::Bpm) continue;
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

	DataSource* activeDataSource = nullptr;

	struct TreeEntry {
		DataSourceNode node;
		int indent    = 0;
		bool expanded = false;
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
	std::atomic<int>  treeGeneration{0};
	PendingResult     pendingResult;
	std::atomic<bool> pendingReady{false};

	SirenScrollWidget* scrollWidget = nullptr;
	widget::Widget*    rowContainer = nullptr;
	SirenTagBar*       tagBar       = nullptr;

	// Set while a metadata indexing scan is running; cleared once step() observes
	// IndexProgress::done. nullptr when no scan is in progress.
	std::shared_ptr<IndexProgress> indexProgress;

	~SirenBrowserPane() override { delete activeDataSource; }

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

		if (!indexProgress) return "";
		int processed = indexProgress->processed.load(std::memory_order_relaxed);

		char msg[64];
		std::snprintf(msg, sizeof(msg), "Indexing\xe2\x80\xa6 %d files", processed);
		return msg;
	}

	// Recursively scans every supported file below the active root, reading audio
	// header info (length, sample rate, bit depth, channels) and detecting BPM
	// from filenames, storing the results in metadata. Does not decode PCM data
	// or build waveform caches. Runs on the worker thread; progress is reported
	// via indexProgress for draw() to display.
	void startIndexing() {
		if (!worker || !activeDataSource) return;
		if (indexProgress && !indexProgress->done.load(std::memory_order_relaxed)) return;

		MetadataStore* meta = activeDataSource->getMetadata();
		if (!meta) return;

		DataSource* ds = activeDataSource;
		auto progress = std::make_shared<IndexProgress>();
		indexProgress = progress;

		worker->work([ds, meta, progress](std::atomic<bool>& cancel) {
			// Index each file as it's discovered, rather than collecting the
			// full file list up front, so progress and cancellation are
			// responsive even for very large libraries.
			std::function<void(const std::string&)> visit = [&](const std::string& id) {
				if (cancel.load(std::memory_order_relaxed)) return;
				// withAudioInfo=false: this listing is only used for traversal/file
				// names here, the full AudioInfo is loaded explicitly below.
				for (const auto& child : ds->loadChildrenSync(id, false)) {
					if (cancel.load(std::memory_order_relaxed)) return;
					if (child.isContainer) {
						visit(child.relativePath);
						continue;
					}

					const std::string& rel = child.relativePath;
					int64_t ts = ds->getTimestamp(rel);
					if (!meta->hasValidAudioInfo(rel, ts)) {
						AudioInfo ai;
						if (ds->loadAudioInfo(rel, ai))
							meta->setAudioInfo(rel, ai.durationSeconds, ai.sampleRate, ai.bitDepth, ai.channels, ts);
					}

					if (meta->getBpm(rel) <= 0.f) {
						float conf = 0.f;
						float bpmVal = BpmDetector::detectFromName(rel, conf);
						if (bpmVal > 0.f) meta->setBpm(rel, bpmVal, conf);
					}
					meta->markSeen(rel);
					progress->processed.fetch_add(1, std::memory_order_relaxed);
				}
			};
			visit(ds->rootId());

			if (!cancel.load(std::memory_order_relaxed))
				ds->saveMetadata();
			progress->done.store(true, std::memory_order_release);
		});
	}

	std::string getRootDisplayName(int idx) const {
		if (idx < 0 || idx >= (int)rootContainers.size()) return "";
		if (idx == activeRootIdx && activeDataSource)
			return activeDataSource->getRootDisplayName();
		return FileSystemDataSource::rootDisplayName(rootContainers[idx]);
	}

	void setRoots(const std::vector<std::string>& roots, int activeIdx) {
		rootContainers = roots;
		activeRootIdx  = activeIdx;
		if (activeRootIdx >= 0 && activeRootIdx < (int)rootContainers.size()) {
			loadRoot(rootContainers[activeRootIdx]);
		}
		else {
			if (onActiveSourceChanging) onActiveSourceChanging();
			delete activeDataSource;
			activeDataSource = nullptr;
			selectedPath.clear();
			rows.clear();
			loadPending = false;
			pendingReady.store(false, std::memory_order_relaxed);
			++treeGeneration;
			rebuildRowWidgets();
		}
	}

	void loadRoot(const std::string& root) {
		if (onActiveSourceChanging) onActiveSourceChanging();
		delete activeDataSource;
		activeDataSource = new FileSystemDataSource(root);
		rows.clear();
		rebuildRowWidgets();
		pendingReady.store(false, std::memory_order_relaxed);
		int gen = ++treeGeneration;
		if (!worker) return;
		loadPending = true;
		activeDataSource->loadChildrenAsync(activeDataSource->rootId(), *worker, [this, gen](std::vector<DataSourceNode> nodes) {
			pendingResult = PendingResult("", 0, std::move(nodes), gen);
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
			}
		}
		if (dropHandler) dropHandler->step();

		if (indexProgress && indexProgress->done.load(std::memory_order_acquire)) {
			indexProgress.reset();
			requestRebuild();
		}

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
			requestRebuild();
			return;
		}

		entry.expanded = true;
		requestRebuild();

		std::string id    = entry.node.relativePath;
		int insertIdx     = rowIdx + 1;

		if (worker && activeDataSource) {
			int gen = treeGeneration.load(std::memory_order_relaxed);
			activeDataSource->loadChildrenAsync(id, *worker, [this, insertIdx, gen](std::vector<DataSourceNode> nodes) {
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

	bool containerHasMatchingDescendant(int rowIdx, MetadataStore* meta) const {
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
			if (!tagExcludeFilter.empty()) {
				bool hasAny = false;
				for (const std::string& t : tagExcludeFilter)
					if (std::find(sm.tags.begin(), sm.tags.end(), t) != sm.tags.end()) { hasAny = true; break; }
				if (hasAny) continue;
			}
			return true;
		}
		return false;
	}

	int findTreeIdx(const std::string& id) {
		for (int i = 0; i < (int)rows.size(); i++)
			if (rows[i].node.relativePath == id) return i;
		return -1;
	}

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

	void selectPath(const DataSourceNode& node, bool startPlay = false) {
		selectedPath = node.relativePath;
		requestRebuild();
		scrollAfterRebuild = true;
		if (!node.isContainer && onFileSelected)
			onFileSelected(node, startPlay);
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
		if (!worker || !activeDataSource) return;
		TagClassifier::registerKeywords(starterTagKeywords());
		DataSource*   ds    = activeDataSource;
		MetadataStore* meta  = ds->getMetadata();
		std::string   rel   = node.relativePath;
		bool          isDir = node.isContainer;
		std::string   name  = node.name;

		using DataSourceNodeId = std::string;

		auto buildLabel = [this, ds](const std::string& tag, const DataSourceNodeId& fileId) -> widget::Widget* {
			struct SampleLabel : ui::MenuItem {
				DataSource*       ds;
				SirenBrowserPane* pane;
				DataSourceNodeId  fileId;
				std::string       groupTag;
				void onAction(const event::Action& e) override {
					pane->selectPath(ds->resolveNode(fileId), true);
					e.unconsume();
				}
				void onButton(const ButtonEvent& e) override {
					if (e.action == GLFW_PRESS && e.button == GLFW_MOUSE_BUTTON_RIGHT) {
						Menu* menu = createMenu();
						menu->addChild(createMenuLabel(ds->getDisplayName(fileId)));
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
			item->ds       = ds;
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
			return rack::string::f("%d tag%s %s", sel, sel == 1 ? "" : "s");
		};

		std::string header = isDir ? "Suggest tags — " + name : "Suggest tags";
		using StreamDlg = StoermelderPackOne::ui::StreamingTagDialog<DataSourceNodeId>;

		ui::MenuOverlay* overlay = new ui::MenuOverlay;
		overlay->bgColor = nvgRGBAf(0.f, 0.f, 0.f, 0.5f);
		StreamDlg* dlg = new StreamDlg(buildLabel, applyFn, header, summaryFn);
		overlay->addChild(dlg);
		APP->scene->addChild(overlay);

		auto progress = dlg->progress;
		worker->work([progress, rel, isDir, ds, meta]() {
			std::vector<DataSourceNodeId> files;
			if (isDir) {
				std::function<void(const std::string&)> collect = [&](const std::string& id) {
					for (const auto& child : ds->loadChildrenSync(id)) {
						if (child.isContainer) collect(child.relativePath);
						else files.push_back(child.relativePath);
					}
				};
				collect(rel);
			}
			else {
				files = {rel};
			}

			progress->total = (int)files.size();
			for (const auto& f : files) {
				// Skip suggestion emission for tags already applied to this sample.
				// Matching is case-insensitive on the trimmed name, mirroring addTag().
				std::set<std::string> existing;
				if (meta) {
					for (const std::string& t : meta->getTags(f)) {
						existing.insert(rack::string::lowercase(rack::string::trim(t)));
					}
				}

				auto stream = ds->openAudioStream(f);
				if (stream) {
					auto suggestions = TagClassifier::classify(*stream, f, 5);
					for (const auto& s : suggestions) {
						if (s.score < 0.5f) continue;
						std::string nameLow = rack::string::lowercase(rack::string::trim(s.name));
						if (existing.count(nameLow)) continue;
						progress->events.push({s.name, f});
					}
				}
				progress->processed++;
			}
			progress->done.store(true, std::memory_order_release);
		});
	}

	bool navigateKey(int key) {
		auto vr = visibleRowWidgets();
		if (vr.empty()) return false;

		int selIdx = -1;
		for (int i = 0; i < (int)vr.size(); i++)
			if (vr[i]->selected) { selIdx = i; break; }

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
	}
};


// ─── Single tag chip widget ───────────────────────────────────────────────────

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
		bool hovered  = APP->event->getHoveredWidget() == this;

		NVGcolor bgColor, fgColor;
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


// ─── Tag chip container (scrollable content inside SirenTagBar) ───────────────

struct SirenTagContainer : widget::OpaqueWidget {
	SirenBrowserPane* pane            = nullptr;
	widget::Widget*   scrollContainer = nullptr;  // scrollWidget->container; updated after layout
	bool              layoutDirty     = true;

	static constexpr float CHIP_H     = SirenTagChip::CHIP_H;
	static constexpr float ROW_STRIDE = CHIP_H + 2.f;
	static constexpr float PAD_X      = 4.f;
	static constexpr float PAD_Y      = 4.f;

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
		if (!pane || !pane->activeDataSource) { box.size.y = PAD_Y * 2.f; return; }
		MetadataStore* meta = pane->activeDataSource->getMetadata();
		if (!meta) { box.size.y = PAD_Y * 2.f; return; }

		auto all = meta->allTags();
		std::vector<std::string> sorted(all.begin(), all.end());
		std::sort(sorted.begin(), sorted.end());

		float x = PAD_X, y = PAD_Y;
		for (const std::string& t : sorted) {
			float tw = SirenTagChip::measure(vg, t);
			if (x + tw > box.size.x - PAD_X) { y += ROW_STRIDE; x = PAD_X; }

			SirenTagChip* chip = new SirenTagChip;
			chip->pane     = pane;
			chip->tag      = t;
			chip->box.pos  = Vec(x, y);
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


// ─── Tag bar: separator + scrollable tag chip area ────────────────────────────

struct SirenTagBar : widget::OpaqueWidget {
	SirenScrollWidget* scrollWidget  = nullptr;
	SirenTagContainer* tagContainer  = nullptr;

	void init(SirenBrowserPane* pane) {
		scrollWidget = new SirenScrollWidget;
		scrollWidget->box.pos  = Vec(0.f, 1.f);  // 1 px below separator
		scrollWidget->box.size = Vec(box.size.x, box.size.y - 1.f);
		addChild(scrollWidget);

		tagContainer = new SirenTagContainer;
		tagContainer->pane            = pane;
		tagContainer->scrollContainer = scrollWidget->container;
		tagContainer->box.pos  = Vec(0.f, 0.f);
		tagContainer->box.size = Vec(box.size.x - SirenScrollWidget::SCROLLBAR_W, 0.f);
		scrollWidget->container->addChild(tagContainer);

		layout();
	}

	// Mark chip layout dirty; actual rebuild happens in draw() once NVG is ready.
	void layout() {
		if (!tagContainer) return;
		tagContainer->box.size.x = box.size.x - SirenScrollWidget::SCROLLBAR_W;
		if (tagContainer->box.size.y <= 0.f)
			tagContainer->box.size.y = SirenTagContainer::CHIP_H + SirenTagContainer::PAD_Y * 2.f;
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


// ─── SirenBrowserPane deferred method implementations ────────────────────────

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

	tagBar->box.pos  = Vec(0.f, size.y - BROWSER_TAG_H);
	tagBar->box.size = Vec(size.x, BROWSER_TAG_H);
	tagBar->scrollWidget->box.size = Vec(size.x, BROWSER_TAG_H - 1.f);
	tagBar->layout();
}

inline MetadataStore* SirenTreeRow::metadata() const {
	return (pane && pane->activeDataSource) ? pane->activeDataSource->getMetadata() : nullptr;
}

inline void SirenBrowserPane::rebuildRowWidgets() {
	rowContainer->clearChildren();
	MetadataStore* meta = activeDataSource ? activeDataSource->getMetadata() : nullptr;

	SearchQuery sq = parseSearchQuery(searchQuery);
	float y = 0.f;
	for (int i = 0; i < (int)rows.size(); i++) {
		const TreeEntry& entry = rows[i];
		const DataSourceNode& n = entry.node;

		if (!sq.empty() && activeDataSource) {
			if (!activeDataSource->matchesSearch(n.relativePath, n.isContainer, sq)) continue;
		}

		if (n.isContainer) {
			if ((favoritesOnly || !tagFilter.empty() || !tagExcludeFilter.empty()) && !containerHasMatchingDescendant(i, meta))
				continue;
		}
		else {
			if (favoritesOnly && meta && !meta->isFavorite(n.relativePath))
				continue;
			if ((!tagFilter.empty() || !tagExcludeFilter.empty()) && meta) {
				auto tags = meta->getTags(n.relativePath);
				if (!tagFilter.empty()) {
					bool hasAll = true;
					for (const std::string& t : tagFilter)
						if (std::find(tags.begin(), tags.end(), t) == tags.end()) { hasAll = false; break; }
					if (!hasAll) continue;
				}
				if (!tagExcludeFilter.empty()) {
					bool hasAny = false;
					for (const std::string& t : tagExcludeFilter)
						if (std::find(tags.begin(), tags.end(), t) != tags.end()) { hasAny = true; break; }
					if (hasAny) continue;
				}
			}
		}

		SirenTreeRow* row = new SirenTreeRow;
		row->init(n, entry.indent, this);
		row->selected     = (n.relativePath == selectedPath);
		row->expanded     = entry.expanded;
		row->box.pos      = Vec(0.f, y);
		row->box.size     = Vec(box.size.x - SirenScrollWidget::SCROLLBAR_W, SirenTreeRow::ROW_H);
		rowContainer->addChild(row);
		y += SirenTreeRow::ROW_H;
	}
	rowContainer->box.size.y = y;
	scrollWidget->container->box.size = rowContainer->box.size;

	if (tagBar) tagBar->layout();
}


// ─── SirenTreeRow out-of-line method implementations ─────────────────────────

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
		if (pane->activeDataSource) {
			ui::Menu* menu = createMenu();
			menu->addChild(createMenuLabel(pane->activeDataSource->getDisplayName(node.relativePath)));
			pane->activeDataSource->appendNodeMenuItems(menu, node, [this]() {
				pane->rebuildRowWidgets();
			});

			menu->addChild(new ui::MenuSeparator);
			menu->addChild(createMenuItem("Suggest tags", "", [this]() {
				pane->startTagClassification(node);
			}));

			menu->addChild(createMenuItem("Clear tags", "", [this]() {
				DataSource* ds = pane->activeDataSource;
				if (!ds) return;
				MetadataStore* meta = ds->getMetadata();
				if (!meta) return;
				if (node.isContainer) {
					const std::string prefix = node.relativePath + "/";
					for (auto& pair : meta->samples)
						if (pair.first.compare(0, prefix.size(), prefix) == 0)
							meta->clearTags(pair.first);
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
	if (pane && pane->dropHandler)
		pane->dropHandler->startDrag(node.relativePath, node.name);
	e.consume(this);
}

inline void SirenTreeRow::onDragEnd(const event::DragEnd& e) {
	if (!pane || !pane->dropHandler || !pane->dropHandler->active) return;
	pane->dropHandler->endDrag(APP->scene->mousePos, pane->worker);
}

} // namespace Siren
} // namespace StoermelderPackOne
