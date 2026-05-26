// n64video.c — our renderer's single translation unit (Stream B.1 / M1).
//
// Ported from the CANONICAL Angrylion fork parallel-rdp embeds:
//   parallel-rdp/angrylion-rdp-plus @ 31bdb1f (src/core/n64video.c).
//
// Single-TU model (mirrors the fork): this file owns the renderer `config`,
// the shared MIN/MAX/SIGN/RGBA macros + irand/clamp, the command-ID table, and
// then #includes the RDP pipeline (rdp/rdp.c, which in turn #includes the stage
// files rdp/{rdram,dither,blender,combiner,coverage,zbuffer,fbuffer,tmem,tcoord,
// tex,rasterizer}.c) under N64VIDEO_C. At M1 the FILL path is wired end-to-end:
// the conformance adapter feeds RDP command words to rdpx_rdp_cmd(), which
// dispatches through the real command table into the rasterizer/fbuffer, so the
// fill suites compare BYTE-FOR-BYTE against the Angrylion oracle.
//
// Symbol hygiene (CRITICAL): the conformance executable links this lib AND the
// Angrylion oracle, which keeps Angrylion's ORIGINAL C names (n64video_init,
// rdp_cmd, state, get_tmem, rdram_hidden, z_init_lut, ...). To avoid duplicate-
// symbol collisions, EVERY external symbol this TU exposes is rdpx_*-prefixed;
// all Angrylion-named functions/tables/state are given INTERNAL LINKAGE (static)
// in rdp/rdp.c. The vdac / msg sinks are rdpx_vdac_* / rdpx_msg_*; the renderer's
// own message reporting routes through static msg_error/warning/debug shims.
//
// Compiled as C++ (Orthodox C++ subset), exposing a pure C ABI. -fno-strict-
// aliasing is pinned by the build (RDRAM is aliased through uint8/16/32*).

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include "n64video.h"
#include "n64video_common.h"

// The renderer's single config global. rdram.c reads config.gfx.rdram /
// config.gfx.rdram_size in rdram_init(). Read by handlers via config.gfx.*.
static struct n64video_config config;

// ---- shared macros (from the fork's n64video.c) --------------------------
#define MIN(a, b) (((a) < (b)) ? (a) : (b))
#define MAX(a, b) (((a) > (b)) ? (a) : (b))
#define CLAMP(x, lo, hi) (((x) > (hi)) ? (hi) : (((x) < (lo)) ? (lo) : (x)))

#define SIGN16(x)   ((int16_t)(x))
#define SIGN8(x)    ((int8_t)(x))

#define SIGN(x, numb)	(((x) & ((1 << (numb)) - 1)) | -((x) & (1 << ((numb) - 1))))
#define SIGNF(x, numb)	((x) | -((x) & (1 << ((numb) - 1))))

#define TRELATIVE(x, y)     ((x) - ((y) << 3))

#define PIXELS_TO_BYTES(pix, siz) (((pix) << (siz)) >> 1)

// RGBA5551 to RGBA8888 helper
#define RGBA16_R(x) (((x) >> 8) & 0xf8)
#define RGBA16_G(x) (((x) & 0x7c0) >> 3)
#define RGBA16_B(x) (((x) & 0x3e) << 2)

// RGBA8888 helper
#define RGBA32_R(x) (((x) >> 24) & 0xff)
#define RGBA32_G(x) (((x) >> 16) & 0xff)
#define RGBA32_B(x) (((x) >> 8) & 0xff)
#define RGBA32_A(x) ((x) & 0xff)

// maximum number of commands to buffer for parallel processing
#define CMD_BUFFER_SIZE 1024

// maximum data size of a single command in bytes
#define CMD_MAX_SIZE 176

// maximum data size of a single command in 32 bit integers
#define CMD_MAX_INTS (CMD_MAX_SIZE / sizeof(int32_t))

// extracts the command ID from a command buffer
#define CMD_ID(cmd) ((*(cmd) >> 24) & 0x3f)

