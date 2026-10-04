// rack.registerContextMenu(): module lifecycle
// The module-level consequences of script-registered context menus: clearing
// the script or switching engines drops the previous engine's registered
// items. These behaviours are engine-independent: each case drives both
// engines through the public module API (loadScript/clearScript/
// getContextMenus) and never touches engine internals.

static const char* QJS_BOOL = R"(/**
 * @engine QuickJs@v1
 */
let v = false;
rack.registerContextMenu({
	type: "boolean",
	label: "Velocity to CC",
	onGetValue: function() {
		return v;
	},
	onChange: function(checked) {
		v = checked;
		rack.log("onChange: " + (checked ? "true" : "false"));
	}
});
)";

static const char* QJS_OPTIONS = R"(/**
 * @engine QuickJs@v1
 */
let v = 1;
rack.registerContextMenu({
	type: "options",
	label: "Out mode",
	options: ["Internal", "External", "Both"],
	onGetValue: function() {
		return v;
	},
	onChange: function(selectedIndex, selectedLabel) {
		v = selectedIndex;
		rack.log("onChange: " + selectedIndex + " " + selectedLabel);
	}
});
)";

static const char* LUA_BOOL = R"(--[[
@engine minilua@v1
--]]
v = false
rack.registerContextMenu({
	type = "boolean",
	label = "Velocity to CC",
	onGetValue = function()
		return v
	end,
	onChange = function(checked)
		v = checked
		rack.log("onChange: " .. tostring(checked))
	end
})
)";

static const char* LUA_OPTIONS = R"(--[[
@engine minilua@v1
--]]
v = 1
rack.registerContextMenu({
	type = "options",
	label = "Out mode",
	options = {"Internal", "External", "Both"},
	onGetValue = function()
		return v
	end,
	onChange = function(selectedIndex, selectedLabel)
		v = selectedIndex
		rack.log("onChange: " .. selectedIndex .. " " .. selectedLabel)
	end
})
)";

// rack.registerContextMenu(): widget integration
// appendContextMenu() inserts an async placeholder that builds the real menu
// items once the worker has evaluated onGetValue — driven by the
// placeholder's step(), which the tests call via buildScriptMenuItems() (the
// worker is inline under SyncTaskWorker, after an engine process() pump). Clicking an item through
// MenuItem::doAction() fires the script callback. The widget behaviour is
// engine-independent: each case builds the menu for a fresh module+widget per
// engine script.
// Drive the async placeholder (ScriptContextMenuItems) that builds the
// script-registered items, then remove and delete the placeholder exactly as
// Rack's Menu::step() would once it has requested deletion. Only the
// placeholder is stepped: stepping the whole menu would run Menu::step()
// (which reads its parent's box) and MenuItem::step() (which needs APP->window
// for font metrics), neither of which the test harness provides. The
// placeholder is the only MenuEntry that is not a MenuItem, MenuLabel, or
// MenuSeparator. Freeing it matters: the built items are its siblings and must
// keep working after it is destroyed — their callbacks capture the module
// pointer, not the placeholder itself.
static void buildScriptMenuItems(rack::ui::Menu* menu) {
	rack::Widget* placeholder = nullptr;
	for (rack::Widget* child : menu->children) {
		if (!dynamic_cast<rack::ui::MenuEntry*>(child))
			continue;
		if (dynamic_cast<rack::ui::MenuItem*>(child))
			continue;
		if (dynamic_cast<rack::ui::MenuLabel*>(child))
			continue;
		if (dynamic_cast<rack::ui::MenuSeparator*>(child))
			continue;
		placeholder = child;
		break;
	}
	if (!placeholder) return;
	placeholder->step();
	// Delete exactly as Rack's Widget::step() would: removeChild() detaches the
	// placeholder from the menu (RemoveEvent + parent = NULL), then delete is
	// legal — Widget::~Widget() asserts the widget is orphaned.
	menu->removeChild(placeholder);
	delete placeholder;
}

TEST_CASE("Context menu: boolean item is built and click fires the callback", "[MidiKit][ContextMenu]") {
	FOR_EACH_LANG;
	const char* script = Pair{QJS_BOOL, LUA_BOOL}.get(lang);
	Kit<> kit;
	MidiKitModule* m = kit.m;
	m->model = modelMidiKit;
	m->loadScript(script);
	MidiKitWidget* mw = Test::createWidget<MidiKitWidget>(m);

	rack::ui::Menu* menu = new rack::ui::Menu;
	mw->appendContextMenu(menu);
	// The script items are built asynchronously: the worker evaluates
	// onGetValue (inline here via SyncTaskWorker) and the placeholder's
	// step() builds the real items. The helper also frees the placeholder
	// as Rack's Menu::step() would, so clicking below runs against the
	// freed placeholder (its callbacks must capture the module, not
	// `this`). The whole menu can't be stepped without a window.
	m->host.getActiveEngine()->process();   // the menu query is low priority: answered on the next pump
	buildScriptMenuItems(menu);

	rack::ui::MenuItem* item = nullptr;
	for (rack::Widget* child : menu->children) {
		if (auto* mi = dynamic_cast<rack::ui::MenuItem*>(child)) {
			if (mi->text == "Velocity to CC") { item = mi; break; }
		}
	}
	REQUIRE(item != nullptr);
	// Unchecked → no checkmark.
	REQUIRE(item->rightText == "");

	item->doAction(true);
	std::string log = drainLog(m);
	REQUIRE(log.find("onChange: true") != std::string::npos);

	delete menu;
	Test::destroyWidget(mw);
}

