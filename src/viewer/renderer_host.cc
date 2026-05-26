/* renderer_host.cc — rdp_core renderer lifecycle + scanout capture for the
 * viewer.
 *
 * Orthodoxy-EXEMPT (mirrors the SDL/ImGui host harness): consumes the rdpx_* C
 * ABI from src/rdp/n64video.h and the demo RDRAM/VI contract from
 * src/demo/demo.h.
 *
 * Config setup mirrors tests/conformance/replayer_driver_ours.cpp: caller-owned
 * RDRAM, VI/DP register pointer arrays, a no-op mi_intr, VI_MODE_NORMAL /
 * VI_INTERP_LINEAR / DP_COMPAT_HIGH. The scanout callback copies the RGBA8888
 * prescale (struct Rgba = bytes r,g,b,a -> SDL_PIXELFORMAT_RGBA32) into a host
 * buffer, forcing alpha to 0xff for opaque display.
 */

#include "renderer_host.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

// DEMO_* RDRAM/VI contract (compile-time macros only). We deliberately use the
// macro constants, NOT the demo_core runtime accessors (demo_init /
// demo_color_fb_addr): demo_core has no bodies yet (V1 has no demo source), and
// the hardcoded clear only needs the color-FB address + dimensions, which are
// fixed macros. The future demo source will call demo_init through the seam.
#include "demo.h"
#include "rdram_io.h"  // XOR-correct halfword I/O for the demo->staging blit

extern "C" {
#include "n64video.h"

// Harness hooks NOT in the public header (test-only / adapter entry points),
// but compiled into rdp_core. Declared here exactly as replayer_driver_ours.cpp
// does.
void rdpx_rdp_cmd(uint32_t wid, const uint32_t* args);
void rdpx_vdac_set_scanout_cb(void (*cb)(const void*, uint32_t, uint32_t,
                                         uint32_t));
}

// ---- RDP command IDs (src/rdp/n64video.c CMD_ID_*) -------------------------
#define CMD_SYNC_FULL 0x29u
#define CMD_SET_SCISSOR 0x2du
#define CMD_SET_OTHER_MODES 0x2fu
#define CMD_FILL_RECTANGLE 0x36u
#define CMD_SET_FILL_COLOR 0x37u
#define CMD_SET_COLOR_IMAGE 0x3fu

// SET_COLOR_IMAGE format/size fields. RGBA5551 -> format RGBA(0), size
// 16bpp(2).
#define IMG_FMT_RGBA 0u
#define IMG_SIZE_16B 2u

// SET_OTHER_MODES cycle_type field (word0 bits [21:20]); 3 = FILL.
#define CYCLE_TYPE_FILL 3u

// Color-FB RDRAM byte address for V1's hardcoded clear. The demo contract puts
// the color FB at offset 0 (DEMO_RDRAM_COLOR_FB_ADDR), but the VI treats
// VI_ORIGIN==0 as "no valid frame" (vi.c: `if (!frame_buffer)
// vdac_sync(true)`), so V1 places its standalone clear FB at a non-zero offset
// past the demo segments. SET_COLOR_IMAGE and VI_ORIGIN both use this address.
// The future demo source uses demo_color_fb_addr() through the seam instead.
#define CLEAR_FB_ADDR (DEMO_RDRAM_USED_END + 0x1000u)

// V1 clear source surface. The VI scans/filters a region slightly larger than
// the 240x240 visible window (the AA/resample crop reads ~8px borders + the
// scaled scan reads hres=255 columns). To keep the OUTPUT uniform we fill a
// CLEAR_FB_DIM-square source that fully contains every sampled texel, so all
// 240x240 output pixels come from inside the fill. 256 >= 255 (hres) and
// >= 240 (vres) with margin for filter neighbor fetches.
#define CLEAR_FB_DIM 256u

// ---- Module state ----------------------------------------------------------
static uint8_t* s_rdram;
static uint32_t s_vi_regs[VI_NUM_REG];
static uint32_t s_dp_regs[DP_NUM_REG];
static uint32_t s_mi_intr;
static uint32_t* s_p_vi_regs[VI_NUM_REG];
static uint32_t* s_p_dp_regs[DP_NUM_REG];

static struct CmdSink s_sink;
static int s_initialized;
static int s_crash_latched;
static uint32_t s_rdram_size;  // bytes actually allocated for s_rdram

// Captured scanout (RGBA8888). Sized for the full PRESCALE area worst case; we
// only ever expect 240x240 from our VI programming, but the VI may report a
// larger frame, so cap defensively.
#define SCANOUT_MAX_W 640
#define SCANOUT_MAX_H 625
static uint32_t s_scanout[SCANOUT_MAX_W * SCANOUT_MAX_H];
static uint32_t s_scanout_w;
static uint32_t s_scanout_h;
static int s_scanout_valid;

