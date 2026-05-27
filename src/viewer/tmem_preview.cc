// tmem_preview.cc — see tmem_preview.h.
//
// The decode MUST mirror the renderer's CI4 sample-tile addressing
// (src/rdp/rdp/tmem.cc): the demo loads the arrows texture as 8bpp half-width
// (libultra's 4b-as-8b LOAD_TILE trick) but the cube samples it as CI4 4bpp via
// a tile with line = 32 bytes (stride 32 -> SET_TILE line field 32>>3 = 4). The
// per-texel byte address is therefore the same as the fetch's, with the TMEM
// load swizzle applied on top:
//   - base: addr = 32*t + (s>>1)         (sample tile line=4)
//   - swizzle: addr ^= (t & 1) ? BYTE_XOR_DWORD_SWAP : BYTE_ADDR_XOR
// and the TLUT entry for index k is replicated 4x across its 64-bit slot, so it
// sits at TLUT[k<<2] (TLUT = (uint16_t*)&tmem[0x800]).
//
// Built with -fno-strict-aliasing (TLUT read type-puns uint8_t* -> uint16_t*).

#include "tmem_preview.h"

#include <stddef.h>  // size_t

#include "n64video_common.h"  // BYTE_ADDR_XOR / BYTE_XOR_DWORD_SWAP (LSB_FIRST)

extern "C" {
#include "demo.h"  // DEMO_CI4_TEX_WIDTH / DEMO_CI4_TEX_HEIGHT
}

// RGBA5551 -> RGBA8888 (byte order R,G,B,A = SDL_PIXELFORMAT_RGBA32). Local
// copy; see tmem_preview.h rationale (intentionally not shared with
// panel_memory.cc).
static uint32_t rgba5551_to_rgba8(uint16_t p) {
  uint32_t r = (p >> 11) & 0x1f;
  uint32_t g = (p >> 6) & 0x1f;
  uint32_t b = (p >> 1) & 0x1f;
  uint32_t const a = 0xff;  // force opaque for display (5551 alpha bit ignored)
  r = (r << 3) | (r >> 2);
  g = (g << 3) | (g >> 2);
  b = (b << 3) | (b >> 2);
  return r | (g << 8) | (b << 16) | (a << 24);
}

void tmem_decode_ci4_preview(const uint8_t *tmem, uint32_t *out_rgba8) {
  const uint16_t *tlut = (const uint16_t *)(tmem + 0x800);  // tmem.c TLUT base
  for (int y = 0; y < DEMO_CI4_TEX_HEIGHT; ++y) {
    for (int x = 0; x < DEMO_CI4_TEX_WIDTH; ++x) {
      uint32_t addr =
          ((uint32_t)y * 32U) + (uint32_t)(x >> 1);  // CI4: 32 B/row
      addr ^=
          (y & 1) ? BYTE_XOR_DWORD_SWAP : BYTE_ADDR_XOR;  // load swizzle (7:3)
      uint8_t const byte = tmem[addr & 0xfffU];
      uint8_t const idx =
          (x & 1) ? (byte & 0x0f) : (byte >> 4);  // hi nibble 1st
      uint16_t const entry =
          tlut[(idx & 0x0f) << 2];  // 4x-replicated slot, bank 0
      out_rgba8[((size_t)y * DEMO_CI4_TEX_WIDTH) + x] =
          rgba5551_to_rgba8(entry);
    }
  }
}
