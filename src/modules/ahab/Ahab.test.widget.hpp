#pragma once
#include "Ahab.test.hpp"
#include "Ahab.test.module.hpp"   // MockUiAccess (dialog/clipboard recording), Mock
#include "../../test/test_harness.hpp"

// Widget tests: keyboard dispatch through AhabSimWidget::onSelectKey/onHoverKey — the plugin's
// first coverage of this dispatch path (previously every shortcut's underlying method was
// tested directly, e.g. simClear()/simLoad(), never through a synthesised key event). Mirrors
// Signls.test.widget.hpp's pattern: Test::Harness + Test::EventDriver, one TEST_CASE per
// shortcut/behavior.
//
// The letter-based shortcuts (Ctrl+A/N/O/B/S/Shift+S/Z/Shift+Z/I/C/X/V) route through
// vcv::ui::getKeyName() (Ahab.cpp's onSelectKey), not GLFW_KEY_* comparison, so they need
// Test::mock::MockUiAccess installed (lowercase, real-keyboard-accurate keyName) - the plain
// vcv::UiAccess/RealUiAccess default answers "" headless (glfwGetKeyName() needs glfwInit(),
// never called in a test binary) prior to that seam existing, and even with the seam the base
// mock answers uppercase ("A", not "a") which these comparisons don't match.

namespace {

// Ahab.test.module.hpp's MockUiAccess (records openDialog/saveDialog calls and scripts their
// results, tracks clipboard writes/reads) extended with a real-keyboard-accurate getKeyName():
// the base vcv::UiAccess/RealUiAccess default routes to glfwGetKeyName(), which returns "" in
// every test binary (GLFW is never initialised there), so the letter-based shortcuts
// (Ctrl+A/N/O/B/S/Shift+S/Z/Shift+Z/I/C/X/V — Ahab.cpp's onSelectKey compares against
// vcv::ui::getKeyName()) would never match without this override. Mirrors
// Test::mock::MockUiAccess's own override (lowercase ASCII, matching an unshifted real
// keypress) while keeping Ahab's own dialog/clipboard recording.
struct AhabMockUiAccess : MockUiAccess {
	std::string getKeyName(int key, int scancode) const override {
		if (key >= 32 && key < 128) return std::string(1, (char) std::tolower(key));
		return rack::widget::getKeyName(key);
	}
};

// Installs AhabMockUiAccess (real-keyboard-accurate keyName + dialog/clipboard recording), so
// the letter-based shortcuts dispatch correctly and file-dialog / clipboard calls don't touch
// real disk. Ctrl+O/B/S/Shift+S open native dialogs; tests that don't care about them get ""
// (cancelled) from the base mock, same as a user dismissing one.
//
// Must be constructed AFTER Test::Harness, not before: Test::Harness installs its own
// HarnessUiAccess into vcv::uiAccess for its own lifetime (test_harness.hpp), so a
// Test::mock::Guard from a mock declared *before* the harness gets shadowed the instant the
// harness's own guard runs - the harness's mock would win for the whole test, and every
// letter-based shortcut below would silently see "" again instead of a real key name.
struct AhabAccessMock {
	AhabMockUiAccess ui;
	Test::mock::Guard<StoermelderPackOne::vcv::UiAccess> uiGuard{StoermelderPackOne::vcv::uiAccess, &ui};
};

// Constructs an AhabWidget with the first-load intro dialog suppressed - every TEST_CASE below
// should use this instead of a bare `h.addWidget<AhabWidget>(m)`, mirroring Signls's
// addSignlsWidget (a fresh test module never has hasDataLoaded set, which would otherwise pop
// the dialog as a full-panel overlay eating every click/key these tests dispatch).
AhabWidget* addAhabWidget(Test::Harness& h, AhabModule* m) {
	bool before = StoermelderPackOne::pluginSettings.ahabInfo;
	StoermelderPackOne::pluginSettings.ahabInfo = false;
	AhabWidget* mw = h.addWidget<AhabWidget>(m);
	StoermelderPackOne::pluginSettings.ahabInfo = before;
	return mw;
}

// Selects the sim widget for keyboard focus. Unlike Signls, AhabSimWidget has no per-frame
// step() override and display_field only syncs inside drawLayer() (needs a real NVGcontext,
// out of scope here) - assertions instead read module->sim's live buffer directly
// (getFieldBuffer()/getFieldHeight()/getFieldWidth()), which *Request() calls mutate once
// drained by process() (see stepSim() in Ahab.test.hpp).
void primeAhabWidget(Test::Harness& h, AhabWidget* mw) {
	h.events().select(mw->simWidget);
}

// Publishes module->sim's current buffer into simWidget->display_field, the same swap
// drawLayer() does with the display_ready pointer (Ahab.cpp:856-866) minus the NVG drawing —
// needed by the handful of shortcuts (Ctrl+Shift+7, Ctrl+C, Ctrl+X) that read display_field
// rather than the sim's own live buffer, since a test never calls drawLayer(). field_copy()
// allocates display_field.buffer via realloc if it's still the field_init()'d NULL (a raw
// `.height =`/`.width =` assignment without this leaves buffer null and segfaults on first
// read - see field_resize_raw() in dep/orca-c/field.c).
void syncDisplayField(AhabModule* m, AhabWidget* mw) {
	Usz h, w;
	m->sim->getDisplayBuffer(h, w);
	Field src;
	src.buffer = const_cast<Glyph*>(m->sim->getFieldBuffer());
	src.height = (U16) h;
	src.width = (U16) w;
	field_copy(&src, &mw->simWidget->display_field);
}

}


