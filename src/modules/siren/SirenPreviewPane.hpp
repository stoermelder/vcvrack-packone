#pragma once
#include <rack.hpp>
#include "SirenAudio.hpp"
#include "SirenMetadata.hpp"
#include "SirenBrowserPane.hpp"  // for SirenDragState
#include "../../utils/TaskWorker.hpp"

namespace StoermelderPackOne {
namespace Siren {

struct SirenPreviewPane : widget::OpaqueWidget {
	// ── layout ───────────────────────────────────────────────────────────────
	static constexpr float TB_H       = 34.f;  // top bar
	static constexpr float READOUT_H  = 26.f;  // bottom readout bar
	static constexpr float WAVE_X     = 8.f;   // left margin (for L/R labels)

	// ── state ────────────────────────────────────────────────────────────────
	std::string currentPath;
	AudioInfo   info;
	WaveformCache cache;
	std::atomic<bool> cacheReady{false};
	std::atomic<bool> cacheBuilding{false};

	float playheadPos      = 0.f;
	bool  draggingPlayhead = false;

	RootMetadata*  metadata  = nullptr;
	SirenDragState* dragState = nullptr;
	TaskWorker*    worker    = nullptr;

	// Called after any metadata change so the browser pane can refresh
	std::function<void()> onMetadataChanged;

	// Audio playback state (audio thread ↔ UI thread via atomics)
	std::atomic<bool>    playing{false};
	std::atomic<int64_t> playFramePos{0};
	std::vector<float>   audioBuffer;
	int audioChannels   = 0;
	int audioSampleRate = 0;

	// Pending waveform cache from worker
	std::atomic<int> cacheGeneration{0};
	struct PendingCache { WaveformCache cache; int gen = -1; bool valid = false; };
	PendingCache      pendingCache;
	std::atomic<bool> pendingCacheReady{false};

	// Cache dir path (set by module widget)
	std::string cacheDir;

	void init(TaskWorker* tw, SirenDragState* ds) {
		worker    = tw;
		dragState = ds;
	}

	void loadFile(const std::string& path, RootMetadata* meta, bool forceRebuild = false) {
		currentPath = path;
		metadata    = meta;
		cacheReady  = false;
		cacheBuilding = false;
		pendingCacheReady.store(false, std::memory_order_relaxed);
		int gen = ++cacheGeneration;
		playing      = false;
		playFramePos = 0;
		playheadPos  = 0.f;
		audioBuffer.clear();
		audioChannels   = 0;
		audioSampleRate = 0;

		if (path.empty()) return;

		loadAudioInfo(path, info);

		std::string cacheFile = cachePathFor(path);
		if (!forceRebuild) {
			WaveformCache loaded;
			if (loadWaveformCacheFile(cacheFile, path, loaded)) {
				cache      = std::move(loaded);
				cacheReady = true;
				loadAudioBuffer(path);
				return;
			}
		}

		if (!worker) return;
		cacheBuilding = true;
		int pw = (int)box.size.x - (int)WAVE_X - 8;
		if (pw < 64) pw = 512;

		std::string pathCopy     = path;
		std::string cacheCopy    = cacheFile;
		std::string cacheDirCopy = cacheDir;
		worker->work([this, pathCopy, cacheCopy, cacheDirCopy, pw, gen]() {
			WaveformCache built;
			bool ok = buildWaveformCache(pathCopy, pw, built);

			if (ok && !cacheDirCopy.empty()) {
				rack::system::createDirectories(cacheDirCopy);
				saveWaveformCacheFile(cacheCopy, built);
			}

			pendingCache.cache = std::move(built);
			pendingCache.gen   = gen;
			pendingCache.valid = ok;
			pendingCacheReady.store(true, std::memory_order_release);
		});
	}

