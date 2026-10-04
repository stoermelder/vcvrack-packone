// Script editor: the dialog opened from MidiKitWidgetBase (menu / Alt+E), its apply and
// close-guard behaviour, and the keys the editing field handles itself. The text arithmetic
// is covered by src/ui/ScriptEditorText.test.cpp.

namespace {

using namespace StoermelderPackOne::ui::editor;

struct EditorUiMock : StoermelderPackOne::vcv::UiAccess {
	std::vector<std::string> messages;
	bool answer = false;
	std::string clipboard;
	void setClipboard(const std::string& text) override { clipboard = text; }
	bool message(StoermelderPackOne::vcv::MessageType, StoermelderPackOne::vcv::MessageButtons, const std::string& msg) override {
		messages.push_back(msg);
		return answer;
	}
};

// Declare after the Harness (see Ahab.test.widget.hpp): the harness installs its own UiAccess.
struct EditorMock {
	EditorUiMock ui;
	Test::mock::Guard<StoermelderPackOne::vcv::UiAccess> uiGuard{StoermelderPackOne::vcv::uiAccess, &ui};
};

static std::vector<ScriptEditorOverlay*> editorOverlays() {
	std::vector<ScriptEditorOverlay*> r;
	for (rack::widget::Widget* c : APP->scene->children) {
		if (auto* o = dynamic_cast<ScriptEditorOverlay*>(c)) r.push_back(o);
	}
	return r;
}

// Overlays live on APP->scene, outside the harness's own bookkeeping, so a test that leaves
// one open would leak it into the next TEST_CASE.
// Also removes context menus a test opened (they are MenuOverlays on the scene too).
struct EditorCleanup {
	std::vector<rack::widget::Widget*> before;
	EditorCleanup() {
		for (rack::widget::Widget* c : APP->scene->children) {
			if (dynamic_cast<rack::ui::MenuOverlay*>(c)) before.push_back(c);
		}
	}
	~EditorCleanup() {
		std::vector<rack::widget::Widget*> added;
		for (rack::widget::Widget* c : APP->scene->children) {
			if (dynamic_cast<rack::ui::MenuOverlay*>(c) && std::find(before.begin(), before.end(), c) == before.end()) added.push_back(c);
		}
		for (rack::widget::Widget* o : added) {
			APP->event->finalizeWidget(o);
			APP->scene->removeChild(o);
			delete o;
		}
	}
};

// One editor opened on `script`, stepped once so the dialog is laid out and centered.
struct OpenEditor {
	ScriptEditorOverlay* overlay;
	ScriptEditorDialog* dialog;
	ScriptEditField* field;
};

static OpenEditor openEditorOn(Test::Harness& h, MidiKitWidget* mw) {
	size_t before = editorOverlays().size();
	mw->openEditor();
	std::vector<ScriptEditorOverlay*> now = editorOverlays();
	REQUIRE(now.size() == before + 1);
	OpenEditor e;
	e.overlay = now.back();
	e.overlay->step();
	e.dialog = e.overlay->dialog;
	e.field = h.events().find<ScriptEditField>(e.overlay);
	return e;
}

// The harness with a MidiKit and its widget on the rack, and the overlays a test opens removed
// again at the end. The module is the production one (its own worker) unless asked for a
// synchronous worker, so rack.setConfig publishes inline.
struct EditorRig : Test::Harness {
	enum Worker { ProductionWorker, SyncWorker };
	EditorCleanup cleanup;
	MidiKitModule* m;
	MidiKitWidget* mw;

	explicit EditorRig(Worker worker = ProductionWorker) {
		if (worker == SyncWorker) {
			m = addModule<MidiKitModule>(std::function<MidiKitModule*()>([]() { MidiKitModule* mod = createModule(); mod->model = modelMidiKit; return mod; }));
		}
		else {
			m = addModule<MidiKitModule>("MidiKit");
		}
		mw = addWidget<MidiKitWidget>(m);
	}
};

static bool selectKeyConsumed(ScriptEditField* field, int key, int mods) {
	rack::widget::EventContext c;
	rack::widget::Widget::SelectKeyEvent e;
	e.context = &c;
	e.key = key;
	e.action = GLFW_PRESS;
	e.mods = mods;
	field->onSelectKey(e);
	return c.target == field;
}

}

TEST_CASE("Editor: opens on the applied script and takes focus", "[MidiKit][Editor]") {
	EditorRig h;
	MidiKitModule* m = h.m;
	MidiKitWidget* mw = h.mw;
	m->loadScript("// @engine QuickJs@v1\nlog(1);\n");

	OpenEditor e = openEditorOn(h, mw);
	REQUIRE(e.field->text == m->host.script);
	REQUIRE(APP->event->selectedWidget == e.field);
	REQUIRE_FALSE(e.dialog->dirty);
	REQUIRE(mw->editorOverlay.get() == e.overlay);
}

TEST_CASE("Editor: Alt+E opens it, a second request does not stack another", "[MidiKit][Editor]") {
	EditorRig h;
	MidiKitWidget* mw = h.mw;

	rack::widget::EventContext c;
	rack::widget::Widget::HoverKeyEvent k;
	k.context = &c;
	k.key = GLFW_KEY_E;
	k.keyName = "e";
	k.action = GLFW_PRESS;
	k.mods = GLFW_MOD_ALT;
	mw->onHoverKey(k);
	REQUIRE(editorOverlays().size() == 1);
	REQUIRE(c.target == mw);

	mw->openEditor();
	REQUIRE(editorOverlays().size() == 1);
}

TEST_CASE("Editor: Ctrl+Enter applies the buffer and keeps the editor open", "[MidiKit][Editor]") {
	EditorRig h;
	MidiKitModule* m = h.m;
	MidiKitWidget* mw = h.mw;
	m->loadScript("// one\n");
	EditorMock mock;

	OpenEditor e = openEditorOn(h, mw);
	h.events().type("// two\n");
	REQUIRE(e.dialog->dirty);

	REQUIRE(h.events().keyPress(GLFW_KEY_ENTER, RACK_MOD_CTRL));
	REQUIRE(m->host.script == "// two\n// one\n");
	REQUIRE(e.field->text == m->host.script);
	REQUIRE_FALSE(e.dialog->dirty);
	REQUIRE_FALSE(e.overlay->requestedDelete);
	REQUIRE(mock.ui.messages.empty());
}

TEST_CASE("Editor: Ctrl+Shift+Enter applies and closes without asking", "[MidiKit][Editor]") {
	EditorRig h(EditorRig::SyncWorker);
	MidiKitModule* m = h.m;
	MidiKitWidget* mw = h.mw;
	EditorMock mock;

	OpenEditor e = openEditorOn(h, mw);
	const std::string script = "/**\n * @engine QuickJs@v1\n */\nrack.log(\"2\");\n";
	e.field->setText(script);
	REQUIRE(h.events().keyPress(GLFW_KEY_ENTER, RACK_MOD_CTRL | GLFW_MOD_SHIFT));
	REQUIRE(m->host.script == script);
	// It closes once the script has loaded, which the widget learns from the log.
	h.dspStep();
	h.uiFrames(2);
	REQUIRE(e.overlay->requestedDelete);
	REQUIRE(mock.ui.messages.empty());
}

TEST_CASE("Editor: Apply & Close keeps the editor open when the script fails to load", "[MidiKit][Editor]") {
	EditorRig h(EditorRig::SyncWorker);
	MidiKitModule* m = h.m;
	OpenEditor e = openEditorOn(h, h.mw);

	SECTION("a script error") {
		e.field->setText("/**\n * @engine QuickJs@v1\n */\nthis is not javascript(\n");
	}
	SECTION("a script no engine takes") {
		e.field->setText("rack.log(\"no header\");\n");
	}
	SECTION("a Lua error") {
		e.field->setText("/**\n * @engine minilua@v1\n */\nthis is not lua(\n");
	}
	e.dialog->applyAndClose();
	h.dspStep();
	h.uiFrames(2);
	REQUIRE_FALSE(e.overlay->requestedDelete);
	REQUIRE(m->host.script == e.field->text);   // still the module's script
	REQUIRE_FALSE(e.dialog->logPanel->view->lines.empty());

	// Fixed and applied again: it closes.
	e.field->setText("/**\n * @engine QuickJs@v1\n */\nrack.log(\"ok\");\n");
	e.dialog->applyAndClose();
	h.dspStep();
	h.uiFrames(2);
	REQUIRE(e.overlay->requestedDelete);
}

TEST_CASE("Editor: Apply & Close of an empty script closes", "[MidiKit][Editor]") {
	EditorRig h(EditorRig::SyncWorker);
	OpenEditor e = openEditorOn(h, h.mw);

	e.field->setText("");
	e.dialog->applyAndClose();
	h.dspStep();
	h.uiFrames(2);
	REQUIRE(e.overlay->requestedDelete);
}

