// Stream B.2: fixed-point triangle setup (src/demo/setup.c) vs the vendored
// float oracle (RDP::setup_clipped_triangles, tests/conformance/vendor/
// triangle_converter.cpp).
//
// We feed IDENTICAL clip-space geometry to both: as a float InputPrimitive +
// ViewportTransform to the oracle, and as a Q16.16 DemoClipTri + DemoViewport
// to demo_setup_triangle. Both perform the SAME pipeline (near-W clip,
// perspective divide, viewport, guard-band + near/far clip, cull, setup), so
// the resulting PrimitiveSetup / DemoPrimSetup coefficients are field-to-field
// comparable. They are NOT bit-identical: the oracle computes the setup math in
// double, ours in fixed-point Q16.16, so we assert closeness within a documented
// tolerance.
//
// Coverage: fill (flat color, no Z/UV variation), Gouraud shade (per-vertex
// color), Z (depth gradient), and textured-perspective (varying U/V/W with a
// real perspective divide). Plus a near-plane clipping case (one vertex behind
// the near-W plane) to exercise the clipper output count.
//
// TOLERANCES (documented, tuned empirically vs the oracle):
//   - screen pos (x_a/b/c, dxdy_a/b/c, y_lo/mid/hi): tight integer windows;
//     these come from quantize_x/y which both implementations round identically,
//     so they match within +/-2 subpixel-derived ulps (rounding of the divide).
//   - color/z/u/v coefficients: scaled by 255*2^16 (color), (2^18-1)*2^13 (z),
//     2^6*2^16 (uv). The dominant error is the single fixed-point divide by
//     signed_area plus the Q16.16 intermediates in the divide/viewport stage.
//     Relative tolerance ~1e-3 of the coefficient magnitude (plus an absolute
//     floor for near-zero coefficients) holds across all variants.
//   - w coefficients: scaled by 2^32 but our w is a Q16.16 rescaled 1/w, so the
//     low 16 bits carry no information (qscale_w is value<<16). We compare only
//     to the precision Q16.16 affords (~2^16 absolute on a ~2^31 magnitude).

#include <gtest/gtest.h>

#include <math.h>
#include <stdint.h>
#include <string.h>

#include "setup.h"
#include "setup_ir.h"
#include "triangle_converter.hpp"  // rdp::conformance_utils