// list of command IDs
#define CMD_ID_NO_OP                           0x00
#define CMD_ID_FILL_TRIANGLE                   0x08
#define CMD_ID_FILL_ZBUFFER_TRIANGLE           0x09
#define CMD_ID_TEXTURE_TRIANGLE                0x0a
#define CMD_ID_TEXTURE_ZBUFFER_TRIANGLE        0x0b
#define CMD_ID_SHADE_TRIANGLE                  0x0c
#define CMD_ID_SHADE_ZBUFFER_TRIANGLE          0x0d
#define CMD_ID_SHADE_TEXTURE_TRIANGLE          0x0e
#define CMD_ID_SHADE_TEXTURE_Z_BUFFER_TRIANGLE 0x0f
#define CMD_ID_TEXTURE_RECTANGLE               0x24
#define CMD_ID_TEXTURE_RECTANGLE_FLIP          0x25
#define CMD_ID_SYNC_LOAD                       0x26
#define CMD_ID_SYNC_PIPE                       0x27
#define CMD_ID_SYNC_TILE                       0x28
#define CMD_ID_SYNC_FULL                       0x29
#define CMD_ID_SET_KEY_GB                      0x2a
#define CMD_ID_SET_KEY_R                       0x2b
#define CMD_ID_SET_CONVERT                     0x2c
#define CMD_ID_SET_SCISSOR                     0x2d
#define CMD_ID_SET_PRIM_DEPTH                  0x2e
#define CMD_ID_SET_OTHER_MODES                 0x2f
#define CMD_ID_LOAD_TLUT                       0x30
#define CMD_ID_SET_TILE_SIZE                   0x32
#define CMD_ID_LOAD_BLOCK                      0x33
#define CMD_ID_LOAD_TILE                       0x34
#define CMD_ID_SET_TILE                        0x35
#define CMD_ID_FILL_RECTANGLE                  0x36
#define CMD_ID_SET_FILL_COLOR                  0x37
#define CMD_ID_SET_FOG_COLOR                   0x38
#define CMD_ID_SET_BLEND_COLOR                 0x39
#define CMD_ID_SET_PRIM_COLOR                  0x3a
#define CMD_ID_SET_ENV_COLOR                   0x3b
#define CMD_ID_SET_COMBINE                     0x3c
#define CMD_ID_SET_TEXTURE_IMAGE               0x3d
#define CMD_ID_SET_MASK_IMAGE                  0x3e
#define CMD_ID_SET_COLOR_IMAGE                 0x3f

static struct
{
    bool fillmbitcrashes, vbusclock, nolerp;
} onetimewarnings;

static int rdp_pipeline_crashed = 0;

static STRICTINLINE int32_t clamp(int32_t value, int32_t min, int32_t max)
{
    if (value < min)
        return min;
    else if (value > max)
        return max;
    else
        return value;
}

// irand: lifted from common.h (static STRICTINLINE). The stage files call the
// global `irand(&state[wid].rseed)`; common.h's definition is in scope.

// ---- message sinks --------------------------------------------------------
// The fork's stage code calls msg_error/msg_warning/msg_debug (variadic). Our
// renderer routes them to file-static shims so they never collide with the
// oracle's externally-linked msg_* and incur no host I/O during validation.
// (The fill paths only emit one-time warnings on the RDP-crash codepaths.)
static void msg_error(const char* err, ...)
{
    (void)err;
}

static void msg_warning(const char* err, ...)
{
    (void)err;
}

static void msg_debug(const char* err, ...)
{
    (void)err;
}

// include guard to prevent compilation of code modules as translation units
#define N64VIDEO_C

#ifdef RDPX_TESTING
// Per-frame count of pixels the rasterizer commits via the coverage/blend write
// path (fbwrite_ptr). File-static in this single TU; defined BEFORE rdp/rdp.c so
// it is in scope where rasterizer.c increments it, and reached by the accessors
// below. Test-only: compiled out entirely when RDPX_TESTING is undefined, so it
// can never perturb a rendered bit on the production picosystem build.
static uint64_t rdpx_pixel_count = 0;
#endif

#include "rdp/rdp.c"