TEST_CASE("Ahab Widget: is reachable through the real ModuleWidget tree", "[ahab][widget]") {
	Test::Harness h;
	AhabModule* m = h.addModule<AhabModule>("Ahab");
	AhabWidget* mw = addAhabWidget(h, m);

	auto* sim = h.events().find<AhabSimWidget>(mw);
	REQUIRE(sim == mw->simWidget);
}

TEST_CASE("Ahab Widget: Space toggles run/stop", "[ahab][widget]") {
	Test::Harness h;
	AhabModule* m = h.addModule<AhabModule>("Ahab");
	AhabWidget* mw = addAhabWidget(h, m);
	primeAhabWidget(h, mw);

	REQUIRE(m->simRunning == true);
	REQUIRE(h.events().keyPress(GLFW_KEY_SPACE));
	h.dspStep();
	REQUIRE(m->simRunning == false);

	REQUIRE(h.events().keyPress(GLFW_KEY_SPACE));
	h.dspStep();
	REQUIRE(m->simRunning == true);
}

TEST_CASE("Ahab Widget: Space in insert mode advances the cursor instead of toggling run", "[ahab][widget]") {
	Test::Harness h;
	AhabModule* m = h.addModule<AhabModule>("Ahab");
	AhabWidget* mw = addAhabWidget(h, m);
	primeAhabWidget(h, mw);
	auto& es = mw->simWidget->editorState;
	es.setInsertMode(true);

	REQUIRE(h.events().keyPress(GLFW_KEY_SPACE));
	REQUIRE(es.cursor_x == 1);
	REQUIRE(es.cursor_y == 0);
	// Run state is untouched - insert mode's Space branch returns before reaching the
	// run-toggle branch.
	REQUIRE(m->simRunning == true);
}

TEST_CASE("Ahab Widget: Backspace clears the selection", "[ahab][widget]") {
	Test::Harness h;
	AhabModule* m = h.addModule<AhabModule>("Ahab");
	AhabWidget* mw = addAhabWidget(h, m);
	primeAhabWidget(h, mw);

	m->sim->setGlyphRequest(0, 0, 'A', Mark_flag_input, true);
	h.dspStep();
	REQUIRE(m->sim->getFieldBuffer()[0] == 'A');

	REQUIRE(h.events().keyPress(GLFW_KEY_BACKSPACE));
	h.dspStep();
	REQUIRE(m->sim->getFieldBuffer()[0] == '.');
}

TEST_CASE("Ahab Widget: arrow keys move the cursor, Shift+arrow extends the selection", "[ahab][widget]") {
	Test::Harness h;
	AhabModule* m = h.addModule<AhabModule>("Ahab");
	AhabWidget* mw = addAhabWidget(h, m);
	primeAhabWidget(h, mw);
	auto& es = mw->simWidget->editorState;

	REQUIRE(h.events().keyPress(GLFW_KEY_RIGHT));
	REQUIRE(es.cursor_x == 1);
	REQUIRE(es.sel_w == 1); // plain move collapses any selection

	REQUIRE(h.events().keyPress(GLFW_KEY_DOWN, GLFW_MOD_SHIFT));
	REQUIRE(es.cursor_y == 1);
	REQUIRE(es.sel_h == 2); // anchor (0,1) to cursor (1,1)
	REQUIRE(es.sel_w == 1);
}

