#pragma once
#include <rack.hpp>
#include "SirenDataSource.hpp"
#include "SirenMetadata.hpp"
#include "SirenTagClassifierApi.hpp"
#include "SirenBpmDetector.hpp"
#include "../../utils/MpmcTaskWorker.hpp"
#include "../../ui/AutoTagDialog.hpp"
#include <atomic>
#include <functional>
#include <map>
#include <memory>
#include <set>
#include <string>


namespace StoermelderPackOne {
namespace Siren {

using namespace rack;

// SirenIndexTask
// Owns the background "index metadata" scan for a SirenBrowserPane: recursively
// reads audio header info (length, sample rate, bit depth, channels) and detects
// BPM from filenames for every supported file below the active root, storing the
// results in metadata. Does not decode PCM data or build waveform caches.
// Progress is held in a shared_ptr so the worker task can safely outlive the
// pane if the module is removed mid-scan; step() reclaims it once done.
struct SirenIndexTask {
	struct Progress {
		std::atomic<int> processed{0};
		std::atomic<bool> done{false};
	};
	std::shared_ptr<Progress> progress;

	bool running() const {
		return progress && !progress->done.load(std::memory_order_relaxed);
	}

	// "" if no scan is in progress.
	std::string statusMessage() const {
		if (!progress) return "";
		int processed = progress->processed.load(std::memory_order_relaxed);

		char msg[64];
		std::snprintf(msg, sizeof(msg), "Indexing\xe2\x80\xa6 %d files", processed);
		return msg;
	}

	// Called from SirenBrowserPane::step(). Returns true if the scan just
	// finished, so the caller can requestRebuild() to refresh durations/BPMs.
	bool step() {
		if (progress && progress->done.load(std::memory_order_acquire)) {
			progress.reset();
			return true;
		}
		return false;
	}

	void start(ITaskWorker* worker, std::shared_ptr<DataSource> ds, std::shared_ptr<std::atomic<bool>> dsCancel) {
		if (running()) return;
		MetadataStore* meta = ds->getMetadata();
		if (!meta) return;

		auto p = std::make_shared<Progress>();
		progress = p;

		worker->work([ds, meta, p, dsCancel](std::atomic<bool>& cancel) {
			auto cancelled = [&]() {
				return cancel.load(std::memory_order_relaxed) || dsCancel->load(std::memory_order_relaxed);
			};
			// Index each file as it's discovered, rather than collecting the
			// full file list up front, so progress and cancellation are
			// responsive even for very large libraries.
			std::function<void(const std::string&)> visit = [&](const std::string& id) {
				if (cancelled()) return;
				// withAudioInfo=false: this listing is only used for traversal/file
				// names here, the full AudioInfo is loaded explicitly below.
				for (const auto& child : ds->loadChildrenSync(id, false)) {
					if (cancelled()) return;
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
					p->processed.fetch_add(1, std::memory_order_relaxed);
				}
			};
			visit(ds->rootId());

			if (!cancelled()) {
				ds->saveMetadata();
			}
			p->done.store(true, std::memory_order_release);
		});
	}
};



// SirenClassifyTask
//
// Owns the background "suggest tags" scan for a SirenBrowserPane: classifies
// every supported file below a given node, collects tag -> file suggestions,
// and once the scan completes shows the tag-confirmation dialog itself (or an
// info dialog if nothing was found). `onApply` is invoked only once the user
// confirms the dialog, with the tags they accepted — that's the pane's cue to
// write them to metadata.
//
// `tagToRels` is written by the worker before `done` is stored with release
// ordering, so step() can read it safely after observing `done` with acquire.
struct SirenClassifyTask {
	using TagToRels = std::map<std::string, std::set<std::string>>;
	using Dialog = StoermelderPackOne::ui::TagConfirmDialog<std::string>;

	struct Progress {
		std::atomic<int> processed{0};
		std::atomic<int> total{0};
		std::atomic<bool> done{false};
		TagToRels tagToRels;
	};
	std::shared_ptr<Progress> progress;

	// `meta` may be null. `rel`/`isDir`/`name` identify the file or directory to scan
	// (`name` is used for the dialog header/summary). `onSelect` is called with
	// startPlay=true when the user clicks a sample label in the result dialog —
	// pass the pane's selectPath(). `onApply` must be set first.
	using SelectCallback = std::function<void(const DataSourceNode&, bool)>;

	// Set by start(), consumed by showResult() once the scan completes.
	std::shared_ptr<DataSource> ds;
	bool isDir = false;
	std::string dirName;
	SelectCallback onSelect;

	// Set by the caller before start(): invoked with the tags the user accepted
	// once they confirm the result dialog. The pane's cue to write them to metadata.
	Dialog::ApplyCallback onApply;

	bool running() const {
		return progress != nullptr;
	}

