// Stream C.2 — RDP command decoder (impl). See cmd_decode.h.
//
// Word counts come from src/rdp/rdp/rdp.c rdp_commands[] (length in bytes / 4).
// Field decodes mirror the rdp_set_* handlers (see shadow_state.cc header
// note).

#include "cmd_decode.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "shadow_state.h"  // pure shadow_decode_* (shared RDP bitfield specs)

// Length table in WORDS, indexed by 6-bit opcode. Mirrors rdp_commands[].length
// (bytes) / 4. 0 marks an opcode the renderer treats as invalid (8-byte /
// 2-word stub) — we still return 2 so the stream advances. Triangles are the
// big ones.
static const uint8_t WORD_COUNT[64] = {
    /*0x00*/ 2, 2,  2,  2,  2,  2,  2,  2,
    /*0x08*/ 8, 12, 24, 28, 24, 28, 40, 44,
    /*0x10*/ 2, 2,  2,  2,  2,  2,  2,  2,
    /*0x18*/ 2, 2,  2,  2,  2,  2,  2,  2,
    /*0x20*/ 2, 2,  2,  2,  4,  4,  2,  2,
    /*0x28*/ 2, 2,  2,  2,  2,  2,  2,  2,
    /*0x30*/ 2, 2,  2,  2,  2,  2,  2,  2,
    /*0x38*/ 2, 2,  2,  2,  2,  2,  2,  2,
};

uint32_t rdp_cmd_word_count(uint8_t id) {
  uint8_t const n = WORD_COUNT[id & 0x3f];
  return n < 2 ? 2 : (uint32_t)n;
}

const char *rdp_cmd_name(uint8_t id) {
  switch (id & 0x3f) {
    case RDPCMD_NO_OP:
      return "NO_OP";
    case RDPCMD_FILL_TRIANGLE:
      return "FILL_TRIANGLE";
    case RDPCMD_FILL_ZBUFFER_TRIANGLE:
      return "FILL_ZBUFFER_TRIANGLE";
    case RDPCMD_TEXTURE_TRIANGLE:
      return "TEXTURE_TRIANGLE";
    case RDPCMD_TEXTURE_ZBUFFER_TRIANGLE:
      return "TEXTURE_ZBUFFER_TRIANGLE";
    case RDPCMD_SHADE_TRIANGLE:
      return "SHADE_TRIANGLE";
    case RDPCMD_SHADE_ZBUFFER_TRIANGLE:
      return "SHADE_ZBUFFER_TRIANGLE";
    case RDPCMD_SHADE_TEXTURE_TRIANGLE:
      return "SHADE_TEXTURE_TRIANGLE";
    case RDPCMD_SHADE_TEXTURE_Z_TRIANGLE:
      return "SHADE_TEXTURE_Z_TRIANGLE";
    case RDPCMD_TEXTURE_RECTANGLE:
      return "TEXTURE_RECTANGLE";
    case RDPCMD_TEXTURE_RECTANGLE_FLIP:
      return "TEXTURE_RECTANGLE_FLIP";
    case RDPCMD_SYNC_LOAD:
      return "SYNC_LOAD";
    case RDPCMD_SYNC_PIPE:
      return "SYNC_PIPE";
    case RDPCMD_SYNC_TILE:
      return "SYNC_TILE";
    case RDPCMD_SYNC_FULL:
      return "SYNC_FULL";
    case RDPCMD_SET_KEY_GB:
      return "SET_KEY_GB";
    case RDPCMD_SET_KEY_R:
      return "SET_KEY_R";
    case RDPCMD_SET_CONVERT:
      return "SET_CONVERT";
    case RDPCMD_SET_SCISSOR:
      return "SET_SCISSOR";
    case RDPCMD_SET_PRIM_DEPTH:
      return "SET_PRIM_DEPTH";
    case RDPCMD_SET_OTHER_MODES:
      return "SET_OTHER_MODES";
    case RDPCMD_LOAD_TLUT:
      return "LOAD_TLUT";
    case RDPCMD_SET_TILE_SIZE:
      return "SET_TILE_SIZE";
    case RDPCMD_LOAD_BLOCK:
      return "LOAD_BLOCK";
    case RDPCMD_LOAD_TILE:
      return "LOAD_TILE";
    case RDPCMD_SET_TILE:
      return "SET_TILE";
    case RDPCMD_FILL_RECTANGLE:
      return "FILL_RECTANGLE";
    case RDPCMD_SET_FILL_COLOR:
      return "SET_FILL_COLOR";
    case RDPCMD_SET_FOG_COLOR:
      return "SET_FOG_COLOR";
    case RDPCMD_SET_BLEND_COLOR:
      return "SET_BLEND_COLOR";
    case RDPCMD_SET_PRIM_COLOR:
      return "SET_PRIM_COLOR";
    case RDPCMD_SET_ENV_COLOR:
      return "SET_ENV_COLOR";
    case RDPCMD_SET_COMBINE:
      return "SET_COMBINE";
    case RDPCMD_SET_TEXTURE_IMAGE:
      return "SET_TEXTURE_IMAGE";
    case RDPCMD_SET_MASK_IMAGE:
      return "SET_MASK_IMAGE";
    case RDPCMD_SET_COLOR_IMAGE:
      return "SET_COLOR_IMAGE";
    default:
      return "UNKNOWN";
  }
}

