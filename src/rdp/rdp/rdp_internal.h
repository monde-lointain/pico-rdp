#pragma once

// rdp_internal.h — shared foundation for the RDP renderer's translation units.
//
// Holds the cross-TU shared MACROS and POD TYPES that the unity build used to
// define inline in n64video.c / rdp.c. Extracted here (verbatim from the
// canonical fork 31bdb1f) so the individual stage TUs can share them once the
// unity #include chain is split apart. Types/macros have no linkage, so nothing
// here collides with the Angrylion oracle; the cross-TU extern symbols
// (rdpxi_state, rdpxi_config, handlers, …) are added as the stages are split.

#include <stdint.h>

#include "n64video.h"
#include "n64video_common.h"

// ---- shared macros (from the fork's n64video.c) --------------------------
#define MIN(a, b) (((a) < (b)) ? (a) : (b))
#define MAX(a, b) (((a) > (b)) ? (a) : (b))
#define CLAMP(x, lo, hi) (((x) > (hi)) ? (hi) : (((x) < (lo)) ? (lo) : (x)))

#define SIGN16(x) ((int16_t)(x))
#define SIGN8(x) ((int8_t)(x))

#define SIGN(x, numb) \
  (((x) & ((1 << (numb)) - 1)) | -((x) & (1 << ((numb) - 1))))
#define SIGNF(x, numb) ((x) | -((x) & (1 << ((numb) - 1))))

#define TRELATIVE(x, y) ((x) - ((y) << 3))

#define PIXELS_TO_BYTES(pix, siz) (((pix) << (siz)) >> 1)

// RGBA5551 to RGBA8888 helper
#define RGBA16_R(x) (((x) >> 8) & 0xf8)
#define RGBA16_G(x) (((x) & 0x7c0) >> 3)
#define RGBA16_B(x) (((x) & 0x3e) << 2)

// RGBA8888 helper
#define RGBA32_R(x) (((x) >> 24) & 0xff)
#define RGBA32_G(x) (((x) >> 16) & 0xff)
#define RGBA32_B(x) (((x) >> 8) & 0xff)
#define RGBA32_A(x) ((x) & 0xff)

// maximum number of commands to buffer for parallel processing
#define CMD_BUFFER_SIZE 1024

// maximum data size of a single command in bytes
#define CMD_MAX_SIZE 176

// maximum data size of a single command in 32 bit integers
#define CMD_MAX_INTS (CMD_MAX_SIZE / sizeof(int32_t))

// extracts the command ID from a command buffer
#define CMD_ID(cmd) ((*(cmd) >> 24) & 0x3f)

// list of command IDs
#define CMD_ID_NO_OP 0x00
#define CMD_ID_FILL_TRIANGLE 0x08
#define CMD_ID_FILL_ZBUFFER_TRIANGLE 0x09
#define CMD_ID_TEXTURE_TRIANGLE 0x0a
#define CMD_ID_TEXTURE_ZBUFFER_TRIANGLE 0x0b
#define CMD_ID_SHADE_TRIANGLE 0x0c
#define CMD_ID_SHADE_ZBUFFER_TRIANGLE 0x0d
#define CMD_ID_SHADE_TEXTURE_TRIANGLE 0x0e
#define CMD_ID_SHADE_TEXTURE_Z_BUFFER_TRIANGLE 0x0f
#define CMD_ID_TEXTURE_RECTANGLE 0x24
#define CMD_ID_TEXTURE_RECTANGLE_FLIP 0x25
#define CMD_ID_SYNC_LOAD 0x26
#define CMD_ID_SYNC_PIPE 0x27
#define CMD_ID_SYNC_TILE 0x28
#define CMD_ID_SYNC_FULL 0x29
#define CMD_ID_SET_KEY_GB 0x2a
#define CMD_ID_SET_KEY_R 0x2b
#define CMD_ID_SET_CONVERT 0x2c
#define CMD_ID_SET_SCISSOR 0x2d
#define CMD_ID_SET_PRIM_DEPTH 0x2e
#define CMD_ID_SET_OTHER_MODES 0x2f
#define CMD_ID_LOAD_TLUT 0x30
#define CMD_ID_SET_TILE_SIZE 0x32
#define CMD_ID_LOAD_BLOCK 0x33
#define CMD_ID_LOAD_TILE 0x34
#define CMD_ID_SET_TILE 0x35
#define CMD_ID_FILL_RECTANGLE 0x36
#define CMD_ID_SET_FILL_COLOR 0x37
#define CMD_ID_SET_FOG_COLOR 0x38
#define CMD_ID_SET_BLEND_COLOR 0x39
#define CMD_ID_SET_PRIM_COLOR 0x3a
#define CMD_ID_SET_ENV_COLOR 0x3b
#define CMD_ID_SET_COMBINE 0x3c
#define CMD_ID_SET_TEXTURE_IMAGE 0x3d
#define CMD_ID_SET_MASK_IMAGE 0x3e
#define CMD_ID_SET_COLOR_IMAGE 0x3f

