#pragma once

#include <stdint.h>

enum CombinerSource
{
  SRC_ZERO = 0, // 0
  SRC_C0, // Constant[0]
  SRC_C1, // Constant[1]
  SRC_FOG, // Fog coordinate
  SRC_DIFFUSE, // Vertex diffuse
  SRC_SPECULAR, // Vertex specular
  SRC_6, // ?
  SRC_7, // ?
  SRC_TEX0, // Texcoord0
  SRC_TEX1, // Texcoord1
  SRC_TEX2, // Texcoord2
  SRC_TEX3, // Texcoord3
  SRC_R0, // R0 from the vertex shader
  SRC_R1, // R1 from the vertex shader
  SRC_SPEC_R0_SUM, // Specular + R0
  SRC_EF_PROD, // Combiner param E * F
};

enum CombinerDest
{
  DST_DISCARD = 0, // Discard the calculation
  DST_C0, // Constant[0]
  DST_C1, // Constant[1]
  DST_FOG, // Fog coordinate
  DST_DIFFUSE, // Vertex diffuse
  DST_SPECULAR, // Vertex specular
  DST_6, // ?
  DST_7, // ?
  DST_TEX0, // Texcoord0
  DST_TEX1, // Texcoord1
  DST_TEX2, // Texcoord2
  DST_TEX3, // Texcoord3
  DST_R0, // R0 from the vertex shader
  DST_R1, // R1 from the vertex shader
  DST_SPEC_R0_SUM, // Specular + R1
  DST_EF_PROD, // Combiner param E * F
};

enum CombinerSumMuxMode
{
  SM_SUM = 0, // ab + cd
  SM_MUX = 1, // r0.a is used to select cd or ab
};

enum CombinerOutOp
{
  OP_IDENTITY = 0, // y = x
  OP_BIAS = 1, // y = x - 0.5
  OP_SHIFT_LEFT_1 = 2, // y = x*2
  OP_SHIFT_LEFT_1_BIAS = 3, // y = (x - 0.5)*2
  OP_SHIFT_LEFT_2 = 4, // y = x*4
  OP_SHIFT_RIGHT_1 = 6, // y = x/2
};

enum CombinerMapping
{
  MAP_UNSIGNED_IDENTITY, // max(0,x)         OK for final combiner
  MAP_UNSIGNED_INVERT, // 1 - max(0,x)     OK for final combiner
  MAP_EXPAND_NORMAL, // 2*max(0,x) - 1   invalid for final combiner
  MAP_EXPAND_NEGATE, // 1 - 2*max(0,x)   invalid for final combiner
  MAP_HALFBIAS_NORMAL, // max(0,x) - 1/2   invalid for final combiner
  MAP_HALFBIAS_NEGATE, // 1/2 - max(0,x)   invalid for final combiner
  MAP_SIGNED_IDENTITY, // x                invalid for final combiner
  MAP_SIGNED_NEGATE, // -x               invalid for final combiner
};

struct CombinerInput {
  CombinerSource source;
  bool alpha;
  CombinerMapping mapping;
};

struct ColorInput : public CombinerInput {
  explicit ColorInput(CombinerSource s, CombinerMapping m = MAP_UNSIGNED_IDENTITY) :
      CombinerInput() {
    source = s;
    alpha = false;
    mapping = m;
  }
};

struct AlphaInput : public CombinerInput {
  explicit AlphaInput(CombinerSource s, CombinerMapping m = MAP_UNSIGNED_IDENTITY) :
      CombinerInput() {
    source = s;
    alpha = true;
    mapping = m;
  }
};

struct ZeroInput : public CombinerInput {
  explicit ZeroInput() : CombinerInput() {
    source = SRC_ZERO;
    alpha = false;
    mapping = MAP_UNSIGNED_IDENTITY;
  }
};

struct NegativeOneInput : public CombinerInput {
  explicit NegativeOneInput() : CombinerInput() {
    source = SRC_ZERO;
    alpha = false;
    mapping = MAP_EXPAND_NORMAL;
  }
};

struct OneInput : public CombinerInput {
  explicit OneInput() : CombinerInput() {
    source = SRC_ZERO;
    alpha = false;
    mapping = MAP_UNSIGNED_INVERT;
  }
};

void SetAlphaBlendEnabled(bool enable = true);

// Sets up the number of enabled color combiners and behavior flags.
//
// same_factor0 == true will reuse the C0 constant across all enabled stages.
// same_factor1 == true will reuse the C1 constant across all enabled stages.
void SetCombinerControl(int num_combiners = 1,
                        bool same_factor0 = false,
                        bool same_factor1 = false,
                        bool mux_msb = false);

void SetInputColorCombiner(int combiner,
                           CombinerSource a_source = SRC_ZERO,
                           bool a_alpha = false,
                           CombinerMapping a_mapping = MAP_UNSIGNED_IDENTITY,
                           CombinerSource b_source = SRC_ZERO,
                           bool b_alpha = false,
                           CombinerMapping b_mapping = MAP_UNSIGNED_IDENTITY,
                           CombinerSource c_source = SRC_ZERO,
                           bool c_alpha = false,
                           CombinerMapping c_mapping = MAP_UNSIGNED_IDENTITY,
                           CombinerSource d_source = SRC_ZERO,
                           bool d_alpha = false,
                           CombinerMapping d_mapping = MAP_UNSIGNED_IDENTITY);

