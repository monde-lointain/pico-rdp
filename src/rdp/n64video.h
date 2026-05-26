#pragma once

// Adapted from the CANONICAL Angrylion fork parallel-rdp embeds:
//   parallel-rdp/angrylion-rdp-plus @ 31bdb1f (src/core/n64video.h).
// Config struct / register + config enums preserved VERBATIM from 31bdb1f so
// the init path, DP_COMPAT_HIGH / VI_MODE_NORMAL / VI_INTERP_LINEAR, and the
// buffer sizes match the oracle byte-for-byte. The only deviation: exported
// entry points are renamed to the rdpx_* C ABI to avoid symbol collision with
// the Angrylion oracle linked alongside in the conformance harness.

#include <stdbool.h>
#include <stdint.h>

#define RDRAM_MAX_SIZE 0x800000

// register enums
enum DpRegister {
  DP_START,
  DP_END,
  DP_CURRENT,
  DP_STATUS,
  DP_CLOCK,
  DP_BUFBUSY,
  DP_PIPEBUSY,
  DP_TMEM,
  DP_NUM_REG
};

enum ViRegister {
  VI_STATUS,  // aka VI_CONTROL
  VI_ORIGIN,  // aka VI_DRAM_ADDR
  VI_WIDTH,
  VI_INTR,
  VI_V_CURRENT_LINE,
  VI_TIMING,
  VI_V_SYNC,
  VI_H_SYNC,
  VI_LEAP,     // aka VI_H_SYNC_LEAP
  VI_H_START,  // aka VI_H_VIDEO
  VI_V_START,  // aka VI_V_VIDEO
  VI_V_BURST,
  VI_X_SCALE,
  VI_Y_SCALE,
  VI_NUM_REG
};

// config enums
enum ViMode {
  VI_MODE_NORMAL,    // color buffer with VI filter
  VI_MODE_COLOR,     // direct color buffer, unfiltered
  VI_MODE_DEPTH,     // depth buffer as grayscale
  VI_MODE_COVERAGE,  // coverage as grayscale
  VI_MODE_NUM
};

enum ViInterp { VI_INTERP_NEAREST, VI_INTERP_LINEAR, VI_INTERP_NUM };

enum DpCompatProfile {
  DP_COMPAT_LOW,
  DP_COMPAT_MEDIUM,
  DP_COMPAT_HIGH,
  DP_COMPAT_NUM
};

struct N64videoPixel {
  uint8_t r;
  uint8_t g;
  uint8_t b;
  uint8_t a;
};

struct N64videoFrameBuffer {
  struct N64videoPixel* pixels;
  uint32_t width;
  uint32_t height;
  uint32_t height_out;
  uint32_t pitch;
  bool valid;
};

struct N64videoConfig {
  struct {
    uint8_t* rdram;            // RDRAM pointer
    uint32_t rdram_size;       // size of RDRAM, typically 4 or 8 MiB
    uint8_t* dmem;             // RSP data memory pointer
    uint32_t** vi_reg;         // video interface registers
    uint32_t** dp_reg;         // display processor registers
    uint32_t* mi_intr_reg;     // MIPS interface interrupt register
    void (*mi_intr_cb)(void);  // interrupt callback function
  } gfx;
  struct {
    enum ViMode mode;      // output mode
    enum ViInterp interp;  // output interpolation method
    bool widescreen;       // force 16:9 aspect ratio if true
    bool hide_overscan;    // crop to visible area if true
    bool vsync;            // enable vsync if true
    bool exclusive;        // run in exclusive mode when in fullscreen if true
  } vi;
  struct {
    enum DpCompatProfile compat;  // multithreading compatibility mode
  } dp;
  bool parallel;         // use multithreaded renderer if true
  uint32_t num_workers;  // number of rendering workers
};

#ifdef __cplusplus
extern "C" {
#endif

void rdpx_video_config_init(struct N64videoConfig* cfg);
void rdpx_video_init(struct N64videoConfig* cfg);
void rdpx_video_update_screen(struct N64videoFrameBuffer* fb);
void rdpx_video_process_list(void);
void rdpx_video_close(void);

// Returns nonzero if the RDP pipeline-crash latch has tripped (a malformed or
// unsupported command halted processing); cleared by rdpx_video_close/init.
// Behavior-neutral query; always compiled in (front-ends surface the state).
int rdpx_pipeline_crashed(void);

#ifdef RDPX_TESTING
// Test-only accessors (compiled into rdp_core only when RDPX_TESTING is
// defined, i.e. BUILD_TESTS=ON). Behavior-neutral instrumentation reached by
// the test/ conformance/perf harnesses; absent from the production picosystem
// build. rdpx_get_pixel_count: count of pixels committed by the rasterizer
// coverage/ blend write path since the last reset, for the perf HUD.
uint64_t rdpx_get_pixel_count(void);
void rdpx_reset_pixel_count(void);
#endif  // RDPX_TESTING

#ifdef __cplusplus
}
#endif
