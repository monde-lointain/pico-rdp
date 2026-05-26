/* panel_perf.cc — Stream C.4: perf HUD.
 *
 * Orthodoxy-EXEMPT (ImGui host idioms; mirrors main.cc / the host harness).
 *
 * Shows, for the most recently presented demo frame: the host build+present
 * wall-clock, the rasterizer-committed pixel count (rdpx_get_pixel_count under
 * RDPX_TESTING), the host Mpixels/s, and — via the docs/perf-extrapolation.md
 * MODEL — the extrapolated RP2040 Cortex-M0+ ms/frame. It also decodes the
 * frame's buffered command stream (the C.2 decoder) into per-command-type
 * counts.
 *
 * The extrapolation is a MODEL (not a re-run of tests/perf/throughput_probe):
 * the doc derives a per-pixel M0+ cost band of ~250 cycles (optimistic, hot
 * LUTs recomputed/hit) to ~700 cycles (pessimistic, full XIP z_com_table
 * thrash). M0+ ms/frame = pixels * cycles_per_px / clock_hz * 1000. We show
 * both bands at the PicoSystem default 250 MHz and at the datasheet 133 MHz the
 * doc tables use, so the numbers line up with §3.2 of the doc.
 */

#include "panel_perf.h"

#include <stdint.h>

#include "dock_layout.h"
#include "imgui.h"

extern "C" {
#include "cmd_decode.h"
}
#include "panels.h"  // struct Playback

// ---- extrapolation model ---------------------------------------------------
double perf_extrapolate_m0_ms(uint64_t pixel_count, double m0_cycles_per_px,
                              double m0_clock_hz) {
  if (m0_clock_hz <= 0.0) return 0.0;
  double cycles = (double)pixel_count * m0_cycles_per_px;
  return (cycles / m0_clock_hz) * 1000.0;
}

// Per-pixel M0+ cycle bands from docs/perf-extrapolation.md §3.2.
#define M0_CYC_PER_PX_OPTIMISTIC 250.0
#define M0_CYC_PER_PX_PESSIMISTIC 700.0
// Clocks: PicoSystem default (set_sys_clock_khz 250 MHz) and the datasheet
// 133 MHz the doc's tables extrapolate at.
#define M0_CLOCK_DEFAULT_HZ 250000000.0
#define M0_CLOCK_DATASHEET_HZ 133000000.0

void panel_perf_draw(struct Playback* pb, const struct PerfSample* s) {
  if (!ImGui::Begin(VIEWER_DOCK_PERF)) {
    ImGui::End();
    return;
  }

  const uint64_t px = s ? s->pixel_count : 0u;
  const double ms = s ? s->build_present_ms : 0.0;

  ImGui::Text("Frame %u  (%u commands)", pb->frame_index, pb->cmd_total);
  ImGui::Separator();

  ImGui::Text("Host build+present: %.3f ms", ms);
  ImGui::Text("Pixels committed:   %llu", (unsigned long long)px);
  if (ms > 0.0) {
    double mpix_s = ((double)px / 1.0e6) / (ms / 1000.0);
    ImGui::Text("Host throughput:    %.2f Mpixels/s", mpix_s);
  } else {
    ImGui::Text("Host throughput:    (n/a)");
  }

  ImGui::Separator();
  ImGui::TextUnformatted(
      "Extrapolated RP2040 M0+ (model: perf-extrapolation.md)");
  ImGui::Text("  per-pixel band: %.0f (optimistic) .. %.0f (pessimistic) cyc",
              M0_CYC_PER_PX_OPTIMISTIC, M0_CYC_PER_PX_PESSIMISTIC);

  if (px > 0) {
    double opt_250 = perf_extrapolate_m0_ms(px, M0_CYC_PER_PX_OPTIMISTIC,
                                            M0_CLOCK_DEFAULT_HZ);
    double pes_250 = perf_extrapolate_m0_ms(px, M0_CYC_PER_PX_PESSIMISTIC,
                                            M0_CLOCK_DEFAULT_HZ);
    double opt_133 = perf_extrapolate_m0_ms(px, M0_CYC_PER_PX_OPTIMISTIC,
                                            M0_CLOCK_DATASHEET_HZ);
    double pes_133 = perf_extrapolate_m0_ms(px, M0_CYC_PER_PX_PESSIMISTIC,
                                            M0_CLOCK_DATASHEET_HZ);
    ImGui::Text("  @250 MHz: %.1f .. %.1f ms/frame  (~%.0f..%.0f fps)", opt_250,
                pes_250, opt_250 > 0 ? 1000.0 / pes_250 : 0.0,
                pes_250 > 0 ? 1000.0 / opt_250 : 0.0);
    ImGui::Text("  @133 MHz: %.1f .. %.1f ms/frame  (~%.0f..%.0f fps)", opt_133,
                pes_133, opt_133 > 0 ? 1000.0 / pes_133 : 0.0,
                pes_133 > 0 ? 1000.0 / opt_133 : 0.0);
  } else {
    ImGui::TextUnformatted("  (no pixels this frame)");
  }

  // ---- per-command-type counts ---------------------------------------------
  ImGui::Separator();
  if (ImGui::CollapsingHeader("Command types (this frame)",
                              ImGuiTreeNodeFlags_DefaultOpen)) {
    // Histogram over the 64 possible 6-bit opcodes; plus a triangle bucket.
    uint32_t by_id[64];
    for (uint32_t i = 0; i < 64; ++i) by_id[i] = 0u;
    uint32_t tri_total = 0u;
    for (uint32_t i = 0; i < pb->cmd_total; ++i) {
      const struct PlaybackCmd* c = &pb->cmds[i];
      struct CmdRecord rec;
      cmd_decode(&pb->words[c->word_off], c->word_count, &rec);
      by_id[rec.id & 0x3f] += 1u;
      if (rec.is_triangle) tri_total += 1u;
    }
    ImGui::Text("Triangles (0x08..0x0f): %u", tri_total);
    if (ImGui::BeginTable(
            "##cmdtypes", 3,
            ImGuiTableFlags_Borders | ImGuiTableFlags_SizingStretchProp)) {
      ImGui::TableSetupColumn("op");
      ImGui::TableSetupColumn("name");
      ImGui::TableSetupColumn("count");
      ImGui::TableHeadersRow();
      for (uint32_t id = 0; id < 64; ++id) {
        if (by_id[id] == 0u) continue;
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ImGui::Text("0x%02x", id);
        ImGui::TableSetColumnIndex(1);
        ImGui::TextUnformatted(rdp_cmd_name((uint8_t)id));
        ImGui::TableSetColumnIndex(2);
        ImGui::Text("%u", by_id[id]);
      }
      ImGui::EndTable();
    }
  }

  ImGui::End();
}
