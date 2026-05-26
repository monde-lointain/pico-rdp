// tcoord.cc — texture-coordinate clamp/shift/wrap + LOD (standalone TU).
// Ported VERBATIM from the fork 31bdb1f. No exports collide with the oracle;
// helpers de-inlined. See tcoord_internal.h.

#include "tcoord_internal.h"

static const int32_t NORM_POINT_TABLE[64] = {
    0x4000, 0x3f04, 0x3e10, 0x3d22, 0x3c3c, 0x3b5d, 0x3a83, 0x39b1,
    0x38e4, 0x381c, 0x375a, 0x369d, 0x35e5, 0x3532, 0x3483, 0x33d9,
    0x3333, 0x3291, 0x31f4, 0x3159, 0x30c3, 0x3030, 0x2fa1, 0x2f15,
    0x2e8c, 0x2e06, 0x2d83, 0x2d03, 0x2c86, 0x2c0b, 0x2b93, 0x2b1e,
    0x2aab, 0x2a3a, 0x29cc, 0x2960, 0x28f6, 0x288e, 0x2828, 0x27c4,
    0x2762, 0x2702, 0x26a4, 0x2648, 0x25ed, 0x2594, 0x253d, 0x24e7,
    0x2492, 0x243f, 0x23ee, 0x239e, 0x234f, 0x2302, 0x22b6, 0x226c,
    0x2222, 0x21da, 0x2193, 0x214d, 0x2108, 0x20c5, 0x2082, 0x2041};

static const int32_t NORM_SLOPE_TABLE[64] = {
    0xf03, 0xf0b, 0xf11, 0xf19, 0xf20, 0xf25, 0xf2d, 0xf32, 0xf37, 0xf3d, 0xf42,
    0xf47, 0xf4c, 0xf50, 0xf55, 0xf59, 0xf5d, 0xf62, 0xf64, 0xf69, 0xf6c, 0xf70,
    0xf73, 0xf76, 0xf79, 0xf7c, 0xf7f, 0xf82, 0xf84, 0xf87, 0xf8a, 0xf8c, 0xf8e,
    0xf91, 0xf93, 0xf95, 0xf97, 0xf99, 0xf9b, 0xf9d, 0xf9f, 0xfa1, 0xfa3, 0xfa4,
    0xfa6, 0xfa8, 0xfa9, 0xfaa, 0xfac, 0xfae, 0xfaf, 0xfb0, 0xfb2, 0xfb3, 0xfb5,
    0xfb5, 0xfb7, 0xfb8, 0xfb9, 0xfba, 0xfbc, 0xfbc, 0xfbe, 0xfbe};

static void tcdiv_persp(int32_t ss, int32_t st, int32_t sw, int32_t* sss,
                        int32_t* sst);
static void tcdiv_nopersp(int32_t ss, int32_t st, int32_t sw, int32_t* sss,
                          int32_t* sst);

void (*tcdiv_func[2])(int32_t, int32_t, int32_t, int32_t*, int32_t*) = {
    tcdiv_nopersp, tcdiv_persp};

int32_t maskbits_table[16];
static int32_t log2table[256];
static int32_t tcdiv_table[0x8000];

static STRICTINLINE void tcmask_copy(uint32_t wid, int32_t* s, int32_t* s1,
                                     int32_t* s2, int32_t* s3, int32_t* t,
                                     int32_t num) {
  int32_t wrap;
  int32_t maskbits_s;
  int32_t swrapthreshold;

  if (rdpxi_state[wid].tile[num].mask_s) {
    if (rdpxi_state[wid].tile[num].ms) {
      swrapthreshold = rdpxi_state[wid].tile[num].f.masksclamped;

      wrap = (*s >> swrapthreshold) & 1;
      *s ^= (-wrap);

      wrap = (*s1 >> swrapthreshold) & 1;
      *s1 ^= (-wrap);

      wrap = (*s2 >> swrapthreshold) & 1;
      *s2 ^= (-wrap);

      wrap = (*s3 >> swrapthreshold) & 1;
      *s3 ^= (-wrap);
    }

    maskbits_s = maskbits_table[rdpxi_state[wid].tile[num].mask_s];
    *s &= maskbits_s;
    *s1 &= maskbits_s;
    *s2 &= maskbits_s;
    *s3 &= maskbits_s;
  }

  if (rdpxi_state[wid].tile[num].mask_t) {
    if (rdpxi_state[wid].tile[num].mt) {
      wrap = *t >> rdpxi_state[wid].tile[num].f.masktclamped;
      wrap &= 1;
      *t ^= (-wrap);
    }

    *t &= maskbits_table[rdpxi_state[wid].tile[num].mask_t];
  }
}

void tcshift_cycle(uint32_t wid, int32_t* s, int32_t* t, int32_t* maxs,
                   int32_t* maxt, uint32_t num) {
  int32_t coord = *s;
  int32_t shifter = rdpxi_state[wid].tile[num].shift_s;

  if (shifter < 11) {
    coord = SIGN16(coord);
    coord >>= shifter;
  } else {
    coord <<= (16 - shifter);
    coord = SIGN16(coord);
  }
  *s = coord;

  *maxs = ((coord >> 3) >= rdpxi_state[wid].tile[num].sh);

  coord = *t;
  shifter = rdpxi_state[wid].tile[num].shift_t;

  if (shifter < 11) {
    coord = SIGN16(coord);
    coord >>= shifter;
  } else {
    coord <<= (16 - shifter);
    coord = SIGN16(coord);
  }
  *t = coord;
  *maxt = ((coord >> 3) >= rdpxi_state[wid].tile[num].th);
}

