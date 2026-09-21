#include "../test/framework.hpp"
#include "Keymap.hpp"

void testPluginInit(rack::Plugin* p) {
	pluginInstance = p;
}

using namespace StoermelderPackOne;

// path -> contents; a missing key means "cannot open". Mirrors MockFileAccess in
// src/vcv/files.test.cpp, extended with write()/exists()/createDirectories() so Keymap's
// persistence path can be exercised without touching real disk.
struct MockFileAccess : vcv::FileAccess {
	std::map<std::string, std::string> files;
	std::vector<std::string> writes;   // paths written, in order (for write-count assertions)
	bool denyWrites = false;

	bool read(const std::string& path, std::string& data) const override {
		auto it = files.find(path);
		if (it == files.end()) return false;
		data = it->second;
		return true;
	}
	bool write(const std::string& path, const std::string& data) override {
		if (denyWrites) return false;
		files[path] = data;
		writes.push_back(path);
		return true;
	}
	bool exists(const std::string& path) const override {
		return files.find(path) != files.end();
	}
	std::string join(const std::string& path1, const std::string& path2) override {
		if (path1.empty()) return path2;
		return path1 + "/" + path2;
	}
	std::string getUserDirectory(const std::string& path) override { return "USER/" + path; }
	bool createDirectories(const std::string&) override { return true; }
};

struct Mock {
	TEST_MOCK_FS(MockFileAccess);
};

// Installs the FS mock and resets the Keymaps registry, so each TEST_CASE starts with a clean
// slate (Keymaps::open() is process-wide and would otherwise leak a cached Keymap across
// TEST_CASEs sharing the same slug).
struct Fixture {
	Mock mock;
	Fixture() { Keymaps::resetForTest(); }
	~Fixture() { Keymaps::resetForTest(); }
};

static const char* SLUG = "TestModule";


// KeyCombo grammar

TEST_CASE("KeyCombo round-trips every table entry through toString/parse", "[Keymap]") {
	const char* specs[] = {
		"Space", "Escape", "Enter", "Tab", "Backspace", "Up", "Down", "Left", "Right",
		"F1", "F12", "A", "Z", "0", "9", "=", "-", "/", ";", "'",
	};
	for (const char* spec : specs) {
		KeyCombo c(spec);
		REQUIRE(c.valid());
		KeyCombo roundTrip(c.toString().c_str());
		REQUIRE(roundTrip.valid());
		CHECK(roundTrip.key == c.key);
		CHECK(roundTrip.mods == c.mods);
	}
}

TEST_CASE("KeyCombo mod parsing is case- and order-insensitive", "[Keymap]") {
	KeyCombo a("Ctrl+Shift+Z");
	KeyCombo b("shift+ctrl+z");
	KeyCombo c("SHIFT+CTRL+Z");
	REQUIRE(a.valid());
	CHECK(a.key == b.key);
	CHECK(a.mods == b.mods);
	CHECK(a.key == c.key);
	CHECK(a.mods == c.mods);
}

TEST_CASE("KeyCombo Cmd/Command/Super normalise to Ctrl", "[Keymap]") {
	KeyCombo ctrl("Ctrl+Z");
	KeyCombo cmd("Cmd+Z");
	KeyCombo command("Command+Z");
	KeyCombo super("Super+Z");
	REQUIRE(ctrl.valid());
	CHECK(cmd.mods == ctrl.mods);
	CHECK(command.mods == ctrl.mods);
	CHECK(super.mods == ctrl.mods);
	CHECK(ctrl.toString() == "Ctrl+Z");
	CHECK(cmd.toString() == "Ctrl+Z");
}

TEST_CASE("KeyCombo::toString() is always the portable \"Ctrl\" spelling, regardless of platform", "[Keymap]") {
	// toString() feeds the keymap JSON file (Keymap::save()): it must stay platform-neutral so
	// a file saved on one machine reads back identically on another, matching the grammar's own
	// Cmd/Command/Super -> Ctrl normalisation on write.
	KeyCombo c("Ctrl+Shift+Alt+Z");
	CHECK(c.toString() == "Ctrl+Shift+Alt+Z");
}

