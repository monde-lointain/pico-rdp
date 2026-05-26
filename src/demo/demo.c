// demo_core public API: arena layout + per-frame RDP command stream — see
// demo.h.
//
// demo_build_frame is the demo fan-in: it sets the render state and drives the
// scene helpers (scene.c) to emit one complete, deterministic RDP command frame
// (clears, state, draws, sync) into a CmdSink, mirroring gldemo's sequence.
//
// Freestanding / Orthodox: plain C, POD, no malloc / printf / float. Baked
// tables are static const (-> flash on device); all texture/TLUT seeding goes
// through the XOR-correct RDRAM I/O (never plain memcpy).
//
// --- Render state derivation (gldemo G_* modes -> our raw RDP bits) ----------
// clang-format off
// gldemo (render.c / graphic.c) on F3DEX2:
//   clear:   G_CYC_FILL, SET_COLOR_IMAGE=Z then color, SET_FILL_COLOR, FILL_RECT
//            (Z fill = GPACK_ZDZ(G_MAXFBZ,0); color fill = GPACK_RGBA5551(0,0,0,1))
//   pyramid: G_CYC_1CYCLE, G_RM_AA_ZB_OPA_SURF, combine = default shade
//            (G_CC_SHADE), G_ZBUFFER|G_SHADE|G_SHADING_SMOOTH|G_CULL_BACK
//   cube:    same cycle/rendermode, our addition = arrows CI4 texture via TLUT,
//            combine = texel * shade (G_CC_MODULATEIA), Z, CULL_BACK
// clang-format on
// We disable AA (task: sharp opaque edges) so other-modes z/coverage stay
// simple. Combine words are baked constants packed exactly as the bit-exact
// oracle RDP::CommandBuilder::set_combiner_2cycle does (1-cycle =>
// first==second):
//
//   PYRAMID (G_CC_SHADE = (0,0,0,SHADE) RGB, (0,0,0,SHADE) A):
//     RGB  muladd=Zero(8) mulsub=Zero(8) mul=Zero(16) add=Shade(4)
//     A    muladd=Zero(7) mulsub=Zero(7) mul=Zero(7)  add=ShadeAlpha(4)
//     -> hi=0x00887f10  lo=0x88fe793c
//   CUBE (G_CC_MODULATEIA = (TEXEL0,0,SHADE,0) RGB, (TEXEL0,0,SHADE,0) A):
//     RGB  muladd=Texel0(1) mulsub=Zero(8) mul=Shade(4)      add=Zero(7)
//     A    muladd=Tex0A(1)  mulsub=Zero(7) mul=ShadeAlpha(4) add=Zero(7)
//     -> hi=0x00121824  lo=0x8833ffff
//
// SET_OTHER_MODES bits (emit_set_other_modes packs these): cycle_type=1cycle,
// z_compare=1, z_update=1, aa=0; perspective=1 only for the textured cube;
// TLUT=1
// + sample point (no bilerp) for the cube. Blender left at defaults (m1a=
// PixelColor) with blend_en=0 -> the combined color is written straight to the
// FB (opaque), matching G_RM_*_OPA_SURF without AA.

#include "demo.h"

#include "cmd_sink.h"
#include "emit.h"
#include "fixed.h"
#include "generated/arrows_ci4.h"
#include "generated/baked_matrices.h"
#include "rdram_io.h"
#include "scene.h"
#include "setup.h"

// Baked combiner words (see header comment for the field-by-field derivation).
// Plain macros (not an enum: the lo words exceed INT_MAX, illegal as
// enumerators).
#define DEMO_COMBINE_SHADE_HI 0x00887f10u
#define DEMO_COMBINE_SHADE_LO 0x88fe793cu
#define DEMO_COMBINE_TEXEL_SHADE_HI 0x00121824u
#define DEMO_COMBINE_TEXEL_SHADE_LO 0x8833ffffu