void tcclamp_cycle(uint32_t wid, int32_t* s, int32_t* t, int32_t* sfrac,
                   int32_t* tfrac, int32_t maxs, int32_t maxt, int32_t num) {
  int32_t const locs = *s;
  int32_t const loct = *t;
  if (rdpxi_state[wid].tile[num].f.clampens) {
    if (maxs) {
      *s = rdpxi_state[wid].tile[num].f.clampdiffs;
      *sfrac = 0;
    } else if (!(locs & 0x10000)) {
      *s = locs >> 5;
    } else {
      *s = 0;
      *sfrac = 0;
    }
  } else {
    *s = (locs >> 5);
  }

  if (rdpxi_state[wid].tile[num].f.clampent) {
    if (maxt) {
      *t = rdpxi_state[wid].tile[num].f.clampdifft;
      *tfrac = 0;
    } else if (!(loct & 0x10000)) {
      *t = loct >> 5;
    } else {
      *t = 0;
      *tfrac = 0;
    }
  } else {
    *t = (loct >> 5);
  }
}

void tcclamp_cycle_light(uint32_t wid, int32_t* s, int32_t* t, int32_t maxs,
                         int32_t maxt, int32_t num) {
  int32_t const locs = *s;
  int32_t const loct = *t;
  if (rdpxi_state[wid].tile[num].f.clampens) {
    if (maxs) {
      *s = rdpxi_state[wid].tile[num].f.clampdiffs;
    } else if (!(locs & 0x10000)) {
      *s = locs >> 5;
    } else {
      *s = 0;
    }
  } else {
    *s = (locs >> 5);
  }

  if (rdpxi_state[wid].tile[num].f.clampent) {
    if (maxt) {
      *t = rdpxi_state[wid].tile[num].f.clampdifft;
    } else if (!(loct & 0x10000)) {
      *t = loct >> 5;
    } else {
      *t = 0;
    }
  } else {
    *t = (loct >> 5);
  }
}

static STRICTINLINE void tcshift_copy(uint32_t wid, int32_t* s, int32_t* t,
                                      uint32_t num) {
  int32_t coord = *s;
  int32_t shifter = rdpxi_state[wid].tile[num].shift_s;

  if (shifter < 11) {
    coord = SIGN16(coord);
    coord >>= shifter;
  } else {
    coord <<= (16 - shifter);
    coord = SIGN16(coord);
  }
  *s = coord;

  coord = *t;
  shifter = rdpxi_state[wid].tile[num].shift_t;

  if (shifter < 11) {
    coord = SIGN16(coord);
    coord >>= shifter;
  } else {
    coord <<= (16 - shifter);
    coord = SIGN16(coord);
  }
  *t = coord;
}

static STRICTINLINE void tclod_4x17_to_15(int32_t scurr, int32_t snext,
                                          int32_t tcurr, int32_t tnext,
                                          int32_t previous, int32_t* lod) {
  int dels = SIGN(snext, 17) - SIGN(scurr, 17);
  if (dels & 0x20000) {
    dels = ~dels & 0x1ffff;
  }
  int delt = SIGN(tnext, 17) - SIGN(tcurr, 17);
  if (delt & 0x20000) {
    delt = ~delt & 0x1ffff;
  }

  dels = (dels > delt) ? dels : delt;
  dels = (previous > dels) ? previous : dels;
  *lod = dels & 0x7fff;
  if (dels & 0x1c000) {
    *lod |= 0x4000;
  }
}

static STRICTINLINE void tclod_tcclamp(int32_t* sss, int32_t* sst) {
  int32_t tempanded;
  int32_t const temps = *sss;
  int32_t const tempt = *sst;

  if (!(temps & 0x40000)) {
    if (!(temps & 0x20000)) {
      tempanded = temps & 0x18000;
      if (tempanded != 0x8000) {
        if (tempanded != 0x10000) {
          *sss &= 0xffff;
        } else {
          *sss = 0x8000;
        }
      } else {
        *sss = 0x7fff;
      }
    } else {
      *sss = 0x8000;
    }
  } else {
    *sss = 0x7fff;
  }

  if (!(tempt & 0x40000)) {
    if (!(tempt & 0x20000)) {
      tempanded = tempt & 0x18000;
      if (tempanded != 0x8000) {
        if (tempanded != 0x10000) {
          *sst &= 0xffff;
        } else {
          *sst = 0x8000;
        }
      } else {
        *sst = 0x7fff;
      }
    } else {
      *sst = 0x8000;
    }
  } else {
    *sst = 0x7fff;
  }
}

