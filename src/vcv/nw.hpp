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

// ---- Swappable network layer ----
// Every HTTP/URL operation in this namespace routes through NwAccess, so the whole layer is
// replaceable: production uses RealNwAccess (defined in nw.cpp, backed by rack::network);
// the unit-test harness installs a recording mock via `nwAccess` to script responses and
// assert on what was requested, without touching the network.
//
// `requestJson` returns a json_t* the caller owns (json_decref it), or nullptr on failure.
// `requestDownload` returns true on success; `progress` (optional) is updated 0→1 while
// downloading. `encodeUrl`/`urlPath` are pure string helpers.
enum class Method { GET, POST, PUT, DELETE };

struct NwAccess {
	virtual ~NwAccess() {}

	// Returns a json_t* the caller owns (json_decref it), or nullptr on failure.
	virtual json_t* requestJson(Method method, const std::string& url, json_t* dataJ,
	                            const std::map<std::string, std::string>& cookies) { return nullptr; }

	// Returns true if downloaded successfully. `progress` (optional) is updated 0→1.
	virtual bool requestDownload(const std::string& url, const std::string& filename, float* progress,
	                             const std::map<std::string, std::string>& cookies) { return false; }
};


// The production implementation; bodies in the .cpp. Declared here — and `final` — so a
// release build's call sites see the concrete type and devirtualize. See cables.hpp.
struct RealNwAccess final : NwAccess {
	json_t* requestJson(Method method, const std::string& url, json_t* dataJ,
	                    const std::map<std::string, std::string>& cookies) override;
	bool requestDownload(const std::string& url, const std::string& filename, float* progress,
	                     const std::map<std::string, std::string>& cookies) override;
};
// The shared production instance, defined in the .cpp.
extern RealNwAccess realNwAccess;


// Debug builds keep the mockable seam; release resolves the access statically. See
// cables.hpp for why, and for the DEBUGPLUGIN contract.
#ifdef DEBUGPLUGIN
// Null by default -> the shared instance above is used. Tests point this at a mock.
extern NwAccess* nwAccess;
NwAccess& nwAccessFor();
#else
#define nwAccessFor() ::StoermelderPackOne::vcv::realNwAccess
#endif


namespace nw {

P1_UNUSED
static json_t* requestJson(Method method, const std::string& url, json_t* dataJ = NULL,
                           const std::map<std::string, std::string>& cookies = {}) {
	return nwAccessFor().requestJson(method, url, dataJ, cookies);
}

P1_UNUSED
static bool requestDownload(const std::string& url, const std::string& filename, float* progress = NULL,
                            const std::map<std::string, std::string>& cookies = {}) {
	return nwAccessFor().requestDownload(url, filename, progress, cookies);
}

} // namespace nw

} // namespace vcv
} // namespace StoermelderPackOne

#undef P1_UNUSED