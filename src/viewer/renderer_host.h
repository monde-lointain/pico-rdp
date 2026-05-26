#pragma once

// renderer_host — owns the rdp_core renderer lifecycle for the viewer.
//
// It allocates the RDRAM arena + VI/DP register arrays + mi_intr, drives
// rdpx_video_init / _close, installs the scanout callback, programs the VI for
// a clean 240x240 scanout, and exposes the captured RGBA8888 prescale frame for
// the host to upload into an SDL texture.
//
// Command submission seam: downstream streams (source-switch / capture / panel
// stepping) feed a raw RDP command-word stream into the renderer through a
// `struct CmdSink` (see src/demo/cmd_sink.h). renderer_host_cmd_sink() hands
// back a sink whose emit() forwards each command to rdpx_rdp_cmd(0, words);
// callers build a frame into it, then call renderer_host_present() to run the
// VI and refresh the scanout. This is the ONLY coupling future submitters need.
//
// Single-instance: rdp_core's config is a file-static singleton, so this host
// is too (one renderer per process). NOT orthodox-enforced (mirrors the
// SDL/ImGui host harness): it consumes the C ABI but lives in the host viewer
// TU set.

#include <stdint.h>

#include "cmd_sink.h"  // struct CmdSink (the submission seam)

#ifdef __cplusplus
extern "C" {
#endif

// Output scanout dimensions. The VI is programmed to land exactly on these.
#define RENDERER_HOST_OUT_WIDTH 240
#define RENDERER_HOST_OUT_HEIGHT 240

// RDRAM arena size handed to rdpx_video_init. >= DEMO_RDRAM_USED_END; rounded
// up to a generous 2 MiB so future demo assets / larger frames fit without a
// realloc.
#define RENDERER_HOST_RDRAM_SIZE (2u * 1024u * 1024u)

// Lifecycle. init allocates the arena + registers and calls rdpx_video_init;
// close releases everything and clears the host crash latch. Returns 0 on
// success, non-zero on failure (e.g. allocation). reset = close then init,
// which also clears rdp_core's internal pipeline-crash latch.
int renderer_host_init(void);
void renderer_host_close(void);
int renderer_host_reset(void);

// The command-submission seam. The returned sink stays valid until close; its
// emit() forwards to rdpx_rdp_cmd(0, words). Returns NULL if not initialized.
struct CmdSink* renderer_host_cmd_sink(void);

// Run the VI (rdpx_video_update_screen) and refresh the captured scanout from
// the scanout callback. Call after submitting a complete frame's commands.
void renderer_host_present(void);

// Emit the hardcoded clear frame (SET_COLOR_IMAGE + SET_OTHER_MODES(fill) +
// SET_SCISSOR + SET_FILL_COLOR + FILL_RECTANGLE over 240x240 + SYNC_FULL) into
// the seam, using the demo color-FB address. rgba5551 is the RGBA5551 fill
// value. This writes the 256-wide staging FB the VI scans out directly.
void renderer_host_submit_clear(uint16_t rgba5551);

// The renderer-owned RDRAM arena (base + size), so a caller (the demo driver)
// can demo_init() it. The base IS the address space the renderer reads (RDP
// SET_*_IMAGE addresses and the VI index into it); the demo color FB therefore
// lands at demo_color_fb_addr() within this buffer. Returns NULL if not
// initialized. *out_size receives the arena byte size.
uint8_t* renderer_host_rdram(uint32_t* out_size);

// Present a demo frame. The VI cannot scan out VI_ORIGIN==0 (the demo color FB
// sits at RDRAM offset 0), and a 240-wide source does not map pixel-exact to the
// VI's cropped 240x240 window. So this blits the 240x240 demo color FB
// (RGBA5551 at src_fb_addr, src stride 240 px) into the renderer's 256-wide
// staging FB (at a non-zero VI_ORIGIN), offset +8 columns so the VI's fixed +8
// crop recovers source columns [0,240); then runs the VI. src_w/src_h must be
// 240x240. Use this instead of renderer_host_present() for the live demo.
void renderer_host_present_demo(uint32_t src_fb_addr, uint32_t src_w,
                                uint32_t src_h);

// Access the most recent captured scanout (RGBA8888, byte order R,G,B,A —
// matches SDL_PIXELFORMAT_RGBA32). Returns NULL until a valid frame has been
// presented. *out_w / *out_h / *out_pitch_px receive width/height/row-stride in
// pixels.
const uint32_t* renderer_host_scanout(uint32_t* out_w, uint32_t* out_h,
                                      uint32_t* out_pitch_px);

// Host-tracked pipeline-crash latch state for the UI. NOTE: rdp_core's real
// latch (rdp_pipeline_crashed) is file-static with no public accessor, so this
// reflects only host-observable signals; renderer_host_reset() clears both.
int renderer_host_crash_latched(void);

#ifdef __cplusplus
}  // extern "C"
#endif
