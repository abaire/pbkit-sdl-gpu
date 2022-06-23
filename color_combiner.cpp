#include "color_combiner.h"
#include <hal/debug.h>
#include <pbkit/nv_regs.h>
#include <pbkit/pbkit.h>
#include "debug_output.h"

#define MASK(mask, val) (((val) << (__builtin_ffs(mask) - 1)) & (mask))
#define TO_BGRA(float_vals) \
  (((uint32_t)((float_vals)[3] * 255.0f) << 24) \
   + ((uint32_t)((float_vals)[0] * 255.0f) << 16) \
   + ((uint32_t)((float_vals)[1] * 255.0f) << 8) + ((uint32_t)((float_vals)[2] * 255.0f)))

void SetAlphaBlendEnabled(bool enable) {
  auto p = pb_begin();
  p = pb_push1(p, NV097_SET_BLEND_ENABLE, enable);
  if (enable) {
    p = pb_push1(p, NV097_SET_BLEND_EQUATION, NV097_SET_BLEND_EQUATION_V_FUNC_ADD);
    p = pb_push1(p, NV097_SET_BLEND_FUNC_SFACTOR, NV097_SET_BLEND_FUNC_SFACTOR_V_SRC_ALPHA);
    p = pb_push1(p, NV097_SET_BLEND_FUNC_DFACTOR,
                 NV097_SET_BLEND_FUNC_DFACTOR_V_ONE_MINUS_SRC_ALPHA);
  }
  pb_end(p);
}

void SetCombinerControl(int num_combiners,
                        bool same_factor0,
                        bool same_factor1,
                        bool mux_msb) {
  PBKITSDLGPU_ASSERT(num_combiners > 0 && num_combiners < 8);
  uint32_t setting = MASK(NV097_SET_COMBINER_CONTROL_ITERATION_COUNT, num_combiners);
  if (!same_factor0) {
    setting |= MASK(NV097_SET_COMBINER_CONTROL_FACTOR0,
                    NV097_SET_COMBINER_CONTROL_FACTOR0_EACH_STAGE);
  }
  if (!same_factor1) {
    setting |= MASK(NV097_SET_COMBINER_CONTROL_FACTOR1,
                    NV097_SET_COMBINER_CONTROL_FACTOR1_EACH_STAGE);
  }
  if (mux_msb) {
    setting |= MASK(NV097_SET_COMBINER_CONTROL_MUX_SELECT,
                    NV097_SET_COMBINER_CONTROL_MUX_SELECT_MSB);
  }

  auto p = pb_begin();
  p = pb_push1(p, NV097_SET_COMBINER_CONTROL, setting);
  pb_end(p);
}

static uint32_t MakeInputCombiner(CombinerSource a_source,
                                  bool a_alpha,
                                  CombinerMapping a_mapping,
                                  CombinerSource b_source,
                                  bool b_alpha,
                                  CombinerMapping b_mapping,
                                  CombinerSource c_source,
                                  bool c_alpha,
                                  CombinerMapping c_mapping,
                                  CombinerSource d_source,
                                  bool d_alpha,
                                  CombinerMapping d_mapping) {
  auto channel = [](CombinerSource src, bool alpha, CombinerMapping mapping) {
    return src + (alpha << 4) + (mapping << 5);
  };

  uint32_t ret = (channel(a_source, a_alpha, a_mapping) << 24)
                 + (channel(b_source, b_alpha, b_mapping) << 16)
                 + (channel(c_source, c_alpha, c_mapping) << 8)
                 + channel(d_source, d_alpha, d_mapping);
  return ret;
}

void SetInputColorCombiner(int combiner,
                           CombinerSource a_source,
                           bool a_alpha,
                           CombinerMapping a_mapping,
                           CombinerSource b_source,
                           bool b_alpha,
                           CombinerMapping b_mapping,
                           CombinerSource c_source,
                           bool c_alpha,
                           CombinerMapping c_mapping,
                           CombinerSource d_source,
                           bool d_alpha,
                           CombinerMapping d_mapping) {
  uint32_t value = MakeInputCombiner(a_source, a_alpha, a_mapping, b_source, b_alpha,
                                     b_mapping, c_source, c_alpha, c_mapping, d_source,
                                     d_alpha, d_mapping);
  auto p = pb_begin();
  p = pb_push1(p, NV097_SET_COMBINER_COLOR_ICW + combiner * 4, value);
  pb_end(p);
}

void ClearInputColorCombiner(int combiner) {
  auto p = pb_begin();
  p = pb_push1(p, NV097_SET_COMBINER_COLOR_ICW + combiner * 4, 0);
  pb_end(p);
}

void ClearInputColorCombiners() {
  auto p = pb_begin();
  pb_push_to(SUBCH_3D, p++, NV097_SET_COMBINER_COLOR_ICW, 8);
  *(p++) = 0x0;
  *(p++) = 0x0;
  *(p++) = 0x0;
  *(p++) = 0x0;
  *(p++) = 0x0;
  *(p++) = 0x0;
  *(p++) = 0x0;
  *(p++) = 0x0;
  pb_end(p);
}

