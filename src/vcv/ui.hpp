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

// ---- Swappable UI layer ----
// Every user-interaction operation in this namespace routes through UiAccess, so the whole
// layer is replaceable: production uses RealUiAccess (defined in vcv_ui.cpp, backed by
// osdialog/GLFW); the unit-test harness installs a mock via `uiAccess` to script answers
// and assert on what was asked, without a live GUI.
//
// RealUiAccess deliberately lives in vcv_ui.cpp, not in this header: its methods are the
// only thing that needs <osdialog.h>, so including vcv_ui.hpp never drags osdialog into
// the header graph (vcv_files.hpp / Strip.hpp currently spread it everywhere).
//
// `message` returns bool (OK/Yes = true), not an int: the only two shapes actually used are
// "tell the user" and "ask yes/no". `openDialog`/`saveDialog`/`openDirectoryDialog` return
// "" on cancellation, and `getClipboard` returns "" when the clipboard holds no text.
// `filters` is a plain osdialog filter string, parsed/freed inside RealUiAccess so callers
// never own an osdialog_filters*.
// Must not be called from the engine thread (the UI API is GUI-thread only).
enum class MessageType { INFO, WARNING, ERROR };
enum class MessageButtons { OK, YES_NO };


// The rack viewport: RackScrollWidget's box, in scene coordinates, and its current zoom.
// Used by anything that needs to know what part of the rack is on screen
// without touching APP->scene->rackScroll directly.
//
// That indirection matters under test: Test::Harness's SceneLayout deliberately keeps
// rackScroll zeroed-out AND hidden, because a live RackScrollWidget segfaults on
// APP->window the instant a HoverScrollEvent reaches its onHoverScroll() — a dispatch
// hazard, not just a "box happens to be empty" one. Routing the read through this seam lets
// a test hand placement code a real, sane viewport without ever giving dispatch a path to
// the actual RackScrollWidget.
struct RackViewport {
	math::Rect box;
	float zoom = 1.f;

	RackViewport() {}
	RackViewport(math::Rect box, float zoom) : box(box), zoom(zoom) {}
};


struct UiAccess {
	virtual ~UiAccess() {}

	// Returns true for OK/Yes. Mock returns a scripted answer.
	virtual bool message(MessageType type, MessageButtons buttons, const std::string& msg) { return false; }

	// Empty string = cancelled.
	virtual std::string openDialog(const std::string& filters, const std::string& dir) { return ""; }
	virtual std::string saveDialog(const std::string& filters, const std::string& dir,
	                               const std::string& filename) { return ""; }

	// Folder picker (OSDIALOG_OPEN_DIR). Empty string = cancelled.
	virtual std::string openDirDialog() { return ""; }

	virtual std::string getClipboard() const { return ""; }
	virtual void setClipboard(const std::string& text) {}

	virtual void openBrowser(const std::string& url) {}

	// See RackViewport's comment. Production reads APP->scene->rackScroll's real box (scene
	// coordinates) and zoom. Base/test default: a generously large box at zoom 1, so placement
	// code gets sane, deterministic behaviour with no mock installed — a test that wants a
	// specific viewport (module at the rack edge, zoomed in, ...) overrides this instead of
	// touching the real, dispatch-hazardous rackScroll.
	virtual RackViewport getRackViewport() const {
		return RackViewport{math::Rect(math::Vec(-10000.f, -10000.f), math::Vec(20000.f, 20000.f)), 1.f};
	}

	// Is a UI present at all? False in headless/CLI Rack and while the plugin editor is closed.
	// Production is APP->window != nullptr. Routing it through this seam is what makes the
	// window-present branch reachable under test (GuiTaskProcessor starts a private worker when
	// the answer is no): APP->window is permanently null in a test binary, and cannot be made
	// otherwise — Window's constructor calls glfwCreateWindow(), it is marked PRIVATE (a
	// compile-time error from a plugin), and it has no virtuals, so it can be neither
	// constructed nor subclassed headless.
	//
	// A bool, not the Window* it wraps: the pointer would only invite use as a drawing handle,
	// which for the same reasons cannot work. The remaining window *state* (pixel ratio, frame
	// timing, the cursor) still wants a seam of its own — see var/TestFramework_review.md, and
	// note the namespace trap recorded there: it cannot be called `vcv::window`.
	virtual bool hasWindow() const { return false; }