// ---- bit constants / enums (from the fork's rdp.c) -----------------------
// bit constants for DP_STATUS
#define DP_STATUS_XBUS_DMA 0x001
#define DP_STATUS_FREEZE 0x002
#define DP_STATUS_FLUSH 0x004
#define DP_STATUS_START_GCLK 0x008
#define DP_STATUS_TMEM_BUSY 0x010
#define DP_STATUS_PIPE_BUSY 0x020
#define DP_STATUS_CMD_BUSY 0x040
#define DP_STATUS_CBUF_BUSY 0x080
#define DP_STATUS_DMA_BUSY 0x100
#define DP_STATUS_END_VALID 0x200
#define DP_STATUS_START_VALID 0x400

#define PIXEL_SIZE_4BIT 0
#define PIXEL_SIZE_8BIT 1
#define PIXEL_SIZE_16BIT 2
#define PIXEL_SIZE_32BIT 3

#define CYCLE_TYPE_1 0
#define CYCLE_TYPE_2 1
#define CYCLE_TYPE_COPY 2
#define CYCLE_TYPE_FILL 3

#define FORMAT_RGBA 0
#define FORMAT_YUV 1
#define FORMAT_CI 2
#define FORMAT_IA 3
#define FORMAT_I 4

#define TEXEL_RGBA4 0
#define TEXEL_RGBA8 1
#define TEXEL_RGBA16 2
#define TEXEL_RGBA32 3
#define TEXEL_YUV4 4
#define TEXEL_YUV8 5
#define TEXEL_YUV16 6
#define TEXEL_YUV32 7
#define TEXEL_CI4 8
#define TEXEL_CI8 9
#define TEXEL_CI16 0xa
#define TEXEL_CI32 0xb
#define TEXEL_IA4 0xc
#define TEXEL_IA8 0xd
#define TEXEL_IA16 0xe
#define TEXEL_IA32 0xf
#define TEXEL_I4 0x10
#define TEXEL_I8 0x11
#define TEXEL_I16 0x12
#define TEXEL_I32 0x13

#define SP_INTERRUPT 0x1
#define SI_INTERRUPT 0x2
#define AI_INTERRUPT 0x4
#define VI_INTERRUPT 0x8
#define PI_INTERRUPT 0x10
#define DP_INTERRUPT 0x20

// ---- POD types (from the fork's rdp.c) -----------------------------------
struct Color {
  int32_t r, g, b, a;
};

struct Rectangle {
  uint16_t xl, yl, xh, yh;
};

struct OtherModes {
  int cycle_type;
  int persp_tex_en;
  int detail_tex_en;
  int sharpen_tex_en;
  int tex_lod_en;
  int en_tlut;
  int tlut_type;
  int sample_type;
  int mid_texel;
  int bi_lerp0;
  int bi_lerp1;
  int convert_one;
  int key_en;
  int rgb_dither_sel;
  int alpha_dither_sel;
  int blend_m1a_0;
  int blend_m1a_1;
  int blend_m1b_0;
  int blend_m1b_1;
  int blend_m2a_0;
  int blend_m2a_1;
  int blend_m2b_0;
  int blend_m2b_1;
  int force_blend;
  int alpha_cvg_select;
  int cvg_times_alpha;
  int z_mode;
  int cvg_dest;
  int color_on_cvg;
  int image_read_en;
  int z_update_en;
  int z_compare_en;
  int antialias_en;
  int z_source_sel;
  int dither_alpha_en;
  int alpha_compare_en;

  struct {
    int stalederivs;
    int dolod;
    int partialreject_1cycle;
    int partialreject_2cycle;
    int rgb_alpha_dither;
    int realblendershiftersneeded;
    int interpixelblendershiftersneeded;
    int getditherlevel;
    int textureuselevel0;
    int textureuselevel1;
  } f;
};

struct Spansigs {
  int endspan;
  int preendspan;
  int nextspan;
  int midspan;
  int longspan;
  int onelessthanmid;
};

struct Tile {
  int format;
  int size;
  int line;
  int tmem;
  int palette;
  int ct, mt, cs, ms;
  int mask_t, shift_t, mask_s, shift_s;

  uint16_t sl, tl, sh, th;

  struct {
    int clampdiffs, clampdifft;
    int clampens, clampent;
    int masksclamped, masktclamped;
    int notlutswitch, tlutswitch;
  } f;
};

struct Span {
  int lx, rx;
  int unscrx;
  int validline;
  int32_t r, g, b, a, s, t, w, z;
  int32_t majorx[4];
  int32_t minorx[4];
  int32_t invalyscan[4];
};

struct CombinerInputs {
  int sub_a_rgb0;
  int sub_b_rgb0;
  int mul_rgb0;
  int add_rgb0;
  int sub_a_a0;
  int sub_b_a0;
  int mul_a0;
  int add_a0;

