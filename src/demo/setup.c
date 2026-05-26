/* demo_core fixed-point triangle setup (see setup.h).
 *
 * Structurally mirrors the vendored float oracle
 * (tests/conformance/vendor/triangle_converter.cpp): near-W clip, perspective
 * divide, viewport transform, guard-band + near/far clip, backface cull, and
 * per-attribute edge-coefficient setup. The geometry pipeline runs in Q16.16
 * (demo_fix), with 64-bit integer intermediates; the final RDP setup
 * coefficients carry their own scales (matched to the oracle within tolerance,
 * not bit-identically — theirs is double, ours is fixed-point).
 *
 * Freestanding / Orthodox: plain C, fixed stack buffers, no malloc / printf /
 * float / C++ runtime; runs on the RP2040 M0+. */

#include "setup.h"

#include <stdint.h>

#include "fixed.h"
#include "setup_ir.h"

/* Subpixel scale: screen X/Y are quantized to 1/(1<<SUBPIXELS_LOG2) units. */
#define SUBPIXELS_LOG2 DEMO_SUBPIXELS_LOG2
#define SUBPIXELS (1 << SUBPIXELS_LOG2)

/* Near-W clip plane. Mirrors the oracle MIN_W = 1/1024 (Q16.16 = 64). Clipping
 * against a small positive W avoids infinities in the perspective divide. */
#define MIN_W_FIX (DEMO_FIX_ONE / 1024) /* 64 */

/* Guard-band clip bounds, in screen-space pixels (post viewport), matching the
 * oracle. Z near/far clip bounds are 0 and 1 in clip-space depth. */
#define GUARD_X_LO_FIX (-1024 * DEMO_FIX_ONE)
#define GUARD_X_HI_FIX (1023 * DEMO_FIX_ONE)
#define GUARD_Y_LO_FIX (-2048 * DEMO_FIX_ONE)
#define GUARD_Y_HI_FIX (2047 * DEMO_FIX_ONE)

/* A clip-space vertex carried through the divide/clip pipeline. All Q16.16.
 * clip[0..3] = x,y,z,w (post-divide, w holds rescaled 1/w). */
struct WorkVert {
  demo_fix clip[4];
  demo_fix color[4];
  demo_fix u, v;
};

struct WorkTri {
  struct WorkVert v[3];
};

/* ---- small fixed-point helpers ------------------------------------------ */

static demo_fix wv_min(demo_fix a, demo_fix b) { return a < b ? a : b; }
static int32_t wv_max_i(int32_t a, int32_t b) { return a > b ? a : b; }

/* Round a Q16.16 value to nearest integer, ties away from zero, returning the
 * integer (used for the subpixel quantization where the fractional value has
 * already been scaled by SUBPIXELS). */
static int32_t fix_round_to_int(demo_fix v) {
  if (v >= 0) {
    return (int32_t)((v + (DEMO_FIX_ONE / 2)) >> DEMO_FIX_SHIFT);
  }
  return -(int32_t)(((-(int64_t)v) + (DEMO_FIX_ONE / 2)) >> DEMO_FIX_SHIFT);
}

static int16_t clamp_int16(int32_t v) {
  if (v < -0x8000) return (int16_t)-0x8000;
  if (v > 0x7fff) return (int16_t)0x7fff;
  return (int16_t)v;
}

/* Quantize a Q16.16 screen coordinate to subpixels (value * SUBPIXELS, rounded,
 * clamped to int16). Mirrors oracle quantize_x / quantize_y. */
static int16_t quantize_screen(demo_fix v) {
  /* v * SUBPIXELS in Q16.16, then round to integer. */
  int32_t q = fix_round_to_int(v * SUBPIXELS);
  return clamp_int16(q);
}

/* round_away_from_zero_divide, 64-bit (mirrors the oracle helper). */
static int32_t round_away_divide(int64_t x, int64_t y) {
  int64_t rounding = y - 1;
  if (x < 0) {
    x -= rounding;
  } else if (x > 0) {
    x += rounding;
  }
  return (int32_t)(x / y);
}