// Fill values (mirror gldemo). Z fill: GPACK_ZDZ(G_MAXFBZ,0) = G_MAXFBZ<<2,
// G_MAXFBZ=0x3fff -> 0xfffc, packed into both 16-bit halves. Color fill: opaque
// black RGBA5551 = (0,0,0,1) = 0x0001, packed into both halves.
#define DEMO_Z_FILL_PIXEL 0xfffcu
#define DEMO_COLOR_FILL_PIXEL 0x0001u
#define DEMO_Z_FILL_WORD ((DEMO_Z_FILL_PIXEL << 16) | DEMO_Z_FILL_PIXEL)
#define DEMO_COLOR_FILL_WORD \
  ((DEMO_COLOR_FILL_PIXEL << 16) | DEMO_COLOR_FILL_PIXEL)

// Backface cull: gldemo G_CULL_BACK. Our screen-space signed_area sign (after a
// no-Y-flip viewport) selects which winding is back; verified end-to-end.
enum { DEMO_SCENE_CULL = DEMO_CULL_CW_ONLY };

// --- Command sink forwarder (declared in cmd_sink.h, defined here) -----------

void cmd_sink_emit(struct CmdSink *sink, const uint32_t *words, uint32_t n) {
  if (sink != 0 && sink->emit != 0) {
    sink->emit(sink->ctx, words, n);
  }
}

// --- Init: lay out arena + seed CI4 texture and TLUT -------------------------

void demo_init(uint8_t *rdram, uint32_t rdram_size) {
  // Caller must provide at least the laid-out arena.
  if (rdram == 0 || rdram_size < DEMO_RDRAM_USED_END) {
    return;
  }

  // Seed the 64x64 CI4 texture (nibble-packed byte stream) into RDRAM, applying
  // the byte XOR so the renderer's LOAD_TILE recovers it byte-for-byte.
  rdram_write_block(rdram, DEMO_RDRAM_CI4_TEX_ADDR, arrows_ci4,
                    DEMO_RDRAM_CI4_TEX_SIZE);

  // Seed the 16-entry RGBA5551 TLUT as N64-native big-endian halfwords (the
  // generated values are logical RGBA5551 pixel values; rdram_write16 stores
  // them so the renderer's halfword read recovers each entry).
  for (uint32_t i = 0; i < DEMO_TLUT_ENTRIES; ++i) {
    rdram_write16(rdram, DEMO_RDRAM_TLUT_ADDR + (i * 2U), arrows_tlut[i]);
  }
}

// --- Per-frame state helpers -------------------------------------------------

// Common viewport: full 240x240 screen, depth [0,1] (Q16.16). No Y-flip (the
// baked PROJ + this transform already land the scene upright; verified e2e).
static void demo_viewport(struct DemoViewport *vp) {
  vp->x = 0;
  vp->y = 0;
  vp->width = (demo_fix)((int32_t)DEMO_FB_WIDTH << DEMO_FIX_SHIFT);
  vp->height = (demo_fix)((int32_t)DEMO_FB_HEIGHT << DEMO_FIX_SHIFT);
  vp->min_depth = 0;
  vp->max_depth = DEMO_FIX_ONE;
}

// Set other-modes for the opaque shaded pass (pyramid): 1-cycle, Z test+write,
// AA off, no texture/perspective.
static void demo_modes_shade(struct DemoOtherModes *m) {
  for (uint32_t i = 0; i < sizeof(*m); ++i) {
    ((uint8_t *)m)[i] = 0;
  }
  m->cycle_type = DEMO_CYCLE_1;
  m->rgb_dither = 3;    // Off
  m->alpha_dither = 3;  // Off
  m->z_compare = 1;
  m->z_update = 1;
}

// Set other-modes for the textured pass (cube): as shade + perspective + TLUT,
// point sampling (no bilerp).
static void demo_modes_texture(struct DemoOtherModes *m) {
  demo_modes_shade(m);
  m->perspective = 1;
  m->TLUT = 1;
}

// Clear one image (Z or color) via the FILL-mode rectangle sequence.
static void demo_clear_image(struct CmdSink *sink, uint32_t addr,
                             uint32_t fill_word) {
  emit_set_color_image(sink, DEMO_TEXFMT_RGBA, DEMO_TEXSIZE_16BPP, addr,
                       DEMO_FB_WIDTH);
  emit_set_fill_color(sink, fill_word);
  emit_fill_rectangle(sink, 0, 0, DEMO_FB_WIDTH, DEMO_FB_HEIGHT);
  emit_sync_pipe(sink);
}