TEST_CASE("Editor: closing with unapplied changes asks, and 'no' keeps the text", "[MidiKit][Editor]") {
	EditorRig h;
	MidiKitModule* m = h.m;
	MidiKitWidget* mw = h.mw;
	m->loadScript("// kept\n");
	EditorMock mock;
	mock.ui.answer = false;

	OpenEditor e = openEditorOn(h, mw);
	h.events().type("edit ");
	const std::string edited = e.field->text;

	SECTION("Esc") {
		h.events().keyPress(GLFW_KEY_ESCAPE);
	}
	SECTION("click outside the dialog") {
		h.events().click(rack::math::Vec(2.f, 2.f));
	}
	SECTION("Close button") {
		e.dialog->requestClose();
	}

	e.overlay->step();   // a frame passes: focus returns to the field after a click outside
	REQUIRE(mock.ui.messages.size() == 1);
	REQUIRE_FALSE(e.overlay->requestedDelete);
	REQUIRE(e.field->text == edited);
	REQUIRE(e.dialog->dirty);
	REQUIRE(m->host.script == "// kept\n");
	REQUIRE(APP->event->selectedWidget == e.field);

	// Answering yes discards and closes.
	mock.ui.answer = true;
	e.dialog->requestClose();
	REQUIRE(mock.ui.messages.size() == 2);
	REQUIRE(e.overlay->requestedDelete);
	REQUIRE(m->host.script == "// kept\n");
}

TEST_CASE("Editor: closing a clean buffer does not ask", "[MidiKit][Editor]") {
	EditorRig h;
	MidiKitWidget* mw = h.mw;
	EditorMock mock;

	OpenEditor e = openEditorOn(h, mw);
	h.events().keyPress(GLFW_KEY_ESCAPE);
	REQUIRE(mock.ui.messages.empty());
	REQUIRE(e.overlay->requestedDelete);
	REQUIRE_FALSE(mw->editorOverlay.get() == nullptr);   // weak ref lives until the deletion happens
}

TEST_CASE("Editor: a buffer edited back to the applied text is clean again", "[MidiKit][Editor]") {
	EditorRig h;
	MidiKitWidget* mw = h.mw;
	OpenEditor e = openEditorOn(h, mw);

	h.events().type("x");
	REQUIRE(e.dialog->dirty);
	h.events().keyPress(GLFW_KEY_BACKSPACE);
	REQUIRE_FALSE(e.dialog->dirty);
}

TEST_CASE("Editor: Ctrl+Z, Ctrl+Shift+Z and Ctrl+Y are consumed, and undo followed by redo leaves the text as typed", "[MidiKit][Editor]") {
	EditorRig h;
	MidiKitWidget* mw = h.mw;
	OpenEditor e = openEditorOn(h, mw);
	h.events().type("abc");

	REQUIRE(selectKeyConsumed(e.field, GLFW_KEY_Z, RACK_MOD_CTRL));
	REQUIRE(selectKeyConsumed(e.field, GLFW_KEY_Z, RACK_MOD_CTRL | GLFW_MOD_SHIFT));
	REQUIRE(selectKeyConsumed(e.field, GLFW_KEY_Y, RACK_MOD_CTRL));
	REQUIRE(e.field->text == "abc");
}

TEST_CASE("Editor: removing the module widget removes the editor without a prompt", "[MidiKit][Editor]") {
	Test::Harness h;
	EditorCleanup cleanup;
	MidiKitModule* m = h.addModule<MidiKitModule>("MidiKit");
	EditorMock mock;

	MidiKitWidget* mw = Test::createWidget<MidiKitWidget>(m);
	mw->openEditor();
	REQUIRE(editorOverlays().size() == 1);
	ScriptEditorOverlay* overlay = editorOverlays()[0];
	overlay->step();
	h.events().find<ScriptEditField>(overlay)->insertText("unapplied");

	Test::destroyWidget(mw);
	REQUIRE(overlay->requestedDelete);
	REQUIRE(mock.ui.messages.empty());
	// Cut off from the module that is gone.
	REQUIRE_FALSE(overlay->dialog->host);
}

TEST_CASE("Editor field: Tab inserts spaces, Enter inserts a newline", "[MidiKit][Editor]") {
	EditorRig h;
	MidiKitWidget* mw = h.mw;
	OpenEditor e = openEditorOn(h, mw);

	REQUIRE(h.events().keyPress(GLFW_KEY_TAB));
	REQUIRE(e.field->text == "    ");
	REQUIRE(APP->event->selectedWidget == e.field);   // focus did not move on
	REQUIRE(h.events().keyPress(GLFW_KEY_ENTER));
	REQUIRE(e.field->text == "    \n    ");   // the new line keeps the indentation
}

TEST_CASE("Editor field: Up/Down keep a sticky column across short lines", "[MidiKit][Editor]") {
	EditorRig h;
	MidiKitWidget* mw = h.mw;
	OpenEditor e = openEditorOn(h, mw);

	e.field->setText("abcdef\nxy\nlonger line");
	e.field->cursor = e.field->selection = 5;       // line 0, col 5

	h.events().keyPress(GLFW_KEY_DOWN);
	REQUIRE(e.field->cursor == 9);                  // end of "xy"
	h.events().keyPress(GLFW_KEY_DOWN);
	REQUIRE(e.field->cursor == 10 + 5);             // column 5 restored
	h.events().keyPress(GLFW_KEY_UP);
	REQUIRE(e.field->cursor == 9);
	h.events().keyPress(GLFW_KEY_UP);
	REQUIRE(e.field->cursor == 5);

	// Shift extends the selection from where it started.
	h.events().keyPress(GLFW_KEY_DOWN, GLFW_MOD_SHIFT);
	REQUIRE(e.field->selection == 5);
	REQUIRE(e.field->cursor == 9);
}

TEST_CASE("Editor field: Home and End stay on the line, also on an empty first line", "[MidiKit][Editor]") {
	EditorRig h;
	MidiKitWidget* mw = h.mw;
	OpenEditor e = openEditorOn(h, mw);

	e.field->setText("\nabc\ndef");
	e.field->cursor = e.field->selection = 0;
	h.events().keyPress(GLFW_KEY_HOME);
	REQUIRE(e.field->cursor == 0);
	h.events().keyPress(GLFW_KEY_END);
	REQUIRE(e.field->cursor == 0);

	e.field->cursor = e.field->selection = 6;       // inside "def"
	h.events().keyPress(GLFW_KEY_HOME);
	REQUIRE(e.field->cursor == 5);
	h.events().keyPress(GLFW_KEY_END);
	REQUIRE(e.field->cursor == 8);
}

TEST_CASE("Editor field: a click lands on the character under it", "[MidiKit][Editor]") {
	EditorRig h;
	MidiKitWidget* mw = h.mw;
	OpenEditor e = openEditorOn(h, mw);
	e.field->setText("ab\ncde\nf");

	// Line 1, between 'd' and 'e'. The field's own column origin and cell width, not panel math.
	rack::math::Vec local(e.field->colToX(2), ScriptEditField::kPadY + 1.5f * ScriptEditField::kLineHeight);
	REQUIRE(h.events().click(Test::EventDriver::pointIn(e.field, local)));
	REQUIRE(e.field->cursor == 3 + 2);
	REQUIRE(e.field->selection == e.field->cursor);

	// Below the last line: end of the buffer. Right of a short line: end of that line.
	e.field->getTextPosition(rack::math::Vec(2000.f, 2000.f));
	REQUIRE(e.field->getTextPosition(rack::math::Vec(2000.f, 2000.f)) == 8);
	REQUIRE(e.field->getTextPosition(rack::math::Vec(2000.f, ScriptEditField::kPadY + 0.5f * ScriptEditField::kLineHeight)) == 2);
}

TEST_CASE("Editor: Revert discards the edits and shows the running script", "[MidiKit][Editor]") {
	EditorRig h;
	MidiKitModule* m = h.m;
	MidiKitWidget* mw = h.mw;
	EditorMock mock;
	m->loadScript("// running\n");
	OpenEditor e = openEditorOn(h, mw);

	h.events().type("junk");
	REQUIRE(e.dialog->dirty);
	e.dialog->revert();
	REQUIRE(e.field->text == "// running\n");
	REQUIRE_FALSE(e.dialog->dirty);
	REQUIRE(e.field->cursor == 0);

	// Reverting follows the module, not the text the editor was opened with.
	m->loadScript("// newer\n");
	e.dialog->revert();
	REQUIRE(e.field->text == "// newer\n");
	REQUIRE(mock.ui.messages.empty());
}

TEST_CASE("Editor field: Shift+Tab outdents, and Tab/Shift+Tab work on a multi-line selection", "[MidiKit][Editor]") {
	EditorRig h;
	MidiKitWidget* mw = h.mw;
	OpenEditor e = openEditorOn(h, mw);

	e.field->setText("        x");
	e.field->cursor = e.field->selection = 9;
	REQUIRE(h.events().keyPress(GLFW_KEY_TAB, GLFW_MOD_SHIFT));
	REQUIRE(e.field->text == "    x");
	REQUIRE(e.field->cursor == 5);
	REQUIRE(e.dialog->dirty);
	h.events().keyPress(GLFW_KEY_TAB, GLFW_MOD_SHIFT);
	h.events().keyPress(GLFW_KEY_TAB, GLFW_MOD_SHIFT);   // nothing left to remove
	REQUIRE(e.field->text == "x");
	REQUIRE(e.field->cursor == 1);
	REQUIRE(APP->event->selectedWidget == e.field);

	e.field->setText("a\nb\nc");
	e.field->selection = 0;
	e.field->cursor = 3;                                  // a and b
	h.events().keyPress(GLFW_KEY_TAB);
	REQUIRE(e.field->text == "    a\n    b\nc");
	h.events().keyPress(GLFW_KEY_TAB, GLFW_MOD_SHIFT);
	REQUIRE(e.field->text == "a\nb\nc");
}

