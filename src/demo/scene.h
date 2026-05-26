#pragma once

// demo_core per-object scene helpers (the fan-in between the baked geometry/
// matrices and the setup+emit pipeline). Internal to demo_core: demo.c is the
// only consumer. Freestanding / Orthodox (plain C, POD, no heap, no float).
//
// Each helper transforms one object's vertices through its
// model-view-projection matrix into clip space (Q16.16), builds per-triangle
// DemoClipTri records, runs demo_setup_triangle (backface cull), and emits the
// resulting RDP triangle commands into the sink. Render state (combine /
// other-modes / image / tile) is set by demo.c before calling these; the
// helpers emit geometry only.

#include <stdint.h>

#include "cmd_sink.h"
#include "fixed.h"
#include "setup.h"

#ifdef __cplusplus
extern "C" {
#endif

// Build the combined model-view-projection matrix for an object whose per-frame
// model matrix is `model16` (16 Q16.16 row-major entries). out = model * view *
// proj (row-vector convention v' = v . M, matching baked_matrices.h).
void scene_build_mvp(struct DemoMat4 *out, const int32_t *model16,
                     const int32_t *view16, const int32_t *proj16);

// Transform + setup + emit the rotating Gouraud pyramid (12 verts / 4 tris,
// vertex-colored, no texture). `mvp` is the object's combined matrix; `vp` the
// viewport transform; backface cull mode is DEMO_CULL_* . Emits only triangle
// commands (caller has already set combine/other-modes/images).
void scene_draw_pyramid(struct CmdSink *sink, const struct DemoMat4 *mvp,
                        const struct DemoViewport *vp, uint32_t cull);

// Transform + setup + emit the rotating arrows-textured cube (24 verts /
// 12 tris, per-vertex texel u,v + vertex color). Perspective-correct texturing
// (handled inside demo_setup_triangle). Caller has set the CI4 tile + TLUT +
// texel*shade combine before calling.
void scene_draw_cube(struct CmdSink *sink, const struct DemoMat4 *mvp,
                     const struct DemoViewport *vp, uint32_t cull);

#ifdef __cplusplus
}  // extern "C"
#endif
