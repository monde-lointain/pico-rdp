// demo_core RDP command emitter (Stream B.3).
//
// Writes RAW RDP command words into a CmdSink, one command at a time. The
// triangle encoding is bit-exact with the vendored oracle
// RDP::CommandBuilder::submit_clipped_primitive
// (tests/conformance/vendor/rdp_command_builder.cpp); the non-triangle helpers
// mirror the same oracle's packing. See emit.h for the public contract.
//
// Orthodox / freestanding: plain C functions, fixed stack buffers, no heap, no
// printf, no C++ runtime. Word order matches what the RDP consumes: word[0]
// holds the high 32 bits of the 64-bit command (6-bit command id in 31..24).

#include "emit.h"

#include <stdint.h>

// RDP command ids (6-bit). Authoritative source: CMD_ID_* in
// src/rdp/n64video.c.
enum {
  EMIT_OP_SHADE_TEXTURE_Z_TRI = 0x0f,
  EMIT_OP_SYNC_LOAD = 0x26,
  EMIT_OP_SYNC_PIPE = 0x27,
  EMIT_OP_SYNC_TILE = 0x28,
  EMIT_OP_SYNC_FULL = 0x29,
  EMIT_OP_SET_SCISSOR = 0x2d,
  EMIT_OP_SET_OTHER_MODES = 0x2f,
  EMIT_OP_LOAD_TLUT = 0x30,
  EMIT_OP_SET_TILE_SIZE = 0x32,
  EMIT_OP_LOAD_TILE = 0x34,
  EMIT_OP_SET_TILE = 0x35,
  EMIT_OP_FILL_RECTANGLE = 0x36,
  EMIT_OP_SET_FILL_COLOR = 0x37,
  EMIT_OP_SET_COMBINE = 0x3c,
  EMIT_OP_SET_TEXTURE_IMAGE = 0x3d,
  EMIT_OP_SET_MASK_IMAGE = 0x3e,  // == SET_DEPTH_IMAGE
  EMIT_OP_SET_COLOR_IMAGE = 0x3f
};

// Forward one command's words into the sink (skips null sinks / callbacks).
static void emit_words(struct CmdSink *sink, const uint32_t *words,
                       uint32_t n) {
  if (sink != 0 && sink->emit != 0) {
    sink->emit(sink->ctx, words, n);
  }
}

// Low `bits` of val, as in the oracle's mask<T>().
static uint32_t mask_bits(int32_t val, unsigned bits) {
  return (uint32_t)val & (((uint32_t)1 << bits) - 1U);
}

// --- Triangle ----------------------------------------------------------------

