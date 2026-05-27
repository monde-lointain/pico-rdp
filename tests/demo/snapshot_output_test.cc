// Stream D.1: demo rendered-output snapshot.
//
// Drives demo frame 0 through OUR renderer (rdp_core, RDPX_TESTING on host) and
// compares the rendered color framebuffer against a COMMITTED golden image.
//
// Setup mirrors tests/conformance/replayer_driver_ours.cpp: caller-owned RDRAM,
// VI/DP register pointer arrays, a no-op mi_intr, VI_MODE_NORMAL /
// VI_INTERP_LINEAR / DP_COMPAT_HIGH. We do NOT exercise the VI here: the demo
// rasterizes into DEMO_RDRAM_COLOR_FB_ADDR, and we read that 240x240 RGBA5551
// surface straight out of RDRAM via the XOR-correct rdram_io helpers (so the
// golden is the raw rasterizer output, host-independent and checked in).
//
// (Re)generate the golden after an INTENTIONAL renderer/demo change by setting
// DEMO_GOLDEN_REGEN=<abs path> and running this binary; it writes the current
// FB there and SUCCEEDs without comparing. Commit the regenerated file.
//
// Not orthodox-enforced (gtest / C++ idioms). RDRAM byte aliasing => built with
// -fno-strict-aliasing (see CMakeLists).

#include <gtest/gtest.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <vector>

#include "cmd_sink.h"
#include "demo.h"
#include "rdp_host_fixture.h"  // RdpHostFixture, feed_emit, n64video.h
#include "rdram_io.h"

namespace {

const uint32_t kFbW = DEMO_FB_WIDTH;   // 240
const uint32_t kFbH = DEMO_FB_HEIGHT;  // 240
const uint32_t kPixels = kFbW * kFbH;  // 57600

#ifndef DEMO_GOLDEN_OUTPUT_PATH
#define DEMO_GOLDEN_OUTPUT_PATH ""
#endif

// Render demo frame 0 and read back the 240x240 RGBA5551 color FB (native
// big-endian halfwords) into out.
void render_frame0_fb(std::vector<uint16_t> *out) {
  RdpHostFixture fx;
  rdp_host_fixture_init(&fx, RDRAM_MAX_SIZE);

  // Seed the CI4 texture + TLUT into the arena, then feed the frame.
  demo_init(fx.rdram.data(), (uint32_t)fx.rdram.size());

  CmdSink sink;
  sink.emit = feed_emit;
  sink.ctx = nullptr;
  demo_build_frame(0, &sink);

  // Read the color FB straight out of RDRAM as native big-endian halfwords.
  out->resize(kPixels);
  for (uint32_t i = 0; i < kPixels; ++i) {
    (*out)[i] = rdram_read16(fx.rdram.data(), DEMO_RDRAM_COLOR_FB_ADDR + i * 2u);
  }

  rdp_host_fixture_close(&fx);
}

}  // namespace

TEST(DemoSnapshotOutput, Frame0MatchesGolden) {
  std::vector<uint16_t> fb;
  render_frame0_fb(&fb);

  // The demo must actually draw something: more than one distinct color.
  bool nonuniform = false;
  for (uint32_t i = 1; i < fb.size(); ++i) {
    if (fb[i] != fb[0]) {
      nonuniform = true;
      break;
    }
  }
  ASSERT_TRUE(nonuniform) << "rendered FB is uniform — demo did not render";

  const char *regen = getenv("DEMO_GOLDEN_REGEN");
  if (regen && regen[0]) {
    FILE *f = fopen(regen, "wb");
    ASSERT_NE(f, nullptr) << "cannot open regen path " << regen;
    size_t wrote = fwrite(fb.data(), sizeof(uint16_t), fb.size(), f);
    fclose(f);
    ASSERT_EQ(wrote, fb.size());
    SUCCEED() << "regenerated golden (" << fb.size() << " px) at " << regen;
    return;
  }

  const char *path = DEMO_GOLDEN_OUTPUT_PATH;
  ASSERT_NE(path[0], '\0') << "DEMO_GOLDEN_OUTPUT_PATH not defined";

  FILE *f = fopen(path, "rb");
  ASSERT_NE(f, nullptr) << "cannot open golden " << path;
  std::vector<uint16_t> golden(kPixels);
  size_t got = fread(golden.data(), sizeof(uint16_t), golden.size(), f);
  fclose(f);
  ASSERT_EQ(got, golden.size()) << "golden pixel count mismatch";

  uint32_t diffs = 0;
  uint32_t first = 0;
  for (uint32_t i = 0; i < kPixels; ++i) {
    if (fb[i] != golden[i]) {
      if (diffs == 0) first = i;
      ++diffs;
    }
  }
  ASSERT_EQ(diffs, 0u) << diffs << "/" << kPixels << " px differ (first @"
                       << first << ": got 0x" << std::hex << fb[first]
                       << " golden 0x" << golden[first] << ")";
}
