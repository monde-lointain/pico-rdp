/* main.cc — rdp_viewer host entry (SDL3 + ImGui docking shell).
 *
 * Orthodoxy-EXEMPT (mirrors platform_sdl / the host harness): SDL3 + ImGui are
 * modern-C++ APIs; local idioms match them.
 *
 * Milestone V1: renderer lifecycle + scanout -> SDL streaming texture + docking
 * shell (Layout A seeded via DockBuilder) + a hardcoded cleared frame. No
 * demo_core / panels yet (other streams own those).
 *
 * Headless: `--headless-dump <path.ppm>` renders ONE hardcoded-clear frame with
 * no window (SDL_VIDEODRIVER=dummy friendly), writes the scanout as a binary
 * PPM, asserts it is 240x240 and uniformly the fill color, then exits.
 */

#include <SDL3/SDL.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "capture.h"
#include "dock_layout.h"
#include "imgui.h"
#include "imgui_impl_sdl3.h"
#include "imgui_impl_sdlrenderer3.h"
#include "panel_memory.h"
#include "panel_perf.h"
#include "panels.h"
#include "renderer_host.h"
#include "source_dump.h"

extern "C" {
#include "cmd_decode.h"
#include "demo.h"
}

// RDPX_TESTING pixel-counter accessors (compiled into rdp_core;
// forward-declared at the use site as the conformance adapter / perf probe do —
// not in the public n64video.h surface).
extern "C" {
uint64_t rdpx_get_pixel_count(void);
void rdpx_reset_pixel_count(void);
uint8_t* rdpx_get_tmem(void);
uint32_t rdpx_get_tmem_size(void);
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
static int headless_dump(const char* path) {
  if (renderer_host_init() != 0) {
    fprintf(stderr, "headless: renderer_host_init failed\n");
    return 1;
  }

  renderer_host_submit_clear((uint16_t)CLEAR_FILL_5551);
  renderer_host_present();

  uint32_t w = 0, h = 0, pitch = 0;
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
    const uint32_t rgb = px[i] & 0x00ffffffu;  // ignore alpha
    if (rgb != (first & 0x00ffffffu)) {
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

// Seed the demo arena into the renderer's RDRAM. Returns 0 on success.
static int seed_demo(void) {
  uint32_t size = 0;
  uint8_t* rdram = renderer_host_rdram(&size);
  if (!rdram || size < DEMO_RDRAM_USED_END) {
    fprintf(stderr, "seed_demo: RDRAM unavailable / too small (%u < %u)\n",
            size, (unsigned)DEMO_RDRAM_USED_END);
    return 1;
  }
  demo_init(rdram, size);
  return 0;
}

// ---- headless demo-frame dump ----------------------------------------------
// Drives demo frame N through the full viewer path (buffer the frame's command
// stream, feed it to the renderer, present via the demo blit) and writes the
// scanout as a binary PPM. Asserts 240x240 and NON-UNIFORM (the rendered scene
// has many distinct colors). Returns 0 on success.
static int headless_demo_frame(uint32_t frame_n, const char* path) {
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

  uint32_t w = 0, h = 0, pitch = 0;
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
    const uint32_t rgb = px[i] & 0x00ffffffu;
    uint32_t j = 0;
    for (; j < nuniq; ++j)
      if (uniq[j] == rgb) break;
    if (j == nuniq) uniq[nuniq++] = rgb;
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
static int headless_perf(uint32_t frame_n) {
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
  uint64_t t0 = SDL_GetPerformanceCounter();
  playback_tick(&pb);  // feeds the whole frame + present_demo
  uint64_t t1 = SDL_GetPerformanceCounter();
  double ms =
      (double)(t1 - t0) / (double)SDL_GetPerformanceFrequency() * 1000.0;
  uint64_t px = rdpx_get_pixel_count();

  // Per-command-type histogram (C.2 decoder over the buffered stream).
  uint32_t by_id[64];
  for (uint32_t i = 0; i < 64; ++i) by_id[i] = 0u;
  uint32_t tri = 0u;
  for (uint32_t i = 0; i < pb.cmd_total; ++i) {
    const struct PlaybackCmd* c = &pb.cmds[i];
    struct CmdRecord rec;
    cmd_decode(&pb.words[c->word_off], c->word_count, &rec);
    by_id[rec.id & 0x3f] += 1u;
    if (rec.is_triangle) tri += 1u;
  }

  // TMEM non-zero check over the loaded CI4 region (low 2 KB).
  const uint8_t* tmem = rdpx_get_tmem();
  uint32_t tmem_sz = rdpx_get_tmem_size();
  uint32_t nz = 0u;
  uint32_t scan = tmem_sz < 2048u ? tmem_sz : 2048u;
  if (tmem) {
    for (uint32_t i = 0; i < scan; ++i)
      if (tmem[i] != 0) ++nz;
  }

  // Extrapolation model (perf-extrapolation.md): 250..700 cyc/px @250 MHz.
  double opt = perf_extrapolate_m0_ms(px, 250.0, 250000000.0);
  double pes = perf_extrapolate_m0_ms(px, 700.0, 250000000.0);

  printf("headless-perf: frame %u\n", frame_n);
  printf("  commands=%u triangles=%u\n", pb.cmd_total, tri);
  printf("  pixels=%llu  host_build_present=%.3f ms", (unsigned long long)px,
         ms);
  if (ms > 0.0)
    printf("  (%.2f Mpix/s)\n", ((double)px / 1.0e6) / (ms / 1000.0));
  else
    printf("\n");
  printf(
      "  extrapolated M0+ @250MHz: %.1f .. %.1f ms/frame (~%.0f..%.0f fps)\n",
      opt, pes, pes > 0 ? 1000.0 / pes : 0.0, opt > 0 ? 1000.0 / opt : 0.0);
  printf("  TMEM non-zero bytes in CI4 region: %u / %u\n", nz, scan);
  printf("  command types:");
  for (uint32_t id = 0; id < 64; ++id)
    if (by_id[id]) printf(" %s=%u", rdp_cmd_name((uint8_t)id), by_id[id]);
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
static int headless_capture_roundtrip(uint32_t frame_n) {
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
  uint32_t aw = 0, ah = 0, ap = 0;
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
  uint32_t bw = 0, bh = 0, bp = 0;
  const uint32_t* bpx = renderer_host_scanout(&bw, &bh, &bp);
  if (!bpx || bw != RENDERER_HOST_OUT_WIDTH || bh != RENDERER_HOST_OUT_HEIGHT) {
    fprintf(stderr, "roundtrip: replay scanout invalid (%ux%u)\n", bw, bh);
    renderer_host_close();
    return 5;
  }

  // (d) Byte-identical comparison.
  uint32_t diffs = 0, first_i = 0;
  for (uint32_t i = 0; i < RENDERER_HOST_OUT_WIDTH * RENDERER_HOST_OUT_HEIGHT;
       ++i) {
    if (snap_a[i] != bpx[i]) {
      if (diffs == 0) first_i = i;
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

// ---- interactive (windowed) ------------------------------------------------
static int run_windowed(void) {
  if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS)) {
    fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
    return 1;
  }

  SDL_Window* window =
      SDL_CreateWindow("RDP Viewer", 1280, 800, SDL_WINDOW_RESIZABLE);
  SDL_Renderer* renderer = SDL_CreateRenderer(window, nullptr);
  if (!window || !renderer) {
    fprintf(stderr, "SDL window/renderer failed: %s\n", SDL_GetError());
    return 1;
  }

  // Streaming texture for the 240x240 scanout (RGBA32 matches struct rgba
  // bytes).
  SDL_Texture* scanout_tex = SDL_CreateTexture(
      renderer, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STREAMING,
      RENDERER_HOST_OUT_WIDTH, RENDERER_HOST_OUT_HEIGHT);
  SDL_SetTextureScaleMode(scanout_tex, SDL_SCALEMODE_NEAREST);

  ImGui::CreateContext();
  ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_DockingEnable;  // docking ONLY
  ImGui::GetIO().IniFilename = nullptr;  // no persisted layout
  ImGui_ImplSDL3_InitForSDLRenderer(window, renderer);
  ImGui_ImplSDLRenderer3_Init(renderer);

  if (renderer_host_init() != 0) {
    fprintf(stderr, "renderer_host_init failed\n");
    return 1;
  }
  if (seed_demo() != 0) return 1;

  // C.4 memory viewers own SDL textures against this renderer.
  panel_memory_init(renderer);

  // Live-demo driver: buffer frame 0 and present it (paused).
  static struct Playback pb;
  memset(&pb, 0, sizeof(pb));
  pb.frame_index = 0;
  pb.playing = true;
  playback_init(&pb);

  // C.4 perf HUD: sampled around each frame's build+present.
  struct PerfSample perf;
  memset(&perf, 0, sizeof(perf));

  // C.5 source switch: when a .rdp dump is loaded, freeze the live-demo driver
  // (the dump owns the renderer + scanout) until the user switches back.
  bool source_is_dump = false;
  static char dump_path[512] = "rdp_viewer_capture.rdp";

  bool first_layout = true;
  bool running = true;
  while (running) {
    SDL_Event ev;
    while (SDL_PollEvent(&ev)) {
      ImGui_ImplSDL3_ProcessEvent(&ev);
      if (ev.type == SDL_EVENT_QUIT) running = false;
    }

    // Advance the demo / feed buffered commands, then refresh the scanout.
    // Sample perf only when the tick actually rebuilds+presents a frame (dirty
    // or playing); otherwise keep the last sample so the HUD doesn't read 0.
    // While a .rdp dump is the active source, the live-demo driver is frozen —
    // the dump's last replay owns the renderer scanout.
    if (!source_is_dump) {
      const bool will_present = pb.dirty || pb.playing;
      if (will_present) rdpx_reset_pixel_count();
      uint64_t t0 = SDL_GetPerformanceCounter();
      playback_tick(&pb);
      if (will_present) {
        uint64_t t1 = SDL_GetPerformanceCounter();
        double freq = (double)SDL_GetPerformanceFrequency();
        perf.build_present_ms = (double)(t1 - t0) / freq * 1000.0;
        perf.pixel_count = rdpx_get_pixel_count();
      }
    }

    // Upload the latest scanout into the SDL texture.
    {
      uint32_t w = 0, h = 0, pitch = 0;
      const uint32_t* px = renderer_host_scanout(&w, &h, &pitch);
      if (px && w == RENDERER_HOST_OUT_WIDTH && h == RENDERER_HOST_OUT_HEIGHT)
        SDL_UpdateTexture(scanout_tex, nullptr, px,
                          (int)(pitch * sizeof(uint32_t)));
    }

    ImGui_ImplSDLRenderer3_NewFrame();
    ImGui_ImplSDL3_NewFrame();
    ImGui::NewFrame();

    // Full-viewport dockspace host window.
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(vp->WorkPos);
    ImGui::SetNextWindowSize(vp->WorkSize);
    ImGui::SetNextWindowViewport(vp->ID);
    ImGuiWindowFlags host_flags =
        ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse |
        ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus |
        ImGuiWindowFlags_MenuBar;
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::Begin("##DockHost", nullptr, host_flags);
    ImGui::PopStyleVar(3);

    ImGuiID dockspace_id = ImGui::GetID("ViewerDockspace");
    ImGui::DockSpace(dockspace_id, ImVec2(0, 0), ImGuiDockNodeFlags_None);
    if (first_layout) {
      viewer_build_default_dock_layout(dockspace_id);
      first_layout = false;
    }

    if (ImGui::BeginMenuBar()) {
      if (ImGui::BeginMenu("Renderer")) {
        const bool crashed = renderer_host_crash_latched() != 0;
        ImGui::MenuItem(crashed ? "Pipeline: CRASHED" : "Pipeline: OK", nullptr,
                        false, false);
        if (ImGui::MenuItem("Reset (close -> init)")) {
          renderer_host_reset();
          seed_demo();
          pb.frame_index = 0;
          pb.playing = false;
          source_is_dump = false;
          playback_init(&pb);
        }
        ImGui::EndMenu();
      }
      // C.5 Source: capture the current demo frame to a .rdp, or load+replay a
      // .rdp through the dump source (switching the active source).
      if (ImGui::BeginMenu("Source")) {
        ImGui::MenuItem(
            source_is_dump ? "Active: .rdp dump" : "Active: live demo", nullptr,
            false, false);
        ImGui::Separator();
        ImGui::InputText("path", dump_path, sizeof(dump_path));

        if (ImGui::MenuItem("Capture current frame -> .rdp", nullptr, false,
                            !source_is_dump)) {
          // Re-buffer the current frame (pristine seeded RDRAM), capture, then
          // re-feed so the live view is unchanged.
          renderer_host_reset();
          seed_demo();
          playback_init(&pb);
          capture_frame_to_rdp(dump_path, &pb);
          playback_tick(&pb);
        }
        if (ImGui::MenuItem("Load + replay .rdp")) {
          if (source_dump_replay(dump_path) == 0) {
            source_is_dump = true;
            pb.playing = false;
          } else {
            // Replay re-inits the renderer; restore the demo to a usable state.
            renderer_host_reset();
            seed_demo();
            source_is_dump = false;
            playback_init(&pb);
          }
        }
        if (ImGui::MenuItem("Switch back to live demo", nullptr, false,
                            source_is_dump)) {
          renderer_host_reset();
          seed_demo();
          source_is_dump = false;
          pb.frame_index = 0;
          pb.playing = false;
          playback_init(&pb);
        }
        ImGui::EndMenu();
      }
      ImGui::EndMenuBar();
    }
    ImGui::End();

    // Scanout panel (docked into the central node).
    if (ImGui::Begin(VIEWER_DOCK_SCANOUT)) {
      ImVec2 avail = ImGui::GetContentRegionAvail();
      float side =
          avail.x < avail.y ? avail.x : avail.y;  // keep the 1:1 square
      if (side < 1.0f) side = 1.0f;
      ImGui::Image((ImTextureID)(intptr_t)scanout_tex, ImVec2(side, side));
    }
    ImGui::End();

    // C.3 debugger panels (docked into the reserved nodes).
    playback_panel(&pb);    // play/pause/step/reset (bottom, tabbed)
    panel_log_draw(&pb);    // decoded command log (bottom, tabbed)
    panel_state_draw(&pb);  // shadow-state inspector (right column)

    // C.4 panels: memory viewers (right-bottom) + perf HUD (bottom, tabbed).
    panel_memory_draw(&pb);
    panel_perf_draw(&pb, &perf);

    ImGui::Render();
    SDL_SetRenderDrawColor(renderer, 30, 30, 30, 255);
    SDL_RenderClear(renderer);
    ImGui_ImplSDLRenderer3_RenderDrawData(ImGui::GetDrawData(), renderer);
    SDL_RenderPresent(renderer);
  }

  panel_memory_shutdown();
  renderer_host_close();
  ImGui_ImplSDLRenderer3_Shutdown();
  ImGui_ImplSDL3_Shutdown();
  ImGui::DestroyContext();
  SDL_DestroyTexture(scanout_tex);
  SDL_DestroyRenderer(renderer);
  SDL_DestroyWindow(window);
  SDL_Quit();
  return 0;
}

int main(int argc, char** argv) {
  for (int i = 1; i < argc; ++i) {
    if (strcmp(argv[i], "--headless-dump") == 0 && i + 1 < argc) {
      return headless_dump(argv[i + 1]);
    }
    if (strcmp(argv[i], "--headless-demo-frame") == 0 && i + 2 < argc) {
      uint32_t n = (uint32_t)strtoul(argv[i + 1], nullptr, 10);
      return headless_demo_frame(n, argv[i + 2]);
    }
    if (strcmp(argv[i], "--headless-perf") == 0 && i + 1 < argc) {
      uint32_t n = (uint32_t)strtoul(argv[i + 1], nullptr, 10);
      return headless_perf(n);
    }
    if (strcmp(argv[i], "--headless-capture-roundtrip") == 0 && i + 1 < argc) {
      uint32_t n = (uint32_t)strtoul(argv[i + 1], nullptr, 10);
      return headless_capture_roundtrip(n);
    }
  }
  return run_windowed();
}
