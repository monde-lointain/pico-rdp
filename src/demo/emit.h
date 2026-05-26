#pragma once

// demo_core RDP command emitter.
//
// Turns the demo's fixed-point triangle setup IR (struct DemoPrimSetup) and the
// surrounding render state into RAW RDP command words, emitted one command at a
// time into a CmdSink. The word stream is bit-for-bit what the N64 RDP consumes:
// each command is N 32-bit words, word[0] carrying the high 32 bits of the
// 64-bit command (so word[0] holds the 6-bit command id in bits 31..24).
//
// The triangle encoding is bit-exact with the vendored test oracle
// RDP::CommandBuilder::submit_clipped_primitive (tests/conformance/vendor/
// rdp_command_builder.cpp): same word layout, same field masks. The non-
// triangle helpers mirror the same oracle's packing for the commands the demo
// needs (image/state/tile/load/clear/sync).
//
// Orthodox / freestanding: plain C-style functions, fixed stack buffers, no
// heap, no printf, no C++ runtime. Usable from RP2040 device code.
//
// Field semantics that differ from a naive raw command and matter to callers:
//   - emit_set_color_image / emit_set_texture_image take WIDTH IN PIXELS; the
//     command stores (width - 1).
//   - emit_set_tile takes the line stride and TMEM offset in BYTES; the command
//     stores them in 64-bit words (>>3). Both must be 8-byte aligned.
//   - rectangle / tile-size / load-tile coordinates are in PIXELS; the command
//     stores them in 1/4-pixel subpixels (x<<2) with the lower-right corner
//     biased by -4 subpixels (-1 pixel) per the RDP inclusive-edge convention.
//   - emit_load_tlut x/y/width/height are in PIXELS; converted to subpixels as
//     the oracle does (lower-right corner biased by -1 pixel before <<2).

#include <stdint.h>

#include "cmd_sink.h"
#include "setup_ir.h"

