# ---------------------------------------------------------------------------
# Performance binaries (*.perf.cpp) — deliberately NOT part of `make test` /
# `make testrun`. The test discovery in the main Makefile is a blind
# `wildcard *.test.cpp` with no tag filter, so a perf source can never be a
# `.test.cpp` file; it uses the distinct `.perf.cpp` suffix and its own
# targets (`make perf` / `make perfrun`). Build/run separately, e.g.:
#   make perf && DYLD_LIBRARY_PATH=../.. TESTING=1 ./build/test/MidiKitRoundTrip.perf
#
# Included from the main Makefile via `include perf.mk`.
# ---------------------------------------------------------------------------
PERF_SOURCES += $(wildcard src/**/*.perf.cpp src/**/**/*.perf.cpp)
PERF_NAMES := $(patsubst %.cpp,%,$(notdir $(PERF_SOURCES)))
PERF_BINARIES := $(patsubst %,build/test/%,$(PERF_NAMES))

# Static pattern rule: explicit target list, so it wins over the generic
# `build/test/%` rule for exactly these targets. The perf sources live under
# src/modules/midikit/.
$(PERF_BINARIES): build/test/%: src/modules/midikit/%.cpp $(TEST_HEADERS) $(TARGET)
	@mkdir -p $(dir $@)
	@echo "Building $@..."
	@$(CXX) -std=c++14 \
		-I$(CURDIR)/src/test -I$(CURDIR)/src/test $(FLAGS) -O0 -UNDEBUG \
		-L$(RACK_DIR) -lRack \
		-o $@ $(TEST_ADD_SOURCES) $(CURDIR)/$(TARGET) $<

# Build perf binaries
perf: $(PERF_BINARIES) $(TARGET)

# Build and run perf binaries
perfrun: perf
	echo "Running perf binaries..."
	@set -e; for t in $(PERF_BINARIES); do \
		echo "Running $$t..."; \
		TESTING=1 DYLD_LIBRARY_PATH=$(RACK_DIR) ./$$t; \
	done