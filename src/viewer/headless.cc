// headless.cc — rdp_viewer headless test harness (see headless.h).
//
// Orthodoxy-EXEMPT (mirrors main.cc / the host harness): SDL3 is a modern-C++
// API; local idioms match it.

#include "headless.h"

#include <SDL3/SDL.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "capture.h"
#include "panel_perf.h"
#include "panels.h"
#include "renderer_host.h"
#include "source_dump.h"
#include "viewer_common.h"

extern "C" {
#include "cmd_decode.h"
}

// RGBA5551 fill for the hardcoded clear: R=12, G=20, B=6, A=1 -> a distinct
// non-black color so a blank frame can't pass the headless assertion.
//   value = (R<<11) | (G<<6) | (B<<1) | A
#define CLEAR_R5 12u
#define CLEAR_G5 20u
#define CLEAR_B5 6u
#define CLEAR_FILL_5551 \
  ((CLEAR_R5 << 11) | (CLEAR_G5 << 6) | (CLEAR_B5 << 1) | 1u)

// ---- headless PPM dump -----------------------------------------------------
// Returns 0 on success (image is 240x240 and uniform, non-black), non-zero
// else.
int headless_dump(const char* path) {
  if (renderer_host_init() != 0) {
    fprintf(stderr, "headless: renderer_host_init failed\n");
    return 1;
  }

  renderer_host_submit_clear((uint16_t)CLEAR_FILL_5551);
  renderer_host_present();

  uint32_t w = 0;
  uint32_t h = 0;
  uint32_t pitch = 0;
  const uint32_t* px = renderer_host_scanout(&w, &h, &pitch);
  if (!px) {
    fprintf(stderr, "headless: no scanout produced\n");
    renderer_host_close();
    return 2;
  }

  if (w != RENDERER_HOST_OUT_WIDTH || h != RENDERER_HOST_OUT_HEIGHT) {
    fprintf(stderr, "headless: scanout %ux%u, expected %dx%d\n", w, h,
            RENDERER_HOST_OUT_WIDTH, RENDERER_HOST_OUT_HEIGHT);
    renderer_host_close();
    return 3;
  }

  // Uniformity + non-black check. Pixels are RGBA32 (byte order R,G,B,A).
  const uint32_t first = px[0];
  const uint8_t r0 = (uint8_t)(first & 0xff);
  const uint8_t g0 = (uint8_t)((first >> 8) & 0xff);
  const uint8_t b0 = (uint8_t)((first >> 16) & 0xff);
  if ((r0 | g0 | b0) == 0) {
    fprintf(stderr, "headless: scanout is black (fill did not render)\n");
    renderer_host_close();
    return 4;
  }
  for (uint32_t i = 0; i < w * h; ++i) {
    const uint32_t rgb = px[i] & 0x00ffffffU;  // ignore alpha
    if (rgb != (first & 0x00ffffffU)) {
      fprintf(stderr, "headless: non-uniform pixel at %u (0x%08x != 0x%08x)\n",
              i, px[i], first);
      renderer_host_close();
      return 5;
    }
  }

  // Write binary PPM (P6): RGB only.
  FILE* f = fopen(path, "wb");
  if (!f) {
    fprintf(stderr, "headless: cannot open %s\n", path);
    renderer_host_close();
    return 6;
  }
  fprintf(f, "P6\n%u %u\n255\n", w, h);
  for (uint32_t i = 0; i < w * h; ++i) {
    uint8_t rgb[3];
    rgb[0] = (uint8_t)(px[i] & 0xff);
    rgb[1] = (uint8_t)((px[i] >> 8) & 0xff);
    rgb[2] = (uint8_t)((px[i] >> 16) & 0xff);
    fwrite(rgb, 1, 3, f);
  }
  fclose(f);

  printf("headless: wrote %s (%ux%u, uniform RGB=%u,%u,%u)\n", path, w, h, r0,
         g0, b0);
  renderer_host_close();
  return 0;
}