TEST_CASE("Editor field: Enter keeps the indentation", "[MidiKit][Editor]") {
	EditorRig h;
	MidiKitWidget* mw = h.mw;
	OpenEditor e = openEditorOn(h, mw);

	e.field->setText("    foo");
	e.field->cursor = e.field->selection = 7;
	h.events().keyPress(GLFW_KEY_ENTER);
	REQUIRE(e.field->text == "    foo\n    ");
	REQUIRE(e.field->cursor == 12);

	// Splitting inside the indent copies only what is left of the cursor.
	e.field->setText("      x");
	e.field->cursor = e.field->selection = 2;
	h.events().keyPress(GLFW_KEY_ENTER);
	REQUIRE(e.field->text == "  \n  " "    x");

	// Ctrl+Enter still applies instead of inserting a line.
	e.field->setText("x");
	h.events().keyPress(GLFW_KEY_ENTER, RACK_MOD_CTRL);
	REQUIRE(e.field->text == "x");
}

TEST_CASE("Editor field: undo and redo", "[MidiKit][Editor]") {
	EditorRig h;
	MidiKitModule* m = h.m;
	MidiKitWidget* mw = h.mw;
	OpenEditor e = openEditorOn(h, mw);
	const int CTRL = RACK_MOD_CTRL;
	const int CTRL_SHIFT = RACK_MOD_CTRL | GLFW_MOD_SHIFT;

	SECTION("typing undoes word by word") {
		h.events().type("ab cd");
		REQUIRE(h.events().keyPress(GLFW_KEY_Z, CTRL));
		REQUIRE(e.field->text == "ab");
		h.events().keyPress(GLFW_KEY_Z, CTRL);
		REQUIRE(e.field->text == "");
		REQUIRE_FALSE(e.dialog->dirty);
		h.events().keyPress(GLFW_KEY_Z, CTRL);            // nothing left: harmless
		REQUIRE(e.field->text == "");

		REQUIRE(h.events().keyPress(GLFW_KEY_Z, CTRL_SHIFT));
		REQUIRE(e.field->text == "ab");
		h.events().keyPress(GLFW_KEY_Z, CTRL_SHIFT);
		REQUIRE(e.field->text == "ab cd");
		REQUIRE(e.field->cursor == 5);
		REQUIRE(e.dialog->dirty);
	}
	SECTION("a new edit clears redo") {
		h.events().type("ab");
		h.events().keyPress(GLFW_KEY_Z, CTRL);
		h.events().type("x");
		h.events().keyPress(GLFW_KEY_Z, CTRL_SHIFT);
		REQUIRE(e.field->text == "x");
	}
	SECTION("moving the cursor ends a typing run") {
		h.events().type("ab");
		h.events().keyPress(GLFW_KEY_LEFT);
		h.events().type("x");
		REQUIRE(e.field->text == "axb");
		h.events().keyPress(GLFW_KEY_Z, CTRL);
		REQUIRE(e.field->text == "ab");
		REQUIRE(e.field->cursor == 1);                  // where the edit happened
		h.events().keyPress(GLFW_KEY_Z, CTRL);
		REQUIRE(e.field->text == "");
	}
	SECTION("backspace, Enter, Tab and paste-like edits are their own steps") {
		h.events().type("abc");
		h.events().keyPress(GLFW_KEY_BACKSPACE);
		h.events().keyPress(GLFW_KEY_BACKSPACE);
		REQUIRE(e.field->text == "a");
		h.events().keyPress(GLFW_KEY_Z, CTRL);
		REQUIRE(e.field->text == "ab");
		h.events().keyPress(GLFW_KEY_ENTER);
		h.events().keyPress(GLFW_KEY_TAB);
		REQUIRE(e.field->text == "ab\n    ");
		h.events().keyPress(GLFW_KEY_Z, CTRL);
		REQUIRE(e.field->text == "ab\n");
		h.events().keyPress(GLFW_KEY_Z, CTRL);
		REQUIRE(e.field->text == "ab");
	}
	SECTION("Revert can be undone") {
		m->loadScript("// run\n");
		h.events().type("junk");
		e.dialog->revert();
		REQUIRE(e.field->text == "// run\n");
		h.events().keyPress(GLFW_KEY_Z, CTRL);
		REQUIRE(e.field->text == "junk");
	}
	SECTION("history starts empty") {
		h.events().keyPress(GLFW_KEY_Z, CTRL);
		REQUIRE(e.field->text == m->host.script);
	}
}

TEST_CASE("Editor: the header names the running engine and follows an apply", "[MidiKit][Editor]") {
	EditorRig h;
	MidiKitModule* m = h.m;
	MidiKitWidget* mw = h.mw;
	EditorMock mock;
	const std::string em = "\xE2\x80\x94";

	SECTION("no script: plain header") {
		OpenEditor e = openEditorOn(h, mw);
		e.overlay->step();
		REQUIRE(e.dialog->headerLabel->text == "Script");
	}
	SECTION("QuickJs, then Lua after editing the header line") {
		m->loadScript("// @engine QuickJs@v1\nlog(1);\n");
		OpenEditor e = openEditorOn(h, mw);
		e.overlay->step();
		REQUIRE(e.dialog->headerLabel->text == "Script " + em + " QuickJs");

		e.field->setText("--[[\n@engine minilua@v1\n--]]\nlog(1)\n");
		e.dialog->apply();
		e.overlay->step();
		REQUIRE(e.dialog->headerLabel->text == "Script " + em + " Lua");
	}
}

TEST_CASE("Editor: applying keeps the script's saved config", "[MidiKit][Editor][Config]") {
	// Synchronous worker, so rack.setConfig publishes inline.
	EditorRig h(EditorRig::SyncWorker);
	MidiKitModule* m = h.m;
	MidiKitWidget* mw = h.mw;
	EditorMock mock;

	// setConfig is applied before onLoad reads the key back, so only a restored value
	// can make `seen` differ from what the new script itself would set (nothing).
	const char* js = R"(/**
 * @engine QuickJs@v1
 */
if (rack.getConfig("channel") === undefined) rack.setConfig("channel", 7);
rack.onLoad = function() { rack.setConfig("seen", rack.getConfig("channel")); };
)";
	const char* lua = R"(--[[
@engine minilua@v1
--]]
if rack.getConfig("channel") == nil then rack.setConfig("channel", 7) end
rack.onLoad = function() rack.setConfig("seen", rack.getConfig("channel")) end
)";
	FOR_EACH_LANG;
	const char* script = Pair{js, lua}.get(lang);
	m->loadScript(script);
	h.uiFrames(3);
	REQUIRE(configInt(m->peekConfigJson(), "channel") == 7);

	// Something the script saved that the new text knows nothing about.
	std::string saved = m->peekConfigJson();
	json_t* j = json_loads(saved.c_str(), 0, nullptr);
	json_object_set_new(j, "extra", json_integer(42));
	char* dumped = json_dumps(j, JSON_COMPACT);
	m->loadScript(script, dumped);
	free(dumped);
	json_decref(j);
	h.uiFrames(3);
	REQUIRE(configInt(m->peekConfigJson(), "extra") == 42);

	OpenEditor e = openEditorOn(h, mw);
	e.field->setText(std::string(script) + "\n// edited\n");
	e.dialog->apply();
	h.uiFrames(3);

	REQUIRE(m->host.script == std::string(script) + "\n// edited\n");
	std::string after = m->peekConfigJson();
	REQUIRE(configInt(after, "channel") == 7);
	REQUIRE(configInt(after, "extra") == 42);
	REQUIRE(configInt(after, "seen") == 7);

	e.dialog->requestClose();
	for (ScriptEditorOverlay* o : editorOverlays()) { APP->event->finalizeWidget(o); APP->scene->removeChild(o); delete o; }
}

TEST_CASE("Editor: the log area mirrors the module's log", "[MidiKit][Editor][Log]") {
	EditorRig h;
	MidiKitModule* m = h.m;
	MidiKitWidget* mw = h.mw;

	// Entries from before the editor opened are there from the start, oldest first.
	m->log.pushText("first");
	m->log.pushText("second");
	h.uiFrame();
	OpenEditor e = openEditorOn(h, mw);
	// (A fresh module logs "No script" first.)
	const std::vector<std::string>& lines = e.dialog->logPanel->view->lines;
	REQUIRE(lines.size() == 3);
	REQUIRE(lines[1] == "first");
	REQUIRE(lines[2] == "second");

	// New ones follow while it is open; a multi-line entry becomes several lines.
	m->log.pushText("third");
	m->log.pushText("SyntaxError: boom\n    at line 3");
	h.uiFrame();
	REQUIRE(e.dialog->logPanel->view->lines.size() == 6);
	REQUIRE(e.dialog->logPanel->view->lines[3] == "third");
	REQUIRE(e.dialog->logPanel->view->lines[5] == "    at line 3");

	// A reset (a script load) clears the area, as it clears the log display.
	m->log.pushReset();
	m->log.pushText("after");
	h.uiFrame();
	REQUIRE(e.dialog->logPanel->view->lines == std::vector<std::string>({"after"}));
}