TEST_CASE("Ahab Widget: Ctrl+arrow uses the grid step instead of a single cell", "[ahab][widget]") {
	Test::Harness h;
	AhabModule* m = h.addModule<AhabModule>("Ahab");
	AhabWidget* mw = addAhabWidget(h, m);
	primeAhabWidget(h, mw);
	auto& es = mw->simWidget->editorState;
	REQUIRE(m->gridStepCol == 8);

	REQUIRE(h.events().keyPress(GLFW_KEY_RIGHT, RACK_MOD_CTRL));
	REQUIRE(es.cursor_x == 8);
}

TEST_CASE("Ahab Widget: arrow keys clamp at the field edges", "[ahab][widget]") {
	Test::Harness h;
	AhabModule* m = h.addModule<AhabModule>("Ahab");
	AhabWidget* mw = addAhabWidget(h, m);
	primeAhabWidget(h, mw);
	auto& es = mw->simWidget->editorState;

	REQUIRE(h.events().keyPress(GLFW_KEY_LEFT)); // already at 0
	REQUIRE(es.cursor_x == 0);
	REQUIRE(h.events().keyPress(GLFW_KEY_UP)); // already at 0
	REQUIRE(es.cursor_y == 0);
}

TEST_CASE("Ahab Widget: held arrow key repeats via GLFW_REPEAT", "[ahab][widget]") {
	Test::Harness h;
	AhabModule* m = h.addModule<AhabModule>("Ahab");
	AhabWidget* mw = addAhabWidget(h, m);
	primeAhabWidget(h, mw);
	auto& es = mw->simWidget->editorState;

	REQUIRE(h.events().keyAt(math::Vec(0, 0), GLFW_KEY_RIGHT, GLFW_PRESS));
	REQUIRE(es.cursor_x == 1);
	REQUIRE(h.events().keyAt(math::Vec(0, 0), GLFW_KEY_RIGHT, GLFW_REPEAT));
	REQUIRE(es.cursor_x == 2);
}

TEST_CASE("Ahab Widget: a single tap does not also move on Rack's own RACK_HELD synthesis", "[ahab][widget]") {
	// Regression test: EventState::handleHover fires a synthesised RACK_HELD event for every
	// currently-held key on every hover pass, with no delay of its own - so a single quick tap
	// can see RACK_HELD arrive on the very next hover before GLFW_RELEASE, well before anything
	// a person would call "holding" the key. Before the fix this doubled every keystroke's
	// effect ("pressing the cursor key is immediately handled as repeat").
	Test::Harness h;
	AhabModule* m = h.addModule<AhabModule>("Ahab");
	AhabWidget* mw = addAhabWidget(h, m);
	primeAhabWidget(h, mw);
	auto& es = mw->simWidget->editorState;

	REQUIRE(h.events().key(GLFW_KEY_RIGHT, GLFW_PRESS));
	REQUIRE(es.cursor_x == 1);

	// A hover pass while the key is still logically held (no release yet) - Rack fires
	// RACK_HELD for it here, same as it would between two frames of a real, brief tap.
	h.events().hover(math::Vec(1, 1));
	REQUIRE(es.cursor_x == 1);   // must NOT have advanced a second time

	REQUIRE(h.events().key(GLFW_KEY_RIGHT, GLFW_RELEASE));
	REQUIRE(es.cursor_x == 1);
}

TEST_CASE("Ahab Widget: Escape collapses the selection to the cursor", "[ahab][widget]") {
	Test::Harness h;
	AhabModule* m = h.addModule<AhabModule>("Ahab");
	AhabWidget* mw = addAhabWidget(h, m);
	primeAhabWidget(h, mw);
	auto& es = mw->simWidget->editorState;

	h.events().keyPress(GLFW_KEY_RIGHT, GLFW_MOD_SHIFT);
	h.events().keyPress(GLFW_KEY_RIGHT, GLFW_MOD_SHIFT);
	REQUIRE(es.sel_w > 1);

	REQUIRE(h.events().keyPress(GLFW_KEY_ESCAPE));
	REQUIRE(es.sel_w == 1);
	REQUIRE(es.sel_h == 1);
}

