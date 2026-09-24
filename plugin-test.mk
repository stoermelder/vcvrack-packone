# Test build rules
# Use "make test" to build tests
# Use "make testrun" to build and run tests (parallel; set JOBS=N to control fan-out, default 8)
# Use "make testrun SUCCESS=1" to print test success messages
# Use "make testrun-one NAME=<Module>" to build (if needed) and run a single test binary
# Use "make testrun-one NAME=<Module> FILTER='[tag]'" to run only matching TEST_CASEs
# Use "make test SANITIZER=thread" (or =undefined, =address) to switch sanitizers; default address.
# TSan is required for any concurrent (ThreadedHarness-style) test.
#
# Test binaries link a static archive of the plugin's objects, not plugin.dylib: a dylib link
# demotes implicitly-inline class members to image-local, so the module's code would exist twice
# in the process (the dylib's unreachable copy plus the test TU's #include of the .cpp). In a .a
# those members stay `weak external` and coalesce into one definition.
#
# Each suite defines its own testPluginInit() — see src/test/CONVERTING.md.

ifdef SUCCESS
	TEST_SUCCESS_FLAG = --success
endif

ifdef FILTER
	TEST_FILTER_ARG = "$(FILTER)"
endif

SANITIZER ?= address

# Archive of exactly the objects the plugin dylib is built from — one `ar` over $(OBJECTS), so
# the sources are compiled once, by the plugin's own rules, with the plugin's own flags
# (including the scoped ones, e.g. QuickJS's -I and CONFIG_VERSION).
#
# Needs DEBUGPLUGIN, which Makefile implies for test targets: the vcv::*Access seam only exists
# as a linkable function under that flag. Note it does not invalidate objects compiled without
# it, so a tree last built by a plain `make` still holds release objects and the link fails on
# the missing seam functions — `make clean` is the fix.
TEST_PLUGIN_ARCHIVE := build/test/.shared/libplugin.a
TEST_PLUGIN_OBJECTS := $(filter-out build/test/%,$(OBJECTS))

$(TEST_PLUGIN_ARCHIVE): $(TEST_PLUGIN_OBJECTS)
	@mkdir -p $(dir $@)
	@echo "Building $@..."
	@rm -f $@
	@$(AR) rcs $@ $^

# Concurrent processes in `testrun`. Safe: sanitizer runtimes are per-process.
JOBS ?= 8

TEST_SOURCES += $(wildcard src/**/*.test.cpp src/**/**/*.test.cpp)

# Catch2 is 12k lines (~10s, two thirds of a test binary's build) and identical for every
# binary, so compile it once. Per-sanitizer so an ASan Catch2 never lands in a TSan binary;
# note the binaries' own paths don't encode SANITIZER, so `rm -rf build/test` when switching.
TEST_CATCH_OBJ := build/test/.shared/$(SANITIZER)/catch_amalgamated.o

# Test binaries #include module sources and utility headers directly, so without these as
# prerequisites a header edit silently re-runs old code. $(TEST_PLUGIN_ARCHIVE) covers .cpp edits.
TEST_HEADERS := $(wildcard src/*.hpp src/*.h src/**/*.hpp src/**/*.h src/**/**/*.hpp src/**/**/*.h)

# Build each test source into its own executable under build/test/ using basenames
TEST_NAMES := $(patsubst %.cpp,%,$(notdir $(TEST_SOURCES)))
TEST_BINARIES := $(patsubst %,build/test/%,$(TEST_NAMES))

# Allow pattern rule to locate test source files by searching these directories
VPATH := $(sort $(dir $(TEST_SOURCES)))

# Shared by the Catch2 object and every test TU — keep identical, since the one object links
# into all of them and an ABI- or sanitizer-affecting flag must not differ.
TEST_CXXFLAGS := -std=c++14 -I$(CURDIR)/src/test $(FLAGS) -O0 -UNDEBUG -DDEBUGPLUGIN \
	-fsanitize=$(SANITIZER) -fno-omit-frame-pointer

# The plugin's own link flags, minus -shared (these are executables). That carries -lRack and,
# on macOS, the `-undefined dynamic_lookup` the dylib is built with — which is what leaves the
# archive's ~47 GL references unresolved and harmless, since nothing in a test binary draws.
TEST_LDFLAGS := $(filter-out -shared,$(LDFLAGS))

$(TEST_CATCH_OBJ): src/test/catch_amalgamated.cpp src/test/catch_amalgamated.hpp
	@mkdir -p $(dir $@)
	@echo "Building $@..."
	@$(CXX) $(TEST_CXXFLAGS) -c -o $@ $<

# No $(TARGET) prerequisite: binaries link the archive, so they don't serialise behind a full
# plugin link, but still track the same sources through it.
build/test/%: %.cpp $(TEST_HEADERS) $(TEST_CATCH_OBJ) $(TEST_PLUGIN_ARCHIVE)
	@mkdir -p $(dir $@)
	@echo "Building $@..."
	@$(CXX) $(TEST_CXXFLAGS) $(TEST_LDFLAGS) \
		-o $@ $(TEST_CATCH_OBJ) $< $(TEST_PLUGIN_ARCHIVE)

# Build all test binaries
test: $(TEST_BINARIES)

# Each binary's output is captured and printed as one block, so concurrent runs don't interleave.
# Its exit status survives the capture (no trailing pipe), so xargs — and make — fail if any did.
testrun: test
	@echo "Running $(words $(TEST_BINARIES)) test binaries ($(JOBS) parallel, SANITIZER=$(SANITIZER))..."
	@printf '%s\n' $(TEST_BINARIES) | xargs -P $(JOBS) -I{} sh -c \
		'out=$$(TESTING=1 DYLD_LIBRARY_PATH=$(RACK_DIR) ./{} $(TEST_SUCCESS_FLAG) 2>&1); status=$$?; echo "=== {} ==="; echo "$$out"; exit $$status'

# Build (if out of date) and run a single test binary, e.g. make testrun-one NAME=Mb
# Add FILTER='[tag]' (or a Catch2 test-name pattern) to run only matching TEST_CASEs — much
# faster during development. The binary's own prerequisites already relink it when anything is
# stale, so this covers both "just run it" and "rebuild, then run it".
.PHONY: testrun-one
testrun-one: build/test/$(NAME).test
	TESTING=1 DYLD_LIBRARY_PATH=$(RACK_DIR) ./$< $(TEST_SUCCESS_FLAG) $(TEST_FILTER_ARG)
