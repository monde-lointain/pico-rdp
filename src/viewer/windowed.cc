// windowed.cc — rdp_viewer interactive entry (SDL3 + ImGui docking shell).
//
// Orthodoxy-EXEMPT (mirrors platform_sdl / the host harness): SDL3 + ImGui are
// modern-C++ APIs; local idioms match them.
//
// Live-demo driver: buffers/feeds the demo command stream through renderer_host
// into a 240x240 scanout streamed to an SDL texture, inside a full-viewport
// dockspace with the C.3/C.4 debugger panels. The Source menu captures the
// current frame to a .rdp or loads+replays one (freezing the live driver).

#include "windowed.h"

#include <SDL3/SDL.h>
#include <stdint.h>
#include <stdio.h>
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
#include "viewer_common.h"

int run_windowed(void) {
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
