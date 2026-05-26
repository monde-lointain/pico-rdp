#pragma once

// blender_internal.h — blender pipeline exports (blender.cc).
// Only the rdp_set_* handlers collide with the oracle (rdpxi_-prefixed); the
// per-pixel blender helpers do not. De-inlined for cross-TU use (LTO inlines).

#include "rdp_internal.h"

void rdpxi_rdp_set_fog_color(uint32_t wid, const uint32_t* args);
void rdpxi_rdp_set_blend_color(uint32_t wid, const uint32_t* args);

void set_blender_input(uint32_t wid, int cycle, int which, int32_t** input_r,
                       int32_t** input_g, int32_t** input_b, int32_t** input_a,
                       int a, int b);
int alpha_compare(uint32_t wid, int32_t comb_alpha);
int blender_1cycle(uint32_t wid, uint32_t* fr, uint32_t* fg, uint32_t* fb,
                   int dith, uint32_t blend_en, uint32_t prewrap,
                   uint32_t curpixel_cvg, uint32_t curpixel_cvbit);
int blender_2cycle_cycle0(uint32_t wid, uint32_t curpixel_cvg,
                          uint32_t curpixel_cvbit);
void blender_2cycle_cycle1(uint32_t wid, uint32_t* fr, uint32_t* fg,
                           uint32_t* fb, int dith, uint32_t blend_en,
                           uint32_t prewrap);
void blender_init_lut(void);
