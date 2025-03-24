cmake_minimum_required(VERSION 3.16)
project(main)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)

find_package(Vulkan REQUIRED)
include_directories(${Vulkan_INCLUDE_DIRS})

find_package(SDL2 REQUIRED)
include_directories(${SDL2_INCLUDE_DIRS})

find_package(Freetype REQUIRED)
include_directories(${FREETYPE_INCLUDE_DIRS})

find_package(OpenMP REQUIRED)

set(CMAKE_C_FLAGS -g)
set(CMAKE_CXX_FLAGS -g)

set(NOVA_BUILD_EXAMPLE false CACHE BOOL "Enable example build" )

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
    ssl/ssl.c
)

add_subdirectory(${CMAKE_CURRENT_LIST_DIR}/external/box2d)
add_subdirectory(${CMAKE_CURRENT_LIST_DIR}/external/volk)
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
    freetype
    box2d
    png
    jpeg
    OpenMP::OpenMP_C
)

add_executable(shadermanager src/nvsm.c src/common.c)
target_compile_definitions(shadermanager PRIVATE NVSM_EXECUTABLE=1)
target_link_libraries(shadermanager ${LIBS})
target_compile_options(shadermanager PRIVATE -ffast-math -Wall -Wextra -pedantic)

add_executable(fontc src/fontc.c src/common.c)
target_compile_definitions(fontc PRIVATE FONTC_EXECUTABLE=1)
target_link_libraries(fontc ${LIBS})
target_compile_options(fontc PRIVATE -g -Wall -Wextra -pedantic -fopenmp)

target_precompile_headers(Nova PUBLIC ${CMAKE_CURRENT_LIST_DIR}/src/std/stdafx.h)
target_link_options(Nova PRIVATE -g -Wall -Wextra)
target_compile_options(Nova PRIVATE -g -Wall -Wextra -Wall -Wextra -pedantic)
target_compile_definitions(Nova PRIVATE ) # avoid making multiple main's 
target_link_libraries(Nova ${LIBS})

file(COPY ${CMAKE_CURRENT_LIST_DIR}/Assets DESTINATION ${CMAKE_BINARY_DIR}/)

if (NOVA_BUILD_EXAMPLE)
    target_link_libraries(nova_example Nova)
endif()