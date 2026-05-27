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
void shadow_decode_rgba(uint32_t w1, struct ShadowColor *c) {
  c->r = (uint8_t)((w1 >> 24) & 0xff);
  c->g = (uint8_t)((w1 >> 16) & 0xff);
  c->b = (uint8_t)((w1 >> 8) & 0xff);
  c->a = (uint8_t)(w1 & 0xff);
}

// SET_COLOR_IMAGE / SET_TEXTURE_IMAGE — fbuffer.c / tex.c. width stores -1.
void shadow_decode_image(uint32_t w0, uint32_t w1, struct ShadowImage *img) {
  img->format = (uint8_t)((w0 >> 21) & 0x7);
  img->size = (uint8_t)((w0 >> 19) & 0x3);
  img->width = (uint16_t)((w0 & 0x3ff) + 1);
  img->addr = w1 & 0x0ffffff;
}

// SET_PRIM_DEPTH — rasterizer.c.
void shadow_decode_prim_depth(uint32_t w1, struct ShadowPrimDepth *pd) {
  pd->z = (uint16_t)((w1 >> 16) & 0x7fff);
  pd->delta_z = (uint16_t)w1;
}

// SET_PRIM_COLOR non-RGBA args (minlod / lodfrac) — combiner.c.
void shadow_decode_prim_meta(uint32_t w0, struct ShadowPrimMeta *m) {
  m->min_level = (uint8_t)((w0 >> 8) & 0x1f);
  m->lod_frac = (uint8_t)(w0 & 0xff);
}

void shadow_decode_other_modes(uint32_t w0, uint32_t w1,
                               struct ShadowOtherModes *o) {
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

void shadow_decode_combine(uint32_t w0, uint32_t w1, struct ShadowCombine *c) {
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

// SET_TILE descriptor fields (NOT sl/tl/sh/th — those come from the tile-coords
// commands). Caller selects the tile slot via (w1>>24)&7.
void shadow_decode_set_tile(uint32_t w0, uint32_t w1, struct ShadowTile *t) {
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
// (rdp_set_tile_size + tile_tlut_common_cs_decoder). Caller selects the tile
// slot via (w1>>24)&7.
void shadow_decode_tile_coords(uint32_t w0, uint32_t w1, struct ShadowTile *t) {
  t->sl = (uint16_t)((w0 >> 12) & 0xfff);
  t->tl = (uint16_t)(w0 & 0xfff);
  t->sh = (uint16_t)((w1 >> 12) & 0xfff);
  t->th = (uint16_t)(w1 & 0xfff);
}

// SET_SCISSOR — rasterizer.c.
void shadow_decode_scissor(uint32_t w0, uint32_t w1, struct ShadowScissor *sc) {
  sc->xh = (uint16_t)((w0 >> 12) & 0xfff);
  sc->yh = (uint16_t)(w0 & 0xfff);
  sc->xl = (uint16_t)((w1 >> 12) & 0xfff);
  sc->yl = (uint16_t)(w1 & 0xfff);
  sc->field = (uint8_t)((w1 >> 25) & 1);
  sc->keep_odd = (uint8_t)((w1 >> 24) & 1);
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
    case RDPCMD_SET_COLOR_IMAGE: {
      struct ShadowImage img;
      shadow_decode_image(w0, w1, &img);
      s->color_format = img.format;
      s->color_size = img.size;
      s->color_width = img.width;
      s->color_addr = img.addr;
      break;
    }
    case RDPCMD_SET_TEXTURE_IMAGE: {
      struct ShadowImage img;
      shadow_decode_image(w0, w1, &img);
      s->tex_format = img.format;
      s->tex_size = img.size;
      s->tex_width = img.width;
      s->tex_addr = img.addr;
      break;
    }
    case RDPCMD_SET_MASK_IMAGE:
      s->depth_addr = w1 & 0x0ffffff;
      break;
    case RDPCMD_SET_PRIM_DEPTH: {
      struct ShadowPrimDepth pd;
      shadow_decode_prim_depth(w1, &pd);
      s->prim_z = pd.z;
      s->prim_delta_z = pd.delta_z;
      break;
    }
    case RDPCMD_SET_OTHER_MODES:
      shadow_decode_other_modes(w0, w1, &s->other_modes);
      break;
    case RDPCMD_SET_COMBINE:
      shadow_decode_combine(w0, w1, &s->combine);
      break;
    case RDPCMD_SET_TILE:
      shadow_decode_set_tile(w0, w1, &s->tile[(w1 >> 24) & 0x7]);
      break;
    case RDPCMD_SET_TILE_SIZE:
    case RDPCMD_LOAD_TILE:
    case RDPCMD_LOAD_TLUT:
      shadow_decode_tile_coords(w0, w1, &s->tile[(w1 >> 24) & 0x7]);
      break;
    case RDPCMD_SET_FILL_COLOR:
      s->fill_color_raw = w1;
      break;
    case RDPCMD_SET_FOG_COLOR:
      shadow_decode_rgba(w1, &s->fog_color);
      break;
    case RDPCMD_SET_BLEND_COLOR:
      shadow_decode_rgba(w1, &s->blend_color);
      break;
    case RDPCMD_SET_PRIM_COLOR: {
      struct ShadowPrimMeta m;
      shadow_decode_prim_meta(w0, &m);
      s->prim_min_level = m.min_level;
      s->prim_lod_frac = m.lod_frac;
      shadow_decode_rgba(w1, &s->prim_color);
      break;
    }
    case RDPCMD_SET_ENV_COLOR:
      shadow_decode_rgba(w1, &s->env_color);
      break;
    case RDPCMD_SET_SCISSOR:
      shadow_decode_scissor(w0, w1, &s->scissor);
      break;
    default:
      // No-op for NO_OP, syncs, draws, key/convert, load_block, and unknown
      // opcodes — none mutate the shadowed settable state. Still counted/seen.
      break;
  }
}