static int is_triangle_id(uint8_t id) {
  return id >= RDPCMD_FILL_TRIANGLE && id <= RDPCMD_SHADE_TEXTURE_Z_TRIANGLE;
}

static const char *fb_format_name(uint8_t f) {
  switch (f) {
    case 0:
      return "RGBA";
    case 1:
      return "YUV";
    case 2:
      return "CI";
    case 3:
      return "IA";
    case 4:
      return "I";
    default:
      return "?";
  }
}

static const char *px_size_name(uint8_t s) {
  switch (s) {
    case 0:
      return "4b";
    case 1:
      return "8b";
    case 2:
      return "16b";
    case 3:
      return "32b";
    default:
      return "?";
  }
}

static const char *cycle_type_name(uint8_t c) {
  switch (c) {
    case 0:
      return "1CYCLE";
    case 1:
      return "2CYCLE";
    case 2:
      return "COPY";
    case 3:
      return "FILL";
    default:
      return "?";
  }
}

// Fill `out->summary` with the decoded fields. Caller has set id/name/etc.
static void decode_summary(uint8_t id, uint32_t w0, uint32_t w1, uint32_t avail,
                           struct CmdRecord *out) {
  char *b = out->summary;
  size_t const cap = sizeof out->summary;
  switch (id) {
    case RDPCMD_SET_COLOR_IMAGE:
    case RDPCMD_SET_TEXTURE_IMAGE: {
      struct ShadowImage img;
      shadow_decode_image(w0, w1, &img);
      snprintf(b, cap, "%s %s w=%u addr=0x%06x", fb_format_name(img.format),
               px_size_name(img.size), (unsigned)img.width,
               (unsigned)img.addr);
      break;
    }
    case RDPCMD_SET_MASK_IMAGE:
      snprintf(b, cap, "z/depth addr=0x%06x", (unsigned)(w1 & 0x0ffffff));
      break;
    case RDPCMD_SET_FILL_COLOR:
      snprintf(b, cap, "raw=0x%08x", (unsigned)w1);
      break;
    case RDPCMD_SET_FOG_COLOR:
    case RDPCMD_SET_BLEND_COLOR:
    case RDPCMD_SET_ENV_COLOR: {
      struct ShadowColor c;
      shadow_decode_rgba(w1, &c);
      snprintf(b, cap, "rgba=%u,%u,%u,%u", (unsigned)c.r, (unsigned)c.g,
               (unsigned)c.b, (unsigned)c.a);
      break;
    }
    case RDPCMD_SET_PRIM_COLOR: {
      struct ShadowColor c;
      struct ShadowPrimMeta m;
      shadow_decode_rgba(w1, &c);
      shadow_decode_prim_meta(w0, &m);
      snprintf(b, cap, "rgba=%u,%u,%u,%u minlod=%u lodfrac=%u", (unsigned)c.r,
               (unsigned)c.g, (unsigned)c.b, (unsigned)c.a,
               (unsigned)m.min_level, (unsigned)m.lod_frac);
      break;
    }
    case RDPCMD_SET_OTHER_MODES: {
      struct ShadowOtherModes o;
      shadow_decode_other_modes(w0, w1, &o);
      snprintf(b, cap,
               "cyc=%s persp=%u TLUT=%u zcmp=%u zupd=%u zmode=%u aa=%u "
               "acmp=%u blend=%u",
               cycle_type_name(o.cycle_type), (unsigned)o.persp_tex_en,
               (unsigned)o.en_tlut, (unsigned)o.z_compare_en,
               (unsigned)o.z_update_en, (unsigned)o.z_mode,
               (unsigned)o.antialias_en, (unsigned)o.alpha_compare_en,
               (unsigned)o.force_blend);
      break;
    }
    case RDPCMD_SET_COMBINE: {
      struct ShadowCombine c;
      shadow_decode_combine(w0, w1, &c);
      snprintf(b, cap,
               "c0 rgb(%u,%u,%u,%u) a(%u,%u,%u,%u) | c1 rgb(%u,%u,%u,%u)",
               (unsigned)c.sub_a_rgb0, (unsigned)c.sub_b_rgb0,
               (unsigned)c.mul_rgb0, (unsigned)c.add_rgb0, (unsigned)c.sub_a_a0,
               (unsigned)c.sub_b_a0, (unsigned)c.mul_a0, (unsigned)c.add_a0,
               (unsigned)c.sub_a_rgb1, (unsigned)c.sub_b_rgb1,
               (unsigned)c.mul_rgb1, (unsigned)c.add_rgb1);
      break;
    }
    case RDPCMD_SET_TILE: {
      struct ShadowTile t;
      shadow_decode_set_tile(w0, w1, &t);
      snprintf(b, cap,
               "tile=%u %s %s line=%u tmem=0x%03x pal=%u cs=%u ct=%u "
               "masks=%u maskt=%u",
               (unsigned)((w1 >> 24) & 7), fb_format_name(t.format),
               px_size_name(t.size), (unsigned)t.line, (unsigned)t.tmem,
               (unsigned)t.palette, (unsigned)t.cs, (unsigned)t.ct,
               (unsigned)t.mask_s, (unsigned)t.mask_t);
      break;
    }
    case RDPCMD_SET_TILE_SIZE:
    case RDPCMD_LOAD_TILE:
    case RDPCMD_LOAD_TLUT: {
      struct ShadowTile t;
      shadow_decode_tile_coords(w0, w1, &t);
      snprintf(b, cap, "tile=%u sl=%u tl=%u sh=%u th=%u",
               (unsigned)((w1 >> 24) & 7), (unsigned)t.sl, (unsigned)t.tl,
               (unsigned)t.sh, (unsigned)t.th);
      break;
    }
    case RDPCMD_LOAD_BLOCK:
      snprintf(b, cap, "tile=%u sl=%u tl=%u sh=%u dxt=%u",
               (unsigned)((w1 >> 24) & 7), (unsigned)((w0 >> 12) & 0xfff),
               (unsigned)(w0 & 0xfff), (unsigned)((w1 >> 12) & 0xfff),
               (unsigned)(w1 & 0xfff));
      break;
    case RDPCMD_SET_SCISSOR: {
      struct ShadowScissor sc;
      shadow_decode_scissor(w0, w1, &sc);
      snprintf(b, cap, "xh=%u yh=%u xl=%u yl=%u field=%u odd=%u",
               (unsigned)sc.xh, (unsigned)sc.yh, (unsigned)sc.xl,
               (unsigned)sc.yl, (unsigned)sc.field, (unsigned)sc.keep_odd);
      break;
    }
    case RDPCMD_SET_PRIM_DEPTH: {
      struct ShadowPrimDepth pd;
      shadow_decode_prim_depth(w1, &pd);
      snprintf(b, cap, "z=%u dz=%u", (unsigned)pd.z, (unsigned)pd.delta_z);
      break;
    }
    case RDPCMD_FILL_RECTANGLE:
    case RDPCMD_TEXTURE_RECTANGLE:
    case RDPCMD_TEXTURE_RECTANGLE_FLIP:
      snprintf(b, cap, "xl=%u yl=%u xh=%u yh=%u tile=%u",
               (unsigned)((w0 >> 12) & 0xfff), (unsigned)(w0 & 0xfff),
               (unsigned)((w1 >> 12) & 0xfff), (unsigned)(w1 & 0xfff),
               (unsigned)((w1 >> 24) & 7));
      break;
    case RDPCMD_NO_OP:
    case RDPCMD_SYNC_LOAD:
    case RDPCMD_SYNC_PIPE:
    case RDPCMD_SYNC_TILE:
    case RDPCMD_SYNC_FULL:
      b[0] = '\0';
      break;
    default:
      if (is_triangle_id(id)) {
        // Common edge header (rasterizer.c): dir/flip, max_level, tile,
        // yl/ym/yh.
        snprintf(b, cap, "dir=%u lvl=%u tile=%u yl=%d ym=%d yh=%d",
                 (unsigned)((w0 >> 23) & 1), (unsigned)((w0 >> 19) & 7),
                 (unsigned)((w0 >> 16) & 7), (int)(w0 & 0x3fff),
                 (int)((w1 >> 16) & 0x3fff), (int)(w1 & 0x3fff));
      } else {
        // Raw-hex fallback for unknown / unhandled opcodes.
        snprintf(b, cap, "raw 0x%08x 0x%08x", (unsigned)w0, (unsigned)w1);
      }
      break;
  }
  // Note truncation if the stream ran out mid-command.
  uint32_t const need = rdp_cmd_word_count(id);
  if (avail < need) {
    size_t const len = strlen(b);
    if (len + 16 < cap) {
      snprintf(b + len, cap - len, " [trunc %u/%u]", (unsigned)avail,
               (unsigned)need);
    }
  }
}

uint32_t cmd_decode(const uint32_t *words, uint32_t avail,
                    struct CmdRecord *out) {
  if (!out) {
    return 2;
  }
  memset(out, 0, sizeof *out);
  if (!words || avail < 1) {
    out->id = 0;
    out->known = 0;
    out->word_count = 2;
    out->name = "UNKNOWN";
    snprintf(out->summary, sizeof out->summary, "no data");
    return 2;
  }

  uint32_t const w0 = words[0];
  uint32_t const w1 = avail >= 2 ? words[1] : 0;
  uint8_t const id = (uint8_t)((w0 >> 24) & 0x3f);

  out->id = id;
  out->name = rdp_cmd_name(id);
  out->word_count = rdp_cmd_word_count(id);
  out->is_triangle = (uint8_t)is_triangle_id(id);
  out->known = (uint8_t)(strcmp(out->name, "UNKNOWN") != 0);

  decode_summary(id, w0, w1, avail, out);
  return out->word_count;
}