TEST_CASE("KeyCombo::displayString() uses RACK_MOD_CTRL_NAME, not a hardcoded \"Ctrl\"", "[Keymap]") {
	// displayString() is for menus/tooltips only, never the file - RACK_MOD_CTRL_NAME is "⌘" on
	// macOS and "Ctrl" elsewhere (Rack/include/widget/event.hpp); RACK_MOD_SHIFT_NAME and
	// RACK_MOD_ALT_NAME are "Shift"/"Alt" on every platform, but still routed through the same
	// constants rather than literals, so a future platform-specific rename doesn't need finding
	// twice.
	KeyCombo c("Ctrl+Shift+Alt+Z");
	CHECK(c.displayString() == RACK_MOD_CTRL_NAME "+" RACK_MOD_SHIFT_NAME "+" RACK_MOD_ALT_NAME "+Z");
}

TEST_CASE("KeyCombo rejects garbage", "[Keymap]") {
	CHECK_FALSE(KeyCombo("").valid());
	CHECK_FALSE(KeyCombo("Nonsense").valid());
	CHECK_FALSE(KeyCombo("Ctrl+").valid());
	CHECK_FALSE(KeyCombo("Ctrl+Nonsense").valid());
	CHECK_FALSE(KeyCombo("Frobnicate+Z").valid());
}

TEST_CASE("KeyCombo accepts keyboard.hpp-style aliases", "[Keymap]") {
	CHECK(KeyCombo("ESC").key == GLFW_KEY_ESCAPE);
	CHECK(KeyCombo("BS").key == GLFW_KEY_BACKSPACE);
}


// KeyCombo::matches

TEST_CASE("KeyCombo::matches masks a stray CapsLock bit", "[Keymap]") {
	KeyCombo c("Ctrl+Z");
	CHECK(c.matches(GLFW_KEY_Z, RACK_MOD_CTRL));
	CHECK(c.matches(GLFW_KEY_Z, RACK_MOD_CTRL | GLFW_MOD_CAPS_LOCK));
}

TEST_CASE("KeyCombo::matches treats keypad digits as their number-row equivalent", "[Keymap]") {
	KeyCombo c("1");
	CHECK(c.matches(GLFW_KEY_KP_1, 0));
	CHECK(c.matches(GLFW_KEY_1, 0));
}

TEST_CASE("KeyCombo::matches distinguishes Shift+M from bare M", "[Keymap]") {
	KeyCombo shiftM("Shift+M");
	KeyCombo bareM("M");
	CHECK(shiftM.matches(GLFW_KEY_M, GLFW_MOD_SHIFT));
	CHECK_FALSE(shiftM.matches(GLFW_KEY_M, 0));
	CHECK(bareM.matches(GLFW_KEY_M, 0));
	CHECK_FALSE(bareM.matches(GLFW_KEY_M, GLFW_MOD_SHIFT));
}


// Create-on-first-use / load

TEST_CASE("first use with no file writes registered defaults to disk", "[Keymap]") {
	Fixture f;
	auto km = Keymaps::open(SLUG);
	km->registerAction("a.one", "One", "Group", "1");
	km->registerAction("a.two", "Two", "Group", "Ctrl+Z");
	km->save();

	std::string path = Keymaps::pathFor(SLUG);
	REQUIRE(f.mock.fs.exists(path));

	std::string data;
	REQUIRE(f.mock.fs.read(path, data));
	std::string error;
	json_t* root = vcv::parseJson(data, error);
	REQUIRE(root != nullptr);
	json_t* bindingsJ = json_object_get(root, "bindings");
	REQUIRE(bindingsJ != nullptr);
	CHECK(std::string(json_string_value(json_object_get(bindingsJ, "a.one"))) == "1");
	CHECK(std::string(json_string_value(json_object_get(bindingsJ, "a.two"))) == "Ctrl+Z");
	json_decref(root);
}

