// Tests for the script editor's text-buffer helpers. Pure string arithmetic, no UI.

#include "../test/framework.hpp"
#include "ScriptEditorText.hpp"

void testPluginInit(rack::Plugin* p) {
	(void)p;
}

using namespace StoermelderPackOne::ui::editor::scripttext;

TEST_CASE("ScriptEditorText lineStarts and lineCount", "[ScriptEditor]") {
	SECTION("empty buffer is one empty line") {
		REQUIRE(lineCount("") == 1);
		REQUIRE(lineStarts("") == std::vector<int>{0});
	}
	SECTION("no trailing newline") {
		REQUIRE(lineCount("ab\ncd") == 2);
		REQUIRE(lineStarts("ab\ncd") == std::vector<int>{0, 3});
	}
	SECTION("trailing newline adds an empty last line") {
		REQUIRE(lineCount("ab\n") == 2);
		REQUIRE(lineStarts("ab\n") == std::vector<int>{0, 3});
	}
	SECTION("only newlines") {
		REQUIRE(lineCount("\n\n") == 3);
	}
}

TEST_CASE("ScriptEditorText offset <-> line/col", "[ScriptEditor]") {
	SECTION("empty buffer") {
		LineCol lc = offsetToLineCol("", 0);
		REQUIRE(lc.line == 0);
		REQUIRE(lc.col == 0);
		REQUIRE(lineColToOffset("", 0, 0) == 0);
		REQUIRE(lineColToOffset("", 5, 5) == 0);
	}
	SECTION("start of each line and end of buffer") {
		std::string t = "ab\ncde\nf";
		REQUIRE(offsetToLineCol(t, 0).line == 0);
		LineCol l1 = offsetToLineCol(t, 3);
		REQUIRE(l1.line == 1);
		REQUIRE(l1.col == 0);
		LineCol l2 = offsetToLineCol(t, (int)t.size());
		REQUIRE(l2.line == 2);
		REQUIRE(l2.col == 1);
	}
	SECTION("cursor at end of buffer after trailing newline") {
		std::string t = "ab\n";
		LineCol lc = offsetToLineCol(t, (int)t.size());
		REQUIRE(lc.line == 1);
		REQUIRE(lc.col == 0);
		REQUIRE(lineColToOffset(t, 1, 0) == 3);
	}
	SECTION("column is clamped to the line length") {
		std::string t = "ab\ncdef";
		REQUIRE(lineColToOffset(t, 0, 99) == 2);
		REQUIRE(lineColToOffset(t, 1, 99) == 7);
	}
	SECTION("line past the end resolves to end of buffer") {
		REQUIRE(lineColToOffset("ab\ncd", 9, 0) == 5);
	}
	SECTION("out-of-range offsets are clamped") {
		LineCol lc = offsetToLineCol("ab", 99);
		REQUIRE(lc.line == 0);
		REQUIRE(lc.col == 2);
		REQUIRE(offsetToLineCol("ab", -3).col == 0);
	}
	SECTION("round trip for every offset") {
		std::string t = "one\n\ntwo three\nx";
		for (int i = 0; i <= (int)t.size(); i++) {
			LineCol lc = offsetToLineCol(t, i);
			REQUIRE(lineColToOffset(t, lc.line, lc.col) == i);
		}
	}
	SECTION("columns count UTF-8 codepoints") {
		std::string t = "a\xC3\xA4" "b\nxy";    // a ä b
		REQUIRE(offsetToLineCol(t, 3).col == 2);
		REQUIRE(lineColToOffset(t, 0, 2) == 3);
		REQUIRE(lineColToOffset(t, 0, 3) == 4);
		for (int i : {0, 1, 3, 4}) {
			LineCol lc = offsetToLineCol(t, i);
			REQUIRE(lineColToOffset(t, lc.line, lc.col) == i);
		}
	}
}

TEST_CASE("ScriptEditorText CRLF", "[ScriptEditor]") {
	std::string t = "ab\r\ncd\r\n";
	SECTION("the line break is not part of the line") {
		REQUIRE(lineCount(t) == 3);
		REQUIRE(lineContentEnd(t, 0) == 2);
		REQUIRE(lineColToOffset(t, 0, 99) == 2);
	}
	SECTION("cursor between CR and LF reads as end of line") {
		LineCol lc = offsetToLineCol(t, 3);
		REQUIRE(lc.line == 0);
		REQUIRE(lc.col == 2);
	}
	SECTION("next line starts after the LF") {
		REQUIRE(lineColToOffset(t, 1, 0) == 4);
		LineCol lc = offsetToLineCol(t, 4);
		REQUIRE(lc.line == 1);
		REQUIRE(lc.col == 0);
	}
}

