#include <gtest/gtest.h>
#include <stdint.h>
#include <string.h>

#include "rdram_io.h"

// The renderer's byte/halfword swizzle constants (src/rdp/n64video_common.h).
// The test re-derives these independently of rdram_io.c so it actually pins the
// renderer's read formula rather than just round-tripping our own constant.
static const uint32_t kByteAddrXor = 3;
static const uint32_t kWordAddrXor = 1;

namespace {

// Arena big enough for arbitrary offsets used below; zeroed.
struct Arena {
  uint8_t mem[4096];
  void clear() { memset(mem, 0, sizeof(mem)); }
};

}  // namespace

// After rdram_write_block, reading each source byte i back via the renderer's
// own formula rdram8[(off+i) ^ 3] must yield the original byte. Cover odd
// offset and odd length.
TEST(RdramXor, WriteBlockMatchesRendererByteRead) {
  Arena a;
  a.clear();

  uint8_t src[37];
  for (uint32_t i = 0; i < sizeof(src); ++i) {
    src[i] = (uint8_t)(0xA5 ^ (i * 7 + 1));
  }

  const uint32_t off = 5;  // odd offset, odd length (37)
  rdram_write_block(a.mem, off, src, sizeof(src));

  for (uint32_t i = 0; i < sizeof(src); ++i) {
    uint8_t got = a.mem[(off + i) ^ kByteAddrXor];  // renderer's read formula
    EXPECT_EQ(got, src[i]) << "byte " << i;
  }
}

// rdram_read_block is the exact inverse of rdram_write_block, for a range of
// offsets/lengths including odd ones and zero length.
TEST(RdramXor, RoundTripBlock) {
  Arena a;

  const uint32_t offs[] = {0, 1, 2, 3, 7, 16, 17, 100};
  const uint32_t lens[] = {0, 1, 2, 3, 4, 5, 8, 33, 64};

  for (uint32_t oi = 0; oi < sizeof(offs) / sizeof(offs[0]); ++oi) {
    for (uint32_t li = 0; li < sizeof(lens) / sizeof(lens[0]); ++li) {
      a.clear();
      uint32_t off = offs[oi];
      uint32_t n = lens[li];

      uint8_t src[64];
      for (uint32_t i = 0; i < n; ++i) {
        src[i] = (uint8_t)(i * 13 + off + 1);
      }

      rdram_write_block(a.mem, off, src, n);

      uint8_t back[64];
      memset(back, 0xCC, sizeof(back));
      rdram_read_block(a.mem, off, back, n);

      EXPECT_EQ(0, memcmp(src, back, n))
          << "off=" << off << " n=" << n;
    }
  }
}

// A plain memcpy into the arena is wrong: the renderer's swizzled read must not
// recover the source for a multi-byte buffer (guards against regressing the
// helper into a memcpy).
TEST(RdramXor, PlainMemcpyWouldBeWrong) {
  Arena a;
  a.clear();

  uint8_t src[8] = {1, 2, 3, 4, 5, 6, 7, 8};
  const uint32_t off = 0;
  memcpy(a.mem + off, src, sizeof(src));

  bool any_mismatch = false;
  for (uint32_t i = 0; i < sizeof(src); ++i) {
    uint8_t got = a.mem[(off + i) ^ kByteAddrXor];
    if (got != src[i]) any_mismatch = true;
  }
  EXPECT_TRUE(any_mismatch);
}

// Single-byte accessors agree with the renderer's byte formula.
TEST(RdramXor, Byte8Accessors) {
  Arena a;
  a.clear();

  for (uint32_t off = 0; off < 16; ++off) {
    uint8_t val = (uint8_t)(0x3C ^ off);
    rdram_write8(a.mem, off, val);
    EXPECT_EQ(a.mem[off ^ kByteAddrXor], val) << "off=" << off;
    EXPECT_EQ(rdram_read8(a.mem, off), val) << "off=" << off;
  }
}

// Halfword accessors land little-endian and match the renderer's halfword read
// rdram16[(off/2) ^ WORD_ADDR_XOR] for even offsets.
TEST(RdramXor, Halfword16MatchesRendererWordRead) {
  Arena a;
  a.clear();

  uint16_t vals[] = {0x0000, 0x1234, 0xBEEF, 0xFFFF, 0x00FF, 0xFF00};
  for (uint32_t off = 0; off + 1 < 32; off += 2) {
    uint16_t v = vals[(off / 2) % (sizeof(vals) / sizeof(vals[0]))];
    rdram_write16(a.mem, off, v);

    // Renderer reads halfwords through a uint16_t view at index off/2 ^ 1.
    const uint16_t *rdram16 = (const uint16_t *)a.mem;
    uint16_t got = rdram16[(off / 2) ^ kWordAddrXor];
    EXPECT_EQ(got, v) << "off=" << off;

    EXPECT_EQ(rdram_read16(a.mem, off), v) << "off=" << off;
  }
}