static STRICTINLINE void lodfrac_lodtile_signals(uint32_t wid, int lodclamp,
                                                 int32_t lod, uint32_t* l_tile,
                                                 uint32_t* magnify,
                                                 uint32_t* distant,
                                                 int32_t* lfdst) {
  uint32_t ltil;
  uint32_t dis;
  uint32_t mag;
  int32_t lf;

  if ((lod & 0x4000) || lodclamp) {
    mag = 0;
    ltil = 0;
    dis = 1;
    lf = 0xff;
  } else if (lod < rdpxi_state[wid].min_level) {
    mag = 1;
    ltil = 0;
    dis = rdpxi_state[wid].max_level == 0;

    if (!rdpxi_state[wid].other_modes.sharpen_tex_en &&
        !rdpxi_state[wid].other_modes.detail_tex_en) {
      if (dis) {
        lf = 0xff;
      } else {
        lf = 0;
      }
    } else {
      lf = rdpxi_state[wid].min_level << 3;
      if (rdpxi_state[wid].other_modes.sharpen_tex_en) {
        lf |= 0x100;
      }
    }
  } else if (lod < 32) {
    mag = 1;
    ltil = 0;
    dis = rdpxi_state[wid].max_level == 0;

    if (!rdpxi_state[wid].other_modes.sharpen_tex_en &&
        !rdpxi_state[wid].other_modes.detail_tex_en) {
      if (dis) {
        lf = 0xff;
      } else {
        lf = 0;
      }
    } else {
      lf = lod << 3;
      if (rdpxi_state[wid].other_modes.sharpen_tex_en) {
        lf |= 0x100;
      }
    }
  } else {
    mag = 0;
    ltil = log2table[(lod >> 5) & 0xff];

    if (rdpxi_state[wid].max_level) {
      dis = ((lod & 0x6000) || (ltil >= rdpxi_state[wid].max_level)) != 0;
    } else {
      dis = 1;
    }

    if (!rdpxi_state[wid].other_modes.sharpen_tex_en &&
        !rdpxi_state[wid].other_modes.detail_tex_en && dis) {
      lf = 0xff;
    } else {
      lf = ((lod << 3) >> ltil) & 0xff;
    }
  }

  *distant = dis;
  *l_tile = ltil;
  *magnify = mag;
  *lfdst = lf;
}

void tclod_2cycle(uint32_t wid, int32_t* sss, int32_t* sst, int32_t s,
                  int32_t t, int32_t w, int32_t dsinc, int32_t dtinc,
                  int32_t dwinc, int32_t prim_tile, int32_t* t1, int32_t* t2,
                  int32_t* lf) {
  int32_t nextys;
  int32_t nextyt;
  int32_t nextysw;
  int32_t nexts;
  int32_t nextt;
  int32_t nextsw;
  int lodclamp = 0;
  int32_t lod = 0;
  uint32_t l_tile;
  uint32_t magnify = 0;
  uint32_t distant = 0;
  int const inits = *sss;
  int const initt = *sst;

  tclod_tcclamp(sss, sst);

  if (rdpxi_state[wid].other_modes.f.dolod) {
    nextsw = (w + dwinc) >> 16;
    nexts = (s + dsinc) >> 16;
    nextt = (t + dtinc) >> 16;
    nextys = (s + rdpxi_state[wid].spans_dsdy) >> 16;
    nextyt = (t + rdpxi_state[wid].spans_dtdy) >> 16;
    nextysw = (w + rdpxi_state[wid].spans_dwdy) >> 16;

    rdpxi_state[wid].tcdiv_ptr(nexts, nextt, nextsw, &nexts, &nextt);
    rdpxi_state[wid].tcdiv_ptr(nextys, nextyt, nextysw, &nextys, &nextyt);

    lodclamp = (initt & 0x60000) || (nextt & 0x60000) || (inits & 0x60000) ||
               (nexts & 0x60000) || (nextys & 0x60000) || (nextyt & 0x60000);

    if (!lodclamp) {
      tclod_4x17_to_15(inits, nexts, initt, nextt, 0, &lod);
      tclod_4x17_to_15(inits, nextys, initt, nextyt, lod, &lod);
    }

    lodfrac_lodtile_signals(wid, lodclamp, lod, &l_tile, &magnify, &distant,
                            lf);

    if (rdpxi_state[wid].other_modes.tex_lod_en) {
      if (distant) {
        l_tile = rdpxi_state[wid].max_level;
      }
      if (!rdpxi_state[wid].other_modes.detail_tex_en) {
        *t1 = (int32_t)((prim_tile + l_tile) & 7);
        if (!distant &&
            (rdpxi_state[wid].other_modes.sharpen_tex_en || !magnify)) {
          *t2 = (*t1 + 1) & 7;
        } else {
          *t2 = *t1;
        }
      } else {
        if (!magnify) {
          *t1 = (int32_t)(prim_tile + l_tile + 1);
        } else {
          *t1 = (int32_t)(prim_tile + l_tile);
        }
        *t1 &= 7;
        if (!distant && !magnify) {
          *t2 = (int32_t)((prim_tile + l_tile + 2) & 7);
        } else {
          *t2 = (int32_t)((prim_tile + l_tile + 1) & 7);
        }
      }
    }
  }
}

