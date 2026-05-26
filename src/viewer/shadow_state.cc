// Stream C.2 — viewer-side shadow RDP state (impl). See shadow_state.h.
//
// Each apply path mirrors the matching rdp_set_* handler in src/rdp/rdp/*.c
// bit-for-bit (only the *settable* fields; the renderer's derived/LUT state is
// not shadowed). Verified against:
//   fbuffer.c  rdp_set_color_image / rdp_set_fill_color
//   tex.c      rdp_set_texture_image / rdp_set_tile / rdp_set_tile_size /
//              rdp_load_tile / rdp_load_tlut (tile_tlut_common_cs_decoder)
//   zbuffer.c  rdp_set_mask_image
//   combiner.c rdp_set_prim_color / rdp_set_env_color / rdp_set_combine
//   blender.c  rdp_set_fog_color / rdp_set_blend_color
//   rasterizer.c rdp_set_scissor / rdp_set_prim_depth
//   rdp.c      rdp_set_other_modes

#include "shadow_state.h"

#include <string.h>

#include "cmd_decode.h"

void shadow_reset(struct ShadowState *s) {
  if (!s) {
    return;
  }
  memset(s, 0, sizeof *s);
}

// RGBA32 unpack — matches RGBA32_R/G/B/A in n64video.c.
static void shadow_unpack_rgba(uint32_t w, struct ShadowColor *c) {
  c->r = (uint8_t)((w >> 24) & 0xff);
  c->g = (uint8_t)((w >> 16) & 0xff);
  c->b = (uint8_t)((w >> 8) & 0xff);
  c->a = (uint8_t)(w & 0xff);
}

static void shadow_apply_other_modes(struct ShadowState *s, uint32_t w0,
                                     uint32_t w1) {
  struct ShadowOtherModes *o = &s->other_modes;
  o->cycle_type = (uint8_t)((w0 >> 20) & 3);
  o->persp_tex_en = (uint8_t)((w0 >> 19) & 1);
  o->detail_tex_en = (uint8_t)((w0 >> 18) & 1);
  o->sharpen_tex_en = (uint8_t)((w0 >> 17) & 1);
  o->tex_lod_en = (uint8_t)((w0 >> 16) & 1);
  o->en_tlut = (uint8_t)((w0 >> 15) & 1);
  o->tlut_type = (uint8_t)((w0 >> 14) & 1);
  o->sample_type = (uint8_t)((w0 >> 13) & 1);
  o->mid_texel = (uint8_t)((w0 >> 12) & 1);
  o->bi_lerp0 = (uint8_t)((w0 >> 11) & 1);
  o->bi_lerp1 = (uint8_t)((w0 >> 10) & 1);
  o->convert_one = (uint8_t)((w0 >> 9) & 1);
  o->key_en = (uint8_t)((w0 >> 8) & 1);
  o->rgb_dither_sel = (uint8_t)((w0 >> 6) & 3);
  o->alpha_dither_sel = (uint8_t)((w0 >> 4) & 3);
  o->blend_m1a_0 = (uint8_t)((w1 >> 30) & 3);
  o->blend_m1a_1 = (uint8_t)((w1 >> 28) & 3);
  o->blend_m1b_0 = (uint8_t)((w1 >> 26) & 3);
  o->blend_m1b_1 = (uint8_t)((w1 >> 24) & 3);
  o->blend_m2a_0 = (uint8_t)((w1 >> 22) & 3);
  o->blend_m2a_1 = (uint8_t)((w1 >> 20) & 3);
  o->blend_m2b_0 = (uint8_t)((w1 >> 18) & 3);
  o->blend_m2b_1 = (uint8_t)((w1 >> 16) & 3);
  o->force_blend = (uint8_t)((w1 >> 14) & 1);
  o->alpha_cvg_select = (uint8_t)((w1 >> 13) & 1);
  o->cvg_times_alpha = (uint8_t)((w1 >> 12) & 1);
  o->z_mode = (uint8_t)((w1 >> 10) & 3);
  o->cvg_dest = (uint8_t)((w1 >> 8) & 3);
  o->color_on_cvg = (uint8_t)((w1 >> 7) & 1);
  o->image_read_en = (uint8_t)((w1 >> 6) & 1);
  o->z_update_en = (uint8_t)((w1 >> 5) & 1);
  o->z_compare_en = (uint8_t)((w1 >> 4) & 1);
  o->antialias_en = (uint8_t)((w1 >> 3) & 1);
  o->z_source_sel = (uint8_t)((w1 >> 2) & 1);
  o->dither_alpha_en = (uint8_t)((w1 >> 1) & 1);
  o->alpha_compare_en = (uint8_t)(w1 & 1);
}

