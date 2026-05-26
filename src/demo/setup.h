#pragma once

// demo_core fixed-point triangle setup (the mini-"RSP" front of the pipeline).
//
// Consumes clip-space vertices (post model/view/projection, BEFORE the
// perspective divide) in Q16.16 fixed-point and a viewport transform, and
// produces up to DEMO_SETUP_MAX_PRIMS fixed-point DemoPrimSetup records ready
// for the command emitter (emit_triangle). It performs, in order, mirroring the
// vendored float oracle (tests/conformance/vendor/triangle_converter.cpp):
//
//   1. near-W clip (so no vertex has W <= ~0, avoiding infinities),
//   2. perspective divide (x,y,z /= w; w -> rescaled 1/w for persp-correct
//      attribute interpolation),
//   3. viewport transform of X/Y to screen space,
//   4. guard-band clip on X / Y and near/far clip on Z (F3DEX2-style near-plane
//      frustum clipping; screen edges left to the RDP hardware scissor),
//   5. backface cull + per-attribute edge-coefficient setup.
//
// The geometry math is plain Q16.16 (demo_fix_mul / demo_fix_div from fixed.h),
// 64-bit integer intermediates throughout; deterministic and freestanding (no
// malloc / printf / float). The output coefficients are NOT Q16.16 — they carry
// the RDP setup scales (see setup_ir.h), matched to the oracle within
// tolerance.
//
// Color is interpolated in [0,1] Q16.16; u/v in texel units Q16.16; z in [0,1]
// clip-space depth Q16.16. The viewport min/max depth and the cull mode follow
// the oracle's semantics.

#include <stdint.h>

#include "fixed.h"
#include "setup_ir.h"

#ifdef __cplusplus
extern "C" {
#endif

// Upper bound on prims a single input triangle can expand to under near-plane
// frustum + guard-band clipping. A convex triangle clipped against 6 half-space
// planes (near, far, +/-X, +/-Y) stays a convex polygon of at most 3+6 = 9
// vertices, i.e. at most 7 triangles in a fan. We size generously to 8 (the
// oracle's PrimitiveSetup prim[8] bound) and assert in setup if exceeded.
enum { DEMO_SETUP_MAX_PRIMS = 8 };

// Backface cull mode (mirrors RDP::CullMode).
enum DemoCullMode {
  DEMO_CULL_NONE = 0,
  DEMO_CULL_CCW_ONLY = 1,  // cull counter-clockwise (signed_area > 0)
  DEMO_CULL_CW_ONLY = 2    // cull clockwise (signed_area < 0)
};

// One input vertex in clip space (pre perspective divide), all Q16.16.
//   x,y,z,w : clip-space homogeneous position
//   color   : RGBA, each in [0,1]
//   u,v     : texture coordinates in texel units (NOT 0..1); the RDP S/T scale
//             (1<<5 per texel) is applied at quantize time, matching the
//             oracle.
struct DemoClipVert {
  demo_fix x, y, z, w;
  demo_fix color[4];
  demo_fix u, v;
};

// One input triangle (three clip-space vertices).
struct DemoClipTri {
  struct DemoClipVert v[3];
};

// Fixed-point viewport transform (mirrors RDP::ViewportTransform), Q16.16.
// Screen X = x + (0.5*ndc_x + 0.5) * width ; likewise Y. Z maps clip-space
// depth [0,1] into [min_depth, max_depth].
struct DemoViewport {
  demo_fix x, y, width, height;
  demo_fix min_depth, max_depth;
};

// Run clip + setup for one input triangle. Writes up to DEMO_SETUP_MAX_PRIMS
// setup records into out[] and returns the count produced (0 if fully clipped
// or culled). cull is an enum DemoCullMode.
uint32_t demo_setup_triangle(struct DemoPrimSetup *out,
                             const struct DemoClipTri *tri, uint32_t cull,
                             const struct DemoViewport *vp);

#ifdef __cplusplus
}  // extern "C"
#endif
