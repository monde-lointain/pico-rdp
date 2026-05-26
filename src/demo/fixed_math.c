/* demo_core fixed-point Q16.16 math kernel (see fixed.h).
 *
 * Pure integer, freestanding: no malloc/printf/float. Deterministic across host
 * OSes and the RP2040 (the SDK AEABI routes C `/` to the SIO 8-cycle HW
 * divider).
 *
 * Convention: matrices are row-major (m[row*4 + col]) with the libultra
 * row-vector convention v' = v . M (translation lives in row 3). This matches
 * src/demo/generated/baked_matrices.h. */

#include <stdint.h>

#include "fixed.h"

demo_fix demo_fix_mul(demo_fix a, demo_fix b) {
  /* 64-bit intermediate, then renormalize by the fractional shift. */
  return (demo_fix)(((int64_t)a * (int64_t)b) >> DEMO_FIX_SHIFT);
}

demo_fix demo_fix_div(demo_fix num, demo_fix den) {
  /* Defensive: integer divide-by-zero is UB in C and would not be
   * deterministic, so guard it. The header's divide contract is the plain
   * integer `/` op; den==0 cannot arise from a finite perspective divide of a
   * vertex in front of the near plane, so 0 is a safe sentinel. */
  if (den == 0) {
    return 0;
  }
  /* Pre-shift numerator into Q32.16-in-64 then integer divide. */
  return (demo_fix)(((int64_t)num << DEMO_FIX_SHIFT) / (int64_t)den);
}

void demo_mat4_mul(struct DemoMat4 *out, const struct DemoMat4 *a,
                   const struct DemoMat4 *b) {
  /* out = a * b, standard row-major matrix product:
   *   out[r][c] = sum_k a[r][k] * b[k][c]. */
  for (int32_t r = 0; r < 4; ++r) {
    for (int32_t c = 0; c < 4; ++c) {
      int64_t acc = 0;
      for (int32_t k = 0; k < 4; ++k) {
        acc += (int64_t)a->m[(r * 4) + k] * (int64_t)b->m[(k * 4) + c];
      }
      out->m[(r * 4) + c] = (demo_fix)(acc >> DEMO_FIX_SHIFT);
    }
  }
}

void demo_mat4_mul_vec4(demo_fix *out, const struct DemoMat4 *m,
                        const demo_fix *v) {
  /* Row-vector convention: out = v . M, so out[c] = sum_r v[r] * m[r][c]. */
  for (int32_t c = 0; c < 4; ++c) {
    int64_t acc = 0;
    for (int32_t r = 0; r < 4; ++r) {
      acc += (int64_t)v[r] * (int64_t)m->m[(r * 4) + c];
    }
    out[c] = (demo_fix)(acc >> DEMO_FIX_SHIFT);
  }
}
