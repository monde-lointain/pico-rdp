#pragma once

// Stream C.2 — viewer-side shadow of RDP state.
//
// The renderer (rdp_core) exposes NO accessor for its internal RDP state; the
// state inspector therefore reconstructs ("shadows") that state by replaying
// the raw RDP command word stream the viewer feeds to rdpx_rdp_cmd (and the
// same stream loaded from a .rdp dump). This struct mirrors the *settable* RDP
// state the inspector cares about: combine mux, other-modes bits, the eight
// tile descriptors, the color/depth/texture image configs, the fixed colors,
// and the scissor rect. Field layouts match the renderer's handlers in
// src/rdp/rdp/*.c exactly (verified against rdp_set_*; this is the source of
// truth, not gbi.h).
//
// Pure C ABI / Orthodox POD: no rendering, SDL or ImGui dependency. A panel
// reads these fields directly. The decoder (cmd_decode.h) drives updates via
// shadow_apply().

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// SET_COMBINE mux (0x3c) — 16 sub-fields, two combine cycles.
struct ShadowCombine {
  uint8_t sub_a_rgb0, sub_b_rgb0, mul_rgb0, add_rgb0;
  uint8_t sub_a_a0, sub_b_a0, mul_a0, add_a0;
  uint8_t sub_a_rgb1, sub_b_rgb1, mul_rgb1, add_rgb1;
  uint8_t sub_a_a1, sub_b_a1, mul_a1, add_a1;
};

// SET_OTHER_MODES (0x2f) — decoded bit-fields the inspector surfaces.
struct ShadowOtherModes {
  uint8_t cycle_type;  // 0=1cyc 1=2cyc 2=copy 3=fill
  uint8_t persp_tex_en;
  uint8_t detail_tex_en;
  uint8_t sharpen_tex_en;
  uint8_t tex_lod_en;
  uint8_t en_tlut;
  uint8_t tlut_type;
  uint8_t sample_type;
  uint8_t mid_texel;
  uint8_t bi_lerp0, bi_lerp1;
  uint8_t convert_one;
  uint8_t key_en;
  uint8_t rgb_dither_sel;
  uint8_t alpha_dither_sel;
  uint8_t blend_m1a_0, blend_m1a_1, blend_m1b_0, blend_m1b_1;
  uint8_t blend_m2a_0, blend_m2a_1, blend_m2b_0, blend_m2b_1;
  uint8_t force_blend;
  uint8_t alpha_cvg_select;
  uint8_t cvg_times_alpha;
  uint8_t z_mode;
  uint8_t cvg_dest;
  uint8_t color_on_cvg;
  uint8_t image_read_en;
  uint8_t z_update_en;
  uint8_t z_compare_en;
  uint8_t antialias_en;
  uint8_t z_source_sel;
  uint8_t dither_alpha_en;
  uint8_t alpha_compare_en;
};

// One of eight tile descriptors (SET_TILE 0x35 + SET_TILE_SIZE 0x32).
struct ShadowTile {
  uint8_t format;   // 0..7
  uint8_t size;     // 0..3 (4/8/16/32 bit)
  uint16_t line;    // 9-bit
  uint16_t tmem;    // 9-bit tmem word addr
  uint8_t palette;  // 4-bit (CI4 palette select)
  uint8_t ct, mt;   // clamp/mirror T
  uint8_t mask_t;
  uint8_t shift_t;
  uint8_t cs, ms;  // clamp/mirror S
  uint8_t mask_s;
  uint8_t shift_s;
  uint16_t sl, tl, sh, th;  // 12-bit each (SET_TILE_SIZE / LOAD_TILE)
};

// RGBA32 color (SET_FILL/FOG/BLEND/PRIM/ENV_COLOR). fill stores the raw word
// too.
struct ShadowColor {
  uint8_t r, g, b, a;
};

// SET_SCISSOR (0x2d).
struct ShadowScissor {
  uint16_t xh, yh, xl, yl;  // 12-bit fixed (1/4 subpixel) bounds
  uint8_t field;            // scfield
  uint8_t keep_odd;         // sckeepodd
};