TEST_CASE("a pre-seeded file with a non-default binding is honoured and not rewritten", "[Keymap]") {
	Fixture f;
	std::string path = Keymaps::pathFor(SLUG);
	f.mock.fs.files[path] = R"({"slug":"TestModule","version":1,"bindings":{"a.one":"Q"}})";

	auto km = Keymaps::open(SLUG);
	km->registerAction("a.one", "One", "Group", "1");
	km->save();

	CHECK(km->lookup(GLFW_KEY_Q, 0, GLFW_PRESS) == "a.one");
	CHECK(km->lookup(GLFW_KEY_1, 0, GLFW_PRESS) == "");
	CHECK(f.mock.fs.writes.empty());
}


// No-default registration

TEST_CASE("the no-default overload leaves an action unmapped", "[Keymap]") {
	Fixture f;
	auto km = Keymaps::open(SLUG);
	km->registerAction("a.one", "One", "Group");
	km->save();

	CHECK(km->has("a.one"));
	CHECK(km->shortcutText("a.one") == "");
	CHECK(km->combosFor("a.one").empty());
}

TEST_CASE("a no-default action already recorded null in the file is not rewritten", "[Keymap]") {
	Fixture f;
	std::string path = Keymaps::pathFor(SLUG);
	f.mock.fs.files[path] = R"({"slug":"TestModule","version":1,"bindings":{"a.none":null}})";

	auto km = Keymaps::open(SLUG);
	km->registerAction("a.none", "None", "Group");
	km->save();

	CHECK(km->shortcutText("a.none") == "");
	CHECK(f.mock.fs.writes.empty());
}

TEST_CASE("a no-default action is written as a null binding", "[Keymap]") {
	Fixture f;
	auto km = Keymaps::open(SLUG);
	km->registerAction("a.one", "One", "Group", "1");   // forces a rewrite so save() runs
	km->registerAction("a.none", "None", "Group");
	km->save();

	std::string path = Keymaps::pathFor(SLUG);
	std::string data;
	REQUIRE(f.mock.fs.read(path, data));
	std::string error;
	json_t* root = vcv::parseJson(data, error);
	REQUIRE(root != nullptr);
	json_t* bindingsJ = json_object_get(root, "bindings");
	json_t* noneJ = json_object_get(bindingsJ, "a.none");
	REQUIRE(noneJ != nullptr);
	CHECK(json_is_null(noneJ));
	json_decref(root);
}

TEST_CASE("a no-default action can still be bound by the user and reset back to unmapped", "[Keymap]") {
	Fixture f;
	auto km = Keymaps::open(SLUG);
	km->registerAction("a.none", "None", "Group");

	km->bind("a.none", KeyCombo("Q"));
	CHECK(km->lookup(GLFW_KEY_Q, 0, GLFW_PRESS) == "a.none");

	km->resetAction("a.none");
	CHECK(km->lookup(GLFW_KEY_Q, 0, GLFW_PRESS) == "");
	CHECK(km->shortcutText("a.none") == "");

	km->bind("a.none", KeyCombo("Q"));
	km->resetToDefaults();
	CHECK(km->lookup(GLFW_KEY_Q, 0, GLFW_PRESS) == "");
}

TEST_CASE("re-registering a no-default action is idempotent and does not assert", "[Keymap]") {
	Fixture f;
	auto km = Keymaps::open(SLUG);
	km->registerAction("a.none", "None", "Group");
	km->bind("a.none", KeyCombo("Q"));

	// Second widget re-registers the same vocabulary; must not reset the user's binding or trip
	// the disagreement assert in registerAction().
	km->registerAction("a.none", "None", "Group");
	CHECK(km->lookup(GLFW_KEY_Q, 0, GLFW_PRESS) == "a.none");
}

TEST_CASE("reload() preserves a no-default action instead of dropping it", "[Keymap]") {
	Fixture f;
	auto km = Keymaps::open(SLUG);
	km->registerAction("a.none", "None", "Group");
	CHECK(km->has("a.none"));

	Keymaps::reload(SLUG);

	// Same shared_ptr; the no-default action must still be registered after the reload rebuild.
	CHECK(km->has("a.none"));
	CHECK(km->shortcutText("a.none") == "");
}


// Sharing / re-registration