// ---- mi_intr ---------------------------------------------------------------
static void mi_intr_noop(void) {}

// ---- scanout callback ------------------------------------------------------
// data is RGBA8888 (struct Rgba = bytes r,g,b,a). Copy row by row honoring the
// source pitch (in pixels) and force alpha to 0xff so SDL renders it opaque.
static void renderer_host_scanout_cb(const void* data, uint32_t width,
                                     uint32_t height, uint32_t pitch) {
  if (!data || width == 0 || height == 0) {
    // Invalid frame (vdac_sync(true)) — mark the scanout stale; the host
    // decides whether to show the last-good frame or blank.
    s_scanout_valid = 0;
    return;
  }
  if (width > SCANOUT_MAX_W) {
    width = SCANOUT_MAX_W;
  }
  if (height > SCANOUT_MAX_H) {
    height = SCANOUT_MAX_H;
  }

  const uint32_t* src = (const uint32_t*)data;  // pitch is in pixels
  for (uint32_t y = 0; y < height; ++y) {
    const uint32_t* srow = src + (size_t)y * pitch;
    uint32_t* drow = s_scanout + (size_t)y * width;
    for (uint32_t x = 0; x < width; ++x) {
      drow[x] =
          srow[x] | 0xff000000U;  // force opaque alpha (top byte = A in RGBA32)
    }
  }
  s_scanout_w = width;
  s_scanout_h = height;
  s_scanout_valid = 1;
}

// ---- command-submission seam -----------------------------------------------
static void renderer_host_emit(void* ctx, const uint32_t* words, uint32_t n) {
  (void)ctx;
  (void)n;  // each emit is one complete RDP command; rdpx_rdp_cmd reads only
            // the words its opcode needs.
  rdpx_rdp_cmd(0, words);
}

// ---- VI programming --------------------------------------------------------
// Clean 240x240 via VI_MODE_NORMAL + hide_overscan=true. In vi_process_full the
// cropped output is fb.width = maxhpass - minhpass, fb.height = vres. With:
//   minhpass = 8  (h_start NOT clamped: post-offset h_start >= 0)
//   maxhpass = hres - 7
// => fb.width = hres - 15. Choose hres = 255 -> width = 240. vres = 240.
//
// The scaled scan reads FB columns [0, hres) at x_add=1024 (1:1) and rows
// [0, vres); the AA/resample filter may fetch a +/-1 neighbor. With a uniform
// CLEAR_FB_DIM(=256)-square fill — wider/taller than the sampled region — every
// one of the 240x240 OUTPUT pixels (and every neighbor the filter touches)
// lands inside the fill, so the dumped frame is uniformly the fill color.
// VI_WIDTH (the FB row stride) is therefore 256, not 240.
//
// Register derivation (NTSC: H offset 108, V offset 34, V_SYNC 525):
//   VI_H_START: start = 108 (h_start raw; post-offset 0, not clamped),
//               end   = 108 + 255 = 363  -> hres = 255  (0x006C016B)
//   VI_V_START: start = 34  (v_start raw; (34-34)/2 = 0),
//               end   = 34 + 480 = 514   -> vres = (514-34)>>1 = 240
//               (0x00220202)
//   VI_X_SCALE = scale 0x400 (1024 = 1:1), bias 0
//   VI_Y_SCALE = scale 0x400 (1024 = 1:1), bias 0
//   VI_WIDTH   = 256 (color-FB row stride in pixels)
//   VI_V_SYNC  = 525 (NTSC; selects vstartoffset=34, ispal=false)
//   VI_STATUS  = RGBA5551(2) | AA_REPLICATE(3<<8) = 0x302
//   VI_ORIGIN  = CLEAR_FB_ADDR
static void renderer_host_program_vi(void) {
  memset(s_vi_regs, 0, sizeof(s_vi_regs));

  s_vi_regs[VI_STATUS] = (2U) | (3U << 8);  // RGBA5551 | AA replicate (0x302)
  s_vi_regs[VI_ORIGIN] =
      CLEAR_FB_ADDR;  // color FB byte addr (non-zero; see macro)
  s_vi_regs[VI_WIDTH] = CLEAR_FB_DIM;  // 256 (FB row stride in pixels)
  s_vi_regs[VI_V_SYNC] = 525U;         // NTSC
  s_vi_regs[VI_H_START] =
      ((108U & 0x3ff) << 16) | ((108U + 255U) & 0x3ff);  // 0x006C016B
  s_vi_regs[VI_V_START] =
      ((34U & 0x3ff) << 16) | ((34U + 480U) & 0x3ff);              // 0x00220202
  s_vi_regs[VI_X_SCALE] = ((0U & 0xfff) << 16) | (1024U & 0xfff);  // 0x00000400
  s_vi_regs[VI_Y_SCALE] = ((0U & 0xfff) << 16) | (1024U & 0xfff);  // 0x00000400
}

