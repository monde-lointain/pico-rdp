// Locks the viewer's Memory->TMEM CI4 preview decode
// (src/viewer/tmem_preview.cc) against the renderer's actual sample-tile
// addressing.
//
// The viewer re-implements the CI4+TLUT decode to preview what's resident in
// TMEM. It must match the renderer's CI4 fetch (src/rdp/rdp/tmem.cc): the TLUT
// entry for index k is replicated 4x across its 64-bit slot (TLUT[k<<2]), and
// the per-texel byte address carries the t-parity load swizzle (^3 even rows,
// ^7 odd rows under LSB_FIRST). A stride-1 TLUT read collapses the palette to 4
// colours; a missing XOR scrambles texels in groups of 8 -- both produce a
// wrong preview while the rendered framebuffer (which uses the real fetch)
// stays correct.
//
// Strategy: drive the demo's real frame 0 (LOAD_TLUT + LOAD_TILE) through OUR
// renderer, decode the live (swizzled) TMEM with the subject under test, and
// compare against an independent oracle that decodes the *source* arrows tables
// in plain logical layout. Agreement proves the subject's addressing inverts
// the load swizzle exactly.
//
// Not orthodox-enforced (gtest / C++ idioms). TLUT read type-puns uint8_t* ->
// uint16_t*; built with -fno-strict-aliasing (see CMakeLists).

#include "tmem_preview.h"  // subject under test

#include <gtest/gtest.h>
#include <stdint.h>

#include "cmd_sink.h"
#include "demo.h"
#include "generated/arrows_ci4.h"  // arrows_ci4[], arrows_tlut[]
#include "rdp_host_fixture.h"      // RdpHostFixture, feed_emit, n64video.h

extern "C" {
// Harness hook compiled into rdp_core but not in the public header.
uint8_t *rdpx_get_tmem(void);
}

namespace {

// Independent oracle copy of the panel's RGBA5551 -> RGBA8 (R,G,B,A byte
// order).
uint32_t oracle_rgba5551_to_rgba8(uint16_t p) {
  uint32_t r = (p >> 11) & 0x1f;
  uint32_t g = (p >> 6) & 0x1f;
  uint32_t b = (p >> 1) & 0x1f;
  uint32_t const a = 0xff;
  r = (r << 3) | (r >> 2);
  g = (g << 3) | (g >> 2);
  b = (b << 3) | (b >> 2);
  return r | (g << 8) | (b << 16) | (a << 24);
}

}  // namespace

// Subject (live swizzled TMEM, decoded by tmem_preview.cc) must equal the
// oracle (source arrows tables in plain logical layout).
TEST(DemoTmemPreview, Frame0DecodeMatchesSourceArrows) {
  RdpHostFixture fx;
  rdp_host_fixture_init(&fx, RDRAM_MAX_SIZE);

  demo_init(fx.rdram.data(), (uint32_t)fx.rdram.size());

  CmdSink sink;
  sink.emit = feed_emit;
  sink.ctx = nullptr;
  demo_build_frame(0, &sink);

  // Subject: decode the renderer's live TMEM.
  static uint32_t got[DEMO_CI4_TEX_WIDTH * DEMO_CI4_TEX_HEIGHT];
  tmem_decode_ci4_preview(rdpx_get_tmem(), got);

  // Oracle: decode the source CI4 + TLUT in plain logical layout (32 B/row,
  // hi-nibble first, direct 16-entry palette -- no swizzle, no replication).
  for (int y = 0; y < DEMO_CI4_TEX_HEIGHT; ++y) {
    for (int x = 0; x < DEMO_CI4_TEX_WIDTH; ++x) {
      uint8_t const byte = arrows_ci4[((size_t)y * 32U) + (size_t)(x >> 1)];
      uint8_t const idx = (x & 1) ? (byte & 0x0f) : (byte >> 4);
      uint32_t const want = oracle_rgba5551_to_rgba8(arrows_tlut[idx & 0x0f]);
      uint32_t const have = got[((size_t)y * DEMO_CI4_TEX_WIDTH) + x];
      ASSERT_EQ(have, want) << "preview mismatch at (" << x << "," << y << ")";
    }
  }

  rdp_host_fixture_close(&fx);
}
