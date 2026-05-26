#ifdef N64VIDEO_C

// rdp.c: RDP command dispatcher + rdp_state + state-setting handlers.
//
// Ported from the CANONICAL Angrylion fork that parallel-rdp embeds:
//   parallel-rdp/angrylion-rdp-plus @ 31bdb1f
//   (src/core/n64video/rdp.c)
//
// Adapted for our single-TU build: this file is #include'd into n64video.c
// under N64VIDEO_C (mirroring the fork, whose n64video.c includes
// n64video/rdp.c which includes rdp/*.c). The MIN/MAX/SIGN/RGBA macros, irand
// and clamp that the fork keeps in n64video.c are provided by n64video.c BEFORE
// this include.
//
// SYMBOL HYGIENE (CRITICAL): the conformance executable links this lib AND the
// Angrylion oracle, which keeps the ORIGINAL C names (state, rdp_cmd, rdp_init,
// rdp_*, get_tmem, z_init_lut, ...). To avoid duplicate-symbol collisions,
// every such symbol is given INTERNAL LINKAGE here. In C++, a name whose FIRST
// declaration is `static` keeps internal linkage for all later declarations, so
// we forward-declare the handlers / init funcs / get_tmem `static` below; the
// stage files' `void rdp_*(...)` definitions then inherit internal linkage. The
// `rdpxi_state[]` array and the command table are declared static directly. The
// only external symbols this TU exposes stay rdpx_*-prefixed (see n64video.c).

#include "rdp_internal.h"

struct RdpState rdpxi_state[RDPX_PARALLEL_MAX_WORKERS];

static int32_t one_color = 0x100;
static int32_t zero_color = 0x00;

// Forward declarations — all `static` so the stage-file definitions inherit
// internal linkage (symbol hygiene vs the oracle's identically-named globals).
static void rdp_init(uint32_t wid, uint32_t num_workers);
static void rdp_invalid(uint32_t wid, const uint32_t* args);
static void rdp_noop(uint32_t wid, const uint32_t* args);
static void rdp_tri_noshade(uint32_t wid, const uint32_t* args);
static void rdp_tri_noshade_z(uint32_t wid, const uint32_t* args);
static void rdp_tri_tex(uint32_t wid, const uint32_t* args);
static void rdp_tri_tex_z(uint32_t wid, const uint32_t* args);
static void rdp_tri_shade(uint32_t wid, const uint32_t* args);
static void rdp_tri_shade_z(uint32_t wid, const uint32_t* args);
static void rdp_tri_texshade(uint32_t wid, const uint32_t* args);
static void rdp_tri_texshade_z(uint32_t wid, const uint32_t* args);
static void rdp_tex_rect(uint32_t wid, const uint32_t* args);
static void rdp_tex_rect_flip(uint32_t wid, const uint32_t* args);
static void rdp_sync_load(uint32_t wid, const uint32_t* args);
static void rdp_sync_pipe(uint32_t wid, const uint32_t* args);
static void rdp_sync_tile(uint32_t wid, const uint32_t* args);
static void rdp_sync_full(uint32_t wid, const uint32_t* args);
static void rdp_set_key_gb(uint32_t wid, const uint32_t* args);
static void rdp_set_key_r(uint32_t wid, const uint32_t* args);
static void rdp_set_convert(uint32_t wid, const uint32_t* args);
static void rdp_set_scissor(uint32_t wid, const uint32_t* args);
static void rdp_set_prim_depth(uint32_t wid, const uint32_t* args);
static void rdp_set_other_modes(uint32_t wid, const uint32_t* args);
static void rdp_set_tile_size(uint32_t wid, const uint32_t* args);
static void rdp_load_block(uint32_t wid, const uint32_t* args);
static void rdp_load_tlut(uint32_t wid, const uint32_t* args);
static void rdp_load_tile(uint32_t wid, const uint32_t* args);
static void rdp_set_tile(uint32_t wid, const uint32_t* args);
static void rdp_fill_rect(uint32_t wid, const uint32_t* args);
static void rdp_set_fill_color(uint32_t wid, const uint32_t* args);
static void rdp_set_fog_color(uint32_t wid, const uint32_t* args);
static void rdp_set_blend_color(uint32_t wid, const uint32_t* args);
static void rdp_set_prim_color(uint32_t wid, const uint32_t* args);
static void rdp_set_env_color(uint32_t wid, const uint32_t* args);
static void rdp_set_combine(uint32_t wid, const uint32_t* args);
static void rdp_set_texture_image(uint32_t wid, const uint32_t* args);
static void rdp_set_mask_image(uint32_t wid, const uint32_t* args);
static void rdp_set_color_image(uint32_t wid, const uint32_t* args);
static void rdp_cmd(uint32_t wid, const uint32_t* args);

