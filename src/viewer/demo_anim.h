// demo_anim.h — runtime per-frame model matrices at gldemo's intended rotation
// rate, for the viewer's live playback.
//
// The baked tables (src/demo/generated/baked_matrices.h) sample a full 360deg
// in 30/40 coarse frames (12deg/9deg per frame) so the device loop is a short 2
// s. gldemo actually rotates +0.2deg (pyramid) / -0.15deg (cube) per frame at
// 60fps (render.c: *_DEG_PER_FRAME = 12.0/60.0, 9.0/60.0). To match that
// on-screen speed smoothly we compute the model matrix per 60fps frame at the
// exact angle instead of indexing the coarse table. Host-only (uses float
// trig); the device demo and golden tests keep using the baked tables.
//
// Output is Q16.16 row-major int32[16], identical layout to the baked tables,
// so it feeds demo_build_frame_models() unchanged.

#ifndef RDP_VIEWER_DEMO_ANIM_H
#define RDP_VIEWER_DEMO_ANIM_H

#include <stdint.h>

// Full visual loop in 60fps frames: lcm(360/0.2 = 1800, 360/0.15 = 2400) = 7200
// frames = 120 s. frame_t wraps here for display/stepping.
#define DEMO_ANIM_GLDEMO_PERIOD 7200u

// Pyramid MODEL at continuous 60fps frame t: rotate(+0.2*t deg, +Y) *
// translate(-75,0,0). out16 receives 16 Q16.16 row-major int32.
void demo_anim_pyramid_model(double frame_t, int32_t *out16);

// Cube MODEL at continuous 60fps frame t: rotate(-0.15*t deg, (1,1,1)) *
// translate(75,0,-50).
void demo_anim_cube_model(double frame_t, int32_t *out16);

#endif  // RDP_VIEWER_DEMO_ANIM_H