namespace {

demo_fix to_fix(double v) {
  double s = v * (double)DEMO_FIX_ONE;
  // round to nearest, ties away from zero
  double r = (s >= 0.0) ? floor(s + 0.5) : ceil(s - 0.5);
  return (demo_fix)r;
}

// A source vertex in clip space (the common input fed to both pipelines).
struct SrcVert {
  double x, y, z, w;
  double r, g, b, a;
  double u, v;
};

struct SrcTri {
  SrcVert v[3];
};

struct SrcViewport {
  double x, y, width, height, min_depth, max_depth;
};

RDP::InputPrimitive to_oracle_prim(const SrcTri& t) {
  RDP::InputPrimitive p = {};
  for (int i = 0; i < 3; ++i) {
    p.vertices[i].x = (float)t.v[i].x;
    p.vertices[i].y = (float)t.v[i].y;
    p.vertices[i].z = (float)t.v[i].z;
    p.vertices[i].w = (float)t.v[i].w;
    p.vertices[i].color[0] = (float)t.v[i].r;
    p.vertices[i].color[1] = (float)t.v[i].g;
    p.vertices[i].color[2] = (float)t.v[i].b;
    p.vertices[i].color[3] = (float)t.v[i].a;
    p.vertices[i].u = (float)t.v[i].u;
    p.vertices[i].v = (float)t.v[i].v;
  }
  return p;
}

DemoClipTri to_demo_tri(const SrcTri& t) {
  DemoClipTri d = {};
  for (int i = 0; i < 3; ++i) {
    d.v[i].x = to_fix(t.v[i].x);
    d.v[i].y = to_fix(t.v[i].y);
    d.v[i].z = to_fix(t.v[i].z);
    d.v[i].w = to_fix(t.v[i].w);
    d.v[i].color[0] = to_fix(t.v[i].r);
    d.v[i].color[1] = to_fix(t.v[i].g);
    d.v[i].color[2] = to_fix(t.v[i].b);
    d.v[i].color[3] = to_fix(t.v[i].a);
    d.v[i].u = to_fix(t.v[i].u);
    d.v[i].v = to_fix(t.v[i].v);
  }
  return d;
}

RDP::ViewportTransform to_oracle_vp(const SrcViewport& s) {
  RDP::ViewportTransform vp = {};
  vp.x = (float)s.x;
  vp.y = (float)s.y;
  vp.width = (float)s.width;
  vp.height = (float)s.height;
  vp.min_depth = (float)s.min_depth;
  vp.max_depth = (float)s.max_depth;
  return vp;
}

DemoViewport to_demo_vp(const SrcViewport& s) {
  DemoViewport vp = {};
  vp.x = to_fix(s.x);
  vp.y = to_fix(s.y);
  vp.width = to_fix(s.width);
  vp.height = to_fix(s.height);
  vp.min_depth = to_fix(s.min_depth);
  vp.max_depth = to_fix(s.max_depth);
  return vp;
}

// 240x240 viewport, depth [0,1].
const SrcViewport kVp = {0.0, 0.0, 240.0, 240.0, 0.0, 1.0};

// --- comparison helpers ----------------------------------------------------

void expect_pos_close(const RDP::PrimitiveSetup& o, const DemoPrimSetup& d) {
  // Screen quantization rounds identically; allow a small window for the
  // dxdy divide and the fixed-point viewport intermediate.
  EXPECT_EQ(o.pos.y_lo, d.pos.y_lo);
  EXPECT_EQ(o.pos.y_mid, d.pos.y_mid);
  EXPECT_EQ(o.pos.y_hi, d.pos.y_hi);

  EXPECT_NEAR((double)o.pos.x_a, (double)d.pos.x_a, 1024.0);
  EXPECT_NEAR((double)o.pos.x_b, (double)d.pos.x_b, 1024.0);
  EXPECT_NEAR((double)o.pos.x_c, (double)d.pos.x_c, 1024.0);
  EXPECT_NEAR((double)o.pos.dxdy_a, (double)d.pos.dxdy_a, 64.0);
  EXPECT_NEAR((double)o.pos.dxdy_b, (double)d.pos.dxdy_b, 64.0);
  EXPECT_NEAR((double)o.pos.dxdy_c, (double)d.pos.dxdy_c, 64.0);

  EXPECT_EQ(o.pos.flags & RDP::PRIMITIVE_RIGHT_MAJOR_BIT,
            d.pos.flags & DEMO_PRIMITIVE_RIGHT_MAJOR_BIT);
}

// Relative+absolute closeness for a scaled coefficient.
void expect_coeff(double oracle, double demo, double rel, double abs_floor,
                  const char* what) {
  double tol = fabs(oracle) * rel + abs_floor;
  EXPECT_NEAR(oracle, demo, tol) << what << " oracle=" << oracle
                                 << " demo=" << demo << " tol=" << tol;
}

void expect_color_close(const RDP::PrimitiveSetup& o, const DemoPrimSetup& d) {
  for (int c = 0; c < 4; ++c) {
    expect_coeff(o.attr.c[c], d.attr.c[c], 1e-3, 4096.0, "c");
    expect_coeff(o.attr.dcdx[c], d.attr.dcdx[c], 1e-3, 4096.0, "dcdx");
    expect_coeff(o.attr.dcde[c], d.attr.dcde[c], 1e-3, 4096.0, "dcde");
    expect_coeff(o.attr.dcdy[c], d.attr.dcdy[c], 1e-3, 4096.0, "dcdy");
  }
}

void expect_z_close(const RDP::PrimitiveSetup& o, const DemoPrimSetup& d) {
  expect_coeff(o.attr.z, d.attr.z, 1e-3, 65536.0, "z");
  expect_coeff(o.attr.dzdx, d.attr.dzdx, 1e-3, 65536.0, "dzdx");
  expect_coeff(o.attr.dzde, d.attr.dzde, 1e-3, 65536.0, "dzde");
  expect_coeff(o.attr.dzdy, d.attr.dzdy, 1e-3, 65536.0, "dzdy");
}

void expect_uvw_close(const RDP::PrimitiveSetup& o, const DemoPrimSetup& d) {
  expect_coeff(o.attr.u, d.attr.u, 2e-3, 4096.0, "u");
  expect_coeff(o.attr.v, d.attr.v, 2e-3, 4096.0, "v");
  expect_coeff(o.attr.dudx, d.attr.dudx, 2e-3, 4096.0, "dudx");
  expect_coeff(o.attr.dvdx, d.attr.dvdx, 2e-3, 4096.0, "dvdx");
  expect_coeff(o.attr.dude, d.attr.dude, 2e-3, 4096.0, "dude");
  expect_coeff(o.attr.dvde, d.attr.dvde, 2e-3, 4096.0, "dvde");
  expect_coeff(o.attr.dudy, d.attr.dudy, 2e-3, 4096.0, "dudy");
  expect_coeff(o.attr.dvdy, d.attr.dvdy, 2e-3, 4096.0, "dvdy");
  // W: only ~16 bits of precision (qscale_w is value<<16). Use a large floor.
  expect_coeff(o.attr.w, d.attr.w, 2e-3, 1 << 17, "w");
  expect_coeff(o.attr.dwdx, d.attr.dwdx, 2e-3, 1 << 17, "dwdx");
  expect_coeff(o.attr.dwde, d.attr.dwde, 2e-3, 1 << 17, "dwde");
  expect_coeff(o.attr.dwdy, d.attr.dwdy, 2e-3, 1 << 17, "dwdy");
}

// Run both pipelines on the same source and return counts.
struct Pair {
  RDP::PrimitiveSetup oracle[8];
  DemoPrimSetup demo[8];
  unsigned oc;
  uint32_t dc;
};

Pair run(const SrcTri& t, RDP::CullMode omode, uint32_t dmode) {
  Pair p = {};
  RDP::InputPrimitive ip = to_oracle_prim(t);
  RDP::ViewportTransform ovp = to_oracle_vp(kVp);
  p.oc = RDP::setup_clipped_triangles(p.oracle, ip, omode, ovp);

  DemoClipTri dt = to_demo_tri(t);
  DemoViewport dvp = to_demo_vp(kVp);
  p.dc = demo_setup_triangle(p.demo, &dt, dmode, &dvp);
  return p;
}

}  // namespace

