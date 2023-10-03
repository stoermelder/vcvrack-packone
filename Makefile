RACK_DIR ?= ../..

SOURCES += $(wildcard src/*.cpp) $(wildcard src/mb/*.cpp) $(wildcard src/drivers/*.cpp) src/elk/elk.c

DISTRIBUTABLES += $(wildcard LICENSE*) res presets

include $(RACK_DIR)/plugin.mk


win-dist: all
	rm -rf dist
	mkdir -p dist/$(SLUG)
	@# Strip and copy plugin binary
	cp $(TARGET) dist/$(SLUG)/
ifdef ARCH_MAC
	$(STRIP) -S dist/$(SLUG)/$(TARGET)
else
	$(STRIP) -s dist/$(SLUG)/$(TARGET)
endif
	@# Copy distributables
	cp -R $(DISTRIBUTABLES) dist/$(SLUG)/
	@# Create vcvplugin package
	cd dist && tar -c $(SLUG) | zstd -$(ZSTD_COMPRESSION_LEVEL) -o "$(SLUG)"-"$(VERSION)"-$(ARCH_NAME).vcvplugin