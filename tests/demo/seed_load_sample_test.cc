// Stream D.1: CI4 texture + TLUT seed/load end-to-end.
//
// Locks the BYTE_ADDR_XOR seeding path AND the CI4-via-TLUT load path against
// the committed arrows palette, in two layers (no external golden needed — the
// expected values ARE the generated arrows_ci4 / arrows_tlut tables, which are
// themselves checked in under src/demo/generated):
//
//   1. SEEDING (locks demo_init + rdram_io BYTE_ADDR_XOR=3): demo_init seeds
//   the
//      64x64 CI4 texture (byte block) and the 16-entry RGBA5551 TLUT (native
//      big-endian halfwords) into the RDRAM arena. We read both straight back
//      through the XOR-correct rdram_io helpers and assert they recover the
//      generated source tables byte-for-byte.
//
//   2. LOAD PATH (locks LOAD_TLUT end-to-end through OUR renderer): we drive
//   the
//      demo's full frame 0 (which includes the texture-load command sequence)
//      through rdp_core, then read TMEM via the RDPX_TESTING accessor and
//      assert the 16 arrows palette entries landed at the TLUT base
//      (tmem[0x800]). On N64 LOAD_TLUT replicates each 16-bit entry 4x within
//      its 64-bit slot, so entry k sits at TMEM16[0x400 + k*16] (verified
//      against rdp_core's own tmem.c TLUT base + the empirically-confirmed
//      replication stride).
//
// Not orthodox-enforced (gtest / C++ idioms). RDRAM byte aliasing => built with
// -fno-strict-aliasing (see CMakeLists).

#include <gtest/gtest.h>
#include <stdint.h>
#include <string.h>

#include <vector>

#include "cmd_sink.h"
#include "demo.h"
#include "generated/arrows_ci4.h"  // arrows_ci4[], arrows_tlut[]
#include "rdram_io.h"

extern "C" {
#include "n64video.h"

// Harness hooks compiled into rdp_core but not in the public header.
void rdpx_rdp_cmd(uint32_t wid, const uint32_t *args);
uint8_t *rdpx_get_tmem(void);
}

namespace {

// LOAD_TLUT TMEM layout (rdp_core tmem.c): TLUT base is byte 0x800 => uint16
// index 0x400. Each 16-bit entry is replicated across its 64-bit slot (4 u16),
// so entry k is at TMEM16[0x400 + k*16].
const uint32_t kTlutBaseU16 = 0x800u / 2u;  // 0x400
const uint32_t kTlutStrideU16 = 16u;        // one 64-bit slot per entry, 4x rep

void feed_emit(void *ctx, const uint32_t *words, uint32_t n) {
  (void)ctx;
  (void)n;
  rdpx_rdp_cmd(0, words);
}

// SYNC_FULL calls mi_intr_cb() unconditionally (rdp.c) — must be non-null.
void mi_intr_noop(void) {}

}  // namespace

// Layer 1: demo_init seeds the CI4 texture + TLUT under BYTE_ADDR_XOR; the
// rdram_io readback recovers the generated source tables exactly.
TEST(DemoSeedLoadSample, SeedingRoundTripsThroughByteXor) {
  std::vector<uint8_t> rdram(DEMO_RDRAM_USED_END, 0);
  demo_init(rdram.data(), (uint32_t)rdram.size());

  // CI4 texture: 2048 nibble-packed bytes.
  uint8_t ci4_back[DEMO_RDRAM_CI4_TEX_SIZE];
  rdram_read_block(rdram.data(), DEMO_RDRAM_CI4_TEX_ADDR, ci4_back,
                   DEMO_RDRAM_CI4_TEX_SIZE);
  ASSERT_EQ((uint32_t)ARROWS_CI4_BYTES, (uint32_t)DEMO_RDRAM_CI4_TEX_SIZE);
  for (uint32_t i = 0; i < DEMO_RDRAM_CI4_TEX_SIZE; ++i) {
    ASSERT_EQ(ci4_back[i], arrows_ci4[i])
        << "CI4 byte " << i << " mismatch after seed";
  }

  // TLUT: 16 RGBA5551 entries, stored as native big-endian halfwords.
  for (uint32_t i = 0; i < DEMO_TLUT_ENTRIES; ++i) {
    uint16_t got = rdram_read16(rdram.data(), DEMO_RDRAM_TLUT_ADDR + i * 2u);
    ASSERT_EQ(got, arrows_tlut[i]) << "TLUT entry " << i << " mismatch";
  }
}

// Layer 2: the demo's texture-load command sequence (run through OUR renderer)
// lands the arrows palette in TMEM at the TLUT base.
TEST(DemoSeedLoadSample, Frame0LoadsArrowsPaletteIntoTmem) {
  std::vector<uint8_t> rdram(RDRAM_MAX_SIZE, 0);

  uint32_t vi_regs[VI_NUM_REG];
  uint32_t dp_regs[DP_NUM_REG];
  memset(vi_regs, 0, sizeof(vi_regs));
  memset(dp_regs, 0, sizeof(dp_regs));
  uint32_t irq = 0;
  uint32_t *p_vi[VI_NUM_REG];
  uint32_t *p_dp[DP_NUM_REG];
  for (uint32_t i = 0; i < VI_NUM_REG; ++i) p_vi[i] = &vi_regs[i];
  for (uint32_t i = 0; i < DP_NUM_REG; ++i) p_dp[i] = &dp_regs[i];

  struct N64videoConfig config;
  memset(&config, 0, sizeof(config));
  config.gfx.rdram = rdram.data();
  config.gfx.rdram_size = (uint32_t)rdram.size();
  config.gfx.vi_reg = p_vi;
  config.gfx.dp_reg = p_dp;
  config.gfx.mi_intr_reg = &irq;
  config.gfx.mi_intr_cb = mi_intr_noop;
  config.vi.mode = VI_MODE_NORMAL;
  config.vi.interp = VI_INTERP_LINEAR;
  config.dp.compat = DP_COMPAT_HIGH;
  rdpx_video_init(&config);

  demo_init(rdram.data(), (uint32_t)rdram.size());

  CmdSink sink;
  sink.emit = feed_emit;
  sink.ctx = nullptr;
  demo_build_frame(0, &sink);

  const uint16_t *TMEM16 = (const uint16_t *)rdpx_get_tmem();
  for (uint32_t k = 0; k < DEMO_TLUT_ENTRIES; ++k) {
    uint16_t got = TMEM16[kTlutBaseU16 + k * kTlutStrideU16];
    ASSERT_EQ(got, arrows_tlut[k])
        << "TLUT entry " << k << " not in TMEM after LOAD_TLUT";
  }

  rdpx_video_close();
}
