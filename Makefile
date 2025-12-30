RACK_DIR ?= ../..


SOURCES += $(wildcard src/*.cpp src/**/**/*.cpp)

DISTRIBUTABLES += $(wildcard LICENSE*) res presets

include $(RACK_DIR)/plugin.mk