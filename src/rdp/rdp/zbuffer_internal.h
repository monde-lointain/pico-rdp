#pragma once

// zbuffer_internal.h — Z-buffer helpers exported by zbuffer.cc.
// rdpxi_z_init_lut + rdpxi_rdp_set_mask_image collide with the oracle
// (prefixed); the per-pixel z helpers do not. De-inlined for cross-TU use.

#include "rdp_internal.h"

void rdpxi_z_init_lut(void);
void rdpxi_rdp_set_mask_image(uint32_t wid, const uint32_t* args);

uint32_t z_compare(uint32_t wid, uint32_t zcurpixel, uint32_t sz,
                   uint16_t dzpix, int dzpixenc, uint32_t* blend_en,
                   uint32_t* prewrap, uint32_t* curpixel_cvg,
                   uint32_t curpixel_memcvg);
void z_store(uint32_t zcurpixel, uint32_t z, int dzpixenc);
uint32_t dz_compress(uint32_t value);
