#pragma once

// Stream C.4 — perf HUD panel.
//
// Orthodoxy-EXEMPT (ImGui host idioms; mirrors main.cc / the host harness).
//
// Each presented frame the host: rdpx_reset_pixel_count() before the build,
// reads rdpx_get_pixel_count() after, and times the build+present wall clock.
// It feeds those into panel_perf_draw together with the buffered command stream
// (Playback), which the panel decodes for per-command-type counts and against
// which it applies the docs/perf-extrapolation.md MODEL to show extrapolated
// M0+ ms/frame. No SDL/texture dependency — pure ImGui text + the C.2 decoder.

#include <stdint.h>

struct Playback;  // panels.h

// A single sampled frame's perf metrics, captured by the host loop around the
// build+present of one demo frame.
struct PerfSample {
  double build_present_ms;  // wall-clock for build+feed+present of the frame
  uint64_t pixel_count;     // rdpx_get_pixel_count() committed over the frame
  double play_fps;          // measured playback rate (advanced frames / wall s)
};

// Draw the Perf dock node. `s` is the latest sampled frame; `pb` is the current
// buffered command stream (for per-command-type counts). Both may be read-only.
void panel_perf_draw(struct Playback* pb, const struct PerfSample* s);

#ifdef __cplusplus
// Apply the docs/perf-extrapolation.md model to a host frame time / pixel
// count, returning the extrapolated M0+ ms/frame for the given per-pixel M0+
// cycle cost at the given core clock (Hz). Exposed for the headless check.
extern "C" {
#endif
double perf_extrapolate_m0_ms(uint64_t pixel_count, double m0_cycles_per_px,
                              double m0_clock_hz);
#ifdef __cplusplus
}
#endif