	// "" if no scan is in progress.
	std::string statusMessage() const {
		if (!progress) return "";
		int processed = progress->processed.load(std::memory_order_relaxed);
		int total = progress->total.load(std::memory_order_relaxed);

		char msg[64];
		if (total > 0) {
			std::snprintf(msg, sizeof(msg), "Analysing\xe2\x80\xa6 %d/%d", processed, total);
		}
		else {
			std::snprintf(msg, sizeof(msg), "Analysing\xe2\x80\xa6");
		}
		return msg;
	}

	// Called from SirenBrowserPane::step(). Returns true if the scan just
	// finished (the result dialog has already been shown).
	bool step() {
		if (progress && progress->done.load(std::memory_order_acquire)) {
			std::shared_ptr<Progress> p = progress;
			progress.reset();
			showResult(p->tagToRels);
			return true;
		}
		return false;
	}

	void start(ITaskWorker* worker, std::shared_ptr<DataSource> ds, MetadataStore* meta,
			const std::string& rel, bool isDir, const std::string& name, SelectCallback onSelect,
			std::shared_ptr<std::atomic<bool>> dsCancel) {
		if (running()) return;

		this->ds = ds;
		this->isDir = isDir;
		this->dirName = name;
		this->onSelect = onSelect;

		auto p = std::make_shared<Progress>();
		progress = p;

		worker->work([p, rel, isDir, ds, meta, dsCancel](std::atomic<bool>& cancel) {
			auto cancelled = [&]() {
				return cancel.load(std::memory_order_relaxed) || dsCancel->load(std::memory_order_relaxed);
			};
			std::vector<std::string> files;
			if (isDir) {
				std::function<void(const std::string&)> collect = [&](const std::string& id) {
					if (cancelled()) return;
					for (const auto& child : ds->loadChildrenSync(id)) {
						if (cancelled()) return;
						if (child.isContainer) {
							collect(child.relativePath);
						}
						else {
							files.push_back(child.relativePath);
						}
					}
				};
				collect(rel);
			}
			else {
				files = {rel};
			}

			p->total = (int)files.size();
			for (const auto& f : files) {
				// Polled per-file so teardown (SirenWidget's destructor sets
				// cancel just before joining the worker thread) or the user
				// cancelling/switching the active source doesn't run the
				// entire remaining file list to completion.
				if (cancelled()) break;

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
						// Per-class threshold from the model; a global 0.5 cut
						// starved recall on imbalanced tags whose calibrated
						// score rarely tops 0.5.
						if (s.score < s.threshold) continue;
						std::string nameLow = rack::string::lowercase(rack::string::trim(s.name));
						if (existing.count(nameLow)) continue;
						p->tagToRels[s.name].insert(f);
					}
				}
				p->processed++;
			}
			p->done.store(true, std::memory_order_release);
		});
	}

	void showResult(const TagToRels& tagToRels) {
		if (tagToRels.empty()) {
			osdialog_message(OSDIALOG_INFO, OSDIALOG_OK, "No new tag assignments found.");
			return;
		}

		Dialog::GroupVector groups;
		for (const auto& pair : tagToRels) {
			groups.push_back({pair.first, pair.second});
		}

		std::string header = isDir ? "Suggest tags — " + dirName : "Suggest tags";
		StoermelderPackOne::ui::openTagConfirmDialog<std::string>(
			header, groups, makeBuildLabel(ds, onSelect), onApply, makeSummaryFn(isDir, dirName));
	}

	// Builds the per-sample label widget shown in each tag row of the result
	// dialog: left-click selects/plays the sample via `onSelect`, right-click
	// offers "Remove from group" (operating on the dialog's own group data).
	static Dialog::BuildLabelCallback makeBuildLabel(std::shared_ptr<DataSource> ds, SelectCallback onSelect) {
		return [ds, onSelect](const std::string& tag, const std::string& fileId) -> widget::Widget* {
			struct SampleLabel : ui::MenuItem {
				DataSource* ds;
				SelectCallback onSelect;
				std::string fileId;
				std::string groupTag;
				void onAction(const event::Action& e) override {
					if (onSelect) onSelect(ds->resolveNode(fileId), true);
					e.unconsume();
				}
				void onButton(const ButtonEvent& e) override {
					if (e.action == GLFW_PRESS && e.button == GLFW_MOUSE_BUTTON_RIGHT) {
						Menu* menu = createMenu();
						menu->addChild(createMenuLabel(ds->getDisplayName(fileId)));
						auto* dlg = getAncestorOfType<Dialog>();
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
			item->text = ds->getDisplayName(fileId);
			item->ds = ds.get();
			item->onSelect = onSelect;
			item->fileId = fileId;
			item->groupTag = tag;
			return item;
		};
	}

	static std::function<std::string(int, int)> makeSummaryFn(bool isDir, std::string name) {
		return [isDir, name](int sel, int items) -> std::string {
			if (isDir) {
				return rack::string::f("%d tag%s across %d file%s", sel, sel == 1 ? "" : "s", items, items == 1 ? "" : "s");
			}
			return rack::string::f("%d tag%s for %s", sel, sel == 1 ? "" : "s", name.c_str());
		};
	}
};

} // namespace Siren
} // namespace StoermelderPackOne
