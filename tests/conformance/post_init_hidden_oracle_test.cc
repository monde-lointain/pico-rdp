/* Stream A.5 — post-init hidden-RDRAM byte-equality vs the Angrylion oracle.
 *
 * This is the assertion the M0 barrier only LOGGED. It instantiates BOTH the
 * Angrylion oracle (A.3, pinned to the canonical 31bdb1f fork) and OUR renderer
 * (rdpx_* C ABI) through the vendored ReplayerState wiring, then asserts that
 * IMMEDIATELY AFTER init — with no clear, no randomize, no commands — the hidden
 * RDRAM buffers are BYTE-IDENTICAL.
 *
 * Before A.5's re-port, ours seeded hidden RDRAM to HB_CLEAN (4) (ported from
 * the WRONG, newer standalone fork) while the oracle seeds 3 (31bdb1f). Those
 * post-init buffers differed by construction, which would have made hidden
 * RDRAM non-bit-exact in every later stage. After the re-port both seed 3, so
 * the buffers match exactly here.
 *
 * Host-only test infrastructure (NOT orthodox-enforced).
 */

#include <stdint.h>
#include <string.h>

#include <gtest/gtest.h>

#include "replayer_driver_ours.hpp"
#include "replayer_state.hpp"

namespace RDP
{
std::unique_ptr<ReplayerDriver> create_replayer_driver_angrylion(CommandInterface &player,
                                                                 ReplayerEventInterface &iface);
}

using namespace RDP;

namespace {

TEST(PostInitHiddenOracle, HiddenRdramSeedMatchesOracleByteForByte)
{
	ReplayerState state;
	state.make_reference = create_replayer_driver_angrylion;
	state.make_gpu = create_replayer_driver_ours;

	ASSERT_TRUE(state.init()) << "ReplayerState::init() failed (driver wiring).";

	// Buffer sizes must match for a byte-for-byte comparison to be meaningful.
	ASSERT_EQ(state.reference->get_rdram_size(), state.gpu->get_rdram_size());
	ASSERT_EQ(state.reference->get_hidden_rdram_size(), state.gpu->get_hidden_rdram_size());

	const uint8_t *hidden_ref = state.reference->get_hidden_rdram();
	const uint8_t *hidden_gpu = state.gpu->get_hidden_rdram();
	const size_t   hidden_sz  = state.gpu->get_hidden_rdram_size();
	ASSERT_NE(hidden_ref, nullptr);
	ASSERT_NE(hidden_gpu, nullptr);
	ASSERT_GT(hidden_sz, 0u);

	// The canonical 31bdb1f rdram_init() seeds every hidden byte to 3. Confirm
	// the oracle does that (guards against an oracle/fork drift) ...
	for (size_t i = 0; i < hidden_sz; ++i)
		ASSERT_EQ(hidden_ref[i], 3) << "oracle hidden byte " << i << " not seeded to 3";

	// ... and that OUR renderer matches the oracle byte-for-byte at post-init.
	for (size_t i = 0; i < hidden_sz; ++i)
		ASSERT_EQ(hidden_gpu[i], hidden_ref[i])
			<< "post-init hidden RDRAM differs at byte " << i;

	// Belt-and-suspenders: the whole buffer in one shot.
	EXPECT_EQ(memcmp(hidden_gpu, hidden_ref, hidden_sz), 0);
}

} // namespace