TEST_CASE("Context menu: the log display's menu starts with the running script's section", "[MidiKit][ContextMenu]") {
	FOR_EACH_LANG;
	const char* script = Pair{QJS_BOOL, LUA_BOOL}.get(lang);
	Kit<> kit;
	MidiKitModule* m = kit.m;
	m->model = modelMidiKit;
	MidiKitWidget* mw = Test::createWidget<MidiKitWidget>(m);
	REQUIRE(mw->logDisplay != nullptr);

	auto findItem = [](rack::ui::Menu* menu) {
		for (rack::Widget* child : menu->children) {
			if (auto* mi = dynamic_cast<rack::ui::MenuItem*>(child)) {
				if (mi->text == "Velocity to CC") return mi;
			}
		}
		return (rack::ui::MenuItem*)nullptr;
	};

	// No script, no script items (and no placeholder to build them).
	rack::ui::Menu* menu = new rack::ui::Menu;
	mw->logDisplay->appendContextMenu(menu);
	size_t plain = menu->children.size();
	REQUIRE(findItem(menu) == nullptr);
	delete menu;

	m->loadScript(script);
	menu = new rack::ui::Menu;
	mw->logDisplay->appendContextMenu(menu);
	REQUIRE(menu->children.size() > plain);   // engine section + separator
	m->host.getActiveEngine()->process();     // the menu query is low priority
	buildScriptMenuItems(menu);

	// The running-script section comes first, then the log's own entries.
	auto* first = dynamic_cast<rack::ui::MenuLabel*>(menu->children.front());
	REQUIRE(first != nullptr);
	REQUIRE(first->text.find("Running Script") == 0);
	rack::ui::MenuItem* item = findItem(menu);
	REQUIRE(item != nullptr);
	size_t itemPos = 0, logPos = 0, i = 0;
	for (rack::Widget* child : menu->children) {
		if (child == item) itemPos = i;
		if (auto* l = dynamic_cast<rack::ui::MenuLabel*>(child)) if (l->text == "Log") logPos = i;
		i++;
	}
	REQUIRE(itemPos < logPos);
	item->doAction(true);
	REQUIRE(drainLog(m).find("onChange: true") != std::string::npos);

	delete menu;
	Test::destroyWidget(mw);
}

TEST_CASE("Context menu: options submenu is built and click fires the callback", "[MidiKit][ContextMenu]") {
	FOR_EACH_LANG;
	const char* script = Pair{QJS_OPTIONS, LUA_OPTIONS}.get(lang);
	Kit<> kit;
	MidiKitModule* m = kit.m;
	m->model = modelMidiKit;
	m->loadScript(script);
	MidiKitWidget* mw = Test::createWidget<MidiKitWidget>(m);

	rack::ui::Menu* menu = new rack::ui::Menu;
	mw->appendContextMenu(menu);
	// See the boolean test case: step the async placeholder so it builds
	// the real items, then free the placeholder (Rack's Menu::step()
	// behaviour) so the submenu callbacks run without `this`.
	m->host.getActiveEngine()->process();   // the menu query is low priority: answered on the next pump
	buildScriptMenuItems(menu);

	rack::ui::MenuItem* sub = nullptr;
	for (rack::Widget* child : menu->children) {
		if (auto* mi = dynamic_cast<rack::ui::MenuItem*>(child)) {
			if (mi->text == "Out mode") { sub = mi; break; }
		}
	}
	REQUIRE(sub != nullptr);

	// Building the child menu is what a hover/click on the submenu does.
	rack::ui::Menu* submenu = sub->createChildMenu();
	REQUIRE(submenu != nullptr);

	rack::ui::MenuItem* external = nullptr;
	rack::ui::MenuItem* both = nullptr;
	for (rack::Widget* child : submenu->children) {
		if (auto* mi = dynamic_cast<rack::ui::MenuItem*>(child)) {
			if (mi->text == "External") external = mi;
			if (mi->text == "Both") both = mi;
		}
	}
	REQUIRE(external != nullptr);
	REQUIRE(both != nullptr);
	// selected == 1 → "External" is the checked one, "Both" is not.
	REQUIRE(external->rightText == "✔");
	REQUIRE(both->rightText == "");

	both->doAction(true);
	std::string log = drainLog(m);
	REQUIRE(log.find("onChange: 2 Both") != std::string::npos);

	delete submenu;
	delete menu;
	Test::destroyWidget(mw);
}


