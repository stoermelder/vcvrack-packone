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
# demotes implicitly-inline class members to image-local, duplicating module code between the
# dylib and the test TU's #include of the .cpp. A .a keeps them `weak external` so they coalesce.
#
# Each suite defines its own testPluginInit() — see src/test/CONVERTING.md.

ifdef SUCCESS
	TEST_SUCCESS_FLAG = --success
endif

ifdef FILTER
	TEST_FILTER_ARG = "$(FILTER)"
endif

SANITIZER ?= address

# TEST_PLUGIN_ARCHIVE's objects are compiled by CXXFLAGS/CFLAGS, not TEST_CXXFLAGS. Without a
# matching -fsanitize here, those objects are uninstrumented while the test TU's #include of the
# same headers/templates is instrumented; the linker keeps only one copy of a shared inline/
# template symbol, which previously caused a false-positive ASan container-overflow. Guarded by
# goal, like the DEBUGPLUGIN block in Makefile, since this file is always included.
ifneq ($(filter test testrun testrun-one perf perfrun,$(MAKECMDGOALS)),)
	CXXFLAGS += -fsanitize=$(SANITIZER)
	CFLAGS += -fsanitize=$(SANITIZER)
	LDFLAGS += -fsanitize=$(SANITIZER)
endif

# Archive of exactly the objects the plugin dylib is built from — one `ar` over $(OBJECTS), so
# the sources are compiled once, by the plugin's own rules and flags.
#
# Needs DEBUGPLUGIN (Makefile implies it for test targets): the vcv::*Access seam only links under
# that flag. A tree last built by a plain `make` still holds release objects and fails to link on
# the missing seam — `make clean` is the fix.
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

# Catch2 is 12k lines (~10s, two thirds of a test binary's build) and identical per binary, so
# compile it once, per-sanitizer. Binary paths don't encode SANITIZER, so `rm -rf build/test`
# when switching.
TEST_CATCH_OBJ := build/test/.shared/$(SANITIZER)/catch_amalgamated.o

# Prerequisites so a header edit doesn't silently build stale test binaries; $(TEST_PLUGIN_ARCHIVE)
# already covers .cpp edits.
TEST_HEADERS := $(wildcard src/*.hpp src/*.h src/**/*.hpp src/**/*.h src/**/**/*.hpp src/**/**/*.h)

# Build each test source into its own executable under build/test/ using basenames
TEST_NAMES := $(patsubst %.cpp,%,$(notdir $(TEST_SOURCES)))
TEST_BINARIES := $(patsubst %,build/test/%,$(TEST_NAMES))

# Allow pattern rule to locate test source files by searching these directories
VPATH := $(sort $(dir $(TEST_SOURCES)))

# Shared by the Catch2 object and every test TU — keep identical, since one Catch2 object links
# into all of them.
TEST_CXXFLAGS := -std=c++14 -I$(CURDIR)/src/test $(FLAGS) -O0 -UNDEBUG -DDEBUGPLUGIN \
	-fsanitize=$(SANITIZER) -fno-omit-frame-pointer

# The plugin's own link flags minus -shared. Carries -lRack and, on macOS, `-undefined
# dynamic_lookup`, which leaves the archive's ~47 GL references unresolved but harmless.
TEST_LDFLAGS := $(filter-out -shared,$(LDFLAGS))

$(TEST_CATCH_OBJ): src/test/catch_amalgamated.cpp src/test/catch_amalgamated.hpp
	@mkdir -p $(dir $@)
	@echo "Building $@..."
	@$(CXX) $(TEST_CXXFLAGS) -c -o $@ $<

# No $(TARGET) prerequisite: binaries link the archive, not a full plugin link.
build/test/%: %.cpp $(TEST_HEADERS) $(TEST_CATCH_OBJ) $(TEST_PLUGIN_ARCHIVE)
	@mkdir -p $(dir $@)
	@echo "Building $@..."
	@$(CXX) $(TEST_CXXFLAGS) $(TEST_LDFLAGS) \
		-o $@ $(TEST_CATCH_OBJ) $< $(TEST_PLUGIN_ARCHIVE)

# Build all test binaries
test: $(TEST_BINARIES)

# Output captured per binary so concurrent runs don't interleave; exit status survives the
# capture, so xargs/make fail if any binary did.
testrun: test
	@echo "Running $(words $(TEST_BINARIES)) test binaries ($(JOBS) parallel, SANITIZER=$(SANITIZER))..."
	@printf '%s\n' $(TEST_BINARIES) | xargs -P $(JOBS) -I{} sh -c \
		'out=$$(TESTING=1 DYLD_LIBRARY_PATH=$(RACK_DIR) ./{} $(TEST_SUCCESS_FLAG) 2>&1); status=$$?; echo "=== {} ==="; echo "$$out"; exit $$status'

# Build (if out of date) and run a single test binary, e.g. make testrun-one NAME=Mb
.PHONY: testrun-one
testrun-one: build/test/$(NAME).test
	TESTING=1 DYLD_LIBRARY_PATH=$(RACK_DIR) ./$< $(TEST_SUCCESS_FLAG) $(TEST_FILTER_ARG)
