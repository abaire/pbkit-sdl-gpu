#include "pbkit_sdl_gpu.h"
#include <hal/debug.h>
#include <pbkit/nv_regs.h>
#include <pbkit/pbkit.h>
#include "third_party/swizzle.h"
#include "third_party/math3d.h"
#include "SDL_gpu.h"
#include "SDL_gpu_RendererImpl.h"
#include "color_combiner.h"
#include "debug_output.h"
#include "precalculated_vertex_shader.h"

#define MAXRAM 0x03FFAFFF
#define MASK(mask, val) (((val) << (__builtin_ffs(mask) - 1)) & (mask))

#define NV097_SET_SPECULAR_ENABLE 0x03b8
#define NV097_SET_LIGHT_CONTROL NV20_TCL_PRIMITIVE_3D_LIGHT_CONTROL
#define NV097_SET_COLOR_MATERIAL 0x298
#define NV097_SET_COLOR_MATERIAL_ALL_FROM_MATERIAL 0
#define NV097_SET_MATERIAL_ALPHA 0x3B4
#define NV097_SET_POINT_PARAMS_ENABLE 0x318
#define NV097_SET_POINT_SMOOTH_ENABLE 0x31C
#define NV097_SET_DEPTH_FUNC_V_LESS 0x00000201

#define NV097_SET_WINDOW_CLIP_HORIZONTAL 0x2C0
#define NV097_SET_WINDOW_CLIP_VERTICAL 0x2E0

#define NV097_SET_TEXTURE_FORMAT_COLOR_SZ_B8G8R8A8 0x3B
#define NV097_SET_TEXTURE_CONTROL0_ENABLE (1 << 30)
#define NV097_SET_TEXTURE_CONTROL0_MIN_LOD_CLAMP 0x3FFC0000
#define NV097_SET_TEXTURE_CONTROL0_MAX_LOD_CLAMP 0x0003FFC0
#define NV097_SET_TEXTURE_CONTROL0_ALPHA_KILL_ENABLE (1 << 2)
#define NV097_SET_TEXTURE_ADDRESS_U 0x0000000F
#define NV097_SET_TEXTURE_ADDRESS_CYLINDERWRAP_U 0x000000F0
#define NV097_SET_TEXTURE_ADDRESS_V 0x00000F00
#define NV097_SET_TEXTURE_ADDRESS_CYLINDERWRAP_V 0x0000F000
#define NV097_SET_TEXTURE_ADDRESS_P 0x000F0000
#define NV097_SET_TEXTURE_ADDRESS_CYLINDERWRAP_P 0x00F00000
#define NV097_SET_TEXTURE_ADDRESS_CYLINDERWRAP_Q 0x0F000000
#define NV097_SET_WEIGHT1F 0x169C
#define NV097_SET_WEIGHT4F 0x16C0
#define NV097_SET_NORMAL3F 0x1530
#define NV097_SET_NORMAL3S 0x1540
#define NV097_SET_DIFFUSE_COLOR4F 0x1550
#define NV097_SET_DIFFUSE_COLOR3F 0x1560
#define NV097_SET_DIFFUSE_COLOR4I 0x156C
#define NV097_SET_SPECULAR_COLOR4F 0x1570
#define NV097_SET_SPECULAR_COLOR3F 0x1580
#define NV097_SET_SPECULAR_COLOR4I 0x158C
#define NV097_SET_FOG_COORD 0x1698
#define NV097_SET_POINT_SIZE 0x43C
#define NV097_SET_TEXCOORD0_2F 0x1590
#define NV097_SET_TEXCOORD0_4F 0x15A0
#define NV097_SET_TEXCOORD0_2S 0x1598
#define NV097_SET_TEXCOORD0_4S 0x15B0
#define NV097_SET_TEXCOORD1_2F 0x15B8
#define NV097_SET_TEXCOORD1_4F 0x15C8
#define NV097_SET_TEXCOORD1_2S 0x15C0
#define NV097_SET_TEXCOORD1_4S 0x15D8
#define NV097_SET_TEXCOORD2_2F 0x15E0
#define NV097_SET_TEXCOORD2_4F 0x15F0
#define NV097_SET_TEXCOORD2_2S 0x15E8
#define NV097_SET_TEXCOORD2_4S 0x1600
#define NV097_SET_TEXCOORD3_2F 0x1608
#define NV097_SET_TEXCOORD3_4F 0x1620
#define NV097_SET_TEXCOORD3_2S 0x1610
#define NV097_SET_TEXCOORD3_4S 0x1630
// NV_PGRAPH_TEXFILTER0_CONVOLUTION_KERNEL from xemu.
#define NV097_SET_TEXTURE_FILTER_CONVOLUTION_KERNEL 0x0000E000

namespace PbkitSdlGpu {

static constexpr GPU_RendererEnum GPU_RENDERER_PBKIT = GPU_RENDERER_CUSTOM_0 + 10;
static GPU_RendererID renderer_id;

struct PBKitSDLContext {
  PBKitSDLContext(GPU_Target* target, DWORD width, DWORD height) : target(target), width(width), height(height) {}

  GPU_Target* target;
  DWORD width;
  DWORD height;
};

struct UVRect {
  float left, top, right, bottom;
};

struct PBKitImageData {
  uint8_t* data;
  int format;
  uint32_t pitch;
  uint32_t byte_length;
  int size_u;
  int size_v;

