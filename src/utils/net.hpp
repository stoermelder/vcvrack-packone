#pragma once
#include <cctype>
#include <string>

namespace StoermelderPackOne {

static inline void trimStr(std::string &s) {
	// left
	size_t i = 0;
	while (i < s.size() && std::isspace((unsigned char)s[i])) ++i;
	s.erase(0, i);
	// right
	if (!s.empty()) {
		size_t j = s.size() - 1;
		while (j != (size_t)-1 && std::isspace((unsigned char)s[j])) --j;
		s.erase(j + 1);
	}
}

static inline bool isValidPort(const std::string &s) {
	if (s.empty()) return true;
	for (char c : s) if (!std::isdigit((unsigned char)c)) return false;
	long p = 0;
	try { p = std::stol(s); } catch (...) { return false; }
	return (p >= 1 && p <= 65535);
}

} // namespace StoermelderPackOne
