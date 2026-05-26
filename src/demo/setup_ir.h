#pragma once

// demo_core fixed-point triangle setup IR.
//
// This is a plain Orthodox POD mirror of the vendored (MIT) test oracle struct
// tests/conformance/vendor/primitive_setup.hpp (RDP::PrimitiveSetup): identical
// field layout and semantics, so the fixed-point coefficients demo_core
// produces are field-to-field comparable (within tolerance) against the
// float-derived oracle in tests. We keep our own struct rather than depend on
// the C++-namespaced original so demo_core stays freestanding/Orthodox.
//
// Semantics (from the oracle): subpixel precision is 1/(1<<SUBPIXELS_LOG2).
// Edge x_* are subpixel screen-X intercepts; dxdy_* are per-scanline slopes;
// y_lo/y_mid/y_hi are subpixel scanline bounds. Attr c/dcdx/dcde/dcdy are the
// per-attribute value and its X / edge / Y derivatives; z and u/v/w carry their
// own X/edge/Y derivatives. These are RDP setup coefficients, NOT Q16.16.

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Mirrors RDP::PrimitiveFlagBits / PrimitiveFlags (uint16_t flags field).
enum DemoPrimitiveFlagBits {
  DEMO_PRIMITIVE_RIGHT_MAJOR_BIT = 1 << 0,
  DEMO_PRIMITIVE_PERSPECTIVE_CORRECT_BIT = 1 << 1,
  DEMO_PRIMITIVE_FLAG_MAX_ENUM = 0x7fff
};

// Mirrors RDP::SUBPIXELS_LOG2.
enum { DEMO_SUBPIXELS_LOG2 = 2 };

// Mirrors RDP::PrimitiveSetupPos.
struct DemoPrimSetupPos {
  int32_t x_a, x_b, x_c;
  int32_t dxdy_a, dxdy_b, dxdy_c;
  int16_t y_lo, y_mid, y_hi;
  uint16_t flags;
};

// Mirrors RDP::PrimitiveSetupAttr.
struct DemoPrimSetupAttr {
  int32_t c[4];
  int32_t dcdx[4];
  int32_t dcde[4];
  int32_t dcdy[4];

  int32_t z, dzdx, dzde, dzdy;
  int32_t u, v, w;
  int32_t dudx, dvdx, dwdx;
  int32_t dude, dvde, dwde;
  int32_t dudy, dvdy, dwdy;
};

// Mirrors RDP::PrimitiveSetup (pos + attr). Layout matches the oracle, which
// asserts 16-byte alignment of the combined struct.
struct DemoPrimSetup {
  struct DemoPrimSetupPos pos;
  struct DemoPrimSetupAttr attr;
};

#ifdef __cplusplus
}  // extern "C"
#endif
