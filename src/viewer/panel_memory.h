#pragma once

// Stream C.4 — memory viewers panel.
//
// Orthodoxy-EXEMPT (ImGui host idioms; mirrors main.cc / the host harness).
//
// A tabbed Memory panel that renders the renderer's live memories as ImGui
// images:
//   - TMEM     : rdpx_get_tmem() (4 KB) — the CI4 texels (offset 0) decoded
//                through the TLUT (tmem[0x800]) into a 64x64 RGBA preview, plus
//                a raw halfword heatmap so the load is always visible.
//   - Color    : demo color FB (RGBA5551, 240x240) read from RDRAM
//   (XOR-decode).
//   - Depth    : DEMO_RDRAM_Z_BUF_ADDR (240x240, 16-bit) z-decoded to
//   grayscale.
//   - Coverage : rdpx_get_hidden_rdram() low region as a per-pixel heatmap.
//
// Textures live for the panel's lifetime; the host must hand it the
// SDL_Renderer once at init and call shutdown before the renderer is destroyed.

struct SDL_Renderer;
struct Playback;  // panels.h

// Create the panel's SDL textures against `renderer`. Call once after the SDL
// renderer exists. Safe to call with NULL (panel then draws a "no renderer"
// note); headless callers skip the textures entirely.
void panel_memory_init(struct SDL_Renderer* renderer);

// Destroy the panel's SDL textures. Call before SDL_DestroyRenderer.
void panel_memory_shutdown(void);

// Draw the tabbed Memory dock node. Refreshes the textures from the current
// renderer memories first.
void panel_memory_draw(struct Playback* pb);
