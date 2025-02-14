# NovaEngine

A Vulkan-based game engine primarily written in **C99**.  
**Dependencies**: FreeType, SDL, and Vulkan (managed via Git submodules).

## Installation

1. Clone the project repository:  
   ```bash
   git clone https://github.com/Tellcode1/NovaEngine.git
   ```

2. Navigate to the project directory and initialize submodules:  
   ```bash
   cd NovaEngine
   git submodule update --init --recursive
   ```

3. Create a build directory and build the example:  
   ```bash
   mkdir build
   cd build
   cmake .. -DNOVA_BUILD_EXAMPLE=1
   make -j
   ```

4. Run the example:  
   ```bash
   ./nova_example
   ```

---

## Usage

NovaEngine builds a static library (`Nova`) that can be linked with your project.  

### Using CMake  
1. Add NovaEngine as a subdirectory in your `CMakeLists.txt`:  
   ```cmake
   add_subdirectory(${CMAKE_SOURCE_DIR}/NovaEngine)
   ```

2. Link the library statically:  
   ```cmake
   target_link_libraries(${PROJECT_NAME} Nova)
   ```

### Using Make  
1. Build NovaEngine:  
   ```bash
   make
   ```

2. Link statically with your project:  
   ```bash
   cc your_project.o -lNova
   ```