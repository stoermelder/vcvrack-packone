#include "Keymap.hpp"
#include "../vcv/fs.hpp"
#include "../vcv/selection.hpp"   // vcv::parseJson
#include "string.hpp"
#include <algorithm>
#include <cctype>
#include <cassert>

namespace StoermelderPackOne {

const std::string Keymap::EMPTY = "";

// Title-case, file-friendly names covering the full GLFW range used by this design. Distinct
// from keyboard.hpp's keyName() (all-caps, built for Stroke's panel display).
namespace {

struct KeyNameEntry { int key; const char* name; };

// clang-format off
static const KeyNameEntry kKeyNames[] = {
	{GLFW_KEY_SPACE, "Space"}, {GLFW_KEY_ESCAPE, "Escape"}, {GLFW_KEY_ENTER, "Enter"},
	{GLFW_KEY_TAB, "Tab"}, {GLFW_KEY_BACKSPACE, "Backspace"}, {GLFW_KEY_INSERT, "Insert"},
	{GLFW_KEY_DELETE, "Delete"}, {GLFW_KEY_RIGHT, "Right"}, {GLFW_KEY_LEFT, "Left"},
	{GLFW_KEY_DOWN, "Down"}, {GLFW_KEY_UP, "Up"}, {GLFW_KEY_PAGE_UP, "PageUp"},
	{GLFW_KEY_PAGE_DOWN, "PageDown"}, {GLFW_KEY_HOME, "Home"}, {GLFW_KEY_END, "End"},
	{GLFW_KEY_CAPS_LOCK, "CapsLock"}, {GLFW_KEY_SCROLL_LOCK, "ScrollLock"},
	{GLFW_KEY_NUM_LOCK, "NumLock"}, {GLFW_KEY_PRINT_SCREEN, "PrintScreen"},
	{GLFW_KEY_PAUSE, "Pause"},
	{GLFW_KEY_F1, "F1"}, {GLFW_KEY_F2, "F2"}, {GLFW_KEY_F3, "F3"}, {GLFW_KEY_F4, "F4"},
	{GLFW_KEY_F5, "F5"}, {GLFW_KEY_F6, "F6"}, {GLFW_KEY_F7, "F7"}, {GLFW_KEY_F8, "F8"},
	{GLFW_KEY_F9, "F9"}, {GLFW_KEY_F10, "F10"}, {GLFW_KEY_F11, "F11"}, {GLFW_KEY_F12, "F12"},
	{GLFW_KEY_F13, "F13"}, {GLFW_KEY_F14, "F14"}, {GLFW_KEY_F15, "F15"}, {GLFW_KEY_F16, "F16"},
	{GLFW_KEY_F17, "F17"}, {GLFW_KEY_F18, "F18"}, {GLFW_KEY_F19, "F19"}, {GLFW_KEY_F20, "F20"},
	{GLFW_KEY_F21, "F21"}, {GLFW_KEY_F22, "F22"}, {GLFW_KEY_F23, "F23"}, {GLFW_KEY_F24, "F24"},
	{GLFW_KEY_F25, "F25"},
	{GLFW_KEY_KP_0, "KP0"}, {GLFW_KEY_KP_1, "KP1"}, {GLFW_KEY_KP_2, "KP2"},
	{GLFW_KEY_KP_3, "KP3"}, {GLFW_KEY_KP_4, "KP4"}, {GLFW_KEY_KP_5, "KP5"},
	{GLFW_KEY_KP_6, "KP6"}, {GLFW_KEY_KP_7, "KP7"}, {GLFW_KEY_KP_8, "KP8"},
	{GLFW_KEY_KP_9, "KP9"}, {GLFW_KEY_KP_DECIMAL, "KPDecimal"}, {GLFW_KEY_KP_DIVIDE, "KPDivide"},
	{GLFW_KEY_KP_MULTIPLY, "KPMultiply"}, {GLFW_KEY_KP_SUBTRACT, "KPSubtract"},
	{GLFW_KEY_KP_ADD, "KPAdd"}, {GLFW_KEY_KP_ENTER, "KPEnter"}, {GLFW_KEY_KP_EQUAL, "KPEqual"},
	{GLFW_KEY_WORLD_1, "World1"}, {GLFW_KEY_WORLD_2, "World2"},
	{GLFW_KEY_MENU, "Menu"},
};
// clang-format on

// Printable keys, spelled out directly rather than via glfwGetKeyName() (layout-dependent, and
// NULL headless with no GLFW window) so parsing/printing is identical in tests and the app.
static const KeyNameEntry kPrintableKeyNames[] = {
	{GLFW_KEY_A, "A"}, {GLFW_KEY_B, "B"}, {GLFW_KEY_C, "C"}, {GLFW_KEY_D, "D"},
	{GLFW_KEY_E, "E"}, {GLFW_KEY_F, "F"}, {GLFW_KEY_G, "G"}, {GLFW_KEY_H, "H"},
	{GLFW_KEY_I, "I"}, {GLFW_KEY_J, "J"}, {GLFW_KEY_K, "K"}, {GLFW_KEY_L, "L"},
	{GLFW_KEY_M, "M"}, {GLFW_KEY_N, "N"}, {GLFW_KEY_O, "O"}, {GLFW_KEY_P, "P"},
	{GLFW_KEY_Q, "Q"}, {GLFW_KEY_R, "R"}, {GLFW_KEY_S, "S"}, {GLFW_KEY_T, "T"},
	{GLFW_KEY_U, "U"}, {GLFW_KEY_V, "V"}, {GLFW_KEY_W, "W"}, {GLFW_KEY_X, "X"},
	{GLFW_KEY_Y, "Y"}, {GLFW_KEY_Z, "Z"},
	{GLFW_KEY_0, "0"}, {GLFW_KEY_1, "1"}, {GLFW_KEY_2, "2"}, {GLFW_KEY_3, "3"},
	{GLFW_KEY_4, "4"}, {GLFW_KEY_5, "5"}, {GLFW_KEY_6, "6"}, {GLFW_KEY_7, "7"},
	{GLFW_KEY_8, "8"}, {GLFW_KEY_9, "9"},
	{GLFW_KEY_APOSTROPHE, "'"}, {GLFW_KEY_COMMA, ","}, {GLFW_KEY_MINUS, "-"},
	{GLFW_KEY_PERIOD, "."}, {GLFW_KEY_SLASH, "/"}, {GLFW_KEY_SEMICOLON, ";"},
	{GLFW_KEY_EQUAL, "="}, {GLFW_KEY_LEFT_BRACKET, "["}, {GLFW_KEY_BACKSLASH, "\\"},
	{GLFW_KEY_RIGHT_BRACKET, "]"}, {GLFW_KEY_GRAVE_ACCENT, "`"},
};

// Extra parse-only aliases so a user writing keyboard.hpp's keyName() spelling by analogy
// with Stroke's display ("ESC", "BS", "PG-UP", ...) still gets a working file.
struct KeyAlias { const char* name; int key; };
static const KeyAlias kKeyAliases[] = {
	{"ESC", GLFW_KEY_ESCAPE}, {"BS", GLFW_KEY_BACKSPACE}, {"INS", GLFW_KEY_INSERT},
	{"DEL", GLFW_KEY_DELETE}, {"PG-UP", GLFW_KEY_PAGE_UP}, {"PG-DW", GLFW_KEY_PAGE_DOWN},
	{"PRINT", GLFW_KEY_PRINT_SCREEN},
	{"KP /", GLFW_KEY_KP_DIVIDE}, {"KP *", GLFW_KEY_KP_MULTIPLY},
	{"KP -", GLFW_KEY_KP_SUBTRACT}, {"KP +", GLFW_KEY_KP_ADD}, {"KP .", GLFW_KEY_KP_DECIMAL},
	{"W1", GLFW_KEY_WORLD_1}, {"W2", GLFW_KEY_WORLD_2},
};

static std::string toUpper(const std::string& s) {
	std::string r = s;
	for (auto& c : r) c = (char) std::toupper((unsigned char) c);
	return r;
}

} // namespace

std::string comboKeyName(int key) {
	for (const auto& e : kKeyNames) {
		if (e.key == key) return e.name;
	}
	for (const auto& e : kPrintableKeyNames) {
		if (e.key == key) return e.name;
	}
	return "";
}

int parseKeyName(const std::string& name) {
	std::string upper = toUpper(name);
	for (const auto& e : kKeyNames) {
		if (toUpper(e.name) == upper) return e.key;
	}
	for (const auto& e : kPrintableKeyNames) {
		if (toUpper(e.name) == upper) return e.key;
	}
	for (const auto& a : kKeyAliases) {
		if (toUpper(a.name) == upper) return a.key;
	}
	return GLFW_KEY_UNKNOWN;
}

KeyCombo::KeyCombo(const char* spec) {
	std::string s = spec ? spec : "";
	int m = 0;
	size_t pos = 0;
	while (true) {
		size_t plus = s.find('+', pos);
		if (plus == std::string::npos) break;
		std::string tok = toUpper(s.substr(pos, plus - pos));
		if (tok == "CTRL" || tok == "CMD" || tok == "COMMAND" || tok == "SUPER") m |= RACK_MOD_CTRL;
		else if (tok == "SHIFT") m |= GLFW_MOD_SHIFT;
		else if (tok == "ALT") m |= GLFW_MOD_ALT;
		else { key = GLFW_KEY_UNKNOWN; mods = 0; return; } // unknown modifier token -> invalid
		pos = plus + 1;
	}
	std::string keyPart = s.substr(pos);
	if (keyPart.empty()) { key = GLFW_KEY_UNKNOWN; mods = 0; return; }
	int k = parseKeyName(keyPart);
	if (k == GLFW_KEY_UNKNOWN) { mods = 0; return; }
	key = k;
	mods = m;
}

bool KeyCombo::matches(int eventKey, int eventMods) const {
	if (!valid()) return false;
	return StoermelderPackOne::keyFix(eventKey) == key && (eventMods & RACK_MOD_MASK) == mods;
}

std::string KeyCombo::toString() const {
	if (!valid()) return "";
	std::string s;
	if (mods & RACK_MOD_CTRL) s += "Ctrl+";
	if (mods & GLFW_MOD_SHIFT) s += "Shift+";
	if (mods & GLFW_MOD_ALT) s += "Alt+";
	s += comboKeyName(key);
	return s;
}

std::string KeyCombo::displayString() const {
	if (!valid()) return "";
	std::string s;
	if (mods & RACK_MOD_CTRL) s += RACK_MOD_CTRL_NAME "+";
	if (mods & GLFW_MOD_SHIFT) s += RACK_MOD_SHIFT_NAME "+";
	if (mods & GLFW_MOD_ALT) s += RACK_MOD_ALT_NAME "+";
	s += comboKeyName(key);
	return s;
}


Keymap::~Keymap() {
	if (parsedUnknownJ_) json_decref(parsedUnknownJ_);
}

Keymap::Action* Keymap::find(const std::string& id) {
	for (auto& a : actions_) if (a.id == id) return &a;
	return nullptr;
}
const Keymap::Action* Keymap::find(const std::string& id) const {
	for (auto& a : actions_) if (a.id == id) return &a;
	return nullptr;
}

bool Keymap::has(const std::string& id) const {
	return find(id) != nullptr;
}

void Keymap::registerAction(const std::string& id, const std::string& label, const std::string& group,
                             KeyCombo defaultCombo, int trigger) {
	Action* existing = find(id);
	if (existing) {
		assert(existing->label == label && existing->group == group && existing->trigger == trigger
		       && !existing->defaults.empty() && existing->defaults[0] == defaultCombo
		       && "Keymap::registerAction: two call sites disagree for the same action id");
		return;
	}

	Action a;
	a.id = id;
	a.label = label;
	a.group = group;
	a.trigger = trigger;
	a.defaults.push_back(defaultCombo);

	auto boundIt = parsedBound_.find(id);
	auto nullIt = parsedNull_.find(id);
	if (boundIt != parsedBound_.end()) {
		a.combos = boundIt->second;
	}
	else if (nullIt != parsedNull_.end()) {
		// explicitly unbound by the user; leave a.combos empty
	}
	else {
		// absent from the file: take the default, and the file needs rewriting
		if (defaultCombo.valid()) a.combos.push_back(defaultCombo);
		markDirty();
	}

	actions_.push_back(std::move(a));
}

void Keymap::registerAlias(const std::string& id, KeyCombo defaultCombo) {
	Action* a = find(id);
	if (!a) return;
	a->defaults.push_back(defaultCombo);
	// Only apply as a live binding if the file didn't already resolve this id (i.e. the
	// action was freshly defaulted, not loaded/unbound from the file).
	bool fromFile = parsedBound_.count(id) || parsedNull_.count(id);
	if (!fromFile) {
		a->combos.push_back(defaultCombo);
	}
}

const std::string& Keymap::lookup(int key, int mods, int action) const {
	int fixedKey = StoermelderPackOne::keyFix(key);
	int maskedMods = mods & RACK_MOD_MASK;
	for (const auto& a : actions_) {
		// RACK_HELD is Rack's own held-key repeat (synthesised every hover frame, no delay of
		// its own) — distinct from GLFW_REPEAT and never a valid trigger here.
		if (action == GLFW_RELEASE || action == RACK_HELD) continue;
		if (action == GLFW_REPEAT && a.trigger != GLFW_REPEAT) continue;
		for (const auto& c : a.combos) {
			if (c.valid() && c.key == fixedKey && c.mods == maskedMods) return a.id;
		}
	}
	return EMPTY;
}

std::vector<KeyCombo> Keymap::combosFor(const std::string& id) const {
	const Action* a = find(id);
	return a ? a->combos : std::vector<KeyCombo>();
}

std::string Keymap::shortcutText(const std::string& id) const {
	const Action* a = find(id);
	if (!a || a->combos.empty()) return "";
	std::string s;
	for (size_t i = 0; i < a->combos.size(); i++) {
		if (i > 0) s += "  ";
		s += a->combos[i].displayString();
	}
	return s;
}

void Keymap::bind(const std::string& id, KeyCombo combo) {
	Action* a = find(id);
	if (!a) return;
	a->combos.clear();
	if (combo.valid()) a->combos.push_back(combo);
	markDirty();
}

void Keymap::addBinding(const std::string& id, KeyCombo combo) {
	Action* a = find(id);
	if (!a || !combo.valid()) return;
	for (const auto& c : a->combos) if (c == combo) return;
	a->combos.push_back(combo);
	markDirty();
}

void Keymap::unbind(const std::string& id) {
	Action* a = find(id);
	if (!a) return;
	a->combos.clear();
	markDirty();
}

void Keymap::resetAction(const std::string& id) {
	Action* a = find(id);
	if (!a) return;
	a->combos = a->defaults;
	markDirty();
}

void Keymap::resetToDefaults() {
	for (auto& a : actions_) a.combos = a.defaults;
	markDirty();
}

std::vector<std::string> Keymap::conflictsFor(KeyCombo combo, const std::string& excludeId) const {
	std::vector<std::string> r;
	if (!combo.valid()) return r;
	for (const auto& a : actions_) {
		if (a.id == excludeId) continue;
		for (const auto& c : a.combos) {
			if (c == combo) { r.push_back(a.id); break; }
		}
	}
	return r;
}

void Keymap::loadParsed(json_t* root, bool frozen) {
	parsedBound_.clear();
	parsedNull_.clear();
	if (parsedUnknownJ_) { json_decref(parsedUnknownJ_); parsedUnknownJ_ = nullptr; }
	frozen_ = frozen;
	if (!root) return;

	json_t* bindingsJ = json_object_get(root, "bindings");
	if (!bindingsJ || !json_is_object(bindingsJ)) return;

	const char* key;
	json_t* value;
	json_object_foreach(bindingsJ, key, value) {
		std::string id = key;
		if (json_is_null(value)) {
			parsedNull_[id] = true;
		}
		else if (json_is_string(value)) {
			KeyCombo c(json_string_value(value));
			if (c.valid()) parsedBound_[id] = {c};
			// unparseable string: leave unresolved here, registerAction() falls through to
			// "absent" behaviour (default + mark stale).
		}
		else if (json_is_array(value)) {
			std::vector<KeyCombo> combos;
			size_t idx; json_t* item;
			json_array_foreach(value, idx, item) {
				if (json_is_string(item)) {
					KeyCombo c(json_string_value(item));
					if (c.valid()) combos.push_back(c);
				}
			}
			if (!combos.empty()) parsedBound_[id] = combos;
		}
	}

	// ids in the file under "bindings" that never get registered are preserved verbatim on
	// the next write; keep an owned deep copy since `root` (and bindingsJ within it) is
	// decref'd by the caller once this call returns.
	parsedUnknownJ_ = json_deep_copy(bindingsJ);
}

void Keymap::assign(Keymap&& other) {
	slug_ = std::move(other.slug_);
	actions_ = std::move(other.actions_);
	parsedBound_ = std::move(other.parsedBound_);
	parsedNull_ = std::move(other.parsedNull_);
	if (parsedUnknownJ_) json_decref(parsedUnknownJ_);
	parsedUnknownJ_ = other.parsedUnknownJ_;
	other.parsedUnknownJ_ = nullptr;
	frozen_ = other.frozen_;
	dirty_ = other.dirty_;
}

void Keymap::save() {
	if (frozen_) return;
	if (!dirty_) return;

	json_t* root = json_object();
	json_object_set_new(root, "slug", json_string(slug_.c_str()));
	json_object_set_new(root, "version", json_integer(1));

	json_t* bindingsJ = json_object();
	std::map<std::string, bool> registeredIds;
	for (const auto& a : actions_) {
		registeredIds[a.id] = true;
		if (a.combos.empty()) {
			json_object_set_new(bindingsJ, a.id.c_str(), json_null());
		}
		else if (a.combos.size() == 1) {
			json_object_set_new(bindingsJ, a.id.c_str(), json_string(a.combos[0].toString().c_str()));
		}
		else {
			json_t* arr = json_array();
			for (const auto& c : a.combos) json_array_append_new(arr, json_string(c.toString().c_str()));
			json_object_set_new(bindingsJ, a.id.c_str(), arr);
		}
	}

	// Preserve ids from the original file that nothing registered.
	if (parsedUnknownJ_) {
		const char* key;
		json_t* value;
		json_object_foreach(parsedUnknownJ_, key, value) {
			if (registeredIds.count(key)) continue;
			json_object_set(bindingsJ, key, value);   // borrowed value; json_object_set copies the ref
		}
	}

	json_object_set_new(root, "bindings", bindingsJ);

	char* dumped = json_dumps(root, JSON_INDENT(2) | JSON_PRESERVE_ORDER);
	json_decref(root);
	if (dumped) {
		DEFER({ std::free(dumped); });
		vcv::fs::createDirectories(Keymaps::directory());
		if (vcv::fs::write(Keymaps::pathFor(slug_), dumped)) {
			dirty_ = false;
		}
	}
}


namespace Keymaps {

namespace {
std::mutex registryMutex;
std::map<std::string, std::shared_ptr<Keymap>> registry;

std::shared_ptr<Keymap> loadFromDisk(const std::string& slug) {
	auto km = std::make_shared<Keymap>();
	km->setSlug(slug);

	std::string path = pathFor(slug);
	if (vcv::fs::exists(path)) {
		std::string data;
		if (vcv::fs::read(path, data)) {
			std::string error;
			json_t* root = vcv::parseJson(data, error);
			if (root) {
				km->loadParsed(root);
				json_decref(root);
			}
			else {
				// Corrupt file: keep registered defaults in memory but never rewrite, so a
				// user's typo stays recoverable by hand-fixing the file.
				WARN("Keymap: failed to parse %s: %s", path.c_str(), error.c_str());
				km->loadParsed(nullptr, /* frozen */ true);
			}
		}
		else {
			km->loadParsed(nullptr);
		}
	}
	else {
		km->loadParsed(nullptr);
	}
	return km;
}
} // namespace

std::shared_ptr<Keymap> open(const std::string& slug) {
	std::lock_guard<std::mutex> lock(registryMutex);
	auto it = registry.find(slug);
	if (it != registry.end()) return it->second;
	auto km = loadFromDisk(slug);
	registry[slug] = km;
	return km;
}

void reload(const std::string& slug) {
	std::shared_ptr<Keymap> existing;
	{
		std::lock_guard<std::mutex> lock(registryMutex);
		auto it = registry.find(slug);
		if (it == registry.end()) return;
		existing = it->second;
	}

	auto fresh = loadFromDisk(slug);
	// Re-run the existing vocabulary against the freshly parsed file, in original order, so
	// a hand-edit is picked up without needing the module to reconstruct its registration.
	for (const auto& a : existing->actions()) {
		if (a.defaults.empty()) continue;
		fresh->registerAction(a.id, a.label, a.group, a.defaults[0], a.trigger);
		for (size_t i = 1; i < a.defaults.size(); i++) fresh->registerAlias(a.id, a.defaults[i]);
	}

	std::lock_guard<std::mutex> lock(registryMutex);
	existing->assign(std::move(*fresh));
}

std::string directory() {
	return vcv::fs::join(vcv::fs::getUserDirectory("Stoermelder-P1"), "keymaps");
}

std::string pathFor(const std::string& slug) {
	return vcv::fs::join(directory(), slug + ".json");
}

void resetForTest() {
	std::lock_guard<std::mutex> lock(registryMutex);
	registry.clear();
}

} // namespace Keymaps


void KeymapHandlers::on(const std::string& id, Predicate when, Handler h) {
	if (keymap && !keymap->has(id)) {
		WARN("KeymapHandlers::on: '%s' is not a registered action", id.c_str());
		return;
	}
	if (!when) {
		// Backstop: two unconditional handlers for the same id means the second is dead (the
		// first always wins in the unscoped pass).
		for (const auto& e : entries_) {
			if (e.id == id && !e.when) {
				WARN("KeymapHandlers::on: a second unscoped handler for '%s' is dead code", id.c_str());
				break;
			}
		}
	}
	entries_.push_back(Entry{id, std::move(when), [h]() -> bool { h(); return true; }});
}

void KeymapHandlers::onTry(const std::string& id, Predicate when, TryHandler h) {
	if (keymap && !keymap->has(id)) {
		WARN("KeymapHandlers::onTry: '%s' is not a registered action", id.c_str());
		return;
	}
	entries_.push_back(Entry{id, std::move(when), std::move(h)});
}

bool KeymapHandlers::dispatch(int key, int mods, int action) const {
	if (!keymap) return false;

	bool exclusiveActive = false;
	for (const auto& gate : exclusiveGates_) {
		if (gate()) { exclusiveActive = true; break; }
	}

	const std::string& id = keymap->lookup(key, mods, action);
	if (id.empty()) return exclusiveActive;   // even an unbound key is swallowed by a picker

	// Pass 1: predicated entries, in registration order.
	for (const auto& e : entries_) {
		if (e.id != id || !e.when) continue;
		if (!e.when()) continue;
		if (e.run()) return true;
	}
	if (exclusiveActive) return true;   // owned by the exclusive scope; never fall through
	// Pass 2: unscoped entries, as fallback.
	for (const auto& e : entries_) {
		if (e.id != id || e.when) continue;
		if (e.run()) return true;
	}
	return false;
}

} // namespace StoermelderPackOne