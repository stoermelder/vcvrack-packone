# Test build rules
# Use "make test" to build tests
# Use "make testrun" to build and run tests (parallel; set JOBS=N to control fan-out, default 8)
# Use "make testrun SUCCESS=1" to print test success messages
# Use "make testrun-one NAME=<Module>" to build (if needed) and run a single test binary
# Use "make testrun-one NAME=<Module> FILTER='[tag]'" to run only matching TEST_CASEs
# Use "make test SANITIZER=thread" (or =undefined, =address) to switch sanitizers; default address.
# TSan is required for any concurrent (ThreadedHarness-style) test.
#
# Every test binary links a static archive of the plugin's own objects
# ($(TEST_PLUGIN_ARCHIVE)) rather than plugin.dylib.
#
# Why an archive and not the dylib: linking a *dylib* demotes every implicitly-inline class
# member to image-local ("was a private external"), so the dylib's copies are unreachable and
# the test TU's #include of the module .cpp is the only definition — the module's code then
# exists twice in one process. The same objects in a *.a* keep `weak external` linkage and
# coalesce with the test TU's copies into one definition.
#
# Each suite defines its own testPluginInit() registering just the models it needs.
# It is deliberately not called init(): that lets the plugin's own
# plugin.cpp link in from the archive untouched, its init() going unreferenced rather than
# colliding, so init()'s ~70 addModel() calls can never run during static initialization
# against model globals in other TUs that are still null.
#
# Caveat: the plugin's objects are built without -fsanitize (the plugin build adds none), so a
# test binary instruments only its own TUs. That still covers the module under test, whose .cpp
# is #included, but not prebuilt code from other modules.

ifdef SUCCESS
	TEST_SUCCESS_FLAG = --success
endif

ifdef FILTER
	TEST_FILTER_ARG = "$(FILTER)"
endif

SANITIZER ?= address

# A static archive of every object the plugin build produces — the plugin's own sources plus the
# vendored dep/ ones (omitting dep/ yields undefined soundtouch::SoundTouch::*). Built from the
# same objects $(TARGET) links, so it needs no separate compile: one `ar` over what already
# exists, shared by every test binary.
TEST_PLUGIN_ARCHIVE := build/test/.shared/libplugin.a
TEST_PLUGIN_OBJECTS := $(filter-out build/test/%,$(OBJECTS))

# GL, which plugin.dylib used to resolve internally; an archive defers those symbols
# (_glGetIntegerv, _glReadPixels, …) to the final link.
ifdef ARCH_MAC
	TEST_GL_LDFLAGS := -framework OpenGL
endif
ifdef ARCH_LIN
	TEST_GL_LDFLAGS := -lGL
endif
ifdef ARCH_WIN
	TEST_GL_LDFLAGS := -lopengl32
endif

$(TEST_PLUGIN_ARCHIVE): $(TEST_PLUGIN_OBJECTS)
	@mkdir -p $(dir $@)
	@echo "Building $@..."
	@rm -f $@
	@$(AR) rcs $@ $^

# Number of test binaries to run concurrently in `testrun`. ASan/TSan/UBSan runtimes are
# independent per-process, so running binaries in parallel is safe; only the binaries
# themselves are serial internally.
JOBS ?= 8