// ---- VI (Stream C / M8) ---------------------------------------------------
// Port of Angrylion's VI from 31bdb1f (src/core/n64video/vi.c + vi/*.c). The VI
// code is included whole (mirroring the fork's `#include "n64video/vi.c"`); it
// in turn includes vi/{gamma,lerp,divot,video,restore,fetch}.c.
//
// The fork's vi.c depends on a handful of symbols that live outside the RDP
// stage files: the `struct rgba` / `struct frame_buffer` scanout PODs (fork's
// vdac.h), the vdac_* sink, the parallel_* worker API, and PARALLEL_MAX_WORKERS.
// We supply file-static stand-ins here so the VI source ports BYTE-FOR-BYTE:
//
//   * struct rgba / struct frame_buffer — same layout as the fork's vdac.h (and
//     our public n64video_pixel / n64video_frame_buffer). Local POD types: types
//     have no linkage, so no collision with the oracle's identically-named ones.
//   * vdac_* — static shims. vdac_write/sync route the finished prescale buffer
//     to the rdpx_vdac_write / rdpx_vdac_sync sink (which forwards to the test
//     adapter), exactly as the oracle's vdac_write calls update_screen().
//   * parallel_* — single-host-worker stubs. config.parallel is always false in
//     the conformance adapter, so the parallel branches in vi.c are dead; the
//     stubs exist only so the dead branches link.
//
// Every VI external (vi_update_screen, vi_set_zbuffer_address, vi_gamma_init,
// vi_restore_init, vi_init, vi_close) was made `static` in the ported vi/*.c so
// nothing collides with the oracle's original VI symbol names at link.

#define PARALLEL_MAX_WORKERS RDPX_PARALLEL_MAX_WORKERS

struct rgba
{
    uint8_t r;
    uint8_t g;
    uint8_t b;
    uint8_t a;
};

struct frame_buffer
{
    struct rgba* pixels;
    uint32_t width;
    uint32_t height;
    uint32_t height_out;
    uint32_t pitch;
};

// Adapter-installed scanout callback. data is RGBA8888 (struct rgba) pixels;
// mirrors the oracle's vdac_write -> update_screen(pixels, width, height, pitch).
// Installed by the conformance adapter via rdpx_vdac_set_scanout_cb (below).
static void (*rdpx_scanout_cb)(const void* data, uint32_t width, uint32_t height, uint32_t pitch);

// Internal bridges: the static vdac_* shims forward the finished prescale frame
// here. On an invalid frame (vdac_sync(true)) we forward a null/zero frame, as
// the oracle's vdac_sync does.
static void rdpx_vdac_write_fb(struct frame_buffer* fb)
{
    if (rdpx_scanout_cb)
        rdpx_scanout_cb(fb->pixels, fb->width, fb->height, fb->pitch);
}

static void rdpx_vdac_sync_internal(bool invalid)
{
    if (invalid && rdpx_scanout_cb)
        rdpx_scanout_cb(NULL, 0, 0, 0);
}

// vdac sink shims — see comment above. vi_init calls vdac_init; vi_process_*
// call vdac_write; vi_update_screen calls vdac_sync; vi_close calls vdac_close.
static void vdac_init(struct n64video_config* cfg)
{
    (void)cfg;
}

static void vdac_write(struct frame_buffer* fb)
{
    rdpx_vdac_write_fb(fb);
}

static void vdac_sync(bool invalid)
{
    rdpx_vdac_sync_internal(invalid);
}

static void vdac_close(void)
{
}

// parallel_* stubs — single host worker. config.parallel is false in the
// adapter, so vi.c's parallel_run / parallel_num_workers branches never execute;
// these only satisfy the linker for the dead code path.
static uint32_t parallel_num_workers(void)
{
    return 1;
}

static void parallel_run(void (*task)(uint32_t))
{
    task(0);
}

#include "vi/vi.c"

#undef N64VIDEO_C

// One-time static init of the Angrylion lookup tables and per-worker RDP state.
// Mirrors the fork's n64video_init() static_init block (fill needs the dither /
// blender / combiner / coverage / z / tex LUTs + the rasterizer clip defaults).
static bool rdpx_static_init_done;

static void rdpx_static_init(void)
{
    if (rdpx_static_init_done)
        return;

    blender_init_lut();
    coverage_init_lut();
    combiner_init_lut();
    tex_init_lut();
    z_init_lut();

    fb_init(0);
    combiner_init(0);
    tex_init(0);
    rasterizer_init(0);

    rdpx_static_init_done = true;
}

