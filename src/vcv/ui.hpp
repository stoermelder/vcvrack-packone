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
	bool hasWindow() const override;
	int getWindowMods() const override;
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
static bool hasWindow() {
	return uiAccessFor().hasWindow();
}

P1_UNUSED
static int getWindowMods() {
	return uiAccessFor().getWindowMods();
}

} // namespace ui

} // namespace vcv
} // namespace StoermelderPackOne

#undef P1_UNUSED
