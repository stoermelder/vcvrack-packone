#include "ui.hpp"
#include <osdialog.h>
#include <cstdlib>

namespace StoermelderPackOne {
namespace vcv {

// The production UI access, backed by osdialog/GLFW/system. Lives here (not in vcv_ui.hpp)
// so that <osdialog.h> stays out of the header graph — see the comment in vcv_ui.hpp.
bool RealUiAccess::message(MessageType type, MessageButtons buttons, const std::string& msg) {
	osdialog_message_level level = OSDIALOG_INFO;
	switch (type) {
		case MessageType::INFO: level = OSDIALOG_INFO; break;
		case MessageType::WARNING: level = OSDIALOG_WARNING; break;
		case MessageType::ERROR: level = OSDIALOG_ERROR; break;
	}
	osdialog_message_buttons b = OSDIALOG_OK;
	switch (buttons) {
		case MessageButtons::OK: b = OSDIALOG_OK; break;
		case MessageButtons::YES_NO: b = OSDIALOG_YES_NO; break;
	}
	return osdialog_message(level, b, msg.c_str()) != 0;
}

std::string RealUiAccess::openDialog(const std::string& filters, const std::string& dir) {
	osdialog_filters* f = osdialog_filters_parse(filters.c_str());
	DEFER({ osdialog_filters_free(f); });
	char* pathC = osdialog_file(OSDIALOG_OPEN, dir.empty() ? NULL : dir.c_str(), NULL, f);
	if (!pathC) return "";
	DEFER({ std::free(pathC); });
	return std::string(pathC);
}

std::string RealUiAccess::saveDialog(const std::string& filters, const std::string& dir, const std::string& filename) {
	osdialog_filters* f = osdialog_filters_parse(filters.c_str());
	DEFER({ osdialog_filters_free(f); });
	char* pathC = osdialog_file(OSDIALOG_SAVE, dir.empty() ? NULL : dir.c_str(), filename.empty() ? NULL : filename.c_str(), f);
	if (!pathC) return "";
	DEFER({ std::free(pathC); });
	return std::string(pathC);
}

std::string RealUiAccess::openDirDialog() {
	char* pathC = osdialog_file(OSDIALOG_OPEN_DIR, NULL, NULL, NULL);
	if (!pathC) return "";
	DEFER({ std::free(pathC); });
	return std::string(pathC);
}

std::string RealUiAccess::getClipboard() const {
	const char* text = glfwGetClipboardString(APP->window->win);
	return text ? std::string(text) : "";
}

void RealUiAccess::setClipboard(const std::string& text) {
	glfwSetClipboardString(APP->window->win, text.c_str());
}

void RealUiAccess::openBrowser(const std::string& url) {
	system::openBrowser(url);
}

// The shared production instance; namespace-scope so no __cxa_guard is tested on access.
// In a release build this is what the uiAccessFor() macro names directly.
RealUiAccess realUiAccess;


#ifdef DEBUGPLUGIN
// One definition, external linkage: a mock installed in a test TU must be seen by
// code compiled into the dylib. See the declaration in the header.
UiAccess* uiAccess = nullptr;
UiAccess& uiAccessFor() {
	return uiAccess ? *uiAccess : realUiAccess;
}
#endif

} // namespace vcv
} // namespace StoermelderPackOne
