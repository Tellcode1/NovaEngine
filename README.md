## NovaEngine

A Vulkan-based game engine primarily written in **C99**.  
**Dependencies**: All dependencies used by this project are loaded as submodules. Install all of them by running the following command in any of the projects directories:
```bash
git submodule update --init --recursive
```

## Installation

Clone the project repository:

Navigate to the project directory and initialize submodules:

Create a build directory and build the example:

Run the example:

## Usage

NovaEngine builds a static library (`Nova`) that can be linked with your project.  
Along with the source project, Nova also provides an example and a test for the string library.

### Using CMake

Add NovaEngine as a subdirectory in your `CMakeLists.txt`:

Link the library statically:

### Using Make

Build NovaEngine:

Link statically with your project:

```plaintext
cc your_project.o -lNova
```

```plaintext
make
```

```plaintext
target_link_libraries(${PROJECT_NAME} Nova)
```

```plaintext
add_subdirectory(${CMAKE_SOURCE_DIR}/NovaEngine)
```

```plaintext
./nova_example
```

```plaintext
mkdir build
cd build
cmake .. -DNOVA_BUILD_EXAMPLE=1
make -j
```

```plaintext
cd NovaEngine
git submodule update --init --recursive
```

```plaintext
git clone https://github.com/Tellcode1/NovaEngine.git
```