static void shadow_apply_combine(struct ShadowState *s, uint32_t w0,
                                 uint32_t w1) {
  struct ShadowCombine *c = &s->combine;
  c->sub_a_rgb0 = (uint8_t)((w0 >> 20) & 0xf);
  c->mul_rgb0 = (uint8_t)((w0 >> 15) & 0x1f);
  c->sub_a_a0 = (uint8_t)((w0 >> 12) & 0x7);
  c->mul_a0 = (uint8_t)((w0 >> 9) & 0x7);
  c->sub_a_rgb1 = (uint8_t)((w0 >> 5) & 0xf);
  c->mul_rgb1 = (uint8_t)(w0 & 0x1f);

  c->sub_b_rgb0 = (uint8_t)((w1 >> 28) & 0xf);
  c->sub_b_rgb1 = (uint8_t)((w1 >> 24) & 0xf);
  c->sub_a_a1 = (uint8_t)((w1 >> 21) & 0x7);
  c->mul_a1 = (uint8_t)((w1 >> 18) & 0x7);
  c->add_rgb0 = (uint8_t)((w1 >> 15) & 0x7);
  c->sub_b_a0 = (uint8_t)((w1 >> 12) & 0x7);
  c->add_a0 = (uint8_t)((w1 >> 9) & 0x7);
  c->add_rgb1 = (uint8_t)((w1 >> 6) & 0x7);
  c->sub_b_a1 = (uint8_t)((w1 >> 3) & 0x7);
  c->add_a1 = (uint8_t)(w1 & 0x7);
}

static void shadow_apply_set_tile(struct ShadowState *s, uint32_t w0,
                                  uint32_t w1) {
  uint32_t const n = (w1 >> 24) & 0x7;
  struct ShadowTile *t = &s->tile[n];
  t->format = (uint8_t)((w0 >> 21) & 0x7);
  t->size = (uint8_t)((w0 >> 19) & 0x3);
  t->line = (uint16_t)((w0 >> 9) & 0x1ff);
  t->tmem = (uint16_t)(w0 & 0x1ff);
  t->palette = (uint8_t)((w1 >> 20) & 0xf);
  t->ct = (uint8_t)((w1 >> 19) & 0x1);
  t->mt = (uint8_t)((w1 >> 18) & 0x1);
  t->mask_t = (uint8_t)((w1 >> 14) & 0xf);
  t->shift_t = (uint8_t)((w1 >> 10) & 0xf);
  t->cs = (uint8_t)((w1 >> 9) & 0x1);
  t->ms = (uint8_t)((w1 >> 8) & 0x1);
  t->mask_s = (uint8_t)((w1 >> 4) & 0xf);
  t->shift_s = (uint8_t)(w1 & 0xf);
}

// SET_TILE_SIZE / LOAD_TILE / LOAD_TLUT all write sl/tl/sh/th identically
// (rdp_set_tile_size + tile_tlut_common_cs_decoder).
static void shadow_apply_tile_coords(struct ShadowState *s, uint32_t w0,
                                     uint32_t w1) {
  uint32_t const n = (w1 >> 24) & 0x7;
  struct ShadowTile *t = &s->tile[n];
  t->sl = (uint16_t)((w0 >> 12) & 0xfff);
  t->tl = (uint16_t)(w0 & 0xfff);
  t->sh = (uint16_t)((w1 >> 12) & 0xfff);
  t->th = (uint16_t)(w1 & 0xfff);
}

