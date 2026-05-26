#ifdef N64VIDEO_C

static STRICTINLINE void video_max_optimized(const uint32_t* pixels,
                                             uint32_t* penumin,
                                             uint32_t* penumax, int numofels) {
  int i;
  int posmax = 0;
  int posmin = 0;
  uint32_t curpenmax = pixels[0];
  uint32_t curpenmin = pixels[0];
  uint32_t max;
  uint32_t min;

  for (i = 1; i < numofels; i++) {
    if (pixels[i] > pixels[posmax]) {
      curpenmax = pixels[posmax];
      posmax = i;
    } else if (pixels[i] < pixels[posmin]) {
      curpenmin = pixels[posmin];
      posmin = i;
    }
  }
  max = pixels[posmax];
  min = pixels[posmin];
  if (curpenmax != max) {
    for (i = posmax + 1; i < numofels; i++) {
      if (pixels[i] > curpenmax) {
        curpenmax = pixels[i];
      }
    }
  }
  if (curpenmin != min) {
    for (i = posmin + 1; i < numofels; i++) {
      if (pixels[i] < curpenmin) {
        curpenmin = pixels[i];
      }
    }
  }
  *penumax = curpenmax;
  *penumin = curpenmin;
}

static STRICTINLINE void video_filter16(int* endr, int* endg, int* endb,
                                        uint32_t fboffset, uint32_t num,
                                        uint32_t hres, uint32_t centercvg,
                                        uint32_t fetchbugstate) {
  int i;
  uint32_t penumaxr;
  uint32_t penumaxg;
  uint32_t penumaxb;
  uint32_t penuminr;
  uint32_t penuming;
  uint32_t penuminb;
  uint16_t pix;
  uint32_t numoffull = 1;
  uint8_t hidval;
  uint32_t r;
  uint32_t g;
  uint32_t b;
  uint32_t backr[7];
  uint32_t backg[7];
  uint32_t backb[7];

  r = *endr;
  g = *endg;
  b = *endb;

  backr[0] = r;
  backg[0] = g;
  backb[0] = b;

  uint32_t const idx = (fboffset >> 1) + num;

  uint32_t const toleft = idx - 2;
  uint32_t const toright = idx + 2;

  uint32_t leftup;
  uint32_t rightup;
  uint32_t leftdown;
  uint32_t rightdown;

  leftup = idx - hres - 1;
  rightup = idx - hres + 1;

  if (fetchbugstate != 1) {
    leftdown = idx + hres - 1;
    rightdown = idx + hres + 1;
  } else {
    leftdown = toleft;
    rightdown = toright;
  }

  const uint32_t dirs[] = {leftup,  rightup,  toleft,
                           toright, leftdown, rightdown};

  for (i = 0; i < 6; i++) {
    rdram_read_pair16(&pix, &hidval, dirs[i]);
    if (hidval == 3 && (pix & 1)) {
      backr[numoffull] = RGBA16_R(pix);
      backg[numoffull] = RGBA16_G(pix);
      backb[numoffull] = RGBA16_B(pix);
      numoffull++;
    }
  }

  uint32_t colr;
  uint32_t colg;
  uint32_t colb;

  video_max_optimized(backr, &penuminr, &penumaxr, numoffull);
  video_max_optimized(backg, &penuming, &penumaxg, numoffull);
  video_max_optimized(backb, &penuminb, &penumaxb, numoffull);

  uint32_t const coeff = 7 - centercvg;
  colr = penuminr + penumaxr - (r << 1);
  colg = penuming + penumaxg - (g << 1);
  colb = penuminb + penumaxb - (b << 1);

  colr = (((colr * coeff) + 4) >> 3) + r;
  colg = (((colg * coeff) + 4) >> 3) + g;
  colb = (((colb * coeff) + 4) >> 3) + b;

  *endr = colr & 0xff;
  *endg = colg & 0xff;
  *endb = colb & 0xff;
}

static STRICTINLINE void video_filter32(int* endr, int* endg, int* endb,
                                        uint32_t fboffset, uint32_t num,
                                        uint32_t hres, uint32_t centercvg,
                                        uint32_t fetchbugstate) {
  int i;
  uint32_t penumaxr;
  uint32_t penumaxg;
  uint32_t penumaxb;
  uint32_t penuminr;
  uint32_t penuming;
  uint32_t penuminb;
  uint32_t numoffull = 1;
  uint32_t pix = 0;
  uint32_t pixcvg = 0;
  uint32_t r;
  uint32_t g;
  uint32_t b;
  uint32_t backr[7];
  uint32_t backg[7];
  uint32_t backb[7];

  r = *endr;
  g = *endg;
  b = *endb;

  backr[0] = r;
  backg[0] = g;
  backb[0] = b;

  uint32_t const idx = (fboffset >> 2) + num;

  uint32_t const toleft = idx - 2;
  uint32_t const toright = idx + 2;

  uint32_t leftup;
  uint32_t rightup;
  uint32_t leftdown;
  uint32_t rightdown;

  leftup = idx - hres - 1;
  rightup = idx - hres + 1;

  if (fetchbugstate != 1) {
    leftdown = idx + hres - 1;
    rightdown = idx + hres + 1;
  } else {
    leftdown = toleft;
    rightdown = toright;
  }

  const uint32_t dirs[] = {leftup,  rightup,  toleft,
                           toright, leftdown, rightdown};

  for (i = 0; i < 6; i++) {
    pix = rdram_read_idx32(dirs[i]);
    pixcvg = (pix >> 5) & 7;
    if (pixcvg == 7) {
      backr[numoffull] = (pix >> 24) & 0xff;
      backg[numoffull] = (pix >> 16) & 0xff;
      backb[numoffull] = (pix >> 8) & 0xff;
      numoffull++;
    }
  }

  uint32_t colr;
  uint32_t colg;
  uint32_t colb;

  video_max_optimized(backr, &penuminr, &penumaxr, numoffull);
  video_max_optimized(backg, &penuming, &penumaxg, numoffull);
  video_max_optimized(backb, &penuminb, &penumaxb, numoffull);

  uint32_t const coeff = 7 - centercvg;
  colr = penuminr + penumaxr - (r << 1);
  colg = penuming + penumaxg - (g << 1);
  colb = penuminb + penumaxb - (b << 1);

  colr = (((colr * coeff) + 4) >> 3) + r;
  colg = (((colg * coeff) + 4) >> 3) + g;
  colb = (((colb * coeff) + 4) >> 3) + b;

  *endr = colr & 0xff;
  *endg = colg & 0xff;
  *endb = colb & 0xff;
}

#endif  // N64VIDEO_C
