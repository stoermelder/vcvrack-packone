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

std::string RealFileAccess::join(const std::string& path1, const std::string& path2) {
	return rack::system::join(path1, path2);
}

std::string RealFileAccess::getDirectory(const std::string& path) {
	return rack::system::getDirectory(path);
}

std::string RealFileAccess::getFilename(const std::string& path) {
	return rack::system::getFilename(path);
}

std::string RealFileAccess::getStem(const std::string& path) {
	return rack::system::getStem(path);
}

std::string RealFileAccess::getExtension(const std::string& path) {
	return rack::system::getExtension(path);
}

std::vector<std::string> RealFileAccess::getEntries(const std::string& dirPath, int depth) {
	return rack::system::getEntries(dirPath, depth);
}

bool RealFileAccess::exists(const std::string& path) {
	return rack::system::exists(path);
}

bool RealFileAccess::isFile(const std::string& path) {
	return rack::system::isFile(path);
}

bool RealFileAccess::isDirectory(const std::string& path) {
	return rack::system::isDirectory(path);
}

uint64_t RealFileAccess::getFileSize(const std::string& path) {
	return rack::system::getFileSize(path);
}

bool RealFileAccess::rename(const std::string& srcPath, const std::string& destPath) {
	return rack::system::rename(srcPath, destPath);
}

bool RealFileAccess::copy(const std::string& srcPath, const std::string& destPath) {
	return rack::system::copy(srcPath, destPath);
}

bool RealFileAccess::createDirectory(const std::string& path) {
	return rack::system::createDirectory(path);
}

bool RealFileAccess::createDirectories(const std::string& path) {
	return rack::system::createDirectories(path);
}

bool RealFileAccess::remove(const std::string& path) {
	return rack::system::remove(path);
}

int RealFileAccess::removeRecursively(const std::string& path) {
	return rack::system::removeRecursively(path);
}

std::string RealFileAccess::getTempDirectory() {
	return rack::system::getTempDirectory();
}

double RealFileAccess::getTime() {
	return rack::system::getTime();
}

void RealFileAccess::openDirectory(const std::string& path) {
	rack::system::openDirectory(path);
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
