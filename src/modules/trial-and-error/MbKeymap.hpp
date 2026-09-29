#pragma once
#include "../../utils/Keymap.hpp"

// MB's keyboard shortcuts, shared across the v1 and v2 browser search fields. The bindings
// below are only the defaults: the actual bindings live in <Rack user folder>/Stoermelder-P1/
// keymaps/Mb.jsonc (see Keymaps::open()). No in-app rebinding menu yet; hand-editing that file
// is the only way to change a shortcut.
//
// v1 has no arrow-key result navigation and no Ctrl+1/2/3/4 layout shortcuts (no header dropdowns
// to jump to); it simply never hooks those action ids. Both UI generations share one keymap
// since they belong to the same module and mostly overlap.

namespace StoermelderPackOne {
namespace Mb {

inline std::shared_ptr<Keymap> registerActions() {
	auto km = Keymaps::open("Mb");

	// PRESS and REPEAT both fire the original dispatcher's action (holding the key kept
	// re-toggling), so these register GLFW_REPEAT rather than the GLFW_PRESS default.
	km->registerAction("browser.close",                  "Close browser",                   "Browser", "Escape", GLFW_REPEAT);
	km->registerAction("browser.clear",                  "Clear filters (empty search)",    "Browser", "Backspace", GLFW_REPEAT);
	// Unbound by default: same as browser.clear, but works with text in the search field too.
	km->registerAction("browser.clear.always",           "Clear filters (any search)",      "Browser");
	km->registerAction("browser.favorite.toggle",        "Toggle favorites (empty search)", "Browser", "Space", GLFW_REPEAT);
	// Unbound by default: same as browser.favorite.toggle, but works with text in the search field too.
	km->registerAction("browser.favorite.toggle.always", "Toggle favorites (any search)",   "Browser");
	km->registerAction("browser.hidden.toggle",          "Toggle hidden",                   "Browser", "Shift+Space", GLFW_REPEAT);
	// v2 also accepted Ctrl+Space for the same toggle; kept as a second default binding.
	km->registerAlias("browser.hidden.toggle", KeyCombo("Ctrl+Space"));

	// Applies to the ModelBox under the cursor (v1 and v2).
	km->registerAction("modelbox.favorite.toggle", "Toggle favorite (hovered module)", "ModelBox", "Ctrl+F");
	km->registerAction("modelbox.hidden.toggle",   "Toggle hidden (hovered module)",   "ModelBox", "Ctrl+H");

	// v2 only (hence the "v2" in the ids, as the JSON file shows nothing else): arrow-key result navigation. Up/Down are always active; Left/Right are gated
	// behind pluginSettings.mbArrowKeyNavigation at the call site.
	km->registerAction("browser.v2.nav.up",    "Select module above",  "Navigation", "Up", GLFW_REPEAT);
	km->registerAction("browser.v2.nav.down",  "Select module below",  "Navigation", "Down", GLFW_REPEAT);
	km->registerAction("browser.v2.nav.left",  "Select module left",   "Navigation", "Left", GLFW_REPEAT);
	km->registerAction("browser.v2.nav.right", "Select module right",  "Navigation", "Right", GLFW_REPEAT);

	// v2 only: open a header dropdown (Brand/Tag/Custom tag). Pressing the same combo again
	// while its dropdown is open closes it - handled at the call site, not via a second action.
	km->registerAction("browser.v2.layout.brand",     "Open Brand dropdown",      "Browser", "Ctrl+1");
	km->registerAction("browser.v2.layout.tag",       "Open Tag dropdown",        "Browser", "Ctrl+2");
	km->registerAction("browser.v2.layout.customtag", "Open Custom Tag dropdown", "Browser", "Ctrl+3");
	km->registerAction("browser.v2.layout.width",     "Open Width dropdown",      "Browser", "Ctrl+4");

	km->save();
	return km;
}

} // namespace Mb
} // namespace StoermelderPackOne
