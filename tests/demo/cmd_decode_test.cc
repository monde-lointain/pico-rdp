// Stream C.2 — RDP command decoder + shadow-state tests.
//
// The demo_cmd_decode_test target links demo_core + gtest_main (it does not link
// the host-only rdp_viewer target, where these sources also compile). To exercise
// the viewer-side decoder/shadow here we compile their TUs directly into the test
// via #include — they are self-contained (only <stdint.h>/<string.h>/<stdio.h>),
// so this needs no extra link deps and no cmake change.
//
// Field expectations are cross-checked against the renderer's own handlers in
// src/rdp/rdp/*.c (the source of truth), per the design's shadow-state contract.

#include <gtest/gtest.h>
#include <stdint.h>

#include "../../src/viewer/cmd_decode.h"
#include "../../src/viewer/shadow_state.h"

// Compile the implementations directly (see header note above).
#include "../../src/viewer/cmd_decode.cc"
#include "../../src/viewer/shadow_state.cc"

namespace {

// --- helpers: build raw command words exactly as the renderer parses them -----

static uint32_t hdr(uint8_t id, uint32_t lo24) { return ((uint32_t)id << 24) | (lo24 & 0x0ffffff); }

// SET_COLOR_IMAGE 0x3f: w0 = [format<<21|size<<19|(width-1)], w1=addr
static void set_color_image(uint32_t *w, uint8_t fmt, uint8_t size, uint16_t width, uint32_t addr) {
  w[0] = hdr(RDPCMD_SET_COLOR_IMAGE, ((uint32_t)fmt << 21) | ((uint32_t)size << 19) | ((width - 1) & 0x3ff));
  w[1] = addr & 0x0ffffff;
}

// SET_FILL_COLOR 0x37: w1 = raw 32-bit fill word.
static void set_fill_color(uint32_t *w, uint32_t raw) {
  w[0] = hdr(RDPCMD_SET_FILL_COLOR, 0);
  w[1] = raw;
}

// --- word-count table -------------------------------------------------------

TEST(CmdDecodeWordCount, MatchesRdpCommandsTable) {
  EXPECT_EQ(rdp_cmd_word_count(RDPCMD_NO_OP), 2u);
  EXPECT_EQ(rdp_cmd_word_count(RDPCMD_FILL_TRIANGLE), 8u);       // 32 bytes
  EXPECT_EQ(rdp_cmd_word_count(RDPCMD_FILL_ZBUFFER_TRIANGLE), 12u);  // 48
  EXPECT_EQ(rdp_cmd_word_count(RDPCMD_TEXTURE_TRIANGLE), 24u);   // 96
  EXPECT_EQ(rdp_cmd_word_count(RDPCMD_SHADE_TEXTURE_Z_TRIANGLE), 44u);  // 176
  EXPECT_EQ(rdp_cmd_word_count(RDPCMD_TEXTURE_RECTANGLE), 4u);   // 16 bytes
  EXPECT_EQ(rdp_cmd_word_count(RDPCMD_SET_COMBINE), 2u);
  EXPECT_EQ(rdp_cmd_word_count(RDPCMD_SET_COLOR_IMAGE), 2u);
  // Unknown opcode still advances by a word pair.
  EXPECT_EQ(rdp_cmd_word_count(0x11), 2u);
}

// --- decoder: known commands ------------------------------------------------

TEST(CmdDecode, SetColorImageFields) {
  uint32_t w[2];
  set_color_image(w, /*fmt=*/0 /*RGBA*/, /*size=*/2 /*16b*/, /*width=*/240, /*addr=*/0x123456);
  CmdRecord rec;
  uint32_t consumed = cmd_decode(w, 2, &rec);
  EXPECT_EQ(consumed, 2u);
  EXPECT_EQ(rec.id, RDPCMD_SET_COLOR_IMAGE);
  EXPECT_TRUE(rec.known);
  EXPECT_FALSE(rec.is_triangle);
  EXPECT_STREQ(rec.name, "SET_COLOR_IMAGE");
  EXPECT_NE(strstr(rec.summary, "RGBA"), nullptr);
  EXPECT_NE(strstr(rec.summary, "16b"), nullptr);
  EXPECT_NE(strstr(rec.summary, "w=240"), nullptr);
  EXPECT_NE(strstr(rec.summary, "0x123456"), nullptr);
}

TEST(CmdDecode, SetCombineKnownAndNamed) {
  uint32_t w[2] = {hdr(RDPCMD_SET_COMBINE, 0), 0};
  CmdRecord rec;
  cmd_decode(w, 2, &rec);
  EXPECT_EQ(rec.id, RDPCMD_SET_COMBINE);
  EXPECT_STREQ(rec.name, "SET_COMBINE");
  EXPECT_TRUE(rec.known);
}

TEST(CmdDecode, TriangleClassifiedAndHeaderDecoded) {
  // SHADE_ZBUFFER_TRIANGLE 0x0d, dir=1, level=2, tile=3, yl=0x100.
  uint32_t w0 = ((uint32_t)RDPCMD_SHADE_ZBUFFER_TRIANGLE << 24) | (1u << 23) |
                (2u << 19) | (3u << 16) | 0x100u;
  uint32_t w[2] = {w0, 0};
  CmdRecord rec;
  uint32_t consumed = cmd_decode(w, 2, &rec);
  EXPECT_TRUE(rec.is_triangle);
  EXPECT_TRUE(rec.known);
  EXPECT_EQ(rec.word_count, 28u);  // 112 bytes
  EXPECT_EQ(consumed, 28u);        // returns full length even if only 2 avail
  EXPECT_NE(strstr(rec.summary, "dir=1"), nullptr);
  EXPECT_NE(strstr(rec.summary, "lvl=2"), nullptr);
  EXPECT_NE(strstr(rec.summary, "tile=3"), nullptr);
  // Truncation noted because avail(2) < needed(28).
  EXPECT_NE(strstr(rec.summary, "trunc"), nullptr);
}

TEST(CmdDecode, UnknownOpcodeRawHexFallbackNoCrash) {
  // 0x11 is a renderer-invalid opcode.
  uint32_t w[2] = {hdr(0x11, 0xabcdef), 0xdeadbeef};
  CmdRecord rec;
  uint32_t consumed = cmd_decode(w, 2, &rec);
  EXPECT_EQ(rec.id, 0x11);
  EXPECT_FALSE(rec.known);
  EXPECT_FALSE(rec.is_triangle);
  EXPECT_STREQ(rec.name, "UNKNOWN");
  EXPECT_NE(strstr(rec.summary, "raw"), nullptr);
  EXPECT_NE(strstr(rec.summary, "0xdeadbeef"), nullptr);
  EXPECT_EQ(consumed, 2u);
}

TEST(CmdDecode, NullAndShortInputsDoNotCrash) {
  CmdRecord rec;
  EXPECT_EQ(cmd_decode(nullptr, 0, &rec), 2u);
  EXPECT_STREQ(rec.name, "UNKNOWN");
  // One word available (header only) for a 2-word command: no over-read.
  uint32_t w1 = hdr(RDPCMD_SET_SCISSOR, 0);
  EXPECT_EQ(cmd_decode(&w1, 1, &rec), 2u);
  EXPECT_STREQ(rec.name, "SET_SCISSOR");
  // Decode into null record returns the safe default.
  EXPECT_EQ(cmd_decode(&w1, 1, nullptr), 2u);
}

// --- shadow state -----------------------------------------------------------

TEST(ShadowState, ResetZeroes) {
  ShadowState s;
  memset(&s, 0xaa, sizeof s);
  shadow_reset(&s);
  EXPECT_EQ(s.cmd_count, 0u);
  EXPECT_EQ(s.color_addr, 0u);
  EXPECT_EQ(s.seen_opcodes, 0u);
}

TEST(ShadowState, ScriptedStreamReflectsAllThree) {
  ShadowState s;
  shadow_reset(&s);

  // 1) SET_COLOR_IMAGE
  uint32_t ci[2];
  set_color_image(ci, /*fmt=*/0, /*size=*/2, /*width=*/240, /*addr=*/0x100000);
  shadow_apply(&s, ci, 2);

  // 2) SET_FILL_COLOR
  uint32_t fc[2];
  set_fill_color(fc, 0x0001ffff);
  shadow_apply(&s, fc, 2);

  // 3) SET_COMBINE (sub_a_rgb0=0xc, mul_rgb0=0x10, add_a1=0x7 chosen as markers)
  uint32_t cw0 = (0xcu << 20) | (0x10u << 15);
  uint32_t cw1 = 0x7u;  // add_a1
  uint32_t cm[2] = {hdr(RDPCMD_SET_COMBINE, cw0 & 0x0ffffff), cw1};
  shadow_apply(&s, cm, 2);

  // All three reflected.
  EXPECT_EQ(s.color_format, 0u);
  EXPECT_EQ(s.color_size, 2u);
  EXPECT_EQ(s.color_width, 240u);
  EXPECT_EQ(s.color_addr, 0x100000u);
  EXPECT_EQ(s.fill_color_raw, 0x0001ffffu);
  EXPECT_EQ(s.combine.sub_a_rgb0, 0xcu);
  EXPECT_EQ(s.combine.mul_rgb0, 0x10u);
  EXPECT_EQ(s.combine.add_a1, 0x7u);

  EXPECT_EQ(s.cmd_count, 3u);
  EXPECT_TRUE(s.seen_opcodes & (1ull << RDPCMD_SET_COLOR_IMAGE));
  EXPECT_TRUE(s.seen_opcodes & (1ull << RDPCMD_SET_FILL_COLOR));
  EXPECT_TRUE(s.seen_opcodes & (1ull << RDPCMD_SET_COMBINE));
}

TEST(ShadowState, OtherModesBitsDecoded) {
  ShadowState s;
  shadow_reset(&s);
  // cycle_type=3 (FILL) at w0[21:20], persp_tex_en=1 at w0[19];
  // z_compare_en=1 at w1[4], z_update_en=1 at w1[5], antialias_en=1 at w1[3].
  uint32_t w0 = (3u << 20) | (1u << 19);
  uint32_t w1 = (1u << 5) | (1u << 4) | (1u << 3);
  uint32_t w[2] = {hdr(RDPCMD_SET_OTHER_MODES, w0 & 0x0ffffff), w1};
  shadow_apply(&s, w, 2);
  EXPECT_EQ(s.other_modes.cycle_type, 3u);
  EXPECT_EQ(s.other_modes.persp_tex_en, 1u);
  EXPECT_EQ(s.other_modes.z_compare_en, 1u);
  EXPECT_EQ(s.other_modes.z_update_en, 1u);
  EXPECT_EQ(s.other_modes.antialias_en, 1u);
  EXPECT_EQ(s.other_modes.alpha_compare_en, 0u);
}

TEST(ShadowState, TileDescriptorAndSize) {
  ShadowState s;
  shadow_reset(&s);
  // SET_TILE tile=2, format=2 (CI), size=0 (4b), line=8, tmem=0, palette=5, cs=1.
  uint32_t sw0 = (2u << 21) | (0u << 19) | (8u << 9) | 0u;
  uint32_t sw1 = (2u << 24) | (5u << 20) | (1u << 9);
  uint32_t st[2] = {hdr(RDPCMD_SET_TILE, sw0 & 0x0ffffff), sw1};
  shadow_apply(&s, st, 2);
  EXPECT_EQ(s.tile[2].format, 2u);
  EXPECT_EQ(s.tile[2].size, 0u);
  EXPECT_EQ(s.tile[2].line, 8u);
  EXPECT_EQ(s.tile[2].palette, 5u);
  EXPECT_EQ(s.tile[2].cs, 1u);

  // SET_TILE_SIZE tile=2: sl=0,tl=0,sh=252 (=63<<2),th=252.
  uint32_t zw0 = (0u << 12) | 0u;
  uint32_t zw1 = (2u << 24) | (252u << 12) | 252u;
  uint32_t sz[2] = {hdr(RDPCMD_SET_TILE_SIZE, zw0 & 0x0ffffff), zw1};
  shadow_apply(&s, sz, 2);
  EXPECT_EQ(s.tile[2].sh, 252u);
  EXPECT_EQ(s.tile[2].th, 252u);
}

TEST(ShadowState, ColorsAndImagesAndScissor) {
  ShadowState s;
  shadow_reset(&s);

  uint32_t prim[2] = {hdr(RDPCMD_SET_PRIM_COLOR, (3u << 8) | 0x20u),
                      0x11223344u};
  shadow_apply(&s, prim, 2);
  EXPECT_EQ(s.prim_color.r, 0x11u);
  EXPECT_EQ(s.prim_color.g, 0x22u);
  EXPECT_EQ(s.prim_color.b, 0x33u);
  EXPECT_EQ(s.prim_color.a, 0x44u);
  EXPECT_EQ(s.prim_min_level, 3u);
  EXPECT_EQ(s.prim_lod_frac, 0x20u);

  uint32_t env[2] = {hdr(RDPCMD_SET_ENV_COLOR, 0), 0xaabbccddu};
  shadow_apply(&s, env, 2);
  EXPECT_EQ(s.env_color.r, 0xaau);
  EXPECT_EQ(s.env_color.a, 0xddu);

  uint32_t ti[2];
  ti[0] = hdr(RDPCMD_SET_TEXTURE_IMAGE, (2u << 21) | (0u << 19) | (63u & 0x3ff));
  ti[1] = 0x200000u;
  shadow_apply(&s, ti, 2);
  EXPECT_EQ(s.tex_format, 2u);
  EXPECT_EQ(s.tex_width, 64u);
  EXPECT_EQ(s.tex_addr, 0x200000u);

  uint32_t mi[2] = {hdr(RDPCMD_SET_MASK_IMAGE, 0), 0x300000u};
  shadow_apply(&s, mi, 2);
  EXPECT_EQ(s.depth_addr, 0x300000u);

  // SET_SCISSOR xh=0,yh=0,xl=960 (=240<<2),yl=960.
  uint32_t sc[2];
  sc[0] = hdr(RDPCMD_SET_SCISSOR, 0);
  sc[1] = (960u << 12) | 960u;
  shadow_apply(&s, sc, 2);
  EXPECT_EQ(s.scissor.xl, 960u);
  EXPECT_EQ(s.scissor.yl, 960u);
}

TEST(ShadowState, UnknownAndShortInputsSafe) {
  ShadowState s;
  shadow_reset(&s);
  // Unknown opcode: counted + seen, no field change, no crash.
  uint32_t unk[2] = {hdr(0x15, 0), 0xffffffffu};
  shadow_apply(&s, unk, 2);
  EXPECT_EQ(s.cmd_count, 1u);
  EXPECT_TRUE(s.seen_opcodes & (1ull << 0x15));
  EXPECT_EQ(s.color_addr, 0u);
  // Short / null inputs ignored.
  shadow_apply(&s, unk, 1);
  shadow_apply(nullptr, unk, 2);
  shadow_apply(&s, nullptr, 2);
  EXPECT_EQ(s.cmd_count, 1u);
}

}  // namespace
