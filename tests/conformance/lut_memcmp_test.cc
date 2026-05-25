// Stream A.0 — LUT memcmp scaffold.
//
// Placeholder for the bit-exact lookup-table comparison: once Stream B ports the
// Angrylion LUTs into src/rdp/luts/luts.h (and the oracle exposes its own), this
// test will memcmp our generated tables against the oracle's, proving the LUTs
// are byte-identical. At M0 the LUTs are declarations only (no data yet), so this
// test asserts trivially. It exists now so the build wiring is in place and
// Stream B has a ready home for the real comparison.
//
// Host-only test infrastructure (gtest; NOT orthodox-enforced).

#include <gtest/gtest.h>

#include "luts/luts.h"

// TODO(Stream B): replace with real memcmp of our LUTs vs the oracle's once both
// sides define table data. Keep the comparison byte-exact.
TEST(LutMemcmp, ScaffoldPresentUntilStreamB)
{
	SUCCEED() << "LUT memcmp scaffold: LUTs are declaration-only at M0; "
	             "Stream B fills the tables and this becomes a real byte-compare.";
}
