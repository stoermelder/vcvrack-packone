#pragma once
#include <string>
#include <vector>
#include <algorithm>
#include <cctype>
#include <regex>

// Text-buffer arithmetic for the script editor. Free functions on a plain
// std::string, no Rack types, so the off-by-one-prone parts are testable
// without any UI.
// Offsets are byte offsets into the buffer (the same unit as ui::TextField's
// cursor/selection). Columns count UTF-8 codepoints, so a multi-byte character
// is one column wide on the monospace display. A line ends at '\n'; a '\r'
// directly before it belongs to the line break, not to the line.

namespace StoermelderPackOne {
namespace ui {
namespace editor {
namespace scripttext {

struct LineCol {
	int line = 0;
	int col = 0;
};

inline bool isContinuationByte(char c) {
	return (static_cast<unsigned char>(c) & 0xC0) == 0x80;
}

// Offsets of every line start. Always holds at least one entry (0), and a
// trailing '\n' contributes an empty last line.
inline std::vector<int> lineStarts(const std::string& text) {
	std::vector<int> starts;
	starts.push_back(0);
	for (size_t i = 0; i < text.size(); i++) {
		if (text[i] == '\n') starts.push_back((int)i + 1);
	}
	return starts;
}

inline int lineCount(const std::string& text) {
	int n = 1;
	for (char c : text) {
		if (c == '\n') n++;
	}
	return n;
}

// Offset one past the last character of the line starting at `start`,
// excluding the line break ("\n" or "\r\n").
inline int lineContentEnd(const std::string& text, int start) {
	size_t nl = text.find('\n', start);
	int end = nl == std::string::npos ? (int)text.size() : (int)nl;
	if (end > start && text[end - 1] == '\r') end--;
	return end;
}

// Number of codepoints between two offsets.
inline int countCodepoints(const std::string& text, int begin, int end) {
	int n = 0;
	for (int i = begin; i < end; i++) {
		if (!isContinuationByte(text[i])) n++;
	}
	return n;
}

inline int clampOffset(const std::string& text, int offset) {
	return std::max(0, std::min(offset, (int)text.size()));
}

inline LineCol offsetToLineCol(const std::string& text, int offset) {
	offset = clampOffset(text, offset);
	LineCol lc;
	int lineStart = 0;
	for (int i = 0; i < offset; i++) {
		if (text[i] == '\n') {
			lc.line++;
			lineStart = i + 1;
		}
	}
	// A cursor between '\r' and '\n' sits at the end of its line's content.
	int end = std::min(offset, lineContentEnd(text, lineStart));
	lc.col = countCodepoints(text, lineStart, end);
	return lc;
}

// Offset of (line, col). A line past the end resolves to the end of the
// buffer, a column past the end of the line to the end of that line.
inline int lineColToOffset(const std::string& text, int line, int col) {
	if (line < 0) line = 0;
	if (col < 0) col = 0;
	int start = 0;
	for (int l = 0; l < line; l++) {
		size_t nl = text.find('\n', start);
		if (nl == std::string::npos) return (int)text.size();
		start = (int)nl + 1;
	}
	int end = lineContentEnd(text, start);
	int pos = start;
	for (int c = 0; c < col && pos < end; c++) {
		pos++;
		while (pos < end && isContinuationByte(text[pos])) pos++;
	}
	return pos;
}

// Up/Down. `preferredCol` is the sticky column remembered across short lines;
// pass a negative value to use the current column. Moving above the first line
// goes to the start of the buffer, below the last line to its end, as in
// common editors.
inline int moveVertical(const std::string& text, int offset, int delta, int preferredCol) {
	offset = clampOffset(text, offset);
	LineCol lc = offsetToLineCol(text, offset);
	int col = preferredCol >= 0 ? preferredCol : lc.col;
	int target = lc.line + delta;
	if (target < 0) return 0;
	if (target >= lineCount(text)) return (int)text.size();
	return lineColToOffset(text, target, col);
}

struct Edit {
	std::string text;
	int cursor = 0;
	int selection = 0;
};

// First and last line touched by a selection. A selection that ends at column 0
// of a line does not touch that line (unless the selection is empty).
inline void selectedLineRange(const std::string& text, int cursor, int selection, int& firstLine, int& lastLine) {
	int b = std::min(cursor, selection);
	int e = std::max(cursor, selection);
	firstLine = offsetToLineCol(text, b).line;
	LineCol end = offsetToLineCol(text, e);
	lastLine = end.line;
	if (e > b && end.col == 0 && lastLine > firstLine) lastLine--;
}

// Adds `width` spaces at the start of every line the selection touches. Empty lines
// stay empty. Cursor and selection follow the text.
inline Edit indentLines(const std::string& text, int cursor, int selection, int width) {
	int first, last;
	selectedLineRange(text, cursor, selection, first, last);
	std::vector<int> starts = lineStarts(text);
	Edit r;
	int shiftCursor = 0, shiftSel = 0;
	int prev = 0;
	for (int l = first; l <= last; l++) {
		int ls = starts[l];
		if (lineContentEnd(text, ls) <= ls) continue;
		r.text.append(text, prev, ls - prev);
		r.text.append(width, ' ');
		prev = ls;
		if (cursor >= ls) shiftCursor += width;
		if (selection >= ls) shiftSel += width;
	}
	r.text.append(text, prev, std::string::npos);
	r.cursor = cursor + shiftCursor;
	r.selection = selection + shiftSel;
	return r;
}

// Removes up to `width` leading spaces, or one leading tab, from every line the
// selection touches. Cursor and selection follow the text.
inline Edit outdentLines(const std::string& text, int cursor, int selection, int width) {
	int first, last;
	selectedLineRange(text, cursor, selection, first, last);
	std::vector<int> starts = lineStarts(text);
	Edit r;
	int shiftCursor = 0, shiftSel = 0;
	int prev = 0;
	for (int l = first; l <= last; l++) {
		int ls = starts[l];
		int n = 0;
		if (ls < (int)text.size() && text[ls] == '\t') {
			n = 1;
		}
		else {
			while (n < width && ls + n < (int)text.size() && text[ls + n] == ' ') n++;
		}
		if (n == 0) continue;
		r.text.append(text, prev, ls - prev);
		prev = ls + n;
		shiftCursor += std::min(n, std::max(0, cursor - ls));
		shiftSel += std::min(n, std::max(0, selection - ls));
	}
	r.text.append(text, prev, std::string::npos);
	r.cursor = cursor - shiftCursor;
	r.selection = selection - shiftSel;
	return r;
}

// The indentation of the line containing `offset`, for auto-indent on Enter. Only the
// part left of `offset` counts, so splitting a line inside its indent does not
// indent the new line deeper than the old one's start.
inline std::string leadingWhitespace(const std::string& text, int offset) {
	offset = clampOffset(text, offset);
	int ls = lineColToOffset(text, offsetToLineCol(text, offset).line, 0);
	int end = std::min(offset, lineContentEnd(text, ls));
	int i = ls;
	while (i < end && (text[i] == ' ' || text[i] == '\t')) i++;
	return text.substr(ls, i - ls);
}

// Whether a script log line reports a failure, for tinting it. A heuristic on the
// text: the engines' error messages differ, and a script may log "error" itself.
inline bool looksLikeError(const std::string& line) {
	std::string lower;
	lower.reserve(line.size());
	for (char c : line) lower.push_back((char)std::tolower((unsigned char)c));
	return lower.find("error") != std::string::npos || lower.find("exception") != std::string::npos;
}

// ── Find ──

inline std::string asciiLower(const std::string& s) {
	std::string r = s;
	for (char& c : r) {
		if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');
	}
	return r;
}

// Start offsets of every non-overlapping occurrence of `needle`, in order. Case folding
// is ASCII only, so byte offsets are the same in the folded copy. Empty needle: none.
inline std::vector<int> findAll(const std::string& text, const std::string& needle, bool matchCase) {
	std::vector<int> found;
	if (needle.empty()) return found;
	const std::string& hay = matchCase ? text : asciiLower(text);
	const std::string pat = matchCase ? needle : asciiLower(needle);
	size_t pos = 0;
	while ((pos = hay.find(pat, pos)) != std::string::npos) {
		found.push_back((int)pos);
		pos += pat.size();
	}
	return found;
}

// Index of the first match starting at or after `pos`, or -1.
inline int firstMatchAtOrAfter(const std::vector<int>& matches, int pos) {
	auto it = std::lower_bound(matches.begin(), matches.end(), pos);
	return it == matches.end() ? -1 : (int)(it - matches.begin());
}

// Index of the last match starting before `pos`, or -1.
inline int lastMatchBefore(const std::vector<int>& matches, int pos) {
	auto it = std::lower_bound(matches.begin(), matches.end(), pos);
	return it == matches.begin() ? -1 : (int)(it - matches.begin()) - 1;
}

// ── Line filter ──

// Keeps the lines of a log that match a pattern: plain text, or an ECMAScript regular
// expression searched anywhere in the line. An empty pattern matches everything. A
// pattern that does not compile is "invalid" and, so a typo does not blank the log,
// also matches everything.
class LineFilter {
public:
	static constexpr size_t kMaxPatternLength = 256;

