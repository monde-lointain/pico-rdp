/* Stream A.0 — M0 side-by-side comparator (empty command stream).
 *
 * The M0 BARRIER runner. Builds a Vulkan-free ReplayerState (A.2), injects the
 * Angrylion oracle (A.3) as the reference and OUR rdp_core renderer (A.0) as the
 * gpu, then proves the harness <-> oracle <-> our-adapter wiring end-to-end:
 *
 *   1. Clear both drivers (the harness's own clear_rdram: visible + hidden = 0)
 *      and drive an EMPTY command stream (no commands) -> compare_rdram EQUAL.
 *   2. Identically randomize RDRAM + hidden on both, run the empty stream again
 *      -> still EQUAL (proves the compare exercises non-zero buffers).
 *
 * NOTE on the post-init hidden seed: both the A.3 oracle and OUR rdram.c are
 * now ported from the SAME canonical fork (angrylion-rdp-plus @ 31bdb1f), which
 * seeds hidden RDRAM to 3 in rdram_init(). So the raw post-init hidden buffers
 * are byte-identical (the A.5 re-port fixed the earlier seed-4 divergence). The
 * dedicated assertion lives in rdp_post_init_hidden_oracle_test; here we still
 * clear/randomize to a common state before replay, exactly as every real
 * conformance test does (see clear_rdram / randomize_rdram).
 *
 * No pixel logic is exercised (Stream B). Exit code 0 on success, nonzero on any
 * mismatch or wiring failure; the ctest `conformance.m0_empty` asserts on it.
 *
 * Host-only test infrastructure (NOT orthodox-enforced).
 */

#include <stdio.h>

#include "conformance_compare.hpp"
#include "portable_rng.hpp"
#include "replayer_driver_ours.hpp"
#include "replayer_state.hpp"

namespace RDP
{
std::unique_ptr<ReplayerDriver> create_replayer_driver_angrylion(CommandInterface &player,
                                                                 ReplayerEventInterface &iface);
}

using namespace RDP;

static bool check_equal(ReplayerState &state, const char *stage)
{
	uint32_t fault_addr = 0;
	bool fault_hidden = false;
	if (!compare_rdram(*state.reference, *state.gpu, &fault_addr, &fault_hidden))
	{
		fprintf(stderr,
		        "M0 MISMATCH at stage '%s': %s RDRAM differs at byte/word %u.\n",
		        stage, fault_hidden ? "hidden" : "visible", fault_addr);
		return false;
	}
	printf("M0 stage '%s': RDRAM + hidden RDRAM EQUAL.\n", stage);
	return true;
}

int main()
{
	ReplayerState state;
	state.make_reference = create_replayer_driver_angrylion;
	state.make_gpu = create_replayer_driver_ours;

	if (!state.init())
	{
		fprintf(stderr, "M0 FAIL: ReplayerState::init() failed (driver wiring).\n");
		return 1;
	}

	// Sanity: sizes must match so compare_rdram is meaningful.
	if (state.reference->get_rdram_size() != state.gpu->get_rdram_size() ||
	    state.reference->get_hidden_rdram_size() != state.gpu->get_hidden_rdram_size())
	{
		fprintf(stderr,
		        "M0 FAIL: buffer sizes differ. rdram %zu/%zu, hidden %zu/%zu.\n",
		        state.reference->get_rdram_size(), state.gpu->get_rdram_size(),
		        state.reference->get_hidden_rdram_size(), state.gpu->get_hidden_rdram_size());
		return 1;
	}

	// Post-init buffers should already be EQUAL now that both renderers seed
	// hidden RDRAM to 3 (same canonical 31bdb1f fork). Log it; the dedicated
	// byte-for-byte assertion is rdp_post_init_hidden_oracle_test.
	{
		uint32_t fa = 0;
		bool fh = false;
		if (compare_rdram(*state.reference, *state.gpu, &fa, &fh))
			printf("M0 note: post-init buffers already equal (both seed hidden=3).\n");
		else
			printf("M0 note: post-init buffers differ at %s %u; clearing to common state.\n",
			       fh ? "hidden" : "visible", fa);
	}

	// Stage 1: clear both, then drive an empty command stream (no commands).
	clear_rdram(*state.reference);
	clear_rdram(*state.gpu);
	state.combined->eof();
	if (!check_equal(state, "empty-stream-cleared"))
		return 1;

	// Stage 2: identically randomize both, run the empty stream again. Proves
	// the compare exercises non-zero buffers (not a trivial all-zero pass).
	RNG rng;
	randomize_rdram(rng, *state.reference, *state.gpu);
	state.combined->eof();
	if (!check_equal(state, "empty-stream-randomized"))
		return 1;

	printf("M0 PASS: oracle and our renderer identical on empty buffers.\n");
	return 0;
}
