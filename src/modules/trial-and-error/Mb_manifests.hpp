#pragma once
#include "../../plugin.hpp"
#include <plugin.hpp>

namespace StoermelderPackOne {
namespace Mb {

// Manifests cache ("Newest" sort, downloaded from the VCV Rack Library)
// https://github.com/VCVRack/library/blob/v2/manifests-cache.json
//
// All file- and network-I/O runs on a detached background thread so the UI
// thread never blocks. Cached results are protected by an internal mutex and
// can be read from any thread.

// Loads the local manifests cache (if present) and, if enabled, checks whether
// new plugins were installed since the cache was last populated, downloading
// an update in that case. Spawns a detached background thread; safe to call
// once per BrowserOverlay lifetime.
void manifestsCacheInit();
// True if a local copy of the manifests cache has been loaded into memory, i.e. "Newest" can be used
bool manifestsCacheExists();
// Returns the creation timestamp (seconds since epoch) of a model, or -1 if unknown
int64_t manifestCreationTimestampGet(Model* model);

} // namespace Mb
} // namespace StoermelderPackOne
