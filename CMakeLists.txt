cmake_minimum_required(VERSION 3.30)
project(pbkit_sdl_gpu VERSION 0.0.2)

set(
        CMAKE_MODULE_PATH
        ${CMAKE_MODULE_PATH}
        "${CMAKE_CURRENT_LIST_DIR}/cmake/modules/"
)

set(CMAKE_VERBOSE_MAKEFILE ON CACHE BOOL "Generate verbose Makefile")

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(_CMAKE_PROCESSING_LANGUAGE "CXX")

include(CheckSymbolExists)
include(FindPkgConfig)

# Determine if this build is for the Xbox or the host system.
if (CMAKE_TOOLCHAIN_FILE MATCHES "toolchain-nxdk.cmake")
    set(IS_TARGET_BUILD ON)
else ()
    message(FATAL_ERROR "This project must be built with the nxdk toolchain")
endif ()

find_package(NXDK REQUIRED)
find_package(NXDK_SDL2 REQUIRED)

include(FetchContent)
find_program(PATCH_EXECUTABLE patch)

FetchContent_Declare(
        _sdl_gpu
        GIT_REPOSITORY https://github.com/grimfang4/sdl-gpu.git
        GIT_TAG 455214775214da77526bfc6d65c7cd986d5384f6
        GIT_SHALLOW TRUE
        GIT_PROGRESS TRUE
        SOURCE_SUBDIR __do_not_build
        PATCH_COMMAND ${PATCH_EXECUTABLE} -p1 -N -i "${CMAKE_CURRENT_LIST_DIR}/patches/stb_image-no-fopen_s.patch"
)
FetchContent_MakeAvailable(_sdl_gpu)
FetchContent_GetProperties(_sdl_gpu SOURCE_DIR sdl_gpu_SOURCE_DIR)

add_library(
        pbkit_sdl_gpu
        color_combiner.cpp
        color_combiner.h
        debug_output.cpp
        debug_output.h
        pbkit_sdl_gpu.cpp
        pbkit_sdl_gpu.h
        precalculated_vertex_shader.cpp
        precalculated_vertex_shader.h
        third_party/math3d.cpp
        third_party/math3d.h
        third_party/swizzle.cpp
        third_party/swizzle.h
        "${sdl_gpu_SOURCE_DIR}/src/SDL_gpu.c"
        "${sdl_gpu_SOURCE_DIR}/src/SDL_gpu_matrix.c"
        "${sdl_gpu_SOURCE_DIR}/src/SDL_gpu_renderer.c"
        "${sdl_gpu_SOURCE_DIR}/src/SDL_gpu_shapes.c"
        "${sdl_gpu_SOURCE_DIR}/src/externals/stb_image/stb_image.c"
        "${sdl_gpu_SOURCE_DIR}/src/externals/stb_image_write/stb_image_write.c"
)

target_include_directories(
        pbkit_sdl_gpu
        PUBLIC
        "${sdl_gpu_SOURCE_DIR}/include"
        "${sdl_gpu_SOURCE_DIR}/src/externals/stb_image"
        "${sdl_gpu_SOURCE_DIR}/src/externals/stb_image_write"
)

target_link_libraries(
        pbkit_sdl_gpu
        PRIVATE
        NXDK::NXDK
        NXDK::SDL2
)

target_compile_options(
        pbkit_sdl_gpu
        PRIVATE
        -Wno-inconsistent-dllimport
)

target_compile_definitions(
        pbkit_sdl_gpu
        PRIVATE
        -DXBOX
        -DAPIENTRY=__cdecl
        -DSTBI_NO_SIMD
        -DGLEW_STATIC
        -DGLEW_NO_GLU
        -DSDL_GPU_DISABLE_SHADERS
        -DSDL_GPU_DISABLE_GLES
        -DSDL_GPU_DISABLE_OPENGL
        -DSDL_GPU_DISABLE_TEXTURE_GETS
)


install(
        TARGETS
        pbkit_sdl_gpu
        LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
)

install(
        FILES
        "color_combiner.h"
        "pbkit_sdl_gpu.h"
        "precalculated_vertex_shader.h"
        "${sdl_gpu_SOURCE_DIR}/include/SDL_gpu.h"
        "${sdl_gpu_SOURCE_DIR}/include/SDL_gpu_GLES_1.h"
        "${sdl_gpu_SOURCE_DIR}/include/SDL_gpu_GLES_2.h"
        "${sdl_gpu_SOURCE_DIR}/include/SDL_gpu_GLES_3.h"
        "${sdl_gpu_SOURCE_DIR}/include/SDL_gpu_OpenGL_1.h"
        "${sdl_gpu_SOURCE_DIR}/include/SDL_gpu_OpenGL_1_BASE.h"
        "${sdl_gpu_SOURCE_DIR}/include/SDL_gpu_OpenGL_2.h"
        "${sdl_gpu_SOURCE_DIR}/include/SDL_gpu_OpenGL_3.h"
        "${sdl_gpu_SOURCE_DIR}/include/SDL_gpu_OpenGL_4.h"
        "${sdl_gpu_SOURCE_DIR}/include/SDL_gpu_RendererImpl.h"
        "${sdl_gpu_SOURCE_DIR}/include/SDL_gpu_version.h"
        "${sdl_gpu_SOURCE_DIR}/src/externals/stb_image/stb_image.h"
        "${sdl_gpu_SOURCE_DIR}/src/externals/stb_image_write/stb_image_write.h"
        DESTINATION
        pbkit_sdl_gpu
)
