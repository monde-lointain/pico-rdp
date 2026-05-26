// viewer_common.cc — shared rdp_viewer helpers (see viewer_common.h).
// Orthodoxy-EXEMPT (mirrors main.cc / the host harness).

#include "viewer_common.h"

#include <stdint.h>
#include <stdio.h>

#include "renderer_host.h"

extern "C" {
#include "demo.h"
}

// Seed the demo arena into the renderer's RDRAM. Returns 0 on success.
int seed_demo(void) {
  uint32_t size = 0;
  uint8_t* rdram = renderer_host_rdram(&size);
  if (!rdram || size < DEMO_RDRAM_USED_END) {
    fprintf(stderr, "seed_demo: RDRAM unavailable / too small (%u < %u)\n",
            size, (unsigned)DEMO_RDRAM_USED_END);
    return 1;
  }
  demo_init(rdram, size);
  return 0;
}
