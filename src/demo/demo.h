#pragma once

// demo_core public API.
//
// demo_core is the portable geometry frontend (mini-"RSP"): it turns the
// fixed-point demo scene (rotating Gouraud pyramid + arrows-textured cube,
// Z-buffered) into a raw RDP command word stream. It is built for BOTH host
// and device (RP2040 / M0+), so it has no malloc / printf / host-only deps:
// it works entirely on the RDRAM arena passed to demo_init, with fixed buffers
// and static-const baked tables.
//
// Consumers (viewer, tests) are C++; the renderer ABI in n64video.h uses C
// linkage, so this header does too via extern "C" guards.

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

struct CmdSink;  // forward decl; see cmd_sink.h

// --- Framebuffer / VI contract -------------------------------------------
//
// Fixed 240x240 square framebuffer (1:1 projection aspect, for device parity).
// Pixel format is RGBA5551 (16-bit). The viewer programs the VI registers
// (VI_ORIGIN/WIDTH/scale) from these values to scan out the color FB at
// exactly 240x240.
#define DEMO_FB_WIDTH 240
#define DEMO_FB_HEIGHT 240
#define DEMO_FB_BPP 2  // bytes per pixel, RGBA5551

// Pixel format tag for the color framebuffer.
enum DemoPixelFormat { DEMO_PIXFMT_RGBA5551 = 0 };

// --- RDRAM arena layout ---------------------------------------------------
//
// demo_init lays the arena out as a set of byte-offset segments from the base
// of the RDRAM passed in. Addresses below are RDRAM byte offsets (the values
// programmed into RDP SET_*_IMAGE commands and the VI). All segments are
// 8-byte aligned (RDP color/depth images want 64-bit alignment).
//
//   color FB:    DEMO_RDRAM_COLOR_FB_ADDR / _SIZE  (115200 bytes)
//   Z buffer:    DEMO_RDRAM_Z_BUF_ADDR    / _SIZE  (115200 bytes)
//   CI4 texture: DEMO_RDRAM_CI4_TEX_ADDR  / _SIZE  (2048 bytes)
//   TLUT:        DEMO_RDRAM_TLUT_ADDR     / _SIZE  (32 bytes)
//   end marker:  DEMO_RDRAM_USED_END  (arena size must be >= this)
//
// The CI4 texture (64x64, 4-bit indices) and the 16-entry RGBA5551 TLUT are
// seeded into RDRAM by demo_init (the renderer DMAs them into TMEM from RDRAM;
// commands only reference these addresses). All host writes into / reads out
// of RDRAM must apply the RDRAM byte-address XOR (BYTE_ADDR_XOR=3); a plain
// memcpy reads back scrambled.

// Color framebuffer: 240*240*2 = 115200, aligned to 8.
#define DEMO_RDRAM_COLOR_FB_ADDR 0x00000000u
#define DEMO_RDRAM_COLOR_FB_SIZE \
  (DEMO_FB_WIDTH * DEMO_FB_HEIGHT * DEMO_FB_BPP)  // 115200

// Z buffer: same dimensions / 16-bit, immediately after the color FB.
// 115200 is a multiple of 8, so no extra padding is needed.
#define DEMO_RDRAM_Z_BUF_ADDR \
  (DEMO_RDRAM_COLOR_FB_ADDR + DEMO_RDRAM_COLOR_FB_SIZE)  // 0x0001C200
#define DEMO_RDRAM_Z_BUF_SIZE (DEMO_FB_WIDTH * DEMO_FB_HEIGHT * 2)  // 115200

// CI4 texture: 64x64 4-bit indices = 2048 bytes (the low 2 KB of TMEM).
#define DEMO_RDRAM_CI4_TEX_ADDR \
  (DEMO_RDRAM_Z_BUF_ADDR + DEMO_RDRAM_Z_BUF_SIZE)  // 0x00038400
#define DEMO_CI4_TEX_WIDTH 64
#define DEMO_CI4_TEX_HEIGHT 64
#define DEMO_RDRAM_CI4_TEX_SIZE \
  ((DEMO_CI4_TEX_WIDTH * DEMO_CI4_TEX_HEIGHT) / 2)  // 2048 (4 bits/texel)

// TLUT: 16 RGBA5551 entries = 32 bytes, immediately after the CI4 texture.
#define DEMO_RDRAM_TLUT_ADDR \
  (DEMO_RDRAM_CI4_TEX_ADDR + DEMO_RDRAM_CI4_TEX_SIZE)  // 0x00038C00
#define DEMO_TLUT_ENTRIES 16
#define DEMO_RDRAM_TLUT_SIZE (DEMO_TLUT_ENTRIES * 2)  // 32

// First unused byte offset; the arena handed to demo_init must be at least
// this large.
#define DEMO_RDRAM_USED_END \
  (DEMO_RDRAM_TLUT_ADDR + DEMO_RDRAM_TLUT_SIZE)  // 0x00038C20

// --- Animation ------------------------------------------------------------
//
// No runtime trig: per-frame model matrices are baked at build time. The
// pyramid loops every 30 frames, the cube every 40; the whole scene repeats
// every DEMO_ANIM_PERIOD_FRAMES (lcm(30,40) = 120). demo_build_frame is a pure
// function of (frame_index mod DEMO_ANIM_PERIOD_FRAMES).
#define DEMO_ANIM_PERIOD_FRAMES 120

// --- API ------------------------------------------------------------------

// Lay out the RDRAM arena and seed the static assets (CI4 texture + RGBA5551
// TLUT) into it, applying the RDRAM byte-address XOR. rdram points at a caller-
// owned buffer of rdram_size bytes; rdram_size must be >= DEMO_RDRAM_USED_END.
void demo_init(uint8_t *rdram, uint32_t rdram_size);

// Emit one complete frame (clears, render state, draws, sync) for the given
// frame index, one RDP command at a time, into sink. References the asset
// addresses seeded by demo_init. No internal animation state beyond the index.
void demo_build_frame(uint32_t frame_index, struct CmdSink *sink);

// --- Accessors (for programming the VI) -----------------------------------

// RDRAM byte address of the color framebuffer (VI_ORIGIN source).
uint32_t demo_color_fb_addr(void);
// Framebuffer dimensions in pixels.
uint32_t demo_fb_width(void);
uint32_t demo_fb_height(void);
// Color framebuffer pixel format (see enum DemoPixelFormat).
uint32_t demo_fb_format(void);
// Total animation period in frames.
uint32_t demo_anim_period_frames(void);

#ifdef __cplusplus
}  // extern "C"
#endif