void SetInputAlphaCombiner(int combiner,
                           CombinerSource a_source,
                           bool a_alpha,
                           CombinerMapping a_mapping,
                           CombinerSource b_source,
                           bool b_alpha,
                           CombinerMapping b_mapping,
                           CombinerSource c_source,
                           bool c_alpha,
                           CombinerMapping c_mapping,
                           CombinerSource d_source,
                           bool d_alpha,
                           CombinerMapping d_mapping) {
  uint32_t value = MakeInputCombiner(a_source, a_alpha, a_mapping, b_source, b_alpha,
                                     b_mapping, c_source, c_alpha, c_mapping, d_source,
                                     d_alpha, d_mapping);
  auto p = pb_begin();
  p = pb_push1(p, NV097_SET_COMBINER_ALPHA_ICW + combiner * 4, value);
  pb_end(p);
}

void ClearInputAlphaColorCombiner(int combiner) {
  auto p = pb_begin();
  p = pb_push1(p, NV097_SET_COMBINER_ALPHA_ICW + combiner * 4, 0);
  pb_end(p);
}

void ClearInputAlphaCombiners() {
  auto p = pb_begin();
  pb_push_to(SUBCH_3D, p++, NV097_SET_COMBINER_ALPHA_ICW, 8);
  *(p++) = 0x0;
  *(p++) = 0x0;
  *(p++) = 0x0;
  *(p++) = 0x0;
  *(p++) = 0x0;
  *(p++) = 0x0;
  *(p++) = 0x0;
  *(p++) = 0x0;
  pb_end(p);
}

static uint32_t MakeOutputCombiner(CombinerDest ab_dst,
                                   CombinerDest cd_dst,
                                   CombinerDest sum_dst,
                                   bool ab_dot_product,
                                   bool cd_dot_product,
                                   CombinerSumMuxMode sum_or_mux,
                                   CombinerOutOp op) {
  uint32_t ret = cd_dst | (ab_dst << 4) | (sum_dst << 8);
  if (cd_dot_product) {
    ret |= 1 << 12;
  }
  if (ab_dot_product) {
    ret |= 1 << 13;
  }
  if (sum_or_mux) {
    ret |= 1 << 14;
  }
  ret |= op << 15;

  return ret;
}

void SetOutputColorCombiner(int combiner,
                            CombinerDest ab_dst,
                            CombinerDest cd_dst,
                            CombinerDest sum_dst,
                            bool ab_dot_product,
                            bool cd_dot_product,
                            CombinerSumMuxMode sum_or_mux,
                            CombinerOutOp op,
                            bool alpha_from_ab_blue,
                            bool alpha_from_cd_blue) {
  uint32_t value = MakeOutputCombiner(ab_dst, cd_dst, sum_dst, ab_dot_product,
                                      cd_dot_product, sum_or_mux, op);
  if (alpha_from_ab_blue) {
    value |= (1 << 19);
  }
  if (alpha_from_cd_blue) {
    value |= (1 << 18);
  }

  auto p = pb_begin();
  p = pb_push1(p, NV097_SET_COMBINER_COLOR_OCW + combiner * 4, value);
  pb_end(p);
}

void ClearOutputColorCombiner(int combiner) {
  auto p = pb_begin();
  p = pb_push1(p, NV097_SET_COMBINER_COLOR_OCW + combiner * 4, 0);
  pb_end(p);
}

void ClearOutputColorCombiners() {
  auto p = pb_begin();
  pb_push_to(SUBCH_3D, p++, NV097_SET_COMBINER_COLOR_OCW, 8);
  *(p++) = 0x0;
  *(p++) = 0x0;
  *(p++) = 0x0;
  *(p++) = 0x0;
  *(p++) = 0x0;
  *(p++) = 0x0;
  *(p++) = 0x0;
  *(p++) = 0x0;
  pb_end(p);
}

void SetOutputAlphaCombiner(int combiner,
                            CombinerDest ab_dst,
                            CombinerDest cd_dst,
                            CombinerDest sum_dst,
                            bool ab_dot_product,
                            bool cd_dot_product,
                            CombinerSumMuxMode sum_or_mux,
                            CombinerOutOp op) {
  uint32_t value = MakeOutputCombiner(ab_dst, cd_dst, sum_dst, ab_dot_product,
                                      cd_dot_product, sum_or_mux, op);
  auto p = pb_begin();
  p = pb_push1(p, NV097_SET_COMBINER_ALPHA_OCW + combiner * 4, value);
  pb_end(p);
}

void ClearOutputAlphaColorCombiner(int combiner) {
  auto p = pb_begin();
  p = pb_push1(p, NV097_SET_COMBINER_ALPHA_OCW + combiner * 4, 0);
  pb_end(p);
}