TEST_CASE("ScriptEditorText moveVertical", "[ScriptEditor]") {
	std::string t = "abcdef\nxy\nlonger line";

	SECTION("moving up from the first line goes to the buffer start") {
		REQUIRE(moveVertical(t, 3, -1, -1) == 0);
		REQUIRE(moveVertical(t, 0, -1, -1) == 0);
	}
	SECTION("moving down from the last line goes to the buffer end") {
		REQUIRE(moveVertical(t, 14, 1, -1) == (int)t.size());
		REQUIRE(moveVertical(t, (int)t.size(), 1, -1) == (int)t.size());
	}
	SECTION("column is kept when the target line is long enough") {
		REQUIRE(moveVertical(t, 3, 1, -1) == 7 + 2);   // clamped to "xy" end
		REQUIRE(moveVertical(t, 3, 2, -1) == 10 + 3);
	}
	SECTION("sticky preferred column survives a short line") {
		int onShort = moveVertical(t, 5, 1, -1);          // col 5 -> clamped to col 2
		REQUIRE(onShort == 9);
		int onLong = moveVertical(t, onShort, 1, 5);      // preferred col 5 restored
		REQUIRE(onLong == 10 + 5);
	}
	SECTION("empty buffer") {
		REQUIRE(moveVertical("", 0, 1, -1) == 0);
		REQUIRE(moveVertical("", 0, -1, -1) == 0);
	}
	SECTION("moving through a trailing empty line") {
		std::string u = "ab\n";
		REQUIRE(moveVertical(u, 1, 1, -1) == 3);
		REQUIRE(moveVertical(u, 3, -1, -1) == 0);        // col 0 on line 0
	}
}

TEST_CASE("ScriptEditorText indentLines", "[ScriptEditor]") {
	SECTION("single line, no selection") {
		Edit r = indentLines("ab\ncd", 4, 4, 4);
		REQUIRE(r.text == "ab\n    cd");
		REQUIRE(r.cursor == 8);
		REQUIRE(r.selection == 8);
	}
	SECTION("every line of a multi-line selection, empty lines stay empty") {
		Edit r = indentLines("a\n\nb\nc", 1, 5, 2);   // from after 'a' to after 'b'
		REQUIRE(r.text == "  a\n\n  b\nc");
		REQUIRE(r.cursor == 3);
		REQUIRE(r.selection == 9);
	}
	SECTION("a selection ending at column 0 leaves that line alone") {
		Edit r = indentLines("a\nb\nc", 0, 4, 2);
		REQUIRE(r.text == "  a\n  b\nc");
	}
	SECTION("empty buffer") {
		Edit r = indentLines("", 0, 0, 4);
		REQUIRE(r.text == "");
		REQUIRE(r.cursor == 0);
	}
}

TEST_CASE("ScriptEditorText outdentLines", "[ScriptEditor]") {
	SECTION("removes up to the width, no more") {
		REQUIRE(outdentLines("      x", 7, 7, 4).text == "  x");
		REQUIRE(outdentLines("  x", 3, 3, 4).text == "x");
	}
	SECTION("a leading tab counts as one indent") {
		Edit r = outdentLines("\tx", 2, 2, 4);
		REQUIRE(r.text == "x");
		REQUIRE(r.cursor == 1);
	}
	SECTION("nothing to remove leaves everything as it was") {
		Edit r = outdentLines("x\n  y", 0, 0, 4);
		REQUIRE(r.text == "x\n  y");
		REQUIRE(r.cursor == 0);
	}
	SECTION("cursor inside the removed indent moves to the line start") {
		Edit r = outdentLines("a\n    b", 4, 4, 4);   // between two of the spaces
		REQUIRE(r.text == "a\nb");
		REQUIRE(r.cursor == 2);
	}
	SECTION("multi-line selection") {
		Edit r = outdentLines("  a\n  b\n  c", 0, 7, 4);
		REQUIRE(r.text == "a\nb\n  c");
		REQUIRE(r.selection == 3);
	}
}

