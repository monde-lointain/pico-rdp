/* panel_state.cc — Stream C.3: RDP state inspector.
 *
 * Orthodoxy-EXEMPT (ImGui host idioms; mirrors main.cc / the host harness).
 *
 * Replays (shadow_apply) the buffered command stream up to the step cursor into
 * a fresh ShadowState, then surfaces combine / other-modes / tiles / image
 * addresses / colors / scissor — exactly the state the renderer would hold after
 * the fed prefix. The renderer exposes no accessor; this shadow is the source.
 */

#include "panels.h"

#include "dock_layout.h"
#include "imgui.h"

extern "C" {
#include "shadow_state.h"
}

static const char* cycle_name(uint8_t c) {
  switch (c & 3) {
    case 0: return "1CYCLE";
    case 1: return "2CYCLE";
    case 2: return "COPY";
    default: return "FILL";
  }
}

void panel_state_draw(struct Playback* pb) {
  if (!ImGui::Begin(VIEWER_DOCK_STATE)) {
    ImGui::End();
    return;
  }

  // Rebuild the shadow over [0, cursor).
  struct ShadowState s;
  shadow_reset(&s);
  const uint32_t cursor = pb->step_mode ? pb->step_cmd : pb->cmd_total;
  for (uint32_t i = 0; i < cursor && i < pb->cmd_total; ++i) {
    const struct PlaybackCmd* c = &pb->cmds[i];
    shadow_apply(&s, &pb->words[c->word_off], c->word_count);
  }

  ImGui::Text("Applied %u / %u commands", s.cmd_count, pb->cmd_total);
  ImGui::Separator();

  if (ImGui::CollapsingHeader("Images", ImGuiTreeNodeFlags_DefaultOpen)) {
    ImGui::Text("Color: addr 0x%06x  fmt %u size %u  w %u", s.color_addr,
                s.color_format, s.color_size, s.color_width);
    ImGui::Text("Depth: addr 0x%06x", s.depth_addr);
    ImGui::Text("Tex  : addr 0x%06x  fmt %u size %u  w %u", s.tex_addr,
                s.tex_format, s.tex_size, s.tex_width);
  }

  if (ImGui::CollapsingHeader("Other modes", ImGuiTreeNodeFlags_DefaultOpen)) {
    const struct ShadowOtherModes* m = &s.other_modes;
    ImGui::Text("cycle_type: %s", cycle_name(m->cycle_type));
    ImGui::Text("z: compare %u update %u  aa %u", m->z_compare_en,
                m->z_update_en, m->antialias_en);
    ImGui::Text("persp %u  tlut %u (type %u)  sample %u", m->persp_tex_en,
                m->en_tlut, m->tlut_type, m->sample_type);
    ImGui::Text("force_blend %u  alpha_cvg_sel %u  cvg_dest %u", m->force_blend,
                m->alpha_cvg_select, m->cvg_dest);
  }

  if (ImGui::CollapsingHeader("Combine", ImGuiTreeNodeFlags_DefaultOpen)) {
    const struct ShadowCombine* c = &s.combine;
    ImGui::Text("RGB0 sa%u sb%u m%u a%u", c->sub_a_rgb0, c->sub_b_rgb0,
                c->mul_rgb0, c->add_rgb0);
    ImGui::Text("A0   sa%u sb%u m%u a%u", c->sub_a_a0, c->sub_b_a0, c->mul_a0,
                c->add_a0);
    ImGui::Text("RGB1 sa%u sb%u m%u a%u", c->sub_a_rgb1, c->sub_b_rgb1,
                c->mul_rgb1, c->add_rgb1);
    ImGui::Text("A1   sa%u sb%u m%u a%u", c->sub_a_a1, c->sub_b_a1, c->mul_a1,
                c->add_a1);
  }

  if (ImGui::CollapsingHeader("Colors")) {
    ImGui::Text("fill (raw) 0x%08x", s.fill_color_raw);
    ImGui::Text("blend %u,%u,%u,%u", s.blend_color.r, s.blend_color.g,
                s.blend_color.b, s.blend_color.a);
    ImGui::Text("prim  %u,%u,%u,%u", s.prim_color.r, s.prim_color.g,
                s.prim_color.b, s.prim_color.a);
    ImGui::Text("env   %u,%u,%u,%u", s.env_color.r, s.env_color.g,
                s.env_color.b, s.env_color.a);
    ImGui::Text("fog   %u,%u,%u,%u", s.fog_color.r, s.fog_color.g,
                s.fog_color.b, s.fog_color.a);
  }

  if (ImGui::CollapsingHeader("Scissor")) {
    ImGui::Text("xh %u yh %u  xl %u yl %u (1/4 px)", s.scissor.xh, s.scissor.yh,
                s.scissor.xl, s.scissor.yl);
  }

  if (ImGui::CollapsingHeader("Tiles")) {
    for (uint32_t t = 0; t < 8; ++t) {
      const struct ShadowTile* tl = &s.tile[t];
      // Only show tiles that look configured (any non-zero field) or tile 0.
      if (t != 0 && tl->format == 0 && tl->size == 0 && tl->tmem == 0 &&
          tl->sh == 0 && tl->th == 0)
        continue;
      ImGui::Text("tile %u: fmt %u size %u line %u tmem 0x%03x pal %u", t,
                  tl->format, tl->size, tl->line, tl->tmem, tl->palette);
      ImGui::Text("   s[%u..%u] t[%u..%u] clampST %u%u", tl->sl, tl->sh, tl->tl,
                  tl->th, tl->cs, tl->ct);
    }
  }

  ImGui::End();
}
