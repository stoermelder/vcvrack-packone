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
// "tell the user" and "ask yes/no". `openDialog`/`saveDialog` return "" on cancellation,
// and `getClipboard` returns "" when the clipboard holds no text. `filters` is a plain
// osdialog filter string, parsed/freed inside RealUiAccess so callers never own an
// osdialog_filters*.
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

	virtual std::string getClipboard() const { return ""; }
	virtual void setClipboard(const std::string& text) {}

	virtual void openBrowser(const std::string& url) {}
};

// The active access. Null in production → the shared RealUiAccess is used. Tests point this
// at a mock.
// This MUST have external linkage with exactly one definition (in vcv_ui.cpp), not the
// `static` per-TU form.
extern UiAccess* uiAccess;

UiAccess& uiAccessFor();


namespace ui {

// Thin dispatch wrappers — keep the original free-function spelling (message/openDialog/
// saveDialog/getClipboard/setClipboard/openBrowser) so call sites can route through the
// active UiAccess without change; every operation now goes through the swappable layer.

P1_UNUSED
static bool message(MessageType type, MessageButtons buttons, const std::string& msg) {
	return uiAccessFor().message(type, buttons, msg);
}

P1_UNUSED
static std::string openDialog(const std::string& filters, const std::string& dir) {
	return uiAccessFor().openDialog(filters, dir);
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

} // namespace ui

} // namespace vcv
} // namespace StoermelderPackOne

#undef P1_UNUSED