// ---- lifecycle -------------------------------------------------------------
static int renderer_host_init_sized(uint32_t rdram_size) {
  if (s_initialized) {
    return 0;
  }
  if (rdram_size == 0) {
    rdram_size = RENDERER_HOST_RDRAM_SIZE;
  }

  s_rdram = (uint8_t*)calloc(1, rdram_size);
  if (!s_rdram) {
    return 1;
  }
  s_rdram_size = rdram_size;

  // V1 leaves RDRAM zeroed: the hardcoded clear's FILL_RECTANGLE writes the
  // color FB itself, and no command references the demo asset segments. The
  // future demo source will demo_init() the arena (via the submission seam)
  // once demo_core ships its bodies.
  for (uint32_t i = 0; i < VI_NUM_REG; ++i) {
    s_p_vi_regs[i] = &s_vi_regs[i];
  }
  for (uint32_t i = 0; i < DP_NUM_REG; ++i) {
    s_p_dp_regs[i] = &s_dp_regs[i];
  }

  renderer_host_program_vi();

  struct N64videoConfig config;
  memset(&config, 0, sizeof(config));
  config.gfx.rdram = s_rdram;
  config.gfx.rdram_size = s_rdram_size;
  config.gfx.vi_reg = s_p_vi_regs;
  config.gfx.dp_reg = s_p_dp_regs;
  config.gfx.mi_intr_reg = &s_mi_intr;
  config.gfx.mi_intr_cb = mi_intr_noop;
  // VI_MODE_COLOR (unfiltered direct read), NOT VI_MODE_NORMAL: the live demo
  // scene is non-uniform, and NORMAL's AA/resample filter softens edges and can
  // bleed neighbor texels across the 240-wide window. COLOR reads the FB pixels
  // straight through for a clean 1:1 scanout. The crop geometry (hres=255 ->
  // 240 wide, +8 column offset) is identical in both modes, so the existing
  // 256-wide staging-FB programming and the +8 demo blit still land exactly.
  config.vi.mode = VI_MODE_COLOR;
  config.vi.interp = VI_INTERP_LINEAR;
  config.vi.hide_overscan = true;  // crop to the active 240x240 window
  config.dp.compat = DP_COMPAT_HIGH;
  rdpx_video_init(&config);

  rdpx_vdac_set_scanout_cb(renderer_host_scanout_cb);

  s_sink.emit = renderer_host_emit;
  s_sink.ctx = NULL;

  s_scanout_valid = 0;
  s_scanout_w = 0;
  s_scanout_h = 0;
  s_crash_latched = 0;
  s_initialized = 1;
  return 0;
}

int renderer_host_init(void) { return renderer_host_init_sized(0); }

void renderer_host_close(void) {
  if (!s_initialized) {
    return;
  }
  rdpx_video_close();  // also resets rdp_core's internal crash latch
  rdpx_vdac_set_scanout_cb(NULL);
  free(s_rdram);
  s_rdram = NULL;
  s_rdram_size = 0;
  s_initialized = 0;
  s_crash_latched = 0;
  s_scanout_valid = 0;
}

int renderer_host_reset(void) {
  renderer_host_close();
  return renderer_host_init();
}

int renderer_host_reset_with_size(uint32_t rdram_size) {
  renderer_host_close();
  return renderer_host_init_sized(rdram_size);
}

const uint32_t* renderer_host_vi_regs(uint32_t* out_count) {
  if (!s_initialized) {
    return NULL;
  }
  if (out_count) {
    *out_count = VI_NUM_REG;
  }
  return s_vi_regs;
}

void renderer_host_set_vi_register(uint32_t index, uint32_t value) {
  if (!s_initialized) {
    return;
  }
  if (index >= VI_NUM_REG) {
    return;
  }
  s_vi_regs[index] = value;
}

struct CmdSink* renderer_host_cmd_sink(void) {
  return s_initialized ? &s_sink : NULL;
}

void renderer_host_present(void) {
  if (!s_initialized) {
    return;
  }
  rdpx_video_update_screen(NULL);  // runs VI -> scanout callback fires
}

// ---- hardcoded clear -------------------------------------------------------
static void emit_cmd2(uint32_t w0, uint32_t w1) {
  uint32_t words[2] = {w0, w1};
  rdpx_rdp_cmd(0, words);
}

