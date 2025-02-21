CC = clang

CFLAGS = -g -Wall -Wextra -Werror -pthread -std=c99 -Wno-typedef-redefinition
LDFLAGS = -g -std=c99

BUILD_DIR ?= build
PCH = $(BUILD_DIR)/pch.gch

LIB_FREETYPE = $(shell pkg-config --libs freetype2)
LIB_SDL = $(shell pkg-config --libs sdl2)
LIB_VULKAN = $(shell pkg-config --libs vulkan)

LIBS_EXTERNAL = freetype2 sdl2 vulkan
INCL = -include-pch $(PCH) $(shell pkg-config --cflags freetype2 sdl2 vulkan) 
LIBS = -ljpeg -lpng -lz -lm -lomp
LIBS_MODULE = -L$(VOLK_BUILD_DIR) -L$(BOX2D_BUILD_DIR) -lvolk -lbox2d
CORE = $(BUILD_DIR)/core.o

NVSM_BINARY = $(BUILD_DIR)/nvsm
FONTC_BINARY = $(BUILD_DIR)/fontc

BOX2D_BUILD_DIR = $(BUILD_DIR)/box2d/src
VOLK_BUILD_DIR = $(BUILD_DIR)/volk

BOX2D_LIB = $(BOX2D_BUILD_DIR)/libbox2d.a
VOLK_LIB = $(VOLK_BUILD_DIR)/libvolk.a

SOURCES = src/engine.c src/main.c src/fontc.c src/nvsm.c src/vk.c
OBJECTS = $(patsubst src/%.c,$(BUILD_DIR)/%.o,$(SOURCES))

BUILD_TEST ?= false

CORE_SOURCE = src/core.c

.PHONY: clean clean-all all

all: $(BUILD_DIR)/nova_example $(NVSM_BINARY) $(FONTC_BINARY)
	cp -r Assets/ $(BUILD_DIR)

test: $(BUILD_DIR)/nova_string_test
	$(BUILD_DIR)/nova_string_test

run: $(BUILD_DIR)/nova_example
	
$(BUILD_DIR)/nova_example: $(BOX2D_LIB) $(VOLK_LIB) $(PCH) $(CORE) $(OBJECTS)
	$(CC) $(CFLAGS) $(LDFLAGS) $(INCL) -o $@ $(CORE) $(OBJECTS) $(LIBS) $(LIB_FREETYPE) $(LIB_SDL) $(LIB_VULKAN) $(LIBS_MODULE) -DNVSM=1 -DFONTC=1

$(BUILD_DIR)/nova_string_test: $(PCH) $(CORE) src/test.c
	$(CC) $(CFLAGS) $(LDFLAGS) $(INCL) src/test.c -o $@ $(CORE) $(LIBS)

$(BUILD_DIR)/%.o: src/%.c $(PCH)
	$(CC) $(CFLAGS) $(INCL) -c $< -o $@

$(CORE): $(CORE_SOURCE) $(PCH)
	mkdir -p $(BUILD_DIR)
	$(CC) $(CFLAGS) $(LDFLAGS) $(INCL) -c $< -o $@

$(NVSM_BINARY): $(CORE) $(PCH) src/nvsm.c
	$(CC) $(CFLAGS) $(LDFLAGS) $(INCL) $(LIBS) $(CORE) src/nvsm.c -DNVSM=1 -DNVSM_EXECUTABLE=1 -o $@

$(FONTC_BINARY): $(CORE) $(PCH) src/fontc.c
	$(CC) $(CFLAGS) $(LDFLAGS) $(INCL) $(LIBS) $(LIB_FREETYPE) $(CORE) src/fontc.c -DFONTC=1 -DFONTC_EXECUTABLE=1 -o $@

$(BOX2D_LIB):
	mkdir -p $(BUILD_DIR)/box2d
	set -e; cmake -DBOX2D_UNIT_TESTS=OFF -DBOX2D_SAMPLES=OFF -S external/box2d -B $(BUILD_DIR)/box2d
	$(MAKE) -C $(BUILD_DIR)/box2d -j$(shell nproc 2>/dev/null || sysctl -n hw.ncpu)

$(VOLK_LIB):
	mkdir -p $(BUILD_DIR)/volk
	set -e; cmake -S external/volk -B $(BUILD_DIR)/volk
	$(MAKE) -C $(BUILD_DIR)/volk -j$(shell nproc 2>/dev/null || sysctl -n hw.ncpu)

$(PCH): std/stdafx.h
	mkdir -p $(BUILD_DIR)
	$(CC) $(CFLAGS) -x c-header $< -o $@

clean:
	rm -f $(PCH) $(CORE) $(BUILD_DIR)/nvsm $(BUILD_DIR)/fontc $(BUILD_DIR)/nova_example

clean-all: clean
	rm -rf $(BUILD_DIR)/volk $(BUILD_DIR)/box2d

format:
	./format.sh
