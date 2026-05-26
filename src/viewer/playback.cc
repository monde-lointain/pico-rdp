/* playback.cc — Stream C.3: live-demo driver + playback/stepping.
 *
 * Orthodoxy-EXEMPT (ImGui host idioms; mirrors main.cc / the host harness).
 *
 * Owns the per-frame buffered RDP command stream: demo_build_frame emits into a
 * buffering CmdSink that appends each command's raw words (and records its
 * offset/length) into the Playback struct. The renderer is then fed a prefix of
 * that buffer — the whole frame when playing, or the first step_cmd commands
 * when stepping — and presented through renderer_host_present_demo().
 */

#include "dock_layout.h"
#include "imgui.h"
#include "panels.h"
#include "renderer_host.h"

extern "C" {
#include "cmd_sink.h"
#include "demo.h"
}

// ---- buffering sink --------------------------------------------------------
// Appends one command (n words) to the Playback buffer, recording its offset.
static void playback_sink_emit(void* ctx, const uint32_t* words, uint32_t n) {
  struct Playback* pb = (struct Playback*)ctx;
  if (pb->cmd_total >= VIEWER_MAX_CMDS) {
    return;
  }
  if (pb->word_total + n > VIEWER_MAX_WORDS) {
    return;
  }
  struct PlaybackCmd* c = &pb->cmds[pb->cmd_total];
  c->word_off = pb->word_total;
  c->word_count = n;
  for (uint32_t i = 0; i < n; ++i) {
    pb->words[pb->word_total + i] = words[i];
  }
  pb->word_total += n;
  pb->cmd_total += 1;
}

// ---- frame buffering -------------------------------------------------------
void playback_init(struct Playback* pb) {
  pb->period = demo_anim_period_frames();
  if (pb->period == 0) {
    pb->period = 1;
  }
  if (pb->frame_index >= pb->period) {
    pb->frame_index %= pb->period;
  }

  pb->word_total = 0;
  pb->cmd_total = 0;

  struct CmdSink sink;
  sink.emit = playback_sink_emit;
  sink.ctx = pb;
  demo_build_frame(pb->frame_index, &sink);

  // Default: show the whole frame.
  pb->step_cmd = pb->cmd_total;
  pb->dirty = 1;
}

// Feed the first `k` buffered commands to the renderer, then present.
static void playback_feed(struct Playback* pb, uint32_t k) {
  if (k > pb->cmd_total) {
    k = pb->cmd_total;
  }
  struct CmdSink* sink = renderer_host_cmd_sink();
  if (!sink) {
    return;
  }
  for (uint32_t i = 0; i < k; ++i) {
    const struct PlaybackCmd* c = &pb->cmds[i];
    cmd_sink_emit(sink, &pb->words[c->word_off], c->word_count);
  }
  renderer_host_present_demo(demo_color_fb_addr(), demo_fb_width(),
                             demo_fb_height());
}

void playback_tick(struct Playback* pb) {
  if (pb->playing) {
    pb->frame_index = (pb->frame_index + 1U) % pb->period;
    playback_init(pb);  // rebuild the new frame; step cursor -> full frame
    pb->step_mode = 0;
  }
  if (!pb->dirty) {
    return;
  }
  // When stepping a partial frame, the renderer holds prior draws in its FB; a
  // full re-feed from the frame start (re-clearing) is required so the visible
  // result == exactly the first step_cmd commands. The demo frame begins with
  // its own clears, so feeding [0,k) reproduces the sub-frame
  // deterministically.
  uint32_t const k = pb->step_mode ? pb->step_cmd : pb->cmd_total;
  playback_feed(pb, k);
  pb->dirty = 0;
}

// ---- controls panel --------------------------------------------------------
void playback_panel(struct Playback* pb) {
  if (!ImGui::Begin(VIEWER_DOCK_PLAYBACK)) {
    ImGui::End();
    return;
  }

  ImGui::Text("Frame %u / %u", pb->frame_index, pb->period - 1U);
  ImGui::Text("Commands: %u  Step: %u%s", pb->cmd_total,
              pb->step_mode ? pb->step_cmd : pb->cmd_total,
              pb->step_mode ? " (sub-frame)" : " (full)");
  ImGui::Separator();

  if (pb->playing) {
    if (ImGui::Button("Pause")) {
      pb->playing = 0;
    }
  } else {
    if (ImGui::Button("Play")) {
      pb->playing = 1;
      pb->step_mode = 0;
      pb->dirty = 1;
    }
  }
  ImGui::SameLine();
  if (ImGui::Button("Step Frame +")) {
    pb->playing = 0;
    pb->frame_index = (pb->frame_index + 1U) % pb->period;
    playback_init(pb);
    pb->step_mode = 0;
  }
  ImGui::SameLine();
  if (ImGui::Button("Step Frame -")) {
    pb->playing = 0;
    pb->frame_index = (pb->frame_index + pb->period - 1U) % pb->period;
    playback_init(pb);
    pb->step_mode = 0;
  }

  ImGui::Separator();
  if (ImGui::Button("Step Cmd +")) {
    pb->playing = 0;
    pb->step_mode = 1;
    if (pb->step_cmd < pb->cmd_total) {
      pb->step_cmd += 1;
    }
    pb->dirty = 1;
  }
  ImGui::SameLine();
  if (ImGui::Button("Step Cmd -")) {
    pb->playing = 0;
    pb->step_mode = 1;
    if (pb->step_cmd > 0) {
      pb->step_cmd -= 1;
    }
    pb->dirty = 1;
  }
  ImGui::SameLine();
  if (ImGui::Button("All Cmds")) {
    pb->step_mode = 0;
    pb->step_cmd = pb->cmd_total;
    pb->dirty = 1;
  }

  int sc = (int)pb->step_cmd;
  if (ImGui::SliderInt("Cmd cursor", &sc, 0, (int)pb->cmd_total)) {
    pb->playing = 0;
    pb->step_mode = 1;
    pb->step_cmd = (uint32_t)sc;
    pb->dirty = 1;
  }

  ImGui::Separator();
  if (ImGui::Button("Reset (frame 0)")) {
    pb->playing = 0;
    pb->frame_index = 0;
    playback_init(pb);
    pb->step_mode = 0;
    renderer_host_reset();
  }

  ImGui::End();
}
