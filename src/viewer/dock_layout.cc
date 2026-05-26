/* dock_layout.cc — Layout A docking seed (DockBuilder).
 *
 * Orthodoxy-EXEMPT (ImGui host idioms). Builds the initial dock tree:
 *
 *   +----------------------------------------+-------------+
 *   |                                        |   State     |
 *   |                                        | (right top) |
 *   |          Scanout (central)             +-------------+
 *   |                                        |   Memory    |
 *   |                                        | (right bot) |
 *   +----------------------------------------+-------------+
 *   |              Commands  |  Perf  (bottom, tabbed pair) |
 *   +--------------------------------------------------------+
 *
 * Later panel streams just ImGui::Begin(VIEWER_DOCK_*) and land in the right
 * node.
 */

#include "dock_layout.h"

#include "imgui_internal.h"  // DockBuilder*

void viewer_build_default_dock_layout(ImGuiID dockspace_id) {
  // Reset any prior tree, then create a fresh dockspace-sized root node.
  ImGui::DockBuilderRemoveNode(dockspace_id);
  ImGui::DockBuilderAddNode(dockspace_id, ImGuiDockNodeFlags_DockSpace);
  ImGui::DockBuilderSetNodeSize(dockspace_id, ImGui::GetMainViewport()->Size);

  // Split a bottom strip (25%) for Commands/Perf; remaining is the working
  // area.
  ImGuiID dock_main = dockspace_id;
  ImGuiID dock_bottom = 0;
  ImGui::DockBuilderSplitNode(dock_main, ImGuiDir_Down, 0.25F, &dock_bottom,
                              &dock_main);

  // Split a right column (28%) off the working area for State/Memory.
  ImGuiID dock_right = 0;
  ImGui::DockBuilderSplitNode(dock_main, ImGuiDir_Right, 0.28F, &dock_right,
                              &dock_main);

  // Split the right column into State (top) and Memory (bottom).
  ImGuiID dock_right_bottom = 0;
  ImGui::DockBuilderSplitNode(dock_right, ImGuiDir_Down, 0.5F,
                              &dock_right_bottom, &dock_right);

  // dock_main is now the central scanout area.
  ImGui::DockBuilderDockWindow(VIEWER_DOCK_SCANOUT, dock_main);
  ImGui::DockBuilderDockWindow(VIEWER_DOCK_COMMANDS, dock_bottom);
  ImGui::DockBuilderDockWindow(VIEWER_DOCK_PERF,
                               dock_bottom);  // tabbed with Commands
  ImGui::DockBuilderDockWindow(VIEWER_DOCK_PLAYBACK,
                               dock_bottom);  // tabbed with Commands (C.3)
  ImGui::DockBuilderDockWindow(VIEWER_DOCK_STATE, dock_right);
  ImGui::DockBuilderDockWindow(VIEWER_DOCK_MEMORY, dock_right_bottom);

  ImGui::DockBuilderFinish(dockspace_id);
}