TEST_CASE("Ahab Widget: Shift+Escape toggles Focus mode", "[ahab][widget]") {
	Test::Harness h;
	AhabModule* m = h.addModule<AhabModule>("Ahab");
	AhabWidget* mw = addAhabWidget(h, m);
	primeAhabWidget(h, mw);

	REQUIRE_FALSE(mw->simWidget->focusMode.active);
	REQUIRE(h.events().keyPress(GLFW_KEY_ESCAPE, GLFW_MOD_SHIFT));
	REQUIRE(mw->simWidget->focusMode.active);
	REQUIRE(h.events().keyPress(GLFW_KEY_ESCAPE, GLFW_MOD_SHIFT));
	REQUIRE_FALSE(mw->simWidget->focusMode.active);
}

TEST_CASE("Ahab Widget: Ctrl+F steps the simulation one tick", "[ahab][widget]") {
	Test::Harness h;
	AhabModule* m = h.addModule<AhabModule>("Ahab");
	AhabWidget* mw = addAhabWidget(h, m);
	primeAhabWidget(h, mw);

	Usz before = m->sim->getTickNumber();
	REQUIRE(h.events().keyPress(GLFW_KEY_F, RACK_MOD_CTRL));
	h.dspStep();
	REQUIRE(m->sim->getTickNumber() == before + 1);
}

TEST_CASE("Ahab Widget: Ctrl+Shift+R resets the tick number to zero", "[ahab][widget]") {
	Test::Harness h;
	AhabModule* m = h.addModule<AhabModule>("Ahab");
	AhabWidget* mw = addAhabWidget(h, m);
	primeAhabWidget(h, mw);

	m->sim->stepRequest();
	h.dspStep();
	REQUIRE(m->sim->getTickNumber() > 0);

	REQUIRE(h.events().keyPress(GLFW_KEY_R, RACK_MOD_CTRL | GLFW_MOD_SHIFT));
	h.dspStep();
	REQUIRE(m->sim->getTickNumber() == 0);
}

TEST_CASE("Ahab Widget: Ctrl+Shift+7 toggles a comment block on the selection", "[ahab][widget]") {
	Test::Harness h;
	AhabModule* m = h.addModule<AhabModule>("Ahab");
	AhabWidget* mw = addAhabWidget(h, m);
	primeAhabWidget(h, mw);
	auto& es = mw->simWidget->editorState;

	es.setSelection(0, 0, 1, 3, m->sim->getFieldHeight(), m->sim->getFieldWidth());
	syncDisplayField(m, mw);   // toggleCommentBlock() reads display_field, not the live sim buffer

	REQUIRE(h.events().keyPress(GLFW_KEY_7, RACK_MOD_CTRL | GLFW_MOD_SHIFT));
	h.dspStep();
	Usz w = m->sim->getFieldWidth();
	REQUIRE(m->sim->getFieldBuffer()[0 * w + 0] == '#');
	REQUIRE(m->sim->getFieldBuffer()[0 * w + 2] == '#');
}


// ---- Letter-based shortcuts (route through vcv::ui::getKeyName(), need MockUiAccess) --------

TEST_CASE("Ahab Widget: Ctrl+A selects the whole field", "[ahab][widget]") {
	Test::Harness h;
	AhabAccessMock mock;
	AhabModule* m = h.addModule<AhabModule>("Ahab");
	AhabWidget* mw = addAhabWidget(h, m);
	primeAhabWidget(h, mw);
	auto& es = mw->simWidget->editorState;

	REQUIRE(h.events().keyPress(GLFW_KEY_A, RACK_MOD_CTRL));
	REQUIRE(es.sel_h == m->sim->getFieldHeight());
	REQUIRE(es.sel_w == m->sim->getFieldWidth());
}

TEST_CASE("Ahab Widget: Ctrl+N clears the field", "[ahab][widget]") {
	Test::Harness h;
	AhabAccessMock mock;
	AhabModule* m = h.addModule<AhabModule>("Ahab");
	AhabWidget* mw = addAhabWidget(h, m);
	primeAhabWidget(h, mw);

	m->sim->setGlyphRequest(0, 0, 'A', Mark_flag_input, true);
	h.dspStep();
	REQUIRE(m->sim->getFieldBuffer()[0] == 'A');

	REQUIRE(h.events().keyPress(GLFW_KEY_N, RACK_MOD_CTRL));
	h.dspStep();
	REQUIRE(m->sim->getFieldBuffer()[0] == '.');
}

