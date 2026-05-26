#pragma once

#include <stdint.h>

// rdp_viewer headless test harness — renders demo frames with no window
// (SDL_VIDEODRIVER=dummy friendly) and asserts invariants. Driven by main.cc's
// CLI dispatch. Each returns 0 on success, non-zero on failure (suitable as a
// process exit code). Orthodoxy-EXEMPT (mirrors main.cc / the host harness).

// One hardcoded-clear frame -> binary PPM; asserts 240x240, uniform, non-black.
int headless_dump(const char* path);

// Demo frame N through the full viewer path -> PPM; asserts 240x240,
// non-uniform.
int headless_demo_frame(uint32_t frame_n, const char* path);

// Demo frame N: pixel count, per-command histogram, M0+ extrapolation, TMEM
// check.
int headless_perf(uint32_t frame_n);

// Demo frame N: direct render vs .rdp capture+replay, asserts byte-identical.
int headless_capture_roundtrip(uint32_t frame_n);