void tclod_2cycle_next(uint32_t wid, int32_t* sss, int32_t* sst, int32_t* sss2,
                       int32_t* sst2, int32_t /*s*/, int32_t /*t*/,
                       int32_t /*w*/, int32_t dsinc, int32_t dtinc,
                       int32_t dwinc, int32_t prim_tile, int32_t* t1,
                       int32_t* t2, int32_t* lf, int scanline) {
  int32_t nextys;
  int32_t nextyt;
  int32_t nextysw;
  int32_t nexts;
  int32_t nextt;
  int32_t nextsw;
  int lodclamp = 0;
  int32_t lod = 0;
  uint32_t l_tile;
  uint32_t magnify = 0;
  uint32_t distant = 0;

  int const inits2 = *sss2;
  int const initt2 = *sst2;
  int32_t dummy_lf;

  tclod_tcclamp(sss, sst);
  tclod_tcclamp(sss2, sst2);

  if (rdpxi_state[wid].other_modes.f.dolod) {
    int const nextscan = scanline + 1;

    nextys =
        (rdpxi_state[wid].span[nextscan].s + rdpxi_state[wid].spans_dsdy) >> 16;
    nextyt =
        (rdpxi_state[wid].span[nextscan].t + rdpxi_state[wid].spans_dtdy) >> 16;
    nextysw =
        (rdpxi_state[wid].span[nextscan].w + rdpxi_state[wid].spans_dwdy) >> 16;

    rdpxi_state[wid].tcdiv_ptr(nextys, nextyt, nextysw, &nextys, &nextyt);

    lodclamp = ((initt2 & 0x60000) || (inits2 & 0x60000) ||
                (nextys & 0x60000) || (nextyt & 0x60000));

    if (!lodclamp) {
      tclod_4x17_to_15(inits2, nextys, initt2, nextyt, 0, &lod);
    }

    lodfrac_lodtile_signals(wid, lodclamp, lod, &l_tile, &magnify, &distant,
                            lf);

    if (rdpxi_state[wid].other_modes.tex_lod_en) {
      if (distant) {
        l_tile = rdpxi_state[wid].max_level;
      }
      if (!rdpxi_state[wid].other_modes.detail_tex_en) {
        *t1 = (int32_t)((prim_tile + l_tile) & 7);
      } else {
        if (!magnify) {
          *t1 = (int32_t)(prim_tile + l_tile + 1);
        } else {
          *t1 = (int32_t)(prim_tile + l_tile);
        }
        *t1 &= 7;
      }

      nexts = (rdpxi_state[wid].span[nextscan].s + dsinc) >> 16;
      nextt = (rdpxi_state[wid].span[nextscan].t + dtinc) >> 16;
      nextsw = (rdpxi_state[wid].span[nextscan].w + dwinc) >> 16;

      rdpxi_state[wid].tcdiv_ptr(nexts, nextt, nextsw, &nexts, &nextt);

      lodclamp = (lodclamp || (nextt & 0x60000) || (nexts & 0x60000));

      if (!lodclamp) {
        tclod_4x17_to_15(inits2, nexts, initt2, nextt, lod, &lod);
      }

      lodfrac_lodtile_signals(wid, lodclamp, lod, &l_tile, &magnify, &distant,
                              &dummy_lf);

      if (distant) {
        l_tile = rdpxi_state[wid].max_level;
      }
      if (!rdpxi_state[wid].other_modes.detail_tex_en) {
        *t2 = (int32_t)((prim_tile + l_tile) & 7);
      } else {
        if (!magnify) {
          *t2 = (int32_t)(prim_tile + l_tile + 1);
        } else {
          *t2 = (int32_t)(prim_tile + l_tile);
        }
        *t2 &= 7;
      }
    }
  }
}

void tclod_2cycle_notexel1(uint32_t wid, int32_t* sss, int32_t* sst, int32_t s,
                           int32_t t, int32_t w, int32_t dsinc, int32_t dtinc,
                           int32_t dwinc, int32_t prim_tile, int32_t* t1) {
  int32_t nextys;
  int32_t nextyt;
  int32_t nextysw;
  int32_t nexts;
  int32_t nextt;
  int32_t nextsw;
  int lodclamp = 0;
  int32_t lod = 0;
  uint32_t l_tile;
  uint32_t magnify = 0;
  uint32_t distant = 0;
  int const inits = *sss;
  int const initt = *sst;

  tclod_tcclamp(sss, sst);

  if (rdpxi_state[wid].other_modes.f.dolod) {
    nextsw = (w + dwinc) >> 16;
    nexts = (s + dsinc) >> 16;
    nextt = (t + dtinc) >> 16;
    nextys = (s + rdpxi_state[wid].spans_dsdy) >> 16;
    nextyt = (t + rdpxi_state[wid].spans_dtdy) >> 16;
    nextysw = (w + rdpxi_state[wid].spans_dwdy) >> 16;

    rdpxi_state[wid].tcdiv_ptr(nexts, nextt, nextsw, &nexts, &nextt);
    rdpxi_state[wid].tcdiv_ptr(nextys, nextyt, nextysw, &nextys, &nextyt);

    lodclamp = (initt & 0x60000) || (nextt & 0x60000) || (inits & 0x60000) ||
               (nexts & 0x60000) || (nextys & 0x60000) || (nextyt & 0x60000);

    if (!lodclamp) {
      tclod_4x17_to_15(inits, nexts, initt, nextt, 0, &lod);
      tclod_4x17_to_15(inits, nextys, initt, nextyt, lod, &lod);
    }

    lodfrac_lodtile_signals(wid, lodclamp, lod, &l_tile, &magnify, &distant,
                            &rdpxi_state[wid].lod_frac);

    if (rdpxi_state[wid].other_modes.tex_lod_en) {
      if (distant) {
        l_tile = rdpxi_state[wid].max_level;
      }
      if (!rdpxi_state[wid].other_modes.detail_tex_en || magnify) {
        *t1 = (int32_t)((prim_tile + l_tile) & 7);
      } else {
        *t1 = (int32_t)((prim_tile + l_tile + 1) & 7);
      }
    }
  }
}