TEST_CASE("Ahab Widget: Ctrl+O opens a load dialog", "[ahab][widget]") {
	Test::Harness h;
	AhabAccessMock mock;
	AhabModule* m = h.addModule<AhabModule>("Ahab");
	AhabWidget* mw = addAhabWidget(h, m);
	primeAhabWidget(h, mw);

	REQUIRE(h.events().keyPress(GLFW_KEY_O, RACK_MOD_CTRL));
	// The dialog is cancelled (mock's default openDialog() returns "") - simLoad() then
	// returns without touching the sim. Asserting the dispatch consumed the key is the point;
	// simLoad()'s own file-load behavior is covered directly in Ahab.test.module.hpp.
}

TEST_CASE("Ahab Widget: Ctrl+B opens an inject-file dialog", "[ahab][widget]") {
	Test::Harness h;
	AhabAccessMock mock;
	AhabModule* m = h.addModule<AhabModule>("Ahab");
	AhabWidget* mw = addAhabWidget(h, m);
	primeAhabWidget(h, mw);

	REQUIRE(h.events().keyPress(GLFW_KEY_B, RACK_MOD_CTRL));
}

TEST_CASE("Ahab Widget: Ctrl+S opens a save dialog", "[ahab][widget]") {
	Test::Harness h;
	AhabAccessMock mock;
	AhabModule* m = h.addModule<AhabModule>("Ahab");
	AhabWidget* mw = addAhabWidget(h, m);
	primeAhabWidget(h, mw);

	REQUIRE(h.events().keyPress(GLFW_KEY_S, RACK_MOD_CTRL));
}

TEST_CASE("Ahab Widget: Ctrl+Shift+S opens a save-selection dialog", "[ahab][widget]") {
	Test::Harness h;
	AhabAccessMock mock;
	AhabModule* m = h.addModule<AhabModule>("Ahab");
	AhabWidget* mw = addAhabWidget(h, m);
	primeAhabWidget(h, mw);

	REQUIRE(h.events().keyPress(GLFW_KEY_S, RACK_MOD_CTRL | GLFW_MOD_SHIFT));
}

TEST_CASE("Ahab Widget: Ctrl+Z/Ctrl+Shift+Z undo and redo a glyph edit", "[ahab][widget]") {
	Test::Harness h;
	AhabAccessMock mock;
	AhabModule* m = h.addModule<AhabModule>("Ahab");
	AhabWidget* mw = addAhabWidget(h, m);
	primeAhabWidget(h, mw);

	m->sim->setGlyphRequest(0, 0, 'A', Mark_flag_input, true);
	h.dspStep();
	REQUIRE(m->sim->getFieldBuffer()[0] == 'A');

	REQUIRE(h.events().keyPress(GLFW_KEY_Z, RACK_MOD_CTRL));
	h.dspStep();
	REQUIRE(m->sim->getFieldBuffer()[0] == '.');

	REQUIRE(h.events().keyPress(GLFW_KEY_Z, RACK_MOD_CTRL | GLFW_MOD_SHIFT));
	h.dspStep();
	REQUIRE(m->sim->getFieldBuffer()[0] == 'A');
}

TEST_CASE("Ahab Widget: Ctrl+I toggles insert mode", "[ahab][widget]") {
	Test::Harness h;
	AhabAccessMock mock;
	AhabModule* m = h.addModule<AhabModule>("Ahab");
	AhabWidget* mw = addAhabWidget(h, m);
	primeAhabWidget(h, mw);
	auto& es = mw->simWidget->editorState;

	REQUIRE_FALSE(es.getInsertMode());
	REQUIRE(h.events().keyPress(GLFW_KEY_I, RACK_MOD_CTRL));
	REQUIRE(es.getInsertMode());
	REQUIRE(h.events().keyPress(GLFW_KEY_I, RACK_MOD_CTRL));
	REQUIRE_FALSE(es.getInsertMode());
}

TEST_CASE("Ahab Widget: Ctrl+C copies the selection to the clipboard", "[ahab][widget]") {
	Test::Harness h;
	AhabAccessMock mock;
	AhabModule* m = h.addModule<AhabModule>("Ahab");
	AhabWidget* mw = addAhabWidget(h, m);
	primeAhabWidget(h, mw);
	auto& es = mw->simWidget->editorState;

	m->sim->setGlyphRequest(0, 0, 'A', Mark_flag_input, true);
	h.dspStep();
	syncDisplayField(m, mw);   // copySelectionToClipboard() reads display_field
	es.setSelection(0, 0, 1, 1, m->sim->getFieldHeight(), m->sim->getFieldWidth());

	REQUIRE(h.events().keyPress(GLFW_KEY_C, RACK_MOD_CTRL));
	REQUIRE_FALSE(mock.ui.clipboardWrites.empty());
	REQUIRE(mock.ui.clipboardWrites.back().find('A') != std::string::npos);
}