// Example-script submenus (appendExampleItems / hasExampleScripts)
// appendExampleItems() and hasExampleScripts() scan a real directory on disk,
// so these tests build a throwaway tree under the system temp dir (see
// TempExampleDir) and point the menu builder at it. They assert on the menu
// structure — leaf items for matching scripts, nested submenus for subfolders,
// empty subfolders skipped, "None found" when nothing matches — and on the
// click-through: a leaf's action loads the script into the module via loadJs().
// Creates a unique, writable directory tree for one test case and removes it on
// destruction. Names come from a static counter, which keeps every concurrently
// live tree unique; a stale leftover from a crashed run is removed first, and
// createDirectories() is happy to recreate it.
struct TempExampleDir {
	std::string root;

	TempExampleDir() {
		root = rack::system::join(rack::system::getTempDirectory(), "MidiKit-example-test-" + std::to_string(++s_counter));
		rack::system::removeRecursively(root);  // clear stale leftovers
		REQUIRE(rack::system::createDirectories(root));
	}
	~TempExampleDir() {
		rack::system::removeRecursively(root);
	}

	// Full path of a root-relative path.
	std::string path(const std::string& rel) const {
		return rack::system::join(root, rel);
	}

	// Writes a file (creating parent dirs) and returns its full path.
	std::string write(const std::string& rel, const std::string& content = "") {
		std::string p = path(rel);
		rack::system::createDirectories(rack::system::getDirectory(p));
		std::ofstream f(p);
		REQUIRE(f.good());
		f << content;
		return p;
	}

	static int s_counter;
};
int TempExampleDir::s_counter = 0;

// Creates the module+widget pair used by the example-menu tests.
static void createExampleFixture(Kit<>& kit, MidiKitModule** m, MidiKitWidget** mw) {
	*m = kit.m;
	(*m)->model = modelMidiKit;
	*mw = Test::createWidget<MidiKitWidget>(*m);
}

// Finds a child MenuItem of `menu` by text; returns NULL when absent.
static rack::ui::MenuItem* findMenuItem(rack::ui::Menu* menu, const std::string& text) {
	for (rack::Widget* child : menu->children) {
		if (auto* mi = dynamic_cast<rack::ui::MenuItem*>(child)) {
			if (mi->text == text) return mi;
		}
	}
	return nullptr;
}

// Returns whether `menu` has a MenuLabel with the given text.
static bool hasMenuLabel(rack::ui::Menu* menu, const std::string& text) {
	for (rack::Widget* child : menu->children) {
		if (auto* label = dynamic_cast<rack::ui::MenuLabel*>(child)) {
			if (label->text == text) return true;
		}
	}
	return false;
}

TEST_CASE("hasExampleScripts detects scripts recursively", "[MidiKit][Examples]") {
	MidiKitModule* m;
	MidiKitWidget* mw;
	Kit<> kit;
	createExampleFixture(kit, &m, &mw);

	TempExampleDir d;
	d.write("A.js");
	d.write("notes.md");
	d.write("sub/B.js");
	d.write("sub/deep/C.js");
	d.write("empty/deep/placeholder.txt");

	REQUIRE(mw->hasExampleScripts(d.root, ".js"));
	REQUIRE(!mw->hasExampleScripts(d.root, ".lua"));           // no .lua anywhere
	REQUIRE(mw->hasExampleScripts(d.path("sub"), ".js"));      // one level down
	REQUIRE(mw->hasExampleScripts(d.path("sub/deep"), ".js")); // nested
	REQUIRE(!mw->hasExampleScripts(d.path("empty"), ".js"));   // only .txt
	REQUIRE(!mw->hasExampleScripts(d.path("missing"), ".js")); // no such dir

	Test::destroyWidget(mw);
}

