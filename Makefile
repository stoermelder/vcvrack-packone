RACK_DIR ?= ../..

# Orca-c dependency
ORCA_SOURCES = \
	dep/orca-c/field.c \
	dep/orca-c/gbuffer.c \
	dep/orca-c/osc_out.c \
	dep/orca-c/sim.c \
	dep/orca-c/sysmisc.c \
	dep/orca-c/vmio.c \
	dep/orca-c/thirdparty/oso.c

ORCA_GENERATED_HEADER := src/modules/ahab/orca_examples.hpp

# SoundTouch dependency (used by Siren for repitch processing)
SOUNDTOUCH_SOURCES = \
	dep/soundtouch/source/SoundTouch/AAFilter.cpp \
	dep/soundtouch/source/SoundTouch/BPMDetect.cpp \
	dep/soundtouch/source/SoundTouch/FIFOSampleBuffer.cpp \
	dep/soundtouch/source/SoundTouch/FIRFilter.cpp \
	dep/soundtouch/source/SoundTouch/InterpolateCubic.cpp \
	dep/soundtouch/source/SoundTouch/InterpolateLinear.cpp \
	dep/soundtouch/source/SoundTouch/InterpolateShannon.cpp \
	dep/soundtouch/source/SoundTouch/PeakFinder.cpp \
	dep/soundtouch/source/SoundTouch/RateTransposer.cpp \
	dep/soundtouch/source/SoundTouch/SoundTouch.cpp \
	dep/soundtouch/source/SoundTouch/TDStretch.cpp \
	dep/soundtouch/source/SoundTouch/cpu_detect_x86.cpp \
	dep/soundtouch/source/SoundTouch/mmx_optimized.cpp \
	dep/soundtouch/source/SoundTouch/sse_optimized.cpp

# QuickJS dependency (used by MIDI-KIT)
QUICKJS_SOURCES = \
	dep/quickjs/quickjs.c \
	dep/quickjs/cutils.c \
	dep/quickjs/libregexp.c \
	dep/quickjs/libunicode.c \
	dep/quickjs/dtoa.c

# Add .cpp files to the build
SOURCES += $(wildcard src/*.cpp src/**/**/*.cpp)
SOURCES += src/modules/midikit/minilua.c
SOURCES += $(QUICKJS_SOURCES)
# Exclude test files from the main build
SOURCES := $(filter-out src/test/%.cpp,$(SOURCES))
SOURCES := $(filter-out %.test.cpp,$(SOURCES))
# Performance harnesses are a standalone measurement tool, never part of the plugin dylib
SOURCES := $(filter-out %.perf.cpp,$(SOURCES))
SOURCES += $(ORCA_SOURCES)
SOURCES += $(SOUNDTOUCH_SOURCES)


# Creates a generated header embedding the ORCA example
# files in `orca-c/examples`. The header is regenerated when any example
# file changes.
orca-examples: $(ORCA_GENERATED_HEADER)

$(ORCA_GENERATED_HEADER): src/modules/ahab/orca_examples.py $(shell find dep/orca-c/examples -type f -name '*.orca')
	python3 src/modules/ahab/orca_examples.py dep/orca-c/examples > $@

include $(RACK_DIR)/arch.mk

# Link libraries for Windows
ifdef ARCH_WIN
	LDFLAGS += -lws2_32 -lopengl32
endif

# Ensure headers from the orca-c tree (and its thirdparty) are found
FLAGS += -Idep
# Ensure headers from the SoundTouch library are found
FLAGS += -Idep/soundtouch/include -Idep/soundtouch/source/SoundTouch

# QuickJS's own sources #include "quickjs-atom.h" etc. unqualified, so they
# need dep/quickjs on the search path — but adding that path plugin-wide
# would let a bare #include <version> resolve to dep/quickjs/version instead
# of the real C++ header, since the two collide on a case-insensitive
# filesystem. A pattern rule that's more specific than compile.mk's own
# "build/%.c.o: %.c" scopes the extra -I (and QuickJS's required
# CONFIG_VERSION define) to just its own objects; everything else (including
# MidiScriptEngineQuickJs.h's #include "quickjs.h", found via plain -Idep)
# is unaffected.
QUICKJS_FLAGS := -Idep/quickjs -DCONFIG_VERSION="\"2026-06-04\""
# QuickJS's own -Wsign-compare noise (unsigned/signed comparisons throughout
# its tagged-value code) isn't ours to fix; silence it here rather than for
# the whole plugin.
QUICKJS_FLAGS += -Wno-sign-compare
build/dep/quickjs/%.c.o: dep/quickjs/%.c
	@mkdir -p $(@D)
	$(CC) $(CFLAGS) $(QUICKJS_FLAGS) -c -o $@ $<


# Add files to the ZIP package when running `make dist`
# The compiled plugin and "plugin.json" are automatically added.
DISTRIBUTABLES += res
DISTRIBUTABLES += $(wildcard LICENSE*)
DISTRIBUTABLES += $(wildcard presets)

# Redirecting into an ignored folder to suppress folder creation
DEP_LOCAL := build/.dep


include $(RACK_DIR)/plugin.mk

ifdef DEBUGPLUGIN
	CXXFLAGS := $(filter-out -fno-omit-frame-pointer,$(CXXFLAGS))
	CXXFLAGS := $(filter-out -funsafe-math-optimizations,$(CXXFLAGS))
	CXXFLAGS := $(filter-out -O3,$(CXXFLAGS))
	CXXFLAGS += -O0 -g -DDEBUGPLUGIN
	CFLAGS := $(filter-out -O3,$(CFLAGS))
	CFLAGS += -O0 -g -DDEBUGPLUGIN
endif

# Test build rules live in plugin-test.mk
include plugin-test.mk

# Performance binaries (*.perf.cpp) live in their own makefile so they stay
# out of the main build/test flow. See perf.mk for the `perf`/`perfrun` targets.
include plugin-perf.mk