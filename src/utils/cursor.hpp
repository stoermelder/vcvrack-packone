#pragma once
#include <rack.hpp>

namespace StoermelderPackOne {
namespace cursor {

// Mouse cursor helpers for learn/map modes.
//
// Call sites used to call glfwCreateStandardCursor() inline, which allocates a fresh cursor
// every time (GLFW does not deduplicate) and was never paired with glfwDestroyCursor() — one
// leak per learn-mode entry. The crosshair is created once per process here instead.
//
// All three no-op when there is no window, so call sites need no `if (APP->window)` guard.

// Sets the crosshair cursor, used to signal that a learn/map mode is active.
inline void setCrosshairCursor() {
	if (!APP->window) return;
	// Never destroyed: it is wanted for as long as the plugin is loaded, and glfwTerminate()
	// cleans up whatever remains.
	static GLFWcursor* crosshair = glfwCreateStandardCursor(GLFW_CROSSHAIR_CURSOR);
	glfwSetCursor(APP->window->win, crosshair);
}

// Restores the default OS arrow, used when a learn/map mode ends.
inline void resetCursor() {
	if (!APP->window) return;
	glfwSetCursor(APP->window->win, NULL);
}

// Sets the crosshair while `active`, the arrow otherwise — the shape most call sites want,
// since they are toggling a learn mode and must not leave a crosshair behind when it ends.
inline void setLearnCursor(bool active) {
	if (active) setCrosshairCursor();
	else resetCursor();
}

} // namespace cursor
} // namespace StoermelderPackOne
