#pragma once
#include "Logging.hpp"
#include "MidiScriptApiDoc.hpp"
#include "../../ui/ScriptEditor.hpp"

namespace StoermelderPackOne {
namespace MidiKit {

// The script editor's view of this module: Apply loads the buffer like "Paste from
// clipboard" does, and the editor never touches `filename`. Owned by the editor
// dialog, which ~MidiKitWidgetBase() dismisses before the module can go away.
template <typename WIDGET>
struct MidiKitEditorHost : ui::editor::ScriptEditorHost {
	using MODULE = typename WIDGET::MODULE;

	MODULE* m;
	// Weak: the editor can outlive the widget by the one frame its deletion takes.
	WeakPtr<WIDGET> widget;
	int logListenerId = -1;

	// What the editor does when the last apply has loaded or failed to.
	std::function<void(bool)> pendingDone;
	// An apply is on its way: the log's RESET marks the swap, and only what follows
	// it is the new script's.
	bool pendingReset = false;

	MidiKitEditorHost(WIDGET* w) : m(w->module), widget(w) {}
	~MidiKitEditorHost() {
		detachLog();
	}
	void detachLog() {
		if (widget && logListenerId >= 0) widget->logs.remove(logListenerId);
		logListenerId = -1;
	}
	void attachLog(std::function<void(const std::string&)> append, std::function<void()> clear) override {
		if (!widget) return;
		// What the log display shows so far, oldest first (its buffer keeps the newest first).
		for (auto it = widget->buffer.rbegin(); it != widget->buffer.rend(); ++it) {
			append(formatLogEntry(*it, widget->logTimeMode()));
		}
		logListenerId = widget->logs.add([this, append, clear](const ScriptLog::Entry& s) {
			LOG_FORMAT format = std::get<0>(s);
			if (format == LOG_FORMAT::RESET) {
				clear();
				pendingReset = false;
				return;
			}
			append(formatLogEntry(s, widget->logTimeMode()));
			if (!pendingReset) {
				if (format == LOG_FORMAT::LOAD) finishApply(true);
				else if (format == LOG_FORMAT::ERROR) finishApply(false);
			}
		});
	}
	void finishApply(bool ok) {
		// Moved out first: the callback may close the editor, which destroys this.
		std::function<void(bool)> done = std::move(pendingDone);
		pendingDone = nullptr;
		if (done) done(ok);
	}
	void onEditorClosed() override {
		detachLog();
	}
	void apply(const std::string& text, std::function<void(bool)> done) override {
		m->loadScriptKeepingConfig(text);
		// An empty script logs no outcome to wait for.
		if (text.empty()) {
			pendingDone = nullptr;
			if (done) done(true);
			return;
		}
		pendingDone = std::move(done);
		pendingReset = true;
	}
	std::string runningScript() override {
		return m->host.script;
	}
	std::string headerSuffix() override {
		if (m->host.isQuickJsEngine()) return "QuickJs";
		if (m->host.isLuaEngine()) return "Lua";
		return "";
	}
	std::vector<ui::editor::scripttext::ApiGroup> apiReference() override {
		return MidiScript::apiReference();
	}
	std::vector<ui::editor::scripttext::ScriptTemplate> templates() override {
		return MidiScript::scriptTemplates();
	}
	// JavaScript for QuickJs, Lua otherwise.
	ui::editor::scripttext::ScriptSyntax syntax() override {
		if (m->host.isQuickJsEngine()) return ui::editor::scripttext::ScriptSyntax("//", ";");
		return ui::editor::scripttext::ScriptSyntax("--", "");
	}
};

} // namespace MidiKit
} // namespace StoermelderPackOne
