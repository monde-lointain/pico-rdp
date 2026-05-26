// n64video.cc — the renderer's public C ABI layer + one-time static init.
//
// Ported from the CANONICAL Angrylion fork parallel-rdp embeds:
//   parallel-rdp/angrylion-rdp-plus @ 31bdb1f (src/core/n64video.c).
//
// Formerly the single unity TU (it #included rdp/rdp.c + vi/vi.c + every stage
// .c under N64VIDEO_C). The pipeline is now split into individual TUs
// (rdp/*.cc, vi/*.cc); this file holds: the renderer globals (rdpxi_config /
// pipeline-crash latch / onetimewarnings / pixel-count), the rdpxi_msg_* +
// rdpxi_vdac_* + rdpxi_parallel_* shims the VI TU calls, rdpx_static_init()
// (which drives each stage's *_init_lut), and the extern "C" rdpx_* ABI exposed
// by n64video.h.
//
// Symbol hygiene (CRITICAL): the conformance executable links this lib AND the
// Angrylion oracle, which keeps Angrylion's ORIGINAL C names (n64video_init,
// rdp_cmd, state, get_tmem, rdram_hidden, z_init_lut, ...). To avoid duplicate-
// symbol collisions, every external symbol this lib exposes is rdpx_* (public
// ABI) or rdpxi_* (cross-TU internal); the oracle's names stay collision-free.
//
// Compiled as C++ (Orthodox C++ subset), exposing a pure C ABI. -fno-strict-
// aliasing is pinned by the build (RDRAM is aliased through uint8/16/32*).

#include "n64video.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "n64video_common.h"
#include "rdp/rdp_internal.h"
#include "rdp/rdram_internal.h"  // rdram_init() + rdpxi_rdram_hidden accessor
#include "vi/vi_internal.h"      // struct Rgba/FrameBuffer + VI exports
// Stage headers for rdpx_static_init() (the *_init/_init_lut funcs) + the
// RDPX_TESTING tmem accessor. The unity build used to get these via rdp.c.
#include "rdp/blender_internal.h"
#include "rdp/combiner_internal.h"
#include "rdp/coverage_internal.h"
#include "rdp/fbuffer_internal.h"
#include "rdp/rasterizer_internal.h"
#include "rdp/tex_internal.h"
#include "rdp/tmem_internal.h"
#include "rdp/zbuffer_internal.h"

// The renderer config global. External linkage (rdpxi_-prefixed to avoid
// colliding with the Angrylion oracle's `config`) so split-out stage TUs
// (rdram.cc, …) can read it. Declared in rdp_internal.h. rdram_init() reads
// rdpxi_config.gfx.rdram / .rdram_size; handlers read rdpxi_config.gfx.*.
struct N64videoConfig rdpxi_config;

struct OneTimeWarnings onetimewarnings;

int rdpxi_pipeline_crashed = 0;

// clamp lives in rdp_internal.h (header static inline, shared across TUs).
// irand: from n64video_common.h (static STRICTINLINE). Stage files call
// irand(&rdpxi_state[wid].rseed); common.h's definition is in scope.

// ---- message sinks --------------------------------------------------------
// The fork's stage code calls msg_error/msg_warning/msg_debug (variadic). Our
// renderer routes them to rdpxi_msg_* shims (external linkage, rdpxi_-prefixed
// so they never collide with the oracle's externally-linked msg_*) and incur no
// host I/O during validation. Declared in rdp_internal.h.
void rdpxi_msg_error(const char* err, ...) { (void)err; }

void rdpxi_msg_warning(const char* err, ...) { (void)err; }

void rdpxi_msg_debug(const char* err, ...) { (void)err; }

#ifdef RDPX_TESTING
// Per-frame count of pixels the rasterizer commits via the coverage/blend write
// path (fbwrite_ptr). File-static in this single TU; defined BEFORE rdp/rdp.c
// so it is in scope where rasterizer.c increments it, and reached by the
// accessors below. Test-only: compiled out entirely when RDPX_TESTING is
// undefined, so it can never perturb a rendered bit on the production
// picosystem build.
uint64_t rdpxi_pixel_count = 0;
#endif