void emit_triangle(struct CmdSink *sink, const struct DemoPrimSetup *setup) {
  // Mirrors submit_clipped_primitive() word-for-word (minus the leading
  // flush_default_state(), which the demo manages separately).
  uint32_t cmd[44];
  for (int32_t i = 0; i < 44; ++i) {
    cmd[i] = 0;
  }

  cmd[0] |= (uint32_t)EMIT_OP_SHADE_TEXTURE_Z_TRI << 24;
  if (!(setup->pos.flags & DEMO_PRIMITIVE_RIGHT_MAJOR_BIT)) {
    cmd[0] |= 1U << 23;
  }

  const uint32_t tile = 0;
  const uint32_t max_level = 6;
  cmd[0] |= tile << 16;
  cmd[0] |= max_level << 19;

  cmd[1] |= mask_bits(setup->pos.y_lo, 14);
  cmd[0] |= mask_bits(setup->pos.y_hi, 14);
  cmd[1] |= mask_bits(setup->pos.y_mid, 14) << 16;

  cmd[2] = mask_bits(setup->pos.x_c, 28);
  cmd[3] = mask_bits(setup->pos.dxdy_c, 30);
  cmd[4] = mask_bits(setup->pos.x_a, 28);

  // Need sign bit to deal with attribute interpolation.
  cmd[5] = (uint32_t)setup->pos.dxdy_a;

  cmd[6] = mask_bits(setup->pos.x_b, 28);
  cmd[7] = mask_bits(setup->pos.dxdy_b, 30);

  // RGBA value (c), split high/low halfword across two 32-bit-word groups.
  cmd[8] |= (uint32_t)setup->attr.c[0] & 0xffff0000U;
  cmd[12] |= ((uint32_t)setup->attr.c[0] << 16) & 0xffff0000U;
  cmd[8] |= ((uint32_t)setup->attr.c[1] >> 16) & 0xffffU;
  cmd[12] |= (uint32_t)setup->attr.c[1] & 0xffffU;
  cmd[9] |= (uint32_t)setup->attr.c[2] & 0xffff0000U;
  cmd[13] |= ((uint32_t)setup->attr.c[2] << 16) & 0xffff0000U;
  cmd[9] |= ((uint32_t)setup->attr.c[3] >> 16) & 0xffffU;
  cmd[13] |= (uint32_t)setup->attr.c[3] & 0xffffU;

  // dC/dx
  cmd[10] |= (uint32_t)setup->attr.dcdx[0] & 0xffff0000U;
  cmd[14] |= ((uint32_t)setup->attr.dcdx[0] << 16) & 0xffff0000U;
  cmd[10] |= ((uint32_t)setup->attr.dcdx[1] >> 16) & 0xffffU;
  cmd[14] |= (uint32_t)setup->attr.dcdx[1] & 0xffffU;
  cmd[11] |= (uint32_t)setup->attr.dcdx[2] & 0xffff0000U;
  cmd[15] |= ((uint32_t)setup->attr.dcdx[2] << 16) & 0xffff0000U;
  cmd[11] |= ((uint32_t)setup->attr.dcdx[3] >> 16) & 0xffffU;
  cmd[15] |= (uint32_t)setup->attr.dcdx[3] & 0xffffU;

  // dC/de
  cmd[16] |= (uint32_t)setup->attr.dcde[0] & 0xffff0000U;
  cmd[20] |= ((uint32_t)setup->attr.dcde[0] << 16) & 0xffff0000U;
  cmd[16] |= ((uint32_t)setup->attr.dcde[1] >> 16) & 0xffffU;
  cmd[20] |= (uint32_t)setup->attr.dcde[1] & 0xffffU;
  cmd[17] |= (uint32_t)setup->attr.dcde[2] & 0xffff0000U;
  cmd[21] |= ((uint32_t)setup->attr.dcde[2] << 16) & 0xffff0000U;
  cmd[17] |= ((uint32_t)setup->attr.dcde[3] >> 16) & 0xffffU;
  cmd[21] |= (uint32_t)setup->attr.dcde[3] & 0xffffU;

  // dC/dy
  cmd[18] |= (uint32_t)setup->attr.dcdy[0] & 0xffff0000U;
  cmd[22] |= ((uint32_t)setup->attr.dcdy[0] << 16) & 0xffff0000U;
  cmd[18] |= ((uint32_t)setup->attr.dcdy[1] >> 16) & 0xffffU;
  cmd[22] |= (uint32_t)setup->attr.dcdy[1] & 0xffffU;
  cmd[19] |= (uint32_t)setup->attr.dcdy[2] & 0xffff0000U;
  cmd[23] |= ((uint32_t)setup->attr.dcdy[2] << 16) & 0xffff0000U;
  cmd[19] |= ((uint32_t)setup->attr.dcdy[3] >> 16) & 0xffffU;
  cmd[23] |= (uint32_t)setup->attr.dcdy[3] & 0xffffU;

  // S/T/W value
  cmd[24] |= (uint32_t)setup->attr.u & 0xffff0000U;
  cmd[28] |= ((uint32_t)setup->attr.u << 16) & 0xffff0000U;
  cmd[24] |= ((uint32_t)setup->attr.v >> 16) & 0xffffU;
  cmd[28] |= (uint32_t)setup->attr.v & 0xffffU;
  cmd[25] |= (uint32_t)setup->attr.w & 0xffff0000U;
  cmd[29] |= ((uint32_t)setup->attr.w << 16) & 0xffff0000U;
  // dS/dx, dT/dx, dW/dx
  cmd[26] |= (uint32_t)setup->attr.dudx & 0xffff0000U;
  cmd[30] |= ((uint32_t)setup->attr.dudx << 16) & 0xffff0000U;
  cmd[26] |= ((uint32_t)setup->attr.dvdx >> 16) & 0xffffU;
  cmd[30] |= (uint32_t)setup->attr.dvdx & 0xffffU;
  cmd[27] |= (uint32_t)setup->attr.dwdx & 0xffff0000U;
  cmd[31] |= ((uint32_t)setup->attr.dwdx << 16) & 0xffff0000U;
  // dS/de, dT/de, dW/de
  cmd[32] |= (uint32_t)setup->attr.dude & 0xffff0000U;
  cmd[36] |= ((uint32_t)setup->attr.dude << 16) & 0xffff0000U;
  cmd[32] |= ((uint32_t)setup->attr.dvde >> 16) & 0xffffU;
  cmd[36] |= (uint32_t)setup->attr.dvde & 0xffffU;
  cmd[33] |= (uint32_t)setup->attr.dwde & 0xffff0000U;
  cmd[37] |= ((uint32_t)setup->attr.dwde << 16) & 0xffff0000U;
  // dS/dy, dT/dy, dW/dy
  cmd[34] |= (uint32_t)setup->attr.dudy & 0xffff0000U;
  cmd[38] |= ((uint32_t)setup->attr.dudy << 16) & 0xffff0000U;
  cmd[34] |= ((uint32_t)setup->attr.dvdy >> 16) & 0xffffU;
  cmd[38] |= (uint32_t)setup->attr.dvdy & 0xffffU;
  cmd[35] |= (uint32_t)setup->attr.dwdy & 0xffff0000U;
  cmd[39] |= ((uint32_t)setup->attr.dwdy << 16) & 0xffff0000U;

  cmd[40] = (uint32_t)setup->attr.z;
  cmd[41] = (uint32_t)setup->attr.dzdx;
  cmd[42] = (uint32_t)setup->attr.dzde;
  cmd[43] = (uint32_t)setup->attr.dzdy;

  emit_words(sink, cmd, 44);
}