	void set(const std::string& pattern, bool matchCase, bool useRegex) {
		this->pattern = pattern;
		this->matchCase = matchCase;
		this->useRegex = useRegex;
		invalid = false;
		needle = matchCase ? pattern : asciiLower(pattern);
		if (useRegex && !pattern.empty()) {
			try {
				if (pattern.size() > kMaxPatternLength) throw std::regex_error(std::regex_constants::error_complexity);
				std::regex::flag_type flags = std::regex::ECMAScript | std::regex::nosubs;
				if (!matchCase) flags |= std::regex::icase;
				re = std::regex(pattern, flags);
			}
			catch (const std::regex_error&) {
				invalid = true;
			}
		}
	}

	bool active() const {
		return !pattern.empty();
	}

	// Only meaningful for regex mode; text patterns always compile.
	bool isValid() const {
		return !invalid;
	}

	bool matches(const std::string& line) const {
		if (pattern.empty() || invalid) return true;
		if (useRegex) return std::regex_search(line, re);
		return (matchCase ? line : asciiLower(line)).find(needle) != std::string::npos;
	}

private:
	std::string pattern;
	std::string needle;
	bool matchCase = false;
	bool useRegex = false;
	bool invalid = false;
	std::regex re;
};

// ── Line operations ──

inline std::vector<std::string> splitLines(const std::string& text) {
	std::vector<std::string> lines;
	size_t start = 0;
	while (true) {
		size_t nl = text.find('\n', start);
		lines.push_back(text.substr(start, nl == std::string::npos ? std::string::npos : nl - start));
		if (nl == std::string::npos) break;
		start = nl + 1;
	}
	return lines;
}

inline std::string joinLines(const std::vector<std::string>& lines) {
	std::string r;
	for (size_t i = 0; i < lines.size(); i++) {
		if (i > 0) r += '\n';
		r += lines[i];
	}
	return r;
}

// Duplicates the lines the selection touches, below them. Cursor and selection move
// to the copy, so repeating the command keeps stacking copies downwards.
inline Edit duplicateLines(const std::string& text, int cursor, int selection) {
	int first, last;
	selectedLineRange(text, cursor, selection, first, last);
	std::vector<int> starts = lineStarts(text);
	int blockStart = starts[first];
	int blockEnd = last + 1 < (int)starts.size() ? starts[last + 1] : (int)text.size();
	std::string block = text.substr(blockStart, blockEnd - blockStart);
	// A last line without a line break needs one before its copy.
	std::string insert = (blockEnd == (int)text.size() && (block.empty() || block.back() != '\n')) ? "\n" + block : block;
	Edit r;
	r.text = text.substr(0, blockEnd) + insert + text.substr(blockEnd);
	r.cursor = cursor + (int)insert.size();
	r.selection = selection + (int)insert.size();
	return r;
}

// Moves the lines the selection touches up (delta -1) or down (+1) past their neighbour.
// At the first / last line it changes nothing. The selection moves with the lines.
inline Edit moveLines(const std::string& text, int cursor, int selection, int delta) {
	int first, last;
	selectedLineRange(text, cursor, selection, first, last);
	std::vector<std::string> lines = splitLines(text);
	int n = (int)lines.size();
	Edit unchanged;
	unchanged.text = text;
	unchanged.cursor = cursor;
	unchanged.selection = selection;
	if ((delta < 0 && first == 0) || (delta > 0 && last == n - 1)) return unchanged;

	std::vector<int> oldStarts = lineStarts(text);
	std::vector<std::string> moved = lines;
	if (delta < 0) {
		std::string displaced = moved[first - 1];
		moved.erase(moved.begin() + (first - 1));
		moved.insert(moved.begin() + last, displaced);
	}
	else {
		std::string displaced = moved[last + 1];
		moved.erase(moved.begin() + (last + 1));
		moved.insert(moved.begin() + first, displaced);
	}
	Edit r;
	r.text = joinLines(moved);
	std::vector<int> newStarts = lineStarts(r.text);

	int b = std::min(cursor, selection);
	int e = std::max(cursor, selection);
	auto mapOffset = [&](int off) {
		LineCol lc = offsetToLineCol(text, off);
		int line = lc.line;
		int inLine = off - oldStarts[line];
		int newLine;
		// A selection ending at column 0 below the block does not include that line.
		if (e > b && off == e && line == last + 1 && inLine == 0) newLine = last + 1 + (delta > 0 ? 1 : 0);
		else if (line >= first && line <= last) newLine = line + delta;
		else if (delta < 0 && line == first - 1) newLine = last;
		else if (delta > 0 && line == last + 1) newLine = first;
		else newLine = line;
		if (newLine >= (int)newStarts.size()) return (int)r.text.size();
		return newStarts[newLine] + inLine;
	};
	r.cursor = mapOffset(cursor);
	r.selection = mapOffset(selection);
	return r;
}

// Deletes the lines the selection touches, line break included. Deleting the last line
// takes the break before it instead, so no empty line is left behind.
inline Edit deleteLines(const std::string& text, int cursor, int selection) {
	int first, last;
	selectedLineRange(text, cursor, selection, first, last);
	std::vector<int> starts = lineStarts(text);
	int blockStart = starts[first];
	int blockEnd = last + 1 < (int)starts.size() ? starts[last + 1] : (int)text.size();
	if (blockEnd == (int)text.size() && first > 0) blockStart -= 1;
	Edit r;
	r.text = text.substr(0, blockStart) + text.substr(blockEnd);
	r.cursor = r.selection = blockStart;
	return r;
}

// ── Comments ──

// The line comment of the script's language, from its own header: "@engine QuickJs..."
// is JavaScript (`//`), anything else Lua (`--`). Reads the buffer, not the running
// engine, so it stays right while the header is being edited.
inline std::string commentPrefix(const std::string& text) {
	size_t at = text.find("@engine");
	if (at != std::string::npos) {
		size_t i = at + 7;
		while (i < text.size() && (text[i] == ' ' || text[i] == '\t')) i++;
		if (text.compare(i, 7, "QuickJs") == 0) return "//";
	}
	return "--";
}

struct TextEdit {
	int pos;
	int removeLen;
	std::string insert;
};

// Applies edits (ascending, not overlapping) and carries the cursor and selection along:
// an offset at or after an edit shifts by it, one inside removed text collapses to its start.
inline Edit applyTextEdits(const std::string& text, const std::vector<TextEdit>& edits, int cursor, int selection) {
	Edit r;
	int prev = 0;
	for (const TextEdit& e : edits) {
		r.text.append(text, prev, e.pos - prev);
		r.text += e.insert;
		prev = e.pos + e.removeLen;
	}
	r.text.append(text, prev, std::string::npos);
	auto map = [&](int off) {
		int delta = 0;
		for (const TextEdit& e : edits) {
			int end = e.pos + e.removeLen;
			if (off >= end) delta += (int)e.insert.size() - e.removeLen;
			else if (off > e.pos) return e.pos + delta;
			else break;
		}
		return off + delta;
	};
	r.cursor = map(cursor);
	r.selection = map(selection);
	return r;
}

inline bool isBlankLine(const std::string& text, int ls) {
	int end = lineContentEnd(text, ls);
	for (int i = ls; i < end; i++) {
		if (text[i] != ' ' && text[i] != '\t') return false;
	}
	return true;
}

// Comments the lines the selection touches, or uncomments them if every non-blank one is
// already commented. The marker goes at the block's smallest indentation, as editors do.
inline Edit toggleComment(const std::string& text, int cursor, int selection, const std::string& prefix) {
	int first, last;
	selectedLineRange(text, cursor, selection, first, last);
	std::vector<int> starts = lineStarts(text);

	std::vector<int> targets;
	for (int l = first; l <= last; l++) {
		if (!isBlankLine(text, starts[l])) targets.push_back(l);
	}
	if (targets.empty()) {
		for (int l = first; l <= last; l++) targets.push_back(l);
	}

	auto indentOf = [&](int l) {
		int ls = starts[l];
		int end = lineContentEnd(text, ls);
		int i = ls;
		while (i < end && (text[i] == ' ' || text[i] == '\t')) i++;
		return i - ls;
	};
	auto commented = [&](int l) {
		return text.compare(starts[l] + indentOf(l), prefix.size(), prefix) == 0;
	};

	bool allCommented = true;
	for (int l : targets) {
		if (isBlankLine(text, starts[l]) || !commented(l)) allCommented = false;
	}

	std::vector<TextEdit> edits;
	if (allCommented) {
		for (int l : targets) {
			int pos = starts[l] + indentOf(l);
			int len = (int)prefix.size();
			if (pos + len < (int)text.size() && text[pos + len] == ' ') len++;
			edits.push_back({pos, len, ""});
		}
	}
	else {
		int minIndent = indentOf(targets[0]);
		for (int l : targets) minIndent = std::min(minIndent, indentOf(l));
		for (int l : targets) edits.push_back({starts[l] + minIndent, 0, prefix + " "});
	}
	return applyTextEdits(text, edits, cursor, selection);
}

// ── Word and line ranges (double / triple click) ──

struct Range {
	int begin = 0;
	int end = 0;
};

// Character classes for word selection: identifier characters (non-ASCII bytes count as
// those, so a UTF-8 letter is not split), blanks, everything else.
enum class CharClass { WORD, SPACE, OTHER };

inline CharClass classOf(char c) {
	unsigned char u = (unsigned char)c;
	if (u >= 0x80 || u == '_' || std::isalnum(u)) return CharClass::WORD;
	if (c == ' ' || c == '\t') return CharClass::SPACE;
	return CharClass::OTHER;
}

// The run of same-class characters at `offset`, within its line: a word, a stretch of
// blanks, or a stretch of punctuation. Between a word and a non-word character the word
// wins, so a click at the end of "foo" selects "foo". An empty line gives an empty range.
inline Range wordRangeAt(const std::string& text, int offset) {
	offset = clampOffset(text, offset);
	int ls = lineColToOffset(text, offsetToLineCol(text, offset).line, 0);
	int le = lineContentEnd(text, ls);
	Range r;
	r.begin = r.end = offset;
	if (le <= ls) return r;

	int i = std::min(offset, le - 1);
	if (offset > ls && offset <= le && classOf(text[i]) != CharClass::WORD && classOf(text[offset - 1]) == CharClass::WORD) i = offset - 1;
	CharClass c = classOf(text[i]);
	int b = i, e = i + 1;
	while (b > ls && classOf(text[b - 1]) == c) b--;
	while (e < le && classOf(text[e]) == c) e++;
	r.begin = b;
	r.end = e;
	return r;
}

// The whole line at `offset`, line break included (the last line has none).
inline Range lineRangeAt(const std::string& text, int offset) {
	offset = clampOffset(text, offset);
	int line = offsetToLineCol(text, offset).line;
	std::vector<int> starts = lineStarts(text);
	Range r;
	r.begin = starts[line];
	r.end = line + 1 < (int)starts.size() ? starts[line + 1] : (int)text.size();
	return r;
}

} // namespace scripttext
} // namespace editor
} // namespace ui
} // namespace StoermelderPackOne
