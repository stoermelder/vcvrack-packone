#pragma once
#include "../../utils/Keymap.hpp"

// Ahab's keyboard shortcuts. The bindings below are only the defaults: the actual bindings
// live in <Rack user folder>/Stoermelder-P1/keymaps/Ahab.json (see Keymaps::open()). No in-app
// rebinding menu yet; hand-editing that file is the only way to change a shortcut.

namespace StoermelderPackOne {
namespace Ahab {

// Registers Ahab's vocabulary into the shared "Ahab" keymap and returns it. Idempotent.
//
// Arrow keys combine three independent modifiers (Ctrl = grid step, Shift = extend selection,
// Alt = also move the rect), so each of the 8 meaningful combinations per direction is its own
// action (32 total) rather than one action per direction with a raw e.mods check inside.
inline std::shared_ptr<Keymap> registerActions() {
	auto km = Keymaps::open("Ahab");

	km->registerAction("edit.clear.selection", "Clear selection", "Edit", "Backspace");
	km->registerAction("transport.toggle",     "Play / stop",     "Transport", "Space");
	km->registerAction("view.focus",           "Focus mode",      "View", "Shift+Escape");
	km->registerAction("cancel",               "Clear selection (Escape)", "Edit", "Escape");

	km->registerAction("edit.select.all", "Select all",         "Edit", "Ctrl+A");
	km->registerAction("edit.clear.field", "Clear field",        "Edit", "Ctrl+N");
	km->registerAction("file.load",       "Load file",           "File", "Ctrl+O");
	km->registerAction("file.inject",     "Inject file",         "File", "Ctrl+B");
	km->registerAction("file.save",       "Save file",           "File", "Ctrl+S");
	km->registerAction("file.save.selection", "Save selection to file", "File", "Ctrl+Shift+S");
	km->registerAction("edit.undo",       "Undo",                "Edit", "Ctrl+Z");
	km->registerAction("edit.redo",       "Redo",                "Edit", "Ctrl+Shift+Z");
	km->registerAction("edit.insert.toggle", "Toggle insert mode", "Edit", "Ctrl+I");
	km->registerAction("edit.copy",       "Copy selection",      "Edit", "Ctrl+C");
	km->registerAction("edit.cut",        "Cut selection",       "Edit", "Ctrl+X");
	km->registerAction("edit.paste",      "Paste at cursor",     "Edit", "Ctrl+V");
	km->registerAction("sim.step",        "Step one tick",       "Simulation", "Ctrl+F");
	km->registerAction("sim.trigger",     "Trigger operator on cursor", "Simulation", "Ctrl+P");
	km->registerAction("sim.tick.reset",  "Reset tick number to zero", "Simulation", "Ctrl+Shift+R");
	km->registerAction("edit.comment.toggle", "Toggle comment block", "Edit", "Ctrl+Shift+7");

	// Navigation - one action per direction per modifier combination (§ header comment).
	static const struct { const char* dir; const char* label; const char* plain; const char* ctrl; const char* shift; const char* ctrlShift; } kNav[] = {
		{"up",    "up",    "Up",    "Ctrl+Up",    "Shift+Up",    "Ctrl+Shift+Up"},
		{"down",  "down",  "Down",  "Ctrl+Down",  "Shift+Down",  "Ctrl+Shift+Down"},
		{"left",  "left",  "Left",  "Ctrl+Left",  "Shift+Left",  "Ctrl+Shift+Left"},
		{"right", "right", "Right", "Ctrl+Right", "Shift+Right", "Ctrl+Shift+Right"},
	};
	for (const auto& n : kNav) {
		km->registerAction(std::string("nav.") + n.dir, std::string("Move cursor ") + n.label,
			"Navigation", n.plain, GLFW_REPEAT);
		km->registerAction(std::string("nav.") + n.dir + ".gridstep", std::string("Move cursor ") + n.label + " by grid step",
			"Navigation", n.ctrl, GLFW_REPEAT);
		km->registerAction(std::string("nav.") + n.dir + ".extend", std::string("Extend selection ") + n.label,
			"Navigation", n.shift, GLFW_REPEAT);
		km->registerAction(std::string("nav.") + n.dir + ".extend.gridstep", std::string("Extend selection ") + n.label + " by grid step",
			"Navigation", n.ctrlShift, GLFW_REPEAT);
	}
	// Alt variants: same four combinations, plus "also schedule a move of the selected rect".
	static const struct { const char* dir; const char* label; const char* alt; const char* ctrlAlt; const char* shiftAlt; const char* ctrlShiftAlt; } kNavAlt[] = {
		{"up",    "up",    "Alt+Up",    "Ctrl+Alt+Up",    "Shift+Alt+Up",    "Ctrl+Shift+Alt+Up"},
		{"down",  "down",  "Alt+Down",  "Ctrl+Alt+Down",  "Shift+Alt+Down",  "Ctrl+Shift+Alt+Down"},
		{"left",  "left",  "Alt+Left",  "Ctrl+Alt+Left",  "Shift+Alt+Left",  "Ctrl+Shift+Alt+Left"},
		{"right", "right", "Alt+Right", "Ctrl+Alt+Right", "Shift+Alt+Right", "Ctrl+Shift+Alt+Right"},
	};
	for (const auto& n : kNavAlt) {
		km->registerAction(std::string("nav.") + n.dir + ".move", std::string("Move cursor ") + n.label + " (also move selection)",
			"Navigation", n.alt, GLFW_REPEAT);
		km->registerAction(std::string("nav.") + n.dir + ".move.gridstep", std::string("Move cursor ") + n.label + " by grid step (also move selection)",
			"Navigation", n.ctrlAlt, GLFW_REPEAT);
		km->registerAction(std::string("nav.") + n.dir + ".move.extend", std::string("Extend selection ") + n.label + " (also move selection)",
			"Navigation", n.shiftAlt, GLFW_REPEAT);
		km->registerAction(std::string("nav.") + n.dir + ".move.extend.gridstep", std::string("Extend selection ") + n.label + " by grid step (also move selection)",
			"Navigation", n.ctrlShiftAlt, GLFW_REPEAT);
	}

	km->save();
	return km;
}

} // namespace Ahab
} // namespace StoermelderPackOne
