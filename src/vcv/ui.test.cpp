#include "../test/test_plugin.hpp"
#include "ui.hpp"

using namespace StoermelderPackOne::vcv;

TEST_CASE("measureTextBox estimate wraps at word boundaries", "[ui][measureTextBox]") {
	UiAccess ui;

	// A width that fits "one two" but not "one two three" forces a wrap after "two".
	std::string oneWord = "one";
	math::Vec oneWordSize = ui.measureTextBox(oneWord, 10.f, 0.f);

	std::string threeWords = "one two three";
	// Unwrapped (width = 0): a single line, wider than any individual word.
	math::Vec unwrapped = ui.measureTextBox(threeWords, 10.f, 0.f);
	CATCH_INFO("unwrapped=" << unwrapped.x << "x" << unwrapped.y);
	REQUIRE(unwrapped.x > oneWordSize.x);

	// Wrapped to fit only ~2 words per line: taller (more lines), narrower than unwrapped.
	float twoWordsWidth = ui.measureTextBox("one two", 10.f, 0.f).x;
	math::Vec wrapped = ui.measureTextBox(threeWords, 10.f, twoWordsWidth + 1.f);
	CATCH_INFO("wrapped=" << wrapped.x << "x" << wrapped.y);
	REQUIRE(wrapped.y > unwrapped.y);
	REQUIRE(wrapped.x <= unwrapped.x);
	REQUIRE(wrapped.x <= twoWordsWidth + 1.f);
}

TEST_CASE("measureTextBox estimate never breaks a single word wider than the wrap width", "[ui][measureTextBox]") {
	UiAccess ui;
	// A width narrower than "supercalifragilistic" itself must not truncate the word or crash;
	// the line simply overflows the requested width.
	std::string longWord = "supercalifragilistic";
	math::Vec size = ui.measureTextBox(longWord, 10.f, 5.f);
	float fullWordWidth = ui.measureTextBox(longWord, 10.f, 0.f).x;
	REQUIRE(math::isNear(size.x, fullWordWidth, 1e-3f));
	// Still one line: the word is alone on its line, nothing to wrap it against.
	float oneLineHeight = ui.measureTextBox("x", 10.f, 0.f).y;
	REQUIRE(math::isNear(size.y, oneLineHeight, 1e-3f));
}

TEST_CASE("measureTextBox estimate grows in height with more text", "[ui][measureTextBox]") {
	UiAccess ui;
	std::string short_ = "Short line.";
	std::string longer = "This is a considerably longer piece of body text that should wrap "
		"across several lines once a narrow width is applied, growing the box height.";

	float width = 100.f;
	math::Vec shortSize = ui.measureTextBox(short_, 10.f, width);
	math::Vec longSize = ui.measureTextBox(longer, 10.f, width);

	CATCH_INFO("short=" << shortSize.x << "x" << shortSize.y << " long=" << longSize.x << "x" << longSize.y);
	REQUIRE(longSize.y > shortSize.y);
}

TEST_CASE("measureTextBox estimate: explicit newlines each start a new line", "[ui][measureTextBox]") {
	UiAccess ui;
	float oneLineHeight = ui.measureTextBox("a", 10.f, 0.f).y;
	math::Vec threeLines = ui.measureTextBox("a\nb\nc", 10.f, 0.f);
	REQUIRE(math::isNear(threeLines.y, 3.f * oneLineHeight, 1e-3f));
}

TEST_CASE("measureTextBox estimate scales with font size", "[ui][measureTextBox]") {
	UiAccess ui;
	math::Vec small = ui.measureTextBox("Hello world", 10.f, 0.f);
	math::Vec large = ui.measureTextBox("Hello world", 20.f, 0.f);
	REQUIRE(large.x > small.x);
	REQUIRE(large.y > small.y);
}

TEST_CASE("measureTextBox estimate on empty text is a single empty line", "[ui][measureTextBox]") {
	UiAccess ui;
	float oneLineHeight = ui.measureTextBox("a", 10.f, 0.f).y;
	math::Vec empty = ui.measureTextBox("", 10.f, 0.f);
	REQUIRE(empty.x == 0.f);
	REQUIRE(math::isNear(empty.y, oneLineHeight, 1e-3f));
}
