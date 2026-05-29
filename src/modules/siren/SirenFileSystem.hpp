#pragma once
#include <rack.hpp>
#include <ghc/filesystem.hpp>
#include "SirenDataSource.hpp"
#include "SirenAudio.hpp"

// dr_libs — declarations only (implementations compiled in SirenDrLibs.cpp)
#include "../../../dep/drlibs/dr_wav.h"
#include "../../../dep/drlibs/dr_flac.h"
#include "../../../dep/drlibs/dr_mp3.h"

namespace StoermelderPackOne {
namespace Siren {

static const std::vector<std::string> SUPPORTED_EXTENSIONS = { ".wav", ".WAV", ".flac", ".FLAC", ".mp3", ".MP3" };

inline bool isSupportedAudioFile(const std::string& path) {
	std::string ext = rack::system::getExtension(rack::system::getFilename(path));
	for (const std::string& e : SUPPORTED_EXTENSIONS)
		if (ext == e) return true;
	return false;
}

// ─── format-specific audio I/O (only place dr_libs are used directly) ────────

// Decode header-only metadata (fast; does not decode PCM).
inline bool loadAudioInfo(const std::string& path, AudioInfo& out) {
	std::string ext = rack::system::getExtension(rack::system::getFilename(path));
	for (char& c : ext) c = (char)tolower(c);

	if (ext == ".wav") {
		drwav wav;
		if (!drwav_init_file(&wav, path.c_str(), nullptr)) return false;
		out.sampleRate      = (int)wav.sampleRate;
		out.channels        = (int)wav.channels;
		out.bitDepth        = (int)wav.bitsPerSample;
		out.frameCount      = (int64_t)wav.totalPCMFrameCount;
		out.durationSeconds = wav.sampleRate > 0
		                    ? (float)wav.totalPCMFrameCount / (float)wav.sampleRate : 0.f;
		drwav_uninit(&wav);
		return true;
	}
	if (ext == ".flac") {
		drflac* flac = drflac_open_file(path.c_str(), nullptr);
		if (!flac) return false;
		out.sampleRate      = (int)flac->sampleRate;
		out.channels        = (int)flac->channels;
		out.bitDepth        = (int)flac->bitsPerSample;
		out.frameCount      = (int64_t)flac->totalPCMFrameCount;
		out.durationSeconds = flac->sampleRate > 0
		                    ? (float)flac->totalPCMFrameCount / (float)flac->sampleRate : 0.f;
		drflac_close(flac);
		return true;
	}
	if (ext == ".mp3") {
		drmp3 mp3;
		if (!drmp3_init_file(&mp3, path.c_str(), nullptr)) return false;
		out.sampleRate      = (int)mp3.sampleRate;
		out.channels        = (int)mp3.channels;
		out.bitDepth        = 16;
		out.frameCount      = (int64_t)drmp3_get_pcm_frame_count(&mp3);
		out.durationSeconds = mp3.sampleRate > 0
		                    ? (float)out.frameCount / (float)mp3.sampleRate : 0.f;
		drmp3_uninit(&mp3);
		return true;
	}
	return false;
}

// Decode full PCM to interleaved float samples normalised to [-1, 1].
// Returns false if the file cannot be opened or decoded.
inline bool decodeAudioF32(const std::string& path,
                           std::vector<float>& samples,
                           int& channels, int& sampleRate) {
	std::string ext = rack::system::getExtension(rack::system::getFilename(path));
	for (char& c : ext) c = (char)tolower(c);

	if (ext == ".wav") {
		drwav wav;
		if (!drwav_init_file(&wav, path.c_str(), nullptr)) return false;
		channels        = (int)wav.channels;
		sampleRate      = (int)wav.sampleRate;
		int64_t frames  = (int64_t)wav.totalPCMFrameCount;
		samples.resize((size_t)(frames * channels));
		drwav_read_pcm_frames_f32(&wav, (drwav_uint64)frames, samples.data());
		drwav_uninit(&wav);
		return channels > 0 && frames > 0;
	}
	if (ext == ".flac") {
		drflac* flac = drflac_open_file(path.c_str(), nullptr);
		if (!flac) return false;
		channels        = (int)flac->channels;
		sampleRate      = (int)flac->sampleRate;
		int64_t frames  = (int64_t)flac->totalPCMFrameCount;
		samples.resize((size_t)(frames * channels));
		drflac_uint64 read = drflac_read_pcm_frames_f32(flac, (drflac_uint64)frames, samples.data());
		drflac_close(flac);
		samples.resize((size_t)(read * channels));
		return channels > 0 && read > 0;
	}
	if (ext == ".mp3") {
		drmp3 mp3;
		if (!drmp3_init_file(&mp3, path.c_str(), nullptr)) return false;
		channels        = (int)mp3.channels;
		sampleRate      = (int)mp3.sampleRate;
		int64_t frames  = (int64_t)drmp3_get_pcm_frame_count(&mp3);
		samples.resize((size_t)(frames * channels));
		drmp3_read_pcm_frames_f32(&mp3, (drmp3_uint64)frames, samples.data());
		drmp3_uninit(&mp3);
		return channels > 0 && frames > 0;
	}
	return false;
}

// ─── FileSystemAudioStream ───────────────────────────────────────────────────
// Keeps a dr_libs decoder open so frames are read on demand — no full-file buffer.

struct FileSystemAudioStream : AudioStream {
	enum class Fmt { WAV, FLAC, MP3 } fmt;