void ClearOutputAlphaCombiners() {
  auto p = pb_begin();
  pb_push_to(SUBCH_3D, p++, NV097_SET_COMBINER_ALPHA_OCW, 8);
  *(p++) = 0x0;
  *(p++) = 0x0;
  *(p++) = 0x0;
  *(p++) = 0x0;
  *(p++) = 0x0;
  *(p++) = 0x0;
  *(p++) = 0x0;
  *(p++) = 0x0;
  pb_end(p);
}

void SetFinalCombiner0(CombinerSource a_source,
                       bool a_alpha,
                       bool a_invert,
                       CombinerSource b_source,
                       bool b_alpha,
                       bool b_invert,
                       CombinerSource c_source,
                       bool c_alpha,
                       bool c_invert,
                       CombinerSource d_source,
                       bool d_alpha,
                       bool d_invert) {
  auto channel = [](CombinerSource src, bool alpha, bool invert) {
    return src + (alpha << 4) + (invert << 5);
  };

  uint32_t value = (channel(a_source, a_alpha, a_invert) << 24)
                   + (channel(b_source, b_alpha, b_invert) << 16)
                   + (channel(c_source, c_alpha, c_invert) << 8)
                   + channel(d_source, d_alpha, d_invert);

  auto p = pb_begin();
  p = pb_push1(p, NV097_SET_COMBINER_SPECULAR_FOG_CW0, value);
  pb_end(p);
}

void SetFinalCombiner1(CombinerSource e_source,
                       bool e_alpha,
                       bool e_invert,
                       CombinerSource f_source,
                       bool f_alpha,
                       bool f_invert,
                       CombinerSource g_source,
                       bool g_alpha,
                       bool g_invert,
                       bool specular_add_invert_r0,
                       bool specular_add_invert_v1,
                       bool specular_clamp) {
  auto channel = [](CombinerSource src, bool alpha, bool invert) {
    return src + (alpha << 4) + (invert << 5);
  };

  // The V1+R0 sum is not available in CW1.
  PBKITSDLGPU_ASSERT(e_source != SRC_SPEC_R0_SUM && f_source != SRC_SPEC_R0_SUM
         && g_source != SRC_SPEC_R0_SUM);

  uint32_t value = (channel(e_source, e_alpha, e_invert) << 24)
                   + (channel(f_source, f_alpha, f_invert) << 16)
                   + (channel(g_source, g_alpha, g_invert) << 8);
  if (specular_add_invert_r0) {
    // NV097_SET_COMBINER_SPECULAR_FOG_CW1_SPECULAR_ADD_INVERT_R12 crashes on hardware.
    value += (1 << 5);
  }
  if (specular_add_invert_v1) {
    value += NV097_SET_COMBINER_SPECULAR_FOG_CW1_SPECULAR_ADD_INVERT_R5;
  }
  if (specular_clamp) {
    value += NV097_SET_COMBINER_SPECULAR_FOG_CW1_SPECULAR_CLAMP;
  }

  auto p = pb_begin();
  p = pb_push1(p, NV097_SET_COMBINER_SPECULAR_FOG_CW1, value);
  pb_end(p);
}

void SetCombinerFactorC0(int combiner, uint32_t value) {
  auto p = pb_begin();
  p = pb_push1(p, NV097_SET_COMBINER_FACTOR0 + 4 * combiner, value);
  pb_end(p);
}

void SetCombinerFactorC0(int combiner, float red, float green, float blue, float alpha) {
  float rgba[4]{ red, green, blue, alpha };
  SetCombinerFactorC0(combiner, TO_BGRA(rgba));
}

void SetCombinerFactorC1(int combiner, uint32_t value) {
  auto p = pb_begin();
  p = pb_push1(p, NV097_SET_COMBINER_FACTOR1 + 4 * combiner, value);
  pb_end(p);
}

void SetCombinerFactorC1(int combiner, float red, float green, float blue, float alpha) {
  float rgba[4]{ red, green, blue, alpha };
  SetCombinerFactorC1(combiner, TO_BGRA(rgba));
}

void SetFinalCombinerFactorC0(uint32_t value) {
  auto p = pb_begin();
  p = pb_push1(p, NV097_SET_SPECULAR_FOG_FACTOR, value);
  pb_end(p);
}

void SetFinalCombinerFactorC0(float red, float green, float blue, float alpha) {
  float rgba[4]{ red, green, blue, alpha };
  SetFinalCombinerFactorC0(TO_BGRA(rgba));
}

void SetFinalCombinerFactorC1(uint32_t value) {
  auto p = pb_begin();
  p = pb_push1(p, NV097_SET_SPECULAR_FOG_FACTOR + 0x04, value);
  pb_end(p);
}

void SetFinalCombinerFactorC1(float red, float green, float blue, float alpha) {
  float rgba[4]{ red, green, blue, alpha };
  SetFinalCombinerFactorC1(TO_BGRA(rgba));
}