	// Currently-held keyboard modifiers, as a GLFW_MOD_* bitmask. Production is
	// APP->window->getMods().
	//
	// Here rather than in a window-state seam of its own because this is the one piece of that
	// state with real consumers today: Spin's and Mb's onHoverScroll() and Sail's hover both
	// gate their central behaviour on it. It cannot be threaded through the event instead —
	// Rack's HoverScrollEvent carries no mods field (Widget.hpp:321), which is precisely why
	// Rack itself polls the window there, so three of the four call sites have no other route.
	//
	// Returns 0 with no window, matching the "no modifiers held" reading that every call site
	// already treats as the default case.
	//
	// Non-virtual in the base, backed by `testMods` below, so a test's mods survive whichever
	// mock a suite installs: three suites (Strip, MidiMon, MidiCat) install their own UiAccess
	// over the harness's, and a virtual here would leave them answering 0 no matter what the
	// test set. RealUiAccess overrides it to read the actual window; nothing else needs to.
	virtual int getWindowMods() const { return testMods; }

	// The mods a test has asked every reader to see. Set through Test::EventDriver::setMods()
	// rather than assigned directly. Lives on the base interface, not on a mock, for the
	// reason above.
	int testMods = 0;

	// glfwGetKeyName(key, scancode) / glfwGetKeyScancode(key). Both require glfwInit() to have
	// run — gated on GLFW's own `_glfw.initialized`, per dep/glfw/src/input.c — which this
	// plugin never calls in a test binary, so both return NULL/-1 there regardless of window
	// state. That silently defeats event::HoverKeyEvent/SelectKeyEvent::keyName: Rack's own
	// EventState::handleKey() (Rack/src/widget/event.cpp) calls glfwGetKeyName() directly, not
	// through this seam, so this cannot make *that* call site work — see
	// Test::EventDriver::keyAt() for the dispatch-side fix. What this seam does cover: any of
	// this plugin's own code that calls glfwGetKeyName()/glfwGetKeyScancode() directly
	// (keyboard.hpp's keyName(), MidiKey.cpp, Stroke.cpp) could route through here instead and
	// get a correct, headless-safe answer under test — not done as part of this change, which
	// only extends the framework.
	//
	// Test default mirrors rack::widget::getKeyName() (Rack/src/widget/event.cpp), the same
	// name table Rack falls back to internally — no GLFW required, ignores `scancode`
	// (rack::widget::getKeyName() takes only the key). RealUiAccess overrides both with the
	// real glfw* calls. Default getKeyScancode() answers -1, GLFW's own "no scancode" value.
	virtual std::string getKeyName(int key, int scancode) const { return rack::widget::getKeyName(key); }
	virtual int getKeyScancode(int key) const { return -1; }

	// Word-wraps `text` to `width` (0 = no wrap limit) and returns the resulting bounding box,
	// both in the same px unit as `fontSize` and `width`. Used for tutorial bubble sizing
	// (Tutorial framework plan §4.4), which measures in module px and lays out before any
	// drawing happens — so this cannot simply be a drawArgs-time nanovg call.
	//
	// Base default: a deterministic estimate with no nanovg/font dependency, so it works
	// headless and gives every test-mocked UiAccess (MockUiAccess, HarnessUiAccess, ...) a
	// working answer for free. 0.55 * fontSize per character approximates DejaVu Sans/Rack's
	// uiFont closely enough for layout purposes; the real width only matters once real text is
	// actually drawn, by RealUiAccess.
	virtual math::Vec measureTextBox(const std::string& text, float fontSize, float width) const {
		float charWidth = 0.55f * fontSize;
		float lineHeight = 1.2f * fontSize;
		float maxLineWidth = 0.f;
		int lineCount = 0;

		size_t pos = 0;
		bool more = true;
		while (more) {
			size_t nl = text.find('\n', pos);
			std::string paragraph = text.substr(pos, nl == std::string::npos ? std::string::npos : nl - pos);
			more = (nl != std::string::npos);
			pos = more ? nl + 1 : text.size();

			// Greedy word wrap: pack words onto a line while it still fits `width` (word count in
			// characters, including one separating space per word after the first).
			size_t wordStart = 0;
			float lineWidth = 0.f;
			bool lineHasWord = false;
			auto flushLine = [&]() {
				maxLineWidth = std::fmax(maxLineWidth, lineWidth);
				lineCount++;
				lineWidth = 0.f;
				lineHasWord = false;
			};

			size_t i = 0;
			while (i <= paragraph.size()) {
				if (i == paragraph.size() || paragraph[i] == ' ') {
					size_t wordLen = i - wordStart;
					if (wordLen > 0) {
						float wordWidth = wordLen * charWidth;
						float addWidth = lineHasWord ? charWidth + wordWidth : wordWidth;
						if (width > 0.f && lineHasWord && lineWidth + addWidth > width) {
							flushLine();
							addWidth = wordWidth;
						}
						lineWidth += addWidth;
						lineHasWord = true;
					}
					wordStart = i + 1;
				}
				i++;
			}
			// Every paragraph contributes at least one line, even if empty (a blank line).
			flushLine();
		}

		return math::Vec(maxLineWidth, lineCount * lineHeight);
	}
};


