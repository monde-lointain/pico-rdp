#pragma once

// vi_internal.h — shared VI (video interface) types + cross-TU exports.
//
// The scanout PODs (Rgba/FrameBuffer) and the VI register/enum types were
// defined inline in n64video.c / vi.c in the unity build; extracted here so the
// VI stage TUs (gamma/lerp/divot/video/restore/fetch + vi.cc) can share them.
// vi_gamma_init/vi_restore_init collide with the oracle (rdpxi_-prefixed); the
// filter helpers do not. De-inlined for cross-TU use (LTO recovers inlining).

#include "../rdp/rdp_internal.h"  // RDPX_PARALLEL_MAX_WORKERS, stdint, bool

#ifndef PARALLEL_MAX_WORKERS
#define PARALLEL_MAX_WORKERS RDPX_PARALLEL_MAX_WORKERS
#endif

// anamorphic NTSC / PAL resolution + typical VI_V_SYNC values
#define H_RES_NTSC 640
#define V_RES_NTSC 480
#define H_RES_PAL 768
#define V_RES_PAL 576
#define V_SYNC_NTSC 525
#define V_SYNC_PAL 625

// maximum possible size of the prescale area
#define PRESCALE_WIDTH H_RES_NTSC
#define PRESCALE_HEIGHT V_SYNC_PAL

// scanout pixel + frame (mirror the fork's vdac.h; local PODs, no linkage)
struct Rgba {
  uint8_t r;
  uint8_t g;
  uint8_t b;
  uint8_t a;
};

struct FrameBuffer {
  struct Rgba* pixels;
  uint32_t width;
  uint32_t height;
  uint32_t height_out;
  uint32_t pitch;
};

enum ViType {
  VI_TYPE_BLANK,     // no data, no sync
  VI_TYPE_RESERVED,  // unused, should never be set
  VI_TYPE_RGBA5551,  // 16 bit color (internally 18 bit RGBA5553)
  VI_TYPE_RGBA8888   // 32 bit color
};

enum ViAa {
  VI_AA_RESAMP_EXTRA_ALWAYS,  // resample and AA (always fetch extra lines)
  VI_AA_RESAMP_EXTRA,         // resample and AA (fetch extra lines if needed)
  VI_AA_RESAMP_ONLY,          // only resample (treat as all fully covered)
  VI_AA_REPLICATE             // replicate pixels, no interpolation
};

struct ViRegCtrl {
  uint8_t type;
  bool gamma_dither_enable;
  bool gamma_enable;
  bool divot_enable;
  bool vbus_clock_enable;
  bool serrate;
  bool test_mode;
  uint8_t aa_mode;
  bool reserved;
  bool kill_we;
  uint8_t pixel_advance;
  bool dither_filter_enable;
};

typedef void (*vi_fetch_filter_func)(struct Rgba*, uint32_t, uint32_t,
                                     struct ViRegCtrl, uint32_t, uint32_t);

// ---- cross-TU VI exports --------------------------------------------------
void rdpxi_vi_gamma_init(void);
void rdpxi_vi_restore_init(void);

void gamma_filters(struct Rgba* pixel, bool gamma_enable,
                   bool gamma_dither_enable, uint32_t noise_seed);
void vi_vl_lerp(struct Rgba* up, struct Rgba down, uint32_t frac);
void divot_filter(struct Rgba* final, struct Rgba center, struct Rgba left,
                  struct Rgba right);
void video_filter16(int* endr, int* endg, int* endb, uint32_t fboffset,
                    uint32_t num, uint32_t hres, uint32_t centercvg,
                    uint32_t fetchbugstate);
void video_filter32(int* endr, int* endg, int* endb, uint32_t fboffset,
                    uint32_t num, uint32_t hres, uint32_t centercvg,
                    uint32_t fetchbugstate);
void restore_filter16(int* r, int* g, int* b, uint32_t fboffset, uint32_t num,
                      uint32_t hres, uint32_t fetchbugstate);
void restore_filter32(int* r, int* g, int* b, uint32_t fboffset, uint32_t num,
                      uint32_t hres, uint32_t fetchbugstate);
void vi_fetch_filter16(struct Rgba* res, uint32_t fboffset, uint32_t cur_x,
                       struct ViRegCtrl ctrl, uint32_t hres,
                       uint32_t fetchstate);
void vi_fetch_filter32(struct Rgba* res, uint32_t fboffset, uint32_t cur_x,
                       struct ViRegCtrl ctrl, uint32_t hres,
                       uint32_t fetchstate);

// vi.cc entry points (vi_set_zbuffer_address collides -> rdpxi_).
void vi_init(void);
void vi_update_screen(void);
void vi_close(void);
void rdpxi_vi_set_zbuffer_address(uint32_t address);

// vdac / parallel shims (defined in n64video.cc; vi.cc calls them). They
// collide with the oracle's externally-linked vdac_*/parallel_* -> rdpxi_.
struct N64videoConfig;
void rdpxi_vdac_init(struct N64videoConfig* cfg);
void rdpxi_vdac_write(struct FrameBuffer* fb);
void rdpxi_vdac_sync(bool invalid);
void rdpxi_vdac_close(void);
uint32_t rdpxi_parallel_num_workers(void);
void rdpxi_parallel_run(void (*task)(uint32_t));
