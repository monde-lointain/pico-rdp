#pragma once

// Stream C.5 — `.rdp` replay source for rdp_viewer. Loads a dump written by
// capture.{h,cc} (or any parallel-rdp-compatible `.rdp`) through the canonical
// DumpPlayer reader (tests/conformance/vendor/rdp_dump.cpp via
// rdp::conformance_utils), re-initializes the renderer at the dump's RDRAM
// size, applies the dump's VI registers + RDRAM regions, feeds its RDP commands
// through the SAME renderer_host command sink the live demo uses, and presents.
//
// A "source switch": the viewer renders either the live demo or a loaded dump.
//
// Orthodoxy-EXEMPT (host viewer TU; consumes the C++ DumpPlayer API directly,
// mirroring the conformance adapter).

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Load `path` via DumpPlayer, re-init the renderer with the dump's
// rdram_size/hidden size, replay all of its records (VI registers, RDRAM
// regions, RDP commands), and present the resulting frame through the
// renderer's scanout. After success the renderer holds the replayed frame; the
// host reads it via renderer_host_scanout() exactly as for the live demo.
//
// Returns 0 on success, non-zero on failure (load/parse error, renderer
// re-init failure). On failure the renderer is left re-initialized to the demo
// default size (caller should re-seed the demo).
int source_dump_replay(const char* path);

#ifdef __cplusplus
}  // extern "C"
#endif
