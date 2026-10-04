#pragma once
#include "MidiScriptEngine.hpp"
#include "../../vcv/ui.hpp"
#include "../../vcv/fs.hpp"

namespace StoermelderPackOne {
namespace MidiScript {

struct ScriptPortInfo : PortInfo {
	bool enabled;
	MidiScriptEngine* se;
	std::string bufferedName;
	std::atomic<bool> queryInFlight{false};

	std::string getName() override {
		if (enabled) {
			bool expected = false;
			if (queryInFlight.compare_exchange_strong(expected, true)) {
				bool queued = se->runLowPriority([=] {
					bufferedName = se->getInputName(portId);
					queryInFlight.store(false);
				});
				if (!queued) {
					queryInFlight.store(false);
				}
			}
			return bufferedName;
		}
		return "<Disabled>";
	}
};


struct ScriptParamQuantity : ParamQuantity {
	bool enabled;
	MidiScriptEngine* se;
	std::string bufferedLabel;
	std::string bufferedDisplayValue;
	std::atomic<bool> queryInFlight{false};

	std::string getLabel() override {
		return enabled ? bufferedLabel : "";
	}
	std::string getDisplayValueString() override {
		if (enabled) {
			bool expected = false;
			if (queryInFlight.compare_exchange_strong(expected, true)) {
				bool queued = se->runLowPriority([=] {
					bufferedLabel = se->getParamName(paramId);
					bufferedDisplayValue = se->getParamFormatValue(paramId);
					queryInFlight.store(false);
				});
				if (!queued) {
					queryInFlight.store(false);
				}
			}
			std::string s = bufferedDisplayValue;
			return !s.empty() ? s : ParamQuantity::getDisplayValueString();
		}
		else {
			return "<Disabled>";
		}
	}
};


// Placeholder menu entry that builds the script-registered items
// (rack.registerContextMenu) asynchronously. getContextMenus() evaluates each
// item's onGetValue callback on the worker thread and then invokes its
// callback with the evaluated specs.
template <typename MODULE>
struct ScriptContextMenuItems : ui::MenuEntry {
	struct Context {
		std::vector<MidiScript::ScriptMenuItem> specs;
		std::atomic<bool> loaded{false};
	};
	MODULE* module;
	std::shared_ptr<Context> ctx;
	bool built = false;

	ScriptContextMenuItems(MODULE* module) : module(module) {
		box.size.y = 0.f;
		ctx = std::make_shared<Context>();
		// Capture a local copy: Apple's Clang rejects capturing the data
		// member `ctx` by name in a capture list.
		std::shared_ptr<Context> c = ctx;
		module->host.getActiveEngine()->getContextMenus([c](const std::vector<MidiScript::ScriptMenuItem>& specs) {
			// Runs on the worker thread once every onGetValue has been
			// evaluated. Only publishes the specs; the menu widgets are
			// constructed by step() on the UI thread.
			c->specs = specs;
			c->loaded.store(true, std::memory_order_release);
		});
	}

	void step() override {
		if (!built && ctx->loaded.load(std::memory_order_acquire)) {
			built = true;
			buildItems();
			requestDelete();
		}
		ui::MenuEntry::step();
	}

	// A "fileopen" item's click: the file dialog, then the file's text to the script. The
	// dialog blocks, so the engine is looked up again afterwards: the script may have
	// been replaced meanwhile, and a stale id is ignored by the engine.
	static void chooseAndReadFile(MODULE* m, const MidiScript::ScriptMenuItem& spec) {
		std::string path = vcv::ui::openDialog("", "");
		if (path.empty()) return;
		// Too big is refused before reading, so a huge file is never loaded.
		if (vcv::fs::getFileSize(path) > MidiScript::ScriptMenuItem::fileMaxBytes) {
			vcv::ui::message(vcv::MessageType::WARNING, vcv::MessageButtons::OK,
				string::f("The file %s is larger than %d bytes", vcv::fs::getFilename(path).c_str(), (int)MidiScript::ScriptMenuItem::fileMaxBytes));
			return;
		}
		std::string content;
		if (!vcv::fs::read(path, content)) {
			vcv::ui::message(vcv::MessageType::WARNING, vcv::MessageButtons::OK,
				string::f("Could not read the file %s", path.c_str()));
			return;
		}
		// The size can change between the check and the read.
		if (content.size() > MidiScript::ScriptMenuItem::fileMaxBytes) return;
		MidiScript::MidiScriptEngine* engine = m->host.getActiveEngine();
		if (engine) engine->invokeContextMenuCallback(spec.callbackId, MidiScript::ScriptMenuClick::file(content, vcv::fs::getFilename(path)));
	}

	void buildItems() {
		Menu* menu = dynamic_cast<Menu*>(parent);
		if (!menu) return;
		MODULE* m = module;
		Widget* anchor = this;
		for (const MidiScript::ScriptMenuItem& spec : ctx->specs) {
			Widget* item;
			if (spec.type == MidiScript::ScriptMenuItem::Type::Boolean) {
				item = createMenuItem(spec.label, CHECKMARK(spec.checked), [m, spec]() {
					m->host.getActiveEngine()->invokeContextMenuCallback(spec.callbackId, spec.checked ? 0 : 1);
				});
			}
			else if (spec.type == MidiScript::ScriptMenuItem::Type::Action) {
				item = createMenuItem(spec.label, "", [m, spec]() {
					m->host.getActiveEngine()->invokeContextMenuCallback(spec.callbackId, MidiScript::ScriptMenuClick());
				});
			}
			else if (spec.type == MidiScript::ScriptMenuItem::Type::FileOpen) {
				item = createMenuItem(spec.label, "", [m, spec]() {
					chooseAndReadFile(m, spec);
				});
			}
			else {
				item = createSubmenuItem(spec.label, "", [m, spec](Menu* sub) {
					for (size_t i = 0; i < spec.options.size(); i++) {
						sub->addChild(createMenuItem(spec.options[i], CHECKMARK(i == static_cast<size_t>(spec.selected)), [m, spec, i]() {
							m->host.getActiveEngine()->invokeContextMenuCallback(spec.callbackId, static_cast<int>(i));
						}));
					}
				});
			}
			menu->addChildAbove(item, anchor);
			anchor = item;
		}
	}
};

} // namespace MidiScript
} // namespace StoermelderPackOne