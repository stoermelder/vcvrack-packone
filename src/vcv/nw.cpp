#include "nw.hpp"

namespace StoermelderPackOne {
namespace vcv {

// The production network access, backed by rack::network. Lives here (not in nw.hpp) so
// that <network.hpp> stays out of the header graph.
json_t* RealNwAccess::requestJson(Method method, const std::string& url, json_t* dataJ,
                                  const std::map<std::string, std::string>& cookies) {
	rack::network::Method m = rack::network::METHOD_GET;
	switch (method) {
		case Method::GET: m = rack::network::METHOD_GET; break;
		case Method::POST: m = rack::network::METHOD_POST; break;
		case Method::PUT: m = rack::network::METHOD_PUT; break;
		case Method::DELETE: m = rack::network::METHOD_DELETE; break;
	}
	return rack::network::requestJson(m, url, dataJ, cookies);
}

bool RealNwAccess::requestDownload(const std::string& url, const std::string& filename, float* progress,
                                   const std::map<std::string, std::string>& cookies) {
	return rack::network::requestDownload(url, filename, progress, cookies);
}

// The shared production instance; namespace-scope so no __cxa_guard is tested on access.
// In a release build this is what the nwAccessFor() macro names directly.
RealNwAccess realNwAccess;


#ifdef DEBUGPLUGIN
// One definition, external linkage: a mock installed in a test TU must be seen by
// code compiled into the dylib. See the declaration in the header.
NwAccess* nwAccess = nullptr;
NwAccess& nwAccessFor() {
	return nwAccess ? *nwAccess : realNwAccess;
}
#endif

} // namespace vcv
} // namespace StoermelderPackOne