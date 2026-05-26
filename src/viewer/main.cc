// main.cc — rdp_viewer host entry: CLI dispatch to the headless test harness
// (headless.cc) or the windowed SDL3 + ImGui inspector (windowed.cc).
//
// Orthodoxy-EXEMPT (mirrors platform_sdl / the host harness). This file is just
// the argument multiplexer; the implementations live in their own TUs and the
// shared seed_demo / rdpx_* accessors in viewer_common.cc.
//
// Flags:
//   --headless-dump <path.ppm>
//   --headless-demo-frame <n> <path.ppm>
//   --headless-perf <n>
//   --headless-capture-roundtrip <n>
// No flag -> windowed mode.

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "headless.h"
#include "windowed.h"

int main(int argc, char** argv) {
  for (int i = 1; i < argc; ++i) {
    if (strcmp(argv[i], "--headless-dump") == 0 && i + 1 < argc) {
      return headless_dump(argv[i + 1]);
    }
    if (strcmp(argv[i], "--headless-demo-frame") == 0 && i + 2 < argc) {
      uint32_t n = (uint32_t)strtoul(argv[i + 1], nullptr, 10);
      return headless_demo_frame(n, argv[i + 2]);
    }
    if (strcmp(argv[i], "--headless-perf") == 0 && i + 1 < argc) {
      uint32_t n = (uint32_t)strtoul(argv[i + 1], nullptr, 10);
      return headless_perf(n);
    }
    if (strcmp(argv[i], "--headless-capture-roundtrip") == 0 && i + 1 < argc) {
      uint32_t n = (uint32_t)strtoul(argv[i + 1], nullptr, 10);
      return headless_capture_roundtrip(n);
    }
  }
  return run_windowed();
}
