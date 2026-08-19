// Build sentinel for the swappable vcv access layer: a symbol that exists only in a
// DEBUGPLUGIN plugin build, i.e. exactly when the layer's `*Access` pointers exist and it can
// be mocked. The test harness (src/test/test_plugin.hpp) declares and references it, so a
// test linked against a release plugin.dylib fails on one named symbol rather than a pile of
// undefined `vcv::*Access`.

namespace StoermelderPackOne {
namespace vcv {

#ifdef DEBUGPLUGIN
// Must NOT be `static`: internal linkage would give each TU its own copy, so the test binary
// would resolve it locally and the check would never fire.
void assertDebugPluginBuild() {}
#endif

} // namespace vcv
} // namespace StoermelderPackOne
