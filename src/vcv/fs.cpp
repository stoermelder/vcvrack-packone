#include "fs.hpp"
#include <cstdio>

namespace StoermelderPackOne {
namespace vcv {

// The production filesystem access, backed by stdio + pluginSettings. Lives here (not in
// vcv_fs.hpp) so that <cstdio> stays out of the header graph.
bool RealFileAccess::read(const std::string& path, std::string& data) const {
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

bool RealFileAccess::write(const std::string& path, const std::string& data) {
	FILE* file = std::fopen(path.c_str(), "w");
	if (!file) return false;
	DEFER({ std::fclose(file); });
	return std::fwrite(data.data(), 1, data.size(), file) == data.size();
}

bool RealFileAccess::exists(const std::string& path) const {
	FILE* file = std::fopen(path.c_str(), "r");
	if (!file) return false;
	std::fclose(file);
	return true;
}

std::string RealFileAccess::getLastDir(const std::string& key) const {
	if (key == "stripDirVcvs") return pluginSettings.stripDirVcvs;
	if (key == "stripDirVcvss") return pluginSettings.stripDirVcvss;
	return "";
}

void RealFileAccess::setLastDir(const std::string& key, const std::string& dir) {
	if (key == "stripDirVcvs") pluginSettings.stripDirVcvs = dir;
	else if (key == "stripDirVcvss") pluginSettings.stripDirVcvss = dir;
	else return;
	pluginSettings.saveToJson();
}

// The shared production instance; namespace-scope so no __cxa_guard is tested on access.
// In a release build this is what the fileAccessFor() macro names directly.
RealFileAccess realFileAccess;


#ifdef DEBUGPLUGIN
// One definition, external linkage: a mock installed in a test TU must be seen by
// code compiled into the dylib. See the declaration in the header.
FileAccess* fileAccess = nullptr;
FileAccess& fileAccessFor() {
	return fileAccess ? *fileAccess : realFileAccess;
}
#endif

} // namespace vcv
} // namespace StoermelderPackOne