// init funcs forward-declared static (internal linkage vs oracle's globals).
static uint8_t* get_tmem(void);
static void z_init_lut(void);

// dither (reseed_noise/noise_get_*/rgb_dither/get_dither_noise) is now a
// standalone TU — see rdp/dither_internal.h.

// clang-format off
static const struct {
  // command handler function pointer
  void (*handler)(uint32_t wid, const uint32_t*);
  // command data length in bytes
  uint32_t length;
} RDP_COMMANDS[] = {
    {rdp_noop,                8},
    {rdp_invalid,             8},
    {rdp_invalid,             8},
    {rdp_invalid,             8},
    {rdp_invalid,             8},
    {rdp_invalid,             8},
    {rdp_invalid,             8},
    {rdp_invalid,             8},
    {rdp_tri_noshade,        32},
    {rdp_tri_noshade_z,      48},
    {rdp_tri_tex,            96},
    {rdp_tri_tex_z,         112},
    {rdp_tri_shade,          96},
    {rdp_tri_shade_z,       112},
    {rdp_tri_texshade,      160},
    {rdp_tri_texshade_z,    176},
    {rdp_invalid,             8},
    {rdp_invalid,             8},
    {rdp_invalid,             8},
    {rdp_invalid,             8},
    {rdp_invalid,             8},
    {rdp_invalid,             8},
    {rdp_invalid,             8},
    {rdp_invalid,             8},
    {rdp_invalid,             8},
    {rdp_invalid,             8},
    {rdp_invalid,             8},
    {rdp_invalid,             8},
    {rdp_invalid,             8},
    {rdp_invalid,             8},
    {rdp_invalid,             8},
    {rdp_invalid,             8},
    {rdp_invalid,             8},
    {rdp_invalid,             8},
    {rdp_invalid,             8},
    {rdp_invalid,             8},
    {rdp_tex_rect,           16},
    {rdp_tex_rect_flip,      16},
    {rdp_sync_load,           8},
    {rdp_sync_pipe,           8},
    {rdp_sync_tile,           8},
    {rdp_sync_full,           8},
    {rdp_set_key_gb,          8},
    {rdp_set_key_r,           8},
    {rdp_set_convert,         8},
    {rdp_set_scissor,         8},
    {rdp_set_prim_depth,      8},
    {rdp_set_other_modes,     8},
    {rdp_load_tlut,           8},
    {rdp_invalid,             8},
    {rdp_set_tile_size,       8},
    {rdp_load_block,          8},
    {rdp_load_tile,           8},
    {rdp_set_tile,            8},
    {rdp_fill_rect,           8},
    {rdp_set_fill_color,      8},
    {rdp_set_fog_color,       8},
    {rdp_set_blend_color,     8},
    {rdp_set_prim_color,      8},
    {rdp_set_env_color,       8},
    {rdp_set_combine,         8},
    {rdp_set_texture_image,   8},
    {rdp_set_mask_image,      8},
    {rdp_set_color_image,     8}
};
// clang-format on

static void deduce_derivatives(uint32_t wid);

// Stage files are #included in DEPENDENCY order (matches the canonical fork
// 31bdb1f). The unity build is order-sensitive: rdram defines the RWRITE*/PAIR*
// macros fbuffer uses; dither defines rgb_dither used by blender; tcoord/tmem
// define tc_pipeline_copy/read_tmem_copy used by rasterizer. clang-format must
// NOT alphabetize these (doing so broke the build — undeclared identifiers).
#include "rdp/dither_internal.h"  // dither is now a standalone TU (dither.cc)
#include "rdp/rdram_internal.h"   // rdram is now a standalone TU (rdram.cc)
// clang-format off
#include "rdp/blender.c"
#include "rdp/combiner.c"
#include "rdp/coverage.c"
#include "rdp/zbuffer.c"
#include "rdp/fbuffer.c"
#include "rdp/tmem.c"
#include "rdp/tcoord.c"
#include "rdp/tex.c"
#include "rdp/rasterizer.c"
// clang-format on

