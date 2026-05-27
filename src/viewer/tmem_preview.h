// tmem_preview.h — pure (SDL/ImGui-free) decode of the demo's CI4 texture out
// of the renderer's live TMEM, for the viewer's Memory->TMEM preview.
//
// Split out of panel_memory.cc so the decode mirrors the renderer's sample-tile
// addressing in ONE place and can be unit-tested headlessly (see
// tests/demo/tmem_preview_test.cc). Orthodox-clean C: no ImGui/SDL deps.

#ifndef RDP_VIEWER_TMEM_PREVIEW_H
#define RDP_VIEWER_TMEM_PREVIEW_H

#include <stdint.h>

// Decode the demo's DEMO_CI4_TEX_WIDTH x DEMO_CI4_TEX_HEIGHT CI4 texture
// (loaded at TMEM offset 0) through the TLUT at tmem[0x800] into out_rgba8,
// row-major, as RGBA8 in SDL_PIXELFORMAT_RGBA32 byte order. tmem must be the
// renderer's 4 KB TMEM (e.g. rdpx_get_tmem()); out_rgba8 must hold WIDTH*HEIGHT
// uint32_t.
void tmem_decode_ci4_preview(const uint8_t *tmem, uint32_t *out_rgba8);

#endif  // RDP_VIEWER_TMEM_PREVIEW_H
