#pragma once
#include "../plugin.hpp"
#include "keyboard.hpp"
#include <functional>
#include <vector>
#include <map>
#include <memory>
#include <mutex>

// A reusable, per-module-slug keyboard mapping layer.
//
//   - A module registers its actions (id, label, group, default KeyCombo) into a process-wide
//     Keymap, shared across every instance of that module via the Keymaps registry.
//   - The Keymap persists bindings as human-editable JSON under
//     <user dir>/Stoermelder-P1/keymaps/<slug>.json, created on first use.
//   - A module's per-widget behaviour lives in a separate KeymapHandlers, so nothing capturing
//     a widget is ever stored in the shared, process-wide Keymap.
//
// GUI-thread-only throughout; never reachable from process().

namespace StoermelderPackOne {

// KeyCombo: one physical key binding

struct KeyCombo {
	int key = GLFW_KEY_UNKNOWN;   // GLFW key code, already keyFix()'d
	int mods = 0;                 // GLFW_MOD_* mask, RACK_MOD_MASK-normalised

	KeyCombo() {}
	KeyCombo(int key, int mods = 0) : key(key), mods(mods) {}
	// Implicit, so `registerAction(..., "Ctrl+Z")` just works. Parses the combo grammar:
	// [Mod+]*KeyName, mods in any order/case, "Cmd"/"Command"/"Super" normalise to
	// RACK_MOD_CTRL. An unparseable spec yields an invalid() combo (key == GLFW_KEY_UNKNOWN).
	KeyCombo(const char* spec);
	KeyCombo(const std::string& spec) : KeyCombo(spec.c_str()) {}

	bool valid() const { return key != GLFW_KEY_UNKNOWN; }
	bool operator==(const KeyCombo& o) const { return key == o.key && mods == o.mods; }

	// Compares against an incoming key event: masks eventMods to RACK_MOD_MASK and runs
	// eventKey through keyFix(), so KP_1 matches a "1" binding and a stray
	// GLFW_MOD_CAPS_LOCK bit never breaks a match.
	bool matches(int eventKey, int eventMods) const;

	// Canonical, portable spelling ("Ctrl+Shift+Z") — round-trips through the parser and is
	// what gets written to the keymap file. Always "Ctrl", even on macOS, so a file is portable
	// across platforms. Use displayString() for anything shown on screen.
	std::string toString() const;

	// Platform-flavoured spelling for menus/tooltips (RACK_MOD_CTRL_NAME: "⌘" on macOS, "Ctrl"
	// elsewhere). Never fed back into the parser or the file.
	std::string displayString() const;
};

// Key-name table used by the combo grammar, covering the full GLFW range with title-case,
// file-friendly names ("Space", "Escape", "Backspace", "Up") distinct from keyboard.hpp's
// keyName() (all-caps, built for Stroke's compact panel display — left untouched). Exposed
// for the menu / key-capture widget as well as the parser.
std::string comboKeyName(int key);
// Accepts both comboKeyName() spellings and keyName()'s (so "ESC"/"BS" also parse). Returns
// GLFW_KEY_UNKNOWN on no match.
int parseKeyName(const std::string& name);


// Keymap: register, look up, persist one module-slug's vocabulary

struct Keymap {
	Keymap() {}
	~Keymap();
	// Not copyable (owns a json_t*); Keymaps::reload() replaces contents in place via assign(),
	// not copy-assignment.
	Keymap(const Keymap&) = delete;
	Keymap& operator=(const Keymap&) = delete;

	struct Action {
		std::string id;
		std::string label;
		std::string group;
		int trigger = GLFW_PRESS;             // GLFW_PRESS or GLFW_REPEAT
		std::vector<KeyCombo> defaults;
		std::vector<KeyCombo> combos;         // current bindings; empty = unbound
	};

	// ---- registration (GUI thread) ----
	// Idempotent: re-registering an already-known id is a no-op that leaves the current
	// binding alone. In debug, asserts if label/group/trigger/default disagree with
	// the first registration.
	void registerAction(const std::string& id, const std::string& label, const std::string& group,
	                     KeyCombo defaultCombo, int trigger = GLFW_PRESS);

	// A second default combo for an already-registered action (genuine aliases only).
	void registerAlias(const std::string& id, KeyCombo defaultCombo);

	bool has(const std::string& id) const;

	// ---- lookup, called from KeymapHandlers::dispatch ----
	// Returns the bound action's id, or "" if the key press is unbound. `action` is the
	// GLFW_PRESS / GLFW_REPEAT of the event; a repeat for an action not registered with
	// GLFW_REPEAT returns "". First-match-wins in registration order.
	const std::string& lookup(int key, int mods, int action) const;

	// ---- reverse lookup, for menus and on-screen help ----
	std::vector<KeyCombo> combosFor(const std::string& id) const;
	// Display text via KeyCombo::displayString() ("⌘+Z", or "Tab  Shift+Tab" for aliases).
	// Menus/tooltips only; never write this to the keymap file.
	std::string shortcutText(const std::string& id) const;