// SET_COLOR_IMAGE (0x3f) / SET_TEXTURE_IMAGE (0x3d) decoded fields.
struct ShadowImage {
  uint8_t format;
  uint8_t size;
  uint16_t width;  // +1 already applied
  uint32_t addr;   // 24-bit
};

// SET_PRIM_DEPTH (0x2e) decoded fields.
struct ShadowPrimDepth {
  uint16_t z;  // top 15 bits
  uint16_t delta_z;
};

// SET_PRIM_COLOR (0x3a) non-RGBA fields (the RGBA uses shadow_decode_rgba).
struct ShadowPrimMeta {
  uint8_t min_level;
  uint8_t lod_frac;
};

struct ShadowState {
  // SET_COLOR_IMAGE (0x3f)
  uint8_t color_format;
  uint8_t color_size;
  uint16_t color_width;  // +1 already applied
  uint32_t color_addr;   // 24-bit

  // SET_TEXTURE_IMAGE (0x3d)
  uint8_t tex_format;
  uint8_t tex_size;
  uint16_t tex_width;  // +1 already applied
  uint32_t tex_addr;   // 24-bit

  // SET_MASK_IMAGE (0x3e) — depth/Z buffer address
  uint32_t depth_addr;  // 24-bit

  // SET_PRIM_DEPTH (0x2e)
  uint16_t prim_z;  // top 15 bits, as written (<<16 masked)
  uint16_t prim_delta_z;

  struct ShadowOtherModes other_modes;
  struct ShadowCombine combine;

  struct ShadowTile tile[8];

  uint32_t fill_color_raw;  // SET_FILL_COLOR stores the raw 32-bit word
  struct ShadowColor fog_color;
  struct ShadowColor blend_color;
  struct ShadowColor prim_color;
  struct ShadowColor env_color;
  uint8_t prim_min_level;  // SET_PRIM_COLOR args[0] high bits
  uint8_t prim_lod_frac;

  struct ShadowScissor scissor;

  // Bookkeeping: which commands have been observed (one bit per 6-bit opcode).
  // Lets a panel grey-out fields never set by the current stream.
  uint64_t seen_opcodes;

  uint32_t cmd_count;  // total commands applied
};

// --- Pure field decoders: one source of truth for the RDP bit layouts -------
// Each decodes one command's raw words into a POD. shadow_apply() stores the
// result into the ShadowState; the viewer's command decoder (cmd_decode.cc)
// formats the same POD for display — so a bitfield spec lives in exactly one
// place. Field layouts match the renderer's rdp_set_* handlers bit-for-bit.
void shadow_decode_rgba(uint32_t w1, struct ShadowColor *c);
void shadow_decode_image(uint32_t w0, uint32_t w1, struct ShadowImage *img);
void shadow_decode_prim_depth(uint32_t w1, struct ShadowPrimDepth *pd);
void shadow_decode_prim_meta(uint32_t w0, struct ShadowPrimMeta *m);
void shadow_decode_other_modes(uint32_t w0, uint32_t w1,
                               struct ShadowOtherModes *o);
void shadow_decode_combine(uint32_t w0, uint32_t w1, struct ShadowCombine *c);
void shadow_decode_set_tile(uint32_t w0, uint32_t w1, struct ShadowTile *t);
void shadow_decode_tile_coords(uint32_t w0, uint32_t w1, struct ShadowTile *t);
void shadow_decode_scissor(uint32_t w0, uint32_t w1, struct ShadowScissor *sc);

// Reset all shadow fields to zero (matches a fresh renderer's pre-init state
// for inspector purposes; the renderer's own defaults are not mirrored — only
// what the stream sets).
void shadow_reset(struct ShadowState *s);

// Apply one RDP command (its raw words) to the shadow. `words` points at `n`
// uint32 words (n>=2). Unknown / non-state-mutating opcodes are ignored safely
// (still counted, opcode marked seen). Never reads past n words; never crashes.
void shadow_apply(struct ShadowState *s, const uint32_t *words, uint32_t n);

#ifdef __cplusplus
}  // extern "C"
#endif
