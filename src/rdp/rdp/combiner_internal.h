#pragma once

// combiner_internal.h — combiner pipeline exports (combiner.cc).
// rdp_set_* handlers collide with the oracle (rdpxi_-prefixed); the per-pixel
// helpers + init do not. De-inlined for cross-TU use (LTO inlines).

#include "rdp_internal.h"

void rdpxi_rdp_set_combine(uint32_t wid, const uint32_t* args);
void rdpxi_rdp_set_env_color(uint32_t wid, const uint32_t* args);
void rdpxi_rdp_set_prim_color(uint32_t wid, const uint32_t* args);
void rdpxi_rdp_set_key_gb(uint32_t wid, const uint32_t* args);
void rdpxi_rdp_set_key_r(uint32_t wid, const uint32_t* args);

void combiner_1cycle(uint32_t wid, int adseed, uint32_t* curpixel_cvg);
void combiner_2cycle_cycle0(uint32_t wid, int adseed, uint32_t cvg,
                            uint32_t* acalpha);
void combiner_2cycle_cycle1(uint32_t wid, int adseed, uint32_t* curpixel_cvg);
void combiner_init_lut(void);
void combiner_init(uint32_t wid);

// LUT read by the rasterizer (oracle keeps it local, so no prefix).
extern uint32_t special_9bit_clamptable[512];