	drwav   wav  = {};
	drflac* flac = nullptr;
	drmp3   mp3  = {};

	int     ch = 0, sr = 0;
	int64_t total = 0;

	~FileSystemAudioStream() override {
		if (fmt == Fmt::WAV)               drwav_uninit(&wav);
		else if (fmt == Fmt::FLAC && flac) drflac_close(flac);
		else if (fmt == Fmt::MP3)          drmp3_uninit(&mp3);
	}

	int     channels()    const override { return ch; }
	int     sampleRate()  const override { return sr; }
	int64_t totalFrames() const override { return total; }

	int64_t readF32(float* buffer, int64_t frameCount) override {
		if (fmt == Fmt::WAV)
			return (int64_t)drwav_read_pcm_frames_f32(&wav, (drwav_uint64)frameCount, buffer);
		if (fmt == Fmt::FLAC)
			return (int64_t)drflac_read_pcm_frames_f32(flac, (drflac_uint64)frameCount, buffer);
		if (fmt == Fmt::MP3)
			return (int64_t)drmp3_read_pcm_frames_f32(&mp3, (drmp3_uint64)frameCount, buffer);
		return 0;
	}

	bool seekTo(int64_t frameIndex) override {
		if (fmt == Fmt::WAV)
			return drwav_seek_to_pcm_frame(&wav, (drwav_uint64)frameIndex) == DRWAV_TRUE;
		if (fmt == Fmt::FLAC)
			return drflac_seek_to_pcm_frame(flac, (drflac_uint64)frameIndex) != 0;
		if (fmt == Fmt::MP3)
			return drmp3_seek_to_pcm_frame(&mp3, (drmp3_uint64)frameIndex) != 0;
		return false;
	}
};

// ─── FileSystemDataSource ─────────────────────────────────────────────────────

struct FileSystemDataSource : DataSource {
	std::string root;
	RootMetadata metadata_;

	explicit FileSystemDataSource(const std::string& rootPath) : root(rootPath) {
		metadata_.rootPath = root;
		metadata_.load(metadataFilePath());
	}

	~FileSystemDataSource() override {
		saveMetadata();
	}

	std::string metadataFilePath() const {
		std::string dir = rack::asset::user("Stoermelder-P1");
		return dir + "/siren-" + hashPath(root) + ".json";
	}

	RootMetadata* getMetadata() override { return &metadata_; }

	void saveMetadata() override {
		rack::system::createDirectories(rack::asset::user("Stoermelder-P1"));
		metadata_.save(metadataFilePath());
	}

	std::string rootPath() const override { return root; }

	bool isSupportedFile(const std::string& path) const override {
		return isSupportedAudioFile(path);
	}

	std::vector<DataSourceNode> loadChildrenSync(const std::string& dirPath) override {
		std::vector<DataSourceNode> result;
		ghc::filesystem::path base(root);

		auto scan = [&](const std::string& path) {
			try {
				for (const auto& entry : ghc::filesystem::directory_iterator(path)) {
					DataSourceNode node;
					node.fullPath = entry.path().string();
					node.name = entry.path().filename().string();
					node.isDirectory = entry.is_directory();
					// Relative path from root, starting with '/'
					auto rel = entry.path().lexically_relative(base);
					node.relativePath = "/" + rel.generic_string();
					if (node.isDirectory || isSupportedAudioFile(node.fullPath)) {
						if (!node.isDirectory) {
							AudioInfo ai;
							if (::StoermelderPackOne::Siren::loadAudioInfo(node.fullPath, ai))
								node.durationSeconds = ai.durationSeconds;
						}
						result.push_back(std::move(node));
					}
				}
			}
			catch (...) {}
			std::sort(result.begin(), result.end(), [](const DataSourceNode& a, const DataSourceNode& b) {
				// Directories first, then files; both alphabetical
				if (a.isDirectory != b.isDirectory) return a.isDirectory > b.isDirectory;
				return rack::string::lowercase(a.name) < rack::string::lowercase(b.name);
			});
		};
		scan(dirPath.empty() ? root : dirPath);
		return result;
	}

