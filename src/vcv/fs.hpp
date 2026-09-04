#pragma once
#include "../plugin.hpp"
#include "selection.hpp" // parseJson (layer 1) — kept visible here so existing vcv::parseJson callers need not change their includes

// Expands to the compiler's unused-attribute, or nothing where it isn't available.
#if defined(__GNUC__) || defined(__clang__)
#define P1_UNUSED __attribute__((unused))
#else
#define P1_UNUSED
#endif

namespace StoermelderPackOne {
namespace vcv {

// ---- Swappable filesystem layer ----
// Every file/disk operation in this namespace routes through FileAccess, so the whole layer
// is replaceable: production uses RealFileAccess (defined in vcv_fs.cpp, backed by stdio and
// pluginSettings); the unit-test harness installs a mock (a std::map<std::string,std::string>)
// via `fileAccess` to script file contents without touching disk.
//
// Deliberately bytes, not JSON. Parsing is the separate pure function vcv::parseJson()
// (selection.hpp, layer 1), so the parse-error path — currently reachable only by handing
// Rack a corrupt file — becomes a plain string-in test.
//
// `read` returns false when the file cannot be opened — distinct from an empty file, which
// reads successfully with empty `data`. (The plan sketched std::optional here; this plugin
// builds with -std=c++11, so the bool + out-param form carries the same distinction.)
struct FileAccess {
	virtual ~FileAccess() {}

	virtual bool read(const std::string& path, std::string& data) const { return false; }
	virtual bool write(const std::string& path, const std::string& data) { return false; }
	virtual bool exists(const std::string& path) const { return false; }

	// Path helpers (pure string manipulation; mockable for root redirection).
	virtual std::string join(const std::string& path1, const std::string& path2) { return path1 + path2; }
	virtual std::string getDirectory(const std::string& path) { return ""; }
	virtual std::string getFilename(const std::string& path) { return ""; }
	virtual std::string getStem(const std::string& path) { return ""; }
	virtual std::string getExtension(const std::string& path) { return ""; }

	// Filesystem queries.
	virtual std::vector<std::string> getEntries(const std::string& dirPath, int depth) { return {}; }
	virtual bool isFile(const std::string& path) { return false; }
	virtual bool isDirectory(const std::string& path) { return false; }
	virtual uint64_t getFileSize(const std::string& path) { return 0; }

	// Filesystem mutations.
	virtual bool rename(const std::string& srcPath, const std::string& destPath) { return false; }
	virtual bool copy(const std::string& srcPath, const std::string& destPath) { return false; }
	virtual bool createDirectory(const std::string& path) { return false; }
	virtual bool createDirectories(const std::string& path) { return false; }
	virtual bool remove(const std::string& path) { return false; }
	virtual int removeRecursively(const std::string& path) { return 0; }

	// Environment / applications.
	virtual std::string getTempDirectory() { return ""; }
	virtual std::string getUserDirectory(const std::string& path) { return ""; }
	virtual double getTime() { return 0.0; }
	virtual void openDirectory(const std::string& path) {}
};


// The production implementation; bodies in the .cpp. Declared here — and `final` — so a
// release build's call sites see the concrete type and devirtualize. See cables.hpp.
struct RealFileAccess final : FileAccess {
	bool read(const std::string& path, std::string& data) const override;
	bool write(const std::string& path, const std::string& data) override;
	bool exists(const std::string& path) const override;
	std::string join(const std::string& path1, const std::string& path2) override;
	std::string getDirectory(const std::string& path) override;
	std::string getFilename(const std::string& path) override;
	std::string getStem(const std::string& path) override;
	std::string getExtension(const std::string& path) override;
	std::vector<std::string> getEntries(const std::string& dirPath, int depth) override;
	bool isFile(const std::string& path) override;
	bool isDirectory(const std::string& path) override;
	uint64_t getFileSize(const std::string& path) override;
	bool rename(const std::string& srcPath, const std::string& destPath) override;
	bool copy(const std::string& srcPath, const std::string& destPath) override;
	bool createDirectory(const std::string& path) override;
	bool createDirectories(const std::string& path) override;
	bool remove(const std::string& path) override;
	int removeRecursively(const std::string& path) override;
	std::string getTempDirectory() override;
	std::string getUserDirectory(const std::string& path) override;
	double getTime() override;
	void openDirectory(const std::string& path) override;
};
// The shared production instance, defined in the .cpp.
extern RealFileAccess realFileAccess;


// Debug builds keep the mockable seam; release resolves the access statically. See
// cables.hpp for why, and for the DEBUGPLUGIN contract.
#ifdef DEBUGPLUGIN
// Null by default -> the shared instance above is used. Tests point this at a mock.
extern FileAccess* fileAccess;
FileAccess& fileAccessFor();
#else
#define fileAccessFor() ::StoermelderPackOne::vcv::realFileAccess
#endif


namespace fs {

P1_UNUSED
static bool read(const std::string& path, std::string& data) {
	return fileAccessFor().read(path, data);
}

P1_UNUSED
static bool write(const std::string& path, const std::string& data) {
	return fileAccessFor().write(path, data);
}

P1_UNUSED
static bool exists(const std::string& path) {
	return fileAccessFor().exists(path);
}

P1_UNUSED
static std::string join(const std::string& path1, const std::string& path2) {
	return fileAccessFor().join(path1, path2);
}

P1_UNUSED
static std::string getDirectory(const std::string& path) {
	return fileAccessFor().getDirectory(path);
}

P1_UNUSED
static std::string getFilename(const std::string& path) {
	return fileAccessFor().getFilename(path);
}

P1_UNUSED
static std::string getStem(const std::string& path) {
	return fileAccessFor().getStem(path);
}

P1_UNUSED
static std::string getExtension(const std::string& path) {
	return fileAccessFor().getExtension(path);
}

P1_UNUSED
static std::vector<std::string> getEntries(const std::string& dirPath, int depth = 0) {
	return fileAccessFor().getEntries(dirPath, depth);
}

P1_UNUSED
static bool isFile(const std::string& path) {
	return fileAccessFor().isFile(path);
}

P1_UNUSED
static bool isDirectory(const std::string& path) {
	return fileAccessFor().isDirectory(path);
}

P1_UNUSED
static uint64_t getFileSize(const std::string& path) {
	return fileAccessFor().getFileSize(path);
}

P1_UNUSED
static bool rename(const std::string& srcPath, const std::string& destPath) {
	return fileAccessFor().rename(srcPath, destPath);
}

P1_UNUSED
static bool copy(const std::string& srcPath, const std::string& destPath) {
	return fileAccessFor().copy(srcPath, destPath);
}

P1_UNUSED
static bool createDirectory(const std::string& path) {
	return fileAccessFor().createDirectory(path);
}

P1_UNUSED
static bool createDirectories(const std::string& path) {
	return fileAccessFor().createDirectories(path);
}

P1_UNUSED
static bool remove(const std::string& path) {
	return fileAccessFor().remove(path);
}

P1_UNUSED
static int removeRecursively(const std::string& path) {
	return fileAccessFor().removeRecursively(path);
}

P1_UNUSED
static std::string getTempDirectory() {
	return fileAccessFor().getTempDirectory();
}

P1_UNUSED
static std::string getUserDirectory(const std::string& path) {
	return fileAccessFor().getUserDirectory(path);
}

P1_UNUSED
static double getTime() {
	return fileAccessFor().getTime();
}

P1_UNUSED
static void openDirectory(const std::string& path) {
	fileAccessFor().openDirectory(path);
}

} // namespace fs

} // namespace vcv
} // namespace StoermelderPackOne

#undef P1_UNUSED
