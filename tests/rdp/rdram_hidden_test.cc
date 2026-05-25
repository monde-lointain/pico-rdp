// Hidden-bit machinery tests (CANONICAL Angrylion fork 31bdb1f behavior).
//
// The 31bdb1f rdram.c has a SIMPLE hidden model: hidden RDRAM is seeded to 3
// in rdram_init(); pair writes store the hidden byte directly with no clean-bit
// (HB_CLEAN) tracking and no delayed-hidden-write state machine. (The newer
// standalone fork's clean-bit/delayed machinery is a divergent lineage and is
// intentionally NOT modeled here.)
//
// Drives fixed pair-write sequences through the ported rdram.c and compares
// visible + hidden bytes against an independent reference model of the SAME
// (31bdb1f) algorithm. Goldens are computed by the reference, per spec.

#include <stdint.h>
#include <string.h>

#include <gtest/gtest.h>

#include "rdram_test_api.h"

namespace {

constexpr uint32_t kRdramSize  = 0x1000;
constexpr uint8_t  HB_SEED     = 3;   // rdram_init seeds hidden to 3
constexpr uint32_t BYTE_XOR    = 3;
constexpr uint32_t WORD_XOR    = 1;
constexpr uint32_t RDRAM_MASK  = 0x00ffffff;

// ---- Independent reference model (mirrors src/rdp/rdp/rdram.c @ 31bdb1f) ----
struct RefRdram {
    uint8_t  rdram[kRdramSize];
    uint8_t  hidden[kRdramSize / 2];
    uint32_t idxlim8, idxlim16;

    void init() {
        memset(rdram, 0, sizeof(rdram));
        memset(hidden, HB_SEED, sizeof(hidden));
        idxlim8  = kRdramSize - 1;
        idxlim16 = (idxlim8 >> 1) & 0xffffffu;
    }
    bool valid8(uint32_t in)  const { return in <= idxlim8; }
    bool valid16(uint32_t in) const { return in <= idxlim16; }