void tclod_1cycle_current(uint32_t wid, int32_t* sss, int32_t* sst,
                          int32_t nexts, int32_t nextt, int32_t s, int32_t t,
                          int32_t w, int32_t dsinc, int32_t dtinc,
                          int32_t dwinc, int32_t scanline, int32_t prim_tile,
                          int32_t* t1, struct Spansigs* sigs) {
  int32_t fars;
  int32_t fart;
  int32_t farsw;
  int lodclamp = 0;
  int32_t lod = 0;
  uint32_t l_tile = 0;
  uint32_t magnify = 0;
  uint32_t distant = 0;

  tclod_tcclamp(sss, sst);

  if (rdpxi_state[wid].other_modes.f.dolod) {
    int const nextscan = scanline + 1;

    if (rdpxi_state[wid].span[nextscan].validline) {
      if (!sigs->endspan || !sigs->longspan) {
        if (!(sigs->preendspan && sigs->longspan) &&
            !(sigs->endspan && sigs->midspan)) {
          farsw = (w + (dwinc << 1)) >> 16;
          fars = (s + (dsinc << 1)) >> 16;
          fart = (t + (dtinc << 1)) >> 16;
        } else {
          farsw = (w - dwinc) >> 16;
          fars = (s - dsinc) >> 16;
          fart = (t - dtinc) >> 16;
        }
      } else {
        fart = (rdpxi_state[wid].span[nextscan].t + dtinc) >> 16;
        fars = (rdpxi_state[wid].span[nextscan].s + dsinc) >> 16;
        farsw = (rdpxi_state[wid].span[nextscan].w + dwinc) >> 16;
      }
    } else {
      farsw = (w + (dwinc << 1)) >> 16;
      fars = (s + (dsinc << 1)) >> 16;
      fart = (t + (dtinc << 1)) >> 16;
    }

    rdpxi_state[wid].tcdiv_ptr(fars, fart, farsw, &fars, &fart);

    lodclamp = (fart & 0x60000) || (nextt & 0x60000) || (fars & 0x60000) ||
               (nexts & 0x60000);

    if (!lodclamp) {
      // NOLINTNEXTLINE(readability-suspicious-call-argument)
      tclod_4x17_to_15(nexts, fars, nextt, fart, 0, &lod);
    }

    lodfrac_lodtile_signals(wid, lodclamp, lod, &l_tile, &magnify, &distant,
                            &rdpxi_state[wid].lod_frac);

    if (rdpxi_state[wid].other_modes.tex_lod_en) {
      if (distant) {
        l_tile = rdpxi_state[wid].max_level;
      }

      if (!rdpxi_state[wid].other_modes.detail_tex_en || magnify) {
        *t1 = (int32_t)((prim_tile + l_tile) & 7);
      } else {
        *t1 = (int32_t)((prim_tile + l_tile + 1) & 7);
      }
    }
  }
}