static void deduce_derivatives(uint32_t wid) {
  int special_bsel0;
  int special_bsel1;

  rdpxi_state[wid].other_modes.f.partialreject_1cycle =
      (rdpxi_state[wid].blender2b_a[0] == &rdpxi_state[wid].inv_pixel_color.a &&
       rdpxi_state[wid].blender1b_a[0] == &rdpxi_state[wid].pixel_color.a);
  rdpxi_state[wid].other_modes.f.partialreject_2cycle =
      (rdpxi_state[wid].blender2b_a[1] == &rdpxi_state[wid].inv_pixel_color.a &&
       rdpxi_state[wid].blender1b_a[1] == &rdpxi_state[wid].pixel_color.a);

  special_bsel0 =
      (rdpxi_state[wid].blender2b_a[0] == &rdpxi_state[wid].memory_color.a);
  special_bsel1 =
      (rdpxi_state[wid].blender2b_a[1] == &rdpxi_state[wid].memory_color.a);

  rdpxi_state[wid].other_modes.f.realblendershiftersneeded =
      (special_bsel0 &&
       rdpxi_state[wid].other_modes.cycle_type == CYCLE_TYPE_1) ||
      (special_bsel1 &&
       rdpxi_state[wid].other_modes.cycle_type == CYCLE_TYPE_2);
  rdpxi_state[wid].other_modes.f.interpixelblendershiftersneeded =
      (special_bsel0 &&
       rdpxi_state[wid].other_modes.cycle_type == CYCLE_TYPE_2);

  rdpxi_state[wid].other_modes.f.rgb_alpha_dither =
      (rdpxi_state[wid].other_modes.rgb_dither_sel << 2) |
      rdpxi_state[wid].other_modes.alpha_dither_sel;

  rdpxi_state[wid].tcdiv_ptr =
      tcdiv_func[rdpxi_state[wid].other_modes.persp_tex_en];

  int texel1_used_in_cc1 = 0;
  int texel0_used_in_cc1 = 0;
  int texel0_used_in_cc0 = 0;
  int texel1_used_in_cc0 = 0;
  int texels_in_cc0 = 0;
  int texels_in_cc1 = 0;
  int lod_frac_used_in_cc1 = 0;
  int lod_frac_used_in_cc0 = 0;
  int texels_or_lf_used_in_ac0 = 0;
  int texel0_used_in_ac0 = 0;
  int texel1_used_in_ac0 = 0;

  if ((rdpxi_state[wid].combiner_rgbmul_r[1] == &rdpxi_state[wid].lod_frac) ||
      (rdpxi_state[wid].combiner_alphamul[1] == &rdpxi_state[wid].lod_frac)) {
    lod_frac_used_in_cc1 = 1;
  }
  if ((rdpxi_state[wid].combiner_rgbmul_r[0] == &rdpxi_state[wid].lod_frac) ||
      (rdpxi_state[wid].combiner_alphamul[0] == &rdpxi_state[wid].lod_frac)) {
    lod_frac_used_in_cc0 = 1;
  }

  if (rdpxi_state[wid].combiner_rgbmul_r[1] ==
          &rdpxi_state[wid].texel1_color.r ||
      rdpxi_state[wid].combiner_rgbsub_a_r[1] ==
          &rdpxi_state[wid].texel1_color.r ||
      rdpxi_state[wid].combiner_rgbsub_b_r[1] ==
          &rdpxi_state[wid].texel1_color.r ||
      rdpxi_state[wid].combiner_rgbadd_r[1] ==
          &rdpxi_state[wid].texel1_color.r ||
      rdpxi_state[wid].combiner_alphamul[1] ==
          &rdpxi_state[wid].texel1_color.a ||
      rdpxi_state[wid].combiner_alphasub_a[1] ==
          &rdpxi_state[wid].texel1_color.a ||
      rdpxi_state[wid].combiner_alphasub_b[1] ==
          &rdpxi_state[wid].texel1_color.a ||
      rdpxi_state[wid].combiner_alphaadd[1] ==
          &rdpxi_state[wid].texel1_color.a ||
      rdpxi_state[wid].combiner_rgbmul_r[1] ==
          &rdpxi_state[wid].texel1_color.a) {
    texel1_used_in_cc1 = 1;
  }
  if (rdpxi_state[wid].combiner_rgbmul_r[1] ==
          &rdpxi_state[wid].texel0_color.r ||
      rdpxi_state[wid].combiner_rgbsub_a_r[1] ==
          &rdpxi_state[wid].texel0_color.r ||
      rdpxi_state[wid].combiner_rgbsub_b_r[1] ==
          &rdpxi_state[wid].texel0_color.r ||
      rdpxi_state[wid].combiner_rgbadd_r[1] ==
          &rdpxi_state[wid].texel0_color.r ||
      rdpxi_state[wid].combiner_alphamul[1] ==
          &rdpxi_state[wid].texel0_color.a ||
      rdpxi_state[wid].combiner_alphasub_a[1] ==
          &rdpxi_state[wid].texel0_color.a ||
      rdpxi_state[wid].combiner_alphasub_b[1] ==
          &rdpxi_state[wid].texel0_color.a ||
      rdpxi_state[wid].combiner_alphaadd[1] ==
          &rdpxi_state[wid].texel0_color.a ||
      rdpxi_state[wid].combiner_rgbmul_r[1] ==
          &rdpxi_state[wid].texel0_color.a) {
    texel0_used_in_cc1 = 1;
  }
  if (rdpxi_state[wid].combiner_alphamul[0] ==
          &rdpxi_state[wid].texel1_color.a ||
      rdpxi_state[wid].combiner_alphasub_a[0] ==
          &rdpxi_state[wid].texel1_color.a ||
      rdpxi_state[wid].combiner_alphasub_b[0] ==
          &rdpxi_state[wid].texel1_color.a ||
      rdpxi_state[wid].combiner_alphaadd[0] ==
          &rdpxi_state[wid].texel1_color.a) {
    texel1_used_in_ac0 = 1;
  }
  if (rdpxi_state[wid].combiner_alphamul[0] ==
          &rdpxi_state[wid].texel0_color.a ||
      rdpxi_state[wid].combiner_alphasub_a[0] ==
          &rdpxi_state[wid].texel0_color.a ||
      rdpxi_state[wid].combiner_alphasub_b[0] ==
          &rdpxi_state[wid].texel0_color.a ||
      rdpxi_state[wid].combiner_alphaadd[0] ==
          &rdpxi_state[wid].texel0_color.a) {
    texel0_used_in_ac0 = 1;
  }
  if (rdpxi_state[wid].combiner_rgbmul_r[0] ==
          &rdpxi_state[wid].texel1_color.r ||
      rdpxi_state[wid].combiner_rgbsub_a_r[0] ==
          &rdpxi_state[wid].texel1_color.r ||
      rdpxi_state[wid].combiner_rgbsub_b_r[0] ==
          &rdpxi_state[wid].texel1_color.r ||
      rdpxi_state[wid].combiner_rgbadd_r[0] ==
          &rdpxi_state[wid].texel1_color.r ||
      texel1_used_in_ac0 ||
      rdpxi_state[wid].combiner_rgbmul_r[0] ==
          &rdpxi_state[wid].texel1_color.a) {
    texel1_used_in_cc0 = 1;
  }
  if (rdpxi_state[wid].combiner_rgbmul_r[0] ==
          &rdpxi_state[wid].texel0_color.r ||
      rdpxi_state[wid].combiner_rgbsub_a_r[0] ==
          &rdpxi_state[wid].texel0_color.r ||
      rdpxi_state[wid].combiner_rgbsub_b_r[0] ==
          &rdpxi_state[wid].texel0_color.r ||
      rdpxi_state[wid].combiner_rgbadd_r[0] ==
          &rdpxi_state[wid].texel0_color.r ||
      texel0_used_in_ac0 ||
      rdpxi_state[wid].combiner_rgbmul_r[0] ==
          &rdpxi_state[wid].texel0_color.a) {
    texel0_used_in_cc0 = 1;
  }
  texels_or_lf_used_in_ac0 =
      texel0_used_in_ac0 || texel1_used_in_ac0 ||
      (rdpxi_state[wid].combiner_alphamul[0] == &rdpxi_state[wid].lod_frac);
  texels_in_cc0 = texel0_used_in_cc0 || texel1_used_in_cc0;
  texels_in_cc1 = texel0_used_in_cc1 || texel1_used_in_cc1;

  if (texel1_used_in_cc1) {
    rdpxi_state[wid].other_modes.f.textureuselevel0 = 0;
  } else if (texel0_used_in_cc1 || lod_frac_used_in_cc1) {
    rdpxi_state[wid].other_modes.f.textureuselevel0 = 1;
  } else {
    rdpxi_state[wid].other_modes.f.textureuselevel0 = 2;
  }

  if (texel1_used_in_cc1 || (rdpxi_state[wid].other_modes.alpha_compare_en &&
                             texels_or_lf_used_in_ac0)) {
    rdpxi_state[wid].other_modes.f.textureuselevel1 = 0;
  } else if (texel1_used_in_cc0 || texel0_used_in_cc1) {
    rdpxi_state[wid].other_modes.f.textureuselevel1 = 1;
  } else if (texel0_used_in_cc0 || lod_frac_used_in_cc0 ||
             lod_frac_used_in_cc1) {
    rdpxi_state[wid].other_modes.f.textureuselevel1 = 2;
  } else {
    rdpxi_state[wid].other_modes.f.textureuselevel1 = 3;
  }

  int lodfracused = 0;

  if ((rdpxi_state[wid].other_modes.cycle_type == CYCLE_TYPE_2 &&
       (lod_frac_used_in_cc0 || lod_frac_used_in_cc1)) ||
      (rdpxi_state[wid].other_modes.cycle_type == CYCLE_TYPE_1 &&
       lod_frac_used_in_cc1)) {
    lodfracused = 1;
  }

  if ((rdpxi_state[wid].other_modes.cycle_type == CYCLE_TYPE_1 &&
       rdpxi_state[wid].combiner_rgbsub_a_r[1] == &rdpxi_state[wid].noise) ||
      (rdpxi_state[wid].other_modes.cycle_type == CYCLE_TYPE_2 &&
       (rdpxi_state[wid].combiner_rgbsub_a_r[0] == &rdpxi_state[wid].noise ||
        rdpxi_state[wid].combiner_rgbsub_a_r[1] == &rdpxi_state[wid].noise)) ||
      rdpxi_state[wid].other_modes.alpha_dither_sel == 2) {
    rdpxi_state[wid].other_modes.f.getditherlevel = 0;
  } else if (rdpxi_state[wid].other_modes.f.rgb_alpha_dither != 0xf) {
    rdpxi_state[wid].other_modes.f.getditherlevel = 1;
  } else {
    rdpxi_state[wid].other_modes.f.getditherlevel = 2;
  }

  rdpxi_state[wid].other_modes.f.dolod =
      rdpxi_state[wid].other_modes.tex_lod_en || lodfracused;
}

