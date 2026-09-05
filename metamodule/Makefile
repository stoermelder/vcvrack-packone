# Makefile wrapper for the MetaModule plugin build.
#
# Usage:
#   make                 Configure (if needed) and build the plugin
#   make clean           Remove the build/ directory
#
# METAMODULE_SDK_DIR can point to an existing SDK checkout, e.g.:
#   make METAMODULE_SDK_DIR=/Users/ben/Dev/metamodule-plugin-sdk
#
# ARM_TOOLCHAIN_BIN can point to the arm-none-eabi-gcc/g++ bin directory if
# it is not already on PATH.

SDK_BRANCH ?= main
METAMODULE_SDK_DIR ?= metamodule-plugin-sdk

# Only search well-known install locations if the compiler isn't already
# reachable via PATH. Newest version wins where a glob matches several.
ifeq ($(shell command -v arm-none-eabi-gcc 2>/dev/null),)
ARM_TOOLCHAIN_SEARCH := \
	$(wildcard /Applications/ArmGNUToolchain/*/arm-none-eabi/bin) \
	$(wildcard /opt/ArmGNUToolchain/*/arm-none-eabi/bin) \
	$(wildcard /usr/local/arm-none-eabi/*/bin) \
	$(wildcard C:/ArmGNUToolchain/*/arm-none-eabi/bin) \
	$(wildcard $(LOCALAPPDATA)/Arm GNU Toolchain*/*/bin)
ARM_TOOLCHAIN_BIN ?= $(lastword $(sort $(ARM_TOOLCHAIN_SEARCH)))
endif

ifneq ($(ARM_TOOLCHAIN_BIN),)
export PATH := $(ARM_TOOLCHAIN_BIN):$(PATH)
endif

BUILD_DIR := build

.PHONY: all build sdk clean distclean

all: build

build:
	@jq -e . plugin-mm.json >/dev/null || (echo "plugin-mm.json is invalid" && exit 1)
	cmake --fresh -B build -G Ninja -DMETAMODULE_SDK_DIR=$(METAMODULE_SDK_DIR)
	cmake --build $(BUILD_DIR)

clean:
	rm -rf $(BUILD_DIR)
