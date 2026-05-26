// Stream D.1: demo command-stream snapshot.
//
// Runs demo_build_frame(0, sink) into a recording sink that captures the full
// raw RDP command-word stream (every 64-bit command flattened to its uint32
// words, in emission order), then compares it word-for-word against a COMMITTED
// golden (tests/demo/golden/demo_frame0_stream.words). The geometry is fixed-
// point and the per-frame matrices are baked, so demo_build_frame(0, ...) is a
// pure, portable function: the golden is host-independent and checked in.
//
// To (re)generate the golden after an INTENTIONAL change to the demo stream,
// set DEMO_GOLDEN_REGEN=<abs path to golden> and run this binary; it writes the
// current stream there and SUCCEEDs without comparing. Commit the regenerated
// file. Normal CI runs never set the var.
//
// Not orthodox-enforced (gtest / C++ idioms).

#include <gtest/gtest.h>

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include <vector>

#include "cmd_sink.h"
#include "demo.h"

namespace {

// Recording sink: appends every emitted command word, in order, to a vector.
struct Recorder {
  std::vector<uint32_t> words;
};

void recorder_emit(void *ctx, const uint32_t *words, uint32_t n) {
  Recorder *r = (Recorder *)ctx;
  for (uint32_t i = 0; i < n; ++i) r->words.push_back(words[i]);
}

void build_frame0_stream(std::vector<uint32_t> *out) {
  Recorder r;
  CmdSink sink;
  sink.emit = recorder_emit;
  sink.ctx = &r;
  demo_build_frame(0, &sink);
  *out = r.words;
}

#ifndef DEMO_GOLDEN_STREAM_PATH
#define DEMO_GOLDEN_STREAM_PATH ""
#endif

}  // namespace

TEST(DemoSnapshotStream, Frame0MatchesGolden) {
  std::vector<uint32_t> stream;
  build_frame0_stream(&stream);
  ASSERT_FALSE(stream.empty()) << "demo_build_frame(0) emitted no words";

  const char *regen = getenv("DEMO_GOLDEN_REGEN");
  if (regen && regen[0]) {
    FILE *f = fopen(regen, "wb");
    ASSERT_NE(f, nullptr) << "cannot open regen path " << regen;
    size_t wrote = fwrite(stream.data(), sizeof(uint32_t), stream.size(), f);
    fclose(f);
    ASSERT_EQ(wrote, stream.size());
    SUCCEED() << "regenerated golden (" << stream.size() << " words) at "
              << regen;
    return;
  }

  const char *path = DEMO_GOLDEN_STREAM_PATH;
  ASSERT_NE(path[0], '\0') << "DEMO_GOLDEN_STREAM_PATH not defined";

  FILE *f = fopen(path, "rb");
  ASSERT_NE(f, nullptr) << "cannot open golden " << path;
  fseek(f, 0, SEEK_END);
  long sz = ftell(f);
  fseek(f, 0, SEEK_SET);
  ASSERT_GT(sz, 0);
  ASSERT_EQ((size_t)sz % sizeof(uint32_t), 0u) << "golden size not word-aligned";

  std::vector<uint32_t> golden(sz / sizeof(uint32_t));
  size_t got = fread(golden.data(), sizeof(uint32_t), golden.size(), f);
  fclose(f);
  ASSERT_EQ(got, golden.size());

  ASSERT_EQ(stream.size(), golden.size())
      << "stream word count changed (got " << stream.size() << ", golden "
      << golden.size() << ")";
  for (size_t i = 0; i < stream.size(); ++i) {
    ASSERT_EQ(stream[i], golden[i])
        << "word " << i << " differs: got 0x" << std::hex << stream[i]
        << " golden 0x" << golden[i];
  }
}

// Determinism: two independent builds of frame 0 produce the identical stream.
TEST(DemoSnapshotStream, Frame0Deterministic) {
  std::vector<uint32_t> a, b;
  build_frame0_stream(&a);
  build_frame0_stream(&b);
  ASSERT_EQ(a, b);
}
