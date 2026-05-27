// Locks the viewer's runtime gldemo-rate playback math
// (src/viewer/demo_anim.cc) and the fixed-timestep pacer
// (src/viewer/frame_pacer.cc).
//
// demo_anim computes per-frame model matrices at gldemo's exact rotation rate
// (pyramid +0.2deg/frame, cube -0.15deg/frame @ 60fps) so live playback matches
// the original's on-screen speed smoothly, instead of indexing the coarse baked
// tables. Two checks:
//   1. Frame-0 is exact: rotate(0) = identity, so MODEL == the translate matrix
//      (tolerance-free anchor for translate/axis/convention).
//   2. At frame_t = 60*k the runtime angle (12k / -9k deg) equals baked frame
//   k,
//      so the runtime matrices must reproduce the trusted Python-generated
//      baked tables within a few Q16.16 ULPs.
// The pacer is checked for 60fps cadence, accumulation, and stall clamping.
//
// Not orthodox-enforced (gtest / C++ idioms).

#include <gtest/gtest.h>
#include <stdint.h>
#include <stdlib.h>

#include "demo_anim.h"
#include "frame_pacer.h"
#include "generated/baked_matrices.h"  // demo_pyramid_model/cube_model + periods

namespace {

constexpr int32_t kOne = 65536;  // Q16.16 1.0

// Build the expected Q16.16 translate matrix (row-vector: translation in row
// 3).
void translate_q16(int32_t out[16], int32_t tx, int32_t ty, int32_t tz) {
  for (int i = 0; i < 16; ++i) {
    out[i] = 0;
  }
  out[0] = out[5] = out[10] = out[15] = kOne;
  out[12] = tx;
  out[13] = ty;
  out[14] = tz;
}

}  // namespace

// 1. Frame 0 == pure translate (rotation is identity). Tolerance-free.
TEST(ViewerAnim, Frame0IsPureTranslate) {
  int32_t pyr[16];
  int32_t cube[16];
  demo_anim_pyramid_model(0.0, pyr);
  demo_anim_cube_model(0.0, cube);

  int32_t want_pyr[16];
  int32_t want_cube[16];
  translate_q16(want_pyr, -75 * kOne, 0, 0);
  translate_q16(want_cube, 75 * kOne, 0, -50 * kOne);

  for (int i = 0; i < 16; ++i) {
    ASSERT_EQ(pyr[i], want_pyr[i]) << "pyramid[" << i << "]";
    ASSERT_EQ(cube[i], want_cube[i]) << "cube[" << i << "]";
  }
}

// 2. Runtime matrices reproduce gldemo angles at the baked sample points: at
// frame_t = 60*k the angle is 12k deg (pyramid) / -9k deg (cube) == baked frame
// k (trig wraps mod 360, so k beyond the period still matches baked[k%period]).
TEST(ViewerAnim, MatchesBakedAtSamplePoints) {
  const int32_t tol = 4;  // Q16.16 ULPs (float64 0.2*60 vs 12.0 etc.)
  for (int k = 0; k < 120; ++k) {
    int32_t pyr[16];
    int32_t cube[16];
    demo_anim_pyramid_model(60.0 * (double)k, pyr);
    demo_anim_cube_model(60.0 * (double)k, cube);

    const int32_t* want_pyr = demo_pyramid_model[k % DEMO_PYRAMID_PERIOD];
    const int32_t* want_cube = demo_cube_model[k % DEMO_CUBE_PERIOD];
    for (int i = 0; i < 16; ++i) {
      ASSERT_LE(abs(pyr[i] - want_pyr[i]), tol)
          << "pyramid k=" << k << " elem " << i << " got " << pyr[i] << " want "
          << want_pyr[i];
      ASSERT_LE(abs(cube[i] - want_cube[i]), tol)
          << "cube k=" << k << " elem " << i << " got " << cube[i] << " want "
          << want_cube[i];
    }
  }
}

// 3a. Pacer yields the right frame count for exact-multiple dts.
TEST(FramePacer, ExactMultiples) {
  struct FramePacer p;
  frame_pacer_init(&p, 60.0);
  EXPECT_EQ(frame_pacer_step(&p, 1.0 / 60.0), 1);
  frame_pacer_init(&p, 60.0);
  EXPECT_EQ(frame_pacer_step(&p, 1.0 / 30.0), 2);  // two 60fps frames in 1/30 s
}

// 3b. Sub-period dts accumulate across calls into one frame.
TEST(FramePacer, Accumulates) {
  struct FramePacer p;
  frame_pacer_init(&p, 60.0);
  EXPECT_EQ(frame_pacer_step(&p, 1.0 / 120.0), 0);  // half a frame
  EXPECT_EQ(frame_pacer_step(&p, 1.0 / 120.0), 1);  // completes one frame
}

// 3c. A long stall is clamped and capped (no spiral of death), then resets.
TEST(FramePacer, StallClampedAndCapped) {
  struct FramePacer p;
  frame_pacer_init(&p, 60.0);
  int const due = frame_pacer_step(&p, 5.0);  // huge dt
  EXPECT_EQ(due, p.max_catchup);              // capped at 4
  EXPECT_LT(p.accum, p.frame_period);         // backlog dropped
  // Next normal frame still produces exactly one.
  EXPECT_EQ(frame_pacer_step(&p, 1.0 / 60.0), 1);
}
