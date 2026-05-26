#pragma once

// Stream C.3 — live-demo driver + debugger panels (playback/stepping, command
// log, state inspector) for rdp_viewer.
//
// Orthodoxy-EXEMPT (ImGui host idioms; mirrors main.cc / the host harness).
//
// One Playback object owns the per-frame buffered RDP command stream and the
// play/pause/step cursor; the three panels read it. main.cc drives the loop:
//   playback_init(); ... per UI frame: playback_tick(); then draw the panels.
// The renderer is fed through renderer_host (see renderer_host.h).

#include <stdint.h>

// A buffered RDP command captured from demo_build_frame: its raw words plus the
// stream offset (for re-feeding the first K commands during step-command).
#define VIEWER_MAX_CMDS 4096
#define VIEWER_MAX_WORDS 65536

struct PlaybackCmd {
  uint32_t word_off;    // offset into Playback.words where this cmd starts
  uint32_t word_count;  // words this command occupies
};

struct Playback {
  // Buffered current-frame command stream (filled by demo_build_frame).
  uint32_t words[VIEWER_MAX_WORDS];
  uint32_t word_total;
  struct PlaybackCmd cmds[VIEWER_MAX_CMDS];
  uint32_t cmd_total;

  uint32_t frame_index;  // current animation frame [0, period)
  uint32_t period;       // DEMO_ANIM_PERIOD_FRAMES

  int playing;        // 1 = advancing a frame per tick
  uint32_t step_cmd;  // step-command cursor: # of commands fed [0, cmd_total]
  int step_mode;      // 1 = honoring step_cmd (sub-frame), 0 = whole frame

  int dirty;  // re-feed + present needed this tick
};

// Build/buffer the current frame's command stream, reset the step cursor to the
// full frame, and mark dirty. Called on init, step-frame, reset, and play tick.
void playback_init(struct Playback* pb);

// Per-UI-frame update: if playing, advance the frame; if dirty, feed the
// appropriate prefix of the buffered stream into the renderer and present.
void playback_tick(struct Playback* pb);

// The three docked panels (call inside the ImGui frame).
void playback_panel(struct Playback* pb);    // play/pause/step/reset controls
void panel_log_draw(struct Playback* pb);    // decoded command log
void panel_state_draw(struct Playback* pb);  // shadow-state inspector
