// Bit-exact-neutrality of boundable hidden-RDRAM.
//
// Replays an identical write set, confined to a single contiguous FB region,
// through both the full-size hidden array and the bounded (offset/remapped)
// hidden buffer. Asserts the visible backing AND every hidden entry are
// BYTE-IDENTICAL between the two modes. The bounded base is the region's
// lowest hidden index, so hidden_at() remaps absolute idx -> bounded buffer.

#include <gtest/gtest.h>
#include <stdint.h>
#include <string.h>

#include "rdram_test_api.h"

namespace {

// 8 MiB RDRAM (full hidden = 4 MiB; that's exactly what bounded avoids).
constexpr uint32_t kRdramSize = 0x800000;

// FB region: a contiguous block of 16-bit pixels. Byte base 0x200000 (2 MiB).
constexpr uint32_t kFbByteBase = 0x200000;
constexpr uint32_t kFbWords16 = 320 * 240;        // 16bpp pixels
constexpr uint32_t kFbByteSpan = kFbWords16 * 2;  // bytes covered
// Hidden index = byte_addr >> 1 == 16-bit-word index. Region lowest hidden idx:
constexpr uint32_t kHiddenBase = kFbByteBase >> 1;

uint8_t* g_full_backing;
uint8_t* g_bnd_backing;

// Deterministic pattern so both runs see identical inputs.
uint8_t pat(uint32_t i) { return (uint8_t)(i * 31u + 7u); }

class HiddenBoundedEquiv : public ::testing::Test {
 protected:
  static void SetUpTestSuite() {
    g_full_backing = (uint8_t*)malloc(kRdramSize);
    g_bnd_backing = (uint8_t*)malloc(kRdramSize);
    ASSERT_NE(g_full_backing, nullptr);
    ASSERT_NE(g_bnd_backing, nullptr);
  }
  static void TearDownTestSuite() {
    free(g_full_backing);
    free(g_bnd_backing);
  }
  void SetUp() override {
    memset(g_full_backing, 0, kRdramSize);
    memset(g_bnd_backing, 0, kRdramSize);
    rdram_full_init(g_full_backing, kRdramSize);
    rdram_bounded_init(g_bnd_backing, kRdramSize);
    rdram_bounded_set_hidden_base(kHiddenBase);
  }
};

TEST_F(HiddenBoundedEquiv, PairWritesInFbRegionAreByteIdentical) {
  // Confine the access set to the FB region. Use 16-bit word indices in
  // [kHiddenBase, kHiddenBase + kFbWords16). Drive pair16 + pair8 + idx
  // writes — the full set of paths that touch hidden storage.
  for (uint32_t k = 0; k < kFbWords16; k += 7) {
    uint32_t word16 = kHiddenBase + k;  // 16-bit word index
    uint32_t byte = word16 << 1;        // byte address

    uint16_t rv = (uint16_t)(pat(k) | (pat(k + 1) << 8));
    uint8_t hv = (uint8_t)(pat(k) & 3);

    rdram_full_write_pair16(word16, rv, hv);
    rdram_bounded_write_pair16(word16, rv, hv);

    // pair8 at the odd byte exercises the hidden-write branch (in & 1).
    uint8_t hv8 = (uint8_t)(pat(k + 1) & 3);
    rdram_full_write_pair8(byte + 1, pat(k + 2), hv8);
    rdram_bounded_write_pair8(byte + 1, pat(k + 2), hv8);
  }

  // Visible backing must be byte-identical across the whole region.
  for (uint32_t b = kFbByteBase; b < kFbByteBase + kFbByteSpan; ++b) {
    ASSERT_EQ(rdram_full_backing_byte(b), rdram_bounded_backing_byte(b))
        << "visible byte mismatch at " << b;
  }

  // Hidden entries (by absolute index) must be byte-identical across region.
  for (uint32_t h = kHiddenBase; h < kHiddenBase + kFbWords16; ++h) {
    ASSERT_EQ(rdram_full_hidden(h), rdram_bounded_hidden(h))
        << "hidden mismatch at absolute idx " << h;
  }
}

// pair32 path equivalence (hits hidden[in<<1] and hidden[(in<<1)+1]).
TEST_F(HiddenBoundedEquiv, Pair32WritesInFbRegionAreByteIdentical) {
  constexpr uint32_t kFbWord32Base = kFbByteBase >> 2;  // 32-bit word base
  constexpr uint32_t kFbWords32 = kFbWords16 / 2;

  for (uint32_t k = 0; k < kFbWords32; k += 5) {
    uint32_t word32 = kFbWord32Base + k;
    uint32_t rv = (uint32_t)(pat(k) | (pat(k + 1) << 8) | (pat(k + 2) << 16) |
                             (pat(k + 3) << 24));
    uint8_t h0 = (uint8_t)(pat(k) & 3);
    uint8_t h1 = (uint8_t)(pat(k + 1) & 3);
    rdram_full_write_pair32(word32, rv, h0, h1);
    rdram_bounded_write_pair32(word32, rv, h0, h1);
  }

  for (uint32_t h = kHiddenBase; h < kHiddenBase + kFbWords16; ++h) {
    ASSERT_EQ(rdram_full_hidden(h), rdram_bounded_hidden(h))
        << "pair32 hidden mismatch at absolute idx " << h;
  }
}

}  // namespace