TEST_CASE("a second open() for the same slug returns the same instance", "[Keymap]") {
	Fixture f;
	auto km1 = Keymaps::open(SLUG);
	km1->registerAction("a.one", "One", "Group", "1");
	auto km2 = Keymaps::open(SLUG);
	CHECK(km1 == km2);
}

TEST_CASE("re-registration after a rebind does not reset the changed binding", "[Keymap]") {
	Fixture f;
	auto km = Keymaps::open(SLUG);
	km->registerAction("a.one", "One", "Group", "1");
	km->bind("a.one", KeyCombo("Q"));

	// Second widget re-registers the same vocabulary.
	km->registerAction("a.one", "One", "Group", "1");

	CHECK(km->lookup(GLFW_KEY_Q, 0, GLFW_PRESS) == "a.one");
	CHECK(km->lookup(GLFW_KEY_1, 0, GLFW_PRESS) == "");
}


// Merge behaviour

TEST_CASE("an id newly added to the vocabulary takes its default and the file is rewritten", "[Keymap]") {
	Fixture f;
	std::string path = Keymaps::pathFor(SLUG);
	f.mock.fs.files[path] = R"({"slug":"TestModule","version":1,"bindings":{"a.one":"Q"}})";

	auto km = Keymaps::open(SLUG);
	km->registerAction("a.one", "One", "Group", "1");
	km->registerAction("a.two", "Two", "Group", "2");   // new since the file was written
	km->save();

	CHECK(km->lookup(GLFW_KEY_2, 0, GLFW_PRESS) == "a.two");
	REQUIRE_FALSE(f.mock.fs.writes.empty());
}

TEST_CASE("a null binding in the file stays unbound and triggers no rewrite", "[Keymap]") {
	Fixture f;
	std::string path = Keymaps::pathFor(SLUG);
	f.mock.fs.files[path] = R"({"slug":"TestModule","version":1,"bindings":{"a.one":null}})";

	auto km = Keymaps::open(SLUG);
	km->registerAction("a.one", "One", "Group", "1");
	km->save();

	CHECK(km->lookup(GLFW_KEY_1, 0, GLFW_PRESS) == "");
	CHECK(km->shortcutText("a.one") == "");
	CHECK(f.mock.fs.writes.empty());
}

TEST_CASE("an id nobody registers is preserved verbatim across a write", "[Keymap]") {
	Fixture f;
	std::string path = Keymaps::pathFor(SLUG);
	f.mock.fs.files[path] = R"({"slug":"TestModule","version":1,"bindings":{"a.one":"Q","a.ghost":"G"}})";

	auto km = Keymaps::open(SLUG);
	km->registerAction("a.one", "One", "Group", "1");
	km->registerAction("a.new", "New", "Group", "N");   // forces a rewrite
	km->save();

	std::string data;
	REQUIRE(f.mock.fs.read(path, data));
	std::string error;
	json_t* root = vcv::parseJson(data, error);
	REQUIRE(root != nullptr);
	json_t* bindingsJ = json_object_get(root, "bindings");
	json_t* ghostJ = json_object_get(bindingsJ, "a.ghost");
	REQUIRE(ghostJ != nullptr);
	CHECK(std::string(json_string_value(ghostJ)) == "G");
	json_decref(root);
}


// Corrupt file / read-only FS

TEST_CASE("a corrupt file falls back to defaults in memory and is left untouched", "[Keymap]") {
	Fixture f;
	std::string path = Keymaps::pathFor(SLUG);
	f.mock.fs.files[path] = "{ not json";

	auto km = Keymaps::open(SLUG);
	km->registerAction("a.one", "One", "Group", "1");
	km->save();

	CHECK(km->lookup(GLFW_KEY_1, 0, GLFW_PRESS) == "a.one");
	CHECK(f.mock.fs.writes.empty());
	// The original (corrupt) contents are exactly as before.
	CHECK(f.mock.fs.files[path] == "{ not json");
}

