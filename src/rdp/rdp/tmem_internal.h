#pragma once

// tmem_internal.h — TMEM fetch/decode exports (tmem.cc).
// get_tmem collides with the oracle (rdpxi_get_tmem); the rest do not.
// De-inlined for cross-TU use (LTO recovers inlining).

#include "rdp_internal.h"

// TMEM accessor macros (used by tmem.cc and tex.cc). They reference `wid` from
// the calling scope, exactly as in the fork's unity build.
#define TMEM16 ((uint16_t*)rdpxi_state[wid].tmem)
#define TC16 ((uint16_t*)rdpxi_state[wid].tmem)
#define TLUT ((uint16_t*)(&rdpxi_state[wid].tmem[0x800]))

uint8_t* rdpxi_get_tmem(void);

void fetch_texel(uint32_t wid, struct Color* color, int s, int t,
                 uint32_t tilenum);
void fetch_texel_quadro(uint32_t wid, struct Color* color0,
                        struct Color* color1, struct Color* color2,
                        struct Color* color3, int s0, int sdiff, int t0,
                        int tdiff, uint32_t tilenum, int unequaluppers);
void fetch_texel_entlut_quadro(uint32_t wid, struct Color* color0,
                               struct Color* color1, struct Color* color2,
                               struct Color* color3, int s0, int sdiff, int t0,
                               int tdiff, uint32_t tilenum, int isupper,
                               int isupperrg);
void fetch_texel_entlut_quadro_nearest(uint32_t wid, struct Color* color0,
                                       struct Color* color1,
                                       struct Color* color2,
                                       struct Color* color3, int s0, int t0,
                                       uint32_t tilenum, int isupper,
                                       int isupperrg);
void get_tmem_idx(uint32_t wid, int s, int t, uint32_t tilenum, uint32_t* idx0,
                  uint32_t* idx1, uint32_t* idx2, uint32_t* idx3,
                  uint32_t* bit3flipped, uint32_t* hibit);
void read_tmem_copy(uint32_t wid, int s, int s1, int s2, int s3, int t,
                    uint32_t tilenum, uint32_t* sortshort, int* hibits,
                    int* lowbits);
void tmem_init_lut(void);
