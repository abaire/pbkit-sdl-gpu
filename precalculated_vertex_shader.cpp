#include "precalculated_vertex_shader.h"
#include <pbkit/pbkit.h>
#include <string>

namespace PbkitSdlGpu {

#define MASK(mask, val) (((val) << (__builtin_ffs(mask) - 1)) & (mask))

// clang format off
static constexpr uint32_t kShader[] = {
    // var float2 I.tex0 : $vin.TEXCOORD0 : TEXCOORD0 : 0 : 1
    // var float2 I.tex1 : $vin.TEXCOORD1 : TEXCOORD1 : 0 : 1
    // var float2 I.tex2 : $vin.TEXCOORD2 : TEXCOORD2 : 0 : 1
    // var float2 I.tex3 : $vin.TEXCOORD3 : TEXCOORD3 : 0 : 1
    // var float4 I.pos : $vin.POSITION : ATTR0 : 0 : 1
    // var float4 I.diffuse : $vin.DIFFUSE : ATTR3 : 0 : 1
    // var float4 main.pos : $vout.POSITION : HPOS : -1 : 1
    // var float4 main.col : $vout.COLOR : COL0 : -1 : 1
    // var float2 main.tex0 : $vout.TEXCOORD0 : TEX0 : -1 : 1
    // var float2 main.tex1 : $vout.TEXCOORD1 : TEX1 : -1 : 1
    // var float2 main.tex2 : $vout.TEXCOORD2 : TEX2 : -1 : 1
    // var float2 main.tex3 : $vout.TEXCOORD3 : TEX3 : -1 : 1
    // 6 instructions, 0 R-regs
    0x00000000, 0x0020001b, 0x0836106c, 0x2070f800, 0x00000000, 0x0020061b, 0x0836106c, 0x2070f818,
    0x00000000, 0x0020121b, 0x0836106c, 0x2070c848, 0x00000000, 0x0020141b, 0x0836106c, 0x2070c850,
    0x00000000, 0x0020161b, 0x0836106c, 0x2070c858, 0x00000000, 0x0020181b, 0x0836106c, 0x2070c861,
};
// clang format on

void LoadPrecalculatedVertexShader() {
  uint32_t* p;
  int i;

  p = pb_begin();

  // Set run address of shader
  p = pb_push1(p, NV097_SET_TRANSFORM_PROGRAM_START, 0);

  p = pb_push1(
      p, NV097_SET_TRANSFORM_EXECUTION_MODE,
      MASK(NV097_SET_TRANSFORM_EXECUTION_MODE_MODE, NV097_SET_TRANSFORM_EXECUTION_MODE_MODE_PROGRAM) |
          MASK(NV097_SET_TRANSFORM_EXECUTION_MODE_RANGE_MODE, NV097_SET_TRANSFORM_EXECUTION_MODE_RANGE_MODE_PRIV));

  p = pb_push1(p, NV097_SET_TRANSFORM_PROGRAM_CXT_WRITE_EN, 0);
  pb_end(p);

  // Set cursor and begin copying program
  p = pb_begin();
  p = pb_push1(p, NV097_SET_TRANSFORM_PROGRAM_LOAD, 0);
  pb_end(p);

  for (i = 0; i < sizeof(kShader) / 16; i++) {
    p = pb_begin();
    pb_push(p++, NV097_SET_TRANSFORM_PROGRAM, 4);
    memcpy(p, &kShader[i * 4], 4 * 4);
    p += 4;
    pb_end(p);
  }
}

} // namespace PbkitSdlGpu