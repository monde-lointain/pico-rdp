#pragma once

// fbuffer_internal.h — framebuffer exports (fbuffer.cc).
// rdp_set_color_image/rdp_set_fill_color handlers collide with the oracle
// (rdpxi_-prefixed); fbfill_*/fb_init do not. De-inlined for cross-TU use.

#include "rdp_internal.h"

void rdpxi_rdp_set_color_image(uint32_t wid, const uint32_t* args);
void rdpxi_rdp_set_fill_color(uint32_t wid, const uint32_t* args);

void fbfill_4(uint32_t wid, uint32_t curpixel);
void fbfill_8(uint32_t wid, uint32_t curpixel);
void fbfill_16(uint32_t wid, uint32_t curpixel);
void fbfill_32(uint32_t wid, uint32_t curpixel);
void fb_init(uint32_t wid);