TEST_CASE("ScriptEditorText leadingWhitespace", "[ScriptEditor]") {
	REQUIRE(leadingWhitespace("", 0) == "");
	REQUIRE(leadingWhitespace("    foo", 7) == "    ");
	REQUIRE(leadingWhitespace("a\n  \tb", 6) == "  \t");
	REQUIRE(leadingWhitespace("a\n    b", 1) == "");        // first line has none
	SECTION("only the part left of the cursor counts") {
		REQUIRE(leadingWhitespace("      x", 2) == "  ");
	}
	SECTION("a blank line of spaces is all indent") {
		REQUIRE(leadingWhitespace("x\n   \ny", 5) == "   ");
	}
}

TEST_CASE("ScriptEditorText looksLikeError", "[ScriptEditor]") {
	REQUIRE(looksLikeError("SyntaxError: unexpected token"));
	REQUIRE(looksLikeError("[   0.0000] error: attempt to call a nil value"));
	REQUIRE(looksLikeError("Uncaught Exception"));
	REQUIRE_FALSE(looksLikeError("note on 60"));
	REQUIRE_FALSE(looksLikeError(""));
}

TEST_CASE("ScriptEditorText findAll", "[ScriptEditor]") {
	SECTION("empty needle or empty text finds nothing") {
		REQUIRE(findAll("abc", "", true).empty());
		REQUIRE(findAll("", "a", true).empty());
	}
	SECTION("case sensitivity") {
		REQUIRE(findAll("Foo foo FOO", "foo", true) == std::vector<int>({4}));
		REQUIRE(findAll("Foo foo FOO", "foo", false) == std::vector<int>({0, 4, 8}));
	}
	SECTION("matches do not overlap") {
		REQUIRE(findAll("aaaa", "aa", true) == std::vector<int>({0, 2}));
	}
	SECTION("across lines, and a needle with a newline") {
		REQUIRE(findAll("ab\nab", "ab", true) == std::vector<int>({0, 3}));
		REQUIRE(findAll("ab\nab", "b\na", true) == std::vector<int>({1}));
	}
	SECTION("non-ASCII bytes are compared as they are") {
		REQUIRE(findAll("x\xC3\xA4y", "\xC3\xA4", false) == std::vector<int>({1}));
		REQUIRE(findAll("\xC3\x84", "\xC3\xA4", false).empty());   // no Unicode folding
	}
}

TEST_CASE("ScriptEditorText match navigation", "[ScriptEditor]") {
	std::vector<int> m = {2, 10, 20};
	REQUIRE(firstMatchAtOrAfter(m, 0) == 0);
	REQUIRE(firstMatchAtOrAfter(m, 2) == 0);
	REQUIRE(firstMatchAtOrAfter(m, 3) == 1);
	REQUIRE(firstMatchAtOrAfter(m, 21) == -1);
	REQUIRE(lastMatchBefore(m, 2) == -1);
	REQUIRE(lastMatchBefore(m, 3) == 0);
	REQUIRE(lastMatchBefore(m, 100) == 2);
	REQUIRE(firstMatchAtOrAfter({}, 0) == -1);
	REQUIRE(lastMatchBefore({}, 5) == -1);
}

TEST_CASE("ScriptEditorText LineFilter", "[ScriptEditor]") {
	LineFilter f;
	SECTION("default and empty pattern match everything") {
		REQUIRE_FALSE(f.active());
		REQUIRE(f.matches("anything"));
		f.set("", false, true);
		REQUIRE(f.matches(""));
	}
	SECTION("plain text: substring, case-insensitive unless asked") {
		f.set("err", false, false);
		REQUIRE(f.active());
		REQUIRE(f.matches("an Error happened"));
		REQUIRE_FALSE(f.matches("all fine"));
		f.set("err", true, false);
		REQUIRE_FALSE(f.matches("an Error happened"));
		REQUIRE(f.matches("an err happened"));
	}
	SECTION("plain text takes regex characters literally") {
		f.set("a.c", false, false);
		REQUIRE(f.matches("xa.cx"));
		REQUIRE_FALSE(f.matches("abc"));
	}
	SECTION("regex: searched anywhere, case-insensitive unless asked") {
		f.set("^note (on|off)", false, true);
		REQUIRE(f.matches("Note ON 60"));
		REQUIRE_FALSE(f.matches("x note on"));
		f.set("^note", true, true);
		REQUIRE_FALSE(f.matches("Note on"));
		f.set("\\d+\\.\\d+", false, true);
		REQUIRE(f.matches("t=12.5"));
		REQUIRE_FALSE(f.matches("t=12"));
	}
	SECTION("an invalid regex is flagged and keeps every line") {
		f.set("(unclosed", false, true);
		REQUIRE_FALSE(f.isValid());
		REQUIRE(f.matches("whatever"));
		f.set("(closed)", false, true);
		REQUIRE(f.isValid());
		REQUIRE_FALSE(f.matches("whatever"));
		f.set("[", false, false);                 // fine as plain text
		REQUIRE(f.isValid());
		REQUIRE(f.matches("a[b"));
	}
	SECTION("an overlong regex is rejected, not compiled") {
		f.set(std::string(300, 'a'), false, true);
		REQUIRE_FALSE(f.isValid());
	}
}