// ---- headless demo-frame dump ----------------------------------------------
// Drives demo frame N through the full viewer path (buffer the frame's command
// stream, feed it to the renderer, present via the demo blit) and writes the
// scanout as a binary PPM. Asserts 240x240 and NON-UNIFORM (the rendered scene
// has many distinct colors). Returns 0 on success.
int headless_demo_frame(uint32_t frame_n, const char* path) {
  if (renderer_host_init() != 0) {
    fprintf(stderr, "headless-demo: renderer_host_init failed\n");
    return 1;
  }
  if (seed_demo() != 0) {
    renderer_host_close();
    return 1;
  }

  // Buffer + feed the frame through the same code path the UI uses.
  static struct Playback pb;
  memset(&pb, 0, sizeof(pb));
  pb.frame_index = frame_n;
  playback_init(&pb);  // buffers frame_n's commands, marks dirty (full frame)
  playback_tick(&pb);  // feeds the whole frame + present_demo

  uint32_t w = 0;
  uint32_t h = 0;
  uint32_t pitch = 0;
  const uint32_t* px = renderer_host_scanout(&w, &h, &pitch);
  if (!px) {
    fprintf(stderr, "headless-demo: no scanout produced\n");
    renderer_host_close();
    return 2;
  }
  if (w != RENDERER_HOST_OUT_WIDTH || h != RENDERER_HOST_OUT_HEIGHT) {
    fprintf(stderr, "headless-demo: scanout %ux%u, expected %dx%d\n", w, h,
            RENDERER_HOST_OUT_WIDTH, RENDERER_HOST_OUT_HEIGHT);
    renderer_host_close();
    return 3;
  }

  // Count distinct RGB colors (ignore alpha). Linear scan into a small unique
  // table is fine for a one-shot 240x240 check; bail once clearly non-uniform.
  uint32_t uniq[512];
  uint32_t nuniq = 0;
  for (uint32_t i = 0; i < w * h && nuniq < 512; ++i) {
    const uint32_t rgb = px[i] & 0x00ffffffU;
    uint32_t j = 0;
    for (; j < nuniq; ++j) {
      if (uniq[j] == rgb) {
        break;
      }
    }
    if (j == nuniq) {
      uniq[nuniq++] = rgb;
    }
  }
  if (nuniq <= 1) {
    fprintf(
        stderr,
        "headless-demo: scanout uniform (%u colors) — demo did not render\n",
        nuniq);
    renderer_host_close();
    return 4;
  }

  FILE* f = fopen(path, "wb");
  if (!f) {
    fprintf(stderr, "headless-demo: cannot open %s\n", path);
    renderer_host_close();
    return 5;
  }
  fprintf(f, "P6\n%u %u\n255\n", w, h);
  for (uint32_t i = 0; i < w * h; ++i) {
    uint8_t rgb[3];
    rgb[0] = (uint8_t)(px[i] & 0xff);
    rgb[1] = (uint8_t)((px[i] >> 8) & 0xff);
    rgb[2] = (uint8_t)((px[i] >> 16) & 0xff);
    fwrite(rgb, 1, 3, f);
  }
  fclose(f);

  printf("headless-demo: frame %u -> %s (%ux%u, >=%u distinct colors)\n",
         frame_n, path, w, h, nuniq);
  renderer_host_close();
  return 0;
}

// ---- headless perf check ---------------------------------------------------
// Drives demo frame N through the viewer path, resetting the pixel counter
// before the build and reading it after; times the build+present wall clock;
// prints the pixel count, per-command-type counts, the extrapolated M0+
// ms/frame (perf model), and confirms TMEM is non-zero in the loaded CI4 region
// after the cube's texture load. Asserts pixel_count > 0 and TMEM non-zero.
// Returns 0 on success.
int headless_perf(uint32_t frame_n) {
  if (renderer_host_init() != 0) {
    fprintf(stderr, "headless-perf: renderer_host_init failed\n");
    return 1;
  }
  if (seed_demo() != 0) {
    renderer_host_close();
    return 1;
  }

  static struct Playback pb;
  memset(&pb, 0, sizeof(pb));
  pb.frame_index = frame_n;
  playback_init(&pb);  // buffers frame N's commands, marks dirty (full frame)

  rdpx_reset_pixel_count();
  uint64_t const t0 = SDL_GetPerformanceCounter();
  playback_tick(&pb);  // feeds the whole frame + present_demo
  uint64_t const t1 = SDL_GetPerformanceCounter();
  double const ms =
      (double)(t1 - t0) / (double)SDL_GetPerformanceFrequency() * 1000.0;
  uint64_t const px = rdpx_get_pixel_count();

  // Per-command-type histogram (C.2 decoder over the buffered stream).
  uint32_t by_id[64];
  for (uint32_t i = 0; i < 64; ++i) {
    by_id[i] = 0U;
  }
  uint32_t tri = 0U;
  for (uint32_t i = 0; i < pb.cmd_total; ++i) {
    const struct PlaybackCmd* c = &pb.cmds[i];
    struct CmdRecord rec;
    cmd_decode(&pb.words[c->word_off], c->word_count, &rec);
    by_id[rec.id & 0x3f] += 1U;
    if (rec.is_triangle) {
      tri += 1U;
    }
  }

  // TMEM non-zero check over the loaded CI4 region (low 2 KB).
  const uint8_t* tmem = rdpx_get_tmem();
  uint32_t const tmem_sz = rdpx_get_tmem_size();
  uint32_t nz = 0U;
  uint32_t const scan = tmem_sz < 2048U ? tmem_sz : 2048U;
  if (tmem) {
    for (uint32_t i = 0; i < scan; ++i) {
      if (tmem[i] != 0) {
        ++nz;
      }
    }
  }

  // Extrapolation model (perf-extrapolation.md): 250..700 cyc/px @250 MHz.
  double const opt = perf_extrapolate_m0_ms(px, 250.0, 250000000.0);
  double const pes = perf_extrapolate_m0_ms(px, 700.0, 250000000.0);

  printf("headless-perf: frame %u\n", frame_n);
  printf("  commands=%u triangles=%u\n", pb.cmd_total, tri);
  printf("  pixels=%llu  host_build_present=%.3f ms", (unsigned long long)px,
         ms);
  if (ms > 0.0) {
    printf("  (%.2f Mpix/s)\n", ((double)px / 1.0e6) / (ms / 1000.0));
  } else {
    printf("\n");
  }
  printf(
      "  extrapolated M0+ @250MHz: %.1f .. %.1f ms/frame (~%.0f..%.0f fps)\n",
      opt, pes, pes > 0 ? 1000.0 / pes : 0.0, opt > 0 ? 1000.0 / opt : 0.0);
  printf("  TMEM non-zero bytes in CI4 region: %u / %u\n", nz, scan);
  printf("  command types:");
  for (uint32_t id = 0; id < 64; ++id) {
    if (by_id[id]) {
      printf(" %s=%u", rdp_cmd_name((uint8_t)id), by_id[id]);
    }
  }
  printf("\n");

  int rc = 0;
  if (px == 0) {
    fprintf(stderr, "headless-perf: FAIL pixel count is 0\n");
    rc = 4;
  }
  if (nz == 0) {
    fprintf(stderr, "headless-perf: FAIL TMEM CI4 region all zero\n");
    rc = rc ? rc : 5;
  }

  renderer_host_close();
  return rc;
}

