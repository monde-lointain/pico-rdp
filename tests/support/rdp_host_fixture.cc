// Shared host-side RDP test scaffolding. See rdp_host_fixture.h.

#include "rdp_host_fixture.h"

#include <string.h>

void mi_intr_noop(void) {}

void feed_emit(void *ctx, const uint32_t *words, uint32_t n) {
  (void)ctx;
  (void)n;
  rdpx_rdp_cmd(0, words);
}

void rdp_host_config_init(struct N64videoConfig *cfg, uint8_t *rdram,
                          uint32_t rdram_size, uint32_t **p_vi, uint32_t **p_dp,
                          uint32_t *irq) {
  memset(cfg, 0, sizeof *cfg);
  cfg->gfx.rdram = rdram;
  cfg->gfx.rdram_size = rdram_size;
  cfg->gfx.vi_reg = p_vi;
  cfg->gfx.dp_reg = p_dp;
  cfg->gfx.mi_intr_reg = irq;
  cfg->gfx.mi_intr_cb = mi_intr_noop;
  cfg->vi.mode = VI_MODE_NORMAL;
  cfg->vi.interp = VI_INTERP_LINEAR;
  cfg->dp.compat = DP_COMPAT_HIGH;
}

void rdp_host_fixture_init(struct RdpHostFixture *fx, size_t rdram_bytes) {
  fx->rdram.assign(rdram_bytes, 0);
  fx->irq = 0;
  memset(fx->vi_regs, 0, sizeof fx->vi_regs);
  memset(fx->dp_regs, 0, sizeof fx->dp_regs);
  for (uint32_t i = 0; i < VI_NUM_REG; ++i) fx->p_vi[i] = &fx->vi_regs[i];
  for (uint32_t i = 0; i < DP_NUM_REG; ++i) fx->p_dp[i] = &fx->dp_regs[i];
  rdp_host_config_init(&fx->config, fx->rdram.data(),
                       (uint32_t)fx->rdram.size(), fx->p_vi, fx->p_dp,
                       &fx->irq);
  rdpx_video_init(&fx->config);
}

void rdp_host_fixture_close(struct RdpHostFixture *fx) {
  (void)fx;
  rdpx_video_close();
}