TEST_CASE("a read-only filesystem still yields a usable keymap", "[Keymap]") {
	Fixture f;
	f.mock.fs.denyWrites = true;

	auto km = Keymaps::open(SLUG);
	km->registerAction("a.one", "One", "Group", "1");
	km->save();   // should not throw/crash despite write() returning false

	CHECK(km->lookup(GLFW_KEY_1, 0, GLFW_PRESS) == "a.one");
}


// Conflicts / reset

TEST_CASE("conflictsFor finds the shadowed action and lookup favours registration order", "[Keymap]") {
	Fixture f;
	auto km = Keymaps::open(SLUG);
	km->registerAction("a.one", "One", "Group", "Q");
	km->registerAction("a.two", "Two", "Group", "W");
	km->bind("a.two", KeyCombo("Q"));   // now shadows a.one

	auto conflicts = km->conflictsFor(KeyCombo("Q"), "a.two");
	REQUIRE(conflicts.size() == 1);
	CHECK(conflicts[0] == "a.one");

	// First-match-wins in registration order: a.one was registered first.
	CHECK(km->lookup(GLFW_KEY_Q, 0, GLFW_PRESS) == "a.one");
}

TEST_CASE("resetAction and resetToDefaults restore and mark the file for rewrite", "[Keymap]") {
	Fixture f;
	auto km = Keymaps::open(SLUG);
	km->registerAction("a.one", "One", "Group", "1");
	km->save();
	f.mock.fs.writes.clear();

	km->bind("a.one", KeyCombo("Q"));
	km->resetAction("a.one");
	CHECK(km->lookup(GLFW_KEY_1, 0, GLFW_PRESS) == "a.one");

	km->bind("a.one", KeyCombo("Q"));
	km->resetToDefaults();
	CHECK(km->lookup(GLFW_KEY_1, 0, GLFW_PRESS) == "a.one");
}


// Registration contract

TEST_CASE("save() is idempotent: two calls with no change in between write once", "[Keymap]") {
	Fixture f;
	auto km = Keymaps::open(SLUG);
	km->registerAction("a.one", "One", "Group", "1");
	km->save();
	km->save();
	CHECK(f.mock.fs.writes.size() == 1);
}

TEST_CASE("save() on a keymap loaded clean from an existing file does not write", "[Keymap]") {
	Fixture f;
	std::string path = Keymaps::pathFor(SLUG);
	f.mock.fs.files[path] = R"({"slug":"TestModule","version":1,"bindings":{"a.one":"1"}})";

	auto km = Keymaps::open(SLUG);
	km->registerAction("a.one", "One", "Group", "1");
	km->save();
	CHECK(f.mock.fs.writes.empty());
}


// ---- Auto-repeat ---------------------------------------------------------------------------------

TEST_CASE("a GLFW_PRESS-only action does not fire on repeat", "[Keymap]") {
	Fixture f;
	auto km = Keymaps::open(SLUG);
	km->registerAction("a.one", "One", "Group", "1");   // default trigger: GLFW_PRESS
	CHECK(km->lookup(GLFW_KEY_1, 0, GLFW_PRESS) == "a.one");
	CHECK(km->lookup(GLFW_KEY_1, 0, GLFW_REPEAT) == "");
}

TEST_CASE("a GLFW_REPEAT action matches both press and repeat", "[Keymap]") {
	Fixture f;
	auto km = Keymaps::open(SLUG);
	km->registerAction("cursor.up", "Up", "Group", "Up", GLFW_REPEAT);
	CHECK(km->lookup(GLFW_KEY_UP, 0, GLFW_PRESS) == "cursor.up");
	CHECK(km->lookup(GLFW_KEY_UP, 0, GLFW_REPEAT) == "cursor.up");
}

TEST_CASE("GLFW_RELEASE never matches anything", "[Keymap]") {
	Fixture f;
	auto km = Keymaps::open(SLUG);
	km->registerAction("cursor.up", "Up", "Group", "Up", GLFW_REPEAT);
	CHECK(km->lookup(GLFW_KEY_UP, 0, GLFW_RELEASE) == "");
}