TEST_CASE("Editor: apply shows the new script's log output", "[MidiKit][Editor][Log]") {
	EditorRig h(EditorRig::SyncWorker);
	MidiKitWidget* mw = h.mw;
	OpenEditor e = openEditorOn(h, mw);

	e.field->setText("/**\n * @engine QuickJs@v1\n */\nrack.log(\"hello from the editor\");\n");
	e.dialog->apply();
	h.dspStep();   // the audio thread completes the swap and its log reset
	h.uiFrames(2);
	bool found = false;
	for (const std::string& l : e.dialog->logPanel->view->lines) {
		if (l.find("hello from the editor") != std::string::npos) found = true;
	}
	REQUIRE(found);
}

TEST_CASE("Editor: a load error shows up in the log area, and the editor stays open", "[MidiKit][Editor][Log]") {
	EditorRig h(EditorRig::SyncWorker);
	MidiKitWidget* mw = h.mw;
	OpenEditor e = openEditorOn(h, mw);

	e.field->setText("/**\n * @engine QuickJs@v1\n */\nthis is not javascript(\n");
	e.dialog->apply();
	h.dspStep();
	h.uiFrames(2);
	REQUIRE_FALSE(e.dialog->logPanel->view->lines.empty());
	REQUIRE(e.field->text.find("not javascript") != std::string::npos);
	REQUIRE_FALSE(e.overlay->requestedDelete);
}

TEST_CASE("Editor: closing it stops the log mirroring", "[MidiKit][Editor][Log]") {
	EditorRig h;
	MidiKitModule* m = h.m;
	MidiKitWidget* mw = h.mw;
	size_t before = mw->logs.size();
	OpenEditor e = openEditorOn(h, mw);
	REQUIRE(mw->logs.size() == before + 1);
	e.dialog->requestClose();
	REQUIRE(mw->logs.size() == before);
	// And the log display keeps working.
	m->log.pushText("still here");
	h.uiFrame();
	REQUIRE(std::get<2>(mw->buffer.front()) == "still here");
}

static size_t menuOverlayCount() {
	size_t n = 0;
	for (rack::widget::Widget* c : APP->scene->children) {
		if (dynamic_cast<rack::ui::MenuOverlay*>(c)) n++;
	}
	return n;
}

TEST_CASE("Editor: right and middle clicks do not close it", "[MidiKit][Editor]") {
	EditorRig h;
	MidiKitWidget* mw = h.mw;
	EditorMock mock;
	OpenEditor e = openEditorOn(h, mw);
	h.events().type("unapplied");

	// On the dialog's background (its header), and outside the dialog altogether.
	rack::math::Vec header = Test::EventDriver::pointIn(e.dialog, rack::math::Vec(e.dialog->box.size.x * 0.5f, 5.f));
	h.events().rightClick(header);
	h.events().click(header, GLFW_MOUSE_BUTTON_MIDDLE);
	h.events().rightClick(rack::math::Vec(2.f, 2.f));
	h.events().click(rack::math::Vec(2.f, 2.f), GLFW_MOUSE_BUTTON_MIDDLE);
	e.overlay->step();

	REQUIRE_FALSE(e.overlay->requestedDelete);
	REQUIRE(mock.ui.messages.empty());
	REQUIRE(e.field->text == "unapplied");
}

static rack::ui::Menu* newestMenu() {
	rack::ui::Menu* menu = nullptr;
	for (rack::widget::Widget* c : APP->scene->children) {
		if (auto* o = dynamic_cast<rack::ui::MenuOverlay*>(c)) {
			for (rack::widget::Widget* w : o->children) {
				if (auto* mm = dynamic_cast<rack::ui::Menu*>(w)) menu = mm;
			}
		}
	}
	return menu;
}

static rack::ui::MenuItem* menuItemNamed(rack::ui::Menu* menu, const std::string& text) {
	REQUIRE(menu != nullptr);
	for (rack::widget::Widget* w : menu->children) {
		if (auto* item = dynamic_cast<rack::ui::MenuItem*>(w)) {
			if (item->text == text) return item;
		}
	}
	FAIL("no menu item \"" << text << "\"");
	return nullptr;
}

static rack::ui::Menu* openSubmenu(rack::ui::Menu* menu, const std::string& text) {
	rack::ui::MenuItem* item = menuItemNamed(menu, text);
	rack::ui::Menu* child = item->createChildMenu();
	REQUIRE(child != nullptr);
	return child;
}

static void clickMenuItem(rack::ui::MenuItem* item) {
	rack::widget::Widget::ActionEvent a;
	rack::widget::EventContext c;
	a.context = &c;
	item->onAction(a);
}

TEST_CASE("Editor: the context menu lists the script API and a click inserts a snippet", "[MidiKit][Editor][Api]") {
	EditorRig h;
	MidiKitModule* m = h.m;
	MidiKitWidget* mw = h.mw;
	m->loadScript("// @engine QuickJs@v1\n");
	OpenEditor e = openEditorOn(h, mw);
	e.field->cursor = e.field->selection = (int)e.field->text.size();

	size_t menus = menuOverlayCount();
	h.events().rightClick(e.field);
	REQUIRE(menuOverlayCount() == menus + 1);
	rack::ui::Menu* menu = newestMenu();

	// Standard items, then one entry per API object.
	for (const char* name : {"Cut", "Copy", "Paste", "Select all", "rack.*", "number.*", "input.*", "trig.*", "param.*", "midi.*", "midiOut.*"}) {
		menuItemNamed(menu, name);
	}

	// A flat group: the function and its parameters are shown, a click inserts it.
	rack::ui::Menu* rackMenu = openSubmenu(menu, "rack.*");
	rack::ui::MenuItem* log = menuItemNamed(rackMenu, "log");
	REQUIRE(log->rightText == "(value)");
	clickMenuItem(log);
	REQUIRE(e.field->text == "// @engine QuickJs@v1\n// value: any value, further arguments are appended without separator\nrack.log(value);");
	// The inserted text is selected.
	REQUIRE(e.field->getSelectedText() == "// value: any value, further arguments are appended without separator\nrack.log(value);");
	REQUIRE(APP->event->selectedWidget == e.field);

	// A categorized group is one level deeper; Lua gets its own comment style and no semicolon.
	m->loadScript("--[[\n@engine minilua@v1\n--]]\n");
	e.field->setText("-- @engine minilua@v1\n");
	e.field->cursor = e.field->selection = (int)e.field->text.size();
	h.events().rightClick(e.field);
	rack::ui::Menu* midiMenu = openSubmenu(newestMenu(), "midi.*");
	rack::ui::Menu* setters = openSubmenu(midiMenu, "Setters");
	rack::ui::MenuItem* noteOn = menuItemNamed(setters, "setNoteOn");
	REQUIRE(noteOn->rightText == "(msg, ch, note, vel)");
	clickMenuItem(noteOn);
	REQUIRE(e.field->text.find("\n-- msg: message handle; ch: channel 1-16; note: note number 0-127; vel: velocity 0-127\nmidi.setNoteOn(msg, ch, note, vel)") != std::string::npos);

	// One undo step takes the insertion back.
	e.field->undo();
	REQUIRE(e.field->text == "-- @engine minilua@v1\n");
}

TEST_CASE("Editor: the script language is that of the running engine", "[MidiKit][Editor][Api]") {
	EditorRig h;
	MidiKitModule* m = h.m;
	MidiKitWidget* mw = h.mw;
	OpenEditor e = openEditorOn(h, mw);

	m->loadScript("/**\n * @engine QuickJs@v1\n */\n");
	REQUIRE(e.field->syntax().lineComment == "//");
	REQUIRE(e.field->syntax().statementEnd == ";");

	// An apply that switches the engine is picked up without reopening the editor.
	m->loadScript("--[[\n@engine minilua@v1\n--]]\n");
	REQUIRE(e.field->syntax().lineComment == "--");
	REQUIRE(e.field->syntax().statementEnd == "");
}

// Removes the most recently opened overlay, i.e. a context menu opened on top of the editor.
static void removeNewestOverlay() {
	rack::widget::Widget* menu = nullptr;
	for (rack::widget::Widget* c : APP->scene->children) {
		if (dynamic_cast<rack::ui::MenuOverlay*>(c)) menu = c;
	}
	REQUIRE(menu != nullptr);
	APP->event->finalizeWidget(menu);
	APP->scene->removeChild(menu);
	delete menu;
}

// Right-clicks, then removes the menu that opened, which would otherwise cover the next click.
static void rightClickClosingMenu(Test::Harness& h, rack::math::Vec pos) {
	size_t before = menuOverlayCount();
	REQUIRE(h.events().rightClick(pos));
	REQUIRE(menuOverlayCount() == before + 1);
	removeNewestOverlay();
}

