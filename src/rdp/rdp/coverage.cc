// coverage.cc — pixel coverage compute + cvmask LUT (standalone TU).
// Ported VERBATIM from the fork 31bdb1f. Cross-TU exports (none collide with
// the oracle) are declared in coverage_internal.h; per-pixel helpers
// de-inlined.

#include <string.h>

#include "coverage_internal.h"

#define CVG_CLAMP 0
#define CVG_WRAP 1
#define CVG_ZAP 2
#define CVG_SAVE 3

static struct {
  uint8_t cvg;
  uint8_t cvbit;
  uint8_t xoff;
  uint8_t yoff;
} cvarray[0x100];

static STRICTINLINE uint32_t rightcvghex(uint32_t x, uint32_t fmask) {
  uint32_t covered = ((x & 7) + 1) >> 1;

  covered = 0xf0 >> covered;
  return (covered & fmask);
}

static STRICTINLINE uint32_t leftcvghex(uint32_t x, uint32_t fmask) {
  uint32_t covered = ((x & 7) + 1) >> 1;
  covered = 0xf >> covered;
  return (covered & fmask);
}

void compute_cvg_flip(uint32_t wid, int32_t scanline) {
  int32_t purgestart;
  int32_t purgeend;
  int i;
  int length;
  int fmask;
  int maskshift;
  int fmaskshifted;
  int32_t minorcur;
  int32_t majorcur;
  int32_t minorcurint;
  int32_t majorcurint;
  int32_t samecvg;

  purgestart = rdpxi_state[wid].span[scanline].rx;
  purgeend = rdpxi_state[wid].span[scanline].lx;
  length = purgeend - purgestart;
  if (length >= 0) {
    memset(&rdpxi_state[wid].cvgbuf[purgestart], 0xff, length + 1);
    for (i = 0; i < 4; i++) {
      fmask = 0xa >> (i & 1);

      maskshift = (i - 2) & 4;
      fmaskshifted = fmask << maskshift;

      if (!rdpxi_state[wid].span[scanline].invalyscan[i]) {
        int k;
        minorcur = rdpxi_state[wid].span[scanline].minorx[i];
        majorcur = rdpxi_state[wid].span[scanline].majorx[i];
        minorcurint = minorcur >> 3;
        majorcurint = majorcur >> 3;

        for (k = purgestart; k <= majorcurint; k++) {
          rdpxi_state[wid].cvgbuf[k] &= ~fmaskshifted;
        }
        for (k = minorcurint; k <= purgeend; k++) {
          rdpxi_state[wid].cvgbuf[k] &= ~fmaskshifted;
        }

        if (minorcurint > majorcurint) {
          rdpxi_state[wid].cvgbuf[minorcurint] |=
              (rightcvghex(minorcur, fmask) << maskshift);
          rdpxi_state[wid].cvgbuf[majorcurint] |=
              (leftcvghex(majorcur, fmask) << maskshift);
        } else if (minorcurint == majorcurint) {
          samecvg = (int32_t)(rightcvghex(minorcur, fmask) &
                              leftcvghex(majorcur, fmask));
          rdpxi_state[wid].cvgbuf[majorcurint] |= (samecvg << maskshift);
        }
      } else {
        int k;
        for (k = purgestart; k <= purgeend; k++) {
          rdpxi_state[wid].cvgbuf[k] &= ~fmaskshifted;
        }
      }
    }
  }
}