  int sub_a_rgb1;
  int sub_b_rgb1;
  int mul_rgb1;
  int add_rgb1;
  int sub_a_a1;
  int sub_b_a1;
  int mul_a1;
  int add_a1;
};

struct RdpState {
  uint32_t stride;
  uint32_t offset;

  int blshifta;
  int blshiftb;
  int pastblshifta;
  int pastblshiftb;

  struct Span span[1024];

  // span states
  int spans_ds;
  int spans_dt;
  int spans_dw;
  int spans_dr;
  int spans_dg;
  int spans_db;
  int spans_da;
  int spans_dz;
  int spans_dzpix;

  int spans_drdy;
  int spans_dgdy;
  int spans_dbdy;
  int spans_dady;
  int spans_dzdy;
  int spans_cdr;
  int spans_cdg;
  int spans_cdb;
  int spans_cda;
  int spans_cdz;

  int spans_dsdy;
  int spans_dtdy;
  int spans_dwdy;

  struct OtherModes other_modes;

  struct Color combined_color;
  struct Color texel0_color;
  struct Color texel1_color;
  struct Color nexttexel_color;
  struct Color shade_color;
  int32_t noise;
  uint32_t noise_seed;
  uint32_t primitive_count;
  int32_t primitive_lod_frac;

  struct Color pixel_color;
  struct Color memory_color;
  struct Color pre_memory_color;

  struct Tile tile[8];

  int32_t k0_tf;
  int32_t k1_tf;
  int32_t k2_tf;
  int32_t k3_tf;
  int32_t k4;
  int32_t k5;
  int32_t lod_frac;

  uint32_t max_level;
  int32_t min_level;

  // irand
  uint32_t rseed;

  // blender
  int32_t* blender1a_r[2];
  int32_t* blender1a_g[2];
  int32_t* blender1a_b[2];
  int32_t* blender1b_a[2];
  int32_t* blender2a_r[2];
  int32_t* blender2a_g[2];
  int32_t* blender2a_b[2];
  int32_t* blender2b_a[2];

  int32_t blender_shade_alpha;

  struct Color blend_color;
  struct Color fog_color;
  struct Color inv_pixel_color;
  struct Color blended_pixel_color;

  // combiner
  struct CombinerInputs combine;

  int32_t* combiner_rgbsub_a_r[2];
  int32_t* combiner_rgbsub_a_g[2];
  int32_t* combiner_rgbsub_a_b[2];
  int32_t* combiner_rgbsub_b_r[2];
  int32_t* combiner_rgbsub_b_g[2];
  int32_t* combiner_rgbsub_b_b[2];
  int32_t* combiner_rgbmul_r[2];
  int32_t* combiner_rgbmul_g[2];
  int32_t* combiner_rgbmul_b[2];
  int32_t* combiner_rgbadd_r[2];
  int32_t* combiner_rgbadd_g[2];
  int32_t* combiner_rgbadd_b[2];

  int32_t* combiner_alphasub_a[2];
  int32_t* combiner_alphasub_b[2];
  int32_t* combiner_alphamul[2];
  int32_t* combiner_alphaadd[2];

  struct Color prim_color;
  struct Color env_color;
  struct Color key_scale;
  struct Color key_center;
  struct Color key_width;

  int32_t keyalpha;

  // tcoord
  void (*tcdiv_ptr)(int32_t, int32_t, int32_t, int32_t*, int32_t*);

  // fbuffer
  void (*fbread1_ptr)(uint32_t, uint32_t, uint32_t*);
  void (*fbread2_ptr)(uint32_t, uint32_t, uint32_t*);
  void (*fbwrite_ptr)(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t,
                      uint32_t, uint32_t, uint32_t);

  int fb_format;
  int fb_size;
  int fb_width;
  uint32_t fb_address;
  uint32_t fill_color;

  // rasterizer
  struct Rectangle clip;
  int scfield;
  int sckeepodd;

  uint32_t primitive_z;
  uint16_t primitive_delta_z;

  // tex
  int ti_format;
  int ti_size;
  int ti_width;
  uint32_t ti_address;

  // coverage
  uint8_t cvgbuf[1024];

  // tmem
  uint8_t tmem[0x1000];

  // zbuffer
  uint32_t zb_address;
  int32_t pastrawdzmem;
};

// Host stub: single worker is sufficient (the device renderer will revisit
// worker layout in Stream C). The fork sizes this PARALLEL_MAX_WORKERS (64).
#ifndef RDPX_PARALLEL_MAX_WORKERS
#define RDPX_PARALLEL_MAX_WORKERS 1u
#endif

// ---- cross-TU shared state ------------------------------------------------
// External-linkage symbols the split-out stage TUs share. rdpxi_-prefixed to
// avoid colliding with the Angrylion oracle's identically-named C symbols
// (config, …) linked alongside us in the conformance harness.
extern struct N64videoConfig rdpxi_config;
