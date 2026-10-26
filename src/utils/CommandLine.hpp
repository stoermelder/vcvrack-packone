#pragma once
#include <cctype>
#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <utility>

namespace StoermelderPackOne {

// A parsed command datagram: the name and the `;`-separated parts of the value.
// Allocation-free.
struct CommandLine {
	static constexpr int MAX_PARTS = 4;

	// Allocation-free counterpart of Orca's `Param`: views into the caller's buffer.
	struct Args {
		const char* part[MAX_PARTS] = {};
		size_t partLen[MAX_PARTS] = {};
		int parts = 0;

		// False if the part is missing, empty or not a whole decimal integer.
		bool getInt(int i, int32_t& out) const {
			if (i < 0 || i >= parts || partLen[i] == 0) return false;
			char buf[16];
			if (partLen[i] >= sizeof(buf)) return false;
			memcpy(buf, part[i], partLen[i]);
			buf[partLen[i]] = '\0';
			char* end = nullptr;
			errno = 0;
			long v = strtol(buf, &end, 10);
			if (end == buf || *end != '\0' || errno == ERANGE || v < INT32_MIN || v > INT32_MAX) return false;
			out = (int32_t)v;
			return true;
		}
	};

	char name[16];
	size_t nameLen = 0;
	Args args;

	// The name is the text before the first `:` with only word characters kept,
	// lowercased; one longer than any command, or empty, fails.
	bool parse(const char* text, size_t len) {
		const char* colon = (const char*)memchr(text, ':', len);
		size_t nameEnd = colon ? (size_t)(colon - text) : len;

		nameLen = 0;
		for (size_t i = 0; i < nameEnd; i++) {
			unsigned char c = (unsigned char)text[i];
			if (!isalnum(c) && c != '_') continue;
			if (nameLen >= sizeof(name) - 1) return false;
			name[nameLen++] = (char)tolower(c);
		}
		name[nameLen] = '\0';
		if (nameLen == 0) return false;

		args = Args();
		if (colon) {
			const char* p = colon + 1;
			const char* end = text + len;
			while (args.parts < MAX_PARTS) {
				const char* sep = (const char*)memchr(p, ';', end - p);
				const char* partEnd = sep ? sep : end;
				args.part[args.parts] = p;
				args.partLen[args.parts] = (size_t)(partEnd - p);
				args.parts++;
				if (!sep) break;
				p = sep + 1;
			}
		}
		return true;
	}

	// The command's full name, or (unless `alias` is false) its first two characters.
	bool is(const char* full, bool alias = true) const {
		return strcmp(full, name) == 0 || (alias && nameLen == 2 && strncmp(full, name, 2) == 0);
	}
};

// Dispatches Orca-style text commands (`name:value;part;part`) to handlers on a module.
// A template on the module type so a module's commander can be included before the
// module is defined: handler bodies are only instantiated where they are used. A module
// keeps its handlers in a struct of its own and hands `execute` the table of them.
template <typename MODULE>
struct CommandTable {
	using Args = CommandLine::Args;
	using Handler = std::function<void(MODULE&, const Args&)>;

	// `noAlias` is left out (false) by most rows: the command then also answers to its
	// first two characters.
	struct Row {
		const char* name;
		Handler fn;
		bool noAlias;
		Row(const char* name, Handler fn, bool noAlias = false) : name(name), fn(std::move(fn)), noAlias(noAlias) {}
	};

	// `rows` ends with a row whose name is nullptr. Registration order matters for
	// two-letter aliases (first match wins). Returns true if a command was recognised and
	// its handler ran.
	static bool execute(const Row* rows, MODULE& m, const char* text, size_t len) {
		CommandLine line;
		if (!line.parse(text, len)) return false;
		for (const Row* r = rows; r->name; r++) {
			if (line.is(r->name, !r->noAlias)) {
				r->fn(m, line.args);
				return true;
			}
		}
		return false;
	}
};

} // namespace StoermelderPackOne