// TMEM byte stride of one CI4 row: 64 texels * 4 bits / 8 = 32 bytes.
#define DEMO_CI4_TMEM_STRIDE 32u
// CI4 loaded as 8-bit at half width: 64 4-bit texels == 32 8-bit "pixels".
#define DEMO_CI4_LOAD_WIDTH (DEMO_CI4_TEX_WIDTH / 2)  // 32

// Program the TLUT + CI4 texture into TMEM for the cube's arrows texture.
//
// Two RDP-hardware constraints drive the exact sequence (both verified e2e
// against rdp_core; getting either wrong latches the renderer's pipeline-crash
// or yields untextured/blank output):
//   1. LOAD_TLUT / LOAD_TILE DMA from the CURRENT SET_TEXTURE_IMAGE address, so
//      the texture image must point at the TLUT data before LOAD_TLUT, then at
//      the CI4 data before LOAD_TILE.
//   2. A 4-bit texture image is illegal for LOAD_TILE (tex.c crashes the
//      pipeline on PIXEL_SIZE_4BIT). The standard N64 idiom is to load CI4 as
//      8-bit at half width (32 px * 64 rows == 2048 bytes), then SAMPLE it with
//      a separate CI4 tile (4bpp) at the same TMEM stride (32 bytes/row).
// The renderer reads the TLUT at the fixed tmem[0x800] base (tmem.c), so the
// load tile's TMEM offset is 0x800; the CI4 sample tile sits at offset 0.
static void demo_load_cube_texture(struct CmdSink *sink) {
  // --- TLUT: texture image -> TLUT data (16-bit), load into tmem[0x800] -----
  emit_set_texture_image(sink, DEMO_TEXFMT_RGBA, DEMO_TEXSIZE_16BPP,
                         DEMO_RDRAM_TLUT_ADDR, DEMO_TLUT_ENTRIES);
  struct DemoTileDesc tlut_tile;
  for (uint32_t i = 0; i < sizeof(tlut_tile); ++i) {
    ((uint8_t *)&tlut_tile)[i] = 0;
  }
  tlut_tile.fmt = DEMO_TEXFMT_RGBA;
  tlut_tile.size = DEMO_TEXSIZE_16BPP;
  tlut_tile.offset = 0x800;  // tmem[0x800] = TLUT region (per tmem.c)
  emit_set_tile(sink, 1, &tlut_tile);
  emit_load_tlut(sink, 1, 0, 0, DEMO_TLUT_ENTRIES, 1);
  emit_sync_load(sink);

  // --- CI4 texels: texture image -> CI4 data as 8-bit half-width ------------
  emit_set_texture_image(sink, DEMO_TEXFMT_CI, DEMO_TEXSIZE_8BPP,
                         DEMO_RDRAM_CI4_TEX_ADDR, DEMO_CI4_LOAD_WIDTH);
  // Load tile (tile 7): 8-bit, TMEM stride 32 bytes, base offset 0.
  struct DemoTileDesc load_tile;
  for (uint32_t i = 0; i < sizeof(load_tile); ++i) {
    ((uint8_t *)&load_tile)[i] = 0;
  }
  load_tile.fmt = DEMO_TEXFMT_CI;
  load_tile.size = DEMO_TEXSIZE_8BPP;
  load_tile.stride = DEMO_CI4_TMEM_STRIDE;
  load_tile.offset = 0;
  emit_set_tile(sink, 7, &load_tile);
  emit_load_tile(sink, 7, 0, 0, DEMO_CI4_LOAD_WIDTH, DEMO_CI4_TEX_HEIGHT);
  emit_sync_load(sink);

  // --- Sample tile (tile 0): CI4 4bpp, same TMEM stride, CLAMP S/T ----------
  // The CI palette field selects the TLUT 16-entry bank; our indices fit one
  // palette, so palette=0. emit_triangle uses tile 0.
  struct DemoTileDesc ci_tile;
  for (uint32_t i = 0; i < sizeof(ci_tile); ++i) {
    ((uint8_t *)&ci_tile)[i] = 0;
  }
  ci_tile.fmt = DEMO_TEXFMT_CI;
  ci_tile.size = DEMO_TEXSIZE_4BPP;
  ci_tile.stride = DEMO_CI4_TMEM_STRIDE;
  ci_tile.offset = 0;
  ci_tile.palette = 0;
  ci_tile.flags = DEMO_TILE_CLAMP_S_BIT | DEMO_TILE_CLAMP_T_BIT;
  emit_set_tile(sink, 0, &ci_tile);
  emit_set_tile_size(sink, 0, 0, 0, DEMO_CI4_TEX_WIDTH, DEMO_CI4_TEX_HEIGHT);
}