	void loadAudioBuffer(const std::string& path) {
		if (!worker || path.empty()) return;
		std::string pathCopy = path;
		worker->work([this, pathCopy]() {
			std::string ext = rack::system::getExtension(rack::system::getFilename(pathCopy));
			for (char& c : ext) c = (char)tolower(c);

			std::vector<float> buf;
			int ch = 0, sr = 0;
			int64_t frames = 0;

			if (ext == ".wav") {
				drwav wav;
				if (drwav_init_file(&wav, pathCopy.c_str(), nullptr)) {
					ch = (int)wav.channels; sr = (int)wav.sampleRate;
					frames = (int64_t)wav.totalPCMFrameCount;
					buf.resize((size_t)(frames * ch));
					drwav_read_pcm_frames_f32(&wav, (drwav_uint64)frames, buf.data());
					drwav_uninit(&wav);
				}
			}
			else if (ext == ".flac") {
				drflac* flac = drflac_open_file(pathCopy.c_str(), nullptr);
				if (flac) {
					ch = (int)flac->channels; sr = (int)flac->sampleRate;
					frames = (int64_t)flac->totalPCMFrameCount;
					buf.resize((size_t)(frames * ch));
					drflac_read_pcm_frames_f32(flac, (drflac_uint64)frames, buf.data());
					drflac_close(flac);
				}
			}
			else if (ext == ".mp3") {
				drmp3 mp3;
				if (drmp3_init_file(&mp3, pathCopy.c_str(), nullptr)) {
					ch = (int)mp3.channels; sr = (int)mp3.sampleRate;
					frames = (int64_t)drmp3_get_pcm_frame_count(&mp3);
					buf.resize((size_t)(frames * ch));
					drmp3_read_pcm_frames_f32(&mp3, (drmp3_uint64)frames, buf.data());
					drmp3_uninit(&mp3);
				}
			}

			audioBuffer     = std::move(buf);
			audioChannels   = ch;
			audioSampleRate = sr;
		});
	}

	// Called from audio thread
	bool fillAudio(float* outL, float* outR, int frames, float /*sampleRate*/) {
		if (!playing || audioBuffer.empty() || audioChannels == 0)
			return false;
		int64_t pos   = playFramePos.load();
		int64_t total = (int64_t)(audioBuffer.size() / (size_t)audioChannels);

		for (int f = 0; f < frames; f++) {
			if (pos >= total) { playing = false; break; }
			float l = audioBuffer[(size_t)(pos * audioChannels)];
			float r = (audioChannels >= 2) ? audioBuffer[(size_t)(pos * audioChannels + 1)] : l;
			outL[f] = l * 5.f;
			outR[f] = r * 5.f;
			pos++;
			if ((f & 0xFF) == 0 && total > 0)
				playheadPos = (float)pos / (float)total;
		}
		playFramePos = pos;
		if (pos >= total) playing = false;
		return true;
	}

	void step() override {
		// Consume pending waveform cache
		if (pendingCacheReady.load(std::memory_order_acquire)) {
			pendingCacheReady.store(false, std::memory_order_relaxed);
			if (pendingCache.valid && pendingCache.gen == cacheGeneration.load(std::memory_order_relaxed)) {
				cache         = std::move(pendingCache.cache);
				cacheReady    = true;
				cacheBuilding = false;
				loadAudioBuffer(currentPath);
			}
		}

		widget::OpaqueWidget::step();
	}