    void write_pair8(uint32_t in, uint8_t rval, uint8_t hval) {
        in &= RDRAM_MASK;
        if (valid8(in)) {
            rdram[in ^ BYTE_XOR] = rval;
            if (in & 1)
                hidden[in >> 1] = hval;
        }
    }
    void write_pair16(uint32_t in, uint16_t rval, uint8_t hval) {
        in &= RDRAM_MASK >> 1;
        if (valid16(in)) {
            uint16_t* p = (uint16_t*)rdram;
            p[in ^ WORD_XOR] = rval;
            hidden[in] = hval;
        }
    }
    void read_pair16(uint16_t* rdst, uint8_t* hdst, uint32_t in) const {
        in &= RDRAM_MASK >> 1;
        if (valid16(in)) {
            const uint16_t* p = (const uint16_t*)rdram;
            *rdst = p[in ^ WORD_XOR];
            *hdst = hidden[in];
        } else {
            *rdst = *hdst = 0;
        }
    }
};

class RdramHidden : public ::testing::Test {
protected:
    void SetUp() override {
        memset(backing_, 0, sizeof(backing_));
        rdram_full_init(backing_, kRdramSize);
        ref_.init();
    }
    // assert visible backing + a window of hidden entries match the reference
    void ExpectMatch(const char* tag, uint32_t hi_lo, uint32_t hi_hi) {
        for (uint32_t b = 0; b < 64; ++b)
            ASSERT_EQ(rdram_full_backing_byte(b), ref_.rdram[b])
                << tag << " visible byte " << b;
        for (uint32_t h = hi_lo; h <= hi_hi; ++h)
            ASSERT_EQ(rdram_full_hidden(h), ref_.hidden[h])
                << tag << " hidden idx " << h;
    }
    alignas(8) uint8_t backing_[kRdramSize];
    RefRdram ref_;
};

// Hidden RDRAM is seeded to 3 by rdram_init() (the 31bdb1f seed, NOT 4).
TEST_F(RdramHidden, InitSeedsHiddenToThree) {
    for (uint32_t h = 0; h < (kRdramSize / 2); ++h)
        ASSERT_EQ(rdram_full_hidden(h), HB_SEED) << "hidden idx " << h;
}

// write_pair8: visible byte always stored (XOR-swizzled); hidden updated only
// on odd byte addresses (in & 1), taking the supplied hidden value verbatim.
TEST_F(RdramHidden, WritePair8OddUpdatesHidden) {
    const uint8_t rvals[] = {0x01, 0x02, 0x03, 0x80, 0x55, 0xAA, 0xFF, 0x10};
    const uint8_t hvals[] = {3, 0, 2, 1, 3, 0, 1, 2};
    for (uint32_t in = 0; in < 16; ++in) {
        uint8_t rv = rvals[in & 7];
        uint8_t hv = hvals[in & 7];
        rdram_full_write_pair8(in, rv, hv);
        ref_.write_pair8(in, rv, hv);
    }
    ExpectMatch("pair8", 0, 8);
}

// Even byte addresses leave hidden at its seed; only odd addresses write it.
TEST_F(RdramHidden, WritePair8EvenLeavesHiddenSeeded) {
    rdram_full_write_pair8(0, 0xAB, 1); // even -> hidden[0] untouched (==3)
    rdram_full_write_pair8(2, 0xCD, 2); // even -> hidden[1] untouched (==3)
    EXPECT_EQ(rdram_full_hidden(0), HB_SEED);
    EXPECT_EQ(rdram_full_hidden(1), HB_SEED);

    rdram_full_write_pair8(1, 0xEF, 2); // odd  -> hidden[0] = 2
    EXPECT_EQ(rdram_full_hidden(0), 2);
}

// read_pair16 returns the stored visible halfword and the stored hidden byte
// verbatim (no clean-bit synthesis in the canonical fork).
TEST_F(RdramHidden, ReadPair16ReturnsStoredHiddenVerbatim) {
    // Freshly-seeded hidden entry reads back as the seed value 3.
    rdram_full_write_idx16(2, 0x0001);
    uint16_t rv; uint8_t hv;
    rdram_full_read_pair16(&rv, &hv, 2);
    EXPECT_EQ(rv, 0x0001);
    EXPECT_EQ(hv, HB_SEED); // hidden untouched by idx16 write -> seed 3

    // After a pair16 write the hidden byte is exactly what was stored.
    rdram_full_write_pair16(2, 0xBEEF, 0);
    rdram_full_read_pair16(&rv, &hv, 2);
    EXPECT_EQ(rv, 0xBEEF);
    EXPECT_EQ(hv, 0);
}

// write_pair16 stores both visible and hidden; verify via read_pair16.
TEST_F(RdramHidden, WritePair16StoresVisibleAndHidden) {
    rdram_full_write_pair16(5, 0xBEEF, 2);
    ref_.write_pair16(5, 0xBEEF, 2);
    uint16_t rv; uint8_t hv;
    rdram_full_read_pair16(&rv, &hv, 5);
    EXPECT_EQ(rv, 0xBEEF);
    EXPECT_EQ(hv, 2);

    // cross-check the visible swizzle against the reference model
    for (uint32_t b = 0; b < 16; ++b)
        EXPECT_EQ(rdram_full_backing_byte(b), ref_.rdram[b]) << "byte " << b;
}

// write_pair32: visible 32-bit word stored directly (no XOR on the 32 path);
// two hidden entries at in<<1 and (in<<1)+1 take hval0 / hval1.
TEST_F(RdramHidden, WritePair32StoresTwoHiddenEntries) {
    rdram_full_write_pair32(3, 0xDEADBEEFu, 1, 2);
    EXPECT_EQ(rdram_full_read_idx32(3), 0xDEADBEEFu);
    EXPECT_EQ(rdram_full_hidden(6), 1); // in<<1
    EXPECT_EQ(rdram_full_hidden(7), 2); // (in<<1)+1
}

} // namespace
