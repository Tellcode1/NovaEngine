cmake_minimum_required(VERSION 3.16)
project(Nova)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)

set(NOVA_ROOT_DIR ${CMAKE_CURRENT_LIST_DIR})
set(NOVA_BUILD_EXAMPLE true CACHE BOOL "Enable example build")
set(NOVA_SANITIZE false CACHE BOOL "Enable address sanitizer on debug builds")
set(CFLAGS -g -Wall -Wextra -Wpedantic)
set(LDFLAGS -g -Wall -Wextra -Wpedantic)

if(NOT CMAKE_BUILD_TYPE)
    message(STATUS "No build type specified, set to Debug")
    set(CMAKE_BUILD_TYPE "Debug" CACHE STRING "Build type" FORCE)
endif()

#if(CMAKE_BUILD_TYPE STREQUAL "Debug")
#    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -g")
#    # set(CFLAGS "${CFLAGS} -O0")
#    # set(LDFLAGS "${LDFLAGS} -O0")
#endif()

# We don't wanna enable address sanitizer on release builds
if(CMAKE_BUILD_TYPE STREQUAL "Debug" AND NOVA_SANITIZE)
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -fsanitize=address")
endif()

find_package(Vulkan REQUIRED)
include_directories(${Vulkan_INCLUDE_DIRS})

find_package(SDL3 REQUIRED)
include_directories(${SDL3_INCLUDE_DIRS})

find_package(Freetype REQUIRED)
include_directories(${FREETYPE_INCLUDE_DIRS})

find_package(OpenMP REQUIRED)


# The name of the output
set(OUTPUT
    Nova
)

set(SOURCES
    src/ctext/ctext.c
    src/ctext/fontc.c
    src/ctext/render.c

    src/engine/camera.c
    src/engine/collider.c
    src/engine/input.c
    src/engine/scene_object.c
    src/engine/ui.c
    src/engine/window.c

    src/shadersystem/nvsm.c
    src/shadersystem/precompiler.c

    src/atlas.c
    src/clayimpl.c
    src/format.c
    src/image.c

    src/sets/sets.c
)

set(VK_SOURCES
    src/renderer.c
    
    src/iris/buffer.c
    src/iris/descriptors.c
    src/iris/driver.c
    src/iris/memory.c
    src/iris/sampler.c
    src/iris/sprite.c
    src/iris/texture.c

    src/vk/init.c
    src/vk/utils.c
)

set(CLAY_INCLUDE_ALL_EXAMPLES OFF)
set(CLAY_INCLUDE_DEMOS OFF)
set(CLAY_INCLUDE_CPP_EXAMPLE OFF)
set(CLAY_INCLUDE_RAYLIB_EXAMPLES OFF)
set(CLAY_INCLUDE_SDL2_EXAMPLES OFF)
set(CLAY_INCLUDE_SDL3_EXAMPLES OFF)

set(ENABLE_GLSLANG_BINARIES OFF CACHE BOOL "" FORCE)
set(BUILD_TESTING OFF CACHE BOOL "" FORCE)
set(BUILD_EXTERNAL OFF CACHE BOOL "" FORCE)
set(ENABLE_SPVREMAPPER OFF CACHE BOOL "" FORCE)
set(ENABLE_OPT OFF CACHE BOOL "" FORCE)
set(ENABLE_HLSL OFF CACHE BOOL "" FORCE)

set(SPIRV_REFLECT_EXECUTABLE OFF CACHE BOOL "" FORCE)
set(SPIRV_REFLECT_STATIC_LIB ON CACHE BOOL "" FORCE)
set(SPIRV_REFLECT_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(SPIRV_REFLECT_ENABLE_ASSERTS ON CACHE BOOL "" FORCE)
set(SPIRV_REFLECT_ENABLE_ASAN OFF CACHE BOOL "" FORCE)

add_subdirectory(${NOVA_ROOT_DIR}/external/box2d)
add_subdirectory(${NOVA_ROOT_DIR}/external/volk)
add_subdirectory(${NOVA_ROOT_DIR}/external/clay)
add_subdirectory(${NOVA_ROOT_DIR}/external/glslang)
add_subdirectory(${NOVA_ROOT_DIR}/external/spirv_reflect)
add_subdirectory(${NOVA_ROOT_DIR}/include/std)

if(NOVA_BUILD_EXAMPLE)
    add_executable(nova_example ${NOVA_ROOT_DIR}/src/main.c ${VK_SOURCES})
endif()

add_library(Nova STATIC ${SOURCES})

set(GLSLANG_LIBS
    glslang
    OSDependent
    glslang-default-resource-limits
    MachineIndependent
    SPIRV)

set(LIBS
    nvstd
    volk
    m
    SDL3
    freetype
    box2d
    OpenMP::OpenMP_C
    ${GLSLANG_LIBS}
    spirv-reflect-static
)

# Box2D uses C17 but still complains about C11 features.
# target_compile_options(box2d PRIVATE -Wall -Wextra -Wno-unused-function -Wno-typedef-redefinition)

target_precompile_headers(Nova PUBLIC ${NOVA_ROOT_DIR}/include/std/include/stdafx.h)
target_link_options(Nova PRIVATE ${LDFLAGS})
target_compile_options(Nova PRIVATE ${CFLAGS})
target_link_libraries(Nova ${LIBS})

add_executable(fontc ${NOVA_ROOT_DIR}/src/ctext/fontc.c)
target_compile_definitions(fontc PRIVATE FONTC_EXECUTABLE=1)
target_link_options(fontc PRIVATE ${CFLAGS} -fopenmp)
target_compile_options(fontc PRIVATE ${LDFLAGS} -fopenmp)
target_link_libraries(fontc Nova nvstd freetype OpenMP::OpenMP_C m)

file(COPY ${NOVA_ROOT_DIR}/Assets DESTINATION ${CMAKE_BINARY_DIR}/)
file(COPY ${NOVA_ROOT_DIR}/src/shaders DESTINATION ${CMAKE_BINARY_DIR}/)
file(COPY ${NOVA_ROOT_DIR}/cfg DESTINATION ${CMAKE_BINARY_DIR}/)

target_link_options(nova_example PRIVATE ${LDFLAGS})
target_compile_options(nova_example PRIVATE ${CFLAGS})

if(NOVA_BUILD_EXAMPLE)
    target_link_libraries(nova_example Nova)
endif()