// --- Image / framebuffer -----------------------------------------------------

void emit_set_color_image(struct CmdSink *sink, uint32_t fmt, uint32_t size,
                          uint32_t addr, uint32_t width_pixels) {
  uint32_t cmd[2] = {0, 0};
  cmd[0] |= (uint32_t)EMIT_OP_SET_COLOR_IMAGE << 24;
  cmd[0] |= fmt << 21;
  cmd[0] |= size << 19;
  cmd[0] |= (width_pixels - 1U) & 1023U;
  cmd[1] = addr;
  emit_words(sink, cmd, 2);
}

void emit_set_depth_image(struct CmdSink *sink, uint32_t addr) {
  uint32_t cmd[2] = {0, 0};
  cmd[0] |= (uint32_t)EMIT_OP_SET_MASK_IMAGE << 24;
  cmd[1] = addr;
  emit_words(sink, cmd, 2);
}

void emit_set_texture_image(struct CmdSink *sink, uint32_t fmt, uint32_t size,
                            uint32_t addr, uint32_t width_pixels) {
  uint32_t cmd[2] = {0, 0};
  cmd[0] |= (uint32_t)EMIT_OP_SET_TEXTURE_IMAGE << 24;
  cmd[0] |= fmt << 21;
  cmd[0] |= size << 19;
  cmd[0] |= (width_pixels - 1U) & 0x3ffU;
  cmd[1] |= addr & 0x00ffffffU;
  emit_words(sink, cmd, 2);
}

// --- Render state ------------------------------------------------------------

void emit_set_combine(struct CmdSink *sink, uint32_t combine_hi,
                      uint32_t combine_lo) {
  uint32_t cmd[2];
  cmd[0] = ((uint32_t)EMIT_OP_SET_COMBINE << 24) | (combine_hi & 0x00ffffffU);
  cmd[1] = combine_lo;
  emit_words(sink, cmd, 2);
}