void shadow_apply(struct ShadowState *s, const uint32_t *words, uint32_t n) {
  if (!s || !words || n < 2) {
    return;
  }
  uint32_t const w0 = words[0];
  uint32_t const w1 = words[1];
  uint8_t const id = (uint8_t)((w0 >> 24) & 0x3f);

  s->seen_opcodes |= ((uint64_t)1 << id);
  s->cmd_count++;

  switch (id) {
    case RDPCMD_SET_COLOR_IMAGE:
      s->color_format = (uint8_t)((w0 >> 21) & 0x7);
      s->color_size = (uint8_t)((w0 >> 19) & 0x3);
      s->color_width = (uint16_t)((w0 & 0x3ff) + 1);
      s->color_addr = w1 & 0x0ffffff;
      break;
    case RDPCMD_SET_TEXTURE_IMAGE:
      s->tex_format = (uint8_t)((w0 >> 21) & 0x7);
      s->tex_size = (uint8_t)((w0 >> 19) & 0x3);
      s->tex_width = (uint16_t)((w0 & 0x3ff) + 1);
      s->tex_addr = w1 & 0x0ffffff;
      break;
    case RDPCMD_SET_MASK_IMAGE:
      s->depth_addr = w1 & 0x0ffffff;
      break;
    case RDPCMD_SET_PRIM_DEPTH:
      s->prim_z = (uint16_t)((w1 >> 16) & 0x7fff);
      s->prim_delta_z = (uint16_t)w1;
      break;
    case RDPCMD_SET_OTHER_MODES:
      shadow_apply_other_modes(s, w0, w1);
      break;
    case RDPCMD_SET_COMBINE:
      shadow_apply_combine(s, w0, w1);
      break;
    case RDPCMD_SET_TILE:
      shadow_apply_set_tile(s, w0, w1);
      break;
    case RDPCMD_SET_TILE_SIZE:
    case RDPCMD_LOAD_TILE:
    case RDPCMD_LOAD_TLUT:
      shadow_apply_tile_coords(s, w0, w1);
      break;
    case RDPCMD_SET_FILL_COLOR:
      s->fill_color_raw = w1;
      break;
    case RDPCMD_SET_FOG_COLOR:
      shadow_unpack_rgba(w1, &s->fog_color);
      break;
    case RDPCMD_SET_BLEND_COLOR:
      shadow_unpack_rgba(w1, &s->blend_color);
      break;
    case RDPCMD_SET_PRIM_COLOR:
      s->prim_min_level = (uint8_t)((w0 >> 8) & 0x1f);
      s->prim_lod_frac = (uint8_t)(w0 & 0xff);
      shadow_unpack_rgba(w1, &s->prim_color);
      break;
    case RDPCMD_SET_ENV_COLOR:
      shadow_unpack_rgba(w1, &s->env_color);
      break;
    case RDPCMD_SET_SCISSOR:
      s->scissor.xh = (uint16_t)((w0 >> 12) & 0xfff);
      s->scissor.yh = (uint16_t)(w0 & 0xfff);
      s->scissor.xl = (uint16_t)((w1 >> 12) & 0xfff);
      s->scissor.yl = (uint16_t)(w1 & 0xfff);
      s->scissor.field = (uint8_t)((w1 >> 25) & 1);
      s->scissor.keep_odd = (uint8_t)((w1 >> 24) & 1);
      break;
    default:
      // No-op for NO_OP, syncs, draws, key/convert, load_block, and unknown
      // opcodes — none mutate the shadowed settable state. Still counted/seen.
      break;
  }
}
