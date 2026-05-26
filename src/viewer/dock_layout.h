#pragma once

// dock_layout — Layout A docking seed for the viewer (see the PC-app design
// doc).
//
// Builds the initial ImGui dock layout programmatically via DockBuilder on
// first run: a central node holds the scanout, with named side/bottom nodes
// reserved so later panel streams can dock their windows by name without
// re-seeding.
//
// Reserved dock window names (later streams call ImGui::Begin with these exact
// titles to attach into the seeded nodes):
#define VIEWER_DOCK_SCANOUT "Scanout"
#define VIEWER_DOCK_COMMANDS "Commands"
#define VIEWER_DOCK_STATE "State"
#define VIEWER_DOCK_MEMORY "Memory"
#define VIEWER_DOCK_PERF "Perf"

#include "imgui.h"

// Seed the Layout A dock tree into dockspace_id. Idempotent per call (it
// removes and rebuilds the node), so the caller should invoke it only on first
// run (or on an explicit "reset layout"). Docks the reserved windows into their
// nodes.
void viewer_build_default_dock_layout(ImGuiID dockspace_id);