TEST_CASE("Editor: a right click moves the caret there, unless it hits the selection", "[MidiKit][Editor][Api]") {
	EditorRig h;
	MidiKitWidget* mw = h.mw;
	OpenEditor e = openEditorOn(h, mw);
	e.field->setText("first\nsecond\nthird");
	e.field->cursor = e.field->selection = 0;

	// On the start of "second": the click point is computed from the field's own hit test.
	rack::math::Vec second = Test::EventDriver::pointIn(e.field, rack::math::Vec(
		ScriptEditField::kPadX + 2 * ScriptEditField::charWidth(),
		ScriptEditField::kPadY + 1.5f * ScriptEditField::kLineHeight));
	rightClickClosingMenu(h, second);
	REQUIRE(e.field->cursor == e.field->selection);
	REQUIRE(e.field->cursor == 6 + 2);

	// Inside a selection the selection survives.
	e.field->selection = 6;
	e.field->cursor = 12;
	rightClickClosingMenu(h, second);
	REQUIRE(e.field->selection == 6);
	REQUIRE(e.field->cursor == 12);

	// Outside it, the selection collapses to the click.
	e.field->selection = 0;
	e.field->cursor = 3;
	rightClickClosingMenu(h, second);
	REQUIRE(e.field->cursor == e.field->selection);
	REQUIRE(e.field->cursor == 8);
}

TEST_CASE("Editor: header templates are inserted at the top and the engine accepts them", "[MidiKit][Editor][Api]") {
	EditorRig h;
	MidiKitWidget* mw = h.mw;
	OpenEditor e = openEditorOn(h, mw);

	for (const char* name : {"Insert QuickJs header", "Insert Lua header"}) {
		const bool js = std::string(name).find("QuickJs") != std::string::npos;
		e.field->setText("log(1);\n");
		e.field->clearHistory();
		// Wherever the caret is, the header goes in front.
		e.field->cursor = e.field->selection = (int)e.field->text.size();
		h.events().rightClick(e.field);
		rack::ui::MenuItem* item = menuItemNamed(newestMenu(), name);
		clickMenuItem(item);

		const std::string text = e.field->text;
		REQUIRE(text.size() > 8);
		REQUIRE(text.substr(text.size() - 8) == "log(1);\n");
		REQUIRE(text.find(js ? "@engine QuickJs@v1" : "@engine minilua@v1") != std::string::npos);
		// The header is selected, and one undo step removes it.
		REQUIRE(e.field->getSelectedText() == text.substr(0, text.size() - 8));
		e.field->undo();
		REQUIRE(e.field->text == "log(1);\n");

		// The loader accepts what was inserted.
		Kit<> tk;
		MidiKitModule* tm = tk.m;
		tm->loadScript(text);
		REQUIRE((js ? tm->host.isQuickJsEngine() : tm->host.isLuaEngine()));
		REQUIRE(drainLog(tm).find("not compatible") == std::string::npos);

		// Close the menu so the next round's click reaches the field.
		removeNewestOverlay();
	}
}

TEST_CASE("Editor: the API table is complete and matches what both engines expose", "[MidiKit][Editor][Api]") {
	std::vector<StoermelderPackOne::ui::editor::scripttext::ApiGroup> api = StoermelderPackOne::MidiScript::apiReference();
	REQUIRE_FALSE(api.empty());

	std::string js = "/**\n * @engine QuickJs@v1\n */\n";
	std::string lua = "--[[\n@engine minilua@v1\n--]]\n";
	for (const auto& g : api) {
		for (const auto& f : g.functions) {
			std::string path = g.name + "." + f.name;
			js += "if (typeof " + path + " !== 'function') throw new Error('missing " + path + "');\n";
			lua += "if type(" + path + ") ~= 'function' then error('missing " + path + "') end\n";
			// A ';' in a description would split the one-line comment in the wrong place.
			for (const auto& p : f.params) {
				REQUIRE(p.description.find(';') == std::string::npos);
				REQUIRE_FALSE(p.description.empty());
			}
		}
	}
	SECTION("QuickJs") {
		Kit<> kit;
		MidiKitModule* m = kit.m;
		m->loadScript(js);
		std::string log = drainLog(m);
		CATCH_INFO(log);
		REQUIRE(m->host.seQuickJs.ctx != nullptr);
		REQUIRE(log.find("missing") == std::string::npos);
	}
	SECTION("Lua") {
		Kit<> kit;
		MidiKitModule* m = kit.m;
		m->loadScript(lua);
		std::string log = drainLog(m);
		CATCH_INFO(log);
		REQUIRE(m->host.seLua.L != nullptr);
		REQUIRE(log.find("missing") == std::string::npos);
	}
}

TEST_CASE("Editor: the log area's context menu copies and clears", "[MidiKit][Editor][Log]") {
	EditorRig h;
	MidiKitModule* m = h.m;
	MidiKitWidget* mw = h.mw;
	EditorMock mock;
	m->log.pushText("one");
	m->log.pushText("two\nthree");
	h.uiFrame();
	OpenEditor e = openEditorOn(h, mw);
	e.overlay->step();
	ScriptLogView* view = e.dialog->logPanel->view;
	REQUIRE(view->lines.size() == 4);   // "No script" + one + two + three

	// A right-click on the area opens a menu and leaves the editor open.
	size_t menus = menuOverlayCount();
	h.events().rightClick(view);
	REQUIRE(menuOverlayCount() == menus + 1);
	REQUIRE_FALSE(e.overlay->requestedDelete);

	view->copyToClipboard();
	REQUIRE(mock.ui.clipboard == "No script\none\ntwo\nthree");

	view->clearAction();
	REQUIRE(view->lines.empty());
	// The panel's own log display is not touched.
	REQUIRE(mw->buffer.size() == 3);   // entries, the multi-line one counts once

	// New output keeps arriving after a clear.
	m->log.pushText("later");
	h.uiFrame();
	REQUIRE(view->lines.size() == 1);
	REQUIRE(view->lines[0] == "later");
}

TEST_CASE("Editor: dragging the handle trades space between code and log, the dialog keeps its size", "[MidiKit][Editor]") {
	EditorRig h;
	MidiKitWidget* mw = h.mw;
	OpenEditor e = openEditorOn(h, mw);
	auto* handle = h.events().find<SplitHandle>(e.dialog);

	const rack::math::Vec size = e.dialog->box.size;
	const float logBefore = e.dialog->logHeight;
	const float codeBefore = e.dialog->scroll->box.size.y;

	// Up: the log grows by what the code area loses.
	h.events().dragBy(handle, rack::math::Vec(0.f, -40.f), 4);
	e.overlay->step();
	REQUIRE(e.dialog->logHeight == Catch::Approx(logBefore + 40.f));
	REQUIRE(e.dialog->scroll->box.size.y == Catch::Approx(codeBefore - 40.f));
	REQUIRE(e.dialog->logPanel->scroll->box.size.y == Catch::Approx(e.dialog->logHeight - ScriptLogPanel::kFilterBarHeight - 10.f));
	REQUIRE(e.dialog->box.size.x == Catch::Approx(size.x));
	REQUIRE(e.dialog->box.size.y == Catch::Approx(size.y));

	// Down again.
	h.events().dragBy(handle, rack::math::Vec(0.f, 40.f), 4);
	REQUIRE(e.dialog->logHeight == Catch::Approx(logBefore));

	// Both areas stay usable at the extremes.
	h.events().dragBy(handle, rack::math::Vec(0.f, -5000.f), 2);
	e.overlay->step();
	REQUIRE(e.dialog->scroll->box.size.y >= ScriptEditorDialog::kMinCodeHeight - 1.f);
	h.events().dragBy(handle, rack::math::Vec(0.f, 5000.f), 2);
	REQUIRE(e.dialog->logHeight >= ScriptEditorDialog::kMinLogHeight - 0.5f);

	// Dragging never closed or dirtied the editor.
	REQUIRE_FALSE(e.overlay->requestedDelete);
	REQUIRE_FALSE(e.dialog->dirty);
}

// ── Find ──

namespace {

static void selectedRange(ScriptEditField* f, int& b, int& e) {
	b = std::min(f->cursor, f->selection);
	e = std::max(f->cursor, f->selection);
}

}

TEST_CASE("Editor find: Ctrl+F opens the bar with focus in it, the dialog keeps its size", "[MidiKit][Editor][Find]") {
	EditorRig h;
	MidiKitWidget* mw = h.mw;
	OpenEditor e = openEditorOn(h, mw);
	REQUIRE_FALSE(e.dialog->findBar->isOpen);
	REQUIRE_FALSE(e.dialog->findBar->visible);
	const float codeBefore = e.dialog->scroll->box.size.y;
	const rack::math::Vec size = e.dialog->box.size;
	const bool dirtyBefore = e.dialog->dirty;

	REQUIRE(h.events().keyPress(GLFW_KEY_F, RACK_MOD_CTRL));
	e.overlay->step();
	REQUIRE(e.dialog->findBar->isOpen);
	REQUIRE(e.dialog->findBar->visible);
	REQUIRE(APP->event->selectedWidget == e.dialog->findBar->input);
	REQUIRE(e.dialog->scroll->box.size.y == Catch::Approx(codeBefore - ScriptFindBar::kHeight));
	REQUIRE(e.dialog->box.size.x == Catch::Approx(size.x));
	REQUIRE(e.dialog->box.size.y == Catch::Approx(size.y));
	// Searching does not touch the buffer.
	REQUIRE(e.dialog->dirty == dirtyBefore);
}

