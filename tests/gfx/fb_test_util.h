/* fb_test_util.h — shared Framebuffer helpers for the gfx unit tests. */
#pragma once

#include "gfx/framebuffer.h"

static inline void fb_clear(Framebuffer *fb, color_t fill) {
  for (int i = 0; i < SCREEN_PIXELS; ++i) fb->px[i] = fill;
}

static inline color_t fb_get(const Framebuffer *fb, int x, int y) {
  return fb->px[y * SCREEN_W + x];
}
