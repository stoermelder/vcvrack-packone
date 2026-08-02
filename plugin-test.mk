# Test build rules
# Use "make test" to build tests
# Use "make testrun" to build and run tests
# Use "make testrun SUCCESS=1" to print test success messages

ifdef SUCCESS
	TEST_SUCCESS_FLAG = --success
endif

TEST_SOURCES += $(wildcard src/**/*.test.cpp src/**/**/*.test.cpp)
TEST_ADD_SOURCES := $(CURDIR)/src/test/catch_amalgamated.cpp

# Build each test source into its own executable under build/test/ using basenames
TEST_NAMES := $(patsubst %.cpp,%,$(notdir $(TEST_SOURCES)))
TEST_BINARIES := $(patsubst %,build/test/%,$(TEST_NAMES))

# Allow pattern rule to locate test source files by searching these directories
VPATH := $(sort $(dir $(TEST_SOURCES)))

# Pattern rule to build an individual test executable
build/test/%: %.cpp $(CURDIR)/src/test/test_context.hpp
	@mkdir -p $(dir $@)
	@echo "Building $@..."
	@$(CXX) -std=c++14 \
		-I$(CURDIR)/src/test -I$(CURDIR)/src/test $(FLAGS) -O0 -UNDEBUG \
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