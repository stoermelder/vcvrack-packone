#include "ui.hpp"
#include <osdialog.h>
#include <cstdlib>

namespace StoermelderPackOne {
namespace vcv {

// The production UI access, backed by osdialog/GLFW/system. Lives here (not in vcv_ui.hpp)
// so that <osdialog.h> stays out of the header graph — see the comment in vcv_ui.hpp.
struct RealUiAccess : UiAccess {
	bool message(MessageType type, MessageButtons buttons, const std::string& msg) override {
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

	std::string openDialog(const std::string& filters, const std::string& dir) override {
		osdialog_filters* f = osdialog_filters_parse(filters.c_str());
		DEFER({ osdialog_filters_free(f); });
		char* pathC = osdialog_file(OSDIALOG_OPEN, dir.empty() ? NULL : dir.c_str(), NULL, f);
		if (!pathC) return "";
		DEFER({ std::free(pathC); });
		return std::string(pathC);
	}

	std::string saveDialog(const std::string& filters, const std::string& dir, const std::string& filename) override {
		osdialog_filters* f = osdialog_filters_parse(filters.c_str());
		DEFER({ osdialog_filters_free(f); });
		char* pathC = osdialog_file(OSDIALOG_SAVE, dir.empty() ? NULL : dir.c_str(), filename.empty() ? NULL : filename.c_str(), f);
		if (!pathC) return "";
		DEFER({ std::free(pathC); });
		return std::string(pathC);
	}

	std::string getClipboard() const override {
		const char* text = glfwGetClipboardString(APP->window->win);
		return text ? std::string(text) : "";
	}

	void setClipboard(const std::string& text) override {
		glfwSetClipboardString(APP->window->win, text.c_str());
	}

	void openBrowser(const std::string& url) override {
		system::openBrowser(url);
	}
};

// The single definition of the swappable UI layer's active access — external linkage,
// exactly one definition in this TU, so a mock installed in a test TU is seen by code
// compiled into the plugin dylib.
UiAccess* uiAccess = nullptr;

UiAccess& uiAccessFor() {
	static RealUiAccess realAccess;
	return uiAccess ? *uiAccess : realAccess;
}

} // namespace vcv
} // namespace StoermelderPackOne
