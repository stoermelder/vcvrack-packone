RACK_DIR ?= ../..

# Orca-c dependency
ORCA_SOURCES = \
	orca-c/field.c \
	orca-c/gbuffer.c \
	orca-c/osc_out.c \
	orca-c/sim.c \
	orca-c/sysmisc.c \
	orca-c/vmio.c \
	orca-c/thirdparty/oso.c

ORCA_GENERATED_HEADER := src/modules/ahab/orca_examples.hpp

# Add .cpp files to the build
SOURCES += $(wildcard src/*.cpp src/**/**/*.cpp)
SOURCES += src/modules/midikit/elk.c
# Exclude test files from the main build
SOURCES := $(filter-out src/test/%.cpp,$(SOURCES))
SOURCES := $(filter-out %.test.cpp,$(SOURCES))
SOURCES += $(ORCA_SOURCES)


# Creates a generated header embedding the ORCA example
# files in `dep/orca-c/examples`. The header is regenerated when any example
# file changes.
orca-examples: $(ORCA_GENERATED_HEADER)

$(ORCA_GENERATED_HEADER): src/modules/ahab/orca_examples.py $(shell find orca-c/examples -type f -name '*.orca')
	python3 src/modules/ahab/orca_examples.py dep/orca-c/examples > $@

include $(RACK_DIR)/arch.mk

# Link Winsock for Windows
ifdef ARCH_WIN
	LDFLAGS += -lws2_32
endif

# Ensure headers from the orca-c tree (and its thirdparty) are found
INCLUDES += -Idep/orca-c -Idep/orca-c/thirdparty


# Add files to the ZIP package when running `make dist`
# The compiled plugin and "plugin.json" are automatically added.
DISTRIBUTABLES += res
DISTRIBUTABLES += $(wildcard LICENSE*)
DISTRIBUTABLES += $(wildcard presets)

# Dependencies
DEP_LOCAL := dep


include $(RACK_DIR)/plugin.mk

ifdef DEBUG
	CXXFLAGS := $(filter-out -fno-omit-frame-pointer,$(CXXFLAGS))
	CXXFLAGS := $(filter-out -funsafe-math-optimizations,$(CXXFLAGS))
	CXXFLAGS := $(filter-out -O3,$(CXXFLAGS))
	CXXFLAGS += -O0 -g
	CFLAGS := $(filter-out -O3,$(CFLAGS))
	CFLAGS += -O0 -g
endif



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
		-I$(CURDIR)/src/test -I$(CURDIR)/src/test $(FLAGS) -O0 \
		-L$(RACK_DIR) -lRack \
		-o $@ $(TEST_ADD_SOURCES) $(CURDIR)/$(TARGET) $<

# Build all test binaries
# Also copy the Rack shared library to build/test/ to avoid runtime linking issues
test: $(TEST_BINARIES) $(TARGET)
	@mkdir -p build/test
	@for f in ../../libRack.*; do \
		if [ -e "$$f" ]; then \
			if [ ! -e "build/test/$$(basename $$f)" ]; then \
				cp "$$f" build/test/ && echo "Copied $$(basename $$f) to build/test"; \
			fi; \
		fi; \
	done
	@cp "$(CURDIR)/$(TARGET)" build/test/ && echo "Copied $(TARGET) to build/test";

# Run all test binaries (exit non-zero on first failure)
testrun: test
	echo "Running tests..."
	@set -e; for t in $(TEST_BINARIES); do \
		echo "Running $$t..."; \
		DYLD_LIBRARY_PATH=$(RACK_DIR) ./$$t $(TEST_SUCCESS_FLAG); \
	done