TEST_CASE("Editor find: typing searches incrementally, Enter / Shift+Enter step and wrap", "[MidiKit][Editor][Find]") {
	EditorRig h;
	MidiKitWidget* mw = h.mw;
	OpenEditor e = openEditorOn(h, mw);
	e.field->setText("foo bar\nFoo baz\nqux foo");   // matches (ignoring case) at 0, 8, 20
	e.field->cursor = e.field->selection = 0;
	e.field->clearHistory();

	h.events().keyPress(GLFW_KEY_F, RACK_MOD_CTRL);
	h.events().type("foo");
	int b, en;
	selectedRange(e.field, b, en);
	REQUIRE(b == 0);
	REQUIRE(en == 3);
	REQUIRE(e.dialog->findBar->count->text == "1 of 3");

	h.events().keyPress(GLFW_KEY_ENTER);
	selectedRange(e.field, b, en);
	REQUIRE(b == 8);
	REQUIRE(e.dialog->findBar->count->text == "2 of 3");
	h.events().keyPress(GLFW_KEY_ENTER);
	h.events().keyPress(GLFW_KEY_ENTER);                // wraps to the first
	selectedRange(e.field, b, en);
	REQUIRE(b == 0);
	REQUIRE(e.dialog->findBar->count->text == "1 of 3");

	h.events().keyPress(GLFW_KEY_ENTER, GLFW_MOD_SHIFT);  // wraps back to the last
	selectedRange(e.field, b, en);
	REQUIRE(b == 20);
	h.events().keyPress(GLFW_KEY_ENTER, GLFW_MOD_SHIFT);
	selectedRange(e.field, b, en);
	REQUIRE(b == 8);

	// The buffer is untouched, focus stayed in the bar, and the editor is clean.
	REQUIRE(e.field->text == "foo bar\nFoo baz\nqux foo");
	REQUIRE(APP->event->selectedWidget == e.dialog->findBar->input);

	// Typing on narrows the search: "foo " (with the space) is at 0 and 8 only.
	h.events().type(" ");
	REQUIRE(e.dialog->findBar->count->text.find("of 2") != std::string::npos);
}

TEST_CASE("Editor find: match case, no results, empty search", "[MidiKit][Editor][Find]") {
	EditorRig h;
	MidiKitWidget* mw = h.mw;
	OpenEditor e = openEditorOn(h, mw);
	e.field->setText("Foo foo FOO");
	e.field->cursor = e.field->selection = 0;

	h.events().keyPress(GLFW_KEY_F, RACK_MOD_CTRL);
	h.events().type("foo");
	REQUIRE(e.dialog->findBar->count->text == "1 of 3");

	e.dialog->findBar->caseButton->action();
	REQUIRE(e.dialog->findBar->caseButton->on);
	REQUIRE(e.dialog->findBar->count->text == "1 of 1");
	int b, en;
	selectedRange(e.field, b, en);
	REQUIRE(b == 4);

	h.events().type("x");
	REQUIRE(e.dialog->findBar->count->text == "No results");
	h.events().keyPress(GLFW_KEY_ENTER);                  // nothing to step to, nothing breaks
	REQUIRE(e.dialog->findBar->count->text == "No results");

	// Emptying the search clears the count and the highlight.
	for (int i = 0; i < 4; i++) h.events().keyPress(GLFW_KEY_BACKSPACE);
	REQUIRE(e.dialog->findBar->count->text == "");
	REQUIRE(e.field->findNeedle.empty());
}

TEST_CASE("Editor find: Esc closes the bar and returns to the editor without closing it", "[MidiKit][Editor][Find]") {
	EditorRig h;
	MidiKitWidget* mw = h.mw;
	EditorMock mock;
	OpenEditor e = openEditorOn(h, mw);
	e.field->setText("abc abc");
	e.field->cursor = e.field->selection = 0;
	const float codeClosed = e.dialog->scroll->box.size.y;

	h.events().keyPress(GLFW_KEY_F, RACK_MOD_CTRL);
	h.events().type("abc");
	e.overlay->step();
	REQUIRE(e.dialog->scroll->box.size.y < codeClosed);

	h.events().keyPress(GLFW_KEY_ESCAPE);
	e.overlay->step();
	REQUIRE_FALSE(e.dialog->findBar->isOpen);
	REQUIRE_FALSE(e.dialog->findBar->visible);
	REQUIRE(e.field->findNeedle.empty());
	REQUIRE(e.dialog->scroll->box.size.y == Catch::Approx(codeClosed));
	REQUIRE(APP->event->selectedWidget == e.field);
	REQUIRE_FALSE(e.overlay->requestedDelete);
	REQUIRE(mock.ui.messages.empty());

	// Esc in the editor itself still closes it (the buffer is dirty from setText: say yes).
	mock.ui.answer = true;
	h.events().keyPress(GLFW_KEY_ESCAPE);
	REQUIRE(e.overlay->requestedDelete);
}

TEST_CASE("Editor find: F3 steps from the editor, a one-line selection becomes the search", "[MidiKit][Editor][Find]") {
	EditorRig h;
	MidiKitWidget* mw = h.mw;
	OpenEditor e = openEditorOn(h, mw);
	e.field->setText("one two one two one");
	e.field->selection = 4;
	e.field->cursor = 7;                                   // "two"

	h.events().keyPress(GLFW_KEY_F, RACK_MOD_CTRL);
	REQUIRE(e.dialog->findBar->input->text == "two");
	REQUIRE(e.dialog->findBar->count->text == "1 of 2");

	// With the bar closed again, F3 from the editor steps through the last search
	// and brings the bar back; focus stays in the editor.
	h.events().keyPress(GLFW_KEY_ESCAPE);
	REQUIRE_FALSE(e.dialog->findBar->isOpen);
	e.field->selection = e.field->cursor = 0;
	REQUIRE(h.events().keyPress(GLFW_KEY_F3));
	int b, en;
	selectedRange(e.field, b, en);
	REQUIRE(b == 4);                                       // "two"
	REQUIRE(e.dialog->findBar->isOpen);
	REQUIRE(APP->event->selectedWidget == e.field);
	REQUIRE(h.events().keyPress(GLFW_KEY_F3));
	selectedRange(e.field, b, en);
	REQUIRE(b == 12);
	REQUIRE(h.events().keyPress(GLFW_KEY_F3, GLFW_MOD_SHIFT));
	selectedRange(e.field, b, en);
	REQUIRE(b == 4);
}

TEST_CASE("Editor find: a match far down is scrolled into view; editing updates the matches", "[MidiKit][Editor][Find]") {
	EditorRig h;
	MidiKitWidget* mw = h.mw;
	OpenEditor e = openEditorOn(h, mw);
	std::string text;
	for (int i = 0; i < 150; i++) text += "line " + std::to_string(i) + "\n";
	text += "needle here\n";
	e.field->setText(text);
	e.field->cursor = e.field->selection = 0;
	e.overlay->step();
	REQUIRE(e.dialog->scroll->offset.y == Catch::Approx(0.f));

	h.events().keyPress(GLFW_KEY_F, RACK_MOD_CTRL);
	h.events().type("needle");
	e.overlay->step();
	e.overlay->step();
	REQUIRE(e.dialog->scroll->offset.y > 0.f);

	// An edit in the buffer is picked up by the open search.
	e.field->selection = e.field->cursor = (int)text.size();
	e.field->insertText("needle again");
	REQUIRE(e.field->getMatches().size() == 2);
	e.overlay->step();
	REQUIRE(e.dialog->findBar->count->text.find("2") != std::string::npos);
}

// ── Log filter ──

namespace {

// The lines the log view currently shows, in order.
static std::vector<std::string> shownLines(ScriptLogView* v) {
	std::vector<std::string> r;
	for (int i : v->getShown()) r.push_back(v->lines[i]);
	return r;
}

}

TEST_CASE("Editor log filter: plain text narrows the shown lines, the rest is kept", "[MidiKit][Editor][Log][Filter]") {
	EditorRig h;
	MidiKitModule* m = h.m;
	MidiKitWidget* mw = h.mw;
	EditorMock mock;
	m->log.pushText("note on 60");
	m->log.pushText("Error: boom");
	m->log.pushText("note off 60");
	h.uiFrame();
	OpenEditor e = openEditorOn(h, mw);
	auto* view = e.dialog->logPanel->view;
	REQUIRE(view->lines.size() == 4);              // "No script" + three

	e.dialog->logPanel->filterInput->setText("note");
	REQUIRE(shownLines(view) == std::vector<std::string>({"note on 60", "note off 60"}));
	REQUIRE(e.dialog->logPanel->filterCount->text == "2 / 4");
	// Case-insensitive by default.
	e.dialog->logPanel->filterInput->setText("ERROR");
	REQUIRE(shownLines(view) == std::vector<std::string>({"Error: boom"}));
	e.dialog->logPanel->filterCaseButton->action();
	REQUIRE(shownLines(view).empty());
	REQUIRE(e.dialog->logPanel->filterCount->text == "0 / 4");
	e.dialog->logPanel->filterCaseButton->action();

	// Copy takes what is shown.
	view->copyToClipboard();
	REQUIRE(mock.ui.clipboard == "Error: boom");

	// New lines are filtered as they arrive.
	m->log.pushText("another error");
	m->log.pushText("note on 61");
	h.uiFrame();
	REQUIRE(shownLines(view) == std::vector<std::string>({"Error: boom", "another error"}));
	REQUIRE(view->lines.size() == 6);

	// Emptying the filter shows everything again.
	e.dialog->logPanel->filterInput->setText("");
	REQUIRE(view->getShown().size() == 6);
	REQUIRE(e.dialog->logPanel->filterCount->text == "");
}