void tclod_1cycle_current_simple(uint32_t wid, int32_t* sss, int32_t* sst,
                                 int32_t s, int32_t t, int32_t w, int32_t dsinc,
                                 int32_t dtinc, int32_t dwinc, int32_t scanline,
                                 int32_t prim_tile, int32_t* t1,
                                 struct Spansigs* sigs) {
  int32_t fars;
  int32_t fart;
  int32_t farsw;
  int32_t nexts;
  int32_t nextt;
  int32_t nextsw;
  int lodclamp = 0;
  int32_t lod = 0;
  uint32_t l_tile = 0;
  uint32_t magnify = 0;
  uint32_t distant = 0;

  tclod_tcclamp(sss, sst);

  if (rdpxi_state[wid].other_modes.f.dolod) {
    int const nextscan = scanline + 1;
    if (rdpxi_state[wid].span[nextscan].validline) {
      if (!sigs->endspan || !sigs->longspan) {
        nextsw = (w + dwinc) >> 16;
        nexts = (s + dsinc) >> 16;
        nextt = (t + dtinc) >> 16;

        if (!(sigs->preendspan && sigs->longspan) &&
            !(sigs->endspan && sigs->midspan)) {
          farsw = (w + (dwinc << 1)) >> 16;
          fars = (s + (dsinc << 1)) >> 16;
          fart = (t + (dtinc << 1)) >> 16;
        } else {
          farsw = (w - dwinc) >> 16;
          fars = (s - dsinc) >> 16;
          fart = (t - dtinc) >> 16;
        }
      } else {
        nextt = rdpxi_state[wid].span[nextscan].t >> 16;
        nexts = rdpxi_state[wid].span[nextscan].s >> 16;
        nextsw = rdpxi_state[wid].span[nextscan].w >> 16;
        fart = (rdpxi_state[wid].span[nextscan].t + dtinc) >> 16;
        fars = (rdpxi_state[wid].span[nextscan].s + dsinc) >> 16;
        farsw = (rdpxi_state[wid].span[nextscan].w + dwinc) >> 16;
      }
    } else {
      nextsw = (w + dwinc) >> 16;
      nexts = (s + dsinc) >> 16;
      nextt = (t + dtinc) >> 16;
      farsw = (w + (dwinc << 1)) >> 16;
      fars = (s + (dsinc << 1)) >> 16;
      fart = (t + (dtinc << 1)) >> 16;
    }

    rdpxi_state[wid].tcdiv_ptr(nexts, nextt, nextsw, &nexts, &nextt);
    rdpxi_state[wid].tcdiv_ptr(fars, fart, farsw, &fars, &fart);

    lodclamp = (fart & 0x60000) || (nextt & 0x60000) || (fars & 0x60000) ||
               (nexts & 0x60000);

    if (!lodclamp) {
      // NOLINTNEXTLINE(readability-suspicious-call-argument)
      tclod_4x17_to_15(nexts, fars, nextt, fart, 0, &lod);
    }

    lodfrac_lodtile_signals(wid, lodclamp, lod, &l_tile, &magnify, &distant,
                            &rdpxi_state[wid].lod_frac);

    if (rdpxi_state[wid].other_modes.tex_lod_en) {
      if (distant) {
        l_tile = rdpxi_state[wid].max_level;
      }
      if (!rdpxi_state[wid].other_modes.detail_tex_en || magnify) {
        *t1 = (int32_t)((prim_tile + l_tile) & 7);
      } else {
        *t1 = (int32_t)((prim_tile + l_tile + 1) & 7);
      }
    }
  }
}

void tclod_1cycle_next(uint32_t wid, int32_t* sss, int32_t* sst, int32_t s,
                       int32_t t, int32_t w, int32_t dsinc, int32_t dtinc,
                       int32_t dwinc, int32_t scanline, int32_t prim_tile,
                       int32_t* t1, struct Spansigs* sigs,
                       int32_t* prelodfrac) {
  int32_t nexts;
  int32_t nextt;
  int32_t nextsw;
  int32_t fars;
  int32_t fart;
  int32_t farsw;
  int lodclamp = 0;
  int32_t lod = 0;
  uint32_t l_tile = 0;
  uint32_t magnify = 0;
  uint32_t distant = 0;

  tclod_tcclamp(sss, sst);

  if (rdpxi_state[wid].other_modes.f.dolod) {
    int const nextscan = scanline + 1;

    if (rdpxi_state[wid].span[nextscan].validline) {
      if (!sigs->nextspan) {
        if (!sigs->endspan || !sigs->longspan) {
          nextsw = (w + dwinc) >> 16;
          nexts = (s + dsinc) >> 16;
          nextt = (t + dtinc) >> 16;

          if (!(sigs->preendspan && sigs->longspan) &&
              !(sigs->endspan && sigs->midspan)) {
            farsw = (w + (dwinc << 1)) >> 16;
            fars = (s + (dsinc << 1)) >> 16;
            fart = (t + (dtinc << 1)) >> 16;
          } else {
            farsw = (w - dwinc) >> 16;
            fars = (s - dsinc) >> 16;
            fart = (t - dtinc) >> 16;
          }
        } else {
          nextt = rdpxi_state[wid].span[nextscan].t;
          nexts = rdpxi_state[wid].span[nextscan].s;
          nextsw = rdpxi_state[wid].span[nextscan].w;
          fart = (nextt + dtinc) >> 16;
          fars = (nexts + dsinc) >> 16;
          farsw = (nextsw + dwinc) >> 16;
          nextt >>= 16;
          nexts >>= 16;
          nextsw >>= 16;
        }
      } else {
        if (sigs->longspan) {
          nextt = (rdpxi_state[wid].span[nextscan].t + dtinc) >> 16;
          nexts = (rdpxi_state[wid].span[nextscan].s + dsinc) >> 16;
          nextsw = (rdpxi_state[wid].span[nextscan].w + dwinc) >> 16;
          fart = (rdpxi_state[wid].span[nextscan].t + (dtinc << 1)) >> 16;
          fars = (rdpxi_state[wid].span[nextscan].s + (dsinc << 1)) >> 16;
          farsw = (rdpxi_state[wid].span[nextscan].w + (dwinc << 1)) >> 16;
        } else if (sigs->midspan) {
          nextt = rdpxi_state[wid].span[nextscan].t >> 16;
          nexts = rdpxi_state[wid].span[nextscan].s >> 16;
          nextsw = rdpxi_state[wid].span[nextscan].w >> 16;
          fart = (rdpxi_state[wid].span[nextscan].t + dtinc) >> 16;
          fars = (rdpxi_state[wid].span[nextscan].s + dsinc) >> 16;
          farsw = (rdpxi_state[wid].span[nextscan].w + dwinc) >> 16;
        } else if (sigs->onelessthanmid) {
          nextsw = (w + dwinc) >> 16;
          nexts = (s + dsinc) >> 16;
          nextt = (t + dtinc) >> 16;
          farsw = (w - dwinc) >> 16;
          fars = (s - dsinc) >> 16;
          fart = (t - dtinc) >> 16;
        } else {
          nextt = (t + dtinc) >> 16;
          nexts = (s + dsinc) >> 16;
          nextsw = (w + dwinc) >> 16;
          fart = (t + (dtinc << 1)) >> 16;
          fars = (s + (dsinc << 1)) >> 16;
          farsw = (w + (dwinc << 1)) >> 16;
        }
      }
    } else {
      nextsw = (w + dwinc) >> 16;
      nexts = (s + dsinc) >> 16;
      nextt = (t + dtinc) >> 16;
      farsw = (w + (dwinc << 1)) >> 16;
      fars = (s + (dsinc << 1)) >> 16;
      fart = (t + (dtinc << 1)) >> 16;
    }

    rdpxi_state[wid].tcdiv_ptr(nexts, nextt, nextsw, &nexts, &nextt);
    rdpxi_state[wid].tcdiv_ptr(fars, fart, farsw, &fars, &fart);

    lodclamp = (fart & 0x60000) || (nextt & 0x60000) || (fars & 0x60000) ||
               (nexts & 0x60000);

    if (!lodclamp) {
      // NOLINTNEXTLINE(readability-suspicious-call-argument)
      tclod_4x17_to_15(nexts, fars, nextt, fart, 0, &lod);
    }

    lodfrac_lodtile_signals(wid, lodclamp, lod, &l_tile, &magnify, &distant,
                            prelodfrac);

    if (rdpxi_state[wid].other_modes.tex_lod_en) {
      if (distant) {
        l_tile = rdpxi_state[wid].max_level;
      }
      if (!rdpxi_state[wid].other_modes.detail_tex_en || magnify) {
        *t1 = (int32_t)((prim_tile + l_tile) & 7);
      } else {
        *t1 = (int32_t)((prim_tile + l_tile + 1) & 7);
      }
    }
  }
}

