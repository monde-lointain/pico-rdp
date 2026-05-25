// Hidden-bit machinery tests.
//
// Drives fixed pair-write sequences (flip=0 and flip=1, including the
// delayedhbwidx delayed-hidden-write state machine) through the ported
// rdram.c and compares visible + hidden bytes against an independent
// reference model of the SAME algorithm (faithfully transcribed from the
// angrylion source). Goldens are computed by the reference, per spec.

#include <stdint.h>
#include <string.h>

#include <gtest/gtest.h>

#include "rdram_test_api.h"

namespace {

constexpr uint32_t kRdramSize = 0x1000;
constexpr int      HB_CLEAN   = 4;
constexpr uint32_t BYTE_XOR   = 3;
constexpr uint32_t WORD_XOR   = 1;
constexpr uint32_t RDRAM_MASK = 0x00ffffff;

// ---- Independent reference model (mirrors src/rdp/rdp/rdram.c verbatim) ----
struct RefRdram {
    uint8_t  rdram[kRdramSize];
    uint8_t  hidden[kRdramSize / 2];
    uint8_t  hidden_old[8];
    uint32_t idxlim8, idxlim16;

    void init() {
        memset(rdram, 0, sizeof(rdram));
        memset(hidden, HB_CLEAN, sizeof(hidden));
        memset(hidden_old, 0, sizeof(hidden_old));
        idxlim8 = kRdramSize - 1;
        idxlim16 = (idxlim8 >> 1) & 0xffffffu;
    }
    bool valid8(uint32_t in)  const { return in <= idxlim8; }
    bool valid16(uint32_t in) const { return in <= idxlim16; }

    uint16_t r16(uint32_t in) const {
        const uint16_t* p = (const uint16_t*)rdram;
        return p[in ^ WORD_XOR];
    }

    void write_pair8(uint32_t in, uint8_t rval, int flip, int* dhw) {
        in &= RDRAM_MASK;
        if (!flip) {
            if (valid8(in)) {
                int hdst8 = hidden[in >> 1];
                if (!(in & 1)) {
                    if (hdst8 & HB_CLEAN)
                        hidden[in >> 1] = (r16(in >> 1) & 1) != 0;
                    else
                        hidden[in >> 1] &= ~2;
                    hidden[in >> 1] |= hidden_old[(in >> 1) & 7] & 2;
                } else {
                    if (hdst8 & HB_CLEAN)
                        hidden[in >> 1] = (r16(in >> 1) & 1) ? 2 : 0;
                    else
                        hidden[in >> 1] &= ~1;
                    hidden[in >> 1] |= rval & 1;
                }
                rdram[in ^ BYTE_XOR] = rval;
            }
            if (in & 1)
                hidden_old[(in >> 1) & 7] = (rval & 1) ? 3 : 0;
        } else {
            if (*dhw >= 0 && (uint32_t)*dhw < in) {
                if (valid8((uint32_t)*dhw)) {
                    int oldhbidx = *dhw >> 1;
                    hidden[oldhbidx] &= ~2;
                    hidden[oldhbidx] |= hidden_old[oldhbidx & 7] & 2;
                }
                *dhw = -1;
            }
            if (in & 1) {
                if (valid8(in)) {
                    if (*dhw >= 0) {
                        hidden[in >> 1] = (rval & 1) ? 3 : 0;
                    } else {
                        int hdst8 = hidden[in >> 1];
                        if (hdst8 & HB_CLEAN)
                            hidden[in >> 1] = (r16(in >> 1) & 1) ? 2 : 0;
                        else
                            hidden[in >> 1] &= ~1;
                        hidden[in >> 1] |= rval & 1;
                    }
                    rdram[in ^ BYTE_XOR] = rval;
                }
                hidden_old[(in >> 1) & 7] = (rval & 1) ? 3 : 0;
                *dhw = -1;
            } else {
                if (valid8(in)) {
                    int hdst8 = hidden[in >> 1];
                    if (hdst8 & HB_CLEAN)
                        hidden[in >> 1] = (r16(in >> 1) & 1) ? 3 : 0;
                    rdram[in ^ BYTE_XOR] = rval;
                }
                *dhw = in + 1;
            }
        }
    }
    void complete(int dhw) {
        if (valid8((uint32_t)dhw)) {
            int oldhbidx = dhw >> 1;
            hidden[oldhbidx] &= ~2;
            hidden[oldhbidx] |= hidden_old[oldhbidx & 7] & 2;
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

// flip == 0 path: even/odd byte writes, hidden clean->dirty transition.
TEST_F(RdramHidden, Flip0Sequence) {
    int dhw_real = -1, dhw_ref = -1;
    const uint8_t vals[] = {0x01, 0x02, 0x03, 0x80, 0x55, 0xAA, 0xFF, 0x10};
    for (uint32_t in = 0; in < 16; ++in) {
        uint8_t v = vals[in & 7];
        rdram_full_write_pair8(in, v, 0, &dhw_real);
        ref_.write_pair8(in, v, 0, &dhw_ref);
    }
    EXPECT_EQ(dhw_real, dhw_ref);
    ExpectMatch("flip0", 0, 8);
}

// flip == 1 path: exercises the delayedhbwidx delayed-hidden-write machine.
TEST_F(RdramHidden, Flip1DelayedSequence) {
    int dhw_real = -1, dhw_ref = -1;
    const uint8_t vals[] = {0x01, 0x00, 0x03, 0x01, 0x00, 0x01, 0xFF, 0x00};
    for (uint32_t in = 0; in < 24; ++in) {
        uint8_t v = vals[in & 7];
        rdram_full_write_pair8(in, v, 1, &dhw_real);
        ref_.write_pair8(in, v, 1, &dhw_ref);
        ASSERT_EQ(dhw_real, dhw_ref) << "delayedhbwidx diverged at in=" << in;
    }
    // flush any pending delayed hidden write
    if (dhw_real >= 0) {
        rdram_full_complete_delayed_hbwrites(dhw_real);
        ref_.complete(dhw_ref);
    }
    ExpectMatch("flip1", 0, 12);
}

// read_pair16 reports the visible halfword and a derived hidden bit; on a CLEAN
// entry the hidden result is synthesized from the visible LSB.
TEST_F(RdramHidden, ReadPair16CleanSynthesizesHidden) {
    rdram_full_write_idx16(2, 0x0001); // LSB set at word idx 2
    rdram_full_write_idx16(3, 0x0000); // LSB clear at word idx 3

    uint16_t rv; uint8_t hv;
    rdram_full_read_pair16(&rv, &hv, 2);
    EXPECT_EQ(rv, 0x0001);
    EXPECT_EQ(hv, 3); // (rdst & 1) -> 3 since hidden was CLEAN

    rdram_full_read_pair16(&rv, &hv, 3);
    EXPECT_EQ(rv, 0x0000);
    EXPECT_EQ(hv, 0);
}

// write_pair16 stores both visible and hidden, and updates hidden_old when
// iscolor; verify via a follow-up read_pair16 (hidden no longer CLEAN).
TEST_F(RdramHidden, WritePair16StoresHidden) {
    rdram_full_write_pair16(5, 0xBEEF, 2 /*hval, not CLEAN*/, 1 /*iscolor*/);
    uint16_t rv; uint8_t hv;
    rdram_full_read_pair16(&rv, &hv, 5);
    EXPECT_EQ(rv, 0xBEEF);
    EXPECT_EQ(hv, 2); // returned verbatim (HB_CLEAN bit not set)
}

} // namespace
