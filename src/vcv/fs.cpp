#include "fs.hpp"
#include <cstdio>

namespace StoermelderPackOne {
namespace vcv {

// The production filesystem access, backed by stdio + pluginSettings. Lives here (not in
// vcv_fs.hpp) so that <cstdio> stays out of the header graph.
struct RealFileAccess : FileAccess {
	bool read(const std::string& path, std::string& data) const override {
		FILE* file = std::fopen(path.c_str(), "r");
		if (!file) return false;
		DEFER({ std::fclose(file); });
		char buffer[4096];
		size_t n;
		while ((n = std::fread(buffer, 1, sizeof(buffer), file)) > 0) {
			data.append(buffer, n);
		}
		return true;
	}

	bool write(const std::string& path, const std::string& data) override {
		FILE* file = std::fopen(path.c_str(), "w");
		if (!file) return false;
		DEFER({ std::fclose(file); });
		return std::fwrite(data.data(), 1, data.size(), file) == data.size();
	}

	bool exists(const std::string& path) const override {
		FILE* file = std::fopen(path.c_str(), "r");
		if (!file) return false;
		std::fclose(file);
		return true;
	}

	std::string getLastDir(const std::string& key) const override {
		if (key == "stripDirVcvs") return pluginSettings.stripDirVcvs;
		if (key == "stripDirVcvss") return pluginSettings.stripDirVcvss;
		return "";
	}

	void setLastDir(const std::string& key, const std::string& dir) override {
		if (key == "stripDirVcvs") pluginSettings.stripDirVcvs = dir;
		else if (key == "stripDirVcvss") pluginSettings.stripDirVcvss = dir;
		else return;
		pluginSettings.saveToJson();
	}
};

// The single definition of the swappable filesystem layer's active access — external
// linkage, exactly one definition in this TU, so a mock installed in a test TU is seen by
// code compiled into the plugin dylib.
FileAccess* fileAccess = nullptr;

FileAccess& fileAccessFor() {
	static RealFileAccess realAccess;
	return fileAccess ? *fileAccess : realAccess;
}

} // namespace vcv
} // namespace StoermelderPackOne
