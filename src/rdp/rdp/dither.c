#ifdef N64VIDEO_C

static const uint8_t BAYER_MATRIX[16] = {0, 4, 1, 5, 4, 0, 5, 1,
                                         3, 7, 2, 6, 7, 3, 6, 2};

static const uint8_t MAGIC_MATRIX[16] = {0, 6, 1, 7, 4, 2, 5, 3,
                                         3, 5, 2, 4, 7, 1, 6, 0};

/* From: https://www.shadertoy.com/view/XlXcW4.
 * Here to make noise deterministic in validation. */
struct SeedState {
  uint32_t x, y, z;
};

static void seed_iteration(struct SeedState* seed) {
  static const uint32_t noise_prime = 1103515245U;
  uint32_t x;
  uint32_t y;
  uint32_t z;

  x = ((seed->x >> 8U) ^ seed->y) * noise_prime;
  y = ((seed->y >> 8U) ^ seed->z) * noise_prime;
  z = ((seed->z >> 8U) ^ seed->x) * noise_prime;

  seed->x = x;
  seed->y = y;
  seed->z = z;
}

void reseed_noise(uint32_t* seed, uint32_t x, uint32_t y, uint32_t offset) {
  struct SeedState s = {x, y, offset};
  seed_iteration(&s);
  seed_iteration(&s);
  seed_iteration(&s);
  *seed = s.x >> 16;
}

int noise_get_combiner(uint32_t seed) { return ((seed & 7U) << 6U) | 0x20U; }

int noise_get_dither_alpha(uint32_t seed) { return seed & 7U; }

int noise_get_dither_color(uint32_t seed) { return seed & 0x1ff; }

int noise_get_blend_threshold(uint32_t seed) { return seed & 0xffU; }

static STRICTINLINE void rgb_dither(int rgb_dither_sel, int* r, int* g, int* b,
                                    int dith) {
  int32_t newr = *r;
  int32_t newg = *g;
  int32_t newb = *b;
  int32_t rcomp;
  int32_t gcomp;
  int32_t bcomp;

  if (newr > 247) {
    newr = 255;
  } else {
    newr = (newr & 0xf8) + 8;
  }
  if (newg > 247) {
    newg = 255;
  } else {
    newg = (newg & 0xf8) + 8;
  }
  if (newb > 247) {
    newb = 255;
  } else {
    newb = (newb & 0xf8) + 8;
  }

  if (rgb_dither_sel != 2) {
    rcomp = gcomp = bcomp = dith;
  } else {
    rcomp = dith & 7;
    gcomp = (dith >> 3) & 7;
    bcomp = (dith >> 6) & 7;
  }

  int32_t replacesign = (rcomp - (*r & 7)) >> 31;

  int32_t ditherdiff = newr - *r;
  *r = *r + (ditherdiff & replacesign);

  replacesign = (gcomp - (*g & 7)) >> 31;
  ditherdiff = newg - *g;
  *g = *g + (ditherdiff & replacesign);

  replacesign = (bcomp - (*b & 7)) >> 31;
  ditherdiff = newb - *b;
  *b = *b + (ditherdiff & replacesign);
}

/* For validation purposes, update combiner noise state separately after
 * reseeding. Pipelined noise isn't exactly meaningful to try to emulate. */
static STRICTINLINE void update_combiner_noise(uint32_t wid) {
  if (!rdpxi_state[wid].other_modes.f.getditherlevel) {
    rdpxi_state[wid].noise = noise_get_combiner(rdpxi_state[wid].noise_seed);
  }
}

static STRICTINLINE void get_dither_noise(uint32_t wid, int x, int y,
                                          int* cdith, int* adith) {
  update_combiner_noise(wid);

  y >>= rdpxi_state[wid].scfield;

  int dithindex;
  switch (rdpxi_state[wid].other_modes.f.rgb_alpha_dither) {
    case 0:
      dithindex = ((y & 3) << 2) | (x & 3);
      *adith = *cdith = MAGIC_MATRIX[dithindex];
      break;
    case 1:
      dithindex = ((y & 3) << 2) | (x & 3);
      *cdith = MAGIC_MATRIX[dithindex];
      *adith = (~(*cdith)) & 7;
      break;
    case 2:
      dithindex = ((y & 3) << 2) | (x & 3);
      *cdith = MAGIC_MATRIX[dithindex];
      *adith = noise_get_dither_alpha(rdpxi_state[wid].noise_seed);
      break;
    case 3:
      dithindex = ((y & 3) << 2) | (x & 3);
      *cdith = MAGIC_MATRIX[dithindex];
      *adith = 0;
      break;
    case 4:
      dithindex = ((y & 3) << 2) | (x & 3);
      *adith = *cdith = BAYER_MATRIX[dithindex];
      break;
    case 5:
      dithindex = ((y & 3) << 2) | (x & 3);
      *cdith = BAYER_MATRIX[dithindex];
      *adith = (~(*cdith)) & 7;
      break;
    case 6:
      dithindex = ((y & 3) << 2) | (x & 3);
      *cdith = BAYER_MATRIX[dithindex];
      *adith = noise_get_dither_alpha(rdpxi_state[wid].noise_seed);
      break;
    case 7:
      dithindex = ((y & 3) << 2) | (x & 3);
      *cdith = BAYER_MATRIX[dithindex];
      *adith = 0;
      break;
    case 8:
      dithindex = ((y & 3) << 2) | (x & 3);
      *cdith = noise_get_dither_color(rdpxi_state[wid].noise_seed);
      *adith = MAGIC_MATRIX[dithindex];
      break;
    case 9:
      dithindex = ((y & 3) << 2) | (x & 3);
      *cdith = noise_get_dither_color(rdpxi_state[wid].noise_seed);
      *adith = (~MAGIC_MATRIX[dithindex]) & 7;
      break;
    case 10:
      *cdith = noise_get_dither_color(rdpxi_state[wid].noise_seed);
      *adith = noise_get_dither_alpha(rdpxi_state[wid].noise_seed);
      break;
    case 11:
      *cdith = noise_get_dither_color(rdpxi_state[wid].noise_seed);
      *adith = 0;
      break;
    case 12:
      dithindex = ((y & 3) << 2) | (x & 3);
      *cdith = 7;
      *adith = BAYER_MATRIX[dithindex];
      break;
    case 13:
      dithindex = ((y & 3) << 2) | (x & 3);
      *cdith = 7;
      *adith = (~BAYER_MATRIX[dithindex]) & 7;
      break;
    case 14:
      *cdith = 7;
      *adith = noise_get_dither_alpha(rdpxi_state[wid].noise_seed);
      break;
    case 15:
      *cdith = 7;
      *adith = 0;
      break;
  }
}

#endif  // N64VIDEO_C
