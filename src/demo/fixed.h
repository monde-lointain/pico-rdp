#pragma once

// demo_core fixed-point convention for the mini-"RSP" geometry.
//
// MANDATED: Q16.16 throughout. The M0+ has no FPU; integer fixed-point is
// exact and deterministic across host and device (portable goldens). The
// only non-multiply kernel op is the perspective divide, which maps to the
// RP2040 SIO 8-cycle hardware integer divider on-device (the SDK AEABI routes
// C `/` and `%` there). No reciprocal LUT, no floats.
//
// Declaration-only API: implementations live in fixed_math.c (another stream).

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Q16.16 signed fixed-point scalar.
typedef int32_t demo_fix;

#define DEMO_FIX_SHIFT 16
#define DEMO_FIX_ONE (1 << DEMO_FIX_SHIFT)

// Fixed-point multiply with a 64-bit intermediate (avoids overflow before the
// >> DEMO_FIX_SHIFT renormalize).
demo_fix demo_fix_mul(demo_fix a, demo_fix b);

// Fixed-point divide (perspective divide). Uses the integer `/` op so it is
// bit-identical host vs device and HW-accelerated on the RP2040 SIO divider.
// Contract: den==0 returns 0 (defensive sentinel, avoids integer-divide UB; an
// in-front vertex's perspective divide never yields den==0).
demo_fix demo_fix_div(demo_fix num, demo_fix den);

// 4x4 matrix in row-major Q16.16. m[row*4 + col].
struct DemoMat4 {
  demo_fix m[16];
};

// out = a * b (4x4 * 4x4). out must not alias a or b.
void demo_mat4_mul(struct DemoMat4 *out, const struct DemoMat4 *a,
                   const struct DemoMat4 *b);

// out[4] = m * v[4]. out must not alias v.
void demo_mat4_mul_vec4(demo_fix *out, const struct DemoMat4 *m,
                        const demo_fix *v);

#ifdef __cplusplus
}  // extern "C"
#endif
