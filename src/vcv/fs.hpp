#pragma once
#include "../plugin.hpp"

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
// Deliberately bytes, not JSON. Parsing is the separate pure function parseJson() below,
// so the parse-error path — currently reachable only by handing Rack a corrupt file —
// becomes a plain string-in test.
//
// `read` returns false when the file cannot be opened — distinct from an empty file, which
// reads successfully with empty `data`. (The plan sketched std::optional here; this plugin
// builds with -std=c++11, so the bool + out-param form carries the same distinction.)
struct FileAccess {
	virtual ~FileAccess() {}

	virtual bool read(const std::string& path, std::string& data) const { return false; }
	virtual bool write(const std::string& path, const std::string& data) { return false; }
	virtual bool exists(const std::string& path) const { return false; }

	// The "last used directory" pair that pluginSettings persists. Keyed, so Siren's file
	// browsing and Strip's dialogs can share one access instead of each growing a bespoke
	// accessor.
	virtual std::string getLastDir(const std::string& key) const { return ""; }
	virtual void setLastDir(const std::string& key, const std::string& dir) {}
};


// The production implementation; bodies in the .cpp. Declared here — and `final` — so a
// release build's call sites see the concrete type and devirtualize. See cables.hpp.
struct RealFileAccess final : FileAccess {
	bool read(const std::string& path, std::string& data) const override;
	bool write(const std::string& path, const std::string& data) override;
	bool exists(const std::string& path) const override;
	std::string getLastDir(const std::string& key) const override;
	void setLastDir(const std::string& key, const std::string& dir) override;
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
static std::string getLastDir(const std::string& key) {
	return fileAccessFor().getLastDir(key);
}

P1_UNUSED
static void setLastDir(const std::string& key, const std::string& dir) {
	fileAccessFor().setLastDir(key, dir);
}

} // namespace fs


// Pure JSON parsing (jansson only — no APP, no osdialog, no disk). Returns nullptr and
// fills `errorOut` on failure; caller owns the returned json_t* (json_decref it).
// NB: this belongs to layer 1 (vcv_selection.hpp) eventually; it lives here for now
// because it is what splits "obtain bytes" (FileAccess/UiAccess) from "parse".
inline json_t* parseJson(const std::string& data, std::string& errorOut) {
	json_error_t error;
	json_t* j = json_loads(data.c_str(), 0, &error);
	if (!j) {
		errorOut = string::f("JSON parsing error at %s %d:%d %s", error.source, error.line, error.column, error.text);
	}
	return j;
}

} // namespace vcv
} // namespace StoermelderPackOne

#undef P1_UNUSED
