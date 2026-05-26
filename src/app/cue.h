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
      return 660u;
    case CUE_MOVE:
      return 330u;
    default:
      return 0u;
  }
}

static inline uint32_t cue_dur(enum Cue c) {
  switch (c) {
    case CUE_SELECT:
      return 40u;
    case CUE_MOVE:
      return 20u;
    default:
      return 0u;
  }
}

#endif /* APP_CUE_H */
