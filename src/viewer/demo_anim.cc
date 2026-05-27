// demo_anim.cc — see demo_anim.h.
//
// Faithful C port of the matrix builders in tools/gen_n64_assets.py
// (gu_rotate/gu_translate/mat_mul/to_q16): same truncated dtor = 3.1415926/180,
// row-major A*B product, round-half-away-from-zero Q16.16 conversion. Computing
// in double keeps the result within a few Q16.16 ULPs of the Python-baked
// tables at the shared sample points (see tests/demo/viewer_anim_test.cc).

#include "demo_anim.h"

#include <math.h>

// Scene constants (from baked_matrices.h header comment / gen_n64_assets.py).
// Duplicated here for the runtime path; the vs-baked test fails if they drift.
#define ANIM_Q16_ONE 65536.0
#define ANIM_DTOR (3.1415926 / 180.0)
#define PYRAMID_DEG_PER_FRAME (12.0 / 60.0)  // +0.2 deg/frame, axis +Y
#define CUBE_DEG_PER_FRAME (-9.0 / 60.0)     // -0.15 deg/frame, axis (1,1,1)

static void anim_identity(double *m /*[16]*/) {
  for (int i = 0; i < 16; ++i) {
    m[i] = 0.0;
  }
  m[0] = m[5] = m[10] = m[15] = 1.0;
}

// Row-major product out = a * b (libultra guMtxCatF; matches demo_mat4_mul).
static void anim_mat_mul(double *out /*[16]*/, const double *a,
                         const double *b) {
  for (int r = 0; r < 4; ++r) {
    for (int c = 0; c < 4; ++c) {
      double s = 0.0;
      for (int k = 0; k < 4; ++k) {
        s += a[(r * 4) + k] * b[(k * 4) + c];
      }
      out[(r * 4) + c] = s;
    }
  }
}

// libultra guRotateF -> row-major mf[16]. Mirrors gen_n64_assets.py gu_rotate.
static void anim_rotate(double *mf /*[16]*/, double angle_deg, double x,
                        double y, double z) {
  double const len = sqrt((x * x) + (y * y) + (z * z));
  if (len != 0.0) {
    x /= len;
    y /= len;
    z /= len;
  }
  double const a = angle_deg * ANIM_DTOR;
  double const sine = sin(a);
  double const cosine = cos(a);
  double const omc = 1.0 - cosine;
  double const ab = x * y * omc;
  double const bc = y * z * omc;
  double const ca = z * x * omc;
  double const xxsine = x * sine;
  double const yxsine = y * sine;
  double const zxsine = z * sine;

  anim_identity(mf);
  double t = x * x;
  mf[(0 * 4) + 0] = t + (cosine * (1.0 - t));
  mf[(2 * 4) + 1] = bc - xxsine;
  mf[(1 * 4) + 2] = bc + xxsine;

  t = y * y;
  mf[(1 * 4) + 1] = t + (cosine * (1.0 - t));
  mf[(2 * 4) + 0] = ca + yxsine;
  mf[(0 * 4) + 2] = ca - yxsine;

  t = z * z;
  mf[(2 * 4) + 2] = t + (cosine * (1.0 - t));
  mf[(1 * 4) + 0] = ab - zxsine;
  mf[(0 * 4) + 1] = ab + zxsine;
}

// libultra guTranslateF -> row-major mf[16] (row-vector: translation in row 3).
static void anim_translate(double *mf /*[16]*/, double tx, double ty,
                           double tz) {
  anim_identity(mf);
  mf[(3 * 4) + 0] = tx;
  mf[(3 * 4) + 1] = ty;
  mf[(3 * 4) + 2] = tz;
}

// float -> Q16.16 int32, round half away from zero (matches gen to_q16).
static int32_t anim_to_q16(double v) {
  double const s = v * ANIM_Q16_ONE;
  double const r = (s >= 0.0) ? floor(s + 0.5) : ceil(s - 0.5);
  return (int32_t)r;
}

// model = rotate * translate, converted to Q16.16 row-major.
static void anim_model(int32_t *out16, double angle_deg, double ax, double ay,
                       double az, double tx, double ty, double tz) {
  double rot[16];
  double trans[16];
  double model[16];
  anim_rotate(rot, angle_deg, ax, ay, az);
  anim_translate(trans, tx, ty, tz);
  anim_mat_mul(model, rot, trans);
  for (int i = 0; i < 16; ++i) {
    out16[i] = anim_to_q16(model[i]);
  }
}

void demo_anim_pyramid_model(double frame_t, int32_t *out16) {
  anim_model(out16, PYRAMID_DEG_PER_FRAME * frame_t, 0.0, 1.0, 0.0, -75.0, 0.0,
             0.0);
}

void demo_anim_cube_model(double frame_t, int32_t *out16) {
  anim_model(out16, CUBE_DEG_PER_FRAME * frame_t, 1.0, 1.0, 1.0, 75.0, 0.0,
             -50.0);
}