	const std::vector<Action>& actions() const { return actions_; }

	// ---- mutation (from the context menu), each marks the keymap stale so save() writes ----
	void bind(const std::string& id, KeyCombo);        // replaces that action's combos
	void addBinding(const std::string& id, KeyCombo);
	void unbind(const std::string& id);
	void resetAction(const std::string& id);
	void resetToDefaults();

	// Other action ids currently bound to `combo` (for conflict warnings in the menu).
	std::vector<std::string> conflictsFor(KeyCombo combo, const std::string& excludeId) const;

	// Writes the file if registration or a mutation changed anything since the last write.
	// Idempotent: calling twice with no change in between writes once.
	void save();

	// ---- internal: consulted by Keymaps::open()/reload(), not part of the module-facing API ----
	void setSlug(const std::string& slug) { slug_ = slug; }
	// `root` is nullptr when no file exists yet. `frozen` means a file exists but failed to
	// parse: keep defaults in memory without ever rewriting it. Borrows `root`; anything kept
	// (the "unknown ids" slice) is deep-copied.
	void loadParsed(json_t* root, bool frozen = false);
	bool dirty() const { return dirty_; }
	// Replaces this keymap's contents with `other`'s, in place, so shared_ptr holders (widgets
	// that called Keymaps::open() earlier) see the reload without re-fetching the pointer.
	void assign(Keymap&& other);

private:
	Action* find(const std::string& id);
	const Action* find(const std::string& id) const;
	void markDirty() { dirty_ = true; }

	std::string slug_;
	std::vector<Action> actions_;
	// Parsed file contents, consulted once per id at registration time, and kept so save() can
	// preserve ids nobody registered (an "unknown" section, written back verbatim).
	std::map<std::string, std::vector<KeyCombo>> parsedBound_;
	std::map<std::string, bool> parsedNull_;      // ids explicitly bound to JSON null
	json_t* parsedUnknownJ_ = nullptr;             // owned deep copy of the file's "bindings" object; see loadParsed()
	// True when the file existed but failed to parse: save() must not overwrite it, so a
	// user's typo stays recoverable by hand-fixing the file.
	bool frozen_ = false;
	bool dirty_ = false;

	static const std::string EMPTY;
};


// Keymaps: the process-wide registry

namespace Keymaps {

// The shared Keymap for this slug, reading the file on the first call for that slug. Never
// returns null: a failed read falls back to registered defaults in memory, so the
// module stays usable on a read-only filesystem.
std::shared_ptr<Keymap> open(const std::string& slug);

// Re-reads from disk, keeping the registered vocabulary — for a "Reload keymap" menu item.
void reload(const std::string& slug);

std::string directory();                     // <user dir>/Stoermelder-P1/keymaps
std::string pathFor(const std::string& slug);

// Test-only: drops every cached Keymap so the next open() re-reads from (mocked) disk.
void resetForTest();

} // namespace Keymaps


// KeymapHandlers: per-widget behaviour

struct KeymapHandlers {
	typedef std::function<void()> Handler;      // the common case
	typedef std::function<bool()> Predicate;
	typedef std::function<bool()> TryHandler;   // the escape hatch: return false to decline and let the next handler try

	explicit KeymapHandlers(std::shared_ptr<Keymap> km) : keymap(km) {}

	void on(const std::string& id, Handler h) { on(id, Predicate(), std::move(h)); }
	void on(const std::string& id, Predicate when, Handler h);
	void onTry(const std::string& id, TryHandler h) { onTry(id, Predicate(), std::move(h)); }
	void onTry(const std::string& id, Predicate when, TryHandler h);

	// Runs the first eligible handler for the key: predicated entries first (registration
	// order), then unscoped ones as fallback, so a scoped handler is never shadowed. Returns
	// true if handled — or if an exclusive scope's predicate is active (see scope()), since
	// that scope owns the keyboard outright and must swallow the event either way.
	bool dispatch(int key, int mods, int action) const;

	// Block registration with a shared predicate. `exclusive` means this scope owns the
	// keyboard outright while its predicate holds (a modal picker), so dispatch() never falls
	// through to an unscoped handler for an id this scope doesn't itself bind.
	struct Scope {
		KeymapHandlers* owner;
		Predicate when;
		void on(const std::string& id, Handler h) const { owner->on(id, when, std::move(h)); }
		void onTry(const std::string& id, TryHandler h) const { owner->onTry(id, when, std::move(h)); }
	};
	Scope scope(Predicate when, bool exclusive = false) {
		if (exclusive) exclusiveGates_.push_back(when);
		return Scope{this, std::move(when)};
	}

	std::shared_ptr<Keymap> keymap;

private:
	struct Entry {
		std::string id;
		Predicate when;      // empty = unscoped
		TryHandler run;      // void handlers are wrapped to always return true
	};
	std::vector<Entry> entries_;
	// Predicates from every exclusive scope() call, checked once per dispatch() independent
	// of the pressed key's resolved action id — see dispatch()'s own comment.
	std::vector<Predicate> exclusiveGates_;
};

} // namespace StoermelderPackOne