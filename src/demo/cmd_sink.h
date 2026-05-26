#pragma once

// demo_core command sink: the seam between the geometry frontend and whatever
// consumes the RDP command word stream (the viewer feeding rdpx_rdp_cmd, the
// capture writer, or the tests). demo_build_frame emits one RDP command at a
// time as raw 64-bit command words (passed as uint32_t word pairs) into a sink.
//
// Orthodox: plain POD + a function pointer (NOT std::function), so it is usable
// from freestanding/device code with no heap and no C++ runtime.

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// emit: receives one command as `n` raw RDP command words at `words`.
//   ctx: opaque caller state passed back unchanged.
struct CmdSink {
  void (*emit)(void *ctx, const uint32_t *words, uint32_t n);
  void *ctx;
};

// Convenience forwarder: calls sink->emit(sink->ctx, words, n) (skips null
// sinks / null callbacks). Defined in the demo_core impl, not inline here.
void cmd_sink_emit(struct CmdSink *sink, const uint32_t *words, uint32_t n);

#ifdef __cplusplus
}  // extern "C"
#endif