#ifdef __cplusplus
extern "C" {
#endif

// --- Enums mirroring the RDP register field encodings (and the oracle) -------

// SET_*_IMAGE / SET_TILE format field (3 bits).
enum DemoTexFormat {
  DEMO_TEXFMT_RGBA = 0,
  DEMO_TEXFMT_YUV = 1,
  DEMO_TEXFMT_CI = 2,
  DEMO_TEXFMT_IA = 3,
  DEMO_TEXFMT_I = 4
};

// SET_*_IMAGE / SET_TILE size field (2 bits): bits per texel.
enum DemoTexSize {
  DEMO_TEXSIZE_4BPP = 0,
  DEMO_TEXSIZE_8BPP = 1,
  DEMO_TEXSIZE_16BPP = 2,
  DEMO_TEXSIZE_32BPP = 3
};

// SET_OTHER_MODES cycle-type field (2 bits, bits 53..52 -> word0 21..20).
enum DemoCycleType {
  DEMO_CYCLE_1 = 0,
  DEMO_CYCLE_2 = 1,
  DEMO_CYCLE_COPY = 2,
  DEMO_CYCLE_FILL = 3
};

// SET_TILE clamp/mirror flag bits (per-axis), matching the oracle TileMeta.
enum DemoTileFlagBits {
  DEMO_TILE_MIRROR_S_BIT = 1 << 0,
  DEMO_TILE_CLAMP_S_BIT = 1 << 1,
  DEMO_TILE_MIRROR_T_BIT = 1 << 2,
  DEMO_TILE_CLAMP_T_BIT = 1 << 3
};

// SET_TILE descriptor. shift_s/shift_t are the 4-bit shift fields, mask_s/mask_t
// the 4-bit wrap-mask fields, palette the 4-bit CI palette index. stride/offset
// are TMEM line stride / tile base IN BYTES (both must be 8-byte aligned).
struct DemoTileDesc {
  uint32_t fmt;     // enum DemoTexFormat
  uint32_t size;    // enum DemoTexSize
  uint32_t stride;  // bytes, multiple of 8
  uint32_t offset;  // bytes, multiple of 8
  uint32_t palette;
  uint32_t shift_s, shift_t;
  uint32_t mask_s, mask_t;
  uint32_t flags;  // enum DemoTileFlagBits
};

// SET_OTHER_MODES is a wide register; the demo only varies a small set of bits.
// This descriptor carries exactly those, all others emit as 0 (matching the
// oracle's default flush_default_state for an all-false / Cycle1 state, except
// the two blender cycles which the oracle defaults non-zero — see emit.c).
struct DemoOtherModes {
  uint32_t cycle_type;   // enum DemoCycleType
  uint32_t rgb_dither;   // 2-bit RGB dither mode (3 = Off)
  uint32_t alpha_dither; // 2-bit alpha dither mode (3 = Off)
  int perspective;       // tex perspective correction
  int tlut;             // TLUT enable
  int tlut_ia_type;     // TLUT IA (vs RGBA)
  int sample_quad;      // 2x2 bilinear sample
  int bilerp_0, bilerp_1;
  int z_compare;     // depth test
  int z_update;      // depth write
  int aa;            // antialias / coverage
  int z_source_prim; // primitive depth source
  int alpha_compare; // alpha test
  int blend_en;      // force blend enable
};

// --- Triangle -----------------------------------------------------------------

// Emit one SHADE_TEXTURE_Z_BUFFER_TRIANGLE (0x0f) command: 44 words, bit-exact
// with RDP::CommandBuilder::submit_clipped_primitive for the same setup. Does
// NOT emit a SET_OTHER_MODES first (the oracle's submit_clipped_primitive does;
// the demo manages render state explicitly, so callers emit modes themselves).
void emit_triangle(struct CmdSink *sink, const struct DemoPrimSetup *setup);

// --- Image / framebuffer ------------------------------------------------------

void emit_set_color_image(struct CmdSink *sink, uint32_t fmt, uint32_t size,
                          uint32_t addr, uint32_t width_pixels);
void emit_set_depth_image(struct CmdSink *sink, uint32_t addr);
void emit_set_texture_image(struct CmdSink *sink, uint32_t fmt, uint32_t size,
                            uint32_t addr, uint32_t width_pixels);

// --- Render state -------------------------------------------------------------

// SET_COMBINE (0x3c). Takes the two packed 32-bit combiner words directly; the
// demo bakes its (few) combiner modes as constants, so this stays a thin pass-
// through rather than re-deriving the 16-subfield encoding.
void emit_set_combine(struct CmdSink *sink, uint32_t combine_hi,
                      uint32_t combine_lo);

void emit_set_other_modes(struct CmdSink *sink,
                          const struct DemoOtherModes *modes);

// --- Tile / texture load ------------------------------------------------------

void emit_set_tile(struct CmdSink *sink, uint32_t tile,
                   const struct DemoTileDesc *desc);
void emit_set_tile_size(struct CmdSink *sink, uint32_t tile, uint32_t x,
                        uint32_t y, uint32_t width, uint32_t height);
void emit_load_tile(struct CmdSink *sink, uint32_t tile, uint32_t x, uint32_t y,
                    uint32_t width, uint32_t height);
void emit_load_tlut(struct CmdSink *sink, uint32_t tile, uint32_t x, uint32_t y,
                    uint32_t width, uint32_t height);

// --- Clear / fill -------------------------------------------------------------

// SET_FILL_COLOR (0x37): packed 32-bit fill value (two RGBA5551 pixels, or a
// 32-bit RGBA8888 pixel) passed through unchanged.
void emit_set_fill_color(struct CmdSink *sink, uint32_t color);

// SET_SCISSOR (0x2d): clip rect in PIXELS (stored as subpixels x<<2). No
// interlace.
void emit_set_scissor(struct CmdSink *sink, uint32_t x, uint32_t y,
                      uint32_t width, uint32_t height);

// FILL_RECTANGLE (0x36): rect in PIXELS.
void emit_fill_rectangle(struct CmdSink *sink, uint32_t x, uint32_t y,
                         uint32_t width, uint32_t height);

// --- Sync ---------------------------------------------------------------------

void emit_sync_full(struct CmdSink *sink);
void emit_sync_pipe(struct CmdSink *sink);
void emit_sync_tile(struct CmdSink *sink);
void emit_sync_load(struct CmdSink *sink);

#ifdef __cplusplus
}  // extern "C"
#endif
