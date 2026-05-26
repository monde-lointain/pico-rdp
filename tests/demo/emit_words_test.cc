// Stream B.3: RDP command emitter (src/demo/emit.c) word-level tests.
//
// Triangle bit-exactness: this TU contains an independent reference copy of the
// oracle's triangle word packing (RDP::CommandBuilder::submit_clipped_primitive,
// tests/conformance/vendor/rdp_command_builder.cpp), transcribed directly from
// that source. We feed several DemoPrimSetup inputs (fill / shade / Z /
// textured-perspective) through emit_triangle and assert word-for-word equality
// vs the reference. The emit_words test target links only demo_core + gtest
// (not rdp::conformance_utils), so the oracle algorithm is mirrored here rather
// than called; the reference below is the authority.
//
// Non-triangle commands: we decode the packed fields and assert they carry the
// expected values (cross-checked against the RDP register layouts).

#include <gtest/gtest.h>

#include <stdint.h>
#include <string.h>

#include "cmd_sink.h"
#include "emit.h"
#include "setup_ir.h"

namespace {

// --- Capture sink -------------------------------------------------------------

struct Capture {
  uint32_t words[64];
  uint32_t n;
};

void capture_emit(void *ctx, const uint32_t *words, uint32_t n) {
  Capture *c = (Capture *)ctx;
  c->n = n;
  for (uint32_t i = 0; i < n && i < 64; ++i) c->words[i] = words[i];
}

CmdSink make_sink(Capture *c) {
  c->n = 0;
  CmdSink s;
  s.emit = capture_emit;
  s.ctx = c;
  return s;
}

// --- Reference triangle packer (transcribed from the oracle) ------------------

uint32_t ref_mask(int32_t val, unsigned bits) {
  return (uint32_t)val & (((uint32_t)1 << bits) - 1u);
}

// Bit-for-bit transcription of submit_clipped_primitive's 44-word fill.
void ref_triangle(const DemoPrimSetup &s, uint32_t cmd[44]) {
  for (int i = 0; i < 44; ++i) cmd[i] = 0;
  cmd[0] |= uint32_t(0x0f) << 24;
  if (!(s.pos.flags & DEMO_PRIMITIVE_RIGHT_MAJOR_BIT)) cmd[0] |= 1u << 23;
  cmd[0] |= 0u << 16;       // tile
  cmd[0] |= 6u << 19;       // max_level
  cmd[1] |= ref_mask(s.pos.y_lo, 14);
  cmd[0] |= ref_mask(s.pos.y_hi, 14);
  cmd[1] |= ref_mask(s.pos.y_mid, 14) << 16;
  cmd[2] = ref_mask(s.pos.x_c, 28);
  cmd[3] = ref_mask(s.pos.dxdy_c, 30);
  cmd[4] = ref_mask(s.pos.x_a, 28);
  cmd[5] = (uint32_t)s.pos.dxdy_a;
  cmd[6] = ref_mask(s.pos.x_b, 28);
  cmd[7] = ref_mask(s.pos.dxdy_b, 30);

  cmd[8] |= (uint32_t)s.attr.c[0] & 0xffff0000u;
  cmd[12] |= ((uint32_t)s.attr.c[0] << 16) & 0xffff0000u;
  cmd[8] |= ((uint32_t)s.attr.c[1] >> 16) & 0xffffu;
  cmd[12] |= (uint32_t)s.attr.c[1] & 0xffffu;
  cmd[9] |= (uint32_t)s.attr.c[2] & 0xffff0000u;
  cmd[13] |= ((uint32_t)s.attr.c[2] << 16) & 0xffff0000u;
  cmd[9] |= ((uint32_t)s.attr.c[3] >> 16) & 0xffffu;
  cmd[13] |= (uint32_t)s.attr.c[3] & 0xffffu;

  cmd[10] |= (uint32_t)s.attr.dcdx[0] & 0xffff0000u;
  cmd[14] |= ((uint32_t)s.attr.dcdx[0] << 16) & 0xffff0000u;
  cmd[10] |= ((uint32_t)s.attr.dcdx[1] >> 16) & 0xffffu;
  cmd[14] |= (uint32_t)s.attr.dcdx[1] & 0xffffu;
  cmd[11] |= (uint32_t)s.attr.dcdx[2] & 0xffff0000u;
  cmd[15] |= ((uint32_t)s.attr.dcdx[2] << 16) & 0xffff0000u;
  cmd[11] |= ((uint32_t)s.attr.dcdx[3] >> 16) & 0xffffu;
  cmd[15] |= (uint32_t)s.attr.dcdx[3] & 0xffffu;

  cmd[16] |= (uint32_t)s.attr.dcde[0] & 0xffff0000u;
  cmd[20] |= ((uint32_t)s.attr.dcde[0] << 16) & 0xffff0000u;
  cmd[16] |= ((uint32_t)s.attr.dcde[1] >> 16) & 0xffffu;
  cmd[20] |= (uint32_t)s.attr.dcde[1] & 0xffffu;
  cmd[17] |= (uint32_t)s.attr.dcde[2] & 0xffff0000u;
  cmd[21] |= ((uint32_t)s.attr.dcde[2] << 16) & 0xffff0000u;
  cmd[17] |= ((uint32_t)s.attr.dcde[3] >> 16) & 0xffffu;
  cmd[21] |= (uint32_t)s.attr.dcde[3] & 0xffffu;

  cmd[18] |= (uint32_t)s.attr.dcdy[0] & 0xffff0000u;
  cmd[22] |= ((uint32_t)s.attr.dcdy[0] << 16) & 0xffff0000u;
  cmd[18] |= ((uint32_t)s.attr.dcdy[1] >> 16) & 0xffffu;
  cmd[22] |= (uint32_t)s.attr.dcdy[1] & 0xffffu;
  cmd[19] |= (uint32_t)s.attr.dcdy[2] & 0xffff0000u;
  cmd[23] |= ((uint32_t)s.attr.dcdy[2] << 16) & 0xffff0000u;
  cmd[19] |= ((uint32_t)s.attr.dcdy[3] >> 16) & 0xffffu;
  cmd[23] |= (uint32_t)s.attr.dcdy[3] & 0xffffu;

  cmd[24] |= (uint32_t)s.attr.u & 0xffff0000u;
  cmd[28] |= ((uint32_t)s.attr.u << 16) & 0xffff0000u;
  cmd[24] |= ((uint32_t)s.attr.v >> 16) & 0xffffu;
  cmd[28] |= (uint32_t)s.attr.v & 0xffffu;
  cmd[25] |= (uint32_t)s.attr.w & 0xffff0000u;
  cmd[29] |= ((uint32_t)s.attr.w << 16) & 0xffff0000u;
  cmd[26] |= (uint32_t)s.attr.dudx & 0xffff0000u;
  cmd[30] |= ((uint32_t)s.attr.dudx << 16) & 0xffff0000u;
  cmd[26] |= ((uint32_t)s.attr.dvdx >> 16) & 0xffffu;
  cmd[30] |= (uint32_t)s.attr.dvdx & 0xffffu;
  cmd[27] |= (uint32_t)s.attr.dwdx & 0xffff0000u;
  cmd[31] |= ((uint32_t)s.attr.dwdx << 16) & 0xffff0000u;
  cmd[32] |= (uint32_t)s.attr.dude & 0xffff0000u;
  cmd[36] |= ((uint32_t)s.attr.dude << 16) & 0xffff0000u;
  cmd[32] |= ((uint32_t)s.attr.dvde >> 16) & 0xffffu;
  cmd[36] |= (uint32_t)s.attr.dvde & 0xffffu;
  cmd[33] |= (uint32_t)s.attr.dwde & 0xffff0000u;
  cmd[37] |= ((uint32_t)s.attr.dwde << 16) & 0xffff0000u;
  cmd[34] |= (uint32_t)s.attr.dudy & 0xffff0000u;
  cmd[38] |= ((uint32_t)s.attr.dudy << 16) & 0xffff0000u;
  cmd[34] |= ((uint32_t)s.attr.dvdy >> 16) & 0xffffu;
  cmd[38] |= (uint32_t)s.attr.dvdy & 0xffffu;
  cmd[35] |= (uint32_t)s.attr.dwdy & 0xffff0000u;
  cmd[39] |= ((uint32_t)s.attr.dwdy << 16) & 0xffff0000u;

  cmd[40] = (uint32_t)s.attr.z;
  cmd[41] = (uint32_t)s.attr.dzdx;
  cmd[42] = (uint32_t)s.attr.dzde;
  cmd[43] = (uint32_t)s.attr.dzdy;
}

void fill_seq(int32_t *p, int n, int32_t base, int32_t step) {
  for (int i = 0; i < n; ++i) p[i] = base + i * step;
}

// A nontrivial setup with distinct, sign-bearing values in every field so any
// misplaced word or mask shows up.
DemoPrimSetup make_setup(bool right_major, bool perspective) {
  DemoPrimSetup s;
  memset(&s, 0, sizeof s);
  s.pos.x_a = 0x0123abcd;
  s.pos.x_b = -0x00765432;
  s.pos.x_c = 0x07ffffff;
  s.pos.dxdy_a = -0x12345678;  // keeps sign bit
  s.pos.dxdy_b = 0x2abcdef0;
  s.pos.dxdy_c = -0x0fedcba9;
  s.pos.y_lo = 0x1abc;
  s.pos.y_mid = -0x0123;
  s.pos.y_hi = 0x0def;
  s.pos.flags = (right_major ? DEMO_PRIMITIVE_RIGHT_MAJOR_BIT : 0) |
                (perspective ? DEMO_PRIMITIVE_PERSPECTIVE_CORRECT_BIT : 0);
  fill_seq(s.attr.c, 4, 0x11110000, 0x01230456);
  fill_seq(s.attr.dcdx, 4, -0x22220000, 0x00330044);
  fill_seq(s.attr.dcde, 4, 0x33330000, -0x00110022);
  fill_seq(s.attr.dcdy, 4, -0x44440000, 0x00550066);
  s.attr.z = 0x7abc1234;
  s.attr.dzdx = -0x00112233;
  s.attr.dzde = 0x44556677;
  s.attr.dzdy = -0x7766aabb;
  s.attr.u = 0x0fff8001;
  s.attr.v = -0x00345678;
  s.attr.w = 0x12340000;
  s.attr.dudx = 0x00010002;
  s.attr.dvdx = -0x00030004;
  s.attr.dwdx = 0x00050006;
  s.attr.dude = -0x00070008;
  s.attr.dvde = 0x0009000a;
  s.attr.dwde = -0x000b000c;
  s.attr.dudy = 0x000d000e;
  s.attr.dvdy = -0x000f0010;
  s.attr.dwdy = 0x00110012;
  return s;
}

void expect_triangle_bit_exact(const DemoPrimSetup &s) {
  Capture c;
  CmdSink sink = make_sink(&c);
  emit_triangle(&sink, &s);
  ASSERT_EQ(c.n, 44u);
  ASSERT_EQ(c.words[0] >> 24, 0x0fu) << "command id";
  uint32_t ref[44];
  ref_triangle(s, ref);
  for (int i = 0; i < 44; ++i)
    EXPECT_EQ(c.words[i], ref[i]) << "triangle word " << i;
}

TEST(EmitTriangle, RightMajorNoPerspective) {
  expect_triangle_bit_exact(make_setup(true, false));
}
TEST(EmitTriangle, LeftMajorSetsBit23) {
  DemoPrimSetup s = make_setup(false, false);
  expect_triangle_bit_exact(s);
  Capture c;
  CmdSink sink = make_sink(&c);
  emit_triangle(&sink, &s);
  EXPECT_TRUE(c.words[0] & (1u << 23)) << "left-major -> bit 23 set";
}
TEST(EmitTriangle, RightMajorClearsBit23) {
  DemoPrimSetup s = make_setup(true, false);
  Capture c;
  CmdSink sink = make_sink(&c);
  emit_triangle(&sink, &s);
  EXPECT_FALSE(c.words[0] & (1u << 23));
}
TEST(EmitTriangle, TexturedPerspective) {
  expect_triangle_bit_exact(make_setup(true, true));
}
TEST(EmitTriangle, ZeroSetupAllZeroAttrWords) {
  DemoPrimSetup s;
  memset(&s, 0, sizeof s);
  s.pos.flags = DEMO_PRIMITIVE_RIGHT_MAJOR_BIT;
  expect_triangle_bit_exact(s);
}

// Spot-check a couple of hand-derived attribute splits (R value c[0]).
TEST(EmitTriangle, AttrSplitLayout) {
  DemoPrimSetup s;
  memset(&s, 0, sizeof s);
  s.pos.flags = DEMO_PRIMITIVE_RIGHT_MAJOR_BIT;
  s.attr.c[0] = (int32_t)0xaaaabbbb;  // R: hi -> cmd[8], lo -> cmd[12]
  Capture c;
  CmdSink sink = make_sink(&c);
  emit_triangle(&sink, &s);
  EXPECT_EQ(c.words[8] & 0xffff0000u, 0xaaaa0000u);
  EXPECT_EQ(c.words[12] & 0xffff0000u, 0xbbbb0000u);
}

// --- Non-triangle commands ----------------------------------------------------

TEST(EmitState, SetColorImage) {
  Capture c;
  CmdSink sink = make_sink(&c);
  emit_set_color_image(&sink, DEMO_TEXFMT_RGBA, DEMO_TEXSIZE_16BPP, 0x00abcdef,
                       240);
  ASSERT_EQ(c.n, 2u);
  EXPECT_EQ(c.words[0] >> 24, 0x3fu);
  EXPECT_EQ((c.words[0] >> 21) & 7u, (uint32_t)DEMO_TEXFMT_RGBA);
  EXPECT_EQ((c.words[0] >> 19) & 3u, (uint32_t)DEMO_TEXSIZE_16BPP);
  EXPECT_EQ(c.words[0] & 1023u, 239u);  // width-1
  EXPECT_EQ(c.words[1], 0x00abcdefu);
}

TEST(EmitState, SetDepthImage) {
  Capture c;
  CmdSink sink = make_sink(&c);
  emit_set_depth_image(&sink, 0x0001c200);
  ASSERT_EQ(c.n, 2u);
  EXPECT_EQ(c.words[0] >> 24, 0x3eu);
  EXPECT_EQ(c.words[1], 0x0001c200u);
}

TEST(EmitState, SetTextureImage) {
  Capture c;
  CmdSink sink = make_sink(&c);
  emit_set_texture_image(&sink, DEMO_TEXFMT_CI, DEMO_TEXSIZE_4BPP, 0x00038400,
                         64);
  ASSERT_EQ(c.n, 2u);
  EXPECT_EQ(c.words[0] >> 24, 0x3du);
  EXPECT_EQ((c.words[0] >> 21) & 7u, (uint32_t)DEMO_TEXFMT_CI);
  EXPECT_EQ((c.words[0] >> 19) & 3u, (uint32_t)DEMO_TEXSIZE_4BPP);
  EXPECT_EQ(c.words[0] & 0x3ffu, 63u);
  EXPECT_EQ(c.words[1], 0x00038400u & 0x00ffffffu);
}

TEST(EmitState, SetCombine) {
  Capture c;
  CmdSink sink = make_sink(&c);
  emit_set_combine(&sink, 0x00abcdef, 0x12345678);
  ASSERT_EQ(c.n, 2u);
  EXPECT_EQ(c.words[0] >> 24, 0x3cu);
  EXPECT_EQ(c.words[0] & 0x00ffffffu, 0x00abcdefu);
  EXPECT_EQ(c.words[1], 0x12345678u);
}

TEST(EmitState, SetOtherModesDefaultsLikeOracle) {
  // Oracle default flush_default_state: Cycle1, dithers Off(3), bilerps true,
  // everything else false -> word1 == 0.
  DemoOtherModes m;
  memset(&m, 0, sizeof m);
  m.cycle_type = DEMO_CYCLE_1;
  m.rgb_dither = 3;
  m.alpha_dither = 3;
  m.bilerp_0 = 1;
  m.bilerp_1 = 1;
  Capture c;
  CmdSink sink = make_sink(&c);
  emit_set_other_modes(&sink, &m);
  ASSERT_EQ(c.n, 2u);
  uint32_t expect0 = (uint32_t)0x2f << 24;
  expect0 |= 1u << 11;  // bilerp0
  expect0 |= 1u << 10;  // bilerp1
  expect0 |= 3u << 6;   // rgb dither
  expect0 |= 3u << 4;   // alpha dither
  EXPECT_EQ(c.words[0], expect0);
  EXPECT_EQ(c.words[1], 0u);
}

TEST(EmitState, SetOtherModesFlags) {
  DemoOtherModes m;
  memset(&m, 0, sizeof m);
  m.cycle_type = DEMO_CYCLE_FILL;
  m.perspective = 1;
  m.z_compare = 1;
  m.z_update = 1;
  m.aa = 1;
  m.blend_en = 1;
  Capture c;
  CmdSink sink = make_sink(&c);
  emit_set_other_modes(&sink, &m);
  EXPECT_EQ((c.words[0] >> 20) & 3u, (uint32_t)DEMO_CYCLE_FILL);
  EXPECT_TRUE(c.words[0] & (1u << 19));  // perspective
  EXPECT_TRUE(c.words[1] & (1u << 14));  // blend_en
  EXPECT_TRUE(c.words[1] & (1u << 5));   // z update
  EXPECT_TRUE(c.words[1] & (1u << 4));   // z compare
  EXPECT_TRUE(c.words[1] & (1u << 3));   // aa
}

TEST(EmitState, SetTile) {
  DemoTileDesc d;
  memset(&d, 0, sizeof d);
  d.fmt = DEMO_TEXFMT_CI;
  d.size = DEMO_TEXSIZE_4BPP;
  d.stride = 64;   // bytes -> 8 words
  d.offset = 128;  // bytes -> 16 words
  d.palette = 0xa;
  d.shift_s = 1;
  d.shift_t = 2;
  d.mask_s = 6;
  d.mask_t = 6;
  d.flags = DEMO_TILE_CLAMP_S_BIT | DEMO_TILE_CLAMP_T_BIT;
  Capture c;
  CmdSink sink = make_sink(&c);
  emit_set_tile(&sink, 3, &d);
  ASSERT_EQ(c.n, 2u);
  EXPECT_EQ(c.words[0] >> 24, 0x35u);
  EXPECT_EQ((c.words[0] >> 21) & 7u, (uint32_t)DEMO_TEXFMT_CI);
  EXPECT_EQ((c.words[0] >> 19) & 3u, (uint32_t)DEMO_TEXSIZE_4BPP);
  EXPECT_EQ((c.words[0] >> 9) & 0x1ffu, 8u);  // stride/8
  EXPECT_EQ(c.words[0] & 0x1ffu, 16u);        // offset/8
  EXPECT_EQ(c.words[1] & 0xfu, 1u);           // shift_s
  EXPECT_EQ((c.words[1] >> 4) & 0xfu, 6u);    // mask_s
  EXPECT_TRUE(c.words[1] & (1u << 9));        // clamp_s
  EXPECT_EQ((c.words[1] >> 10) & 0xfu, 2u);   // shift_t
  EXPECT_TRUE(c.words[1] & (1u << 19));       // clamp_t
  EXPECT_EQ((c.words[1] >> 20) & 0xfu, 0xau); // palette
  EXPECT_EQ((c.words[1] >> 24) & 7u, 3u);     // tile
}

TEST(EmitLoad, SetTileSizeSubpixelBias) {
  Capture c;
  CmdSink sink = make_sink(&c);
  emit_set_tile_size(&sink, 0, 0, 0, 64, 64);
  ASSERT_EQ(c.n, 2u);
  EXPECT_EQ(c.words[0] >> 24, 0x32u);
  EXPECT_EQ((c.words[0] >> 12) & 0xfffu, 0u);    // sl
  EXPECT_EQ(c.words[0] & 0xfffu, 0u);            // tl
  EXPECT_EQ((c.words[1] >> 12) & 0xfffu, 252u);  // sh = 64*4-4
  EXPECT_EQ(c.words[1] & 0xfffu, 252u);          // th
}

TEST(EmitLoad, LoadTile) {
  Capture c;
  CmdSink sink = make_sink(&c);
  emit_load_tile(&sink, 0, 0, 0, 64, 64);
  ASSERT_EQ(c.n, 2u);
  EXPECT_EQ(c.words[0] >> 24, 0x34u);
  EXPECT_EQ((c.words[1] >> 12) & 0xfffu, 252u);
}

TEST(EmitLoad, LoadTlutPixelBias) {
  Capture c;
  CmdSink sink = make_sink(&c);
  emit_load_tlut(&sink, 1, 0, 0, 16, 1);
  ASSERT_EQ(c.n, 2u);
  EXPECT_EQ(c.words[0] >> 24, 0x30u);
  EXPECT_EQ((c.words[1] >> 24) & 7u, 1u);
  // sh = (0 + 16 - 1) << 2 = 60
  EXPECT_EQ((c.words[1] >> 12) & 0xfffu, 60u);
}

TEST(EmitClear, SetFillColor) {
  Capture c;
  CmdSink sink = make_sink(&c);
  emit_set_fill_color(&sink, 0xdeadbeef);
  ASSERT_EQ(c.n, 2u);
  EXPECT_EQ(c.words[0] >> 24, 0x37u);
  EXPECT_EQ(c.words[1], 0xdeadbeefu);
}

TEST(EmitClear, SetScissor) {
  Capture c;
  CmdSink sink = make_sink(&c);
  emit_set_scissor(&sink, 0, 0, 240, 240);
  ASSERT_EQ(c.n, 2u);
  EXPECT_EQ(c.words[0] >> 24, 0x2du);
  EXPECT_EQ((c.words[0] >> 12) & 0xfffu, 0u);     // xh
  EXPECT_EQ((c.words[1] >> 12) & 0xfffu, 960u);   // xl = 240*4
  EXPECT_EQ(c.words[1] & 0xfffu, 960u);           // yl
}

TEST(EmitClear, FillRectangle) {
  Capture c;
  CmdSink sink = make_sink(&c);
  emit_fill_rectangle(&sink, 0, 0, 240, 240);
  ASSERT_EQ(c.n, 2u);
  EXPECT_EQ(c.words[0] >> 24, 0x36u);
  EXPECT_EQ((c.words[1] >> 12) & 0xfffu, 0u);     // xl (low corner)
  EXPECT_EQ((c.words[0] >> 12) & 0xfffu, 956u);   // xh = 240*4-4
  EXPECT_EQ(c.words[0] & 0xfffu, 956u);           // yh
}

TEST(EmitSync, AllFour) {
  struct {
    void (*fn)(CmdSink *);
    uint32_t op;
  } cases[] = {
      {emit_sync_full, 0x29u},
      {emit_sync_pipe, 0x27u},
      {emit_sync_tile, 0x28u},
      {emit_sync_load, 0x26u},
  };
  for (int i = 0; i < 4; ++i) {
    Capture c;
    CmdSink sink = make_sink(&c);
    cases[i].fn(&sink);
    ASSERT_EQ(c.n, 2u);
    EXPECT_EQ(c.words[0] >> 24, cases[i].op);
    EXPECT_EQ(c.words[1], 0u);
  }
}

TEST(EmitSink, NullSafe) {
  emit_sync_full(nullptr);
  CmdSink s;
  s.emit = nullptr;
  s.ctx = nullptr;
  emit_sync_full(&s);  // must not crash
  SUCCEED();
}

}  // namespace
