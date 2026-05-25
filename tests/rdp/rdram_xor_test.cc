// XOR-addressing swizzle tests for the visible-RDRAM access paths.
// BYTE_ADDR_XOR = 3, WORD_ADDR_XOR = 1 (LSB_FIRST). Verify that idx8/16/32
// reads and writes land on the byte-swizzled backing offsets and round-trip.

#include <stdint.h>
#include <string.h>

#include <gtest/gtest.h>

#include "rdram_test_api.h"

namespace {

constexpr uint32_t kRdramSize = 0x1000; // 4 KiB backing, plenty for these idxs

class RdramXor : public ::testing::Test {
protected:
    void SetUp() override {
        memset(backing_, 0, sizeof(backing_));
        rdram_full_init(backing_, kRdramSize);
    }
    alignas(8) uint8_t backing_[kRdramSize];
};

// idx8: byte index `in` maps to backing[in ^ 3].
TEST_F(RdramXor, Idx8SwizzleEvenAndOdd) {
    rdram_full_write_idx8(0, 0xAA); // -> backing[0^3] = backing[3]
    rdram_full_write_idx8(1, 0xBB); // -> backing[1^3] = backing[2]
    rdram_full_write_idx8(2, 0xCC); // -> backing[2^3] = backing[1]
    rdram_full_write_idx8(3, 0xDD); // -> backing[3^3] = backing[0]

    EXPECT_EQ(rdram_full_backing_byte(3), 0xAA);
    EXPECT_EQ(rdram_full_backing_byte(2), 0xBB);
    EXPECT_EQ(rdram_full_backing_byte(1), 0xCC);
    EXPECT_EQ(rdram_full_backing_byte(0), 0xDD);

    EXPECT_EQ(rdram_full_read_idx8(0), 0xAA);
    EXPECT_EQ(rdram_full_read_idx8(1), 0xBB);
    EXPECT_EQ(rdram_full_read_idx8(2), 0xCC);
    EXPECT_EQ(rdram_full_read_idx8(3), 0xDD);
}

// idx16: word index `in` maps to rdram16[in ^ 1] = backing bytes [(in^1)*2].
TEST_F(RdramXor, Idx16SwizzleEvenAndOdd) {
    rdram_full_write_idx16(0, 0x1234); // -> rdram16[1] -> backing bytes 2,3
    rdram_full_write_idx16(1, 0x5678); // -> rdram16[0] -> backing bytes 0,1

    // little-endian store of the 16-bit values into the swizzled words
    EXPECT_EQ(rdram_full_backing_byte(2), 0x34);
    EXPECT_EQ(rdram_full_backing_byte(3), 0x12);
    EXPECT_EQ(rdram_full_backing_byte(0), 0x78);
    EXPECT_EQ(rdram_full_backing_byte(1), 0x56);

    EXPECT_EQ(rdram_full_read_idx16(0), 0x1234);
    EXPECT_EQ(rdram_full_read_idx16(1), 0x5678);
}

// idx32: no XOR on the 32-bit path; word index `in` maps directly to rdram32[in].
TEST_F(RdramXor, Idx32NoSwizzle) {
    rdram_full_write_idx32(0, 0xDEADBEEFu);
    rdram_full_write_idx32(1, 0x01020304u);

    EXPECT_EQ(rdram_full_read_idx32(0), 0xDEADBEEFu);
    EXPECT_EQ(rdram_full_read_idx32(1), 0x01020304u);

    // little-endian backing layout for word 0
    EXPECT_EQ(rdram_full_backing_byte(0), 0xEF);
    EXPECT_EQ(rdram_full_backing_byte(1), 0xBE);
    EXPECT_EQ(rdram_full_backing_byte(2), 0xAD);
    EXPECT_EQ(rdram_full_backing_byte(3), 0xDE);
}

// out-of-range reads return 0; out-of-range writes are dropped.
TEST_F(RdramXor, BoundsCheck) {
    EXPECT_EQ(rdram_full_read_idx8(kRdramSize + 100), 0);
    rdram_full_write_idx8(kRdramSize + 100, 0xFF); // dropped, must not crash
    EXPECT_EQ(rdram_full_read_idx8(kRdramSize + 100), 0);
}

} // namespace