TEST_SOURCES += $(wildcard src/**/*.test.cpp src/**/**/*.test.cpp)

# Catch2's amalgamated source is 12k lines and takes ~10s to compile — two thirds of the
# cost of a test binary — and it is byte-identical for all of them. Compile it once into a
# shared object and link that instead of recompiling it per binary. Kept per-sanitizer so
# an ASan Catch2 is never linked into a TSan binary. Note that switching SANITIZER does not
# by itself rebuild the test binaries (their paths don't encode it) — as before this change,
# `rm -rf build/test` when changing sanitizer.
TEST_CATCH_OBJ := build/test/.shared/$(SANITIZER)/catch_amalgamated.o

# Every project header. Test/perf binaries #include module sources and utility
# headers directly, so without these as prerequisites a header edit leaves the
# binary stale and `make testrun`/`make perfrun` silently re-runs old code.
# $(TEST_PLUGIN_ARCHIVE) covers the same risk for module .cpp edits.
TEST_HEADERS := $(wildcard src/*.hpp src/*.h src/**/*.hpp src/**/*.h src/**/**/*.hpp src/**/**/*.h)

# Build each test source into its own executable under build/test/ using basenames
TEST_NAMES := $(patsubst %.cpp,%,$(notdir $(TEST_SOURCES)))
TEST_BINARIES := $(patsubst %,build/test/%,$(TEST_NAMES))

# Allow pattern rule to locate test source files by searching these directories
VPATH := $(sort $(dir $(TEST_SOURCES)))

# The compile flags shared by the Catch2 object and every test TU. Keep them identical:
# the shared object is linked into all of them, so a flag that changes ABI or sanitizer
# behaviour must not differ between the two.
TEST_CXXFLAGS := -std=c++14 -I$(CURDIR)/src/test $(FLAGS) -O0 -UNDEBUG -DDEBUGPLUGIN \
	-fsanitize=$(SANITIZER) -fno-omit-frame-pointer

# Catch2, compiled once and linked into every test binary.
$(TEST_CATCH_OBJ): src/test/catch_amalgamated.cpp src/test/catch_amalgamated.hpp
	@mkdir -p $(dir $@)
	@echo "Building $@..."
	@$(CXX) $(TEST_CXXFLAGS) -c -o $@ $<

# Pattern rule to build an individual test executable. No $(TARGET) prerequisite: test binaries
# link the archive, not the dylib, so they don't serialise behind a full plugin *link* — they
# still depend on the same objects, via $(TEST_PLUGIN_ARCHIVE).
build/test/%: %.cpp $(TEST_HEADERS) $(TEST_CATCH_OBJ) $(TEST_PLUGIN_ARCHIVE)
	@mkdir -p $(dir $@)
	@echo "Building $@..."
	@$(CXX) $(TEST_CXXFLAGS) \
		-L$(RACK_DIR) -lRack $(TEST_GL_LDFLAGS) \
		-o $@ $(TEST_CATCH_OBJ) $< $(TEST_PLUGIN_ARCHIVE)

# Build all test binaries
test: $(TEST_BINARIES)

# Run all test binaries in parallel (JOBS concurrent processes; default 8). Each binary's own
# output is captured and printed as one block once it finishes (prefixed with its name), so
# concurrent runs don't interleave garbage on stdout. The binary's own exit status is preserved
# through the capture (not masked by a trailing pipe), so xargs exits non-zero if any binary
# failed, which make then propagates.
testrun: test
	@echo "Running $(words $(TEST_BINARIES)) test binaries ($(JOBS) parallel, SANITIZER=$(SANITIZER))..."
	@printf '%s\n' $(TEST_BINARIES) | xargs -P $(JOBS) -I{} sh -c \
		'out=$$(TESTING=1 DYLD_LIBRARY_PATH=$(RACK_DIR) ./{} $(TEST_SUCCESS_FLAG) 2>&1); status=$$?; echo "=== {} ==="; echo "$$out"; exit $$status'

# Build (if out of date) and run a single test binary, e.g. make testrun-one NAME=Mb
# Add FILTER='[tag]' (or a Catch2 test-name pattern) to run only matching TEST_CASEs instead of
# the whole binary — much faster during development than a full-binary run.
# The binary is build/test/<NAME>.test. Its own prerequisites (above) already relink it whenever
# any project header or the plugin archive is stale, so this one target covers both "just run it"
# and "rebuild everything, then run it" — there used to be a separate test-one target for the
# latter, but once the plugin's own output became a prerequisite of the binary itself the two were
# identical, just reached via a different number of `make` invocations. Removed rather than kept
# as a confusing alias.
.PHONY: testrun-one
testrun-one: build/test/$(NAME).test
	TESTING=1 DYLD_LIBRARY_PATH=$(RACK_DIR) ./$< $(TEST_SUCCESS_FLAG) $(TEST_FILTER_ARG)