void emit_set_other_modes(struct CmdSink *sink,
                          const struct DemoOtherModes *modes) {
  uint32_t cmd[2] = {0, 0};
  cmd[0] |= (uint32_t)EMIT_OP_SET_OTHER_MODES << 24;
  cmd[0] |= modes->cycle_type << 20;
  if (modes->perspective) {
    cmd[0] |= 1U << 19;
  }
  if (modes->TLUT) {
    cmd[0] |= 1U << 15;
  }
  if (modes->tlut_ia_type) {
    cmd[0] |= 1U << 14;
  }
  if (modes->sample_quad) {
    cmd[0] |= 1U << 13;
  }
  if (modes->bilerp_0) {
    cmd[0] |= 1U << 11;
  }
  if (modes->bilerp_1) {
    cmd[0] |= 1U << 10;
  }
  cmd[0] |= (modes->rgb_dither & 3U) << 6;
  cmd[0] |= (modes->alpha_dither & 3U) << 4;

  // Blender / z / coverage default to 0 (the oracle's default blender enums all
  // encode 0, ZMode::Opaque=0, CoverageMode::Clamp=0).
  if (modes->blend_en) {
    cmd[1] |= 1U << 14;
  }
  if (modes->z_update) {
    cmd[1] |= 1U << 5;
  }
  if (modes->z_compare) {
    cmd[1] |= 1U << 4;
  }
  if (modes->aa) {
    cmd[1] |= 1U << 3;
  }
  if (modes->z_source_prim) {
    cmd[1] |= 1U << 2;
  }
  if (modes->alpha_compare) {
    cmd[1] |= 1U << 0;
  }
  emit_words(sink, cmd, 2);
}

// --- Tile / texture load -----------------------------------------------------

void emit_set_tile(struct CmdSink *sink, uint32_t tile,
                   const struct DemoTileDesc *desc) {
  uint32_t cmd[2] = {0, 0};
  cmd[0] |= (uint32_t)EMIT_OP_SET_TILE << 24;

  cmd[1] |= (desc->shift_s & 0xfU) << 0;
  cmd[1] |= (desc->mask_s & 0xfU) << 4;
  cmd[1] |= (uint32_t)((desc->flags & DEMO_TILE_MIRROR_S_BIT) != 0) << 8;
  cmd[1] |= (uint32_t)((desc->flags & DEMO_TILE_CLAMP_S_BIT) != 0) << 9;
  cmd[1] |= (desc->shift_t & 0xfU) << 10;
  cmd[1] |= (desc->mask_t & 0xfU) << 14;
  cmd[1] |= (uint32_t)((desc->flags & DEMO_TILE_MIRROR_T_BIT) != 0) << 18;
  cmd[1] |= (uint32_t)((desc->flags & DEMO_TILE_CLAMP_T_BIT) != 0) << 19;
  cmd[1] |= (desc->palette & 0xfU) << 20;
  cmd[1] |= (tile & 7U) << 24;

  // offset/stride are bytes, stored as 64-bit words (>>3); both 8-byte aligned.
  cmd[0] |= (desc->offset >> 3) << 0;
  cmd[0] |= (desc->stride >> 3) << 9;
  cmd[0] |= desc->size << 19;
  cmd[0] |= desc->fmt << 21;
  emit_words(sink, cmd, 2);
}

// Shared lower-right-corner (-1 pixel == -4 subpixels) rect packer used by
// SET_TILE_SIZE / LOAD_TILE. x/y/width/height are PIXELS.
static void emit_tile_rect(struct CmdSink *sink, uint32_t op, uint32_t tile,
                           uint32_t x, uint32_t y, uint32_t width,
                           uint32_t height) {
  uint32_t xs = x << 2;
  uint32_t ys = y << 2;
  uint32_t ws = width << 2;
  uint32_t hs = height << 2;
  uint32_t sl = xs & 0xfffU;
  uint32_t tl = ys & 0xfffU;
  uint32_t sh = (xs + ws - 4U) & 0xfffU;
  uint32_t th = (ys + hs - 4U) & 0xfffU;
  uint32_t cmd[2] = {0, 0};
  cmd[0] |= op << 24;
  cmd[1] |= tile << 24;
  cmd[0] |= sl << 12;
  cmd[0] |= tl << 0;
  cmd[1] |= sh << 12;
  cmd[1] |= th << 0;
  emit_words(sink, cmd, 2);
}