/* ---- attribute quantization scales (mirroring the oracle) ----------------
 *
 * The oracle's setup math is, per attribute a with values a0,a1,a2 at the three
 * (reordered) vertices:
 *   inv_area = SUBPIXELS / signed_area               (signed_area is integer)
 *   dadx = -inv_area * (ab_y*a2 + ca_y*a1 + bc_y*a0)
 *   dady =  inv_area * (ab_x*a2 + ca_x*a1 + bc_x*a0)
 *   dade =  dady + dadx * dxdy_a_in_screen
 *   a    =  a0 - yfrac * dade
 * then each is multiplied by a per-attribute quantization scale and rounded.
 *
 * We fold the quantization scale into the vertex values up front (so a-values
 * become "quantized units"), keeping the divide by signed_area as the only
 * lossy step. Inputs (color/uv/z) arrive in Q16.16; we convert to the oracle's
 * target integer scale via a 64-bit multiply, picking shifts that keep full
 * precision. */

/* Oracle scales (per quantize_*):
 *   color: c * 255 * 2^16   (c in [0,1])
 *   u,v:   c * 2^6 * 2^16    (c in texel units)  -> our u,v already texel units
 *   w:     c * 2^32          (c is rescaled 1/w, <= ~0.5)
 *   z:     z * (2^18 - 1) * 2^13   (z in [0,1])
 * Inputs are Q16.16 (value<<16). We compute quantized = round(value_real*scale)
 * = round((value_fix/2^16) * scale) = round(value_fix * scale / 2^16). */

static int64_t qscale_color(demo_fix v) {
  /* round(v/2^16 * 255 * 2^16) = round(v * 255). */
  return (int64_t)v * 255;
}
static int64_t qscale_uv(demo_fix v) {
  /* round(v/2^16 * 2^6 * 2^16) = round(v * 2^6) = v << 6. */
  return (int64_t)v << 6;
}
static int64_t qscale_w(demo_fix v) {
  /* round(v/2^16 * 2^32) = v * 2^16 = v << 16. */
  return (int64_t)v << 16;
}
static int64_t qscale_z(demo_fix v) {
  /* round(v/2^16 * (2^18-1) * 2^13) = round(v * (2^18-1) * 2^13 / 2^16)
   *   = round(v * (2^18-1) / 8). Keep the /8 with rounding. */
  int64_t n = (int64_t)v * ((1 << 18) - 1);
  if (n >= 0) {
    return (n + 4) >> 3;
  }
  return -(((-n) + 4) >> 3);
}

/* Compute the four coefficients (c, dcdx, dcde, dcdy) for one attribute given
 * the three quantized vertex values q[3] (already in the attribute's RDP scale,
 * 64-bit), the reordered edge deltas, signed_area, dxdy_a (the long-edge slope
 * in the screen-X-per-Y Q16.16 form), and yfrac (Q16.16 fraction in [0,1)).
 * Writes results into out_c/out_dcdx/out_dcde/out_dcdy. */
static void attr_coeffs(int64_t q0, int64_t q1, int64_t q2, int32_t ab_x,
                        int32_t bc_x, int32_t ca_x, int32_t ab_y, int32_t bc_y,
                        int32_t ca_y, int64_t signed_area, int32_t dxdy_a,
                        demo_fix yfrac, int32_t *out_c, int32_t *out_dcdx,
                        int32_t *out_dcde, int32_t *out_dcdy) {
  /* dcdx = -SUBPIXELS * (ab_y*q2 + ca_y*q1 + bc_y*q0) / signed_area
   * dcdy =  SUBPIXELS * (ab_x*q2 + ca_x*q1 + bc_x*q0) / signed_area
   * The q* already carry the attribute's quantization scale, so the results are
   * directly in quantized units. signed_area is in subpixel^2 units. */
  int64_t sum_dx = (int64_t)ab_y * q2 + (int64_t)ca_y * q1 + (int64_t)bc_y * q0;
  int64_t sum_dy = (int64_t)ab_x * q2 + (int64_t)ca_x * q1 + (int64_t)bc_x * q0;

  /* Multiply by SUBPIXELS then divide by signed_area, rounded to nearest. */
  int64_t num_dx = -(int64_t)SUBPIXELS * sum_dx;
  int64_t num_dy = (int64_t)SUBPIXELS * sum_dy;

  int64_t dcdx = (num_dx >= 0) ? (num_dx + signed_area / 2) / signed_area
                               : -(((-num_dx) + signed_area / 2) / signed_area);
  int64_t dcdy = (num_dy >= 0) ? (num_dy + signed_area / 2) / signed_area
                               : -(((-num_dy) + signed_area / 2) / signed_area);

  /* dcde = dcdy + dcdx * dxdy_a (dxdy_a is Q16.16 screen-X per screen-Y). */
  int64_t dcde = dcdy + (int64_t)((dcdx * (int64_t)dxdy_a) >> DEMO_FIX_SHIFT);

  /* c = q0 - yfrac * dcde (yfrac Q16.16). */
  int64_t c = q0 - (int64_t)((dcde * (int64_t)yfrac) >> DEMO_FIX_SHIFT);

  *out_c = (int32_t)c;
  *out_dcdx = (int32_t)dcdx;
  *out_dcde = (int32_t)dcde;
  *out_dcdy = (int32_t)dcdy;
}