	void loadChildrenAsync(const std::string& path, TaskWorker& worker,
			std::function<void(std::vector<DataSourceNode>)> onDone) override {
		std::string scanPath = path.empty() ? root : path;
		std::string rootCopy = root;
		worker.work([scanPath, rootCopy, onDone]() {
			std::vector<DataSourceNode> result;
			ghc::filesystem::path base(rootCopy);
			try {
				for (const auto& entry : ghc::filesystem::directory_iterator(scanPath)) {
					DataSourceNode node;
					node.fullPath = entry.path().string();
					node.name = entry.path().filename().string();
					node.isDirectory = entry.is_directory();
					auto rel = entry.path().lexically_relative(base);
					node.relativePath = "/" + rel.generic_string();
					if (node.isDirectory || isSupportedAudioFile(node.fullPath)) {
						if (!node.isDirectory) {
							AudioInfo ai;
							if (::StoermelderPackOne::Siren::loadAudioInfo(node.fullPath, ai))
								node.durationSeconds = ai.durationSeconds;
						}
						result.push_back(std::move(node));
					}
				}
			}
			catch (...) {}
			std::sort(result.begin(), result.end(), [](const DataSourceNode& a, const DataSourceNode& b) {
				if (a.isDirectory != b.isDirectory) return a.isDirectory > b.isDirectory;
				return rack::string::lowercase(a.name) < rack::string::lowercase(b.name);
			});
			onDone(std::move(result));
		});
	}

	std::string getDisplayName(const std::string& id) const override {
		return rack::system::getFilename(id);
	}

	std::string getRelativePath(const std::string& id) const override {
		ghc::filesystem::path rel = ghc::filesystem::path(id)
		                            .lexically_relative(ghc::filesystem::path(root));
		return "/" + rel.generic_string();
	}

	int64_t getTimestamp(const std::string& id) const override {
		return getFileTimestamp(id);
	}

	bool loadAudioInfo(const std::string& id, AudioInfo& out) const override {
		return ::StoermelderPackOne::Siren::loadAudioInfo(id, out);
	}

	bool decodeAudioF32(const std::string& id,
	                    std::vector<float>& samples,
	                    int& channels, int& sampleRate) const override {
		return ::StoermelderPackOne::Siren::decodeAudioF32(id, samples, channels, sampleRate);
	}

	std::unique_ptr<AudioStream> openAudioStream(const std::string& id) const override {
		std::string ext = rack::system::getExtension(rack::system::getFilename(id));
		for (char& c : ext) c = (char)tolower(c);

		auto s = std::unique_ptr<FileSystemAudioStream>(new FileSystemAudioStream());
		if (ext == ".wav") {
			if (!drwav_init_file(&s->wav, id.c_str(), nullptr)) return nullptr;
			s->fmt = FileSystemAudioStream::Fmt::WAV;
			s->ch  = (int)s->wav.channels;
			s->sr  = (int)s->wav.sampleRate;
			s->total = (int64_t)s->wav.totalPCMFrameCount;
		} else if (ext == ".flac") {
			s->flac = drflac_open_file(id.c_str(), nullptr);
			if (!s->flac) return nullptr;
			s->fmt = FileSystemAudioStream::Fmt::FLAC;
			s->ch  = (int)s->flac->channels;
			s->sr  = (int)s->flac->sampleRate;
			s->total = (int64_t)s->flac->totalPCMFrameCount;
		} else if (ext == ".mp3") {
			if (!drmp3_init_file(&s->mp3, id.c_str(), nullptr)) return nullptr;
			s->fmt = FileSystemAudioStream::Fmt::MP3;
			s->ch  = (int)s->mp3.channels;
			s->sr  = (int)s->mp3.sampleRate;
			s->total = (int64_t)drmp3_get_pcm_frame_count(&s->mp3);
		} else {
			return nullptr;
		}
		return s;
	}