  UVRect MakeTexCoords(GPU_Rect* src_rect, GPU_Image* image) const {
    float pixel_left = src_rect->x;
    float pixel_top = src_rect->y;
    float pixel_right = pixel_left + src_rect->w;
    float pixel_bottom = pixel_top + src_rect->h;

    UVRect ret{ pixel_left / (float)image->texture_w, pixel_top / (float)image->texture_h,
                pixel_right / (float)image->texture_w,
                pixel_bottom / (float)image->texture_h };
    return std::move(ret);
  }
};

#define NV2A_VERTEX_ATTR_POSITION 0
#define NV2A_VERTEX_ATTR_WEIGHT 1
#define NV2A_VERTEX_ATTR_NORMAL 2
#define NV2A_VERTEX_ATTR_DIFFUSE 3
#define NV2A_VERTEX_ATTR_SPECULAR 4
#define NV2A_VERTEX_ATTR_FOG_COORD 5
#define NV2A_VERTEX_ATTR_POINT_SIZE 6
#define NV2A_VERTEX_ATTR_BACK_DIFFUSE 7
#define NV2A_VERTEX_ATTR_BACK_SPECULAR 8
#define NV2A_VERTEX_ATTR_TEXTURE0 9
#define NV2A_VERTEX_ATTR_TEXTURE1 10
#define NV2A_VERTEX_ATTR_TEXTURE2 11
#define NV2A_VERTEX_ATTR_TEXTURE3 12
// These do not have a default semantic but are usable from custom vertex shaders.
#define NV2A_VERTEX_ATTR_13 13
#define NV2A_VERTEX_ATTR_14 14
#define NV2A_VERTEX_ATTR_15 15

enum ShaderStageProgram
{
  STAGE_NONE = 0,
  STAGE_2D_PROJECTIVE,
  STAGE_3D_PROJECTIVE,
  STAGE_CUBE_MAP,
  STAGE_PASS_THROUGH,
  STAGE_CLIP_PLANE,
  STAGE_BUMPENVMAP,
  STAGE_BUMPENVMAP_LUMINANCE,
  STAGE_BRDF,
  STAGE_DOT_ST,
  STAGE_DOT_ZW,
  STAGE_DOT_REFLECT_DIFFUSE,
  STAGE_DOT_REFLECT_SPECULAR,
  STAGE_DOT_STR_3D,
  STAGE_DOT_STR_CUBE,
  STAGE_DEPENDENT_AR,
  STAGE_DEPENDENT_GB,
  STAGE_DOT_PRODUCT,
  STAGE_DOT_REFLECT_SPECULAR_CONST,
};

enum VertexAttribute
{
  POSITION = 1 << NV2A_VERTEX_ATTR_POSITION,
  WEIGHT = 1 << NV2A_VERTEX_ATTR_WEIGHT,
  NORMAL = 1 << NV2A_VERTEX_ATTR_NORMAL,
  DIFFUSE = 1 << NV2A_VERTEX_ATTR_DIFFUSE,
  SPECULAR = 1 << NV2A_VERTEX_ATTR_SPECULAR,
  FOG_COORD = 1 << NV2A_VERTEX_ATTR_FOG_COORD,
  POINT_SIZE = 1 << NV2A_VERTEX_ATTR_POINT_SIZE,
  BACK_DIFFUSE = 1 << NV2A_VERTEX_ATTR_BACK_DIFFUSE,
  BACK_SPECULAR = 1 << NV2A_VERTEX_ATTR_BACK_SPECULAR,
  TEXCOORD0 = 1 << NV2A_VERTEX_ATTR_TEXTURE0,
  TEXCOORD1 = 1 << NV2A_VERTEX_ATTR_TEXTURE1,
  TEXCOORD2 = 1 << NV2A_VERTEX_ATTR_TEXTURE2,
  TEXCOORD3 = 1 << NV2A_VERTEX_ATTR_TEXTURE3,
  V13 = 1 << NV2A_VERTEX_ATTR_13,
  V14 = 1 << NV2A_VERTEX_ATTR_14,
  V15 = 1 << NV2A_VERTEX_ATTR_15,
};

enum ConvolutionKernel
{
  K_QUINCUNX = 1,
  K_GAUSSIAN_3 = 2,
};

enum MinFilter
{
  MIN_BOX_LOD0 = 1,
  MIN_TENT_LOD0,
  MIN_BOX_NEARESTLOD,
  MIN_TENT_NEARESTLOD,
  MIN_BOX_TENT_LOD,
  MIN_TENT_TENT_LOD,
  MIN_CONVOLUTION_2D_LOD0,
};

enum MagFilter
{
  MAG_BOX_LOD0 = 1,
  MAG_TENT_LOD0 = 2,
  MAG_CONVOLUTION_2D_LOD0 = 4,
};

enum WrapMode
{
  WRAP_REPEAT = 1,
  WRAP_MIRROR,
  WRAP_CLAMP_TO_EDGE,
  WRAP_BORDER,
  WRAP_CLAMP_TO_EDGE_OGL
};

// bitscan forward
static int bsf(int val) {
  __asm bsf eax, val
}

static unsigned int getNearestPowerOf2(unsigned int n) {
  unsigned int x = 1;
  while (x < n) {
    x <<= 1;
  }
  return x;
}

static uint32_t* pb_push1f(uint32_t* p, DWORD command, float param1) {
  pb_push_to(SUBCH_3D, p, command, 1);
  *((float*)(p + 1)) = param1;
  return p + 2;
}

static uint32_t* pb_push2f(uint32_t* p, DWORD command, float param1, float param2) {
  pb_push_to(SUBCH_3D, p, command, 2);
  *((float*)(p + 1)) = param1;
  *((float*)(p + 2)) = param2;
  return p + 3;
}

static uint32_t* pb_push3f(
    uint32_t* p, DWORD command, float param1, float param2, float param3) {
  pb_push_to(SUBCH_3D, p, command, 3);
  *((float*)(p + 1)) = param1;
  *((float*)(p + 2)) = param2;
  *((float*)(p + 3)) = param3;
  return p + 4;
}

static uint32_t* pb_push_4x3_matrix(uint32_t* p, DWORD command, const float* m) {
  pb_push_to(SUBCH_3D, p++, command, 12);

  *((float*)p++) = m[_11];
  *((float*)p++) = m[_12];
  *((float*)p++) = m[_13];
  *((float*)p++) = m[_14];

  *((float*)p++) = m[_21];
  *((float*)p++) = m[_22];
  *((float*)p++) = m[_23];
  *((float*)p++) = m[_24];

  *((float*)p++) = m[_31];
  *((float*)p++) = m[_32];
  *((float*)p++) = m[_33];
  *((float*)p++) = m[_34];

  return p;
}

static GPU_Target* SDLCALL Init(GPU_Renderer* renderer,
                                GPU_RendererID renderer_request,
                                Uint16 w,
                                Uint16 h,
                                GPU_WindowFlagEnum SDL_flags) {
  int status = pb_init();
  if (status) {
    debugPrint("pb_init Error %d\n", status);
    pb_show_debug_screen();
    Sleep(2000);
    return nullptr;
  }

  GPU_Target* target = (GPU_Target*)SDL_malloc(sizeof(GPU_Target));
  memset(target, 0, sizeof(GPU_Target));
  target->refcount = 1;
  target->renderer = renderer;
  target->is_alias = GPU_FALSE;
  target->data = nullptr;
  target->image = nullptr;
  target->context = (GPU_Context*)SDL_malloc(sizeof(GPU_Context));
  memset(target->context, 0, sizeof(GPU_Context));
  target->context->refcount = 1;

  auto data = new PBKitSDLContext(target, pb_back_buffer_width(), pb_back_buffer_height());

  target->context->data = data;
  target->context->context = nullptr;

  uint32_t value =
      MASK(NV097_SET_SURFACE_FORMAT_COLOR, NV097_SET_SURFACE_FORMAT_COLOR_LE_A8R8G8B8)
      | MASK(NV097_SET_SURFACE_FORMAT_ZETA, NV097_SET_SURFACE_FORMAT_ZETA_Z16)
      | MASK(NV097_SET_SURFACE_FORMAT_ANTI_ALIASING,
             NV097_SET_SURFACE_FORMAT_ANTI_ALIASING_CENTER_1)
      | MASK(NV097_SET_SURFACE_FORMAT_TYPE, NV097_SET_SURFACE_FORMAT_TYPE_PITCH);

  auto p = pb_begin();
  p = pb_push1(p, NV097_SET_SURFACE_FORMAT, value);
  p = pb_push1(p, NV097_SET_SURFACE_CLIP_HORIZONTAL, (data->width << 16));
  p = pb_push1(p, NV097_SET_SURFACE_CLIP_VERTICAL, (data->height << 16));

  p = pb_push1(p, NV097_SET_LIGHTING_ENABLE, false);
  p = pb_push1(p, NV097_SET_SPECULAR_ENABLE, false);
  p = pb_push1(p, NV097_SET_LIGHT_CONTROL, 0x20001);
  p = pb_push1(p, NV097_SET_LIGHT_ENABLE_MASK, NV097_SET_LIGHT_ENABLE_MASK_LIGHT0_OFF);
  p = pb_push1(p, NV097_SET_COLOR_MATERIAL, NV097_SET_COLOR_MATERIAL_ALL_FROM_MATERIAL);
  p = pb_push1f(p, NV097_SET_MATERIAL_ALPHA, 1.0f);

  p = pb_push1(p, NV097_SET_BLEND_ENABLE, true);
  p = pb_push1(p, NV097_SET_BLEND_EQUATION, NV097_SET_BLEND_EQUATION_V_FUNC_ADD);
  p = pb_push1(p, NV097_SET_BLEND_FUNC_SFACTOR, NV097_SET_BLEND_FUNC_SFACTOR_V_SRC_ALPHA);
  p = pb_push1(p, NV097_SET_BLEND_FUNC_DFACTOR,
               NV097_SET_BLEND_FUNC_DFACTOR_V_ONE_MINUS_SRC_ALPHA);
  p = pb_push1(p, NV20_TCL_PRIMITIVE_3D_LIGHT_MODEL_TWO_SIDE_ENABLE, 0);
  p = pb_push1(p, NV097_SET_FRONT_POLYGON_MODE, NV097_SET_FRONT_POLYGON_MODE_V_FILL);
  p = pb_push1(p, NV097_SET_BACK_POLYGON_MODE, NV097_SET_FRONT_POLYGON_MODE_V_FILL);

  p = pb_push1(p, NV097_SET_VERTEX_DATA4UB + 0x10, 0); // Specular
  p = pb_push1(p, NV097_SET_VERTEX_DATA4UB + 0x1C, 0xFFFFFFFF); // Back diffuse
  p = pb_push1(p, NV097_SET_VERTEX_DATA4UB + 0x20, 0); // Back specular

  p = pb_push1(p, NV097_SET_POINT_PARAMS_ENABLE, false);
  p = pb_push1(p, NV097_SET_POINT_SMOOTH_ENABLE, false);
  p = pb_push1(p, NV097_SET_POINT_SIZE, 8);

  p = pb_push1(p, NV097_SET_SHADER_OTHER_STAGE_INPUT, 0);
  p = pb_push1(p, NV097_SET_SHADER_STAGE_PROGRAM, 0x0);

  p = pb_push1(p, NV097_SET_FOG_ENABLE, false);
  p = pb_push4(p, NV097_SET_TEXTURE_MATRIX_ENABLE, 0, 0, 0, 0);
  p = pb_push1(p, NV097_SET_TEXTURE_BORDER_COLOR, 0xFFFFFFFF);

  p = pb_push1(p, NV097_SET_FRONT_FACE, NV097_SET_FRONT_FACE_V_CW);
  p = pb_push1(p, NV097_SET_CULL_FACE, NV097_SET_CULL_FACE_V_BACK);
  p = pb_push1(p, NV097_SET_CULL_FACE_ENABLE, true);

  p = pb_push1(p, NV097_SET_DEPTH_MASK, true);
  p = pb_push1(p, NV097_SET_DEPTH_FUNC, NV097_SET_DEPTH_FUNC_V_LESS);
  p = pb_push1(p, NV097_SET_DEPTH_TEST_ENABLE, false);
  p = pb_push1(p, NV097_SET_STENCIL_TEST_ENABLE, false);
  p = pb_push1(p, NV097_SET_STENCIL_MASK, true);

  p = pb_push1(p, NV097_SET_NORMALIZATION_ENABLE, false);

  p = pb_push1(p, NV097_SET_WINDOW_CLIP_HORIZONTAL, w << 16);
  p = pb_push1(p, NV097_SET_WINDOW_CLIP_VERTICAL, h << 16);

  p = pb_push1(p, NV097_SET_TEXGEN_S, NV097_SET_TEXGEN_S_DISABLE);
  p = pb_push1(p, NV097_SET_TEXGEN_T, NV097_SET_TEXGEN_S_DISABLE);
  p = pb_push1(p, NV097_SET_TEXGEN_R, NV097_SET_TEXGEN_S_DISABLE);
  p = pb_push1(p, NV097_SET_TEXGEN_Q, NV097_SET_TEXGEN_S_DISABLE);

  p = pb_push4f(p, NV097_SET_TEXTURE_SET_BUMP_ENV_MAT, 0.0f, 0.0f, 0.0f, 0.0f);
  p = pb_push1f(p, NV097_SET_TEXTURE_SET_BUMP_ENV_SCALE, 0.0f);
  p = pb_push1f(p, NV097_SET_TEXTURE_SET_BUMP_ENV_OFFSET, 0.0f);

  p = pb_push1(p, NV097_SET_COMBINER_CONTROL, 1);

  pb_end(p);

  PbkitSdlGpu::LoadPrecalculatedVertexShader();

  ClearInputColorCombiners();
  ClearInputAlphaCombiners();
  ClearOutputColorCombiners();
  ClearOutputAlphaCombiners();

  SetInputColorCombiner(0, SRC_DIFFUSE, false, MAP_UNSIGNED_IDENTITY, SRC_ZERO, false,
                        MAP_UNSIGNED_INVERT);
  SetInputAlphaCombiner(0, SRC_DIFFUSE, true, MAP_UNSIGNED_IDENTITY, SRC_ZERO, false,
                        MAP_UNSIGNED_INVERT);

  SetOutputColorCombiner(0, DST_DISCARD, DST_DISCARD, DST_R0);
  SetOutputAlphaCombiner(0, DST_DISCARD, DST_DISCARD, DST_R0);

  SetFinalCombiner0(SRC_ZERO, false, false, SRC_ZERO, false, false, SRC_ZERO, false, false,
                    SRC_R0);
  SetFinalCombiner1Just(SRC_R0, true);

  return target;
}

static GPU_Target* SDLCALL CreateTargetFromWindow(GPU_Renderer* renderer,
                                                  Uint32 windowID,
                                                  GPU_Target* target) {
  PBKITSDLGPU_ASSERT(!"TODO: Implement me");
  return nullptr;
}

static GPU_bool SDLCALL SetActiveTarget(GPU_Renderer* renderer, GPU_Target* target) {
  PBKITSDLGPU_ASSERT(!"TODO: Implement me");
  return false;
}

static void SDLCALL MakeCurrent(GPU_Renderer* renderer,
                                GPU_Target* target,
                                Uint32 windowID) {
  SDL_Window* window;

  if (target == nullptr || target->context == nullptr)
    return;

  if (target->image != nullptr)
    return;

  renderer->current_context_target = target;
}

static void SDLCALL SetAsCurrent(GPU_Renderer* renderer) {
  if (renderer->current_context_target == nullptr)
    return;

  renderer->impl->MakeCurrent(renderer, renderer->current_context_target,
                              renderer->current_context_target->context->windowID);
}

/*! \see GPU_CreateAliasTarget() */
static GPU_Target* SDLCALL CreateAliasTarget(GPU_Renderer* renderer, GPU_Target* target) {
  PBKITSDLGPU_ASSERT(!"TODO: Implement me");
  return nullptr;
}

static void SDLCALL ResetRendererState(GPU_Renderer* renderer) {
  PBKITSDLGPU_ASSERT(!"TODO: Implement me");
}

static GPU_bool SDLCALL AddDepthBuffer(GPU_Renderer* renderer, GPU_Target* target) {
  PBKITSDLGPU_ASSERT(!"TODO: Implement me");
  return false;
}

static GPU_bool SDLCALL SetWindowResolution(GPU_Renderer* renderer, Uint16 w, Uint16 h) {
  PBKITSDLGPU_ASSERT(!"TODO: Implement me");
  return false;
}

static void SDLCALL SetVirtualResolution(GPU_Renderer* renderer,
                                         GPU_Target* target,
                                         Uint16 w,
                                         Uint16 h) {
  PBKITSDLGPU_ASSERT(!"TODO: Implement me");
}

static void SDLCALL UnsetVirtualResolution(GPU_Renderer* renderer, GPU_Target* target) {
  PBKITSDLGPU_ASSERT(!"TODO: Implement me");
}

static void SDLCALL Quit(GPU_Renderer* renderer) {
  PBKITSDLGPU_ASSERT(!"TODO: Implement me");
}

static GPU_bool SDLCALL SetFullscreen(GPU_Renderer* renderer,
                                      GPU_bool enable_fullscreen,
                                      GPU_bool use_desktop_resolution) {
  PBKITSDLGPU_ASSERT(!"TODO: Implement me");
  return false;
}

static GPU_Camera SDLCALL SetCamera(GPU_Renderer* renderer,
                                    GPU_Target* target,
                                    GPU_Camera* cam) {
  PBKITSDLGPU_ASSERT(!"TODO: Implement me");
  return {};
}

static GPU_Image* CreateUninitializedImage(GPU_Renderer* renderer,
                                           Uint16 w,
                                           Uint16 h,
                                           GPU_FormatEnum format) {
  int bytes_per_pixel = 0;
  int pbkit_format;
  SDL_Color white = { 255, 255, 255, 255 };

  switch (format) {
  case GPU_FORMAT_RGB:
  case GPU_FORMAT_RGBA:
    pbkit_format = NV097_SET_TEXTURE_FORMAT_COLOR_SZ_A8B8G8R8;
    bytes_per_pixel = 4;
    break;

  case GPU_FORMAT_BGR:
  case GPU_FORMAT_BGRA:
    pbkit_format = NV097_SET_TEXTURE_FORMAT_COLOR_SZ_R8G8B8A8;
    bytes_per_pixel = 4;
    break;

  case GPU_FORMAT_ABGR:
    pbkit_format = NV097_SET_TEXTURE_FORMAT_COLOR_SZ_A8B8G8R8;
    bytes_per_pixel = 4;
    break;

  default:
    GPU_PushErrorCode("GPU_CreateUninitializedImage", GPU_ERROR_DATA_ERROR,
                      "Unsupported image format (0x%x)", format);
    return nullptr;
  }

  if (bytes_per_pixel < 1 || bytes_per_pixel > 4) {
    GPU_PushErrorCode("GPU_CreateUninitializedImage", GPU_ERROR_DATA_ERROR,
                      "Unsupported number of bytes per pixel (%d)", bytes_per_pixel);
    return nullptr;
  }

  // Create the GPU_Image
  auto result = (GPU_Image*)SDL_malloc(sizeof(GPU_Image));
  result->refcount = 1;
  auto data = (PBKitImageData*)SDL_malloc(sizeof(PBKitImageData));
  result->target = nullptr;
  result->renderer = renderer;
  result->context_target = renderer->current_context_target;
  result->format = format;
  result->num_layers = 1;
  result->bytes_per_pixel = bytes_per_pixel;
  result->has_mipmaps = GPU_FALSE;

  result->anchor_x = renderer->default_image_anchor_x;
  result->anchor_y = renderer->default_image_anchor_y;

  result->color = white;
  result->use_blending = GPU_TRUE;
  result->blend_mode = GPU_GetBlendModeFromPreset(GPU_BLEND_NORMAL);
  result->filter_mode = GPU_FILTER_LINEAR;
  result->snap_mode = GPU_SNAP_POSITION_AND_DIMENSIONS;
  result->wrap_mode_x = GPU_WRAP_NONE;
  result->wrap_mode_y = GPU_WRAP_NONE;

  result->data = data;
  result->is_alias = GPU_FALSE;
  data->format = pbkit_format;

  result->using_virtual_resolution = GPU_FALSE;
  result->w = w;
  result->h = h;
  result->base_w = w;
  result->base_h = h;

  result->texture_w = w;
  result->texture_h = h;

  return result;
}

static GPU_Image* SDLCALL CreateImage(GPU_Renderer* renderer,
                                      Uint16 w,
                                      Uint16 h,
                                      GPU_FormatEnum format) {
  GPU_Image* result;
  static unsigned char* zero_buffer = nullptr;
  static unsigned int zero_buffer_size = 0;

  if (format < 1) {
    GPU_PushErrorCode("GPU_CreateImage", GPU_ERROR_DATA_ERROR,
                      "Unsupported image format (0x%x)", format);
    return nullptr;
  }

  result = CreateUninitializedImage(renderer, w, h, format);
  if (result == nullptr) {
    GPU_PushErrorCode("GPU_CreateImage", GPU_ERROR_BACKEND_ERROR,
                      "Could not create image as requested.");
    return nullptr;
  }

  auto image_data = (PBKitImageData*)result->data;

  w = getNearestPowerOf2(result->w);
  h = getNearestPowerOf2(result->h);

  image_data->pitch = w * result->bytes_per_pixel;
  image_data->byte_length = image_data->pitch * h;

  image_data->size_u = bsf((int)w);
  image_data->size_v = bsf((int)h);

  image_data->data = static_cast<uint8_t*>(MmAllocateContiguousMemoryEx(
      image_data->byte_length, 0, MAXRAM, 0, PAGE_WRITECOMBINE | PAGE_READWRITE));
  PBKITSDLGPU_ASSERT(image_data->data);

  result->texture_w = w;
  result->texture_h = h;

  return result;
}

static GPU_Image* SDLCALL CreateImageUsingTexture(GPU_Renderer* renderer,
                                                  GPU_TextureHandle handle,
                                                  GPU_bool take_ownership) {
  PBKITSDLGPU_ASSERT(!"TODO: Implement me");
  return nullptr;
}

static GPU_Image* SDLCALL CreateAliasImage(GPU_Renderer* renderer, GPU_Image* image) {
  PBKITSDLGPU_ASSERT(!"TODO: Implement me");
  return nullptr;
}

static GPU_bool SDLCALL SaveImage(GPU_Renderer* renderer,
                                  GPU_Image* image,
                                  const char* filename,
                                  GPU_FileFormatEnum format) {
  PBKITSDLGPU_ASSERT(!"TODO: Implement me");
  return false;
}

static GPU_Image* SDLCALL CopyImage(GPU_Renderer* renderer, GPU_Image* image) {
  PBKITSDLGPU_ASSERT(!"TODO: Implement me");
  return nullptr;
}

static void SDLCALL UpdateImage(GPU_Renderer* renderer,
                                GPU_Image* image,
                                const GPU_Rect* image_rect,
                                SDL_Surface* surface,
                                const GPU_Rect* surface_rect) {
  PBKITSDLGPU_ASSERT(!image_rect);

  GPU_Rect fallback_surface_rect;
  if (!surface_rect) {
    fallback_surface_rect = { 0.0f, 0.0f, (float)surface->w, (float)surface->h };
    surface_rect = &fallback_surface_rect;
  }

  auto image_data = (PBKitImageData*)image->data;
  auto source = static_cast<uint8_t*>(surface->pixels);
  bool free_source_needed = false;
  auto source_pitch = surface->pitch;
  auto source_bpp = surface->format->BytesPerPixel;

  if (image->w != image->texture_w || surface->format->BytesPerPixel == 3) {
    free_source_needed = true;
    PBKITSDLGPU_ASSERT(image->bytes_per_pixel == 4);
    auto padded_dest = (uint8_t*)SDL_malloc(image_data->byte_length);
    PBKITSDLGPU_ASSERT(padded_dest);
    auto dest = padded_dest;

    source_bpp = image->bytes_per_pixel;
    source_pitch = image_data->pitch;

    source += surface->pitch * (int)surface_rect->y + (int)surface_rect->x;

    if (surface->format->BytesPerPixel == 3) {
      for (uint32_t y = 0; y < surface_rect->h; ++y) {
        auto spixel = source;
        auto dpixel = dest;
        for (uint32_t x = 0; x < surface_rect->w; ++x) {
          *dpixel++ = *spixel++;
          *dpixel++ = *spixel++;
          *dpixel++ = *spixel++;
          *dpixel++ = 0xFF;
        }
        source += surface->pitch;
        dest += image_data->pitch;
      }
    } else {
      uint32_t bytes_per_row = surface->format->BytesPerPixel * surface_rect->w;
      for (uint32_t y = 0; y < surface_rect->h; ++y) {
        // TODO: Support image_rect.
        memcpy(dest, source, bytes_per_row);
        source += surface->pitch;
        dest += image_data->pitch;
      }
    }

    source = padded_dest;
  }

  PbkitSdlGpu::swizzle_rect(source, image->texture_w, image->texture_h, image_data->data, source_pitch,
               source_bpp);

  if (free_source_needed) {
    SDL_free(source);
  }
}

static void SDLCALL UpdateImageBytes(GPU_Renderer* renderer,
                                     GPU_Image* image,
                                     const GPU_Rect* image_rect,
                                     const unsigned char* bytes,
                                     int bytes_per_row) {
  PBKITSDLGPU_ASSERT(!"TODO: Implement me");
}

static GPU_bool SDLCALL ReplaceImage(GPU_Renderer* renderer,
                                     GPU_Image* image,
                                     SDL_Surface* surface,
                                     const GPU_Rect* surface_rect) {
  PBKITSDLGPU_ASSERT(!"TODO: Implement me");
  return false;
}

static GPU_Image* SDLCALL CopyImageFromSurface(GPU_Renderer* renderer,
                                               SDL_Surface* surface,
                                               GPU_Rect* surface_rect) {
  if (surface == nullptr) {
    GPU_PushErrorCode("GPU_CopyImageFromSurface", GPU_ERROR_NULL_ARGUMENT, "surface");
    return nullptr;
  }

  if (surface->w == 0 || surface->h == 0) {
    GPU_PushErrorCode("GPU_CopyImageFromSurface", GPU_ERROR_DATA_ERROR,
                      "Surface has a zero dimension.");
    return nullptr;
  }

  int sw = !surface_rect ? surface->w : surface_rect->w;
  int sh = !surface_rect ? surface->h : surface_rect->h;

  // See what the best image format is.
  GPU_FormatEnum format;
  if (surface->format->Amask == 0) {
    if (SDL_ISPIXELFORMAT_ALPHA(surface->format->format)) {
      format = GPU_FORMAT_RGBA;
    } else {
      format = GPU_FORMAT_RGB;
    }
  } else {
    // TODO: Choose the best format for the texture depending on endianness.
    format = GPU_FORMAT_RGBA;
  }

  GPU_Image* image = renderer->impl->CreateImage(renderer, (Uint16)sw, (Uint16)sh, format);
  if (!image) {
    return nullptr;
  }

  renderer->impl->UpdateImage(renderer, image, nullptr, surface, surface_rect);

  return image;
}

static GPU_Image* SDLCALL CopyImageFromTarget(GPU_Renderer* renderer, GPU_Target* target) {
  PBKITSDLGPU_ASSERT(!"TODO: Implement me");
  return nullptr;
}

static SDL_Surface* SDLCALL CopySurfaceFromTarget(GPU_Renderer* renderer,
                                                  GPU_Target* target) {
  PBKITSDLGPU_ASSERT(!"TODO: Implement me");
  return nullptr;
}

static SDL_Surface* SDLCALL CopySurfaceFromImage(GPU_Renderer* renderer, GPU_Image* image) {
  PBKITSDLGPU_ASSERT(!"TODO: Implement me");
  return nullptr;
}

static void SDLCALL FreeImage(GPU_Renderer* renderer, GPU_Image* image) {
  PBKITSDLGPU_ASSERT(!"TODO: Implement me");
}

static GPU_Target* SDLCALL GetTarget(GPU_Renderer* renderer, GPU_Image* image) {
  if(!image)
    return nullptr;

  if(image->target)
    return image->target;

  if(!(renderer->enabled_features & GPU_FEATURE_RENDER_TARGETS))
    return nullptr;

  PBKITSDLGPU_ASSERT(!"TODO: Implement me");
  return nullptr;
}

static void SDLCALL FreeTarget(GPU_Renderer* renderer, GPU_Target* target) {
  PBKITSDLGPU_ASSERT(!"TODO: Implement me");
}

static void SDLCALL Blit(GPU_Renderer* renderer,
                         GPU_Image* image,
                         GPU_Rect* src_rect,
                         GPU_Target* target,
                         float x,
                         float y) {
  PBKITSDLGPU_ASSERT(!"TODO: Implement me");
}

static void SDLCALL BlitRotate(GPU_Renderer* renderer,
                               GPU_Image* image,
                               GPU_Rect* src_rect,
                               GPU_Target* target,
                               float x,
                               float y,
                               float degrees) {
  float w, h;
  if (image == NULL) {
    GPU_PushErrorCode("GPU_BlitRotate", GPU_ERROR_NULL_ARGUMENT, "image");
    return;
  }
  if (target == NULL) {
    GPU_PushErrorCode("GPU_BlitRotate", GPU_ERROR_NULL_ARGUMENT, "target");
    return;
  }

  w = (src_rect == NULL ? image->w : src_rect->w);
  h = (src_rect == NULL ? image->h : src_rect->h);
  renderer->impl->BlitTransformX(renderer, image, src_rect, target, x, y,
                                 w * image->anchor_x, h * image->anchor_y, degrees, 1.0f,
                                 1.0f);
}

static void SDLCALL BlitScale(GPU_Renderer* renderer,
                              GPU_Image* image,
                              GPU_Rect* src_rect,
                              GPU_Target* target,
                              float x,
                              float y,
                              float scaleX,
                              float scaleY) {
  float w, h;
  if (image == NULL) {
    GPU_PushErrorCode("GPU_BlitScale", GPU_ERROR_NULL_ARGUMENT, "image");
    return;
  }
  if (target == NULL) {
    GPU_PushErrorCode("GPU_BlitScale", GPU_ERROR_NULL_ARGUMENT, "target");
    return;
  }

  w = (src_rect == NULL ? image->w : src_rect->w);
  h = (src_rect == NULL ? image->h : src_rect->h);
  renderer->impl->BlitTransformX(renderer, image, src_rect, target, x, y,
                                 w * image->anchor_x, h * image->anchor_y, 0.0f, scaleX,
                                 scaleY);
}

static void SDLCALL BlitTransform(GPU_Renderer* renderer,
                                  GPU_Image* image,
                                  GPU_Rect* src_rect,
                                  GPU_Target* target,
                                  float x,
                                  float y,
                                  float degrees,
                                  float scaleX,
                                  float scaleY) {
  float w, h;
  if (image == NULL) {
    GPU_PushErrorCode("GPU_BlitTransform", GPU_ERROR_NULL_ARGUMENT, "image");
    return;
  }
  if (target == NULL) {
    GPU_PushErrorCode("GPU_BlitTransform", GPU_ERROR_NULL_ARGUMENT, "target");
    return;
  }

  w = (src_rect == NULL ? image->w : src_rect->w);
  h = (src_rect == NULL ? image->h : src_rect->h);
  renderer->impl->BlitTransformX(renderer, image, src_rect, target, x, y,
                                 w * image->anchor_x, h * image->anchor_y, degrees, scaleX,
                                 scaleY);
}

static void UnbindTexture(uint32_t stage = 0) {
  PBKITSDLGPU_ASSERT(stage < 4);
  auto p = pb_begin();
  // NV097_SET_TEXTURE_CONTROL0
  p = pb_push1(p, NV20_TCL_PRIMITIVE_3D_TX_ENABLE(stage), 0);

  // TODO: Store the texture stage programs so more than one stage may be used.
  PBKITSDLGPU_ASSERT(stage == 0);
  p = pb_push1(p, NV097_SET_SHADER_STAGE_PROGRAM, 0);
  pb_end(p);

  SetInputColorCombiner(0, SRC_DIFFUSE, false, MAP_UNSIGNED_IDENTITY, SRC_ZERO, false,
                        MAP_UNSIGNED_INVERT);
  SetInputAlphaCombiner(0, SRC_DIFFUSE, true, MAP_UNSIGNED_IDENTITY, SRC_ZERO, false,
                        MAP_UNSIGNED_INVERT);
}

static void BindTexture(GPU_Image* image, uint32_t stage = 0) {
  PBKITSDLGPU_ASSERT(stage < 4);
  // TODO: Store the texture stage programs so more than one stage may be used.
  PBKITSDLGPU_ASSERT(stage == 0);

  SetInputColorCombiner(0, SRC_TEX0, false, MAP_UNSIGNED_IDENTITY, SRC_ZERO, false,
                        MAP_UNSIGNED_INVERT);
  SetInputAlphaCombiner(0, SRC_TEX0, true, MAP_UNSIGNED_IDENTITY, SRC_ZERO, false,
                        MAP_UNSIGNED_INVERT);

  auto p = pb_begin();

  const uint32_t DMA_A = 1;

  auto image_data = (PBKitImageData*)image->data;

  // NV097_SET_TEXTURE_OFFSET
  p = pb_push1(p, NV20_TCL_PRIMITIVE_3D_TX_OFFSET(stage),
               (intptr_t)image_data->data & 0x03ffffff);

  uint32_t format = MASK(NV097_SET_TEXTURE_FORMAT_CONTEXT_DMA, DMA_A)
                    | MASK(NV097_SET_TEXTURE_FORMAT_CUBEMAP_ENABLE, 0)
                    | MASK(NV097_SET_TEXTURE_FORMAT_BORDER_SOURCE,
                           NV097_SET_TEXTURE_FORMAT_BORDER_SOURCE_COLOR)
                    | MASK(NV097_SET_TEXTURE_FORMAT_DIMENSIONALITY, 2)
                    | MASK(NV097_SET_TEXTURE_FORMAT_COLOR, image_data->format)
                    | MASK(NV097_SET_TEXTURE_FORMAT_MIPMAP_LEVELS, 1)
                    | MASK(NV097_SET_TEXTURE_FORMAT_BASE_SIZE_U, image_data->size_u)
                    | MASK(NV097_SET_TEXTURE_FORMAT_BASE_SIZE_V, image_data->size_v)
                    | MASK(NV097_SET_TEXTURE_FORMAT_BASE_SIZE_P, 0);
  // NV097_SET_TEXTURE_FORMAT
  p = pb_push1(p, NV20_TCL_PRIMITIVE_3D_TX_FORMAT(stage), format);

  uint32_t pitch_param = (image_data->pitch) << 16;
  // NV097_SET_TEXTURE_CONTROL1
  p = pb_push1(p, NV20_TCL_PRIMITIVE_3D_TX_NPOT_PITCH(stage), pitch_param);

  uint32_t size_param = (image->texture_w << 16) | (image->texture_h & 0xFFFF);
  // NV097_SET_TEXTURE_IMAGE_RECT
  p = pb_push1(p, NV20_TCL_PRIMITIVE_3D_TX_NPOT_SIZE(stage), size_param);

  // NV097_SET_TEXTURE_ADDRESS
  uint32_t texture_address = MASK(NV097_SET_TEXTURE_ADDRESS_U, WRAP_CLAMP_TO_EDGE)
                             | MASK(NV097_SET_TEXTURE_ADDRESS_CYLINDERWRAP_U, false)
                             | MASK(NV097_SET_TEXTURE_ADDRESS_V, WRAP_CLAMP_TO_EDGE)
                             | MASK(NV097_SET_TEXTURE_ADDRESS_CYLINDERWRAP_V, false)
                             | MASK(NV097_SET_TEXTURE_ADDRESS_P, WRAP_CLAMP_TO_EDGE)
                             | MASK(NV097_SET_TEXTURE_ADDRESS_CYLINDERWRAP_P, false)
                             | MASK(NV097_SET_TEXTURE_ADDRESS_CYLINDERWRAP_Q, false);
  p = pb_push1(p, NV20_TCL_PRIMITIVE_3D_TX_WRAP(stage), texture_address);

  // NV097_SET_TEXTURE_FILTER
  uint32_t texture_filter = MASK(NV097_SET_TEXTURE_FILTER_MIPMAP_LOD_BIAS, 0)
                            | MASK(NV097_SET_TEXTURE_FILTER_CONVOLUTION_KERNEL, K_QUINCUNX)
                            | MASK(NV097_SET_TEXTURE_FILTER_MIN, MIN_TENT_LOD0)
                            | MASK(NV097_SET_TEXTURE_FILTER_MAG, MAG_TENT_LOD0);
  p = pb_push1(p, NV20_TCL_PRIMITIVE_3D_TX_FILTER(stage), texture_filter);

  p = pb_push1(p, NV097_SET_TEXTURE_MATRIX_ENABLE + (4 * stage), false);

  // NV097_SET_TEXTURE_CONTROL0
  p = pb_push1(p, NV20_TCL_PRIMITIVE_3D_TX_ENABLE(stage),
               NV097_SET_TEXTURE_CONTROL0_ENABLE
                   | MASK(NV097_SET_TEXTURE_CONTROL0_ALPHA_KILL_ENABLE, false)
                   | MASK(NV097_SET_TEXTURE_CONTROL0_MIN_LOD_CLAMP, 0)
                   | MASK(NV097_SET_TEXTURE_CONTROL0_MAX_LOD_CLAMP, 4095));

  p = pb_push1(p, NV097_SET_SHADER_STAGE_PROGRAM,
               MASK(NV097_SET_SHADER_STAGE_PROGRAM_STAGE0, STAGE_2D_PROJECTIVE));

  pb_end(p);
}

// clang-format off
/*! Scales, rotates around a pivot point, and draws the given image to the given render target.
 * The drawing point (x, y) coincides with the pivot point on the src image (pivot_x, pivot_y).
	* \param src_rect The region of the source image to use.  Pass NULL for the entire image.
	* \param x Destination x-position
	* \param y Destination y-position
	* \param pivot_x Pivot x-position (in image coordinates)
	* \param pivot_y Pivot y-position (in image coordinates)
	* \param degrees Rotation angle (in degrees)
	* \param scaleX Horizontal stretch factor
	* \param scaleY Vertical stretch factor */
// clang-format on
static void SDLCALL BlitTransformX(GPU_Renderer* renderer,
                                   GPU_Image* image,
                                   GPU_Rect* src_rect,
                                   GPU_Target* target,
                                   float x,
                                   float y,
                                   float pivot_x,
                                   float pivot_y,
                                   float degrees,
                                   float scaleX,
                                   float scaleY) {
  if (image == NULL) {
    GPU_PushErrorCode("GPU_BlitTransformX", GPU_ERROR_NULL_ARGUMENT, "image");
    return;
  }
  if (target == NULL) {
    GPU_PushErrorCode("GPU_BlitTransformX", GPU_ERROR_NULL_ARGUMENT, "target");
    return;
  }
  if (renderer != image->renderer || renderer != target->renderer) {
    GPU_PushErrorCode("GPU_BlitTransformX", GPU_ERROR_USER_ERROR, "Mismatched renderer");
    return;
  }

  GPU_Rect fallback_surface_rect;
  if (!src_rect) {
    fallback_surface_rect = { 0.0f, 0.0f, (float)image->w, (float)image->h };
    src_rect = &fallback_surface_rect;
  }

  BindTexture(image);

  if (image->snap_mode == GPU_SNAP_POSITION
      || image->snap_mode == GPU_SNAP_POSITION_AND_DIMENSIONS) {
    // Avoid rounding errors in texture sampling by insisting on integral pixel positions
    x = floorf(x);
    y = floorf(y);
  }

  auto p = pb_begin();
  p = pb_push1(p, NV097_SET_FRONT_POLYGON_MODE, NV097_SET_FRONT_POLYGON_MODE_V_FILL);
  p = pb_push1(p, NV097_SET_BACK_POLYGON_MODE, NV097_SET_FRONT_POLYGON_MODE_V_FILL);

  p = pb_push1(p, NV097_SET_BEGIN_END, NV097_SET_BEGIN_END_OP_QUADS);

  x -= pivot_x;
  y -= pivot_y;

  auto vtx = [&p](float x, float y, float u, float v) {
    //    p = pb_push1(p, NV097_SET_DIFFUSE_COLOR4I, 0xFFFF00FF);
    p = pb_push2f(p, NV097_SET_TEXCOORD0_2F, u, v);
    p = pb_push4f(p, NV097_SET_VERTEX4F, x, y, 0, 1);
  };

  auto image_data = (PBKitImageData*)image->data;
  auto tex_coords = image_data->MakeTexCoords(src_rect, image);

  vtx(x, y, tex_coords.left, tex_coords.top);
  vtx(x + src_rect->w, y, tex_coords.right, tex_coords.top);
  vtx(x + src_rect->w, y + src_rect->h, tex_coords.right, tex_coords.bottom);
  vtx(x, y + src_rect->h, tex_coords.left, tex_coords.bottom);

  p = pb_push1(p, NV097_SET_BEGIN_END, NV097_SET_BEGIN_END_OP_END);
  pb_end(p);
}

static void SDLCALL PrimitiveBatchV(GPU_Renderer* renderer,
                                    GPU_Image* image,
                                    GPU_Target* target,
                                    GPU_PrimitiveEnum primitive_type,
                                    unsigned short num_vertices,
                                    void* values,
                                    unsigned int num_indices,
                                    unsigned short* indices,
                                    GPU_BatchFlagEnum flags) {
  PBKITSDLGPU_ASSERT(!"TODO: Implement me");
}

static void SDLCALL GenerateMipmaps(GPU_Renderer* renderer, GPU_Image* image) {
  PBKITSDLGPU_ASSERT(!"TODO: Implement me");
}

static GPU_Rect SDLCALL SetClip(
    GPU_Renderer* renderer, GPU_Target* target, Sint16 x, Sint16 y, Uint16 w, Uint16 h) {
  PBKITSDLGPU_ASSERT(!"TODO: Implement me");
  return {};
}

static void SDLCALL UnsetClip(GPU_Renderer* renderer, GPU_Target* target) {
  PBKITSDLGPU_ASSERT(!"TODO: Implement me");
}

static SDL_Color SDLCALL GetPixel(GPU_Renderer* renderer,
                                  GPU_Target* target,
                                  Sint16 x,
                                  Sint16 y) {
  PBKITSDLGPU_ASSERT(!"TODO: Implement me");
  return {};
}

static void SDLCALL SetImageFilter(GPU_Renderer* renderer,
                                   GPU_Image* image,
                                   GPU_FilterEnum filter) {
  // TODO: Implement me.
}

static void SDLCALL SetWrapMode(GPU_Renderer* renderer,
                                GPU_Image* image,
                                GPU_WrapEnum wrap_mode_x,
                                GPU_WrapEnum wrap_mode_y) {
  PBKITSDLGPU_ASSERT(!"TODO: Implement me");
}

static GPU_TextureHandle SDLCALL GetTextureHandle(GPU_Renderer* renderer, GPU_Image* image) {
  PBKITSDLGPU_ASSERT(!"TODO: Implement me");
  return {};
}

static void SDLCALL ClearRGBA(
    GPU_Renderer* renderer, GPU_Target* target, Uint8 r, Uint8 g, Uint8 b, Uint8 a) {
  PBKITSDLGPU_ASSERT(target->context);
  auto context = static_cast<PBKitSDLContext*>(target->context->data);
  pb_fill(0, 0, context->width, context->height, (a << 24) | (r << 16) | (g << 8) | b);
}

static void SDLCALL FlushBlitBuffer(GPU_Renderer* renderer) {
  //  PBKITSDLGPU_ASSERT(!"TODO: Implement me");
}

static void SDLCALL Flip(GPU_Renderer* renderer, GPU_Target* target) {
  renderer->impl->FlushBlitBuffer(renderer);

  while (pb_busy()) {
    /* Wait for completion... */
  }

  while (pb_finished()) {
    /* Not ready to swap yet */
  }

  pb_wait_for_vbl();
  pb_target_back_buffer();
  pb_reset();
}

static Uint32 SDLCALL CreateShaderProgram(GPU_Renderer* renderer) {
  PBKITSDLGPU_ASSERT(!"TODO: Implement me");
  return 0;
}

static void SDLCALL FreeShaderProgram(GPU_Renderer* renderer, Uint32 program_object) {
  PBKITSDLGPU_ASSERT(!"TODO: Implement me");
}

static Uint32 SDLCALL CompileShader_RW(GPU_Renderer* renderer,
                                       GPU_ShaderEnum shader_type,
                                       SDL_RWops* shader_source,
                                       GPU_bool free_rwops) {
  PBKITSDLGPU_ASSERT(!"TODO: Implement me");
  return 0;
}

static Uint32 SDLCALL CompileShader(GPU_Renderer* renderer,
                                    GPU_ShaderEnum shader_type,
                                    const char* shader_source) {
  PBKITSDLGPU_ASSERT(!"TODO: Implement me");
  return 0;
}

static void SDLCALL FreeShader(GPU_Renderer* renderer, Uint32 shader_object) {
  PBKITSDLGPU_ASSERT(!"TODO: Implement me");
}

static void SDLCALL AttachShader(GPU_Renderer* renderer,
                                 Uint32 program_object,
                                 Uint32 shader_object) {
  PBKITSDLGPU_ASSERT(!"TODO: Implement me");
}

static void SDLCALL DetachShader(GPU_Renderer* renderer,
                                 Uint32 program_object,
                                 Uint32 shader_object) {
  PBKITSDLGPU_ASSERT(!"TODO: Implement me");
}

static GPU_bool SDLCALL LinkShaderProgram(GPU_Renderer* renderer, Uint32 program_object) {
  PBKITSDLGPU_ASSERT(!"TODO: Implement me");
  return false;
}

static void SDLCALL ActivateShaderProgram(GPU_Renderer* renderer,
                                          Uint32 program_object,
                                          GPU_ShaderBlock* block) {
  PBKITSDLGPU_ASSERT(!"TODO: Implement me");
}

static void SDLCALL DeactivateShaderProgram(GPU_Renderer* renderer) {
  PBKITSDLGPU_ASSERT(!"TODO: Implement me");
}

static const char* SDLCALL GetShaderMessage(GPU_Renderer* renderer) {
  PBKITSDLGPU_ASSERT(!"TODO: Implement me");
  return nullptr;
}

static int SDLCALL GetAttributeLocation(GPU_Renderer* renderer,
                                        Uint32 program_object,
                                        const char* attrib_name) {
  PBKITSDLGPU_ASSERT(!"TODO: Implement me");
  return 0;
}

static int SDLCALL GetUniformLocation(GPU_Renderer* renderer,
                                      Uint32 program_object,
                                      const char* uniform_name) {
  PBKITSDLGPU_ASSERT(!"TODO: Implement me");
  return 0;
}

static GPU_ShaderBlock SDLCALL LoadShaderBlock(GPU_Renderer* renderer,
                                               Uint32 program_object,
                                               const char* position_name,
                                               const char* texcoord_name,
                                               const char* color_name,
                                               const char* modelViewMatrix_name) {
  PBKITSDLGPU_ASSERT(!"TODO: Implement me");
  return {};
}

static void SDLCALL SetShaderBlock(GPU_Renderer* renderer, GPU_ShaderBlock block) {
  PBKITSDLGPU_ASSERT(!"TODO: Implement me");
}

static void SDLCALL SetShaderImage(GPU_Renderer* renderer,
                                   GPU_Image* image,
                                   int location,
                                   int image_unit) {
  PBKITSDLGPU_ASSERT(!"TODO: Implement me");
}

static void SDLCALL GetUniformiv(GPU_Renderer* renderer,
                                 Uint32 program_object,
                                 int location,
                                 int* values) {
  PBKITSDLGPU_ASSERT(!"TODO: Implement me");
}

static void SDLCALL SetUniformi(GPU_Renderer* renderer, int location, int value) {
  PBKITSDLGPU_ASSERT(!"TODO: Implement me");
}

static void SDLCALL SetUniformiv(GPU_Renderer* renderer,
                                 int location,
                                 int num_elements_per_value,
                                 int num_values,
                                 int* values) {
  PBKITSDLGPU_ASSERT(!"TODO: Implement me");
}

static void SDLCALL GetUniformuiv(GPU_Renderer* renderer,
                                  Uint32 program_object,
                                  int location,
                                  unsigned int* values) {
  PBKITSDLGPU_ASSERT(!"TODO: Implement me");
}

static void SDLCALL SetUniformui(GPU_Renderer* renderer, int location, unsigned int value) {
  PBKITSDLGPU_ASSERT(!"TODO: Implement me");
}

static void SDLCALL SetUniformuiv(GPU_Renderer* renderer,
                                  int location,
                                  int num_elements_per_value,
                                  int num_values,
                                  unsigned int* values) {
  PBKITSDLGPU_ASSERT(!"TODO: Implement me");
}

static void SDLCALL GetUniformfv(GPU_Renderer* renderer,
                                 Uint32 program_object,
                                 int location,
                                 float* values) {
  PBKITSDLGPU_ASSERT(!"TODO: Implement me");
}

static void SDLCALL SetUniformf(GPU_Renderer* renderer, int location, float value) {
  PBKITSDLGPU_ASSERT(!"TODO: Implement me");
}

static void SDLCALL SetUniformfv(GPU_Renderer* renderer,
                                 int location,
                                 int num_elements_per_value,
                                 int num_values,
                                 float* values) {
  PBKITSDLGPU_ASSERT(!"TODO: Implement me");
}

static void SDLCALL SetUniformMatrixfv(GPU_Renderer* renderer,
                                       int location,
                                       int num_matrices,
                                       int num_rows,
                                       int num_columns,
                                       GPU_bool transpose,
                                       float* values) {
  PBKITSDLGPU_ASSERT(!"TODO: Implement me");
}

static void SDLCALL SetAttributef(GPU_Renderer* renderer, int location, float value) {
  PBKITSDLGPU_ASSERT(!"TODO: Implement me");
}

static void SDLCALL SetAttributei(GPU_Renderer* renderer, int location, int value) {
  PBKITSDLGPU_ASSERT(!"TODO: Implement me");
}

static void SDLCALL SetAttributeui(GPU_Renderer* renderer,
                                   int location,
                                   unsigned int value) {
  PBKITSDLGPU_ASSERT(!"TODO: Implement me");
}

static void SDLCALL SetAttributefv(GPU_Renderer* renderer,
                                   int location,
                                   int num_elements,
                                   float* value) {
  PBKITSDLGPU_ASSERT(!"TODO: Implement me");
}

static void SDLCALL SetAttributeiv(GPU_Renderer* renderer,
                                   int location,
                                   int num_elements,
                                   int* value) {
  PBKITSDLGPU_ASSERT(!"TODO: Implement me");
}

static void SDLCALL SetAttributeuiv(GPU_Renderer* renderer,
                                    int location,
                                    int num_elements,
                                    unsigned int* value) {
  PBKITSDLGPU_ASSERT(!"TODO: Implement me");
}

static void SDLCALL SetAttributeSource(GPU_Renderer* renderer,
                                       int num_values,
                                       GPU_Attribute source) {
  PBKITSDLGPU_ASSERT(!"TODO: Implement me");
}

static float SDLCALL SetLineThickness(GPU_Renderer* renderer, float thickness) {
  PBKITSDLGPU_ASSERT(!"TODO: Implement me");
  return 0.0f;
}

static float SDLCALL GetLineThickness(GPU_Renderer* renderer) {
  PBKITSDLGPU_ASSERT(!"TODO: Implement me");
  return 0.0f;
}

static void SDLCALL
    Pixel(GPU_Renderer* renderer, GPU_Target* target, float x, float y, SDL_Color color) {
  PBKITSDLGPU_ASSERT(!"TODO: Implement me");
}

static void SDLCALL Line(GPU_Renderer* renderer,
                         GPU_Target* target,
                         float x1,
                         float y1,
                         float x2,
                         float y2,
                         SDL_Color color) {
  PBKITSDLGPU_ASSERT(!"TODO: Implement me");
}

static void SDLCALL Arc(GPU_Renderer* renderer,
                        GPU_Target* target,
                        float x,
                        float y,
                        float radius,
                        float start_angle,
                        float end_angle,
                        SDL_Color color) {
  PBKITSDLGPU_ASSERT(!"TODO: Implement me");
}

static void SDLCALL ArcFilled(GPU_Renderer* renderer,
                              GPU_Target* target,
                              float x,
                              float y,
                              float radius,
                              float start_angle,
                              float end_angle,
                              SDL_Color color) {
  PBKITSDLGPU_ASSERT(!"TODO: Implement me");
}

static void SDLCALL Circle(GPU_Renderer* renderer,
                           GPU_Target* target,
                           float x,
                           float y,
                           float radius,
                           SDL_Color color) {
  PBKITSDLGPU_ASSERT(!"TODO: Implement me");
}

static void SDLCALL CircleFilled(GPU_Renderer* renderer,
                                 GPU_Target* target,
                                 float x,
                                 float y,
                                 float radius,
                                 SDL_Color color) {
  PBKITSDLGPU_ASSERT(!"TODO: Implement me");
}

static void SDLCALL Ellipse(GPU_Renderer* renderer,
                            GPU_Target* target,
                            float x,
                            float y,
                            float rx,
                            float ry,
                            float degrees,
                            SDL_Color color) {
  PBKITSDLGPU_ASSERT(!"TODO: Implement me");
}

static void SDLCALL EllipseFilled(GPU_Renderer* renderer,
                                  GPU_Target* target,
                                  float x,
                                  float y,
                                  float rx,
                                  float ry,
                                  float degrees,
                                  SDL_Color color) {
  PBKITSDLGPU_ASSERT(!"TODO: Implement me");
}

static void SDLCALL Sector(GPU_Renderer* renderer,
                           GPU_Target* target,
                           float x,
                           float y,
                           float inner_radius,
                           float outer_radius,
                           float start_angle,
                           float end_angle,
                           SDL_Color color) {
  PBKITSDLGPU_ASSERT(!"TODO: Implement me");
}

static void SDLCALL SectorFilled(GPU_Renderer* renderer,
                                 GPU_Target* target,
                                 float x,
                                 float y,
                                 float inner_radius,
                                 float outer_radius,
                                 float start_angle,
                                 float end_angle,
                                 SDL_Color color) {
  PBKITSDLGPU_ASSERT(!"TODO: Implement me");
}

static void SDLCALL Tri(GPU_Renderer* renderer,
                        GPU_Target* target,
                        float x1,
                        float y1,
                        float x2,
                        float y2,
                        float x3,
                        float y3,
                        SDL_Color color) {
  PBKITSDLGPU_ASSERT(!"TODO: Implement me");
}

static void SDLCALL TriFilled(GPU_Renderer* renderer,
                              GPU_Target* target,
                              float x1,
                              float y1,
                              float x2,
                              float y2,
                              float x3,
                              float y3,
                              SDL_Color color) {
  PBKITSDLGPU_ASSERT(!"TODO: Implement me");
}

static void SDLCALL Rectangle(GPU_Renderer* renderer,
                              GPU_Target* target,
                              float x1,
                              float y1,
                              float x2,
                              float y2,
                              SDL_Color color) {
  UnbindTexture();
  auto p = pb_begin();
  p = pb_push1(p, NV097_SET_FRONT_POLYGON_MODE, NV097_SET_FRONT_POLYGON_MODE_V_LINE);
  // Note: This shouldn't strictly be necessary, but at the moment xemu disallows different
  // fill modes for front and back.
  p = pb_push1(p, NV097_SET_BACK_POLYGON_MODE, NV097_SET_FRONT_POLYGON_MODE_V_LINE);

  p = pb_push1(p, NV097_SET_BEGIN_END, NV097_SET_BEGIN_END_OP_QUADS);

  auto vtx = [&p](float x, float y, float r, float g, float b, float a) {
    p = pb_push4f(p, NV097_SET_DIFFUSE_COLOR4F, r, g, b, a);
    p = pb_push4f(p, NV097_SET_VERTEX4F, x, y, 0, 1);
  };

  vtx(x1, y1, color.r, color.g, color.b, color.a);
  vtx(x2, y1, color.r, color.g, color.b, color.a);
  vtx(x2, y2, color.r, color.g, color.b, color.a);
  vtx(x1, y2, color.r, color.g, color.b, color.a);

  p = pb_push1(p, NV097_SET_BEGIN_END, NV097_SET_BEGIN_END_OP_END);

  p = pb_push1(p, NV097_SET_FRONT_POLYGON_MODE, NV097_SET_FRONT_POLYGON_MODE_V_FILL);
  p = pb_push1(p, NV097_SET_BACK_POLYGON_MODE, NV097_SET_FRONT_POLYGON_MODE_V_FILL);
  pb_end(p);
}

static void SDLCALL RectangleFilled(GPU_Renderer* renderer,
                                    GPU_Target* target,
                                    float x1,
                                    float y1,
                                    float x2,
                                    float y2,
                                    SDL_Color color) {
  UnbindTexture();
  auto p = pb_begin();
  p = pb_push1(p, NV097_SET_BEGIN_END, NV097_SET_BEGIN_END_OP_QUADS);

  auto vtx = [&p](float x, float y, uint32_t r, uint32_t g, uint32_t b, uint32_t a) {
    p = pb_push1(p, NV097_SET_DIFFUSE_COLOR4I, r + (g << 8) + (b << 16) + (a << 24));
    p = pb_push2f(p, NV097_SET_TEXCOORD0_2F, 0.0f, 0.0f);
    p = pb_push4f(p, NV097_SET_VERTEX4F, x, y, 1.0f, 1);
  };

  vtx(x1, y1, color.r, color.g, color.b, color.a);
  vtx(x2, y1, color.r, color.g, color.b, color.a);
  vtx(x2, y2, color.r, color.g, color.b, color.a);
  vtx(x1, y2, color.r, color.g, color.b, color.a);

  p = pb_push1(p, NV097_SET_BEGIN_END, NV097_SET_BEGIN_END_OP_END);
  pb_end(p);
}

static void SDLCALL RectangleRound(GPU_Renderer* renderer,
                                   GPU_Target* target,
                                   float x1,
                                   float y1,
                                   float x2,
                                   float y2,
                                   float radius,
                                   SDL_Color color) {
  PBKITSDLGPU_ASSERT(!"TODO: Implement me");
}

static void SDLCALL RectangleRoundFilled(GPU_Renderer* renderer,
                                         GPU_Target* target,
                                         float x1,
                                         float y1,
                                         float x2,
                                         float y2,
                                         float radius,
                                         SDL_Color color) {
  PBKITSDLGPU_ASSERT(!"TODO: Implement me");
}

static void SDLCALL Polygon(GPU_Renderer* renderer,
                            GPU_Target* target,
                            unsigned int num_vertices,
                            float* vertices,
                            SDL_Color color) {
  PBKITSDLGPU_ASSERT(!"TODO: Implement me");
}

static void SDLCALL Polyline(GPU_Renderer* renderer,
                             GPU_Target* target,
                             unsigned int num_vertices,
                             float* vertices,
                             SDL_Color color,
                             GPU_bool close_loop) {
  PBKITSDLGPU_ASSERT(!"TODO: Implement me");
}

static void SDLCALL PolygonFilled(GPU_Renderer* renderer,
                                  GPU_Target* target,
                                  unsigned int num_vertices,
                                  float* vertices,
                                  SDL_Color color) {
  PBKITSDLGPU_ASSERT(!"TODO: Implement me");
}

static GPU_Renderer* CreateRenderer(GPU_RendererID request) {
  GPU_Renderer* renderer = (GPU_Renderer*)SDL_malloc(sizeof(GPU_Renderer));
  if (!renderer) {
    return nullptr;
  }

  memset(renderer, 0, sizeof(GPU_Renderer));

  renderer->id = request;
  renderer->id.renderer = GPU_RENDERER_PBKIT;
  renderer->shader_language = GPU_LANGUAGE_NONE;
  renderer->min_shader_version = 0;
  renderer->max_shader_version = 0;

  renderer->default_image_anchor_x = 0.5f;
  renderer->default_image_anchor_y = 0.5f;

  renderer->current_context_target = nullptr;

  renderer->impl = (GPU_RendererImpl*)SDL_malloc(sizeof(GPU_RendererImpl));
  memset(renderer->impl, 0, sizeof(GPU_RendererImpl));

  // TODO: Populate the impl function pointers.
#define ASSIGN(fname) renderer->impl->fname = fname
  ASSIGN(Init);
  ASSIGN(CreateTargetFromWindow);
  ASSIGN(SetActiveTarget);
  ASSIGN(SetAsCurrent);
  ASSIGN(MakeCurrent);
  ASSIGN(SetAsCurrent);
  ASSIGN(ResetRendererState);
  ASSIGN(AddDepthBuffer);
  ASSIGN(SetWindowResolution);
  ASSIGN(SetVirtualResolution);
  ASSIGN(UnsetVirtualResolution);
  ASSIGN(Quit);
  ASSIGN(SetFullscreen);
  ASSIGN(SetCamera);
  ASSIGN(CreateImage);
  ASSIGN(CreateImageUsingTexture);
  ASSIGN(CreateAliasImage);
  ASSIGN(SaveImage);
  ASSIGN(CopyImage);
  ASSIGN(UpdateImage);
  ASSIGN(UpdateImageBytes);
  ASSIGN(ReplaceImage);
  ASSIGN(CopyImageFromSurface);
  ASSIGN(CopyImageFromTarget);
  ASSIGN(CopySurfaceFromTarget);
  ASSIGN(CopySurfaceFromImage);
  ASSIGN(FreeImage);
  ASSIGN(GetTarget);
  ASSIGN(FreeTarget);
  ASSIGN(Blit);
  ASSIGN(BlitRotate);
  ASSIGN(BlitScale);
  ASSIGN(BlitTransform);
  ASSIGN(BlitTransformX);
  ASSIGN(PrimitiveBatchV);
  ASSIGN(GenerateMipmaps);
  ASSIGN(SetClip);
  ASSIGN(UnsetClip);
  ASSIGN(GetPixel);
  ASSIGN(SetImageFilter);
  ASSIGN(SetWrapMode);
  ASSIGN(GetTextureHandle);
  ASSIGN(ClearRGBA);
  ASSIGN(FlushBlitBuffer);
  ASSIGN(Flip);
  ASSIGN(CreateShaderProgram);
  ASSIGN(FreeShaderProgram);
  ASSIGN(CompileShader_RW);
  ASSIGN(CompileShader);
  ASSIGN(FreeShader);
  ASSIGN(AttachShader);
  ASSIGN(DetachShader);
  ASSIGN(LinkShaderProgram);
  ASSIGN(ActivateShaderProgram);
  ASSIGN(DeactivateShaderProgram);
  ASSIGN(GetShaderMessage);
  ASSIGN(GetAttributeLocation);
  ASSIGN(GetUniformLocation);
  ASSIGN(LoadShaderBlock);
  ASSIGN(SetShaderBlock);
  ASSIGN(SetShaderImage);
  ASSIGN(GetUniformiv);
  ASSIGN(SetUniformi);
  ASSIGN(SetUniformiv);
  ASSIGN(GetUniformuiv);
  ASSIGN(SetUniformui);
  ASSIGN(SetUniformuiv);
  ASSIGN(GetUniformfv);
  ASSIGN(SetUniformf);
  ASSIGN(SetUniformfv);
  ASSIGN(SetUniformMatrixfv);
  ASSIGN(SetAttributef);
  ASSIGN(SetAttributei);
  ASSIGN(SetAttributeui);
  ASSIGN(SetAttributefv);
  ASSIGN(SetAttributeiv);
  ASSIGN(SetAttributeuiv);
  ASSIGN(SetAttributeSource);
  ASSIGN(SetLineThickness);
  ASSIGN(GetLineThickness);
  ASSIGN(Pixel);
  ASSIGN(Line);
  ASSIGN(Arc);
  ASSIGN(ArcFilled);
  ASSIGN(Circle);
  ASSIGN(CircleFilled);
  ASSIGN(Ellipse);
  ASSIGN(EllipseFilled);
  ASSIGN(Sector);
  ASSIGN(SectorFilled);
  ASSIGN(Tri);
  ASSIGN(TriFilled);
  ASSIGN(Rectangle);
  ASSIGN(RectangleFilled);
  ASSIGN(RectangleRound);
  ASSIGN(RectangleRoundFilled);
  ASSIGN(Polygon);
  ASSIGN(Polyline);
  ASSIGN(PolygonFilled);
#undef ASSIGN

  return renderer;
}

static void FreeRenderer(GPU_Renderer* renderer) {
  PBKITSDLGPU_ASSERT(!"TODO: IMPLEMENT ME");
}

}  // namespace PbkitSdlGpu

extern "C" {
// Dummy implementation of fopen_s to allow SDL_gpu to compile.
int fopen_s(FILE** pFile, const char* filename, const char* mode) {
  *pFile = fopen(filename, mode);
  return *pFile == 0;
}
}  // extern "C"

void PBKitSDLGPUInit() {
  PbkitSdlGpu::renderer_id = GPU_MakeRendererID("pbkit", PbkitSdlGpu::GPU_RENDERER_PBKIT, 1, 0);
  GPU_RegisterRenderer(PbkitSdlGpu::renderer_id, &PbkitSdlGpu::CreateRenderer, &PbkitSdlGpu::FreeRenderer);

  GPU_SetRendererOrder(1, &PbkitSdlGpu::renderer_id);
}