// ---- VI (Stream C / M8) ---------------------------------------------------
// Port of Angrylion's VI from 31bdb1f (src/core/n64video/vi.c + vi/*.c). The VI
// code is included whole (mirroring the fork's `#include "n64video/vi.c"`); it
// in turn includes vi/{gamma,lerp,divot,video,restore,fetch}.c.
//
// The fork's vi.c depends on a handful of symbols that live outside the RDP
// stage files: the `struct Rgba` / `struct FrameBuffer` scanout PODs (fork's
// vdac.h), the vdac_* sink, the parallel_* worker API, and
// PARALLEL_MAX_WORKERS. We supply file-static stand-ins here so the VI source
// ports BYTE-FOR-BYTE:
//
//   * struct Rgba / struct FrameBuffer — same layout as the fork's vdac.h (and
//     our public n64video_pixel / n64video_frame_buffer). Local POD types:
//     types have no linkage, so no collision with the oracle's
//     identically-named ones.
//   * vdac_* — static shims. rdpxi_vdac_write/sync route the finished prescale
//   buffer
//     to the rdpx_vdac_write / rdpx_vdac_sync sink (which forwards to the test
//     adapter), exactly as the oracle's rdpxi_vdac_write calls update_screen().
//   * parallel_* — single-host-worker stubs. rdpxi_config.parallel is always
//   false in
//     the conformance adapter, so the parallel branches in vi.c are dead; the
//     stubs exist only so the dead branches link.
//
// Every VI external (vi_update_screen, rdpxi_vi_set_zbuffer_address,
// rdpxi_vi_gamma_init, rdpxi_vi_restore_init, vi_init, vi_close) was made
// `static` in the ported vi/*.c so nothing collides with the oracle's original
// VI symbol names at link.

// struct Rgba / struct FrameBuffer / PARALLEL_MAX_WORKERS now live in
// vi/vi_internal.h (shared with the VI stage TUs).

// Adapter-installed scanout callback. data is RGBA8888 (struct Rgba) pixels;
// mirrors the oracle's rdpxi_vdac_write -> update_screen(pixels, width, height,
// pitch). Installed by the conformance adapter via rdpx_vdac_set_scanout_cb
// (below).
static void (*rdpx_scanout_cb)(const void* data, uint32_t width,
                               uint32_t height, uint32_t pitch);

// Internal bridges: the static vdac_* shims forward the finished prescale frame
// here. On an invalid frame (rdpxi_vdac_sync(true)) we forward a null/zero
// frame, as the oracle's rdpxi_vdac_sync does.
static void rdpx_vdac_write_fb(struct FrameBuffer* fb) {
  if (rdpx_scanout_cb) {
    rdpx_scanout_cb(fb->pixels, fb->width, fb->height, fb->pitch);
  }
}

static void rdpx_vdac_sync_internal(bool invalid) {
  if (invalid && rdpx_scanout_cb) {
    rdpx_scanout_cb(NULL, 0, 0, 0);
  }
}

// vdac sink shims — see comment above. vi_init calls rdpxi_vdac_init;
// vi_process_* call rdpxi_vdac_write; vi_update_screen calls rdpxi_vdac_sync;
// vi_close calls rdpxi_vdac_close.
void rdpxi_vdac_init(struct N64videoConfig* cfg) { (void)cfg; }

void rdpxi_vdac_write(struct FrameBuffer* fb) { rdpx_vdac_write_fb(fb); }

void rdpxi_vdac_sync(bool invalid) { rdpx_vdac_sync_internal(invalid); }

void rdpxi_vdac_close(void) {}

// parallel_* stubs — single host worker. rdpxi_config.parallel is false in the
// adapter, so vi.c's rdpxi_parallel_run / rdpxi_parallel_num_workers branches
// never execute; these only satisfy the linker for the dead code path.
uint32_t rdpxi_parallel_num_workers(void) { return 1; }

void rdpxi_parallel_run(void (*task)(uint32_t)) { task(0); }

// One-time static init of the Angrylion lookup tables and per-worker RDP state.
// Mirrors the fork's n64video_init() static_init block (fill needs the dither /
// blender / combiner / coverage / z / tex LUTs + the rasterizer clip defaults).
static bool rdpx_static_init_done;

static void rdpx_static_init(void) {
  if (rdpx_static_init_done) {
    return;
  }

  blender_init_lut();
  coverage_init_lut();
  combiner_init_lut();
  tex_init_lut();
  rdpxi_z_init_lut();

  fb_init(0);
  combiner_init(0);
  tex_init(0);
  rasterizer_init(0);

  rdpx_static_init_done = true;
}