void renderer_host_submit_clear(uint16_t rgba5551) {
  if (!s_initialized) {
    return;
  }

  // Fill the full CLEAR_FB_DIM-square source (see renderer_host_program_vi):
  // the visible 240x240 window crops from inside it, so the output is uniform.
  const uint32_t w = CLEAR_FB_DIM;
  const uint32_t h = CLEAR_FB_DIM;
  const uint32_t addr = CLEAR_FB_ADDR;

  // SET_COLOR_IMAGE: fmt RGBA(0) << 21, size 16b(2) << 19, (width-1) in [9:0].
  emit_cmd2((CMD_SET_COLOR_IMAGE << 24) | (IMG_FMT_RGBA << 21) |
                (IMG_SIZE_16B << 19) | ((w - 1U) & 0x3ffU),
            addr);

  // SET_OTHER_MODES: select FILL cycle type so FILL_RECTANGLE writes fill
  // color.
  emit_cmd2((CMD_SET_OTHER_MODES << 24) | (CYCLE_TYPE_FILL << 20), 0U);

  // SET_SCISSOR: full 240x240, subpixel coords (<<2). xh/yh in word0, xl/yl
  // word1.
  {
    const uint32_t xh = 0U;
    const uint32_t yh = 0U;
    const uint32_t xl = (w << 2);
    const uint32_t yl = (h << 2);
    emit_cmd2((CMD_SET_SCISSOR << 24) | (xh << 12) | yh, (xl << 12) | yl);
  }

  // SET_FILL_COLOR: RGBA5551 16-bit value replicated into both 16-bit halves
  // (the fill writes two pixels per 32-bit RDRAM word).
  {
    const uint32_t fill = ((uint32_t)rgba5551 << 16) | (uint32_t)rgba5551;
    emit_cmd2((CMD_SET_FILL_COLOR << 24), fill);
  }

  // FILL_RECTANGLE over the whole framebuffer. Subpixel: word0 holds (xl,yl) as
  // (x+width-4, y+height-4); word1 holds (xh,yh) = (x,y). Here x=y=0.
  {
    const uint32_t xl = (w << 2) - 4U;  // inclusive lower-right, subpixel
    const uint32_t yl = (h << 2) - 4U;
    emit_cmd2(
        (CMD_FILL_RECTANGLE << 24) | ((xl & 0xfffU) << 12) | (yl & 0xfffU), 0U);
  }

  // SYNC_FULL: flush the pipeline (raises the DP interrupt in real HW).
  emit_cmd2((CMD_SYNC_FULL << 24), 0U);
}

// ---- demo driving ----------------------------------------------------------
uint8_t* renderer_host_rdram(uint32_t* out_size) {
  if (!s_initialized) {
    return NULL;
  }
  if (out_size) {
    *out_size = s_rdram_size;
  }
  return s_rdram;
}

void renderer_host_present_demo(uint32_t src_fb_addr, uint32_t src_w,
                                uint32_t src_h) {
  if (!s_initialized) {
    return;
  }

  // Blit the demo color FB (RGBA5551, src_w-wide rows) into the 256-wide
  // staging FB the VI scans out, shifting +8 columns so the VI's fixed +8 crop
  // recovers source columns [0, src_w). Both buffers live in the renderer's
  // RDRAM under the byte-swizzle, so copy via the XOR-correct halfword helpers.
  const uint32_t dst_stride_px = CLEAR_FB_DIM;  // 256 (staging row stride)
  const uint32_t dst_col_off = 8U;              // matches the VI crop offset
  if (src_w > dst_stride_px - dst_col_off) {
    src_w = dst_stride_px - dst_col_off;
  }
  if (src_h > CLEAR_FB_DIM) {
    src_h = CLEAR_FB_DIM;
  }

  for (uint32_t y = 0; y < src_h; ++y) {
    const uint32_t src_row_off = src_fb_addr + (y * src_w) * 2U;
    const uint32_t dst_row_off =
        CLEAR_FB_ADDR + (y * dst_stride_px + dst_col_off) * 2U;
    for (uint32_t x = 0; x < src_w; ++x) {
      uint16_t const px = rdram_read16(s_rdram, src_row_off + x * 2U);
      rdram_write16(s_rdram, dst_row_off + x * 2U, px);
    }
  }

  rdpx_video_update_screen(NULL);  // runs VI over the staging FB -> scanout cb
}

// ---- accessors -------------------------------------------------------------
const uint32_t* renderer_host_scanout(uint32_t* out_w, uint32_t* out_h,
                                      uint32_t* out_pitch_px) {
  if (!s_scanout_valid) {
    return NULL;
  }
  if (out_w) {
    *out_w = s_scanout_w;
  }
  if (out_h) {
    *out_h = s_scanout_h;
  }
  if (out_pitch_px) {
    *out_pitch_px = s_scanout_w;  // captured tightly packed
  }
  return s_scanout;
}

int renderer_host_crash_latched(void) {
  return s_crash_latched || rdpx_pipeline_crashed();
}