TEST_CASE("appendExampleItems builds nested submenus and skips empty folders", "[MidiKit][Examples]") {
	MidiKitModule* m;
	MidiKitWidget* mw;
	Kit<> kit;
	createExampleFixture(kit, &m, &mw);

	TempExampleDir d;
	d.write("Alpha.js");
	d.write("Beta.md");               // wrong extension → ignored
	d.write("sub/SubOne.js");
	d.write("sub/SubTwo.js");
	d.write("sub/deep/DeepOne.js");
	d.write("empty/placeholder.txt"); // no .js in this subtree → skipped
	d.write("other/Other.lua");       // .lua only → skipped for a .js listing

	rack::ui::Menu* menu = new rack::ui::Menu;
	mw->appendExampleItems(menu, d.root, ".js");

	// Top level: subfolders come first (sorted), then files (sorted).
	// Folders without any matching script do not appear at all.
	rack::ui::MenuItem* sub = findMenuItem(menu, "sub");
	REQUIRE(sub != nullptr);
	REQUIRE(sub->rightText == "▸");                  // submenu arrow

	rack::ui::MenuItem* alpha = findMenuItem(menu, "Alpha");
	REQUIRE(alpha != nullptr);
	REQUIRE(alpha->rightText == "");                 // leaf: no submenu arrow
	REQUIRE(findMenuItem(menu, "Beta") == nullptr);  // wrong ext ignored
	REQUIRE(findMenuItem(menu, "empty") == nullptr); // no .js inside
	REQUIRE(findMenuItem(menu, "other") == nullptr); // no .js inside

	// Verify ordering: subfolder(s) appear before file(s).
	int subIdx = 0, alphaIdx = 0, idx = 0;
	for (auto* child : menu->children) {
		if (child == sub) subIdx = idx;
		if (child == alpha) alphaIdx = idx;
		idx++;
	}
	REQUIRE(subIdx < alphaIdx);

	// Open sub/: SubOne and SubTwo are leaves, deep/ is another submenu.
	rack::ui::Menu* subMenu = sub->createChildMenu();
	REQUIRE(subMenu != nullptr);
	REQUIRE(findMenuItem(subMenu, "SubOne") != nullptr);
	REQUIRE(findMenuItem(subMenu, "SubTwo") != nullptr);
	rack::ui::MenuItem* deep = findMenuItem(subMenu, "deep");
	REQUIRE(deep != nullptr);

	// Open deep/: only DeepOne, no "None found" (a non-empty match).
	rack::ui::Menu* deepMenu = deep->createChildMenu();
	REQUIRE(findMenuItem(deepMenu, "DeepOne") != nullptr);
	REQUIRE(deepMenu->children.size() == 1);
	REQUIRE(!hasMenuLabel(deepMenu, "None found"));

	delete deepMenu;
	delete subMenu;
	delete menu;
	Test::destroyWidget(mw);
}

TEST_CASE("appendExampleItems leaf click loads the script", "[MidiKit][Examples]") {
	MidiKitModule* m;
	MidiKitWidget* mw;
	Kit<> kit;
	createExampleFixture(kit, &m, &mw);

	static const std::string CONTENT =
		"/**\n"
		" * @engine QuickJs@v1\n"
		" */\n"
		"rack.log(\"loaded from submenu\");\n";

	TempExampleDir d;
	std::string path = d.write("Alpha.js", CONTENT);

	rack::ui::Menu* menu = new rack::ui::Menu;
	mw->appendExampleItems(menu, d.root, ".js");

	rack::ui::MenuItem* alpha = findMenuItem(menu, "Alpha");
	REQUIRE(alpha != nullptr);

	// Clicking a leaf is what Rack does on mouse release: it runs the item's
	// action, which records the file path and loads it into the module.
	alpha->doAction(true);

	REQUIRE(mw->filename == path);
	REQUIRE(m->host.script == CONTENT);
	REQUIRE(m->host.isQuickJsEngine());

	delete menu;
	Test::destroyWidget(mw);
}

TEST_CASE("appendExampleItems shows 'None found' when nothing matches", "[MidiKit][Examples]") {
	MidiKitModule* m;
	MidiKitWidget* mw;
	Kit<> kit;
	createExampleFixture(kit, &m, &mw);

	TempExampleDir d;
	d.write("readme.md"); // no scripts of the requested engine

	// A directory with only non-matching files → label only, no items.
	rack::ui::Menu* menu = new rack::ui::Menu;
	mw->appendExampleItems(menu, d.root, ".js");
	REQUIRE(hasMenuLabel(menu, "None found"));
	for (rack::Widget* child : menu->children) {
		REQUIRE(dynamic_cast<rack::ui::MenuItem*>(child) == nullptr);
	}
	delete menu;

	// A directory that does not exist at all → same behaviour.
	rack::ui::Menu* menu2 = new rack::ui::Menu;
	mw->appendExampleItems(menu2, d.path("missing"), ".js");
	REQUIRE(hasMenuLabel(menu2, "None found"));
	delete menu2;

	Test::destroyWidget(mw);
}


// "action" and "file" items through the real menu

static const char* QJS_ACTION_FILE = R"(/**
 * @engine QuickJs@v1
 */
rack.registerContextMenu({ type: "action", label: "Do it", onChange: function() { rack.log("did it"); } });
rack.registerContextMenu({ type: "file", label: "Pick file", onChange: function(content, name) { rack.log("got [" + content + "] from " + name); } });
)";

