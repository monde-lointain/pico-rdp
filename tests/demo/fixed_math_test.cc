/* Unit tests for the demo_core fixed-point Q16.16 math kernel (src/demo/fixed.h
 * / fixed_math.c). Goldens are hand-computed against an exact int64 reference
 * and against the baked matrices' documented row-major, row-vector convention.
 */

#include <gtest/gtest.h>
#include <stdint.h>

#include "fixed.h"

/* baked_matrices.h re-#defines DEMO_FIX_ONE to the identical value (65536) but
 * via a different token (DEMO_FIX_FRAC), which trips -Wmacro-redefined. They
 * agree; drop ours before including so the data header's definition stands. */
#undef DEMO_FIX_ONE
#include "generated/baked_matrices.h"

namespace {

/* Exact int64 reference mirroring the implementation's intended semantics. */
int64_t ref_mul(int32_t a, int32_t b) {
  return ((int64_t)a * (int64_t)b) >> DEMO_FIX_SHIFT;
}
int64_t ref_div(int32_t num, int32_t den) {
  if (den == 0) return 0;
  return ((int64_t)num << DEMO_FIX_SHIFT) / (int64_t)den;
}

}  // namespace

TEST(DemoFixMul, IdentityAndUnits) {
  EXPECT_EQ(demo_fix_mul(DEMO_FIX_ONE, DEMO_FIX_ONE), DEMO_FIX_ONE);
  EXPECT_EQ(demo_fix_mul(DEMO_FIX_ONE, 12345), 12345);
  EXPECT_EQ(demo_fix_mul(0, 99999), 0);
}

TEST(DemoFixMul, HandComputedGoldens) {
  /* 3.0 * 2.0 = 6.0 */
  EXPECT_EQ(demo_fix_mul(3 * DEMO_FIX_ONE, 2 * DEMO_FIX_ONE), 6 * DEMO_FIX_ONE);
  /* -0.5 * 4.0 = -2.0 */
  EXPECT_EQ(demo_fix_mul(-(DEMO_FIX_ONE / 2), 4 * DEMO_FIX_ONE),
            -2 * DEMO_FIX_ONE);
  /* 1.5 * 1.5 = 2.25 -> 98304 * 98304 >> 16 = 147456 */
  EXPECT_EQ(demo_fix_mul(98304, 98304), 147456);
}

TEST(DemoFixMul, MatchesInt64ReferenceWide) {
  const int32_t vals[] = {
      0,      1,      -1,       DEMO_FIX_ONE, -DEMO_FIX_ONE, 98304,
      -98304, 123456, -7654321, 2000000000,   -2000000000,   65535};
  const int n = (int)(sizeof(vals) / sizeof(vals[0]));
  for (int i = 0; i < n; ++i) {
    for (int j = 0; j < n; ++j) {
      EXPECT_EQ(demo_fix_mul(vals[i], vals[j]),
                (demo_fix)ref_mul(vals[i], vals[j]))
          << "a=" << vals[i] << " b=" << vals[j];
    }
  }
}

TEST(DemoFixDiv, HandComputedGoldens) {
  /* 3.0 / 2.0 = 1.5 */
  EXPECT_EQ(demo_fix_div(3 * DEMO_FIX_ONE, 2 * DEMO_FIX_ONE),
            3 * DEMO_FIX_ONE / 2);
  /* 1.0 / 2.0 = 0.5 */
  EXPECT_EQ(demo_fix_div(DEMO_FIX_ONE, 2 * DEMO_FIX_ONE), DEMO_FIX_ONE / 2);
  /* -5.0 / 2.0 = -2.5 */
  EXPECT_EQ(demo_fix_div(-5 * DEMO_FIX_ONE, 2 * DEMO_FIX_ONE),
            -5 * DEMO_FIX_ONE / 2);
}

TEST(DemoFixDiv, DivByZeroReturnsZeroDefensively) {
  EXPECT_EQ(demo_fix_div(7 * DEMO_FIX_ONE, 0), 0);
  EXPECT_EQ(demo_fix_div(0, 0), 0);
  EXPECT_EQ(demo_fix_div(-123456, 0), 0);
}

TEST(DemoFixDiv, MatchesInt64ReferenceWide) {
  const int32_t nums[] = {0,      DEMO_FIX_ONE, -DEMO_FIX_ONE, 158218,
                          316436, -19464192,    123456,        -7654321};
  const int32_t dens[] = {
      1,      DEMO_FIX_ONE, -DEMO_FIX_ONE, 2 * DEMO_FIX_ONE, 297 * DEMO_FIX_ONE,
      -65536, 12345};
  const int nn = (int)(sizeof(nums) / sizeof(nums[0]));
  const int nd = (int)(sizeof(dens) / sizeof(dens[0]));
  for (int i = 0; i < nn; ++i) {
    for (int j = 0; j < nd; ++j) {
      EXPECT_EQ(demo_fix_div(nums[i], dens[j]),
                (demo_fix)ref_div(nums[i], dens[j]))
          << "num=" << nums[i] << " den=" << dens[j];
    }
  }
}

