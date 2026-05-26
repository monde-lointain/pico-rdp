/* source_dump.cc — Stream C.5: `.rdp` replay source for rdp_viewer.
 *
 * Loads a dump via the canonical DumpPlayer reader and replays it through the
 * SAME renderer_host path the live demo uses, so the viewer can switch between
 * the live demo and a captured/standalone `.rdp`. See source_dump.h.
 *
 * Orthodoxy-EXEMPT: this host TU consumes the C++ DumpPlayer/CommandListener
 * API (rdp::conformance_utils) directly, mirroring the conformance adapter
 * (tests/conformance/replayer_driver_ours.cpp). The renderer + demo seams are
 * the C ABIs renderer_host.h / demo.h.
 */

#include "source_dump.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "rdp_dump.hpp"  // RDP::DumpPlayer, CommandListenerInterface (vendored)
#include "renderer_host.h"

extern "C" {
#include "cmd_sink.h"
#include "demo.h"
}

// RDPX_TESTING hidden-RDRAM accessor (compiled into rdp_core; forward-declared
// at the use site as the conformance adapter does — not in n64video.h's public
// surface). Lets us land a dump's hidden-RDRAM regions if it carries any.
extern "C" {
uint8_t* rdpx_get_hidden_rdram(void);
uint32_t rdpx_get_hidden_rdram_size(void);
}

namespace {

// CommandListener that drives renderer_host as DumpPlayer iterates a dump.
// Mirrors OursReplayer (conformance adapter): update_rdram memcpys into the
// renderer arena, command() feeds raw words through the renderer sink, and
// end_frame presents. The renderer is re-initialized to the dump's RDRAM size
// BEFORE iteration begins (see source_dump_replay), so all offsets fit.
struct DumpToRenderer : RDP::CommandListenerInterface {
  bool ok = true;

  void set_vi_register(RDP::VIRegister reg, uint32_t value) override {
    renderer_host_set_vi_register((uint32_t)reg, value);
  }

  void signal_complete() override {}

  void command(RDP::Op /*cmd_id*/, uint32_t num_words,
               const uint32_t* words) override {
    struct CmdSink* sink = renderer_host_cmd_sink();
    if (!sink) {
      this->ok = false;
      return;
    }
    // Feed the raw RDP command words straight into our dispatch (exactly as the
    // conformance adapter's rdpx_rdp_cmd(0, words)); cmd_id is unused — the
    // opcode lives in words[0].
    cmd_sink_emit(sink, words, num_words);
  }

  void end_frame() override {
    // Present through the SAME blit-then-VI path the live demo uses, so a dump
    // captured from the demo reproduces its scanout byte-for-byte. The demo
    // color-FB address/size are fixed macros independent of frame content.
    renderer_host_present_demo(demo_color_fb_addr(), demo_fb_width(),
                               demo_fb_height());
  }

  void eof() override {}

  void update_rdram(const void* data, size_t size, size_t offset) override {
    uint32_t cap = 0;
    uint8_t* rdram = renderer_host_rdram(&cap);
    if (!rdram || offset + size > cap) {
      this->ok = false;
      return;
    }
    memcpy(rdram + offset, data, size);
  }

  void update_hidden_rdram(const void* data, size_t size,
                           size_t offset) override {
    uint8_t* hid = rdpx_get_hidden_rdram();
    uint32_t cap = rdpx_get_hidden_rdram_size();
    if (!hid || offset + size > cap) {
      // Hidden-RDRAM is coverage scratch; our VI_MODE_COLOR scanout does not
      // read it. A size mismatch (dump declares 4 MiB, our bounded buffer is
      // smaller) is non-fatal — clamp what fits.
      if (!hid) return;
      if (offset >= cap) return;
      size = cap - offset;
    }
    memcpy(hid + offset, data, size);
  }
};

}  // namespace

extern "C" int source_dump_replay(const char* path) {
  if (!path) return 1;

  RDP::DumpPlayer player;
  if (!player.load_dump(path)) {
    fprintf(stderr, "source_dump: failed to load/parse %s\n", path);
    return 2;
  }

  uint32_t rdram_size = (uint32_t)player.get_rdram_size();

  // Re-init the renderer at the dump's RDRAM size so its UpdateDram offsets all
  // fit (dumps carry 4 or 8 MiB; the demo arena is 2 MiB).
  if (renderer_host_reset_with_size(rdram_size) != 0) {
    fprintf(stderr, "source_dump: renderer re-init (%u bytes) failed\n",
            rdram_size);
    renderer_host_reset();  // restore a usable default-size renderer
    return 3;
  }

  DumpToRenderer listener;
  player.set_command_interface(&listener);

  while (listener.ok && player.iterate()) {
    // iterate() returns false at EndOfFile; loop body intentionally empty.
  }

  if (!listener.ok) {
    fprintf(stderr, "source_dump: replay error during %s\n", path);
    return 4;
  }

  printf("source_dump: replayed %s (rdram=%u)\n", path, rdram_size);
  return 0;
}
