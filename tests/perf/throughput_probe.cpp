/* Stream D.1 / M6 — host throughput probe for the serialized pixel pipeline.
 *
 * Drives a REPRESENTATIVE full-pipeline workload (textured + 2-cycle combined +
 * blended + AA/coverage, depth-tested) through OUR renderer's per-command
 * dispatch (rdpx_rdp_cmd) and measures host Mpixels/s. The number feeds
 * docs/perf-extrapolation.md, which extrapolates to the 133 MHz Cortex-M0+ for
 * the M6 device-feasibility go/no-go.
 *
 * This is NOT a conformance test: we do not compare against the oracle here.
 * We reuse the vendored CommandBuilder (the same path the conformance harness
 * uses) so the emitted RDP command words are byte-identical to what the device
 * would replay, then feed them straight into rdpx_rdp_cmd(0, words).
 *
 * The workload fills ~240x240 (the PicoSystem panel) with two large triangles
 * per "frame", so each frame touches ~2*240*240/2 = 57600 covered pixels (the
 * two triangles tile the 240x240 quad). We time many frames and report:
 *   - frames/s and ms/frame
 *   - Mpixels/s (covered pixels per second)
 *
 * Host-only test infrastructure (NOT orthodox-enforced — matches the vendored
 * harness's Modern-C++ idioms locally, per CLAUDE.md "Pragmatism").
 */

#include "rdp_command_builder.hpp"
#include "rdp_common.hpp"
#include "triangle_converter.hpp"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#if defined(_WIN32)
#include <windows.h>
#endif

#include <vector>

extern "C" {
#include "n64video.h"

uint8_t *rdpx_get_hidden_rdram(void);
uint32_t rdpx_get_hidden_rdram_size(void);
void rdpx_rdp_cmd(uint32_t wid, const uint32_t *args);
}

using namespace RDP;

// ---------------------------------------------------------------------------
// Listener that forwards every CommandBuilder-emitted RDP command straight into
// our renderer's per-command dispatch, and applies RDRAM updates to the
// caller-owned buffer. Mirrors OursReplayer's command()/update_rdram().
// ---------------------------------------------------------------------------
struct ProbeListener : CommandListenerInterface {
  uint8_t *rdram;
  size_t rdram_size;

  ProbeListener(uint8_t *r, size_t n) : rdram(r), rdram_size(n) {}

  void set_vi_register(VIRegister, uint32_t) override {}
  void signal_complete() override {}
  void end_frame() override {}
  void eof() override {}

  void command(Op, uint32_t, const uint32_t *words) override {
    rdpx_rdp_cmd(0, words);
  }

  void update_rdram(const void *data, size_t size, size_t offset) override {
    if (offset + size <= rdram_size) memcpy(rdram + offset, data, size);
  }

  void update_hidden_rdram(const void *data, size_t size, size_t offset) override {
    uint8_t *h = rdpx_get_hidden_rdram();
    if (offset + size <= rdpx_get_hidden_rdram_size()) memcpy(h + offset, data, size);
  }
};

static void mi_intr_noop(void) {}

// Two large triangles covering the full clip volume; with the viewport below
// they tile the framebuffer. Vertices are clip-space (x,y already * w with w=1).
static void fill_quad_prims(InputPrimitive *a, InputPrimitive *b) {
  // tri A: top-left, top-right, bottom-left
  // tri B: bottom-left, top-right, bottom-right
  const float corners[4][2] = {
      {-1.0f, -1.0f},  // 0 TL
      {1.0f, -1.0f},   // 1 TR
      {-1.0f, 1.0f},   // 2 BL
      {1.0f, 1.0f},    // 3 BR
  };
  const float uv[4][2] = {{0, 0}, {64, 0}, {0, 64}, {64, 64}};
  const int ia[3] = {0, 1, 2};
  const int ib[3] = {2, 1, 3};

  memset(a, 0, sizeof *a);
  memset(b, 0, sizeof *b);
  for (int i = 0; i < 3; i++) {
    a->vertices[i].x = corners[ia[i]][0];
    a->vertices[i].y = corners[ia[i]][1];
    a->vertices[i].z = 0.5f;
    a->vertices[i].w = 1.0f;
    a->vertices[i].u = uv[ia[i]][0];
    a->vertices[i].v = uv[ia[i]][1];
    for (int j = 0; j < 4; j++) a->vertices[i].color[j] = 0.5f + 0.1f * float(i);

    b->vertices[i].x = corners[ib[i]][0];
    b->vertices[i].y = corners[ib[i]][1];
    b->vertices[i].z = 0.5f;
    b->vertices[i].w = 1.0f;
    b->vertices[i].u = uv[ib[i]][0];
    b->vertices[i].v = uv[ib[i]][1];
    for (int j = 0; j < 4; j++) b->vertices[i].color[j] = 0.4f + 0.1f * float(i);
  }
}