// --- Per-frame builder -------------------------------------------------------

void demo_build_frame(uint32_t frame_index, struct CmdSink *sink) {
  uint32_t frame = frame_index % DEMO_ANIM_PERIOD;
  struct DemoViewport vp;
  demo_viewport(&vp);

  // 1) Scissor to the full FB, then clear Z and color via FILL mode.
  emit_set_scissor(sink, 0, 0, DEMO_FB_WIDTH, DEMO_FB_HEIGHT);

  struct DemoOtherModes fill_modes;
  for (uint32_t i = 0; i < sizeof(fill_modes); ++i) {
    ((uint8_t *)&fill_modes)[i] = 0;
  }
  fill_modes.cycle_type = DEMO_CYCLE_FILL;
  fill_modes.rgb_dither = 3;
  fill_modes.alpha_dither = 3;
  emit_set_other_modes(sink, &fill_modes);

  emit_set_depth_image(sink, DEMO_RDRAM_Z_BUF_ADDR);
  demo_clear_image(sink, DEMO_RDRAM_Z_BUF_ADDR, DEMO_Z_FILL_WORD);
  demo_clear_image(sink, DEMO_RDRAM_COLOR_FB_ADDR, DEMO_COLOR_FILL_WORD);

  // From here on we draw into the color FB with Z buffering.
  emit_set_color_image(sink, DEMO_TEXFMT_RGBA, DEMO_TEXSIZE_16BPP,
                       DEMO_RDRAM_COLOR_FB_ADDR, DEMO_FB_WIDTH);
  emit_set_depth_image(sink, DEMO_RDRAM_Z_BUF_ADDR);

  // 2) Pyramid: shade-only, Z test+write, AA off.
  struct DemoOtherModes shade_modes;
  demo_modes_shade(&shade_modes);
  emit_set_other_modes(sink, &shade_modes);
  emit_set_combine(sink, DEMO_COMBINE_SHADE_HI, DEMO_COMBINE_SHADE_LO);

  struct DemoMat4 pyr_mvp;
  scene_build_mvp(&pyr_mvp, demo_pyramid_model[frame % DEMO_PYRAMID_PERIOD],
                  demo_view_matrix, demo_proj_matrix);
  scene_draw_pyramid(sink, &pyr_mvp, &vp, DEMO_SCENE_CULL);

  // 3) Cube: texel * shade (CI4 via TLUT), Z, perspective-correct.
  demo_load_cube_texture(sink);

  struct DemoOtherModes tex_modes;
  demo_modes_texture(&tex_modes);
  emit_set_other_modes(sink, &tex_modes);
  emit_set_combine(sink, DEMO_COMBINE_TEXEL_SHADE_HI,
                   DEMO_COMBINE_TEXEL_SHADE_LO);

  struct DemoMat4 cube_mvp;
  scene_build_mvp(&cube_mvp, demo_cube_model[frame % DEMO_CUBE_PERIOD],
                  demo_view_matrix, demo_proj_matrix);
  scene_draw_cube(sink, &cube_mvp, &vp, DEMO_SCENE_CULL);

  // 4) Finish the frame.
  emit_sync_full(sink);
}

// --- Accessors ---------------------------------------------------------------

uint32_t demo_color_fb_addr(void) { return DEMO_RDRAM_COLOR_FB_ADDR; }
uint32_t demo_fb_width(void) { return DEMO_FB_WIDTH; }
uint32_t demo_fb_height(void) { return DEMO_FB_HEIGHT; }
uint32_t demo_fb_format(void) { return DEMO_PIXFMT_RGBA5551; }
uint32_t demo_anim_period_frames(void) { return DEMO_ANIM_PERIOD_FRAMES; }
