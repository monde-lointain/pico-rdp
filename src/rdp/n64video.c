// n64video.c — our renderer's single translation unit (M0 STUB, Stream A.0).
//
// Ported skeleton of angrylion-rdp-plus src/core/n64video.c. At M0 this TU is a
// NO-OP stub: it owns the `config` global, pulls in the RDRAM/hidden module
// (rdp/rdram.c) under N64VIDEO_C, and implements the rdpx_* C ABI as no-ops that
// only init/clear RDRAM + hidden + TMEM per the config. No pixel logic yet (that
// is Stream B). Its sole job at M0 is to wire harness -> adapter -> renderer and
// prove the initial RDRAM/hidden buffers compare EQUAL against the Angrylion
// oracle over an empty command stream.
//
// Symbol hygiene (CRITICAL): the conformance executable links this lib AND the
// Angrylion oracle, which keeps Angrylion's ORIGINAL C names (n64video_init,
// rdp_cmd, vdac_*, msg_*, get_tmem, rdram_hidden). To avoid duplicate-symbol
// collisions, EVERY external symbol this TU exposes is rdpx_*-prefixed; the
// hidden/rdram statics from rdram.c are file-static (not exported). The vdac /
// msg sinks are rdpx_vdac_* / rdpx_msg_*.
//
// Compiled as C++ (Orthodox C++ subset), exposing a pure C ABI. -fno-strict-
// aliasing is pinned by the build (RDRAM is aliased through uint8/16/32*).

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "n64video.h"
#include "n64video_common.h"

// The renderer's single config global. rdram.c reads config.gfx.rdram /
// config.gfx.rdram_size in rdram_init(); the rest is unused at M0.
static struct n64video_config config;

// RDRAM / hidden-RDRAM module. Provides the file-static rdram_hidden array,
// rdram_init(), and the XOR-addressed accessors. Pulled in under N64VIDEO_C
// exactly as the real renderer will (single-TU model).
#define N64VIDEO_C
#include "rdp/rdram.c"
#undef N64VIDEO_C

// TMEM: 4 KiB, matching Angrylion's per-worker tmem[0x1000]. File-static so it
// never collides with the oracle's symbols. A single tile cache is enough for
// the host stub (the device renderer will revisit worker layout in Stream C).
#define RDPX_TMEM_SIZE 0x1000
static uint8_t rdpx_tmem[RDPX_TMEM_SIZE];

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
    // clears hidden RDRAM to HB_CLEAN, byte-for-byte as the oracle does.
    config = *cfg;
    rdram_init();
    memset(rdpx_tmem, 0, sizeof(rdpx_tmem));
}

void rdpx_video_process_list(void)
{
    // M0 stub: no command processing. Stream B fills the pixel pipeline.
}

void rdpx_video_update_screen(struct n64video_frame_buffer* fb)
{
    // M0 stub: no scanout. The vdac sink below is the eventual output path.
    (void)fb;
}

void rdpx_video_close(void)
{
    // M0 stub: nothing to release (all storage is file-static / caller-owned).
}

// ---- rdpx_vdac_* scanout sink --------------------------------------------
// Mirrors the oracle's vdac_* sink shape, rdpx_-prefixed. No-ops at M0; the
// adapter forwards nothing to the event interface until Stream B produces
// pixels.

struct rdpx_frame_buffer
{
    uint32_t* pixels;
    uint32_t width;
    uint32_t height;
    uint32_t pitch;
};

void rdpx_vdac_init(struct n64video_config* cfg)
{
    (void)cfg;
}

void rdpx_vdac_write(struct rdpx_frame_buffer* fb)
{
    (void)fb;
}

void rdpx_vdac_sync(bool invalid)
{
    (void)invalid;
}

void rdpx_vdac_close(void)
{
}

// ---- rdpx_msg_* log sink --------------------------------------------------
// rdpx_-prefixed stand-ins for angrylion's msg_error/warning/debug. No-ops at
// M0 (host stub has nothing to report).

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
// config.gfx.rdram). LUT pointers are intentionally empty at M0 (Stream B
// fills the LUTs; the lut_memcmp scaffold asserts trivially until then).
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
    return rdpx_tmem;
}

uint32_t rdpx_get_tmem_size(void)
{
    return (uint32_t)sizeof(rdpx_tmem);
}

#endif  // RDPX_TESTING

}  // extern "C"