void compute_cvg_noflip(uint32_t wid, int32_t scanline) {
  int32_t purgestart;
  int32_t purgeend;
  int i;
  int length;
  int fmask;
  int maskshift;
  int fmaskshifted;
  int32_t minorcur;
  int32_t majorcur;
  int32_t minorcurint;
  int32_t majorcurint;
  int32_t samecvg;

  purgestart = rdpxi_state[wid].span[scanline].lx;
  purgeend = rdpxi_state[wid].span[scanline].rx;
  length = purgeend - purgestart;

  if (length >= 0) {
    memset(&rdpxi_state[wid].cvgbuf[purgestart], 0xff, length + 1);

    for (i = 0; i < 4; i++) {
      fmask = 0xa >> (i & 1);
      maskshift = (i - 2) & 4;
      fmaskshifted = fmask << maskshift;

      if (!rdpxi_state[wid].span[scanline].invalyscan[i]) {
        int k;
        minorcur = rdpxi_state[wid].span[scanline].minorx[i];
        majorcur = rdpxi_state[wid].span[scanline].majorx[i];
        minorcurint = minorcur >> 3;
        majorcurint = majorcur >> 3;

        for (k = purgestart; k <= minorcurint; k++) {
          rdpxi_state[wid].cvgbuf[k] &= ~fmaskshifted;
        }
        for (k = majorcurint; k <= purgeend; k++) {
          rdpxi_state[wid].cvgbuf[k] &= ~fmaskshifted;
        }

        if (majorcurint > minorcurint) {
          rdpxi_state[wid].cvgbuf[minorcurint] |=
              (leftcvghex(minorcur, fmask) << maskshift);
          rdpxi_state[wid].cvgbuf[majorcurint] |=
              (rightcvghex(majorcur, fmask) << maskshift);
        } else if (minorcurint == majorcurint) {
          samecvg = (int32_t)(leftcvghex(minorcur, fmask) &
                              rightcvghex(majorcur, fmask));
          rdpxi_state[wid].cvgbuf[majorcurint] |= (samecvg << maskshift);
        }
      } else {
        int k;
        for (k = purgestart; k <= purgeend; k++) {
          rdpxi_state[wid].cvgbuf[k] &= ~fmaskshifted;
        }
      }
    }
  }
}

int finalize_spanalpha(int cvg_dest, uint32_t blend_en, uint32_t curpixel_cvg,
                       uint32_t curpixel_memcvg) {
  int finalcvg = 0;

  switch (cvg_dest) {
    case CVG_CLAMP:
      if (!blend_en) {
        finalcvg = (int)(curpixel_cvg - 1);

      } else {
        finalcvg = (int)(curpixel_cvg + curpixel_memcvg);
      }

      if (!(finalcvg & 8)) {
        finalcvg &= 7;
      } else {
        finalcvg = 7;
      }

      break;
    case CVG_WRAP:
      finalcvg = (int)((curpixel_cvg + curpixel_memcvg) & 7);
      break;
    case CVG_ZAP:
      finalcvg = 7;
      break;
    case CVG_SAVE:
      finalcvg = (int)curpixel_memcvg;
      break;
    default:  // cvg_dest is always one of the 4 CVG_* modes; unreachable
      break;
  }

  return finalcvg;
}

static STRICTINLINE uint16_t decompress_cvmask_frombyte(uint8_t x) {
  uint16_t const y = (x & 0x5) | ((x & 0x5a) << 4) | ((x & 0xa0) << 8);
  return y;
}

void lookup_cvmask_derivatives(uint8_t mask, uint8_t* offx, uint8_t* offy,
                               uint32_t* curpixel_cvg,
                               uint32_t* curpixel_cvbit) {
  *curpixel_cvg = cvarray[mask].cvg;
  *curpixel_cvbit = cvarray[mask].cvbit;
  *offx = cvarray[mask].xoff;
  *offy = cvarray[mask].yoff;
}

void coverage_init_lut(void) {
  int i = 0;
  int k = 0;
  uint16_t mask = 0;
  uint16_t maskx = 0;
  uint16_t masky = 0;
  uint8_t offx = 0;
  uint8_t offy = 0;
  const uint8_t yarray[16] = {0, 0, 1, 0, 2, 0, 1, 0, 3, 0, 1, 0, 2, 0, 1, 0};
  const uint8_t xarray[16] = {0, 3, 2, 2, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0};

  for (; i < 0x100; i++) {
    mask = decompress_cvmask_frombyte(i);
    cvarray[i].cvg = cvarray[i].cvbit = 0;
    cvarray[i].cvbit = (i >> 7) & 1;
    for (k = 0; k < 8; k++) {
      cvarray[i].cvg += ((i >> k) & 1);
    }

    masky = 0;
    for (k = 0; k < 4; k++) {
      masky |= ((mask & (0xf000 >> (k << 2))) > 0) << k;
    }

    offy = yarray[masky];

    maskx = (mask & (0xf000 >> (offy << 2))) >> ((offy ^ 3) << 2);

    offx = xarray[maskx];

    cvarray[i].xoff = offx;
    cvarray[i].yoff = offy;
  }
}