	void appendNodeMenuItems(ui::Menu* menu, const DataSourceNode& node,
	                         std::function<void()> onChanged) override {
		if (node.isDirectory) {
			std::string dirPath = node.fullPath;
			menu->addChild(createMenuItem("Show folder", "", [dirPath]() {
				rack::system::openDirectory(dirPath);
			}));

			menu->addChild(new ui::MenuSeparator);

			// Sticky submenu: scan directory, then show all tags; clicking adds/removes
			// the tag for every direct audio file in the folder.
			menu->addChild(Rack::createStickySubmenuItem("Tag all samples", "", [this, dirPath, onChanged](ui::Menu* tagMenu) {
				// Scan for direct audio children (runs on UI thread; acceptable for a menu action)
				auto children = loadChildrenSync(dirPath);
				std::vector<std::string> audioRels;
				for (const auto& child : children)
					if (!child.isDirectory)
						audioRels.push_back(child.relativePath);

				if (audioRels.empty()) {
					tagMenu->addChild(createMenuLabel("No audio files in folder"));
					return;
				}

				auto allTagsSet = metadata_.allTags();
				std::vector<std::string> sorted(allTagsSet.begin(), allTagsSet.end());
				std::sort(sorted.begin(), sorted.end());

				for (const std::string& tag : sorted) {
					// Check whether ALL files already carry this tag
					bool allHave = true;
					for (const auto& rel : audioRels) {
						auto fileTags = metadata_.getTags(rel);
						if (std::find(fileTags.begin(), fileTags.end(), tag) == fileTags.end()) {
							allHave = false;
							break;
						}
					}

					struct FolderTagItem : ui::MenuItem {
						FileSystemDataSource* src;
						std::vector<std::string> rels;
						std::string tag;
						bool wasAllHave;
						std::function<void()> onChanged;
						void onAction(const event::Action& e) override {
							if (wasAllHave)
								for (const auto& rel : rels) src->metadata_.removeTag(rel, tag);
							else
								for (const auto& rel : rels) src->metadata_.addTag(rel, tag);
							src->saveMetadata();
							if (onChanged) onChanged();
						}
					};

					FolderTagItem* item   = new FolderTagItem;
					item->text            = toTitleCase(tag);
					item->rightText       = CHECKMARK(allHave);
					item->src             = this;
					item->rels            = audioRels;
					item->tag             = tag;
					item->wasAllHave      = allHave;
					item->onChanged       = onChanged;
					tagMenu->addChild(item);
				}
			}));
		}
		else {
			// File: open the folder it lives in
			std::string dir = ghc::filesystem::path(node.fullPath).parent_path().string();
			menu->addChild(createMenuItem("Open containing folder", "", [dir]() {
				rack::system::openDirectory(dir);
			}));

			std::string rel = node.relativePath;

			menu->addChild(new ui::MenuSeparator);

			// Favorite toggle
			menu->addChild(createCheckMenuItem("Favorite", "",
				[this, rel]() { return metadata_.isFavorite(rel); },
				[this, rel, onChanged]() {
					metadata_.setFavorite(rel, !metadata_.isFavorite(rel));
					saveMetadata();
					if (onChanged) onChanged();
				}
			));

			menu->addChild(new ui::MenuSeparator);
			menu->addChild(createMenuLabel("Tags"));

			// Text field for adding a new tag
			struct FileNewTagField : ui::TextField {
				RootMetadata* metadata;
				std::string rel;
				FileSystemDataSource* src;
				std::function<void()> onChanged;
				void onSelectKey(const event::SelectKey& e) override {
					if (e.action == GLFW_PRESS && e.key == GLFW_KEY_ENTER) {
						std::string tag = rack::string::trim(text);
						if (!tag.empty()) {
							metadata->addTag(rel, tag);
							src->saveMetadata();
							if (onChanged) onChanged();
						}
						ui::MenuOverlay* overlay = getAncestorOfType<ui::MenuOverlay>();
						if (overlay) overlay->requestDelete();
						e.consume(this);
						return;
					}
					if (!e.getTarget()) ui::TextField::onSelectKey(e);
				}
			};
			FileNewTagField* ntf = new FileNewTagField;
			ntf->box.size.x  = 150.f;
			ntf->placeholder = "New tag...";
			ntf->metadata    = &metadata_;
			ntf->rel         = rel;
			ntf->src         = this;
			ntf->onChanged   = onChanged;
			menu->addChild(ntf);
			APP->event->setSelectedWidget(ntf);

			// All known tags with checkmarks — live toggle per item
			struct FileTagItem : ui::MenuItem {
				RootMetadata* metadata;
				std::string rel;
				std::string tag;
				FileSystemDataSource* src;
				std::function<void()> onChanged;
				void onAction(const event::Action& e) override {
					auto current = metadata->getTags(rel);
					bool has = std::find(current.begin(), current.end(), tag) != current.end();
					if (has) metadata->removeTag(rel, tag);
					else     metadata->addTag(rel, tag);
					src->saveMetadata();
					if (onChanged) onChanged();
					e.unconsume();
				}
				void step() override {
					auto current = metadata->getTags(rel);
					rightText = CHECKMARK(std::find(current.begin(), current.end(), tag) != current.end());
					MenuItem::step();
				}
			};

			auto allTagsSet = metadata_.allTags();
			std::vector<std::string> sorted(allTagsSet.begin(), allTagsSet.end());
			std::sort(sorted.begin(), sorted.end());
			for (const std::string& tag : sorted) {
				FileTagItem* item = new FileTagItem;
				item->text      = toTitleCase(tag);
				item->metadata  = &metadata_;
				item->rel       = rel;
				item->tag       = tag;
				item->src       = this;
				item->onChanged = onChanged;
				menu->addChild(item);
			}
		}
	}
};

} // namespace Siren
} // namespace StoermelderPackOne