static const char* LUA_ACTION_FILE = R"(--[[
@engine minilua@v1
--]]
rack.registerContextMenu({ type = "action", label = "Do it", onChange = function() rack.log("did it") end })
rack.registerContextMenu({ type = "file", label = "Pick file", onChange = function(content, name) rack.log("got [" .. content .. "] from " .. name) end })
)";

namespace {

struct ScriptFileUiMock : StoermelderPackOne::vcv::UiAccess {
	std::string chosen;                 // what the dialog answers; empty = cancelled
	int dialogs = 0;
	std::vector<std::string> messages;
	std::string openDialog(const std::string&, const std::string&) override {
		dialogs++;
		return chosen;
	}
	bool message(StoermelderPackOne::vcv::MessageType, StoermelderPackOne::vcv::MessageButtons, const std::string& msg) override {
		messages.push_back(msg);
		return false;
	}
};

// One fake file; a path of an unreadable file is "bad.txt".
struct ScriptFileFsMock : Test::mock::MockFileAccess {
	std::string content;
	uint64_t reportedSize = 0;
	bool readable = true;
	bool read(const std::string&, std::string& data) const override {
		if (!readable) return false;
		data = content;
		return true;
	}
	uint64_t getFileSize(const std::string&) override { return reportedSize; }
	std::string getFilename(const std::string& path) override {
		size_t at = path.find_last_of('/');
		return at == std::string::npos ? path : path.substr(at + 1);
	}
};

} // namespace

TEST_CASE("Context menu: an action item calls onChange on every click", "[MidiKit][ContextMenu]") {
	Pair scripts{QJS_ACTION_FILE, LUA_ACTION_FILE};
	FOR_EACH_LANG;
	const char* script = scripts.get(lang);
	Kit<> kit;
	MidiKitModule* m = kit.m;
	m->model = modelMidiKit;
	m->loadScript(script);
	MidiKitWidget* mw = Test::createWidget<MidiKitWidget>(m);

	rack::ui::Menu* menu = new rack::ui::Menu;
	mw->appendContextMenu(menu);
	m->host.getActiveEngine()->process();
	buildScriptMenuItems(menu);

	rack::ui::MenuItem* item = nullptr;
	for (rack::Widget* child : menu->children) {
		if (auto* mi = dynamic_cast<rack::ui::MenuItem*>(child)) {
			if (mi->text == "Do it") item = mi;
		}
	}
	REQUIRE(item != nullptr);
	REQUIRE(item->rightText == "");     // no checkmark, no submenu arrow

	drainLog(m);
	item->doAction(true);
	item->doAction(true);
	std::string log = drainLog(m);
	size_t n = 0;
	for (size_t at = log.find("did it"); at != std::string::npos; at = log.find("did it", at + 1)) n++;
	REQUIRE(n == 2);

	delete menu;
	Test::destroyWidget(mw);
}

TEST_CASE("Context menu: a file item reads the chosen file and passes it to onChange", "[MidiKit][ContextMenu]") {
	ScriptFileUiMock ui;
	ScriptFileFsMock fs;
	Test::mock::Guard<StoermelderPackOne::vcv::UiAccess> uiGuard{StoermelderPackOne::vcv::uiAccess, &ui};
	Test::mock::Guard<StoermelderPackOne::vcv::FileAccess> fsGuard{StoermelderPackOne::vcv::fileAccess, &fs};

	Pair scripts{QJS_ACTION_FILE, LUA_ACTION_FILE};
	FOR_EACH_LANG;
	const char* script = scripts.get(lang);
	Kit<> kit;
	MidiKitModule* m = kit.m;
	m->model = modelMidiKit;
	m->loadScript(script);
	MidiKitWidget* mw = Test::createWidget<MidiKitWidget>(m);

	rack::ui::Menu* menu = new rack::ui::Menu;
	mw->appendContextMenu(menu);
	m->host.getActiveEngine()->process();
	buildScriptMenuItems(menu);

	rack::ui::MenuItem* item = nullptr;
	for (rack::Widget* child : menu->children) {
		if (auto* mi = dynamic_cast<rack::ui::MenuItem*>(child)) {
			if (mi->text == "Pick file") item = mi;
		}
	}
	REQUIRE(item != nullptr);
	REQUIRE(item->rightText == "");
	drainLog(m);
	ui.dialogs = 0;
	ui.messages.clear();

	// Cancelled dialog: nothing happens.
	ui.chosen = "";
	item->doAction(true);
	REQUIRE(ui.dialogs == 1);
	REQUIRE(drainLog(m).empty());
	REQUIRE(ui.messages.empty());

	// A chosen file: its text and its name arrive.
	ui.chosen = "/some/dir/scale.scl";
	fs.content = "hello\nworld";
	fs.reportedSize = fs.content.size();
	item->doAction(true);
	m->host.getActiveEngine()->process();
	std::string log = drainLog(m);
	CATCH_INFO(log);
	REQUIRE(log.find("got [hello\nworld] from scale.scl") != std::string::npos);

	// Exactly the limit is accepted, one byte more is refused before reading.
	fs.content = std::string(StoermelderPackOne::MidiScript::ScriptMenuItem::fileMaxBytes, 'a');
	fs.reportedSize = fs.content.size();
	item->doAction(true);
	REQUIRE(drainLog(m).find("got [") != std::string::npos);
	REQUIRE(ui.messages.empty());

	fs.content = std::string(StoermelderPackOne::MidiScript::ScriptMenuItem::fileMaxBytes + 1, 'a');
	fs.reportedSize = fs.content.size();
	item->doAction(true);
	REQUIRE(drainLog(m).empty());
	REQUIRE(ui.messages.size() == 1);
	REQUIRE(ui.messages[0].find("scale.scl") != std::string::npos);
	REQUIRE(ui.messages[0].find("8192") != std::string::npos);

	// A file that grew between the size check and the read is refused too.
	ui.messages.clear();
	fs.reportedSize = 10;
	item->doAction(true);
	REQUIRE(drainLog(m).empty());

	// Unreadable file: a message, no call.
	fs.readable = false;
	fs.content = "x";
	fs.reportedSize = 1;
	item->doAction(true);
	REQUIRE(drainLog(m).empty());
	REQUIRE(ui.messages.size() == 1);
	REQUIRE(ui.messages[0].find("Could not read") != std::string::npos);
	fs.readable = true;

	delete menu;
	Test::destroyWidget(mw);
}