/* ---- triangle setup (mirrors oracle setup_triangle) --------------------- */

static int setup_one(struct DemoPrimSetup *setup, const struct WorkTri *in,
                     uint32_t cull) {
  struct DemoPrimSetup zero = {0};
  *setup = zero;

  int16_t xs[3];
  int16_t ys[3];
  for (int i = 0; i < 3; ++i) {
    xs[i] = quantize_screen(in->v[i].clip[0]);
    ys[i] = quantize_screen(in->v[i].clip[1]);
  }

  int ia = 0, ib = 1, ic = 2;
  /* Sort by Y, tie-break on X (mirrors the oracle's three compare/swap steps). */
  if (ys[ib] < ys[ia] || (ys[ib] == ys[ia] && xs[ib] < xs[ia])) {
    int t = ia;
    ia = ib;
    ib = t;
  }
  if (ys[ic] < ys[ib] || (ys[ic] == ys[ib] && xs[ic] < xs[ib])) {
    int t = ib;
    ib = ic;
    ic = t;
  }
  if (ys[ib] < ys[ia] || (ys[ib] == ys[ia] && xs[ib] < xs[ia])) {
    int t = ia;
    ia = ib;
    ib = t;
  }

  int16_t y_lo = ys[ia], y_mid = ys[ib], y_hi = ys[ic];
  int16_t x_a = xs[ia], x_b = xs[ib], x_c = xs[ic];

  setup->pos.x_a = (int32_t)x_a << (16 - SUBPIXELS_LOG2);
  setup->pos.x_b = (int32_t)x_a << (16 - SUBPIXELS_LOG2);
  setup->pos.x_c = (int32_t)x_b << (16 - SUBPIXELS_LOG2);

  setup->pos.y_lo = y_lo;
  setup->pos.y_mid = y_mid;
  setup->pos.y_hi = y_hi;

  setup->pos.dxdy_a = round_away_divide(((int64_t)(x_c - x_a)) << 16,
                                        wv_max_i(1, y_hi - y_lo));
  setup->pos.dxdy_b = round_away_divide(((int64_t)(x_b - x_a)) << 16,
                                        wv_max_i(1, y_mid - y_lo));
  setup->pos.dxdy_c = round_away_divide(((int64_t)(x_c - x_b)) << 16,
                                        wv_max_i(1, y_hi - y_mid));

  /* Low 3 bits ignored by the rasterizer. */
  setup->pos.dxdy_a &= ~7;
  setup->pos.dxdy_b &= ~7;
  setup->pos.dxdy_c &= ~7;

  /* Step from integer Y on the first two slopes. */
  uint32_t sub_pix_y = (uint32_t)(y_lo & (SUBPIXELS - 1));
  setup->pos.x_a -= (setup->pos.dxdy_a >> SUBPIXELS_LOG2) * (int32_t)sub_pix_y;
  setup->pos.x_b -= (setup->pos.dxdy_b >> SUBPIXELS_LOG2) * (int32_t)sub_pix_y;

  if (setup->pos.dxdy_b < setup->pos.dxdy_a) {
    setup->pos.flags |= DEMO_PRIMITIVE_RIGHT_MAJOR_BIT;
  }

  /* Winding from the ORIGINAL (unsorted) vertices. */
  int32_t ab_x0 = xs[1] - xs[0];
  int32_t ab_y0 = ys[1] - ys[0];
  int32_t bc_x0 = xs[2] - xs[1];
  int32_t bc_y0 = ys[2] - ys[1];
  int64_t signed_area = (int64_t)ab_x0 * bc_y0 - (int64_t)ab_y0 * bc_x0;

  if (signed_area == 0) {
    return 0;
  }
  if (cull == DEMO_CULL_CCW_ONLY && signed_area > 0) {
    return 0;
  }
  if (cull == DEMO_CULL_CW_ONLY && signed_area < 0) {
    return 0;
  }

  /* Recompute deltas from the REORDERED vertices for interpolation. */
  int32_t ab_x = x_b - x_a;
  int32_t bc_x = x_c - x_b;
  int32_t ca_x = x_a - x_c;
  int32_t ab_y = y_mid - y_lo;
  int32_t bc_y = y_hi - y_mid;
  int32_t ca_y = y_lo - y_hi;

  signed_area = (int64_t)ab_x * bc_y - (int64_t)ab_y * bc_x;
  if (signed_area == 0) {
    return 0;
  }

  int32_t dxdy_a = setup->pos.dxdy_a;
  /* yfrac = (y_lo & (SUBPIXELS-1)) / SUBPIXELS, in Q16.16. */
  demo_fix yfrac = (demo_fix)(((int32_t)(y_lo & (SUBPIXELS - 1)))
                              << (DEMO_FIX_SHIFT - SUBPIXELS_LOG2));

  /* Color (4 channels). */
  for (int c = 0; c < 4; ++c) {
    int64_t q0 = qscale_color(in->v[ia].color[c]);
    int64_t q1 = qscale_color(in->v[ib].color[c]);
    int64_t q2 = qscale_color(in->v[ic].color[c]);
    attr_coeffs(q0, q1, q2, ab_x, bc_x, ca_x, ab_y, bc_y, ca_y, signed_area,
                dxdy_a, yfrac, &setup->attr.c[c], &setup->attr.dcdx[c],
                &setup->attr.dcde[c], &setup->attr.dcdy[c]);
  }

  /* Z. */
  attr_coeffs(qscale_z(in->v[ia].clip[2]), qscale_z(in->v[ib].clip[2]),
              qscale_z(in->v[ic].clip[2]), ab_x, bc_x, ca_x, ab_y, bc_y, ca_y,
              signed_area, dxdy_a, yfrac, &setup->attr.z, &setup->attr.dzdx,
              &setup->attr.dzde, &setup->attr.dzdy);

  /* U. */
  attr_coeffs(qscale_uv(in->v[ia].u), qscale_uv(in->v[ib].u),
              qscale_uv(in->v[ic].u), ab_x, bc_x, ca_x, ab_y, bc_y, ca_y,
              signed_area, dxdy_a, yfrac, &setup->attr.u, &setup->attr.dudx,
              &setup->attr.dude, &setup->attr.dudy);

  /* V. */
  attr_coeffs(qscale_uv(in->v[ia].v), qscale_uv(in->v[ib].v),
              qscale_uv(in->v[ic].v), ab_x, bc_x, ca_x, ab_y, bc_y, ca_y,
              signed_area, dxdy_a, yfrac, &setup->attr.v, &setup->attr.dvdx,
              &setup->attr.dvde, &setup->attr.dvdy);

  /* W (the rescaled 1/w). */
  attr_coeffs(qscale_w(in->v[ia].clip[3]), qscale_w(in->v[ib].clip[3]),
              qscale_w(in->v[ic].clip[3]), ab_x, bc_x, ca_x, ab_y, bc_y, ca_y,
              signed_area, dxdy_a, yfrac, &setup->attr.w, &setup->attr.dwdx,
              &setup->attr.dwde, &setup->attr.dwdy);

  setup->pos.flags |= DEMO_PRIMITIVE_PERSPECTIVE_CORRECT_BIT;
  return 1;
}