// ---- headless capture -> replay round-trip ---------------------------------
// (a) render demo frame N directly -> scanout A; (b) capture frame N to a temp
// .rdp (DumpPlayer-compatible); (c) replay that .rdp via the dump source ->
// scanout B; (d) assert A == B byte-identical. Confirms the capture writer
// produces a file the canonical reader parses AND that the replay path
// reproduces the frame exactly. Returns 0 on success.
int headless_capture_roundtrip(uint32_t frame_n) {
  if (renderer_host_init() != 0) {
    fprintf(stderr, "roundtrip: renderer_host_init failed\n");
    return 1;
  }
  if (seed_demo() != 0) {
    renderer_host_close();
    return 1;
  }

  // Buffer frame N's command stream. RDRAM is now in its pristine seeded state.
  static struct Playback pb;
  memset(&pb, 0, sizeof(pb));
  pb.frame_index = frame_n;
  playback_init(&pb);

  // Capture BEFORE feeding: the dump carries the seeded (pre-render) RDRAM +
  // the command stream, so replay regenerates the rendered framebuffer itself.
  const char* tmp_path = "rdp_viewer_roundtrip.rdp";
  if (capture_frame_to_rdp(tmp_path, &pb) != 0) {
    fprintf(stderr, "roundtrip: capture failed\n");
    renderer_host_close();
    return 2;
  }

  // (a) Direct render of frame N -> scanout A.
  playback_tick(&pb);
  uint32_t aw = 0;
  uint32_t ah = 0;
  uint32_t ap = 0;
  const uint32_t* apx = renderer_host_scanout(&aw, &ah, &ap);
  if (!apx || aw != RENDERER_HOST_OUT_WIDTH || ah != RENDERER_HOST_OUT_HEIGHT) {
    fprintf(stderr, "roundtrip: direct scanout invalid (%ux%u)\n", aw, ah);
    renderer_host_close();
    return 3;
  }
  // Snapshot A (the next replay re-inits the renderer, clobbering s_scanout).
  static uint32_t snap_a[RENDERER_HOST_OUT_WIDTH * RENDERER_HOST_OUT_HEIGHT];
  memcpy(snap_a, apx, sizeof(snap_a));

  // (c) Replay the dump through the source -> scanout B. source_dump_replay
  // re-inits the renderer at the dump's RDRAM size, so no manual reset needed.
  if (source_dump_replay(tmp_path) != 0) {
    fprintf(stderr, "roundtrip: replay failed\n");
    renderer_host_close();
    return 4;
  }
  uint32_t bw = 0;
  uint32_t bh = 0;
  uint32_t bp = 0;
  const uint32_t* bpx = renderer_host_scanout(&bw, &bh, &bp);
  if (!bpx || bw != RENDERER_HOST_OUT_WIDTH || bh != RENDERER_HOST_OUT_HEIGHT) {
    fprintf(stderr, "roundtrip: replay scanout invalid (%ux%u)\n", bw, bh);
    renderer_host_close();
    return 5;
  }

  // (d) Byte-identical comparison.
  uint32_t diffs = 0;
  uint32_t first_i = 0;
  for (uint32_t i = 0; i < RENDERER_HOST_OUT_WIDTH * RENDERER_HOST_OUT_HEIGHT;
       ++i) {
    if (snap_a[i] != bpx[i]) {
      if (diffs == 0) {
        first_i = i;
      }
      ++diffs;
    }
  }

  int rc = 0;
  if (diffs != 0) {
    fprintf(stderr,
            "roundtrip: FAIL frame %u — %u/%u px differ (first @%u: A=0x%08x "
            "B=0x%08x)\n",
            frame_n, diffs, aw * ah, first_i, snap_a[first_i], bpx[first_i]);
    rc = 6;
  } else {
    printf(
        "roundtrip: OK frame %u — direct vs .rdp replay byte-identical "
        "(%ux%u)\n",
        frame_n, aw, ah);
  }

  renderer_host_close();
  remove(tmp_path);
  return rc;
}
