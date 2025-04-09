cmake_minimum_required(VERSION 3.16)
project(main)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)

find_package(Vulkan REQUIRED)
include_directories(${Vulkan_INCLUDE_DIRS})

find_package(SDL2 REQUIRED)
find_package(SDL2_image REQUIRED)
include_directories(${SDL2_INCLUDE_DIRS})
include_directories(${SDL2_image_INCLUDE_DIRS})

find_package(Freetype REQUIRED)
include_directories(${FREETYPE_INCLUDE_DIRS})

find_package(OpenMP REQUIRED)

# Box2D uses C17 but still complains about C11 features.
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -g -Wall -Wextra -Wno-unused-function -Wno-typedef-redefinition")

set(NOVA_BUILD_EXAMPLE true CACHE BOOL "Enable example build" )

# The name of the output
set(OUTPUT
    Nova
)

set(SOURCES
    src/engine.c
    src/vk.c
    src/nvsm.c
    src/fontc.c
    src/common.c
)

set(CLAY_INCLUDE_ALL_EXAMPLES OFF)
set(CLAY_INCLUDE_DEMOS OFF)
set(CLAY_INCLUDE_CPP_EXAMPLE OFF)
set(CLAY_INCLUDE_RAYLIB_EXAMPLES OFF)
set(CLAY_INCLUDE_SDL2_EXAMPLES OFF)
set(CLAY_INCLUDE_SDL3_EXAMPLES OFF)

add_subdirectory(${CMAKE_CURRENT_LIST_DIR}/src/external/box2d)
add_subdirectory(${CMAKE_CURRENT_LIST_DIR}/src/external/volk)
add_subdirectory(${CMAKE_CURRENT_LIST_DIR}/src/external/clay)
add_subdirectory(${CMAKE_CURRENT_LIST_DIR}/src/std)

if (NOVA_BUILD_EXAMPLE)
    add_executable(nova_example ${CMAKE_CURRENT_LIST_DIR}/src/main.c)
endif()

add_library(Nova STATIC ${SOURCES})

set(LIBS
    nvstd
    volk
    m
    SDL2
    SDL2_image
    freetype
    box2d
    png
    jpeg
    OpenMP::OpenMP_C
)

add_executable(fontc src/fontc.c src/common.c)
target_compile_definitions(fontc PRIVATE FONTC_EXECUTABLE=1)
target_link_options(fontc PRIVATE ${CFLAGS} -fopenmp)
target_compile_options(fontc PRIVATE ${LFLAGS} -fopenmp)
target_link_libraries(fontc ${LIBS})

target_precompile_headers(Nova PUBLIC ${CMAKE_CURRENT_LIST_DIR}/src/std/stdafx.h)
target_link_options(Nova PRIVATE ${LFLAGS} -g)
target_compile_options(Nova PRIVATE ${CFLAGS} -g)
target_compile_definitions(Nova PRIVATE ) # avoid making multiple main's 
target_link_libraries(Nova ${LIBS})

file(COPY ${CMAKE_CURRENT_LIST_DIR}/src/Assets DESTINATION ${CMAKE_BINARY_DIR}/)
file(COPY ${CMAKE_CURRENT_LIST_DIR}/src/Shaders DESTINATION ${CMAKE_BINARY_DIR}/)

if (NOVA_BUILD_EXAMPLE)
    target_link_libraries(nova_example Nova)
endif()