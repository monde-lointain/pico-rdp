#pragma once

// dither_internal.h — noise + RGB dither helpers exported by dither.cc.
//
// rdpxi_-prefixed names collide with the oracle (nm shows it exports
// reseed_noise + noise_get_*); rgb_dither / get_dither_noise /
// update_combiner_noise do not, so they keep their names. The noise_get_*
// helpers that are only used inside dither.cc (combiner/dither_alpha/
// dither_color) stay file-static there and are not declared here.

#include "rdp_internal.h"

void rdpxi_reseed_noise(uint32_t* seed, uint32_t x, uint32_t y,
                        uint32_t offset);
int rdpxi_noise_get_blend_threshold(uint32_t seed);

void rgb_dither(int rgb_dither_sel, int* r, int* g, int* b, int dith);
void get_dither_noise(uint32_t wid, int x, int y, int* cdith, int* adith);
void update_combiner_noise(uint32_t wid);