/* ---- clipping (mirrors oracle clip_* helpers) --------------------------- */

static void interp_vert(struct WorkVert *out, const struct WorkVert *a,
                        const struct WorkVert *b, demo_fix l) {
  demo_fix left = DEMO_FIX_ONE - l;
  for (int i = 0; i < 4; ++i) {
    out->clip[i] =
        demo_fix_mul(a->clip[i], left) + demo_fix_mul(b->clip[i], l);
    out->color[i] =
        demo_fix_mul(a->color[i], left) + demo_fix_mul(b->color[i], l);
  }
  out->u = demo_fix_mul(a->u, left) + demo_fix_mul(b->u, l);
  out->v = demo_fix_mul(a->v, left) + demo_fix_mul(b->v, l);
}

static unsigned clip_code_low(const struct WorkTri *t, demo_fix limit,
                              int comp) {
  unsigned code = 0;
  for (int i = 0; i < 3; ++i) {
    if (t->v[i].clip[comp] < limit) code |= (1u << i);
  }
  return code;
}

static unsigned clip_code_high(const struct WorkTri *t, demo_fix limit,
                               int comp) {
  unsigned code = 0;
  for (int i = 0; i < 3; ++i) {
    if (t->v[i].clip[comp] > limit) code |= (1u << i);
  }
  return code;
}