	void drawLayer(const DrawArgs& args, int layer) override {
		if (layer != 1) return;

		float w = box.size.x;
		float h = box.size.y;

		// Derived layout values
		float waveY = TB_H + 2.f;
		float waveW = w - WAVE_X - 4.f;
		float waveH = h - waveY - READOUT_H - 4.f;
		if (waveH < 20.f) waveH = 20.f;

		bool isPlaying = playing.load();

		// ── top bar ───────────────────────────────────────────────────────────
		if (!currentPath.empty()) {
			// Play/stop button
			nvgFontSize(args.vg, 14.f);
			nvgFillColor(args.vg, isPlaying
				? nvgRGBf(1.f, 0.85f, 0.1f)
				: nvgRGBf(0.55f, 0.55f, 0.55f));
			nvgText(args.vg, 8.f, 22.f, isPlaying ? "■" : "▶", nullptr);

			// Filename — gold when playing, light when idle
			std::string fname = rack::system::getFilename(currentPath);
			nvgFontSize(args.vg, 12.f);
			nvgFillColor(args.vg, isPlaying
				? nvgRGBf(1.f, 0.85f, 0.1f)
				: nvgRGBf(0.92f, 0.92f, 0.88f));
			float maxFnW = w - 100.f;
			nvgScissor(args.vg, 28.f, 0.f, maxFnW, TB_H);
			nvgText(args.vg, 28.f, 22.f, fname.c_str(), nullptr);
			nvgResetScissor(args.vg);

			// Badges (right-aligned in top bar)
			float bx = w - 6.f;
			nvgFontSize(args.vg, 9.f);
			nvgFillColor(args.vg, nvgRGBf(0.50f, 0.50f, 0.50f));

			if (info.durationSeconds > 0.f) {
				int mm = (int)(info.durationSeconds / 60.f);
				float ss = info.durationSeconds - mm * 60.f;
				std::string dur = rack::string::f("%02d:%05.2f", mm, ss);
				// Right-justify duration
				float dw = dur.size() * 6.2f;
				nvgText(args.vg, bx - dw, 28.f, dur.c_str(), nullptr);
				bx -= dw + 4.f;
			}

			// ch · sr · bit
			std::string badges;
			if (info.bitDepth > 0)   badges = rack::string::f("%dbit", info.bitDepth);
			if (info.sampleRate > 0) badges = rack::string::f("%dk", info.sampleRate / 1000) + (badges.empty() ? "" : " · ") + badges;
			if (info.channels > 0)   badges = std::string(info.channels == 1 ? "MONO" : "STEREO") + (badges.empty() ? "" : " · ") + badges;
			if (!badges.empty()) {
				float bw2 = badges.size() * 5.4f;
				nvgText(args.vg, bx - bw2, 17.f, badges.c_str(), nullptr);
			}
		}

		int numCh = cacheReady ? (int)cache.peaks.size() : info.channels;
		if (numCh < 1) numCh = 1;

		if (cacheReady && !cache.empty()) {
			float chH = waveH / numCh;
			for (int ch = 0; ch < (int)cache.peaks.size(); ch++) {
				float chY  = waveY + ch * chH;
				float midY = chY + chH * 0.5f;
				const auto& chPeaks = cache.peaks[ch];
				int buckets = (int)chPeaks.size();
				if (buckets == 0) continue;

				float bw = waveW / (float)buckets;

				// Filled waveform contour
				nvgBeginPath(args.vg);
				for (int b = 0; b < buckets; b++) {
					float px = WAVE_X + b * bw;
					float py = midY - chPeaks[b].second * chH * 0.44f;
					if (b == 0) nvgMoveTo(args.vg, px, py);
					else        nvgLineTo(args.vg, px, py);
				}
				for (int b = buckets - 1; b >= 0; b--) {
					float px = WAVE_X + b * bw;
					float py = midY - chPeaks[b].first * chH * 0.44f;
					nvgLineTo(args.vg, px, py);
				}
				nvgClosePath(args.vg);
				nvgFillColor(args.vg, nvgRGBf(0.70f, 0.70f, 0.63f));
				nvgFill(args.vg);

				// Zero-line
				nvgBeginPath(args.vg);
				nvgMoveTo(args.vg, WAVE_X, midY);
				nvgLineTo(args.vg, WAVE_X + waveW, midY);
				nvgStrokeColor(args.vg, nvgRGBAf(1.f, 1.f, 1.f, 0.07f));
				nvgStrokeWidth(args.vg, 0.5f);
				nvgStroke(args.vg);

				// Channel label
				nvgFontSize(args.vg, 9.f);
				nvgFillColor(args.vg, nvgRGBAf(1.f, 1.f, 1.f, 0.30f));
				nvgText(args.vg, WAVE_X + 2.f, chY + 11.f, ch == 0 ? "L" : "R", nullptr);
			}
		}
		else if (cacheBuilding) {
			nvgFontSize(args.vg, 10.f);
			nvgFillColor(args.vg, nvgRGBAf(1.f, 1.f, 1.f, 0.28f));
			nvgText(args.vg, WAVE_X + 4.f, waveY + waveH * 0.5f + 4.f, "Building waveform…", nullptr);
		}

		// Tick marks along waveform bottom
		if (info.durationSeconds > 0.f && waveW > 0.f) {
			float dur = info.durationSeconds;
			static const float ivs[] = {0.5f, 1.f, 2.f, 5.f, 10.f, 30.f, 60.f, 300.f};
			float tickIv = 1.f;
			for (float iv : ivs) {
				if (dur / iv <= 14.f) { tickIv = iv; break; }
			}
			float tickY = waveY + waveH;
			for (float t = 0.f; t <= dur + 0.001f; t += tickIv) {
				float tx = WAVE_X + (t / dur) * waveW;
				nvgBeginPath(args.vg);
				nvgMoveTo(args.vg, tx, tickY - 4.f);
				nvgLineTo(args.vg, tx, tickY);
				nvgStrokeColor(args.vg, nvgRGBAf(1.f, 1.f, 1.f, 0.18f));
				nvgStrokeWidth(args.vg, 0.5f);
				nvgStroke(args.vg);
			}
		}

		// Playhead line + triangle pointer
		if (!currentPath.empty()) {
			float phX = WAVE_X + playheadPos * waveW;

			nvgBeginPath(args.vg);
			nvgMoveTo(args.vg, phX, waveY);
			nvgLineTo(args.vg, phX, waveY + waveH);
			nvgStrokeColor(args.vg, nvgRGBf(1.f, 1.f, 1.f));
			nvgStrokeWidth(args.vg, 1.f);
			nvgStroke(args.vg);

			const float ts = 5.f;
			nvgBeginPath(args.vg);
			nvgMoveTo(args.vg, phX - ts, waveY);
			nvgLineTo(args.vg, phX + ts, waveY);
			nvgLineTo(args.vg, phX, waveY + ts * 1.6f);
			nvgClosePath(args.vg);
			nvgFillColor(args.vg, nvgRGBf(1.f, 1.f, 1.f));
			nvgFill(args.vg);
		}

		// ── bottom readout ────────────────────────────────────────────────────
		float bbY = waveY + waveH + 4.f;

		auto formatTime = [](float s) -> std::string {
			if (s < 0.f) s = 0.f;
			int mm = (int)(s / 60.f);
			float ss = s - mm * 60.f;
			return rack::string::f("%02d:%05.2f", mm, ss);
		};

		float pos = playheadPos * info.durationSeconds;
		float col = waveW / 4.f;
		nvgFontSize(args.vg, 10.f);

		auto drawReadout = [&](float x, const char* lbl, float val) {
			nvgFillColor(args.vg, nvgRGBAf(1.f, 1.f, 1.f, 0.38f));
			nvgText(args.vg, x, bbY + 16.f, lbl, nullptr);
			nvgFillColor(args.vg, nvgRGBf(0.88f, 0.88f, 0.83f));
			nvgText(args.vg, x + 20.f, bbY + 16.f, formatTime(val).c_str(), nullptr);
		};

		drawReadout(WAVE_X,           "IN",  0.f);
		drawReadout(WAVE_X + col,     "OUT", info.durationSeconds);
		drawReadout(WAVE_X + col * 2, "LEN", info.durationSeconds);
		drawReadout(WAVE_X + col * 3, "POS", pos);

		// Drag label
		if (dragState && dragState->active) {
			Vec mp = APP->scene->mousePos;
			Vec lp = getRelativeOffset(mp, APP->scene);
			nvgBeginPath(args.vg);
			nvgRoundedRect(args.vg, lp.x + 10.f, lp.y, 150.f, 18.f, 3.f);
			nvgFillColor(args.vg, nvgRGBAf(0.f, 0.f, 0.f, 0.7f));
			nvgFill(args.vg);
			nvgFontSize(args.vg, 10.f);
			nvgFillColor(args.vg, nvgRGBf(1.f, 0.85f, 0.1f));
			std::string lbl = rack::system::getFilename(dragState->dragPath);
			nvgText(args.vg, lp.x + 14.f, lp.y + 12.f, lbl.c_str(), nullptr);
		}
	}