TEST_CASE("Ahab Widget: Ctrl+X cuts the selection", "[ahab][widget]") {
	Test::Harness h;
	AhabAccessMock mock;
	AhabModule* m = h.addModule<AhabModule>("Ahab");
	AhabWidget* mw = addAhabWidget(h, m);
	primeAhabWidget(h, mw);
	auto& es = mw->simWidget->editorState;

	m->sim->setGlyphRequest(0, 0, 'A', Mark_flag_input, true);
	h.dspStep();
	syncDisplayField(m, mw);   // cutSelection() copies via display_field first (see copy above)
	es.setSelection(0, 0, 1, 1, m->sim->getFieldHeight(), m->sim->getFieldWidth());

	REQUIRE(h.events().keyPress(GLFW_KEY_X, RACK_MOD_CTRL));
	h.dspStep();
	REQUIRE_FALSE(mock.ui.clipboardWrites.empty());
	REQUIRE(m->sim->getFieldBuffer()[0] == '.');
}

TEST_CASE("Ahab Widget: Ctrl+V pastes the clipboard at the cursor", "[ahab][widget]") {
	Test::Harness h;
	AhabAccessMock mock;
	AhabModule* m = h.addModule<AhabModule>("Ahab");
	AhabWidget* mw = addAhabWidget(h, m);
	primeAhabWidget(h, mw);

	mock.ui.clipboardText = "A";

	REQUIRE(h.events().keyPress(GLFW_KEY_V, RACK_MOD_CTRL));
	h.dspStep();
	REQUIRE(m->sim->getFieldBuffer()[0] == 'A');
}


// ---- Context menu shortcut text reflects the live keymap binding, not a hardcoded string ----

namespace {
// Finds a direct MenuItem child by its label text. Menu items are added as plain children
// (widget::Widget::children), not through any Ahab-specific API, so a linear scan + dynamic_cast
// is the whole helper needed.
ui::MenuItem* findMenuItemByText(ui::Menu* menu, const std::string& text) {
	for (Widget* w : menu->children) {
		auto* item = dynamic_cast<ui::MenuItem*>(w);
		if (item && item->text == text) return item;
	}
	return nullptr;
}
}

TEST_CASE("Ahab Widget: context menu shows the default Undo shortcut", "[ahab][widget]") {
	Test::Harness h;
	AhabModule* m = h.addModule<AhabModule>("Ahab");
	AhabWidget* mw = addAhabWidget(h, m);
	primeAhabWidget(h, mw);

	ui::Menu menu;
	mw->simWidget->appendContextMenu(&menu, true);

	auto* undo = findMenuItemByText(&menu, "Undo");
	REQUIRE(undo != nullptr);
	// rightText is the platform-flavoured display form (KeyCombo::displayString(), "⌘" on
	// macOS) - RACK_MOD_CTRL_NAME, not a hardcoded "Ctrl", is what a real menu would show here.
	CHECK(undo->rightText.find(RACK_MOD_CTRL_NAME "+Z") != std::string::npos);
}

TEST_CASE("Ahab Widget: context menu reflects a rebound shortcut, not the old default", "[ahab][widget]") {
	// Keymaps is a process-wide registry (Keymap.hpp): reset it so a rebind here doesn't leak
	// into other TEST_CASEs sharing the "Ahab" slug within this binary.
	Keymaps::resetForTest();
	Test::Harness h;
	AhabModule* m = h.addModule<AhabModule>("Ahab");
	AhabWidget* mw = addAhabWidget(h, m);
	primeAhabWidget(h, mw);

	mw->simWidget->keymap->bind("edit.copy", KeyCombo("Ctrl+Q"));

	ui::Menu menu;
	mw->simWidget->appendContextMenu(&menu, true);

	// Copy/Cut/Paste live inside the "Selection" submenu, built lazily by
	// MenuItem::createChildMenu() when the submenu is actually opened - not eagerly as direct
	// children of the top-level menu appendContextMenu() just populated.
	auto* selection = findMenuItemByText(&menu, "Selection");
	REQUIRE(selection != nullptr);
	ui::Menu* submenu = selection->createChildMenu();
	REQUIRE(submenu != nullptr);

	auto* copy = findMenuItemByText(submenu, "Copy");
	REQUIRE(copy != nullptr);
	CHECK(copy->rightText == RACK_MOD_CTRL_NAME "+Q");

	Keymaps::resetForTest();
}