/* Two vertices a,b interpolated toward c (the inside vertex). One output prim. */
static void clip_single(struct WorkTri *out, const struct WorkTri *in, int comp,
                        demo_fix target, int a, int b, int c) {
  demo_fix la = demo_fix_div(target - in->v[a].clip[comp],
                             in->v[c].clip[comp] - in->v[a].clip[comp]);
  demo_fix lb = demo_fix_div(target - in->v[b].clip[comp],
                             in->v[c].clip[comp] - in->v[b].clip[comp]);
  interp_vert(&out->v[a], &in->v[a], &in->v[c], la);
  interp_vert(&out->v[b], &in->v[b], &in->v[c], lb);
  out->v[a].clip[comp] = target;
  out->v[b].clip[comp] = target;
  out->v[c] = in->v[c];
}

/* One vertex a interpolated against the plane; produces two output prims. */
static void clip_dual(struct WorkTri *out, const struct WorkTri *in, int comp,
                      demo_fix target, int a, int b, int c) {
  demo_fix lab = demo_fix_div(target - in->v[a].clip[comp],
                              in->v[b].clip[comp] - in->v[a].clip[comp]);
  demo_fix lac = demo_fix_div(target - in->v[a].clip[comp],
                              in->v[c].clip[comp] - in->v[a].clip[comp]);
  struct WorkVert ab, ac;
  interp_vert(&ab, &in->v[a], &in->v[b], lab);
  interp_vert(&ac, &in->v[a], &in->v[c], lac);
  ab.clip[comp] = target;
  ac.clip[comp] = target;

  out[0].v[0] = ab;
  out[0].v[1] = in->v[b];
  out[0].v[2] = ac;
  out[1].v[0] = ac;
  out[1].v[1] = in->v[b];
  out[1].v[2] = in->v[c];
}

/* Clip one prim against one half-space; writes 0/1/2 prims, returns count. */
static unsigned clip_component(struct WorkTri *prims, const struct WorkTri *in,
                               int comp, demo_fix target, unsigned code) {
  switch (code) {
    case 0:
      prims[0] = *in;
      return 1;
    case 1:
      clip_dual(prims, in, comp, target, 0, 1, 2);
      return 2;
    case 2:
      clip_dual(prims, in, comp, target, 1, 2, 0);
      return 2;
    case 3:
      clip_single(&prims[0], in, comp, target, 0, 1, 2);
      return 1;
    case 4:
      clip_dual(prims, in, comp, target, 2, 0, 1);
      return 2;
    case 5:
      clip_single(&prims[0], in, comp, target, 2, 0, 1);
      return 1;
    case 6:
      clip_single(&prims[0], in, comp, target, 1, 2, 0);
      return 1;
    default: /* 7 = all clipped */
      return 0;
  }
}

/* Buffers sized to the convex-clip bound: 6 planes on a triangle keep a convex
 * polygon of <= 9 verts -> <= 7 triangles. 16 is a safe ceiling. */
enum { CLIP_BUF = 16 };

static unsigned clip_pass(struct WorkTri *out, const struct WorkTri *in,
                          unsigned count, int comp, demo_fix target) {
  unsigned n = 0;
  for (unsigned i = 0; i < count; ++i) {
    unsigned code = (target > 0) ? clip_code_high(&in[i], target, comp)
                                 : clip_code_low(&in[i], target, comp);
    unsigned c = clip_component(out, &in[i], comp, target, code);
    out += c;
    n += c;
  }
  return n;
}

/* Run perspective divide + viewport + clip on a single near-W-clipped prim,
 * then setup. Mirrors oracle setup_clipped_triangles_clipped_w. */