extern "C" {

// ---- rdpx_* ABI entry points (see n64video.h) ----------------------------

void rdpx_video_config_init(struct n64video_config* cfg)
{
    // Mirror angrylion n64video_config_init: zero-init the config to known
    // defaults. The caller fills gfx.* pointers/sizes before rdpx_video_init.
    memset(cfg, 0, sizeof(*cfg));
}

void rdpx_video_init(struct n64video_config* cfg)
{
    // Take a copy of the caller's config (matches angrylion, which copies into
    // its file-static `config`). rdram_init() wires the aliasing pointers and
    // seeds hidden RDRAM to 3, byte-for-byte as the 31bdb1f oracle does.
    config = *cfg;

    rdpx_static_init();

    rdram_init();
    vi_init();
    rdp_pipeline_crashed = 0;
    memset(&onetimewarnings, 0, sizeof(onetimewarnings));

    // Single host worker (no parallel split): worker 0 owns full framebuffer.
    rdp_init(0, 1);
}

void rdpx_video_process_list(void)
{
    // The conformance harness feeds commands directly via rdpx_rdp_cmd(); the
    // DP-register-driven list walk (n64video_process_list) is not exercised on
    // host. Left as a no-op so the adapter's per-command callback can stay.
}

void rdpx_rdp_cmd(uint32_t wid, const uint32_t* args)
{
    // Real per-command dispatch — the harness entry point. Mirrors the oracle
    // adapter's rdp_cmd(0, words). If a prior command crashed the pipeline,
    // angrylion stops processing; replicate by gating on rdp_pipeline_crashed.
    if (rdp_pipeline_crashed)
        return;
    rdp_cmd(wid, args);
}

void rdpx_video_update_screen(struct n64video_frame_buffer* fb)
{
    // M8: run the VI. Reads VI registers from config.gfx.vi_reg, fetches +
    // filters the framebuffer into the prescale buffer, and emits the finished
    // frame through the vdac sink (vdac_write -> rdpx_vdac_write callback ->
    // adapter -> iface.update_screen). The argument is unused: the harness reads
    // scanout via the event interface, not this struct.
    (void)fb;
    vi_update_screen();
}

void rdpx_video_close(void)
{
    // Release VI resources (none beyond file-static storage) and reset the crash
    // latch so a fresh driver instance starts clean.
    vi_close();
    rdp_pipeline_crashed = 0;
}

// ---- rdpx_vdac_* scanout sink --------------------------------------------
// The VI emits the finished prescale buffer through the static vdac_write /
// vdac_sync shims (above), which forward here. The conformance adapter installs
// a scanout callback (rdpx_vdac_set_scanout_cb) that mirrors the oracle's
// vdac_write -> update_screen(pixels, width, height, pitch) path; on an invalid
// frame (vdac_sync(true)) it forwards a null/zero frame, exactly like the
// oracle's vdac_sync. Pixels are RGBA8888 (struct rgba), which is what
// compare_image checks.

struct rdpx_frame_buffer
{
    uint32_t* pixels;
    uint32_t width;
    uint32_t height;
    uint32_t pitch;
};

void rdpx_vdac_set_scanout_cb(void (*cb)(const void*, uint32_t, uint32_t, uint32_t))
{
    rdpx_scanout_cb = cb;
}

void rdpx_vdac_init(struct n64video_config* cfg)
{
    (void)cfg;
}

void rdpx_vdac_write(struct rdpx_frame_buffer* fb)
{
    if (rdpx_scanout_cb)
        rdpx_scanout_cb(fb->pixels, fb->width, fb->height, fb->pitch);
}

void rdpx_vdac_sync(bool invalid)
{
    rdpx_vdac_sync_internal(invalid);
}

void rdpx_vdac_close(void)
{
}

// ---- rdpx_msg_* log sink --------------------------------------------------
// rdpx_-prefixed stand-ins for angrylion's msg_error/warning/debug. No-ops.

void rdpx_msg_error(const char* err)
{
    (void)err;
}

void rdpx_msg_warning(const char* err)
{
    (void)err;
}

void rdpx_msg_debug(const char* err)
{
    (void)err;
}

// ---- RDPX_TESTING accessor block ------------------------------------------
// Exposes file-static base pointers so the conformance adapter can hand the
// harness our hidden-RDRAM and TMEM buffers (RDRAM itself is caller-owned via
// config.gfx.rdram). TMEM lives inside state[0] (per-worker tmem[0x1000]).
#ifdef RDPX_TESTING

uint8_t* rdpx_get_hidden_rdram(void)
{
    return rdram_hidden;
}

uint32_t rdpx_get_hidden_rdram_size(void)
{
    return (uint32_t)sizeof(rdram_hidden);
}

uint8_t* rdpx_get_tmem(void)
{
    return get_tmem();
}

uint32_t rdpx_get_tmem_size(void)
{
    return (uint32_t)sizeof(state[0].tmem);
}

uint64_t rdpx_get_pixel_count(void)
{
    return rdpx_pixel_count;
}

void rdpx_reset_pixel_count(void)
{
    rdpx_pixel_count = 0;
}

#endif  // RDPX_TESTING

}  // extern "C"