void emit_set_tile_size(struct CmdSink *sink, uint32_t tile, uint32_t x,
                        uint32_t y, uint32_t width, uint32_t height) {
  emit_tile_rect(sink, EMIT_OP_SET_TILE_SIZE, tile, x, y, width, height);
}

void emit_load_tile(struct CmdSink *sink, uint32_t tile, uint32_t x, uint32_t y,
                    uint32_t width, uint32_t height) {
  emit_tile_rect(sink, EMIT_OP_LOAD_TILE, tile, x, y, width, height);
}

void emit_load_tlut(struct CmdSink *sink, uint32_t tile, uint32_t x, uint32_t y,
                    uint32_t width, uint32_t height) {
  // LOAD_TLUT biases the lower-right corner by -1 PIXEL before <<2 (not -4
  // subpixels), matching the oracle's load_tlut().
  uint32_t cmd[2] = {0, 0};
  cmd[0] |= (uint32_t)EMIT_OP_LOAD_TLUT << 24;
  cmd[1] |= tile << 24;
  uint32_t sl = (x << 2) & 0xfffU;
  uint32_t tl = (y << 2) & 0xfffU;
  uint32_t sh = ((x + width - 1U) << 2) & 0xfffU;
  uint32_t th = ((y + height - 1U) << 2) & 0xfffU;
  cmd[0] |= sl << 12;
  cmd[0] |= tl << 0;
  cmd[1] |= sh << 12;
  cmd[1] |= th << 0;
  emit_words(sink, cmd, 2);
}

// --- Clear / fill ------------------------------------------------------

void emit_set_fill_color(struct CmdSink *sink, uint32_t color) {
  uint32_t cmd[2] = {0, 0};
  cmd[0] |= (uint32_t)EMIT_OP_SET_FILL_COLOR << 24;
  cmd[1] = color;
  emit_words(sink, cmd, 2);
}

void emit_set_scissor(struct CmdSink *sink, uint32_t x, uint32_t y,
                      uint32_t width, uint32_t height) {
  uint32_t xh = x << 2;
  uint32_t yh = y << 2;
  uint32_t xl = (x + width) << 2;
  uint32_t yl = (y + height) << 2;
  uint32_t cmd[2] = {0, 0};
  cmd[0] |= (uint32_t)EMIT_OP_SET_SCISSOR << 24;
  cmd[0] |= (xh & 0xfffU) << 12;
  cmd[0] |= (yh & 0xfffU) << 0;
  cmd[1] |= (xl & 0xfffU) << 12;
  cmd[1] |= (yl & 0xfffU) << 0;
  emit_words(sink, cmd, 2);
}

void emit_fill_rectangle(struct CmdSink *sink, uint32_t x, uint32_t y,
                         uint32_t width, uint32_t height) {
  uint32_t xs = x << 2;
  uint32_t ys = y << 2;
  uint32_t ws = width << 2;
  uint32_t hs = height << 2;
  uint32_t cmd[2] = {0, 0};
  cmd[0] |= (uint32_t)EMIT_OP_FILL_RECTANGLE << 24;
  cmd[0] |= ((xs + ws - 4U) & 0xfffU) << 12;
  cmd[1] |= (xs & 0xfffU) << 12;
  cmd[0] |= ((ys + hs - 4U) & 0xfffU) << 0;
  cmd[1] |= (ys & 0xfffU) << 0;
  emit_words(sink, cmd, 2);
}

// --- Sync  -------------------------------------------------------------------

static void emit_sync(struct CmdSink *sink, uint32_t op) {
  uint32_t cmd[2] = {0, 0};
  cmd[0] = op << 24;
  emit_words(sink, cmd, 2);
}

void emit_sync_full(struct CmdSink *sink) {
  emit_sync(sink, EMIT_OP_SYNC_FULL);
}
void emit_sync_pipe(struct CmdSink *sink) {
  emit_sync(sink, EMIT_OP_SYNC_PIPE);
}
void emit_sync_tile(struct CmdSink *sink) {
  emit_sync(sink, EMIT_OP_SYNC_TILE);
}
void emit_sync_load(struct CmdSink *sink) {
  emit_sync(sink, EMIT_OP_SYNC_LOAD);
}