// The production implementation; bodies in the .cpp. Declared here — and `final` — so a
// release build's call sites see the concrete type and devirtualize. See cables.hpp.
struct RealUiAccess final : UiAccess {
	bool message(MessageType type, MessageButtons buttons, const std::string& msg) override;
	std::string openDialog(const std::string& filters, const std::string& dir) override;
	std::string saveDialog(const std::string& filters, const std::string& dir, const std::string& filename) override;
	std::string openDirDialog() override;
	std::string getClipboard() const override;
	void setClipboard(const std::string& text) override;
	void openBrowser(const std::string& url) override;
	RackViewport getRackViewport() const override;
	bool hasWindow() const override;
	int getWindowMods() const override;
	std::string getKeyName(int key, int scancode) const override;
	int getKeyScancode(int key) const override;
	math::Vec measureTextBox(const std::string& text, float fontSize, float width) const override;
};
// The shared production instance, defined in the .cpp.
extern RealUiAccess realUiAccess;


// Debug builds keep the mockable seam; release resolves the access statically. See
// cables.hpp for why, and for the DEBUGPLUGIN contract.
#ifdef DEBUGPLUGIN
// Null by default -> the shared instance above is used. Tests point this at a mock.
extern UiAccess* uiAccess;
UiAccess& uiAccessFor();
#else
#define uiAccessFor() ::StoermelderPackOne::vcv::realUiAccess
#endif


namespace ui {

P1_UNUSED
static bool message(MessageType type, MessageButtons buttons, const std::string& msg) {
	return uiAccessFor().message(type, buttons, msg);
}

P1_UNUSED
static std::string openDialog(const std::string& filters, const std::string& dir) {
	return uiAccessFor().openDialog(filters, dir);
}

P1_UNUSED
static std::string openDirectoryDialog() {
	return uiAccessFor().openDirDialog();
}

P1_UNUSED
static std::string saveDialog(const std::string& filters, const std::string& dir, const std::string& filename) {
	return uiAccessFor().saveDialog(filters, dir, filename);
}

P1_UNUSED
static std::string getClipboard() {
	return uiAccessFor().getClipboard();
}

P1_UNUSED
static void setClipboard(const std::string& text) {
	uiAccessFor().setClipboard(text);
}

P1_UNUSED
static void openBrowser(const std::string& url) {
	uiAccessFor().openBrowser(url);
}

P1_UNUSED
static RackViewport getRackViewport() {
	return uiAccessFor().getRackViewport();
}

P1_UNUSED
static bool hasWindow() {
	return uiAccessFor().hasWindow();
}

P1_UNUSED
static int getWindowMods() {
	return uiAccessFor().getWindowMods();
}

P1_UNUSED
static std::string getKeyName(int key, int scancode) {
	return uiAccessFor().getKeyName(key, scancode);
}

P1_UNUSED
static int getKeyScancode(int key) {
	return uiAccessFor().getKeyScancode(key);
}

P1_UNUSED
static math::Vec measureTextBox(const std::string& text, float fontSize, float width) {
	return uiAccessFor().measureTextBox(text, fontSize, width);
}

} // namespace ui

} // namespace vcv
} // namespace StoermelderPackOne

#undef P1_UNUSED
