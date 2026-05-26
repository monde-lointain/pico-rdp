#pragma once

// Bounded min/max helpers. Overloaded inline functions (FunctionOverload is
// enabled in .orthodoxy.yml specifically for these) — replaces the Orthodox-
// forbidden std::min/std::max. Prefixed `rdpx_` to avoid colliding with local
// identifiers named min/max (e.g. vi/video.cc locals, clamp() params).
//
// Overloads are exactly int32_t / uint32_t / float. Do NOT add `int`
// (== int32_t) or `unsigned` (== uint32_t) overloads — identical types, a
// redefinition. At a mixed-signedness call site, cast the operand to the
// destination's exact type so resolution is unambiguous.
//
// C++ ONLY: include from .cc TUs (via rdp_internal.h). Never include from
// n64video_common.h — that header is reached by demo C code, and overloaded
// functions are a C compile error.

#include <stdint.h>

static inline int32_t rdpx_min(int32_t a, int32_t b) { return a < b ? a : b; }
static inline int32_t rdpx_max(int32_t a, int32_t b) { return a > b ? a : b; }

static inline uint32_t rdpx_min(uint32_t a, uint32_t b) {
  return a < b ? a : b;
}
static inline uint32_t rdpx_max(uint32_t a, uint32_t b) {
  return a > b ? a : b;
}

static inline float rdpx_min(float a, float b) { return a < b ? a : b; }
static inline float rdpx_max(float a, float b) { return a > b ? a : b; }
