/* Stream C / M8 — VI conformance suite runner (host, Vulkan-free).
 *
 * Drives the VI conformance suites (register_vi_suites, A.2) against the
 * Angrylion oracle (reference, A.3) and OUR rdp_core VI (gpu, Stream C) side by
 * side, comparing the VI scanout image (compare_image) each iteration.
 *
 * Mirrors suite_runner_main.cpp (the RDP runner) but:
 *   - fetches register_vi_suites() instead of register_rdp_suites(),
 *   - defaults args.hi = 32 (the upstream VI default; vs 10 for rdp), and
 *   - wires make_gpu_scaled to our no-op scaled stub (the vendored VI host
 *     dereferences state.gpu_scaled unconditionally in set_default_vi_registers;
 *     our engine is a singleton with no upscaler, so the stub merely satisfies
 *     that dereference — see replayer_driver_ours.cpp).
 *
 * UPSCALE / PER-SCANLINE FINDING: the four per-scanline-xh* suites compare
 * state.gpu (or gpu_scaled) against a SYNTHETIC reference image, exercising
 * parallel-rdp's GPU-only per-scanline VI register table + upscaler. The
 * Angrylion-derived VI (and thus our port, and the oracle itself) has no
 * per-scanline register support — set_vi_register_for_scanline is a no-op — so
 * those suites are N/A for a native renderer and are intentionally NOT
 * registered as ctests. The 21 native aa-* suites (native-vs-native) are.
 *
 * Host-only test infrastructure (NOT orthodox-enforced).
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "replayer_driver_ours.hpp"
#include "replayer_state.hpp"
#include "suite_runner.hpp"

namespace RDP
{
std::unique_ptr<ReplayerDriver> create_replayer_driver_angrylion(CommandInterface &player,
                                                                 ReplayerEventInterface &iface);
}

using namespace RDP;

int main(int argc, char **argv)
{
	Arguments args;
	args.hi = 32;  // upstream VI default (vs 10 for rdp).
	bool list_only = false;

	for (int i = 1; i < argc; i++)
	{
		const char *a = argv[i];
		if (!strcmp(a, "--suite") && i + 1 < argc)
			args.suite = argv[++i];
		else if (!strcmp(a, "--suite-glob") && i + 1 < argc)
			args.suite_glob = argv[++i];
		else if (!strcmp(a, "--range") && i + 2 < argc)
		{
			args.lo = unsigned(strtoul(argv[++i], nullptr, 0));
			args.hi = unsigned(strtoul(argv[++i], nullptr, 0));
		}
		else if (!strcmp(a, "--list-suites"))
			list_only = true;
		else if (!strcmp(a, "--verbose"))
			args.verbose = true;
		else
		{
			fprintf(stderr, "Unknown argument: %s\n", a);
			return 2;
		}
	}

	std::vector<Suite> suites = register_vi_suites();

	if (list_only)
	{
		list_suites(suites);
		return 0;
	}

	ReplayerState state;
	state.make_reference = create_replayer_driver_angrylion;
	state.make_gpu = create_replayer_driver_ours;
	state.make_gpu_scaled = create_replayer_driver_ours_scaled;

	if (!state.init())
	{
		fprintf(stderr, "FAIL: ReplayerState::init() failed (driver wiring).\n");
		return 1;
	}

	if (!run_suites(state, suites, args))
		return 1;

	return 0;
}
