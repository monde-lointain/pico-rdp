// vi/lerp.cc — VI lerp stage (standalone TU). Ported VERBATIM from the fork
// 31bdb1f. Cross-TU exports declared in vi_internal.h.

#include "vi_internal.h"

void vi_vl_lerp(struct Rgba* up, struct Rgba down, uint32_t frac) {
  uint32_t r0;
  uint32_t g0;
  uint32_t b0;
  if (!frac) {
    return;
  }

  r0 = up->r;
  g0 = up->g;
  b0 = up->b;

  up->r = ((((down.r - r0) * frac + 16) >> 5) + r0) & 0xff;
  up->g = ((((down.g - g0) * frac + 16) >> 5) + g0) & 0xff;
  up->b = ((((down.b - b0) * frac + 16) >> 5) + b0) & 0xff;
}