TEST_CASE("RACK_HELD never matches anything, even a GLFW_REPEAT action", "[Keymap]") {
	// Regression test: RACK_HELD (Rack/include/widget/event.hpp) is Rack's own held-key
	// repeat, synthesised once per hover frame for every currently-held key
	// (EventState::handleHover) - with no delay of its own, so it can arrive on the very next
	// frame after a genuine GLFW_PRESS. A dispatcher that treated it as an ordinary match (or
	// even as equivalent to GLFW_REPEAT) would fire again immediately after a single tap,
	// before the key is ever actually held down for any perceptible time - "pressing the
	// cursor key is immediately handled as repeat". No dispatcher in this plugin has ever
	// acted on RACK_HELD; Keymap must not start now, for GLFW_PRESS-only actions or
	// GLFW_REPEAT ones alike.
	Fixture f;
	auto km = Keymaps::open(SLUG);
	km->registerAction("a.one", "One", "Group", "1");                    // GLFW_PRESS trigger
	km->registerAction("cursor.up", "Up", "Group", "Up", GLFW_REPEAT);   // GLFW_REPEAT trigger

	CHECK(km->lookup(GLFW_KEY_1, 0, RACK_HELD) == "");
	CHECK(km->lookup(GLFW_KEY_UP, 0, RACK_HELD) == "");
}


// ---- Reload ----------------------------------------------------------------------------------

TEST_CASE("reload() picks up a hand-edit without invalidating existing shared_ptrs", "[Keymap]") {
	Fixture f;
	auto km = Keymaps::open(SLUG);
	km->registerAction("a.one", "One", "Group", "1");
	CHECK(km->lookup(GLFW_KEY_1, 0, GLFW_PRESS) == "a.one");

	std::string path = Keymaps::pathFor(SLUG);
	f.mock.fs.files[path] = R"({"slug":"TestModule","version":1,"bindings":{"a.one":"Q"}})";
	Keymaps::reload(SLUG);

	// Same shared_ptr the widget already holds now reflects the edit.
	CHECK(km->lookup(GLFW_KEY_Q, 0, GLFW_PRESS) == "a.one");
	CHECK(km->lookup(GLFW_KEY_1, 0, GLFW_PRESS) == "");
}


// KeymapHandlers

TEST_CASE("a scoped handler runs only when its predicate passes", "[Keymap]") {
	Fixture f;
	auto km = Keymaps::open(SLUG);
	km->registerAction("a.one", "One", "Group", "1");
	KeymapHandlers h(km);

	bool enabled = false;
	int fired = 0;
	h.on("a.one", [&]{ return enabled; }, [&]{ fired++; });

	CHECK_FALSE(h.dispatch(GLFW_KEY_1, 0, GLFW_PRESS));
	CHECK(fired == 0);

	enabled = true;
	CHECK(h.dispatch(GLFW_KEY_1, 0, GLFW_PRESS));
	CHECK(fired == 1);
}

TEST_CASE("an unscoped handler cannot shadow a scoped one, regardless of registration order", "[Keymap]") {
	Fixture f;
	auto km = Keymaps::open(SLUG);
	km->registerAction("bank.toggle", "Toggle", "Group", "Tab");
	KeymapHandlers h(km);

	int unscopedFired = 0, scopedFired = 0;
	bool bankOpen = true;
	// Unscoped registered FIRST — the stuck-Tab regression this design guards against.
	h.on("bank.toggle", [&]{ unscopedFired++; });
	h.on("bank.toggle", [&]{ return bankOpen; }, [&]{ scopedFired++; });

	CHECK(h.dispatch(GLFW_KEY_TAB, 0, GLFW_PRESS));
	CHECK(scopedFired == 1);
	CHECK(unscopedFired == 0);

	bankOpen = false;
	CHECK(h.dispatch(GLFW_KEY_TAB, 0, GLFW_PRESS));
	CHECK(unscopedFired == 1);
}