static uint32_t setup_clipped_w(struct DemoPrimSetup *out, struct WorkTri prim,
                                uint32_t cull, const struct DemoViewport *vp) {
  /* Early reject if all verts are off one X/Y clip side (in clip space). */
  int all_xlo = 1, all_xhi = 1, all_ylo = 1, all_yhi = 1;
  for (int i = 0; i < 3; ++i) {
    demo_fix w = prim.v[i].clip[3];
    if (!(prim.v[i].clip[0] < -w)) all_xlo = 0;
    if (!(prim.v[i].clip[0] > w)) all_xhi = 0;
    if (!(prim.v[i].clip[1] < -w)) all_ylo = 0;
    if (!(prim.v[i].clip[1] > w)) all_yhi = 0;
  }
  if (all_xlo || all_xhi || all_ylo || all_yhi) {
    return 0;
  }

  /* min_w = 0.49 * min(w) for the W rescale. */
  demo_fix min_w = prim.v[0].clip[3];
  min_w = wv_min(min_w, prim.v[1].clip[3]);
  min_w = wv_min(min_w, prim.v[2].clip[3]);
  /* 0.49 in Q16.16 = round(0.49 * 65536) = 32113. */
  min_w = demo_fix_mul(min_w, 32113);

  /* Perspective divide; replace w with rescaled 1/w. Apply viewport to X/Y. */
  for (int i = 0; i < 3; ++i) {
    demo_fix w = prim.v[i].clip[3];
    demo_fix iw = demo_fix_div(DEMO_FIX_ONE, w);
    prim.v[i].clip[0] = demo_fix_mul(prim.v[i].clip[0], iw);
    prim.v[i].clip[1] = demo_fix_mul(prim.v[i].clip[1], iw);
    prim.v[i].clip[2] = demo_fix_mul(prim.v[i].clip[2], iw);

    demo_fix iw_s = demo_fix_mul(iw, min_w);
    prim.v[i].u = demo_fix_mul(prim.v[i].u, iw_s);
    prim.v[i].v = demo_fix_mul(prim.v[i].v, iw_s);
    prim.v[i].clip[3] = iw_s;

    /* Viewport: x = vp.x + (0.5*ndc_x + 0.5)*width. */
    demo_fix nx = prim.v[i].clip[0];
    demo_fix ny = prim.v[i].clip[1];
    demo_fix sx = demo_fix_mul((nx >> 1) + (DEMO_FIX_ONE / 2), vp->width);
    demo_fix sy = demo_fix_mul((ny >> 1) + (DEMO_FIX_ONE / 2), vp->height);
    prim.v[i].clip[0] = vp->x + sx;
    prim.v[i].clip[1] = vp->y + sy;
  }

  struct WorkTri a[CLIP_BUF];
  struct WorkTri b[CLIP_BUF];

  unsigned count = clip_pass(a, &prim, 1, 0, GUARD_X_LO_FIX);
  count = clip_pass(b, a, count, 0, GUARD_X_HI_FIX);
  count = clip_pass(a, b, count, 1, GUARD_Y_LO_FIX);
  count = clip_pass(b, a, count, 1, GUARD_Y_HI_FIX);
  count = clip_pass(a, b, count, 2, 0);
  count = clip_pass(b, a, count, 2, DEMO_FIX_ONE);

  uint32_t produced = 0;
  for (unsigned i = 0; i < count && produced < DEMO_SETUP_MAX_PRIMS; ++i) {
    /* Apply viewport for Z after clipping: z = min + z*(max-min). */
    for (int j = 0; j < 3; ++j) {
      demo_fix z = b[i].v[j].clip[2];
      b[i].v[j].clip[2] =
          vp->min_depth + demo_fix_mul(z, vp->max_depth - vp->min_depth);
    }
    if (setup_one(&out[produced], &b[i], cull)) {
      produced++;
    }
  }
  return produced;
}

uint32_t demo_setup_triangle(struct DemoPrimSetup *out,
                             const struct DemoClipTri *tri, uint32_t cull,
                             const struct DemoViewport *vp) {
  struct WorkTri prim;
  for (int i = 0; i < 3; ++i) {
    prim.v[i].clip[0] = tri->v[i].x;
    prim.v[i].clip[1] = tri->v[i].y;
    prim.v[i].clip[2] = tri->v[i].z;
    prim.v[i].clip[3] = tri->v[i].w;
    for (int c = 0; c < 4; ++c) prim.v[i].color[c] = tri->v[i].color[c];
    prim.v[i].u = tri->v[i].u;
    prim.v[i].v = tri->v[i].v;
  }

  /* Clip near-W first (against MIN_W) so no W <= ~0 reaches the divide. */
  unsigned code = clip_code_low(&prim, MIN_W_FIX, 3);
  struct WorkTri clipped_w[2];
  unsigned cw = clip_component(clipped_w, &prim, 3, MIN_W_FIX, code);

  uint32_t total = 0;
  for (unsigned i = 0; i < cw && total < DEMO_SETUP_MAX_PRIMS; ++i) {
    total += setup_clipped_w(&out[total], clipped_w[i], cull, vp);
  }
  return total;
}
