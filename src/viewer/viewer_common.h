#pragma once

#include <stdint.h>

// Shared helpers for the rdp_viewer entry points (headless harness + windowed
// UI). Orthodoxy-EXEMPT (mirrors main.cc / the host harness).

// Seed the demo arena into the renderer's RDRAM. Returns 0 on success.
int seed_demo(void);

// RDPX_TESTING pixel-counter / TMEM accessors (compiled into rdp_core;
// forward-declared here as the conformance adapter / perf probe do — not in the
// public n64video.h surface). Used by the perf harness and the live HUD.
extern "C" {
uint64_t rdpx_get_pixel_count(void);
void rdpx_reset_pixel_count(void);
uint8_t* rdpx_get_tmem(void);
uint32_t rdpx_get_tmem_size(void);
}
