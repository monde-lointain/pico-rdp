#pragma once

// tex_internal.h — texture pipeline exports (tex.cc).
// The 7 rdp_* handlers collide with the oracle (rdpxi_-prefixed); the per-pixel
// helpers + init do not. De-inlined for cross-TU use (LTO recovers inlining).

#include "rdp_internal.h"

void rdpxi_rdp_load_block(uint32_t wid, const uint32_t* args);
void rdpxi_rdp_load_tile(uint32_t wid, const uint32_t* args);
void rdpxi_rdp_load_tlut(uint32_t wid, const uint32_t* args);
void rdpxi_rdp_set_convert(uint32_t wid, const uint32_t* args);
void rdpxi_rdp_set_texture_image(uint32_t wid, const uint32_t* args);
void rdpxi_rdp_set_tile(uint32_t wid, const uint32_t* args);
void rdpxi_rdp_set_tile_size(uint32_t wid, const uint32_t* args);

void get_texel1_1cycle(uint32_t wid, int32_t* s1, int32_t* t1, int32_t s,
                       int32_t t, int32_t w, int32_t dsinc, int32_t dtinc,
                       int32_t dwinc, int32_t scanline, struct Spansigs* sigs);
void texture_pipeline_cycle(uint32_t wid, struct Color* tex, struct Color* prev,
                            int32_t sss, int32_t sst, uint32_t tilenum,
                            uint32_t cycle);
void tex_init(uint32_t wid);
void tex_init_lut(void);