	// ── waveform interaction ──────────────────────────────────────────────────

	Rect waveformRect() const {
		float waveY = TB_H + 2.f;
		float waveW = box.size.x - WAVE_X - 4.f;
		float waveH = box.size.y - waveY - READOUT_H - 4.f;
		if (waveH < 20.f) waveH = 20.f;
		return Rect(Vec(WAVE_X, waveY), Vec(waveW, waveH));
	}

	bool inWaveformArea(Vec pos) const {
		return waveformRect().contains(pos);
	}

	float posToPlayhead(Vec pos) const {
		Rect r = waveformRect();
		return rack::math::clamp((pos.x - r.pos.x) / r.size.x, 0.f, 1.f);
	}

	void startPlaybackFrom(float pos) {
		playheadPos = pos;
		if (!audioBuffer.empty() && audioChannels > 0) {
			int64_t total = (int64_t)(audioBuffer.size() / (size_t)audioChannels);
			playFramePos  = (int64_t)(pos * (float)total);
			playing       = true;
		}
	}

	void createContextMenu() {
		if (currentPath.empty() || !metadata) return;

		ghc::filesystem::path absPath(currentPath);
		std::string rel = "/" + absPath.lexically_relative(
			ghc::filesystem::path(metadata->rootPath)).generic_string();

		ui::Menu* menu = createMenu();

		// File label + open folder
		menu->addChild(createMenuLabel(rack::system::getFilename(currentPath)));
		menu->addChild(new ui::MenuSeparator);

		// Favorite toggle
		menu->addChild(createCheckMenuItem("Favorite", "",
			[this, rel]() { return metadata->isFavorite(rel); },
			[this, rel]() {
				metadata->setFavorite(rel, !metadata->isFavorite(rel));
				if (onMetadataChanged) onMetadataChanged();
			}
		));

		menu->addChild(new ui::MenuSeparator);
		menu->addChild(createMenuLabel("Tags"));

		// Text field for adding a new tag
		struct NewTagField : ui::TextField {
			RootMetadata* metadata;
			std::string rel;
			std::function<void()> onChanged;
			void onSelectKey(const event::SelectKey& e) override {
				if (e.action == GLFW_PRESS && e.key == GLFW_KEY_ENTER) {
					std::string tag = rack::string::trim(text);
					if (!tag.empty()) {
						// Store lowercase for consistency
						std::string lower = tag;
						std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
						metadata->addTag(rel, lower);
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
		NewTagField* ntf = new NewTagField;
		ntf->box.size.x = 150.f;
		ntf->placeholder = "New tag...";
		ntf->metadata = metadata;
		ntf->rel = rel;
		ntf->onChanged = onMetadataChanged;
		menu->addChild(ntf);
		APP->event->setSelectedWidget(ntf);

		// All known tags with checkmarks
		struct TagItem : ui::MenuItem {
			RootMetadata* metadata;
			std::string rel;
			std::string tag;
			std::function<void()> onChanged;
			void onAction(const event::Action& e) override {
				auto current = metadata->getTags(rel);
				bool has = std::find(current.begin(), current.end(), tag) != current.end();
				if (has) metadata->removeTag(rel, tag);
				else     metadata->addTag(rel, tag);
				if (onChanged) onChanged();
				e.unconsume();
			}
			void step() override {
				auto current = metadata->getTags(rel);
				rightText = CHECKMARK(std::find(current.begin(), current.end(), tag) != current.end());
				MenuItem::step();
			}
		};

		auto allTags = metadata->allTags();
		std::vector<std::string> sorted(allTags.begin(), allTags.end());
		std::sort(sorted.begin(), sorted.end());
		for (const std::string& tag : sorted) {
			TagItem* item = new TagItem;
			item->text     = toTitleCase(tag);
			item->metadata = metadata;
			item->rel      = rel;
			item->tag      = tag;
			item->onChanged = onMetadataChanged;
			menu->addChild(item);
		}
	}

	void onButton(const event::Button& e) override {
		if (e.button == GLFW_MOUSE_BUTTON_RIGHT && e.action == GLFW_PRESS) {
			createContextMenu();
			e.consume(this);
			return;
		}
		if (e.button == GLFW_MOUSE_BUTTON_LEFT && e.action == GLFW_PRESS) {
			// Waveform area: place playhead and begin drag
			if (!currentPath.empty() && inWaveformArea(e.pos)) {
				startPlaybackFrom(posToPlayhead(e.pos));
				draggingPlayhead = true;
				e.consume(this);
				return;
			}
			// Play/stop button (left side of top bar)
			if (e.pos.x < 26.f && e.pos.y < TB_H) {
				if (isPlaying()) playing = false;
				else if (!currentPath.empty()) startPlaybackFrom(playheadPos);
				e.consume(this);
				return;
			}
		}
		widget::OpaqueWidget::onButton(e);
	}

	void onDragStart(const event::DragStart& e) override {
		if (!currentPath.empty() && !draggingPlayhead && dragState) {
			dragState->active   = true;
			dragState->dragPath = currentPath;
		}
	}

	void onDragMove(const event::DragMove& e) override {
		if (draggingPlayhead && !currentPath.empty()) {
			Rect r = waveformRect();
			if (r.size.x > 0.f)
				playheadPos = rack::math::clamp(playheadPos + e.mouseDelta.x / r.size.x, 0.f, 1.f);
		}
	}

	void onDragEnd(const event::DragEnd& e) override {
		if (draggingPlayhead) {
			draggingPlayhead = false;
			startPlaybackFrom(playheadPos);
			return;
		}
		if (dragState && dragState->active && !dragState->dragPath.empty()) {
			Vec pos = APP->scene->mousePos;
			std::string path = dragState->dragPath;
			dragState->active = false;
			dragState->dragPath.clear();
			Widget::PathDropEvent pd(std::vector<std::string>{path});
			pd.pos = pos;
			APP->scene->onPathDrop(pd);
		}
	}

	std::string cachePathFor(const std::string& audioPath) const {
		return cacheDir + "/" + hashPath(audioPath) + ".json";
	}

private:
	bool isPlaying() const { return playing.load(); }
};

} // namespace Siren
} // namespace StoermelderPackOne