TEST_CASE("Editor log filter: regular expressions, and an invalid one keeps every line", "[MidiKit][Editor][Log][Filter]") {
	EditorRig h;
	MidiKitModule* m = h.m;
	MidiKitWidget* mw = h.mw;
	m->log.pushText("note on 60");
	m->log.pushText("cc 7 100");
	m->log.pushText("note off 62");
	h.uiFrame();
	OpenEditor e = openEditorOn(h, mw);
	auto* view = e.dialog->logPanel->view;

	e.dialog->logPanel->filterRegexButton->action();
	REQUIRE(e.dialog->logPanel->filterRegexButton->on);
	e.dialog->logPanel->filterInput->setText("^note (on|off) 6[02]$");
	REQUIRE(shownLines(view) == std::vector<std::string>({"note on 60", "note off 62"}));
	e.dialog->logPanel->filterInput->setText("\\d{3}");
	REQUIRE(shownLines(view) == std::vector<std::string>({"cc 7 100"}));

	// Half-typed pattern: flagged, nothing disappears.
	e.dialog->logPanel->filterInput->setText("(note");
	REQUIRE(e.dialog->logPanel->filterCount->text == "Invalid regex");
	REQUIRE(view->getShown().size() == view->lines.size());

	// The same text is fine as plain text.
	e.dialog->logPanel->filterRegexButton->action();
	REQUIRE(e.dialog->logPanel->filterCount->text == "0 / 4");
}

TEST_CASE("Editor log filter: typing goes to the filter, Enter / Esc return to the editor, clear keeps the filter", "[MidiKit][Editor][Log][Filter]") {
	EditorRig h;
	MidiKitModule* m = h.m;
	MidiKitWidget* mw = h.mw;
	EditorMock mock;
	m->log.pushText("keep me");
	h.uiFrame();
	OpenEditor e = openEditorOn(h, mw);

	h.events().select(e.dialog->logPanel->filterInput);
	h.events().type("keep");
	REQUIRE(e.dialog->logPanel->filterInput->text == "keep");
	REQUIRE(e.field->text == m->host.script);            // nothing leaked into the buffer
	REQUIRE(shownLines(e.dialog->logPanel->view) == std::vector<std::string>({"keep me"}));

	h.events().keyPress(GLFW_KEY_ENTER);
	REQUIRE(APP->event->selectedWidget == e.field);
	h.events().select(e.dialog->logPanel->filterInput);
	h.events().keyPress(GLFW_KEY_ESCAPE);
	REQUIRE(APP->event->selectedWidget == e.field);
	REQUIRE_FALSE(e.overlay->requestedDelete);             // Esc here does not close the editor
	REQUIRE(mock.ui.messages.empty());

	// Clearing the log empties the lines but the filter stays for what comes next.
	e.dialog->logPanel->view->clearAction();
	m->log.pushText("keep this too");
	m->log.pushText("drop this");
	h.uiFrame();
	REQUIRE(shownLines(e.dialog->logPanel->view) == std::vector<std::string>({"keep this too"}));
	REQUIRE(e.dialog->logPanel->filterInput->text == "keep");
}

TEST_CASE("Editor log panel: the filter row sits below the log, both inside one panel", "[MidiKit][Editor][Log][Filter]") {
	EditorRig h;
	MidiKitWidget* mw = h.mw;
	OpenEditor e = openEditorOn(h, mw);
	ScriptLogPanel* panel = e.dialog->logPanel;

	REQUIRE(panel->scroll->parent == panel);
	REQUIRE(panel->filterBar->parent == panel);
	REQUIRE(panel->filterBar->box.pos.y >= panel->scroll->box.pos.y + panel->scroll->box.size.y);
	REQUIRE(panel->filterBar->box.pos.y + panel->filterBar->box.size.y <= panel->box.size.y + 0.5f);
	REQUIRE(panel->scroll->box.size.y > 0.f);

	// The panel sits under the footer and fills the dialog's bottom edge (less the margin).
	REQUIRE(panel->box.pos.y + panel->box.size.y == Catch::Approx(e.dialog->box.size.y - ScriptEditorDialog::kMargin));
}

TEST_CASE("Editor log panel: the scrollbar is shown even when everything fits", "[MidiKit][Editor][Log]") {
	EditorRig h;
	MidiKitModule* m = h.m;
	MidiKitWidget* mw = h.mw;
	OpenEditor e = openEditorOn(h, mw);
	ScriptLogPanel* panel = e.dialog->logPanel;
	e.overlay->step();

	// A line or two: nothing to scroll, the bar is there all the same.
	REQUIRE(panel->scroll->getContainerOffsetBound().size.y <= 0.f);
	REQUIRE(panel->scroll->verticalScrollbar->isVisible());

	// With enough lines to scroll, it is still shown, and now has a range.
	for (int i = 0; i < 100; i++) m->log.pushText("line " + std::to_string(i));
	h.uiFrame();
	e.overlay->step();
	e.overlay->step();
	REQUIRE(panel->scroll->getContainerOffsetBound().size.y > 0.f);
	REQUIRE(panel->scroll->verticalScrollbar->isVisible());

	// Clicking the bar with nothing to scroll does not disturb the scroll position.
	e.dialog->logPanel->clear();
	e.overlay->step();
	e.overlay->step();
	REQUIRE(panel->scroll->getContainerOffsetBound().size.y <= 0.f);
	h.events().click(panel->scroll->verticalScrollbar);
	e.overlay->step();
	REQUIRE(panel->scroll->offset.y == Catch::Approx(0.f));
	REQUIRE(panel->scroll->verticalScrollbar->isVisible());
}

TEST_CASE("Editor: the code area's scrollbar is shown even for a short script", "[MidiKit][Editor]") {
	EditorRig h;
	MidiKitWidget* mw = h.mw;
	OpenEditor e = openEditorOn(h, mw);
	e.field->setText("short\n");
	e.overlay->step();
	e.overlay->step();

	REQUIRE(e.dialog->scroll->getContainerOffsetBound().size.y <= 0.f);
	REQUIRE(e.dialog->scroll->verticalScrollbar->isVisible());

	// Clicking a line still places the caret: the bar does not cover the text.
	rack::math::Vec local(e.field->colToX(2), ScriptEditField::kPadY + 0.5f * ScriptEditField::kLineHeight);
	h.events().click(Test::EventDriver::pointIn(e.field, local));
	REQUIRE(e.field->cursor == 2);

	// And a long one scrolls, with the bar still there.
	std::string text;
	for (int i = 0; i < 200; i++) text += "line\n";
	e.field->setText(text);
	e.overlay->step();
	e.overlay->step();
	REQUIRE(e.dialog->scroll->getContainerOffsetBound().size.y > 0.f);
	REQUIRE(e.dialog->scroll->verticalScrollbar->isVisible());
}

// ── Line operations ──

TEST_CASE("Editor field: line operations by key, each one undoable", "[MidiKit][Editor][Lines]") {
	EditorRig h;
	MidiKitWidget* mw = h.mw;
	OpenEditor e = openEditorOn(h, mw);
	const int CTRL = RACK_MOD_CTRL;

	e.field->setText("one\ntwo\nthree");
	e.field->clearHistory();
	e.field->cursor = e.field->selection = 5;               // in "two"

	SECTION("Ctrl+D duplicates the line and moves to the copy") {
		REQUIRE(h.events().keyPress(GLFW_KEY_D, CTRL));
		REQUIRE(e.field->text == "one\ntwo\ntwo\nthree");
		REQUIRE(e.field->cursor == 9);
		h.events().keyPress(GLFW_KEY_D, CTRL);               // stacks downwards
		REQUIRE(e.field->text == "one\ntwo\ntwo\ntwo\nthree");
		h.events().keyPress(GLFW_KEY_Z, CTRL);
		h.events().keyPress(GLFW_KEY_Z, CTRL);
		REQUIRE(e.field->text == "one\ntwo\nthree");
		REQUIRE(e.field->cursor == 5);
	}
	SECTION("Alt+Up / Alt+Down move the line") {
		REQUIRE(h.events().keyPress(GLFW_KEY_DOWN, GLFW_MOD_ALT));
		REQUIRE(e.field->text == "one\nthree\ntwo");
		REQUIRE(e.field->cursor == 11);
		h.events().keyPress(GLFW_KEY_DOWN, GLFW_MOD_ALT);   // already last: nothing happens
		REQUIRE(e.field->text == "one\nthree\ntwo");
		h.events().keyPress(GLFW_KEY_UP, GLFW_MOD_ALT);
		h.events().keyPress(GLFW_KEY_UP, GLFW_MOD_ALT);
		REQUIRE(e.field->text == "two\none\nthree");
		h.events().keyPress(GLFW_KEY_Z, CTRL);
		REQUIRE(e.field->text == "one\ntwo\nthree");
	}
	SECTION("Ctrl+Shift+K deletes the line") {
		REQUIRE(h.events().keyPress(GLFW_KEY_K, CTRL | GLFW_MOD_SHIFT));
		REQUIRE(e.field->text == "one\nthree");
		REQUIRE(e.field->cursor == 4);
		h.events().keyPress(GLFW_KEY_Z, CTRL);
		REQUIRE(e.field->text == "one\ntwo\nthree");
	}
	SECTION("a selection of several lines is handled as a block") {
		e.field->selection = 0;
		e.field->cursor = 6;                                // "one" and "two"
		h.events().keyPress(GLFW_KEY_DOWN, GLFW_MOD_ALT);
		REQUIRE(e.field->text == "three\none\ntwo");
		h.events().keyPress(GLFW_KEY_K, CTRL | GLFW_MOD_SHIFT);
		REQUIRE(e.field->text == "three");
	}
	REQUIRE(e.dialog->dirty);
	REQUIRE(APP->event->selectedWidget == e.field);
}

