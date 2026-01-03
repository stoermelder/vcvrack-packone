RACK_DIR ?= ../..

# Add .cpp files to the build
SOURCES += $(wildcard src/*.cpp src/**/**/*.cpp)
SOURCES := $(filter-out src/test/%.cpp,$(SOURCES))

# Add files to the ZIP package when running `make dist`
# The compiled plugin and "plugin.json" are automatically added.
DISTRIBUTABLES += res
DISTRIBUTABLES += $(wildcard LICENSE*)
DISTRIBUTABLES += $(wildcard presets)

include $(RACK_DIR)/plugin.mk

ifdef DEBUG
	CXXFLAGS := $(filter-out -fno-omit-frame-pointer,$(CXXFLAGS))
	CXXFLAGS := $(filter-out -funsafe-math-optimizations,$(CXXFLAGS))
	CXXFLAGS := $(filter-out -O3,$(CXXFLAGS))
	CXXFLAGS += -O0 -g
	CFLAGS += -O0 -g
endif


# Unit test build rules
# Use "make test" to build tests
# Use "make testrun" to build and run tests
# Use "make testrun SUCCESS=1" to print test success messages

ifdef SUCCESS
  	TEST_SUCCESS_FLAG = --success
endif

TEST_SOURCES += $(wildcard src/test/*.test.cpp)

# Build each test source into its own executable under build/test/
TEST_BINARIES := $(patsubst src/test/%.cpp,build/test/%,$(TEST_SOURCES))

# Pattern rule to build an individual test executable
build/test/%: src/test/%.cpp $(CURDIR)/src/test/catch2/catch_amalgamated.cpp
	@mkdir -p $(dir $@)
	@echo "Building test $@..."
	@$(CXX) -std=c++14 \
		-I$(CURDIR)/src/test -I$(CURDIR)/src/test/catch2 $(FLAGS) -O0 \
		-L$(RACK_DIR) -lRack \
		-o $@ $(CURDIR)/src/test/catch2/catch_amalgamated.cpp $<

# Build all test binaries
# Also copy the Rack shared library to build/test/ to avoid runtime linking issues
test: $(TEST_BINARIES)
	@mkdir -p build/test
	@for f in ../../libRack.*; do \
		if [ -e "$$f" ]; then \
			if [ ! -e "build/test/$$(basename $$f)" ]; then \
				cp "$$f" build/test/ && echo "Copied $$(basename $$f) to build/test"; \
			fi; \
		fi; \
	done

# Run all test binaries (exit non-zero on first failure)
testrun: test
	@echo "Running tests..."
	@set -e; for t in $(TEST_BINARIES); do DYLD_LIBRARY_PATH=$(RACK_DIR) ./$$t $(TEST_SUCCESS_FLAG); done