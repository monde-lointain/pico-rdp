/* panel_log.cc — Stream C.3: command-log panel.
 *
 * Orthodoxy-EXEMPT (ImGui host idioms; mirrors main.cc / the host harness).
 *
 * Decodes the current frame's buffered RDP command stream (Playback) via the
 * shared decoder (cmd_decode.h) into a scrollable list of id / name / summary.
 * The command currently at the step cursor is highlighted; when stepping, rows
 * past the cursor are dimmed (not yet fed to the renderer).
 */

#include "panels.h"

#include "dock_layout.h"
#include "imgui.h"

extern "C" {
#include "cmd_decode.h"
}

void panel_log_draw(struct Playback* pb) {
  if (!ImGui::Begin(VIEWER_DOCK_COMMANDS)) {
    ImGui::End();
    return;
  }

  const uint32_t cursor = pb->step_mode ? pb->step_cmd : pb->cmd_total;
  ImGui::Text("Frame %u: %u commands  (fed: %u)", pb->frame_index,
              pb->cmd_total, cursor);
  ImGui::Separator();

  if (ImGui::BeginChild("##cmdlog", ImVec2(0, 0), false,
                        ImGuiWindowFlags_HorizontalScrollbar)) {
    for (uint32_t i = 0; i < pb->cmd_total; ++i) {
      const struct PlaybackCmd* c = &pb->cmds[i];
      struct CmdRecord rec;
      cmd_decode(&pb->words[c->word_off], c->word_count, &rec);

      const int is_current = (i + 1u == cursor);  // last fed command
      const int is_future = (i >= cursor);        // not yet fed (stepping)

      if (is_current)
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.85f, 0.2f, 1.0f));
      else if (is_future)
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.5f, 0.5f, 0.5f, 1.0f));

      ImGui::Text("%4u  %02x %-22s %s", i, rec.id, rec.name, rec.summary);

      if (is_current || is_future) ImGui::PopStyleColor();
    }
  }
  ImGui::EndChild();

  ImGui::End();
}
