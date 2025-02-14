CC = clang

CFLAGS = -g -Wall
LDFLAGS = -g

PCH = build/pch.gch
INCL = -include-pch $(PCH) $(shell pkg-config --cflags freetype2 sdl2 vulkan)
LIBS = -ljpeg -lpng -lz -lm -lomp $(shell pkg-config --libs sdl2 freetype2 vulkan)
LIBS_EXTERNAL = -L$(VOLK_BUILD_DIR) -L$(BOX2D_BUILD_DIR) -lvolk -lbox2d
CORE = build/core.o

NVSM_BINARY = build/nvsm
FONTC_BINARY = build/fontc

BOX2D_BUILD_DIR = build/box2d/src
VOLK_BUILD_DIR = build/volk

BOX2D_LIB = $(BOX2D_BUILD_DIR)/libbox2d.a
VOLK_LIB = $(VOLK_BUILD_DIR)/libvolk.a

SOURCES = src/engine.c src/main.c src/preprocessors.c src/vk.c
CORE_SOURCE = src/core.c

.PHONY: clean clean-all all

all: build/nova_example $(NVSM_BINARY) $(FONTC_BINARY)

build/nova_example: $(BOX2D_LIB) $(VOLK_LIB) $(PCH) $(CORE) $(SOURCES)
	$(CC) $(CFLAGS) $(LDFLAGS) $(INCL) -o $@ $(CORE) $(SOURCES) $(LIBS) $(LIBS_EXTERNAL) -DNVSM=1 -DFONTC=1

$(CORE): $(CORE_SOURCE) $(PCH)
	mkdir -p build
	$(CC) $(CFLAGS) $(LDFLAGS) $(INCL) -c $< -o $@

$(NVSM_BINARY): $(CORE) $(PCH) src/preprocessors.c
	$(CC) $(CFLAGS) $(LDFLAGS) $(INCL) $(LIBS) $(CORE) src/preprocessors.c -DNVSM=1 -DNVSM_EXECUTABLE=1 -o $@

$(FONTC_BINARY): $(CORE) $(PCH) src/preprocessors.c
	$(CC) $(CFLAGS) $(LDFLAGS) $(INCL) $(LIBS) $(CORE) src/preprocessors.c -DFONTC=1 -DFONTC_EXECUTABLE=1 -o $@

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
	$(CC) $(CFLAGS) -x c-header -pthread $< -o $@

clean:
	rm -f $(PCH) $(CORE) build/nvsm build/fontc build/nova_example

clean-all: clean
	rm -rf build/volk build/box2d