TEST_CASE("ScriptEditorText duplicateLines", "[ScriptEditor]") {
	SECTION("a line in the middle, the cursor follows the copy") {
		Edit r = duplicateLines("a\nb\nc", 2, 2);
		REQUIRE(r.text == "a\nb\nb\nc");
		REQUIRE(r.cursor == 4);
	}
	SECTION("a last line without a line break") {
		Edit r = duplicateLines("a\nb", 2, 2);
		REQUIRE(r.text == "a\nb\nb");
		REQUIRE(r.cursor == 4);
	}
	SECTION("several lines") {
		Edit r = duplicateLines("a\nb\nc", 0, 3);
		REQUIRE(r.text == "a\nb\na\nb\nc");
		REQUIRE(r.cursor == 4);
		REQUIRE(r.selection == 7);
	}
	SECTION("empty buffer and an empty last line") {
		REQUIRE(duplicateLines("", 0, 0).text == "\n");
		REQUIRE(duplicateLines("a\n", 2, 2).text == "a\n\n");
	}
	SECTION("a CRLF file keeps its line breaks") {
		REQUIRE(duplicateLines("a\r\nb\r\n", 0, 0).text == "a\r\na\r\nb\r\n");
	}
}

TEST_CASE("ScriptEditorText moveLines", "[ScriptEditor]") {
	SECTION("up and down swap with the neighbour, the cursor stays on its line") {
		Edit up = moveLines("a\nb\nc", 2, 2, -1);
		REQUIRE(up.text == "b\na\nc");
		REQUIRE(up.cursor == 0);
		Edit down = moveLines("a\nb\nc", 1, 1, 1);
		REQUIRE(down.text == "b\na\nc");
		REQUIRE(down.cursor == 3);
	}
	SECTION("the column is kept") {
		Edit r = moveLines("abc\ndef", 5, 5, -1);   // between d and e
		REQUIRE(r.text == "def\nabc");
		REQUIRE(r.cursor == 1);
	}
	SECTION("nothing moves past the ends") {
		Edit up = moveLines("a\nb", 0, 0, -1);
		REQUIRE(up.text == "a\nb");
		REQUIRE(up.cursor == 0);
		Edit down = moveLines("a\nb", 3, 3, 1);
		REQUIRE(down.text == "a\nb");
		REQUIRE(down.cursor == 3);
	}
	SECTION("a block of lines moves as one, with its selection") {
		Edit r = moveLines("a\nb\nc\nd", 0, 3, 1);   // a and b
		REQUIRE(r.text == "c\na\nb\nd");
		REQUIRE(r.cursor == 2);
		REQUIRE(r.selection == 5);
		Edit back = moveLines(r.text, r.cursor, r.selection, -1);
		REQUIRE(back.text == "a\nb\nc\nd");
		REQUIRE(back.cursor == 0);
		REQUIRE(back.selection == 3);
	}
	SECTION("a selection ending at column 0 below the block does not take that line") {
		Edit r = moveLines("a\nb\nc", 0, 4, 1);      // a, b and the line break after b
		REQUIRE(r.text == "c\na\nb");
		REQUIRE(r.cursor == 2);
		REQUIRE(r.selection == 5);
		Edit up = moveLines("a\nb\nc", 2, 4, -1);    // b and its line break, ending at c
		REQUIRE(up.text == "b\na\nc");
		REQUIRE(up.selection == 4);                  // still at the start of c
	}
	SECTION("a trailing line break stays put") {
		REQUIRE(moveLines("a\nb\n", 0, 0, 1).text == "b\na\n");
	}
	SECTION("CRLF lines move whole") {
		REQUIRE(moveLines("a\r\nb\r\nc", 0, 0, 1).text == "b\r\na\r\nc");
	}
}

