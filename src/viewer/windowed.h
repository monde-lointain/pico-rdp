#pragma once

// rdp_viewer interactive (windowed) entry: SDL3 + ImGui docking shell that
// renders the demo scene through our renderer and presents an inspector.
// Orthodoxy-EXEMPT (mirrors main.cc / the host harness). Returns 0 on clean
// exit.
int run_windowed(void);
