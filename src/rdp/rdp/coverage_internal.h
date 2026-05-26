#pragma once

// coverage_internal.h — coverage helpers exported by coverage.cc.
// None collide with the oracle, so names are unprefixed. De-inlined for
// cross-TU use (LTO recovers inlining).

#include "rdp_internal.h"

void compute_cvg_flip(uint32_t wid, int32_t scanline);
void compute_cvg_noflip(uint32_t wid, int32_t scanline);
int finalize_spanalpha(int cvg_dest, uint32_t blend_en, uint32_t curpixel_cvg,
                       uint32_t curpixel_memcvg);
void lookup_cvmask_derivatives(uint8_t mask, uint8_t* offx, uint8_t* offy,
                               uint32_t* curpixel_cvg,
                               uint32_t* curpixel_cvbit);
void coverage_init_lut(void);