TEST_CASE("ScriptEditorText deleteLines", "[ScriptEditor]") {
	SECTION("a middle line") {
		Edit r = deleteLines("a\nb\nc", 2, 2);
		REQUIRE(r.text == "a\nc");
		REQUIRE(r.cursor == 2);
	}
	SECTION("the first line") {
		Edit r = deleteLines("a\nb", 0, 0);
		REQUIRE(r.text == "b");
		REQUIRE(r.cursor == 0);
	}
	SECTION("the last line takes the break before it") {
		Edit r = deleteLines("a\nb", 3, 3);
		REQUIRE(r.text == "a");
		REQUIRE(r.cursor == 1);
	}
	SECTION("the only line, and the empty last line") {
		REQUIRE(deleteLines("a", 0, 0).text == "");
		REQUIRE(deleteLines("", 0, 0).text == "");
		REQUIRE(deleteLines("a\n", 2, 2).text == "a");
	}
	SECTION("several lines") {
		REQUIRE(deleteLines("a\nb\nc", 0, 3).text == "c");
		REQUIRE(deleteLines("a\nb\nc", 2, 5).text == "a");
	}
}

TEST_CASE("ScriptEditorText toggleComment", "[ScriptEditor]") {
	SECTION("comment, then uncomment, restores the text") {
		Edit on = toggleComment("x\ny", 0, 3, "--");
		REQUIRE(on.text == "-- x\n-- y");
		Edit off = toggleComment(on.text, on.cursor, on.selection, "--");
		REQUIRE(off.text == "x\ny");
		REQUIRE(off.cursor == 0);
		REQUIRE(off.selection == 3);
	}
	SECTION("the marker goes at the block's smallest indentation") {
		Edit on = toggleComment("  x\n    y", 0, 8, "--");
		REQUIRE(on.text == "  -- x\n  --   y");
		REQUIRE(toggleComment(on.text, 0, (int)on.text.size(), "--").text == "  x\n    y");
	}
	SECTION("a partly commented block gets commented all the way") {
		Edit r = toggleComment("-- a\nb", 0, 6, "--");
		REQUIRE(r.text == "-- -- a\n-- b");
	}
	SECTION("blank lines are skipped") {
		REQUIRE(toggleComment("a\n\nb", 0, 4, "//").text == "// a\n\n// b");
		REQUIRE(toggleComment("// a\n\n// b", 0, 9, "//").text == "a\n\nb");
	}
	SECTION("an empty line can be commented on its own") {
		REQUIRE(toggleComment("", 0, 0, "--").text == "-- ");
	}
	SECTION("the marker without a space is removed alone") {
		REQUIRE(toggleComment("//x", 0, 0, "//").text == "x");
	}
	SECTION("the cursor follows the text") {
		Edit on = toggleComment("abc", 2, 2, "--");
		REQUIRE(on.text == "-- abc");
		REQUIRE(on.cursor == 5);
		Edit off = toggleComment(on.text, 1, 1, "--");   // inside the marker
		REQUIRE(off.text == "abc");
		REQUIRE(off.cursor == 0);
	}
	SECTION("only the lines the selection touches") {
		REQUIRE(toggleComment("a\nb\nc", 0, 3, "--").text == "-- a\n-- b\nc");
		REQUIRE(toggleComment("a\nb\nc", 0, 2, "--").text == "-- a\nb\nc");   // ends at column 0 of b
		REQUIRE(toggleComment("a\nb\nc", 0, 4, "--").text == "-- a\n-- b\nc");   // ends at column 0 of c
	}
}