static void rdp_init(uint32_t wid, uint32_t num_workers) {
  rdpxi_state[wid].stride = num_workers;
  rdpxi_state[wid].offset = wid;
  rdpxi_state[wid].rseed = 3 + wid * 13;

  uint32_t tmp[2] = {0};
  rdp_set_other_modes(wid, tmp);
}

static void rdp_invalid(uint32_t wid, const uint32_t* args) {}

static void rdp_noop(uint32_t wid, const uint32_t* args) {}

static void rdp_sync_load(uint32_t wid, const uint32_t* args) {}

static void rdp_sync_pipe(uint32_t wid, const uint32_t* args) {}

static void rdp_sync_tile(uint32_t wid, const uint32_t* args) {}

static void rdp_sync_full(uint32_t /*wid*/, const uint32_t* /*args*/) {
  // signal DP interrupt
  *rdpxi_config.gfx.mi_intr_reg |= DP_INTERRUPT;
  rdpxi_config.gfx.mi_intr_cb();
}

static void rdp_set_other_modes(uint32_t wid, const uint32_t* args) {
  rdpxi_state[wid].other_modes.cycle_type = (args[0] >> 20) & 3;
  rdpxi_state[wid].other_modes.persp_tex_en = (args[0] >> 19) & 1;
  rdpxi_state[wid].other_modes.detail_tex_en = (args[0] >> 18) & 1;
  rdpxi_state[wid].other_modes.sharpen_tex_en = (args[0] >> 17) & 1;
  rdpxi_state[wid].other_modes.tex_lod_en = (args[0] >> 16) & 1;
  rdpxi_state[wid].other_modes.en_tlut = (args[0] >> 15) & 1;
  rdpxi_state[wid].other_modes.tlut_type = (args[0] >> 14) & 1;
  rdpxi_state[wid].other_modes.sample_type = (args[0] >> 13) & 1;
  rdpxi_state[wid].other_modes.mid_texel = (args[0] >> 12) & 1;
  rdpxi_state[wid].other_modes.bi_lerp0 = (args[0] >> 11) & 1;
  rdpxi_state[wid].other_modes.bi_lerp1 = (args[0] >> 10) & 1;
  rdpxi_state[wid].other_modes.convert_one = (args[0] >> 9) & 1;
  rdpxi_state[wid].other_modes.key_en = (args[0] >> 8) & 1;
  rdpxi_state[wid].other_modes.rgb_dither_sel = (args[0] >> 6) & 3;
  rdpxi_state[wid].other_modes.alpha_dither_sel = (args[0] >> 4) & 3;
  rdpxi_state[wid].other_modes.blend_m1a_0 = (args[1] >> 30) & 3;
  rdpxi_state[wid].other_modes.blend_m1a_1 = (args[1] >> 28) & 3;
  rdpxi_state[wid].other_modes.blend_m1b_0 = (args[1] >> 26) & 3;
  rdpxi_state[wid].other_modes.blend_m1b_1 = (args[1] >> 24) & 3;
  rdpxi_state[wid].other_modes.blend_m2a_0 = (args[1] >> 22) & 3;
  rdpxi_state[wid].other_modes.blend_m2a_1 = (args[1] >> 20) & 3;
  rdpxi_state[wid].other_modes.blend_m2b_0 = (args[1] >> 18) & 3;
  rdpxi_state[wid].other_modes.blend_m2b_1 = (args[1] >> 16) & 3;
  rdpxi_state[wid].other_modes.force_blend = (args[1] >> 14) & 1;
  rdpxi_state[wid].other_modes.alpha_cvg_select = (args[1] >> 13) & 1;
  rdpxi_state[wid].other_modes.cvg_times_alpha = (args[1] >> 12) & 1;
  rdpxi_state[wid].other_modes.z_mode = (args[1] >> 10) & 3;
  rdpxi_state[wid].other_modes.cvg_dest = (args[1] >> 8) & 3;
  rdpxi_state[wid].other_modes.color_on_cvg = (args[1] >> 7) & 1;
  rdpxi_state[wid].other_modes.image_read_en = (args[1] >> 6) & 1;
  rdpxi_state[wid].other_modes.z_update_en = (args[1] >> 5) & 1;
  rdpxi_state[wid].other_modes.z_compare_en = (args[1] >> 4) & 1;
  rdpxi_state[wid].other_modes.antialias_en = (args[1] >> 3) & 1;
  rdpxi_state[wid].other_modes.z_source_sel = (args[1] >> 2) & 1;
  rdpxi_state[wid].other_modes.dither_alpha_en = (args[1] >> 1) & 1;
  rdpxi_state[wid].other_modes.alpha_compare_en = (args[1] >> 0) & 1;

  set_blender_input(wid, 0, 0, &rdpxi_state[wid].blender1a_r[0],
                    &rdpxi_state[wid].blender1a_g[0],
                    &rdpxi_state[wid].blender1a_b[0],
                    &rdpxi_state[wid].blender1b_a[0],
                    rdpxi_state[wid].other_modes.blend_m1a_0,
                    rdpxi_state[wid].other_modes.blend_m1b_0);
  set_blender_input(wid, 0, 1, &rdpxi_state[wid].blender2a_r[0],
                    &rdpxi_state[wid].blender2a_g[0],
                    &rdpxi_state[wid].blender2a_b[0],
                    &rdpxi_state[wid].blender2b_a[0],
                    rdpxi_state[wid].other_modes.blend_m2a_0,
                    rdpxi_state[wid].other_modes.blend_m2b_0);
  set_blender_input(wid, 1, 0, &rdpxi_state[wid].blender1a_r[1],
                    &rdpxi_state[wid].blender1a_g[1],
                    &rdpxi_state[wid].blender1a_b[1],
                    &rdpxi_state[wid].blender1b_a[1],
                    rdpxi_state[wid].other_modes.blend_m1a_1,
                    rdpxi_state[wid].other_modes.blend_m1b_1);
  set_blender_input(wid, 1, 1, &rdpxi_state[wid].blender2a_r[1],
                    &rdpxi_state[wid].blender2a_g[1],
                    &rdpxi_state[wid].blender2a_b[1],
                    &rdpxi_state[wid].blender2b_a[1],
                    rdpxi_state[wid].other_modes.blend_m2a_1,
                    rdpxi_state[wid].other_modes.blend_m2b_1);

  rdpxi_state[wid].other_modes.f.stalederivs = 1;
}

static void rdp_cmd(uint32_t wid, const uint32_t* args) {
  uint32_t const cmd_id = CMD_ID(args);
  RDP_COMMANDS[cmd_id].handler(wid, args);
}

#endif  // N64VIDEO_C
