A C Vulkan based game engine. Primarily written in C99.
Freetype, SDL and Vulkan are the only dependancies (uses git submodule).

## Installation
1. clone project repo:
   ```bash
   git clone https://github.com/Tellcode1/NovaEngine.git

2. move to project source and install submodules:
  ```bash
  cd NovaEngine ; git submodule update --init --recursive
3. make build directory and build example:
  ```bash
  mkdir build; cd build; cmake .. -DNOVA_BUILD_EXAMPLE=1 ; make -j
4. run example:
  ```bash
  ./nova_example

The project builds a static library (Nova) that you can link with.
Nova provides both a Makefile and a CMakeFile in the repository.

When using CMake, add the project as subdirectory:
  ```bash
  add_subdirectory(${CMAKE_SOURCE_DIR}/NovaEngine)
And then, link statically:
  ```bash
  target_link_libraries({PROJECT NAME} Nova)

When using make, Just make the project and statically link with it:
  ```bash
  cc your_project.o -l Nova