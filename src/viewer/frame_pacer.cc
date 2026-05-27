// frame_pacer.cc — see frame_pacer.h.

#include "frame_pacer.h"

void frame_pacer_init(struct FramePacer *p, double target_fps) {
  p->frame_period = (target_fps > 0.0) ? (1.0 / target_fps) : (1.0 / 60.0);
  p->dt_clamp = 0.25;  // ignore stalls longer than this (don't fast-forward)
  p->max_catchup = 4;
  p->accum = 0.0;
}

int frame_pacer_step(struct FramePacer *p, double dt) {
  if (dt < 0.0) {
    dt = 0.0;
  }
  if (dt > p->dt_clamp) {
    dt = p->dt_clamp;
  }
  p->accum += dt;

  int due = 0;
  while (p->accum >= p->frame_period && due < p->max_catchup) {
    p->accum -= p->frame_period;
    ++due;
  }
  // Drop any backlog beyond the catch-up cap so accum never grows unbounded.
  if (p->accum >= p->frame_period) {
    p->accum = 0.0;
  }
  return due;
}
