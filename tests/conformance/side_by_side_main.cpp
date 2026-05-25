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
 * NOTE on the post-init hidden seed: the A.3 oracle is pinned to the OLDER
 * Themaister angrylion fork, which seeds hidden RDRAM to 3 in rdram_init(). Our
 * rdram.c (ported by A.5 from the NEWER fork) seeds it to HB_CLEAN (4). So the
 * raw post-init hidden buffers DIFFER by construction. This is a known A.5-vs-A.3
 * fork divergence (RE-SURFACED to A.5). It does NOT affect the M0 wiring proof:
 * we clear both to a common state first, which is exactly what every real
 * conformance test does before replay (see clear_rdram / randomize_rdram). The
 * post-init values are logged for visibility but not asserted.
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

	// Post-init divergence is EXPECTED (A.5 newer-fork seed 4 vs A.3 older-fork
	// seed 3); log it but do not assert. The wiring proof starts from a common
	// cleared state, exactly as every real conformance replay does.
	{
		uint32_t fa = 0;
		bool fh = false;
		if (compare_rdram(*state.reference, *state.gpu, &fa, &fh))
			printf("M0 note: post-init buffers already equal.\n");
		else
			printf("M0 note: post-init hidden seed differs (expected: A.5 fork=4 "
			       "vs A.3 fork=3); clearing to common state.\n");
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
