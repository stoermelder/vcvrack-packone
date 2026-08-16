#pragma once

// Shortcut header: include the whole swappable vcv access layer in one line.
//   #include "vcv/api.hpp"
// instead of the individual headers:
//   modules.hpp  — modules in the current patch (ModuleAccess)
//   cables.hpp   — cables between those modules (CableAccess)
//   scene.hpp    — the rack scene: selection (SceneAccess)
//   ui.hpp       — the user: dialogs, clipboard, browser (UiAccess)
//   fs.hpp       — the filesystem + pure parseJson (FileAccess)
//.  history.hpp. - HistoryAccess
//
// All headers are #pragma once, so this is safe to include alongside any of
// the individual ones; nothing here introduces new definitions.

#include "modules.hpp"
#include "cables.hpp"
#include "scene.hpp"
#include "ui.hpp"
#include "fs.hpp"
#include "history.hpp" 