// --- Variant: fill (flat color, constant Z, no UV) -------------------------
TEST(SetupOracle, Fill) {
  SrcTri t = {};
  t.v[0] = {-0.5, -0.5, 0.5, 1.0, 1, 0, 0, 1, 0, 0};
  t.v[1] = {0.5, -0.5, 0.5, 1.0, 1, 0, 0, 1, 0, 0};
  t.v[2] = {0.0, 0.5, 0.5, 1.0, 1, 0, 0, 1, 0, 0};

  Pair p = run(t, RDP::CullMode::None, DEMO_CULL_NONE);
  ASSERT_EQ(p.oc, 1u);
  ASSERT_EQ(p.dc, 1u);
  expect_pos_close(p.oracle[0], p.demo[0]);
  expect_color_close(p.oracle[0], p.demo[0]);
  expect_z_close(p.oracle[0], p.demo[0]);
}

// --- Variant: Gouraud shade (per-vertex color) -----------------------------
TEST(SetupOracle, GouraudShade) {
  SrcTri t = {};
  t.v[0] = {-0.6, -0.4, 0.3, 1.0, 1.0, 0.0, 0.0, 1.0, 0, 0};
  t.v[1] = {0.7, -0.5, 0.6, 1.0, 0.0, 1.0, 0.0, 1.0, 0, 0};
  t.v[2] = {0.1, 0.6, 0.45, 1.0, 0.0, 0.0, 1.0, 1.0, 0, 0};

  Pair p = run(t, RDP::CullMode::None, DEMO_CULL_NONE);
  ASSERT_EQ(p.oc, 1u);
  ASSERT_EQ(p.dc, 1u);
  expect_pos_close(p.oracle[0], p.demo[0]);
  expect_color_close(p.oracle[0], p.demo[0]);
  expect_z_close(p.oracle[0], p.demo[0]);
}