TEST_CASE("onTry declining passes to the next handler; returning true stops the walk", "[Keymap]") {
	Fixture f;
	auto km = Keymaps::open(SLUG);
	km->registerAction("node.edit", "Edit", "Group", "Enter");
	KeymapHandlers h(km);

	bool canEdit = false;
	int tryFired = 0, fallbackFired = 0;
	h.onTry("node.edit", [&]() -> bool {
		tryFired++;
		if (!canEdit) return false;
		return true;
	});
	h.on("node.edit", [&]{ fallbackFired++; });

	CHECK(h.dispatch(GLFW_KEY_ENTER, 0, GLFW_PRESS));
	CHECK(tryFired == 1);
	CHECK(fallbackFired == 1);   // decline falls through to the unscoped handler

	canEdit = true;
	CHECK(h.dispatch(GLFW_KEY_ENTER, 0, GLFW_PRESS));
	CHECK(tryFired == 2);
	CHECK(fallbackFired == 1);   // accept stops the walk
}

TEST_CASE("dispatch on an unbound key returns false", "[Keymap]") {
	Fixture f;
	auto km = Keymaps::open(SLUG);
	km->registerAction("a.one", "One", "Group", "1");
	KeymapHandlers h(km);
	int fired = 0;
	h.on("a.one", [&]{ fired++; });

	CHECK_FALSE(h.dispatch(GLFW_KEY_Z, 0, GLFW_PRESS));
	CHECK(fired == 0);
}


// Exclusive scope (modal picker: swallow everything while open)

TEST_CASE("an exclusive scope's own handler still runs normally when its predicate passes", "[Keymap]") {
	Fixture f;
	auto km = Keymaps::open(SLUG);
	km->registerAction("a.one", "One", "Group", "1");
	KeymapHandlers h(km);
	int fired = 0;
	auto picker = h.scope([]{ return true; }, /* exclusive */ true);
	picker.on("a.one", [&]{ fired++; });

	CHECK(h.dispatch(GLFW_KEY_1, 0, GLFW_PRESS));
	CHECK(fired == 1);
}

TEST_CASE("an exclusive scope swallows an id it doesn't bind, unlike an ordinary scope", "[Keymap]") {
	Fixture f;
	auto km = Keymaps::open(SLUG);
	km->registerAction("a.one", "One", "Group", "1");
	km->registerAction("a.two", "Two", "Group", "2");
	KeymapHandlers h(km);
	int globalFired = 0;
	bool pickerOpen = true;
	auto picker = h.scope([&]{ return pickerOpen; }, /* exclusive */ true);
	picker.on("a.one", [&]{});   // binds a.one only
	h.on("a.two", [&]{ globalFired++; });   // unscoped, would otherwise fire for a.two

	// a.two has no picker-scoped handler, but the picker is exclusive: dispatch must still
	// report it as handled and must NOT run the unscoped global handler.
	CHECK(h.dispatch(GLFW_KEY_2, 0, GLFW_PRESS));
	CHECK(globalFired == 0);

	// Once the picker closes, the same key reaches the global handler again.
	pickerOpen = false;
	CHECK(h.dispatch(GLFW_KEY_2, 0, GLFW_PRESS));
	CHECK(globalFired == 1);
}

TEST_CASE("an exclusive scope swallows even a completely unbound key", "[Keymap]") {
	Fixture f;
	auto km = Keymaps::open(SLUG);
	km->registerAction("a.one", "One", "Group", "1");
	KeymapHandlers h(km);
	auto picker = h.scope([]{ return true; }, /* exclusive */ true);
	picker.on("a.one", [&]{});

	// Z is not bound to anything at all, yet the exclusive scope's predicate is active.
	CHECK(h.dispatch(GLFW_KEY_Z, 0, GLFW_PRESS));
}

TEST_CASE("dispatch behaves like an ordinary (non-exclusive) scope when its predicate is false", "[Keymap]") {
	Fixture f;
	auto km = Keymaps::open(SLUG);
	km->registerAction("a.two", "Two", "Group", "2");
	KeymapHandlers h(km);
	int globalFired = 0;
	auto picker = h.scope([]{ return false; }, /* exclusive */ true);
	picker.on("a.two", [&]{});
	h.on("a.two", [&]{ globalFired++; });

	CHECK(h.dispatch(GLFW_KEY_2, 0, GLFW_PRESS));
	CHECK(globalFired == 1);
}