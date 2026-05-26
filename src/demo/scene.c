// demo_core per-object scene helpers — see scene.h.
//
// Transforms the baked geometry (src/demo/generated/) through each object's
// MVP into Q16.16 clip space, builds DemoClipTri records, runs the fixed-point
// triangle setup (backface cull), and emits the RDP triangle commands. Pure
// integer / freestanding: no malloc / printf / float.
//
// Conventions consumed (frozen contracts):
//   - baked_matrices.h: 16x int32 Q16.16 row-major, row-vector v' = v . M.
//   - geometry.h: positions int16 model-space; colors RGBA8; cube texel u,v in
//     [0,64] (int16). DemoClipVert wants color in [0,1] Q16.16, u,v in texel
//     units Q16.16 (the RDP S/T 2^6 scale + persp-correct interp are applied by
//     demo_setup_triangle; mirrors the oracle, which applies no texel-center
//     offset — its u_offset block is #if 0'd).

#include "scene.h"

#include "emit.h"
#include "generated/baked_matrices.h"
#include "generated/geometry.h"

// int16 model-space coordinate -> Q16.16 (value << 16).
static demo_fix scene_pos_to_fix(int16_t v) {
  return (demo_fix)((int32_t)v << DEMO_FIX_SHIFT);
}

// 8-bit color channel [0,255] -> [0,1] Q16.16: round(c/255 * 2^16).
static demo_fix scene_color_to_fix(uint8_t c) {
  return (demo_fix)((((uint32_t)c * DEMO_FIX_ONE) + 127U) / 255U);
}

// int16 texel coordinate [0,64] -> texel-unit Q16.16 (value << 16).
static demo_fix scene_uv_to_fix(int16_t v) {
  return (demo_fix)((int32_t)v << DEMO_FIX_SHIFT);
}

void scene_build_mvp(struct DemoMat4 *out, const int32_t *model16,
                     const int32_t *view16, const int32_t *proj16) {
  struct DemoMat4 model;
  struct DemoMat4 view;
  struct DemoMat4 proj;
  struct DemoMat4 mv;
  for (int32_t i = 0; i < 16; ++i) {
    model.m[i] = (demo_fix)model16[i];
    view.m[i] = (demo_fix)view16[i];
    proj.m[i] = (demo_fix)proj16[i];
  }
  // mv = model * view ; out = mv * proj. Row-vector clip = v . MODEL . VIEW .
  // PROJ.
  demo_mat4_mul(&mv, &model, &view);
  demo_mat4_mul(out, &mv, &proj);
}

// Transform one untextured (Gouraud) vertex into clip space.
static void scene_xform_vtx(struct DemoClipVert *out, const struct DemoVtx *in,
                            const struct DemoMat4 *mvp) {
  demo_fix model_pos[4];
  demo_fix clip[4];
  model_pos[0] = scene_pos_to_fix(in->x);
  model_pos[1] = scene_pos_to_fix(in->y);
  model_pos[2] = scene_pos_to_fix(in->z);
  model_pos[3] = DEMO_FIX_ONE;
  demo_mat4_mul_vec4(clip, mvp, model_pos);
  out->x = clip[0];
  out->y = clip[1];
  out->z = clip[2];
  out->w = clip[3];
  out->color[0] = scene_color_to_fix(in->r);
  out->color[1] = scene_color_to_fix(in->g);
  out->color[2] = scene_color_to_fix(in->b);
  out->color[3] = scene_color_to_fix(in->a);
  out->u = 0;
  out->v = 0;
}

// Transform one textured vertex (carries texel u,v) into clip space.
static void scene_xform_vtx_t(struct DemoClipVert *out,
                              const struct DemoVtxT *in,
                              const struct DemoMat4 *mvp) {
  demo_fix model_pos[4];
  demo_fix clip[4];
  model_pos[0] = scene_pos_to_fix(in->x);
  model_pos[1] = scene_pos_to_fix(in->y);
  model_pos[2] = scene_pos_to_fix(in->z);
  model_pos[3] = DEMO_FIX_ONE;
  demo_mat4_mul_vec4(clip, mvp, model_pos);
  out->x = clip[0];
  out->y = clip[1];
  out->z = clip[2];
  out->w = clip[3];
  out->color[0] = scene_color_to_fix(in->r);
  out->color[1] = scene_color_to_fix(in->g);
  out->color[2] = scene_color_to_fix(in->b);
  out->color[3] = scene_color_to_fix(in->a);
  out->u = scene_uv_to_fix(in->u);
  out->v = scene_uv_to_fix(in->v);
}

// Setup + emit the prims a single clip-space triangle expands to.
static void scene_emit_tri(struct CmdSink *sink, const struct DemoClipTri *tri,
                           uint32_t cull, const struct DemoViewport *vp) {
  struct DemoPrimSetup prims[DEMO_SETUP_MAX_PRIMS];
  uint32_t n = demo_setup_triangle(prims, tri, cull, vp);
  for (uint32_t i = 0; i < n; ++i) {
    emit_triangle(sink, &prims[i]);
  }
}

void scene_draw_pyramid(struct CmdSink *sink, const struct DemoMat4 *mvp,
                        const struct DemoViewport *vp, uint32_t cull) {
  struct DemoClipVert verts[DEMO_PYRAMID_VTX_COUNT];
  for (int32_t i = 0; i < DEMO_PYRAMID_VTX_COUNT; ++i) {
    scene_xform_vtx(&verts[i], &demo_pyramid_vtx[i], mvp);
  }
  for (int32_t t = 0; t < DEMO_PYRAMID_TRI_COUNT; ++t) {
    struct DemoClipTri tri;
    tri.v[0] = verts[demo_pyramid_tri[t].a];
    tri.v[1] = verts[demo_pyramid_tri[t].b];
    tri.v[2] = verts[demo_pyramid_tri[t].c];
    scene_emit_tri(sink, &tri, cull, vp);
  }
}

void scene_draw_cube(struct CmdSink *sink, const struct DemoMat4 *mvp,
                     const struct DemoViewport *vp, uint32_t cull) {
  struct DemoClipVert verts[DEMO_CUBE_VTX_COUNT];
  for (int32_t i = 0; i < DEMO_CUBE_VTX_COUNT; ++i) {
    scene_xform_vtx_t(&verts[i], &demo_cube_vtx[i], mvp);
  }
  for (int32_t t = 0; t < DEMO_CUBE_TRI_COUNT; ++t) {
    struct DemoClipTri tri;
    tri.v[0] = verts[demo_cube_tri[t].a];
    tri.v[1] = verts[demo_cube_tri[t].b];
    tri.v[2] = verts[demo_cube_tri[t].c];
    scene_emit_tri(sink, &tri, cull, vp);
  }
}