TEST_CASE("Editor field: Ctrl+/ toggles comments in the running script's language", "[MidiKit][Editor][Lines]") {
	EditorRig h;
	MidiKitModule* m = h.m;
	MidiKitWidget* mw = h.mw;
	OpenEditor e = openEditorOn(h, mw);
	const int CTRL = RACK_MOD_CTRL;

	SECTION("JavaScript: //") {
		m->loadScript("/**\n * @engine QuickJs@v1\n */\n");
		e.field->setText("/**\n * @engine QuickJs@v1\n */\nlog(1);\n");
		e.field->clearHistory();
		e.field->cursor = e.field->selection = 31;           // in "log(1);"
		REQUIRE(h.events().keyPress(GLFW_KEY_SLASH, CTRL));
		REQUIRE(e.field->text.find("// log(1);") != std::string::npos);
		h.events().keyPress(GLFW_KEY_SLASH, CTRL);
		REQUIRE(e.field->text == "/**\n * @engine QuickJs@v1\n */\nlog(1);\n");
	}
	SECTION("Lua: --, and the same on a layout where / is Shift+7") {
		m->loadScript("--[[\n@engine minilua@v1\n--]]\n");
		e.field->setText("--[[\n@engine minilua@v1\n--]]\nlog(1)\n");
		e.field->clearHistory();
		e.field->cursor = e.field->selection = 30;           // in "log(1)"
		REQUIRE(h.events().keyPress(GLFW_KEY_7, CTRL | GLFW_MOD_SHIFT));
		REQUIRE(e.field->text.find("-- log(1)") != std::string::npos);
		h.events().keyPress(GLFW_KEY_Z, CTRL);
		REQUIRE(e.field->text == "--[[\n@engine minilua@v1\n--]]\nlog(1)\n");
	}
	SECTION("the language follows the engine after an apply") {
		m->loadScript("--[[\n@engine minilua@v1\n--]]\n");
		e.field->setText("x\n");
		e.field->cursor = e.field->selection = 0;
		h.events().keyPress(GLFW_KEY_SLASH, CTRL);
		REQUIRE(e.field->text == "-- x\n");
		m->loadScript("/**\n * @engine QuickJs@v1\n */\n");
		e.field->setText("x\n");
		e.field->cursor = e.field->selection = 0;
		h.events().keyPress(GLFW_KEY_SLASH, CTRL);
		REQUIRE(e.field->text == "// x\n");
	}
}

// ── Double / triple click ──

TEST_CASE("Editor field: double-click selects a word, triple-click the line, a fourth starts over", "[MidiKit][Editor][Click]") {
	EditorRig h;
	MidiKitWidget* mw = h.mw;
	OpenEditor e = openEditorOn(h, mw);
	e.field->setText("first line\nsecond word here\nthird");
	e.field->cursor = e.field->selection = 0;
	e.field->clearHistory();

	// Between "s" and "e" of "second", the field's own cell geometry.
	rack::math::Vec at = Test::EventDriver::pointIn(e.field, rack::math::Vec(e.field->colToX(2), ScriptEditField::kPadY + 1.5f * ScriptEditField::kLineHeight));
	auto selected = [&]() { return e.field->getSelectedText(); };

	h.events().click(at);
	REQUIRE(e.field->cursor == 13);
	REQUIRE(selected() == "");

	h.events().click(at);
	REQUIRE(selected() == "second");
	REQUIRE(e.field->selection == 11);
	REQUIRE(e.field->cursor == 17);

	h.events().click(at);
	REQUIRE(selected() == "second word here\n");

	h.events().click(at);                      // starts over: a plain caret again
	REQUIRE(selected() == "");
	REQUIRE(e.field->cursor == 13);

	// Selecting does not touch the buffer, or the undo history.
	REQUIRE(e.field->text == "first line\nsecond word here\nthird");
	REQUIRE(e.field->undoStack.empty());
}

TEST_CASE("Editor field: clicks far apart do not count as a double-click", "[MidiKit][Editor][Click]") {
	EditorRig h;
	MidiKitWidget* mw = h.mw;
	OpenEditor e = openEditorOn(h, mw);
	e.field->setText("alpha beta gamma");
	e.field->cursor = e.field->selection = 0;

	auto at = [&](int col) { return Test::EventDriver::pointIn(e.field, rack::math::Vec(e.field->colToX(col), ScriptEditField::kPadY + 0.5f * ScriptEditField::kLineHeight)); };
	h.events().click(at(2));
	h.events().click(at(12));                  // another word, many pixels away
	REQUIRE(e.field->getSelectedText() == "");
	REQUIRE(e.field->cursor == 12);
}

TEST_CASE("Editor field: double-click then typing replaces the word", "[MidiKit][Editor][Click]") {
	EditorRig h;
	MidiKitWidget* mw = h.mw;
	OpenEditor e = openEditorOn(h, mw);
	e.field->setText("alpha beta gamma");
	e.field->cursor = e.field->selection = 0;

	rack::math::Vec on = Test::EventDriver::pointIn(e.field, rack::math::Vec(e.field->colToX(8), ScriptEditField::kPadY + 0.5f * ScriptEditField::kLineHeight));
	h.events().doubleClick(on);
	REQUIRE(e.field->getSelectedText() == "beta");
	h.events().type("X");
	REQUIRE(e.field->text == "alpha X gamma");
}

TEST_CASE("Editor field: the mouse moving while a double / triple click is held keeps the word / line selected", "[MidiKit][Editor][Click]") {
	EditorRig h;
	MidiKitWidget* mw = h.mw;
	OpenEditor e = openEditorOn(h, mw);
	e.field->setText("first line\nsecond word here\nthird");
	e.field->cursor = e.field->selection = 0;
	auto pos = [&](int col, int line) { return Test::EventDriver::pointIn(e.field, rack::math::Vec(e.field->colToX(col) + 1.f, ScriptEditField::kPadY + (line + 0.5f) * ScriptEditField::kLineHeight)); };

	// What Rack does after the press: hover events while the button is down. The second
	// click lands in the middle of "second", so the pointer is not at the word's end.
	h.events().click(pos(3, 1));
	h.events().button(pos(3, 1), GLFW_MOUSE_BUTTON_LEFT, GLFW_PRESS);
	h.events().hover(pos(3, 1));
	h.events().hover(pos(4, 1));
	REQUIRE(e.field->getSelectedText() == "second");
	REQUIRE(e.field->selection == 11);
	REQUIRE(e.field->cursor == 17);

	// Dragging on extends by whole words, from the original one.
	h.events().hover(pos(13, 1));                          // inside "here"
	REQUIRE(e.field->getSelectedText() == "second word here");
	h.events().hover(pos(0, 0));                           // back over "first"
	REQUIRE(e.field->getSelectedText() == "first line\nsecond");
	h.events().button(pos(0, 0), GLFW_MOUSE_BUTTON_LEFT, GLFW_RELEASE);

	// Triple-click: the line, and dragging extends by whole lines. (Start counting afresh,
	// or the clicks above would carry over into this sequence.)
	e.field->clickCount = 0;
	e.field->lastClickTime = -1e9;
	h.events().click(pos(3, 1));
	h.events().click(pos(3, 1));
	h.events().button(pos(3, 1), GLFW_MOUSE_BUTTON_LEFT, GLFW_PRESS);
	h.events().hover(pos(5, 1));
	REQUIRE(e.field->getSelectedText() == "second word here\n");
	h.events().hover(pos(2, 2));
	REQUIRE(e.field->getSelectedText() == "second word here\nthird");
	h.events().button(pos(2, 2), GLFW_MOUSE_BUTTON_LEFT, GLFW_RELEASE);

	// A plain press-and-drag still selects by character.
	e.field->clickCount = 0;
	e.field->lastClickTime = -1e9;
	h.events().button(pos(2, 0), GLFW_MOUSE_BUTTON_LEFT, GLFW_PRESS);
	h.events().hover(pos(5, 0));
	REQUIRE(e.field->getSelectedText() == "rst");
	h.events().button(pos(5, 0), GLFW_MOUSE_BUTTON_LEFT, GLFW_RELEASE);
}
