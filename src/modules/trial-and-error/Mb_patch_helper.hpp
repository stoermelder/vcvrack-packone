#pragma once
#include <widget/Widget.hpp>
#include <plugin.hpp>
#include <helpers.hpp>
#include <map>
#include <memory>
#include "../../vcv/api.hpp"

namespace StoermelderPackOne {
namespace Mb {
namespace patch {

struct PatchHelperWidget : widget::Widget {
	std::string cacheDir;
	std::string pendingPatchPath;
	bool pendingMbModuleCheck = false;

	/** Global cache map for all datasources.
	 * Key is datasource identifier (e.g., root path for filesystem, "patchstorage" for API).
	 * Value is shared_ptr<void> holding any cached object (json_t, string, etc.).
	 * Custom deleters ensure proper cleanup (e.g., json_decref for json_t).
	 * PatchHelperWidget clears this on application exit.
	 */
	typedef void(*CacheDeleter)(void*);
	struct CacheEntry {
		std::shared_ptr<void> data;
		CacheDeleter deleter;
	};

	static std::map<std::string, CacheEntry>& getGlobalCacheMap() {
		static std::map<std::string, CacheEntry> cache;
		return cache;
	}

	static PatchHelperWidget* getInstance() {
		return APP->scene->menuBar->getFirstDescendantOfType<PatchHelperWidget>();
	}

	/** Register a shared cache entry for a datasource key.
	 * @tparam T The type of the cached data
	 * @param key Unique identifier for the datasource
	 * @param data Shared pointer to the cached data
	 * @param deleter Function to call when the cache entry is destroyed
	 */
	template<typename T>
	void setGlobalCache(const std::string& key, std::shared_ptr<T> data, CacheDeleter deleter) {
		auto& cacheMap = getGlobalCacheMap();
		CacheEntry entry{std::static_pointer_cast<void>(data), deleter};
		cacheMap[key] = entry;
	}

	/** Get a shared cache entry by key, casting to the specified type.
	 * @tparam T The type to cast the cached data to
	 * @param key Unique identifier for the datasource
	 * @return Shared pointer to the cached data, or empty if not found
	 */
	template<typename T>
	std::shared_ptr<T> getGlobalCache(const std::string& key) {
		auto& cacheMap = getGlobalCacheMap();
		auto it = cacheMap.find(key);
		if (it != cacheMap.end())
			return std::static_pointer_cast<T>(it->second.data);
		return std::shared_ptr<T>();
	}

	PatchHelperWidget() {
		cacheDir = vcv::fs::join(vcv::fs::getTempDirectory(), "mb_patch_cache");
		vcv::fs::createDirectories(cacheDir);
	}

	~PatchHelperWidget() {
		// Clear all datasource caches on application exit.
		// The shared_ptr<void> will call its stored deleter on reset/destruction.
		auto& cacheMap = getGlobalCacheMap();
		cacheMap.clear();

		if (!cacheDir.empty() && vcv::fs::exists(cacheDir)) {
			vcv::fs::removeRecursively(cacheDir);
		}
	}

	void step() override {
		widget::Widget::step();

		if (!pendingPatchPath.empty()) {
			std::string path = pendingPatchPath;
			pendingPatchPath.clear();

			// Trigger the actual patch load
			const std::vector<std::string>& paths = {path};
			const Widget::PathDropEvent e(paths);
			APP->scene->onPathDrop(e);

			// Delay Mb module check by one frame
			pendingMbModuleCheck = true;
		}

		if (pendingMbModuleCheck) {
			pendingMbModuleCheck = false;
			checkAndAddMbModule();
		}
	}

	void setPendingPatchPath(const std::string& path) {
		pendingPatchPath = path;
	}

	void checkAndAddMbModule() {
		// Get all modules in the current rack via the swappable module layer
		bool hasMbModule = false;
		for (ModuleWidget* mw : vcv::getModuleWidgets()) {
			if (mw && mw->model && mw->model->slug == "Mb") {
				hasMbModule = true;
				break;
			}
		}

		if (!hasMbModule) {
            // Calculate position based on existing modules
            Vec mbSize(0, 0);
            float minX = std::numeric_limits<float>::max();
            float minY = std::numeric_limits<float>::max();

            for (ModuleWidget* mw : vcv::getModuleWidgets()) {
                minX = std::min(minX, mw->box.pos.x);
                minY = std::min(minY, mw->box.pos.y);
            }

            // Create module to get its size
            Module* mbModule = modelMb->createModule();
            APP->engine->addModule(mbModule);
            ModuleWidget* mw = modelMb->createModuleWidget(mbModule);
            mbSize = mw->box.size;

            // Place to the left of the leftmost module, at same y as topmost
            mw->box.pos = Vec(minX - mbSize.x - 4 * RACK_GRID_WIDTH, minY);

            APP->scene->rack->addModule(mw);
            APP->scene->rack->setModulePosForce(mw, mw->box.pos);
		}
	}
};

} // namespace patch
} // namespace Mb
} // namespace StoermelderPackOne