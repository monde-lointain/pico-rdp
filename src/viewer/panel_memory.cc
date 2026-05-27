/* panel_memory.cc — Stream C.4: memory viewers (TMEM / color / depth /
 * coverage).
 *
 * Orthodoxy-EXEMPT (ImGui host idioms; mirrors main.cc / the host harness).
 *
 * Visualizes the renderer's live memories as SDL streaming textures shown via
 * ImGui::Image in a tabbed Memory dock node:
 *
 *   - TMEM (rdpx_get_tmem(), 4 KB): the demo loads its 64x64 arrows texture as
 *     CI4 (4-bit palette indices) into TMEM offset 0 at half width (32 bytes /
 *     row, per demo.c) and its 16-entry RGBA5551 TLUT at tmem[0x800] (the fixed
 *     base the renderer samples from, tmem.c). We decode the CI4 texels through
 *     the TLUT into a 64x64 RGBA preview (so "the arrows texture is present" is
 *     directly visible), and ALSO draw a 64x32 raw halfword heatmap of the low
 *     2 KB so the load shows even before the cube's LOAD_TILE runs.
 *
 *   - Color FB: demo_color_fb_addr() (RGBA5551, 240x240) read out of the
 *     renderer's RDRAM with the byte-swizzle undone (rdram_read16) -> RGBA8.
 *
 *   - Depth: DEMO_RDRAM_Z_BUF_ADDR (240x240, 16-bit). We reproduce the RDP
 *     z-decode arithmetically (zbuffer.c: idx=(zb>>2)&0x3fff, exp=(idx>>11)&7,
 *     man=idx&0x7ff, z18=((man<<shift[exp])+add[exp])&0x3ffff) and normalize
 * the 18-bit z to grayscale.
 *
 *   - Coverage: rdpx_get_hidden_rdram() — the per-pixel 3-bit coverage/hidden
 *     bits the renderer keeps alongside RDRAM; the low DEMO_FB pixels are shown
 *     as a heatmap (value * 32 -> gray).
 */

#include "panel_memory.h"

#include <SDL3/SDL.h>
#include <stdint.h>
#include <string.h>

#include "dock_layout.h"
#include "imgui.h"
#include "minmax.h"
#include "renderer_host.h"
#include "tmem_preview.h"

extern "C" {
#include "demo.h"
#include "rdram_io.h"
}
#include "panels.h"  // struct Playback

// RDPX_TESTING test-accessors compiled into rdp_core (not in n64video.h's
// public surface; forward-declared at the use site exactly as the conformance
// adapter / perf probe do).
extern "C" {
uint8_t* rdpx_get_tmem(void);
uint32_t rdpx_get_tmem_size(void);
uint8_t* rdpx_get_hidden_rdram(void);
uint32_t rdpx_get_hidden_rdram_size(void);
}

// ---- texture dimensions ----------------------------------------------------
#define TEX_TMEM_W DEMO_CI4_TEX_WIDTH   // 64 — decoded CI4 preview
#define TEX_TMEM_H DEMO_CI4_TEX_HEIGHT  // 64
#define TEX_RAW_W 64  // raw TMEM halfword heatmap (2 KB / 2 / 64 = 16 rows)
#define TEX_RAW_H 32
#define TEX_FB_W DEMO_FB_WIDTH   // 240
#define TEX_FB_H DEMO_FB_HEIGHT  // 240

// ---- module state ----------------------------------------------------------
static SDL_Renderer* s_renderer;
static SDL_Texture* s_tex_tmem;   // decoded CI4-through-TLUT preview (64x64)
static SDL_Texture* s_tex_raw;    // raw TMEM halfword heatmap
static SDL_Texture* s_tex_color;  // color FB RGBA5551 -> RGBA8
static SDL_Texture* s_tex_depth;  // z grayscale
static SDL_Texture* s_tex_cvg;    // coverage heatmap

// Scratch RGBA8 staging (largest is the 240x240 FB).
static uint32_t s_stage[TEX_FB_W * TEX_FB_H];

// ---- z-decode table (mirrors zbuffer.c z_dec_table) ------------------------
static const uint32_t Z_SHIFT[8] = {6, 5, 4, 3, 2, 1, 0, 0};
static const uint32_t Z_ADD[8] = {0x00000, 0x20000, 0x30000, 0x38000,
                                  0x3c000, 0x3e000, 0x3f000, 0x3f800};

static uint32_t z_decode18(uint16_t zb) {
  uint32_t const idx = ((uint32_t)zb >> 2) & 0x3fffU;
  uint32_t const exp = (idx >> 11) & 7U;
  uint32_t const man = idx & 0x7ffU;
  return ((man << Z_SHIFT[exp]) + Z_ADD[exp]) & 0x3ffffU;
}

