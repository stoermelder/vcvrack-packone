# Test build rules
# Use "make test" to build tests
# Use "make testrun" to build and run tests (parallel; set JOBS=N to control fan-out, default 8)
# Use "make testrun SUCCESS=1" to print test success messages
# Use "make testrun-one NAME=<Module>" to build (if needed) and run a single test binary
# Use "make testrun-one NAME=<Module> FILTER='[tag]'" to run only matching TEST_CASEs
# Use "make test SANITIZER=thread" (or =undefined, =address) to switch sanitizers; default address.
# TSan is required for any concurrent (ThreadedHarness-style) test.

ifdef SUCCESS
	TEST_SUCCESS_FLAG = --success
endif

ifdef FILTER
	TEST_FILTER_ARG = "$(FILTER)"
endif

SANITIZER ?= address

# Number of test binaries to run concurrently in `testrun`. ASan/TSan/UBSan runtimes are
# independent per-process, so running binaries in parallel is safe; only the binaries
# themselves are serial internally.
JOBS ?= 8

TEST_SOURCES += $(wildcard src/**/*.test.cpp src/**/**/*.test.cpp)
TEST_ADD_SOURCES := $(CURDIR)/src/test/catch_amalgamated.cpp

# Every project header. Test/perf binaries #include module sources and utility
# headers directly, so without these as prerequisites a header edit leaves the
# binary stale and `make testrun`/`make perfrun` silently re-runs old code.
# $(TARGET) is a prerequisite for the same reason: the binaries link it in.
TEST_HEADERS := $(wildcard src/*.hpp src/*.h src/**/*.hpp src/**/*.h src/**/**/*.hpp src/**/**/*.h)

# Build each test source into its own executable under build/test/ using basenames
TEST_NAMES := $(patsubst %.cpp,%,$(notdir $(TEST_SOURCES)))
TEST_BINARIES := $(patsubst %,build/test/%,$(TEST_NAMES))

# Allow pattern rule to locate test source files by searching these directories
VPATH := $(sort $(dir $(TEST_SOURCES)))

# Pattern rule to build an individual test executable
build/test/%: %.cpp $(TEST_HEADERS) $(TARGET)
	@mkdir -p $(dir $@)
	@echo "Building $@..."
	@$(CXX) -std=c++14 \
		-I$(CURDIR)/src/test $(FLAGS) -O0 -UNDEBUG -DDEBUGPLUGIN -fsanitize=$(SANITIZER) -fno-omit-frame-pointer \
		-L$(RACK_DIR) -lRack \
		-o $@ $(TEST_ADD_SOURCES) $(CURDIR)/$(TARGET) $<

# Build all test binaries
test: $(TEST_BINARIES) $(TARGET)

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
# The binary is build/test/<NAME>.test. Its own build/test/%: %.cpp $(TEST_HEADERS) $(TARGET)
# prerequisites (above) already relink it whenever the plugin dylib or any project header is
# stale, so this one target covers both "just run it" and "rebuild everything, then run it" —
# there used to be a separate test-one target for the latter, but once $(TARGET) became a
# prerequisite of the binary itself the two were identical, just reached via a different number
# of `make` invocations. Removed rather than kept as a confusing alias.
.PHONY: testrun-one
testrun-one: build/test/$(NAME).test
	TESTING=1 DYLD_LIBRARY_PATH=$(RACK_DIR) ./$< $(TEST_SUCCESS_FLAG) $(TEST_FILTER_ARG)