// A widget for MultiConfig without a registered model: two of each MIDI port,
// no controls. Enough to build the base's context menu.
struct MultiWidget : MidiKitWidgetBase<MultiConfig> {
	MultiWidget(MultiModule* module) : MidiKitWidgetBase<MultiConfig>(module, "MidiKit") {
		addMidiInputDisplay(0, Rect(Vec(0.f, 36.4f), Vec(180.f, 44.6f)));
		addMidiInputDisplay(1, Rect(Vec(0.f, 81.f), Vec(180.f, 44.6f)));
		addLogDisplay(Rect(Vec(0.f, 126.f), Vec(180.f, 60.f)));
		addMidiOutputDisplay(0, Rect(Vec(0.f, 190.f), Vec(180.f, 44.6f)));
		addMidiOutputDisplay(1, Rect(Vec(0.f, 235.f), Vec(180.f, 44.6f)));
	}
};

// Widgets

static int countMenuEntries(rack::ui::Menu* menu, const std::string& text) {
	int n = 0;
	for (rack::Widget* child : menu->children) {
		if (auto* mi = dynamic_cast<rack::ui::MenuItem*>(child)) {
			if (mi->text == text) n++;
		}
		else if (auto* ml = dynamic_cast<rack::ui::MenuLabel*>(child)) {
			if (ml->text == text) n++;
		}
	}
	return n;
}

// The "Log" submenu of `menu` (owned by the caller), or null without one.
static rack::ui::Menu* createLogSubmenu(rack::ui::Menu* menu) {
	for (rack::Widget* child : menu->children) {
		auto* mi = dynamic_cast<rack::ui::MenuItem*>(child);
		if (mi && mi->text == "Log") return mi->createChildMenu();
	}
	return nullptr;
}

TEST_CASE("Variant: context menu lists every MIDI port once all are enabled", "[MidiKit][Variant][ContextMenu]") {
	Kit<MultiModule> kit;
	MultiModule* m = kit.m;
	m->enableMidiIn(2);
	m->enableMidiOut(2);
	MultiWidget* mw = new MultiWidget(m);

	rack::ui::Menu* menu = new rack::ui::Menu;
	mw->appendContextMenu(menu);
	REQUIRE(countMenuEntries(menu, "MIDI input 1") == 1);
	REQUIRE(countMenuEntries(menu, "MIDI input 2") == 1);
	REQUIRE(countMenuEntries(menu, "MIDI output 1") == 1);
	REQUIRE(countMenuEntries(menu, "MIDI output 2") == 1);
	REQUIRE(countMenuEntries(menu, "MIDI input") == 0);

	delete menu;
	Test::destroyWidget(mw);
}

// Log display context menu

struct ClipboardSpy : StoermelderPackOne::vcv::UiAccess {
	std::string text;
	int sets = 0;
	void setClipboard(const std::string& t) override { text = t; sets++; }
};

// The last entry called `text`: the log display's own "Log" section comes after
// the "Script" section, which has entries of the same names.
static rack::ui::MenuItem* findLastMenuItem(rack::ui::Menu* menu, const std::string& text) {
	rack::ui::MenuItem* found = nullptr;
	for (rack::Widget* child : menu->children) {
		auto* mi = dynamic_cast<rack::ui::MenuItem*>(child);
		if (mi && mi->text == text) found = mi;
	}
	return found;
}

