// Stream D.1: demo command-stream capture round-trip (SDL-free gtest).
//
// The viewer owns the real .rdp capture (DumpPlayer-compatible, SDL-bound); the
// end-to-end scanout round-trip is exercised by the
// `demo_capture_roundtrip_run` ctest, which runs `rdp_viewer
// --headless-capture-roundtrip 0` under SDL_VIDEODRIVER=dummy (registered in
// CMakeLists). This gtest is the pure, SDL-free complement: it serializes
// demo_build_frame(0)'s command stream into an in-memory buffer using the .rdp
// RDPCommand record framing (capture.h), reads it back, and asserts the decoded
// word stream is identical — locking the per-command framing that the capture
// writer/reader depend on, with no SDL.
//
// Not orthodox-enforced (gtest / C++ idioms).

#include <gtest/gtest.h>
#include <stdint.h>
#include <string.h>

#include <vector>

#include "cmd_sink.h"
#include "demo.h"

namespace {

// One buffered command: its raw words (the .rdp RDPCommand payload).
struct Cmd {
  std::vector<uint32_t> words;
};

struct CmdList {
  std::vector<Cmd> cmds;
};

// Capture sink: one Cmd per emit (each emit is one complete RDP command).
void capture_emit(void *ctx, const uint32_t *words, uint32_t n) {
  CmdList *cl = (CmdList *)ctx;
  Cmd c;
  for (uint32_t i = 0; i < n; ++i) c.words.push_back(words[i]);
  cl->cmds.push_back(c);
}

// RDPCommand record tag (capture.h: Command tag 2 = RDPCommand).
const uint32_t kRdpCommand = 2u;

// Serialize a command list to the .rdp RDPCommand record framing:
//   uint32 tag(=2), uint32 cmd_id, uint32 word_count, words[word_count]
// (cmd_id derived from word0 bits [29:24], as source_dump.cc does; it is
// informational for replay — rdpx_rdp_cmd reads only the words).
void serialize(const CmdList &cl, std::vector<uint32_t> *out) {
  for (size_t i = 0; i < cl.cmds.size(); ++i) {
    const Cmd &c = cl.cmds[i];
    uint32_t cmd_id = c.words.empty() ? 0u : ((c.words[0] >> 24) & 0x3fu);
    out->push_back(kRdpCommand);
    out->push_back(cmd_id);
    out->push_back((uint32_t)c.words.size());
    for (size_t w = 0; w < c.words.size(); ++w) out->push_back(c.words[w]);
  }
}

// Decode the record framing back into a command list.
bool deserialize(const std::vector<uint32_t> &buf, CmdList *out) {
  size_t p = 0;
  while (p < buf.size()) {
    if (p + 3 > buf.size()) return false;
    uint32_t tag = buf[p++];
    uint32_t cmd_id = buf[p++];
    (void)cmd_id;
    uint32_t wc = buf[p++];
    if (tag != kRdpCommand) return false;
    if (p + wc > buf.size()) return false;
    Cmd c;
    for (uint32_t w = 0; w < wc; ++w) c.words.push_back(buf[p++]);
    out->cmds.push_back(c);
  }
  return true;
}

void build_frame0(CmdList *out) {
  CmdSink sink;
  sink.emit = capture_emit;
  sink.ctx = out;
  demo_build_frame(0, &sink);
}

}  // namespace

TEST(DemoCaptureRoundtrip, Frame0StreamSurvivesEncodeDecode) {
  CmdList orig;
  build_frame0(&orig);
  ASSERT_FALSE(orig.cmds.empty()) << "demo_build_frame(0) emitted no commands";

  std::vector<uint32_t> buf;
  serialize(orig, &buf);
  ASSERT_FALSE(buf.empty());

  CmdList back;
  ASSERT_TRUE(deserialize(buf, &back)) << "record framing failed to decode";

  ASSERT_EQ(back.cmds.size(), orig.cmds.size());
  for (size_t i = 0; i < orig.cmds.size(); ++i) {
    ASSERT_EQ(back.cmds[i].words, orig.cmds[i].words)
        << "command " << i << " differs after round-trip";
  }
}
