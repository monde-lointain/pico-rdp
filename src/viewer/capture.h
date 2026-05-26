#pragma once

// Stream C.5 — capture the viewer's live RDP command stream to a `.rdp` file
// that is BYTE-COMPATIBLE with parallel-rdp's DumpPlayer reader
// (tests/conformance/vendor/rdp_dump.cpp). The written file can be replayed
// standalone by source_dump.{h,cc} (which loads it through DumpPlayer) to
// reproduce the captured frame's scanout byte-for-byte.
//
// File layout mirrored from DumpPlayer::load_dump / ::iterate exactly:
//   - 8-byte header magic "RDPDUMP2"
//   - uint32 rdram_size  (must be 4 MiB or 8 MiB)
//   - uint32 hidden_dram_size (must be 4 MiB)
//   - a record stream; each record begins with a uint32 Command tag:
//       1 UpdateDram            : uint32 offset, uint32 size, then size bytes
//       2 RDPCommand            : uint32 cmd_id, uint32 word_count, words[]
//       3 SetVIRegister         : uint32 index, uint32 value
//       4 EndFrame              : (no payload)
//       5 SignalComplete        : (no payload)
//       6 EndOfFile             : (no payload)
//       7 UpdateDramFlush       : (no payload) -> reader calls update_rdram
//       8 UpdateHiddenDram      : uint32 offset, uint32 size, then size bytes
//       9 UpdateHiddenDramFlush : (no payload)
//   All multi-byte values are little-endian uint32 (native on x86_64 host).
//
// Orthodoxy-EXEMPT (host viewer TU; mirrors main.cc / renderer_host).

#include <stdint.h>

struct Playback;  // panels.h

#ifdef __cplusplus
extern "C" {
#endif

// Capture the demo frame currently buffered in `pb` (its cmd/word arrays) to
// `path` as a DumpPlayer-compatible `.rdp`. The renderer's CURRENT RDRAM is
// snapshotted as the seeded state, the renderer's current VI registers are
// recorded, and every buffered command is written as an RDPCommand record so a
// standalone replay reproduces the frame through the same render path.
//
// Intended call sequence (matches the viewer's playback feed):
//   renderer_host_init(); seed_demo(); playback_init(pb);   // pb buffers frame
//   capture_frame_to_rdp(path, pb);                         // BEFORE feeding
// Capturing the pre-render (seeded) RDRAM keeps the dump self-contained: replay
// re-runs the commands to regenerate the rendered framebuffer
// deterministically.
//
// Returns 0 on success, non-zero on failure (I/O, renderer not initialized,
// RDRAM size not a valid DumpPlayer size).
int capture_frame_to_rdp(const char* path, const struct Playback* pb);

#ifdef __cplusplus
}  // extern "C"
#endif