TEST(DemoMat4Mul, IdentityIsNeutral) {
  struct DemoMat4 id = {{DEMO_FIX_ONE, 0, 0, 0, 0, DEMO_FIX_ONE, 0, 0, 0, 0,
                         DEMO_FIX_ONE, 0, 0, 0, 0, DEMO_FIX_ONE}};
  /* A non-trivial matrix (the baked view matrix). */
  struct DemoMat4 a;
  for (int i = 0; i < 16; ++i) a.m[i] = demo_view_matrix[i];

  struct DemoMat4 out;
  demo_mat4_mul(&out, &a, &id);
  for (int i = 0; i < 16; ++i)
    EXPECT_EQ(out.m[i], a.m[i]) << "id on right, i=" << i;

  demo_mat4_mul(&out, &id, &a);
  for (int i = 0; i < 16; ++i)
    EXPECT_EQ(out.m[i], a.m[i]) << "id on left, i=" << i;
}

TEST(DemoMat4Mul, ViewTimesProjHandComputed) {
  /* out = VIEW * PROJ. Goldens from the int64 reference (verified by hand:
   * row 3 = translation row of view * proj). */
  struct DemoMat4 view, proj, out;
  for (int i = 0; i < 16; ++i) view.m[i] = demo_view_matrix[i];
  for (int i = 0; i < 16; ++i) proj.m[i] = demo_proj_matrix[i];
  demo_mat4_mul(&out, &view, &proj);

  const demo_fix golden[16] = {158218, 0, 0,        0,       0,      158218,
                               0,      0, 0,        0,       -66860, -65536,
                               0,      0, 18734040, 19660800};
  for (int i = 0; i < 16; ++i) EXPECT_EQ(out.m[i], golden[i]) << "i=" << i;
}

TEST(DemoMat4MulVec4, RowVectorViewTransform) {
  /* Row-vector convention v' = v . M. Camera at z=300 looking at origin:
   * a vertex at (1,2,3) should land at z = 3 - 300 = -297 in view space. */
  struct DemoMat4 view;
  for (int i = 0; i < 16; ++i) view.m[i] = demo_view_matrix[i];
  demo_fix v[4] = {1 * DEMO_FIX_ONE, 2 * DEMO_FIX_ONE, 3 * DEMO_FIX_ONE,
                   1 * DEMO_FIX_ONE};
  demo_fix out[4];
  demo_mat4_mul_vec4(out, &view, v);

  EXPECT_EQ(out[0], 1 * DEMO_FIX_ONE);
  EXPECT_EQ(out[1], 2 * DEMO_FIX_ONE);
  EXPECT_EQ(out[2], -297 * DEMO_FIX_ONE);
  EXPECT_EQ(out[3], 1 * DEMO_FIX_ONE);
}

TEST(DemoMat4MulVec4, ClipCoordsSaneViaProjView) {
  /* Full clip-space transform of a sample vertex: v . VIEW . PROJ.
   * Combining (VIEW*PROJ) then transforming must equal the two-step path, and
   * the resulting w must be positive (vertex in front of the eye). */
  struct DemoMat4 view, proj, vp;
  for (int i = 0; i < 16; ++i) view.m[i] = demo_view_matrix[i];
  for (int i = 0; i < 16; ++i) proj.m[i] = demo_proj_matrix[i];
  demo_mat4_mul(&vp, &view, &proj);

  demo_fix v[4] = {1 * DEMO_FIX_ONE, 2 * DEMO_FIX_ONE, 3 * DEMO_FIX_ONE,
                   1 * DEMO_FIX_ONE};

  demo_fix view_v[4], clip_two_step[4], clip_combined[4];
  demo_mat4_mul_vec4(view_v, &view, v);
  demo_mat4_mul_vec4(clip_two_step, &proj, view_v);
  demo_mat4_mul_vec4(clip_combined, &vp, v);

  for (int i = 0; i < 4; ++i)
    EXPECT_EQ(clip_two_step[i], clip_combined[i]) << "i=" << i;

  /* Hand-computed goldens (int64 reference). */
  EXPECT_EQ(clip_combined[0], 158218);
  EXPECT_EQ(clip_combined[1], 316436);
  EXPECT_EQ(clip_combined[2], 18533460);
  EXPECT_EQ(clip_combined[3], 19464192);

  /* Sanity: w > 0, and the perspective divide yields sane NDC magnitudes. */
  EXPECT_GT(clip_combined[3], 0);
  demo_fix ndc_x = demo_fix_div(clip_combined[0], clip_combined[3]);
  demo_fix ndc_y = demo_fix_div(clip_combined[1], clip_combined[3]);
  /* x is small/positive, |ndc| well under 1.0 (65536) for this near vertex. */
  EXPECT_GT(ndc_x, 0);
  EXPECT_LT(ndc_x, DEMO_FIX_ONE);
  EXPECT_GT(ndc_y, 0);
  EXPECT_LT(ndc_y, DEMO_FIX_ONE);
}
