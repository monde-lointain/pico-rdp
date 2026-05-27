// Shared host-side RDP test scaffolding.
//
// Several host tests spin up rdp_core over a caller-owned, zeroed RDRAM arena
// with the same nine N64videoConfig fields. This collects that boilerplate in
// one place so a config-field change is a single edit, not shotgun surgery
// across the demo / perf / conformance test suites.
//
// Host-only test infrastructure (NOT orthodox-enforced; C++/gtest idioms).
#pragma once

#include <stddef.h>
#include <stdint.h>

#include <vector>

extern "C" {
#include "n64video.h"

// Per-command dispatch entry — the harness hook compiled into rdp_core but not
// in the public header (mirrors renderer_host.cc / replayer_driver_ours.cpp).
void rdpx_rdp_cmd(uint32_t wid, const uint32_t *args);
}

// SYNC_FULL calls config.gfx.mi_intr_cb() unconditionally (rdp.c), so it must
// be non-null. Single shared no-op definition.
void mi_intr_noop(void);

// CmdSink emit callback: forward one complete command straight into rdp_core.
// Each emit is one complete command, so the word count is ignored.
void feed_emit(void *ctx, const uint32_t *words, uint32_t n);

// Fill the nine settable N64videoConfig fields the host tests share. Caller
// owns all storage (rdram, register arrays, irq); cfg is fully zeroed first.
void rdp_host_config_init(struct N64videoConfig *cfg, uint8_t *rdram,
                          uint32_t rdram_size, uint32_t **p_vi, uint32_t **p_dp,
                          uint32_t *irq);

// Self-contained host fixture for tests that just need a live rdp_core over a
// zeroed RDRAM arena. init() resizes RDRAM, wires the register pointer arrays,
// fills config, and calls rdpx_video_init(); close() calls rdpx_video_close().
// The caller reads/writes the arena through fx->rdram.
struct RdpHostFixture {
  std::vector<uint8_t> rdram;
  uint32_t vi_regs[VI_NUM_REG];
  uint32_t dp_regs[DP_NUM_REG];
  uint32_t *p_vi[VI_NUM_REG];
  uint32_t *p_dp[DP_NUM_REG];
  uint32_t irq;
  struct N64videoConfig config;
};

void rdp_host_fixture_init(struct RdpHostFixture *fx, size_t rdram_bytes);
void rdp_host_fixture_close(struct RdpHostFixture *fx);
