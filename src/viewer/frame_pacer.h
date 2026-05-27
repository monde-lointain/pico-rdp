// frame_pacer.h — fixed-timestep accumulator for the viewer's windowed loop.
//
// Decouples animation rate from the window's render/refresh rate: the loop
// calls frame_pacer_step(dt) each iteration and advances the demo by the
// returned count so the animation runs at exactly target_fps (gldemo = 60)
// regardless of vsync refresh. Clamps large dt (post-stall) and caps catch-up
// to avoid a spiral of death. Pure; no SDL/ImGui deps.

#ifndef RDP_VIEWER_FRAME_PACER_H
#define RDP_VIEWER_FRAME_PACER_H

struct FramePacer {
  double accum;         // unspent elapsed time (seconds)
  double frame_period;  // 1 / target_fps
  double dt_clamp;      // max dt accepted per step (anti-spiral)
  int max_catchup;      // max frames advanced in one step
};

// target_fps must be > 0. Sets frame_period = 1/target_fps, dt_clamp = 0.25 s,
// max_catchup = 4, accum = 0.
void frame_pacer_init(struct FramePacer *p, double target_fps);

// Add elapsed dt (seconds); return how many fixed frames are due now
// (0..max_catchup). dt is clamped to dt_clamp first; any backlog beyond the
// catch-up cap is dropped (accum reset) so the pacer never runs away.
int frame_pacer_step(struct FramePacer *p, double dt);

#endif  // RDP_VIEWER_FRAME_PACER_H
