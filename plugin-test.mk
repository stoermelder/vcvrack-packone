# Test build rules
# Use "make test" to build tests
# Use "make testrun" to build and run tests
# Use "make testrun SUCCESS=1" to print test success messages
# Use "make testrun-one NAME=<Module>" to run a single test binary (builds it if needed)
# Use "make test-one NAME=<Module>" to rebuild (if out of date) and run a single test binary

ifdef SUCCESS
	TEST_SUCCESS_FLAG = --success
endif

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
		-I$(CURDIR)/src/test -I$(CURDIR)/src/test $(FLAGS) -O0 -UNDEBUG -DDEBUGPLUGIN \
		-L$(RACK_DIR) -lRack \
		-o $@ $(TEST_ADD_SOURCES) $(CURDIR)/$(TARGET) $<

# Build all test binaries
test: $(TEST_BINARIES) $(TARGET)

# Run all test binaries (exit non-zero on first failure)
testrun: test
	echo "Running tests..."
	@set -e; for t in $(TEST_BINARIES); do \
		echo "Running $$t..."; \
		TESTING=1 DYLD_LIBRARY_PATH=$(RACK_DIR) ./$$t $(TEST_SUCCESS_FLAG); \
	done

# Run a single test binary (builds it if needed), e.g. make testrun-one NAME=Mb
# The binary is build/test/<NAME>.test.
.PHONY: testrun-one
testrun-one: build/test/$(NAME).test
	TESTING=1 DYLD_LIBRARY_PATH=$(RACK_DIR) ./$< $(TEST_SUCCESS_FLAG)

# Rebuild (if out of date) and run a single test binary, e.g. make testone NAME=Mb
# Rebuilds the plugin dylib first, removes the stale binary so it is freshly
# relinked, then runs it via `testrun-one`.
.PHONY: test-one
test-one: $(TARGET)
	rm -f build/test/$(NAME).test
	$(MAKE) testrun-one NAME=$(NAME)