inline void SetInputColorCombiner(int combiner,
                                  CombinerInput a,
                                  CombinerInput b = ZeroInput(),
                                  CombinerInput c = ZeroInput(),
                                  CombinerInput d = ZeroInput()) {
  SetInputColorCombiner(combiner, a.source, a.alpha, a.mapping, b.source, b.alpha,
                        b.mapping, c.source, c.alpha, c.mapping, d.source, d.alpha,
                        d.mapping);
}

void ClearInputColorCombiner(int combiner);
void ClearInputColorCombiners();

void SetInputAlphaCombiner(int combiner,
                           CombinerSource a_source = SRC_ZERO,
                           bool a_alpha = false,
                           CombinerMapping a_mapping = MAP_UNSIGNED_IDENTITY,
                           CombinerSource b_source = SRC_ZERO,
                           bool b_alpha = false,
                           CombinerMapping b_mapping = MAP_UNSIGNED_IDENTITY,
                           CombinerSource c_source = SRC_ZERO,
                           bool c_alpha = false,
                           CombinerMapping c_mapping = MAP_UNSIGNED_IDENTITY,
                           CombinerSource d_source = SRC_ZERO,
                           bool d_alpha = false,
                           CombinerMapping d_mapping = MAP_UNSIGNED_IDENTITY);

inline void SetInputAlphaCombiner(int combiner,
                                  CombinerInput a,
                                  CombinerInput b = ZeroInput(),
                                  CombinerInput c = ZeroInput(),
                                  CombinerInput d = ZeroInput()) {
  SetInputAlphaCombiner(combiner, a.source, a.alpha, a.mapping, b.source, b.alpha,
                        b.mapping, c.source, c.alpha, c.mapping, d.source, d.alpha,
                        d.mapping);
}

void ClearInputAlphaColorCombiner(int combiner);
void ClearInputAlphaCombiners();

void SetOutputColorCombiner(int combiner,
                            CombinerDest ab_dst = DST_DISCARD,
                            CombinerDest cd_dst = DST_DISCARD,
                            CombinerDest sum_dst = DST_DISCARD,
                            bool ab_dot_product = false,
                            bool cd_dot_product = false,
                            CombinerSumMuxMode sum_or_mux = SM_SUM,
                            CombinerOutOp op = OP_IDENTITY,
                            bool alpha_from_ab_blue = false,
                            bool alpha_from_cd_blue = false);
void ClearOutputColorCombiner(int combiner);
void ClearOutputColorCombiners();

void SetOutputAlphaCombiner(int combiner,
                            CombinerDest ab_dst = DST_DISCARD,
                            CombinerDest cd_dst = DST_DISCARD,
                            CombinerDest sum_dst = DST_DISCARD,
                            bool ab_dot_product = false,
                            bool cd_dot_product = false,
                            CombinerSumMuxMode sum_or_mux = SM_SUM,
                            CombinerOutOp op = OP_IDENTITY);
void ClearOutputAlphaColorCombiner(int combiner);
void ClearOutputAlphaCombiners();

void SetFinalCombiner0(CombinerSource a_source = SRC_ZERO,
                       bool a_alpha = false,
                       bool a_invert = false,
                       CombinerSource b_source = SRC_ZERO,
                       bool b_alpha = false,
                       bool b_invert = false,
                       CombinerSource c_source = SRC_ZERO,
                       bool c_alpha = false,
                       bool c_invert = false,
                       CombinerSource d_source = SRC_ZERO,
                       bool d_alpha = false,
                       bool d_invert = false);

inline void SetFinalCombiner0Just(CombinerSource d_source,
                                  bool d_alpha = false,
                                  bool d_invert = false) {
  SetFinalCombiner0(SRC_ZERO, false, false, SRC_ZERO, false, false, SRC_ZERO, false, false,
                    d_source, d_alpha, d_invert);
}

void SetFinalCombiner1(CombinerSource e_source = SRC_ZERO,
                       bool e_alpha = false,
                       bool e_invert = false,
                       CombinerSource f_source = SRC_ZERO,
                       bool f_alpha = false,
                       bool f_invert = false,
                       CombinerSource g_source = SRC_ZERO,
                       bool g_alpha = false,
                       bool g_invert = false,
                       bool specular_add_invert_r0 = false,
                       bool specular_add_invert_v1 = false,
                       bool specular_clamp = false);

inline void SetFinalCombiner1Just(CombinerSource g_source,
                                  bool g_alpha = false,
                                  bool g_invert = false) {
  SetFinalCombiner1(SRC_ZERO, false, false, SRC_ZERO, false, false, g_source, g_alpha,
                    g_invert);
}

void SetCombinerFactorC0(int combiner, uint32_t value);
void SetCombinerFactorC0(int combiner, float red, float green, float blue, float alpha);
void SetCombinerFactorC1(int combiner, uint32_t value);
void SetCombinerFactorC1(int combiner, float red, float green, float blue, float alpha);

void SetFinalCombinerFactorC0(uint32_t value);
void SetFinalCombinerFactorC0(float red, float green, float blue, float alpha);
void SetFinalCombinerFactorC1(uint32_t value);
void SetFinalCombinerFactorC1(float red, float green, float blue, float alpha);
