/* Stream B.1 — conformance suite runner (host, Vulkan-free).
 *
 * Drives the rasterization/load conformance suites (register_rdp_suites, A.2)
 * against the Angrylion oracle (reference, A.3) and OUR rdp_core renderer (gpu,
 * A.0/B.1) side by side, comparing RDRAM byte-for-byte each iteration.
 *
 * CLI (subset of upstream rdp_conformance.cpp, parsed without Granite):
 *   --suite NAME        run exactly the named suite (else: all suites)
 *   --suite-glob GLOB   run suites matching GLOB
 *   --range LO HI       iteration range [LO, HI)
 *   --list-suites       print suite names and exit
 *   --verbose           per-iteration logging
 *
 * Exit code 0 iff at least one suite matched and all matching suites passed.
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

	std::vector<Suite> suites = register_rdp_suites();

	if (list_only)
	{
		list_suites(suites);
		return 0;
	}

	ReplayerState state;
	state.make_reference = create_replayer_driver_angrylion;
	state.make_gpu = create_replayer_driver_ours;

	if (!state.init())
	{
		fprintf(stderr, "FAIL: ReplayerState::init() failed (driver wiring).\n");
		return 1;
	}

	if (!run_suites(state, suites, args))
		return 1;

	return 0;
}