void tclod_copy(uint32_t wid, int32_t* sss, int32_t* sst, int32_t s, int32_t t,
                int32_t w, int32_t dsinc, int32_t dtinc, int32_t dwinc,
                int32_t prim_tile, int32_t* t1) {
  int32_t nexts;
  int32_t nextt;
  int32_t nextsw;
  int32_t fars;
  int32_t fart;
  int32_t farsw;
  int lodclamp = 0;
  int32_t lod = 0;
  uint32_t l_tile = 0;
  uint32_t magnify = 0;
  uint32_t distant = 0;

  tclod_tcclamp(sss, sst);

  if (rdpxi_state[wid].other_modes.tex_lod_en) {
    nextsw = (w + dwinc) >> 16;
    nexts = (s + dsinc) >> 16;
    nextt = (t + dtinc) >> 16;
    farsw = (w + (dwinc << 1)) >> 16;
    fars = (s + (dsinc << 1)) >> 16;
    fart = (t + (dtinc << 1)) >> 16;

    rdpxi_state[wid].tcdiv_ptr(nexts, nextt, nextsw, &nexts, &nextt);
    rdpxi_state[wid].tcdiv_ptr(fars, fart, farsw, &fars, &fart);

    lodclamp = (fart & 0x60000) || (nextt & 0x60000) || (fars & 0x60000) ||
               (nexts & 0x60000);

    if (!lodclamp) {
      // NOLINTNEXTLINE(readability-suspicious-call-argument)
      tclod_4x17_to_15(nexts, fars, nextt, fart, 0, &lod);
    }

    if ((lod & 0x4000) || lodclamp) {
      magnify = 0;
      l_tile = rdpxi_state[wid].max_level;
    } else if (lod < 32) {
      magnify = 1;
      l_tile = 0;
    } else {
      magnify = 0;
      l_tile = log2table[(lod >> 5) & 0xff];

      if (rdpxi_state[wid].max_level) {
        distant =
            ((lod & 0x6000) || (l_tile >= rdpxi_state[wid].max_level)) != 0;
      } else {
        distant = 1;
      }

      if (distant) {
        l_tile = rdpxi_state[wid].max_level;
      }
    }

    if (!rdpxi_state[wid].other_modes.detail_tex_en || magnify) {
      *t1 = (int32_t)((prim_tile + l_tile) & 7);
    } else {
      *t1 = (int32_t)((prim_tile + l_tile + 1) & 7);
    }
  }
}

