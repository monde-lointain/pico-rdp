// irand LCG sequence test.
// Formula (angrylion n64video.c): state = state*0x343fd + 0x269ec3;
//                                 return (state >> 16) & 0x7fff
// Worker seed = 3 + worker_id*13.

#include <stdint.h>

#include <gtest/gtest.h>

#include "n64video_common.h"

namespace {

// Independent reference implementation of the same recurrence, used to verify
// irand() reproduces the exact LCG sequence (no reliance on captured magic
// numbers — the formula itself is the oracle).
static uint32_t ref_irand(uint32_t* s) {
    *s = *s * 0x343fdu + 0x269ec3u;
    return (*s >> 16) & 0x7fffu;
}

TEST(Irand, MatchesFormulaFromSeed3) {
    uint32_t state = 3;
    uint32_t ref_state = 3;
    for (int i = 0; i < 4096; ++i) {
        uint32_t got = irand(&state);
        uint32_t want = ref_irand(&ref_state);
        ASSERT_EQ(got, want) << "divergence at step " << i;
        ASSERT_EQ(state, ref_state) << "state divergence at step " << i;
        ASSERT_LE(got, 0x7fffu);
    }
}

TEST(Irand, FirstValueExplicit) {
    // state0 = 3*0x343fd + 0x269ec3 = 0x9D3F7 + 0x269EC3 = 0x307CBA
    // return = (0x307CBA >> 16) & 0x7fff = 0x30
    uint32_t state = 3;
    uint32_t expected_state = 3u * 0x343fdu + 0x269ec3u;
    uint32_t v = irand(&state);
    EXPECT_EQ(state, expected_state);
    EXPECT_EQ(v, (expected_state >> 16) & 0x7fffu);
}

TEST(Irand, WorkerSeedFormula) {
    // Each worker seeds at 3 + worker_id*13; distinct workers diverge.
    for (uint32_t worker = 0; worker < 8; ++worker) {
        uint32_t seed = 3 + worker * 13;
        uint32_t state = seed;
        uint32_t ref_state = seed;
        for (int i = 0; i < 16; ++i) {
            ASSERT_EQ(irand(&state), ref_irand(&ref_state));
        }
    }
}

} // namespace
