PLUGIN_ROOT := ../..
RACK_DIR    ?= $(PLUGIN_ROOT)/../..

CXX ?= c++
CC  ?= cc

INCLUDES := \
	-I$(PLUGIN_ROOT)/src/modules/siren \
	-I$(PLUGIN_ROOT)/dep/drlibs \
	-I$(RACK_DIR)/dep/pffft \
	-I$(RACK_DIR)/dep/include

OUT := build/siren_extract_features

.PHONY: all clean

all: $(OUT)

build/pffft.o: $(RACK_DIR)/dep/pffft/pffft.c | build
	$(CC) -O2 $(INCLUDES) -c $< -o $@

$(OUT): siren_extract_features.cpp build/pffft.o | build
	$(CXX) -std=c++11 -O2 $(INCLUDES) -o $@ $^ -lm

build:
	mkdir -p build

clean:
	rm -f $(OUT) build/pffft.o
