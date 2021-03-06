# SDL_gpu is a CMake-based project, so for now an nxdk-compatible stub is used.
# This must be kept in sync with the content of the sdl-gpu directory.

PBKIT_SDL_GPU_DIR := $(dir $(lastword $(MAKEFILE_LIST)))
SDL_GPU_DIR := ./

STB_IMAGE_DIR := $(SDL_GPU_DIR)/src/externals/stb_image
STB_IMAGE_WRITE_DIR := $(SDL_GPU_DIR)/src/externals/stb_image_write
GLEW_DIR := $(SDL_GPU_DIR)/src/externals/glew

PBKIT_SDL_GPU_SRCS = \
	$(PBKIT_SDL_GPU_DIR)/color_combiner.cpp \
	$(PBKIT_SDL_GPU_DIR)/debug_output.cpp \
	$(PBKIT_SDL_GPU_DIR)/pbkit_sdl_gpu.cpp \
	$(PBKIT_SDL_GPU_DIR)/precalculated_vertex_shader.cpp \
	$(PBKIT_SDL_GPU_DIR)/third_party/math3d.cpp \
	$(PBKIT_SDL_GPU_DIR)/third_party/swizzle.cpp

SDL_GPU_SRCS = \
	$(SDL_GPU_DIR)/src/SDL_gpu.c \
	$(SDL_GPU_DIR)/src/SDL_gpu_matrix.c \
	$(SDL_GPU_DIR)/src/SDL_gpu_renderer.c \
	$(SDL_GPU_DIR)/src/SDL_gpu_shapes.c \
	$(STB_IMAGE_DIR)/stb_image.c \
	$(STB_IMAGE_WRITE_DIR)/stb_image_write.c \
	$(PBKIT_SDL_GPU_SRCS)

SDL_GPU_OBJS = $(addsuffix .obj, $(basename $(SDL_GPU_SRCS)))
LIB_DEPS += $(addsuffix .d, $(SDL_GPU_SRCS))

SDL_GPU_CFLAGS := \
    -Wno-inconsistent-dllimport \
    -DXBOX \
    -DAPIENTRY=__cdecl \
	-DSTBI_NO_SIMD \
	-DGLEW_STATIC \
	-DGLEW_NO_GLU \
	-DSDL_GPU_DISABLE_SHADERS \
	-DSDL_GPU_DISABLE_GLES \
	-DSDL_GPU_DISABLE_OPENGL \
	-DSDL_GPU_DISABLE_TEXTURE_GETS \
	-I$(SDL_GPU_DIR)/include \
	-I$(STB_IMAGE_DIR) \
	-I$(STB_IMAGE_WRITE_DIR) \
	-I$(GLEW_DIR)/GL

ifeq ($(DEBUG),y)
	SDL_GPU_CFLAGS += -g -gdwarf-4
else
	SDL_GPU_CFLAGS += -O3
endif

$(NXDK_DIR)/lib/libsdl_gpu.lib: CFLAGS += $(SDL_GPU_CFLAGS)
$(NXDK_DIR)/lib/libsdl_gpu.lib: CXXFLAGS += $(SDL_GPU_CFLAGS)
$(NXDK_DIR)/lib/libsdl_gpu.lib: $(SDL_GPU_OBJS)

main.exe: $(NXDK_DIR)/lib/libsdl_gpu.lib

CLEANRULES += clean-sdl-gpu

.PHONY: clean-sdl-gpu
clean-sdl-gpu:
	$(VE)rm -f $(SDL_GPU_OBJS) $(NXDK_DIR)/lib/libsdl_gpu.lib
