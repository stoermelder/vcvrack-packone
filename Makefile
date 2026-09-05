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

# Add .cpp files to the build
SOURCES += $(wildcard src/*.cpp src/**/*.cpp src/**/**/*.cpp)
# Exclude test files from the main build
SOURCES := $(filter-out src/test/%.cpp,$(SOURCES))
SOURCES := $(filter-out %.test.cpp,$(SOURCES))
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


include plugin-test.mk
include plugin-check.mk