// ---- RGBA5551 -> RGBA8888 (byte order R,G,B,A = SDL_PIXELFORMAT_RGBA32) -----
static uint32_t rgba5551_to_rgba8(uint16_t p) {
  uint32_t r = (p >> 11) & 0x1f;
  uint32_t g = (p >> 6) & 0x1f;
  uint32_t b = (p >> 1) & 0x1f;
  uint32_t const a =
      0xff;  // force opaque for display (RGBA5551 alpha bit ignored)
  r = (r << 3) | (r >> 2);
  g = (g << 3) | (g >> 2);
  b = (b << 3) | (b >> 2);
  return r | (g << 8) | (b << 16) | (a << 24);
}

static uint32_t gray_rgba8(uint8_t v) {
  return (uint32_t)v | ((uint32_t)v << 8) | ((uint32_t)v << 16) | 0xff000000U;
}

// ---- lifecycle -------------------------------------------------------------
static SDL_Texture* make_tex(int w, int h) {
  SDL_Texture* t = SDL_CreateTexture(s_renderer, SDL_PIXELFORMAT_RGBA32,
                                     SDL_TEXTUREACCESS_STREAMING, w, h);
  if (t) {
    SDL_SetTextureScaleMode(t, SDL_SCALEMODE_NEAREST);
  }
  return t;
}

void panel_memory_init(struct SDL_Renderer* renderer) {
  s_renderer = renderer;
  if (!s_renderer) {
    return;
  }
  s_tex_tmem = make_tex(TEX_TMEM_W, TEX_TMEM_H);
  s_tex_raw = make_tex(TEX_RAW_W, TEX_RAW_H);
  s_tex_color = make_tex(TEX_FB_W, TEX_FB_H);
  s_tex_depth = make_tex(TEX_FB_W, TEX_FB_H);
  s_tex_cvg = make_tex(TEX_FB_W, TEX_FB_H);
}

void panel_memory_shutdown(void) {
  if (s_tex_tmem) {
    SDL_DestroyTexture(s_tex_tmem);
  }
  if (s_tex_raw) {
    SDL_DestroyTexture(s_tex_raw);
  }
  if (s_tex_color) {
    SDL_DestroyTexture(s_tex_color);
  }
  if (s_tex_depth) {
    SDL_DestroyTexture(s_tex_depth);
  }
  if (s_tex_cvg) {
    SDL_DestroyTexture(s_tex_cvg);
  }
  s_tex_tmem = NULL;
  s_tex_raw = NULL;
  s_tex_color = NULL;
  s_tex_depth = NULL;
  s_tex_cvg = NULL;
  s_renderer = NULL;
}

// ---- texture refresh -------------------------------------------------------
// Decode the 64x64 CI4 texels (TMEM offset 0) through the TLUT at tmem[0x800]
// into s_stage as RGBA8, then upload. The decode mirrors the renderer's
// sample-tile addressing (stride-4 replicated TLUT + the t-parity byte XOR
// swizzle) and lives in tmem_preview.cc so it stays in lockstep with tmem.cc
// and is unit-tested headlessly (tests/demo/tmem_preview_test.cc).
static void refresh_tmem(void) {
  const uint8_t* tmem = rdpx_get_tmem();
  if (!tmem || !s_tex_tmem) {
    return;
  }
  tmem_decode_ci4_preview(tmem, s_stage);
  SDL_UpdateTexture(s_tex_tmem, NULL, s_stage,
                    TEX_TMEM_W * (int)sizeof(uint32_t));

  // Raw heatmap of the low 2 KB as halfwords (1024 halfwords -> 64x16 used).
  if (s_tex_raw) {
    for (int i = 0; i < TEX_RAW_W * TEX_RAW_H; ++i) {
      uint32_t const off = (uint32_t)i * 2U;
      uint16_t const hw = (uint16_t)((tmem[off] << 8) | tmem[off + 1]);
      // map the halfword to a blue->yellow heat by its magnitude.
      uint8_t const v = (uint8_t)(hw >> 8);
      s_stage[i] = (uint32_t)v | ((uint32_t)v << 8) |
                   ((uint32_t)(255 - v) << 16) | 0xff000000U;
    }
    SDL_UpdateTexture(s_tex_raw, NULL, s_stage,
                      TEX_RAW_W * (int)sizeof(uint32_t));
  }
}

static void refresh_color(void) {
  if (!s_tex_color) {
    return;
  }
  uint32_t rsz = 0;
  const uint8_t* rdram = renderer_host_rdram(&rsz);
  if (!rdram) {
    return;
  }
  const uint32_t base = demo_color_fb_addr();
  for (int y = 0; y < TEX_FB_H; ++y) {
    for (int x = 0; x < TEX_FB_W; ++x) {
      uint32_t const off = base + ((((uint32_t)y * TEX_FB_W) + x) * 2U);
      uint16_t const p = (off + 1U < rsz) ? rdram_read16(rdram, off) : 0U;
      s_stage[((size_t)y * TEX_FB_W) + x] = rgba5551_to_rgba8(p);
    }
  }
  SDL_UpdateTexture(s_tex_color, NULL, s_stage,
                    TEX_FB_W * (int)sizeof(uint32_t));
}