TEST_CASE("ScriptEditorText wordRangeAt", "[ScriptEditor]") {
	auto word = [](const std::string& t, int off) {
		Range r = wordRangeAt(t, off);
		return t.substr(r.begin, r.end - r.begin);
	};
	SECTION("a word, from anywhere inside it or at its edges") {
		REQUIRE(word("foo bar", 0) == "foo");
		REQUIRE(word("foo bar", 1) == "foo");
		REQUIRE(word("foo bar", 3) == "foo");     // right after the word
		REQUIRE(word("foo bar", 4) == "bar");
		REQUIRE(word("foo bar", 7) == "bar");     // at the end of the buffer
	}
	SECTION("identifier characters: digits and underscore belong to the word") {
		REQUIRE(word("a_b9 x", 2) == "a_b9");
		REQUIRE(word("x = 12.5", 5) == "12");
	}
	SECTION("blanks and punctuation select their own run") {
		REQUIRE(word("a   b", 2) == "   ");
		REQUIRE(word("a == b", 3) == "==");
		REQUIRE(word("a ; b", 2) == ";");
		REQUIRE(word("f()", 2) == "()");
	}
	SECTION("a word before punctuation wins at the boundary") {
		REQUIRE(word("foo(bar)", 3) == "foo");
		REQUIRE(word("(foo)", 4) == "foo");
	}
	SECTION("never leaves the line") {
		REQUIRE(word("ab\ncd", 2) == "ab");
		REQUIRE(word("ab\ncd", 3) == "cd");
		REQUIRE(word("ab\r\ncd", 2) == "ab");   // the CR belongs to the line break
	}
	SECTION("an empty line or buffer gives an empty range") {
		Range a = wordRangeAt("", 0);
		REQUIRE(a.begin == a.end);
		Range b = wordRangeAt("a\n\nb", 2);
		REQUIRE(b.begin == 2);
		REQUIRE(b.end == 2);
	}
	SECTION("a multi-byte letter is not split") {
		REQUIRE(word("x \xC3\xA4\xC3\xB6 y", 3) == "\xC3\xA4\xC3\xB6");
	}
}

TEST_CASE("ScriptEditorText lineRangeAt", "[ScriptEditor]") {
	Range a = lineRangeAt("one\ntwo\nthree", 5);
	REQUIRE(a.begin == 4);
	REQUIRE(a.end == 8);                  // "two\n"
	Range b = lineRangeAt("one\ntwo\nthree", 12);
	REQUIRE(b.begin == 8);
	REQUIRE(b.end == 13);                 // last line, no break
	Range c = lineRangeAt("", 0);
	REQUIRE(c.begin == 0);
	REQUIRE(c.end == 0);
	Range d = lineRangeAt("a\n", 2);      // the empty last line
	REQUIRE(d.begin == 2);
	REQUIRE(d.end == 2);
}

TEST_CASE("ScriptEditorText apiSnippet", "[ScriptEditor]") {
	ApiFunction noteOn("setNoteOn", "Setters", {{"msg", "message handle"}, {"ch", "channel 1-16"}});
	ApiFunction noteOff("setNoteOff", "Setters", {{"msg", "message handle"}, {"vel", "release velocity", true}});
	ApiFunction none("random", "", {});
	ApiFunction onlyOptional("enableTipsyIn", "", {{"enabled", "false releases", true}});

	SECTION("comment line above the call, describing every parameter") {
		std::string s = apiSnippet("midi", noteOn, ScriptSyntax("--", ""));
		REQUIRE(s == "-- msg: message handle; ch: channel 1-16\nmidi.setNoteOn(msg, ch)");
	}
	SECTION("a // language also gets the semicolon") {
		std::string s = apiSnippet("midi", noteOn, ScriptSyntax("//", ";"));
		REQUIRE(s == "// msg: message handle; ch: channel 1-16\nmidi.setNoteOn(msg, ch);");
	}
	SECTION("the call line takes the indentation") {
		std::string s = apiSnippet("midi", noteOn, ScriptSyntax("--", ""), "    ");
		REQUIRE(s == "-- msg: message handle; ch: channel 1-16\n    midi.setNoteOn(msg, ch)");
	}
	SECTION("optional parameters are described but not in the call") {
		std::string s = apiSnippet("midi", noteOff, ScriptSyntax("--", ""));
		REQUIRE(s == "-- msg: message handle; [vel]: release velocity (optional)\nmidi.setNoteOff(msg)");
	}
	SECTION("no parameters: no comment") {
		std::string s = apiSnippet("rack", none, ScriptSyntax("//", ";"));
		REQUIRE(s == "rack.random();");
	}
	SECTION("only optional parameters: empty call") {
		std::string s = apiSnippet("trig", onlyOptional, ScriptSyntax("--", ""));
		REQUIRE(s == "-- [enabled]: false releases (optional)\ntrig.enableTipsyIn()");
	}
}