static double now_seconds(void) {
#if defined(_WIN32)
  LARGE_INTEGER freq, count;
  QueryPerformanceFrequency(&freq);
  QueryPerformanceCounter(&count);
  return (double)count.QuadPart / (double)freq.QuadPart;
#else
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return (double)ts.tv_sec + (double)ts.tv_nsec * 1e-9;
#endif
}

int main(int argc, char **argv) {
  // Panel is 240x240. Use a 240-wide RGBA16 color buffer + a depth buffer.
  const uint32_t kWidth = 240;
  const uint32_t kHeight = 240;

  unsigned warmup_frames = 50;
  unsigned measure_frames = 2000;
  for (int i = 1; i < argc; i++) {
    if (!strcmp(argv[i], "--frames") && i + 1 < argc)
      measure_frames = unsigned(strtoul(argv[++i], nullptr, 0));
    else if (!strcmp(argv[i], "--warmup") && i + 1 < argc)
      warmup_frames = unsigned(strtoul(argv[++i], nullptr, 0));
  }

  // RDRAM layout: color image at 0, depth image after it, texture after that.
  std::vector<uint8_t> rdram(RDRAM_MAX_SIZE, 0);

  const uint32_t color_addr = 0;
  const uint32_t depth_addr = kWidth * kHeight * 2;               // 16bpp color
  const uint32_t tex_addr = depth_addr + kWidth * kHeight * 2;    // 16bpp depth
  // A small RGBA16 texture (64x64) in RDRAM.
  const uint32_t tex_w = 64, tex_h = 64;
  for (uint32_t t = 0; t < tex_w * tex_h; t++) {
    uint16_t texel = uint16_t((t * 2654435761u) >> 16) | 1u;  // RGBA5551, alpha=1
    ((uint16_t *)(rdram.data() + tex_addr))[t] = texel;
  }

  uint32_t vi_regs[VI_NUM_REG] = {};
  uint32_t dp_regs[DP_NUM_REG] = {};
  uint32_t irq = 0;
  uint32_t *p_vi[VI_NUM_REG];
  uint32_t *p_dp[DP_NUM_REG];
  for (unsigned i = 0; i < VI_NUM_REG; i++) p_vi[i] = &vi_regs[i];
  for (unsigned i = 0; i < DP_NUM_REG; i++) p_dp[i] = &dp_regs[i];

  struct n64video_config config = {};
  config.gfx.rdram = rdram.data();
  config.gfx.rdram_size = uint32_t(rdram.size());
  config.gfx.vi_reg = p_vi;
  config.gfx.dp_reg = p_dp;
  config.gfx.mi_intr_reg = &irq;
  config.gfx.mi_intr_cb = mi_intr_noop;
  config.vi.mode = VI_MODE_NORMAL;
  config.vi.interp = VI_INTERP_LINEAR;
  config.dp.compat = DP_COMPAT_HIGH;
  rdpx_video_init(&config);

  ProbeListener listener(rdram.data(), rdram.size());
  CommandBuilder builder;
  builder.set_command_interface(&listener);

  // ---- Representative full-pipeline state (textured + combined + blended +
  //      AA/coverage, depth-tested), 2-cycle. ----
  builder.set_color_image(TextureFormat::RGBA, TextureSize::Bpp16, color_addr, kWidth);
  builder.set_depth_image(depth_addr);
  builder.set_scissor(0, 0, kWidth, kHeight);
  builder.set_viewport({0, 0, float(kWidth), float(kHeight), 0, 1});

  builder.set_cycle_type(CycleType::Cycle2);

  // Texture image + tile.
  builder.set_texture_image(tex_addr, TextureFormat::RGBA, TextureSize::Bpp16, tex_w);
  TileMeta tile = {};
  tile.fmt = TextureFormat::RGBA;
  tile.size = TextureSize::Bpp16;
  tile.stride = tex_w * 2 / 8;  // 64-bit words per row
  tile.offset = 0;
  tile.mask_s = 6;  // 64
  tile.mask_t = 6;
  builder.set_tile(0, tile);
  builder.set_tile_size(0, 0, 0, tex_w, tex_h);
  builder.load_tile(0, 0, 0, tex_w, tex_h);

  // 2-cycle combiner: (TEX0 * SHADE) then modulate with PRIM. Representative
  // "lit textured" combine.
  builder.set_combiner_2cycle(
      {{RGBMulAdd::Texel0, RGBMulSub::Zero, RGBMul::Shade, RGBAdd::Zero},
       {AlphaAddSub::Zero, AlphaAddSub::Zero, AlphaMul::Zero, AlphaAddSub::Texel0Alpha}},
      {{RGBMulAdd::Combined, RGBMulSub::Zero, RGBMul::Primitive, RGBAdd::Zero},
       {AlphaAddSub::Zero, AlphaAddSub::Zero, AlphaMul::Zero, AlphaAddSub::CombinedAlpha}});

  // Blender: src-over against framebuffer (alpha blend), 2-cycle.
  builder.set_blend_mode(0, BlendMode1A::PixelColor, BlendMode1B::ShadeAlpha,
                         BlendMode2A::FogColor, BlendMode2B::InvPixelAlpha);
  builder.set_blend_mode(1, BlendMode1A::PixelColor, BlendMode1B::PixelAlpha,
                         BlendMode2A::MemoryColor, BlendMode2B::InvPixelAlpha);
  builder.set_enable_blend(true);
  builder.set_fog_color(40, 60, 90, 255);
  builder.set_blend_color(0, 0, 0, 130);
  builder.set_env_color(200, 180, 160, 255);
  builder.set_primitive_color(16, 0xaa, 220, 210, 200, 255);

  builder.set_enable_aa(true);
  builder.set_coverage_mode(CoverageMode::Clamp);
  builder.set_image_read_enable(true);  // AA reads framebuffer coverage
  builder.set_depth_test(true);
  builder.set_depth_write(true);
  builder.set_z_mode(ZMode::Opaque);
  builder.set_perspective(true);
  builder.set_dither(RGBDitherMode::Magic);
  builder.set_dither(AlphaDitherMode::Pattern);

  InputPrimitive triA, triB;
  fill_quad_prims(&triA, &triB);

  // Covered pixels per frame: the two triangles tile the kWidth x kHeight quad.
  const double covered_px_per_frame = double(kWidth) * double(kHeight);

  // ---- Warmup ----
  for (unsigned f = 0; f < warmup_frames; f++) {
    builder.draw_triangle(triA);
    builder.draw_triangle(triB);
  }

  // ---- Measure ----
  double t0 = now_seconds();
  for (unsigned f = 0; f < measure_frames; f++) {
    builder.draw_triangle(triA);
    builder.draw_triangle(triB);
  }
  double t1 = now_seconds();

  rdpx_video_close();

  double elapsed = t1 - t0;
  if (elapsed <= 0.0) elapsed = 1e-9;
  double frames_per_s = double(measure_frames) / elapsed;
  double ms_per_frame = 1000.0 * elapsed / double(measure_frames);
  double total_px = covered_px_per_frame * double(measure_frames);
  double mpix_per_s = (total_px / elapsed) / 1e6;

  printf("=== M6 host throughput probe ===\n");
  printf("workload: 2-cycle textured(RGBA16) + combined + blended(alpha) + "
         "AA/coverage + depth, perspective\n");
  printf("framebuffer: %ux%u RGBA5551, depth %ux%u\n", kWidth, kHeight, kWidth, kHeight);
  printf("frames measured: %u (warmup %u)\n", measure_frames, warmup_frames);
  printf("covered pixels/frame: %.0f\n", covered_px_per_frame);
  printf("elapsed: %.4f s\n", elapsed);
  printf("frames/s: %.1f\n", frames_per_s);
  printf("ms/frame (240x240 full): %.4f\n", ms_per_frame);
  printf("Mpixels/s: %.2f\n", mpix_per_s);

  return 0;
}
