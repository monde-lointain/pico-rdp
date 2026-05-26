#ifndef APP_CUE_H
#define APP_CUE_H

#include <stdint.h>

#include "app/app.h"

/* Cue -> (freq_hz, duration_ms) mapping for the platform beeper.
 *
 * Shared by both entry points (main_pico / main_sdl): the cue semantics are
 * game-defined and platform-independent, so they live here once rather than
 * being duplicated per main. CUE_NONE / unknown -> 0 (silent).
 *
 * Header-only static inline (Orthodox C): no extra TU, no CMake change. */

static inline uint32_t cue_freq(enum Cue c) {
  switch (c) {
    case CUE_SELECT:
      return 660U;
    case CUE_MOVE:
      return 330U;
    default:
      return 0U;
  }
}

static inline uint32_t cue_dur(enum Cue c) {
  switch (c) {
    case CUE_SELECT:
      return 40U;
    case CUE_MOVE:
      return 20U;
    default:
      return 0U;
  }
}

#endif /* APP_CUE_H */
