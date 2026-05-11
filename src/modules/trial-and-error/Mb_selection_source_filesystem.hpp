#pragma once
#include "Mb_selection_source.hpp"

namespace StoermelderPackOne {
namespace Mb {
namespace selection {
namespace filesystem {

/**
 * Concrete SelectionSource that reads from the local file system.
 * This is a drop-in replacement for the previous inline file-system logic.
 */
struct FileSystemSelectionSource : SelectionSource {
	std::string rootContainer_;
	std::string currentContainer_;

	static constexpr const char* SLUG = "filesystem";

	/** Show a folder picker dialog, returning the chosen path or empty on cancel. */
	static std::string selectFolder() {
		std::string dir = asset::user("selections");
		char* path = osdialog_file(OSDIALOG_OPEN_DIR, dir.c_str(), NULL, NULL);
		if (!path) return "";
		std::string result(path);
		free(path);
		return result;
	}

	void onAttach() override {
		// If no root is set, default to the user's selections directory
		if (rootContainer_.empty()) {
			rootContainer_ = currentContainer_ = asset::user("selections");
		}
	}

	std::string getContainer() const override { return currentContainer_; }
	void setContainer(const std::string& path) override { currentContainer_ = path; }

	std::string getRootContainer() const override { return rootContainer_; }
	void setRootContainer(const std::string& path) override { rootContainer_ = path; }

	std::vector<std::string> getEntries(const std::string& folder) override {
		return system::getEntries(folder);
	}

	bool isDirectory(const std::string& path) override {
		return system::isDirectory(path);
	}

	bool isFile(const std::string& path) override {
		return system::isFile(path);
	}

	std::string getParentContainer(const std::string& path) override {
		return system::getDirectory(path);
	}

	std::string getFilename(const std::string& path) override {
		return system::getFilename(path);
	}

	json_t* toJson() const override {
		json_t* j = json_object();
		json_object_set_new(j, "type", json_string(SLUG));
		json_object_set_new(j, "rootContainer", json_string(rootContainer_.c_str()));
		json_object_set_new(j, "currentContainer", json_string(currentContainer_.c_str()));
		return j;
	}

	bool fromJson(json_t* sourceJ) override {
		json_t* typeJ = json_object_get(sourceJ, "type");
		if (!typeJ || std::string(json_string_value(typeJ)) != SLUG)
			return false;

		json_t* rootJ = json_object_get(sourceJ, "rootContainer");
		if (rootJ) rootContainer_ = json_string_value(rootJ);

		json_t* currentJ = json_object_get(sourceJ, "currentContainer");
		if (currentJ) currentContainer_ = json_string_value(currentJ);

		return true;
	}

	std::string getSourceType() const override {
		return SLUG;
	}

	std::string getName() const override {
		if (!rootContainer_.empty()) return string::f("Folder %s", rootContainer_.c_str());
		return "(no folder)";
	}

	SelectionSource* createSource() const override {
		std::string path = selectFolder();
		if (path.empty()) return nullptr;
		FileSystemSelectionSource* src = new FileSystemSelectionSource;
		src->setRootContainer(path);
		src->setContainer(path);
		return src;
	}

	void appendMenuItems(ui::Menu* menu) override {
		menu->addChild(createMenuLabel(getRootContainer().empty() ? "(no folder selected)" : string::f("Root %s", getRootContainer().c_str())));
		menu->addChild(createMenuItem("Select root folder...", "", [=]() {
			std::string path = selectFolder();
			if (path.empty()) return;
			setRootContainer(path);
			setContainer(path);
		}));
		if (!rootContainer_.empty()) {
			menu->addChild(createMenuItem("Open in file explorer", "", [=]() {
				system::openDirectory(rootContainer_);
			}));
		}
	}
};

inline extern std::string getSlug() {
    return FileSystemSelectionSource::SLUG;
}
inline SelectionSource* getSource() {
    return new FileSystemSelectionSource;
}

} // namespace filesystem
} // namespace selection
} // namespace Mb
} // namespace StoermelderPackOne