extern "C" {

// ---- rdpx_* ABI entry points (see n64video.h) ----------------------------

void rdpx_video_config_init(struct N64videoConfig* cfg) {
  // Mirror angrylion n64video_config_init: zero-init the config to known
  // defaults. The caller fills gfx.* pointers/sizes before rdpx_video_init.
  memset(cfg, 0, sizeof(*cfg));
}

void rdpx_video_init(struct N64videoConfig* cfg) {
  // Take a copy of the caller's config (matches angrylion, which copies into
  // its file-static `config`). rdram_init() wires the aliasing pointers and
  // seeds hidden RDRAM to 3, byte-for-byte as the 31bdb1f oracle does.
  rdpxi_config = *cfg;

  rdpx_static_init();

  rdram_init();
  vi_init();
  rdpxi_pipeline_crashed = 0;
  memset(&onetimewarnings, 0, sizeof(onetimewarnings));

  // Single host worker (no parallel split): worker 0 owns full framebuffer.
  rdpxi_rdp_init(0, 1);
}

void rdpx_video_process_list(void) {
  // The conformance harness feeds commands directly via rdpx_rdp_cmd(); the
  // DP-register-driven list walk (n64video_process_list) is not exercised on
  // host. Left as a no-op so the adapter's per-command callback can stay.
}

void rdpx_rdp_cmd(uint32_t wid, const uint32_t* args) {
  // Real per-command dispatch — the harness entry point. Mirrors the oracle
  // adapter's rdpxi_rdp_cmd(0, words). If a prior command crashed the pipeline,
  // angrylion stops processing; replicate by gating on rdpxi_pipeline_crashed.
  if (rdpxi_pipeline_crashed) {
    return;
  }
  rdpxi_rdp_cmd(wid, args);
}

void rdpx_video_update_screen(struct N64videoFrameBuffer* fb) {
  // M8: run the VI. Reads VI registers from rdpxi_config.gfx.vi_reg, fetches +
  // filters the framebuffer into the prescale buffer, and emits the finished
  // frame through the vdac sink (rdpxi_vdac_write -> rdpx_vdac_write callback
  // -> adapter -> iface.update_screen). The argument is unused: the harness
  // reads scanout via the event interface, not this struct.
  (void)fb;
  vi_update_screen();
}

void rdpx_video_close(void) {
  // Release VI resources (none beyond file-static storage) and reset the crash
  // latch so a fresh driver instance starts clean.
  vi_close();
  rdpxi_pipeline_crashed = 0;
}

// ---- rdpx_vdac_* scanout sink --------------------------------------------
// The VI emits the finished prescale buffer through the static rdpxi_vdac_write
// / rdpxi_vdac_sync shims (above), which forward here. The conformance adapter
// installs a scanout callback (rdpx_vdac_set_scanout_cb) that mirrors the
// oracle's rdpxi_vdac_write -> update_screen(pixels, width, height, pitch)
// path; on an invalid frame (rdpxi_vdac_sync(true)) it forwards a null/zero
// frame, exactly like the oracle's rdpxi_vdac_sync. Pixels are RGBA8888 (struct
// Rgba), which is what compare_image checks.

struct RdpxFrameBuffer {
  uint32_t* pixels;
  uint32_t width;
  uint32_t height;
  uint32_t pitch;
};

void rdpx_vdac_set_scanout_cb(void (*cb)(const void*, uint32_t, uint32_t,
                                         uint32_t)) {
  rdpx_scanout_cb = cb;
}

void rdpx_vdac_init(struct N64videoConfig* cfg) { (void)cfg; }

void rdpx_vdac_write(struct RdpxFrameBuffer* fb) {
  if (rdpx_scanout_cb) {
    rdpx_scanout_cb(fb->pixels, fb->width, fb->height, fb->pitch);
  }
}

void rdpx_vdac_sync(bool invalid) { rdpx_vdac_sync_internal(invalid); }

void rdpx_vdac_close(void) {}

// ---- rdpx_msg_* log sink --------------------------------------------------
// rdpx_-prefixed stand-ins for angrylion's msg_error/warning/debug. No-ops.

void rdpx_msg_error(const char* err) { (void)err; }

void rdpx_msg_warning(const char* err) { (void)err; }

void rdpx_msg_debug(const char* err) { (void)err; }

// Public query for the pipeline-crash latch (see header). Behavior-neutral;
// always compiled in so a front-end can surface the crash state.
int rdpx_pipeline_crashed(void) { return rdpxi_pipeline_crashed; }

// ---- RDPX_TESTING accessor block ------------------------------------------
// Exposes file-static base pointers so the conformance adapter can hand the
// harness our hidden-RDRAM and TMEM buffers (RDRAM itself is caller-owned via
// rdpxi_config.gfx.rdram). TMEM lives inside rdpxi_state[0] (per-worker
// tmem[0x1000]).
#ifdef RDPX_TESTING

uint8_t* rdpx_get_hidden_rdram(void) { return rdpxi_rdram_hidden; }

uint32_t rdpx_get_hidden_rdram_size(void) {
  return (uint32_t)sizeof(rdpxi_rdram_hidden);
}

uint8_t* rdpx_get_tmem(void) { return rdpxi_get_tmem(); }

uint32_t rdpx_get_tmem_size(void) {
  return (uint32_t)sizeof(rdpxi_state[0].tmem);
}

uint64_t rdpx_get_pixel_count(void) { return rdpxi_pixel_count; }

void rdpx_reset_pixel_count(void) { rdpxi_pixel_count = 0; }

#endif  // RDPX_TESTING

}  // extern "C"
