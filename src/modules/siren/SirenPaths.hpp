#pragma once
#include <rack.hpp>


namespace StoermelderPackOne {
namespace Siren {

// helper: settings dir / file paths
inline std::string settingsDirPath() {
	return rack::asset::user("Stoermelder-P1");
}

inline std::string sirenFilePath() {
	return settingsDirPath() + "/siren.json";
}

inline std::string sirenCacheDirPath() {
	return settingsDirPath() + "/siren-cache";
}

// Compute 8-char hex hash of a string (for JSON filename derivation)
inline std::string hashPath(const std::string& path) {
	uint32_t h = 2166136261u;
	for (unsigned char c : path) {
		h ^= c;
		h *= 16777619u;
	}
	char buf[9];
	snprintf(buf, sizeof(buf), "%08x", h);
	return std::string(buf);
}

} // namespace Siren
} // namespace StoermelderPackOne