static void refresh_depth(void) {
  if (!s_tex_depth) {
    return;
  }
  uint32_t rsz = 0;
  const uint8_t* rdram = renderer_host_rdram(&rsz);
  if (!rdram) {
    return;
  }
  for (int y = 0; y < TEX_FB_H; ++y) {
    for (int x = 0; x < TEX_FB_W; ++x) {
      uint32_t const off =
          DEMO_RDRAM_Z_BUF_ADDR + ((((uint32_t)y * TEX_FB_W) + x) * 2U);
      uint16_t const zb = (off + 1U < rsz) ? rdram_read16(rdram, off) : 0U;
      uint32_t const z18 = z_decode18(zb);
      uint8_t const v = (uint8_t)((z18 * 255U) / 0x3ffffU);
      s_stage[((size_t)y * TEX_FB_W) + x] = gray_rgba8(v);
    }
  }
  SDL_UpdateTexture(s_tex_depth, NULL, s_stage,
                    TEX_FB_W * (int)sizeof(uint32_t));
}

static void refresh_coverage(void) {
  if (!s_tex_cvg) {
    return;
  }
  const uint8_t* hid = rdpx_get_hidden_rdram();
  uint32_t const hsz = rdpx_get_hidden_rdram_size();
  if (!hid) {
    return;
  }
  // Hidden RDRAM holds the per-RDRAM-halfword hidden/coverage bits, indexed by
  // the same halfword offset as the color FB. Each entry is the low 3 cvg bits
  // (0..7); scale to 0..255. Index by the color-FB halfword offset.
  const uint32_t base_hw = demo_color_fb_addr() >> 1;  // halfword index
  for (int y = 0; y < TEX_FB_H; ++y) {
    for (int x = 0; x < TEX_FB_W; ++x) {
      uint32_t const hwi = base_hw + ((uint32_t)y * TEX_FB_W) + x;
      uint8_t const c = (hwi < hsz) ? (uint8_t)(hid[hwi] & 0x07U) : 0U;
      uint8_t const v = (uint8_t)(c * 36U);  // 7*36 ~= 252
      s_stage[((size_t)y * TEX_FB_W) + x] = gray_rgba8(v);
    }
  }
  SDL_UpdateTexture(s_tex_cvg, NULL, s_stage, TEX_FB_W * (int)sizeof(uint32_t));
}

// ---- panel -----------------------------------------------------------------
static void image_fit(SDL_Texture* tex, int native_w, int native_h) {
  if (!tex) {
    ImGui::TextUnformatted("(texture unavailable)");
    return;
  }
  ImVec2 const avail = ImGui::GetContentRegionAvail();
  float scale = avail.x / (float)native_w;
  float const maxh = avail.y > 1.0F ? avail.y : (float)native_h;
  if (scale * (float)native_h > maxh) {
    scale = maxh / (float)native_h;
  }
  scale = rdpx_max(scale, 1.0F);
  ImGui::Image((ImTextureID)(intptr_t)tex,
               ImVec2((float)native_w * scale, (float)native_h * scale));
}

void panel_memory_draw(struct Playback* pb) {
  (void)pb;
  if (!ImGui::Begin(VIEWER_DOCK_MEMORY)) {
    ImGui::End();
    return;
  }

  if (!s_renderer) {
    ImGui::TextUnformatted("Memory viewers need an SDL renderer (headless).");
    ImGui::End();
    return;
  }

  refresh_tmem();
  refresh_color();
  refresh_depth();
  refresh_coverage();

  if (ImGui::BeginTabBar("##memtabs")) {
    if (ImGui::BeginTabItem("TMEM")) {
      ImGui::Text("CI4 64x64 decoded via TLUT @tmem[0x800] (%u B total)",
                  rdpx_get_tmem_size());
      image_fit(s_tex_tmem, TEX_TMEM_W, TEX_TMEM_H);
      ImGui::Separator();
      ImGui::TextUnformatted("Raw low-2KB halfword heatmap:");
      image_fit(s_tex_raw, TEX_RAW_W, TEX_RAW_H);
      ImGui::EndTabItem();
    }
    if (ImGui::BeginTabItem("Color")) {
      ImGui::Text("color FB @0x%06x  RGBA5551 %dx%d", demo_color_fb_addr(),
                  TEX_FB_W, TEX_FB_H);
      image_fit(s_tex_color, TEX_FB_W, TEX_FB_H);
      ImGui::EndTabItem();
    }
    if (ImGui::BeginTabItem("Depth")) {
      ImGui::Text("Z buffer @0x%06x  16-bit (z-decoded grayscale)",
                  DEMO_RDRAM_Z_BUF_ADDR);
      image_fit(s_tex_depth, TEX_FB_W, TEX_FB_H);
      ImGui::EndTabItem();
    }
    if (ImGui::BeginTabItem("Coverage")) {
      ImGui::Text("hidden RDRAM (cvg 0..7 -> gray), %u B",
                  rdpx_get_hidden_rdram_size());
      image_fit(s_tex_cvg, TEX_FB_W, TEX_FB_H);
      ImGui::EndTabItem();
    }
    ImGui::EndTabBar();
  }

  ImGui::End();
}
