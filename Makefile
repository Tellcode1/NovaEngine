CC = clang

CFLAGS = -g -Wall -Wextra -Werror -pthread -std=c99 -Wno-typedef-redefinition
LDFLAGS = -g -std=c99

PCH = build/pch.gch

LIB_FREETYPE = $(shell pkg-config --libs freetype2)
LIB_SDL = $(shell pkg-config --libs sdl2)
LIB_VULKAN = $(shell pkg-config --libs vulkan)

LIBS_EXTERNAL = freetype2 sdl2 vulkan
INCL = -include-pch $(PCH) $(shell pkg-config --cflags freetype2 sdl2 vulkan) 
LIBS = -ljpeg -lpng -lz -lm -lomp
LIBS_MODULE = -L$(VOLK_BUILD_DIR) -L$(BOX2D_BUILD_DIR) -lvolk -lbox2d
CORE = build/core.o

NVSM_BINARY = build/nvsm
FONTC_BINARY = build/fontc

BOX2D_BUILD_DIR = build/box2d/src
VOLK_BUILD_DIR = build/volk

BOX2D_LIB = $(BOX2D_BUILD_DIR)/libbox2d.a
VOLK_LIB = $(VOLK_BUILD_DIR)/libvolk.a

SOURCES = src/engine.c src/main.c src/fontc.c src/nvsm.c src/vk.c
OBJECTS = $(patsubst src/%.c,build/%.o,$(SOURCES))

CORE_SOURCE = src/core.c

.PHONY: clean clean-all all

all: build/nova_example $(NVSM_BINARY) $(FONTC_BINARY)

run: build/nova_example
	

build/nova_example: $(BOX2D_LIB) $(VOLK_LIB) $(PCH) $(CORE) $(OBJECTS)
	$(CC) $(CFLAGS) $(LDFLAGS) $(INCL) -o $@ $(CORE) $(OBJECTS) $(LIBS) $(LIB_FREETYPE) $(LIB_SDL) $(LIB_VULKAN) $(LIBS_MODULE) -DNVSM=1 -DFONTC=1

build/%.o: src/%.c $(PCH)
	$(CC) $(CFLAGS) $(INCL) -c $< -o $@

$(CORE): $(CORE_SOURCE) $(PCH)
	mkdir -p build
	$(CC) $(CFLAGS) $(LDFLAGS) $(INCL) -c $< -o $@

$(NVSM_BINARY): $(CORE) $(PCH) src/nvsm.c
	$(CC) $(CFLAGS) $(LDFLAGS) $(INCL) $(LIBS) $(CORE) src/nvsm.c -DNVSM=1 -DNVSM_EXECUTABLE=1 -o $@

$(FONTC_BINARY): $(CORE) $(PCH) src/fontc.c
	$(CC) $(CFLAGS) $(LDFLAGS) $(INCL) $(LIBS) $(LIB_FREETYPE) $(CORE) src/fontc.c -DFONTC=1 -DFONTC_EXECUTABLE=1 -o $@

$(BOX2D_LIB):
	mkdir -p build/box2d
	set -e; cmake -DBOX2D_UNIT_TESTS=OFF -DBOX2D_SAMPLES=OFF -S external/box2d -B build/box2d
	$(MAKE) -C build/box2d -j$(shell nproc 2>/dev/null || sysctl -n hw.ncpu)

$(VOLK_LIB):
	mkdir -p build/volk
	set -e; cmake -S external/volk -B build/volk
	$(MAKE) -C build/volk -j$(shell nproc 2>/dev/null || sysctl -n hw.ncpu)

$(PCH): std/stdafx.h
	mkdir -p build
	$(CC) $(CFLAGS) -x c-header $< -o $@

clean:
	rm -f $(PCH) $(CORE) build/nvsm build/fontc build/nova_example

clean-all: clean
	rm -rf build/volk build/box2d