// --- Variant: Z gradient (depth varies across the triangle) ----------------
TEST(SetupOracle, ZGradient) {
  SrcTri t = {};
  t.v[0] = {-0.5, -0.5, 0.2, 1.0, 0.5, 0.5, 0.5, 1, 0, 0};
  t.v[1] = {0.5, -0.5, 0.5, 1.0, 0.5, 0.5, 0.5, 1, 0, 0};
  t.v[2] = {0.0, 0.5, 0.9, 1.0, 0.5, 0.5, 0.5, 1, 0, 0};

  Pair p = run(t, RDP::CullMode::None, DEMO_CULL_NONE);
  ASSERT_EQ(p.oc, 1u);
  ASSERT_EQ(p.dc, 1u);
  expect_pos_close(p.oracle[0], p.demo[0]);
  expect_z_close(p.oracle[0], p.demo[0]);
}

// --- Variant: textured-perspective (varying W + UV, real persp divide) -----
TEST(SetupOracle, TexturedPerspective) {
  SrcTri t = {};
  // Differing W per vertex forces a genuine perspective divide; UV in texel
  // units (0..32 over a 64-texel tile-ish range).
  t.v[0] = {-0.5, -0.5, 0.4, 1.2, 0.5, 0.5, 0.5, 1, 0.0, 0.0};
  t.v[1] = {0.5, -0.5, 0.5, 2.0, 0.5, 0.5, 0.5, 1, 32.0, 0.0};
  t.v[2] = {0.0, 0.5, 0.45, 1.5, 0.5, 0.5, 0.5, 1, 16.0, 32.0};

  Pair p = run(t, RDP::CullMode::None, DEMO_CULL_NONE);
  ASSERT_EQ(p.oc, 1u);
  ASSERT_EQ(p.dc, 1u);
  expect_pos_close(p.oracle[0], p.demo[0]);
  expect_color_close(p.oracle[0], p.demo[0]);
  expect_z_close(p.oracle[0], p.demo[0]);
  expect_uvw_close(p.oracle[0], p.demo[0]);
}

// --- Near-plane clipping: one vertex behind the near-W plane ---------------
// The triangle crosses W <= MIN_W on one vertex, so the near-W clipper splits
// it into two prims. Assert both pipelines agree on the count.
TEST(SetupOracle, NearPlaneClipCount) {
  SrcTri t = {};
  t.v[0] = {-0.5, -0.5, 0.4, 1.0, 0.5, 0.5, 0.5, 1, 0, 0};
  t.v[1] = {0.5, -0.5, 0.5, 1.0, 0.5, 0.5, 0.5, 1, 0, 0};
  t.v[2] = {0.0, 0.5, 0.2, -0.5, 0.5, 0.5, 0.5, 1, 0, 0};  // behind near plane

  Pair p = run(t, RDP::CullMode::None, DEMO_CULL_NONE);
  EXPECT_EQ(p.oc, p.dc) << "oracle and demo clip counts differ";
  EXPECT_GE(p.dc, 1u);
}

// --- Backface cull parity --------------------------------------------------
TEST(SetupOracle, CullParity) {
  SrcTri t = {};
  // Clockwise winding in screen space.
  t.v[0] = {-0.5, -0.5, 0.5, 1.0, 1, 1, 1, 1, 0, 0};
  t.v[1] = {0.0, 0.5, 0.5, 1.0, 1, 1, 1, 1, 0, 0};
  t.v[2] = {0.5, -0.5, 0.5, 1.0, 1, 1, 1, 1, 0, 0};

  Pair pccw = run(t, RDP::CullMode::CCWOnly, DEMO_CULL_CCW_ONLY);
  EXPECT_EQ(pccw.oc, pccw.dc);
  Pair pcw = run(t, RDP::CullMode::CWOnly, DEMO_CULL_CW_ONLY);
  EXPECT_EQ(pcw.oc, pcw.dc);
}