TEST_CASE("Log display context menu offers the Script section", "[MidiKit][LogMenu]") {
	Kit<> kit;
	MidiKitModule* m = kit.m;
	m->model = modelMidiKit;
	MidiKitWidget* mw = Test::createWidget<MidiKitWidget>(m);
	REQUIRE(mw->logDisplay != nullptr);

	rack::ui::Menu* menu = new rack::ui::Menu;
	mw->logDisplay->appendContextMenu(menu);
	REQUIRE(countMenuEntries(menu, "Script") == 1);
	for (const char* name : { "Edit…", "Paste from clipboard", "Load", "Reload", "Save as" }) {
		REQUIRE(countMenuEntries(menu, name) == 1);
	}
	// "Clear" and "Copy to clipboard" exist in both the Script and the Log section.
	REQUIRE(countMenuEntries(menu, "Clear") == 2);
	REQUIRE(countMenuEntries(menu, "Copy to clipboard") == 2);
	delete menu;
	Test::destroyWidget(mw);
}

TEST_CASE("Log display context menu copies the whole log to the clipboard and clears it", "[MidiKit][LogMenu]") {
	Kit<> kit;
	MidiKitModule* m = kit.m;
	m->model = modelMidiKit;
	MidiKitWidget* mw = Test::createWidget<MidiKitWidget>(m);
	REQUIRE(mw->logDisplay != nullptr);

	ClipboardSpy spy;
	Test::mock::Guard<StoermelderPackOne::vcv::UiAccess> uiGuard{StoermelderPackOne::vcv::uiAccess, &spy};

	// Empty log: both entries are there but disabled.
	rack::ui::Menu* menu = new rack::ui::Menu;
	mw->logDisplay->appendContextMenu(menu);
	REQUIRE(findLastMenuItem(menu, "Copy to clipboard") != nullptr);
	REQUIRE(findLastMenuItem(menu, "Clear") != nullptr);
	REQUIRE(findLastMenuItem(menu, "Copy to clipboard")->disabled);
	REQUIRE(findLastMenuItem(menu, "Clear")->disabled);
	delete menu;

	// More lines than the display can show: the copy still has all of them, oldest first.
	m->loadScript(QUICKJS_EMPTY);   // pushes a RESET, clearing older entries
	for (int i = 0; i < 40; i++) m->writeLog("line" + std::to_string(i), false);
	mw->step();
	menu = new rack::ui::Menu;
	mw->logDisplay->appendContextMenu(menu);
	REQUIRE_FALSE(findLastMenuItem(menu, "Copy to clipboard")->disabled);
	findLastMenuItem(menu, "Copy to clipboard")->doAction(false);
	REQUIRE(spy.sets == 1);
	REQUIRE(spy.text.find("line0\n") != std::string::npos);
	REQUIRE(spy.text.find("line39\n") != std::string::npos);
	REQUIRE(spy.text.find("line0\n") < spy.text.find("line39\n"));
	REQUIRE(mw->buffer.size() >= 40);   // Copy leaves the log alone

	findLastMenuItem(menu, "Clear")->doAction(false);
	REQUIRE(mw->buffer.empty());
	mw->logDisplay->step();
	REQUIRE(mw->logDisplay->text.empty());
	delete menu;

	// Nothing left: the entries are disabled again.
	menu = new rack::ui::Menu;
	mw->logDisplay->appendContextMenu(menu);
	REQUIRE(findLastMenuItem(menu, "Copy to clipboard")->disabled);
	REQUIRE(findLastMenuItem(menu, "Clear")->disabled);
	delete menu;

	Test::destroyWidget(mw);
}

TEST_CASE("Variant: context menu lists MIDI ports 2+ only while the script enables them", "[MidiKit][Variant][ContextMenu]") {
	Kit<MultiModule> kit;
	MultiModule* m = kit.m;
	MultiWidget* mw = new MultiWidget(m);   // no registered model, so built directly

	auto count = [&](const std::string& text) {
		rack::ui::Menu* menu = new rack::ui::Menu;
		mw->appendContextMenu(menu);
		int n = countMenuEntries(menu, text);
		delete menu;
		return n;
	};
	REQUIRE(count("MIDI input 1") == 1);
	REQUIRE(count("MIDI output 1") == 1);
	REQUIRE(count("MIDI input 2") == 0);
	REQUIRE(count("MIDI output 2") == 0);

	m->loadScript(JS_PORTS_OUT);   // outputs only
	REQUIRE(count("MIDI input 2") == 0);
	REQUIRE(count("MIDI output 2") == 1);

	m->loadScript(JS_PORT_PROBE);  // inputs only
	REQUIRE(count("MIDI input 2") == 1);
	REQUIRE(count("MIDI output 2") == 0);

	Test::destroyWidget(mw);
}

