#pragma once

// tcoord_internal.h — texture-coordinate exports (tcoord.cc).
// None collide with the oracle, so names are unprefixed. De-inlined for
// cross-TU use (LTO recovers inlining).

#include "rdp_internal.h"

// Data read cross-TU (oracle keeps these local, so no prefix):
extern void (*tcdiv_func[2])(int32_t, int32_t, int32_t, int32_t*, int32_t*);
extern int32_t maskbits_table[16];

void tcoord_init(uint32_t wid);
void tcoord_init_lut(void);

void tcclamp_cycle(uint32_t wid, int32_t* s, int32_t* t, int32_t* sfrac,
                   int32_t* tfrac, int32_t maxs, int32_t maxt, int32_t num);
void tcclamp_cycle_light(uint32_t wid, int32_t* s, int32_t* t, int32_t maxs,
                         int32_t maxt, int32_t num);
void tcshift_cycle(uint32_t wid, int32_t* s, int32_t* t, int32_t* maxs,
                   int32_t* maxt, uint32_t num);
void tc_pipeline_load(uint32_t wid, int32_t* sss, int32_t* sst, int tilenum,
                      int coord_quad);
void tc_pipeline_copy(uint32_t wid, int32_t* sss0, int32_t* sss1, int32_t* sss2,
                      int32_t* sss3, int32_t* sst, int tilenum);

void tclod_1cycle_current(uint32_t wid, int32_t* sss, int32_t* sst,
                          int32_t nexts, int32_t nextt, int32_t s, int32_t t,
                          int32_t w, int32_t dsinc, int32_t dtinc,
                          int32_t dwinc, int32_t scanline, int32_t prim_tile,
                          int32_t* t1, struct Spansigs* sigs);
void tclod_1cycle_current_simple(uint32_t wid, int32_t* sss, int32_t* sst,
                                 int32_t s, int32_t t, int32_t w, int32_t dsinc,
                                 int32_t dtinc, int32_t dwinc, int32_t scanline,
                                 int32_t prim_tile, int32_t* t1,
                                 struct Spansigs* sigs);
void tclod_1cycle_next(uint32_t wid, int32_t* sss, int32_t* sst, int32_t s,
                       int32_t t, int32_t w, int32_t dsinc, int32_t dtinc,
                       int32_t dwinc, int32_t scanline, int32_t prim_tile,
                       int32_t* t1, struct Spansigs* sigs, int32_t* prelodfrac);
void tclod_2cycle(uint32_t wid, int32_t* sss, int32_t* sst, int32_t s,
                  int32_t t, int32_t w, int32_t dsinc, int32_t dtinc,
                  int32_t dwinc, int32_t prim_tile, int32_t* t1, int32_t* t2,
                  int32_t* lf);
void tclod_2cycle_next(uint32_t wid, int32_t* sss, int32_t* sst, int32_t* sss2,
                       int32_t* sst2, int32_t s, int32_t t, int32_t w,
                       int32_t dsinc, int32_t dtinc, int32_t dwinc,
                       int32_t prim_tile, int32_t* t1, int32_t* t2, int32_t* lf,
                       int scanline);
void tclod_2cycle_notexel1(uint32_t wid, int32_t* sss, int32_t* sst, int32_t s,
                           int32_t t, int32_t w, int32_t dsinc, int32_t dtinc,
                           int32_t dwinc, int32_t prim_tile, int32_t* t1);
void tclod_copy(uint32_t wid, int32_t* sss, int32_t* sst, int32_t s, int32_t t,
                int32_t w, int32_t dsinc, int32_t dtinc, int32_t dwinc,
                int32_t prim_tile, int32_t* t1);
