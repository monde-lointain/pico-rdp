#pragma once

// rasterizer_internal.h — span/triangle exports (rasterizer.cc).
// The 13 triangle/rect/scissor/prim-depth/fill handlers collide with the oracle
// (rdpxi_-prefixed); rasterizer_init does not. render_spans_*/edgewalker are
// rasterizer-internal and not declared here.

#include "rdp_internal.h"

void rdpxi_rdp_fill_rect(uint32_t wid, const uint32_t* args);
void rdpxi_rdp_set_prim_depth(uint32_t wid, const uint32_t* args);
void rdpxi_rdp_set_scissor(uint32_t wid, const uint32_t* args);
void rdpxi_rdp_tex_rect(uint32_t wid, const uint32_t* args);
void rdpxi_rdp_tex_rect_flip(uint32_t wid, const uint32_t* args);
void rdpxi_rdp_tri_noshade(uint32_t wid, const uint32_t* args);
void rdpxi_rdp_tri_noshade_z(uint32_t wid, const uint32_t* args);
void rdpxi_rdp_tri_shade(uint32_t wid, const uint32_t* args);
void rdpxi_rdp_tri_shade_z(uint32_t wid, const uint32_t* args);
void rdpxi_rdp_tri_tex(uint32_t wid, const uint32_t* args);
void rdpxi_rdp_tri_texshade(uint32_t wid, const uint32_t* args);
void rdpxi_rdp_tri_texshade_z(uint32_t wid, const uint32_t* args);
void rdpxi_rdp_tri_tex_z(uint32_t wid, const uint32_t* args);

void rasterizer_init(uint32_t wid);