void tc_pipeline_copy(uint32_t wid, int32_t* sss0, int32_t* sss1, int32_t* sss2,
                      int32_t* sss3, int32_t* sst, int tilenum) {
  int32_t ss0 = *sss0;
  int32_t ss1 = 0;
  int32_t ss2 = 0;
  int32_t ss3 = 0;
  int32_t st = *sst;

  tcshift_copy(wid, &ss0, &st, tilenum);

  ss0 = TRELATIVE(ss0, rdpxi_state[wid].tile[tilenum].sl);
  st = TRELATIVE(st, rdpxi_state[wid].tile[tilenum].tl);
  ss0 = (ss0 >> 5);
  st = (st >> 5);

  ss1 = ss0 + 1;
  ss2 = ss0 + 2;
  ss3 = ss0 + 3;

  tcmask_copy(wid, &ss0, &ss1, &ss2, &ss3, &st, tilenum);

  *sss0 = ss0;
  *sss1 = ss1;
  *sss2 = ss2;
  *sss3 = ss3;
  *sst = st;
}

void tc_pipeline_load(uint32_t wid, int32_t* sss, int32_t* sst, int tilenum,
                      int coord_quad) {
  int sss1 = *sss;
  int sst1 = *sst;
  sss1 = SIGN16(sss1);
  sst1 = SIGN16(sst1);

  sss1 = TRELATIVE(sss1, rdpxi_state[wid].tile[tilenum].sl);
  sst1 = TRELATIVE(sst1, rdpxi_state[wid].tile[tilenum].tl);

  if (!coord_quad) {
    sss1 = (sss1 >> 5);
    sst1 = (sst1 >> 5);
  } else {
    sss1 = (sss1 >> 3);
    sst1 = (sst1 >> 3);
  }

  *sss = sss1;
  *sst = sst1;
}

static void tcdiv_nopersp(int32_t ss, int32_t st, int32_t /*sw*/, int32_t* sss,
                          int32_t* sst) {
  *sss = (SIGN16(ss)) & 0x1ffff;
  *sst = (SIGN16(st)) & 0x1ffff;
}

static void tcdiv_persp(int32_t ss, int32_t st, int32_t sw, int32_t* sss,
                        int32_t* sst) {
  int w_carry = 0;
  int shift;
  int tlu_rcp;
  int sprod;
  int tprod;
  int outofbounds_s;
  int outofbounds_t;
  int tempmask;
  int shift_value;
  int32_t temps;
  int32_t tempt;

  int overunder_s = 0;
  int overunder_t = 0;

  if (SIGN16(sw) <= 0) {
    w_carry = 1;
  }

  sw &= 0x7fff;

  shift = tcdiv_table[sw];
  tlu_rcp = shift >> 4;
  shift &= 0xf;

  sprod = SIGN16(ss) * tlu_rcp;
  tprod = SIGN16(st) * tlu_rcp;

  tempmask = ((1 << 30) - 1) & -((1 << 29) >> shift);

  outofbounds_s = sprod & tempmask;
  outofbounds_t = tprod & tempmask;

  if (shift != 0xe) {
    shift_value = 13 - shift;
    temps = sprod = (sprod >> shift_value);
    tempt = tprod = (tprod >> shift_value);
  } else {
    temps = sprod << 1;
    tempt = tprod << 1;
  }

  if (outofbounds_s != tempmask && outofbounds_s != 0) {
    if (!(sprod & (1 << 29))) {
      overunder_s = 2 << 17;
    } else {
      overunder_s = 1 << 17;
    }
  }

  if (outofbounds_t != tempmask && outofbounds_t != 0) {
    if (!(tprod & (1 << 29))) {
      overunder_t = 2 << 17;
    } else {
      overunder_t = 1 << 17;
    }
  }

  if (w_carry) {
    overunder_s |= (2 << 17);
    overunder_t |= (2 << 17);
  }

  *sss = (temps & 0x1ffff) | overunder_s;
  *sst = (tempt & 0x1ffff) | overunder_t;
}

void tcoord_init_lut(void) {
  int i;
  int k;

  log2table[0] = log2table[1] = 0;
  for (i = 2; i < 256; i++) {
    for (k = 7; k > 0; k--) {
      if ((i >> k) & 1) {
        log2table[i] = k;
        break;
      }
    }
  }

  int temppoint;
  int tempslope;
  int normout;
  int wnorm;
  int shift;
  int tlu_rcp;

  for (i = 0; i < 0x8000; i++) {
    for (k = 1; k <= 14 && !((i << k) & 0x8000); k++) {
      ;
    }
    shift = k - 1;
    normout = (i << shift) & 0x3fff;
    wnorm = (normout & 0xff) << 2;
    normout >>= 8;

    temppoint = NORM_POINT_TABLE[normout];
    tempslope = NORM_SLOPE_TABLE[normout];

    tempslope = (tempslope | ~0x3ff) + 1;

    tlu_rcp = (((tempslope * wnorm) >> 10) + temppoint) & 0x7fff;

    tcdiv_table[i] = shift | (tlu_rcp << 4);
  }

  maskbits_table[0] = 0x3ff;
  for (i = 1; i < 16; i++) {
    maskbits_table[i] = ((uint16_t)(0xffff) >> (16 - i)) & 0x3ff;
  }
}

void tcoord_init(uint32_t wid) { rdpxi_state[wid].tcdiv_ptr = tcdiv_func[0]; }