TEST_CASE("Variant: MidiKit context menu lists only MIDI port 1 by default", "[MidiKit][Variant][ContextMenu]") {
	Kit<> kit;
	MidiKitModule* m = kit.m;
	m->model = modelMidiKit;
	MidiKitWidget* mw = Test::createWidget<MidiKitWidget>(m);

	rack::ui::Menu* menu = new rack::ui::Menu;
	mw->appendContextMenu(menu);
	REQUIRE(countMenuEntries(menu, "MIDI input 1") == 1);
	REQUIRE(countMenuEntries(menu, "MIDI output 1") == 1);
	REQUIRE(countMenuEntries(menu, "MIDI input 2") == 0);   // not enabled by the script
	REQUIRE(countMenuEntries(menu, "MIDI output 2") == 0);
	REQUIRE(countMenuEntries(menu, "Log") == 0);   // only MidiKitMicro has it

	delete menu;
	Test::destroyWidget(mw);
}

TEST_CASE("Variant: MidiKitMicro widget works without a log display", "[MidiKit][Variant][Micro]") {
	Kit<MidiKitMicroModule> kit;
	MidiKitMicroModule* m = kit.m;
	m->model = modelMidiKitMicro;
	MidiKitMicroWidget* mw = Test::createWidget<MidiKitMicroWidget>(m);
	REQUIRE(mw != nullptr);
	REQUIRE(mw->logDisplay == nullptr);

	// The log is drained into the widget, which keeps only the newest five
	// lines for the context menu; stepping must not touch the missing display.
	m->loadScript(QUICKJS_EMPTY);   // pushes a RESET, clearing older entries
	for (int i = 0; i < 8; i++) m->writeLog("line" + std::to_string(i), false);
	mw->step();
	ScriptLog::Entry t;
	REQUIRE_FALSE(m->log.tryPop(t));
	REQUIRE(mw->buffer.size() == 5);

	// The last lines are in the "Log" submenu, newest first.
	rack::ui::Menu* logMenu = new rack::ui::Menu;
	mw->appendContextMenu(logMenu);
	rack::ui::Menu* sub = createLogSubmenu(logMenu);
	REQUIRE(sub != nullptr);
	REQUIRE(sub->children.size() == 5);
	delete sub;
	delete logMenu;

	// Long lines wrap inside a fixed width: same width, but taller than a
	// short line. The default UiAccess measures text without a window.
	StoermelderPackOne::vcv::UiAccess measureFallback;
	Test::mock::Guard<StoermelderPackOne::vcv::UiAccess> uiGuard{StoermelderPackOne::vcv::uiAccess, &measureFallback};
	std::string longText;
	for (int i = 0; i < 60; i++) longText += "word ";
	m->writeLog(longText, false);
	mw->step();
	rack::ui::Menu* longMenu = new rack::ui::Menu;
	mw->appendContextMenu(longMenu);
	sub = createLogSubmenu(longMenu);
	REQUIRE(sub != nullptr);
	REQUIRE(sub->children.size() == 5);
	auto it = sub->children.begin();
	rack::Widget* longLine = *it++;
	rack::Widget* shortLine = *it;
	longLine->step();
	shortLine->step();
	REQUIRE(longLine->box.size.x == shortLine->box.size.x);
	REQUIRE(longLine->box.size.y > shortLine->box.size.y);   // newest is the 400-char line
	delete sub;
	delete longMenu;

	// A reset clears the menu log.
	m->loadScript(QUICKJS_EMPTY);
	mw->step();
	REQUIRE(mw->buffer.size() < 5);
	mw->resetLog();
	REQUIRE(mw->buffer.empty());
	rack::ui::Menu* emptyMenu = new rack::ui::Menu;
	mw->appendContextMenu(emptyMenu);
	sub = createLogSubmenu(emptyMenu);
	REQUIRE(sub != nullptr);
	REQUIRE(sub->children.size() == 1);   // the "(empty)" placeholder
	delete sub;
	delete emptyMenu;

	// It still has the MIDI menu and the base's script menu.
	rack::ui::Menu* menu = new rack::ui::Menu;
	mw->appendContextMenu(menu);
	REQUIRE(countMenuEntries(menu, "MIDI input 1") == 1);
	REQUIRE(countMenuEntries(menu, "MIDI output 1") == 1);
	REQUIRE(countMenuEntries(menu, "MIDI input 2") == 0);
	REQUIRE(countMenuEntries(menu, "MIDI output 2") == 0);
	REQUIRE(countMenuEntries(menu, "Log") == 1);

	delete menu;
	Test::destroyWidget(mw);
}
