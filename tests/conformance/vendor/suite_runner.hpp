/* Suite registry + main-less runner for the conformance harness (A.2).
 *
 * Upstream rdp_conformance.cpp / vi_conformance.cpp each had their own main()
 * that built a local std::vector<Suite>, parsed CLI via Granite's cli_parser,
 * created a Vulkan ReplayerState, and looped the suites. That couples suite
 * registration to a main() and to Vulkan.
 *
 * This header re-hosts the shared scaffolding so the A.0 conformance executable
 * can own main(), build a Vulkan-free ReplayerState (with injected driver
 * factories), fetch the suite lists, and drive them:
 *
 *     std::vector<Suite> suites = register_rdp_suites();   // or register_vi_suites()
 *     // ... or list them for --list-suites
 *     ReplayerState state; state.make_reference = ...; state.make_gpu = ...;
 *     state.init();
 *     run_suites(state, suites, args);
 *
 * The Arguments fields and the run/skip/glob loop match upstream so behaviour
 * (ranges, --suite / --suite-glob selection, --list-suites output) is identical.
 *
 * NOT orthodox-enforced (vendored host-only test infrastructure).
 */

#pragma once

#include <string>
#include <vector>

#include "conformance_compare.hpp"
#include "logging.hpp"
#include "replayer_state.hpp"

namespace RDP
{
// Shared CLI argument bag. `hi` default differs per suite kind upstream
// (rdp: 10, vi: 32); callers set it after construction as needed.
struct Arguments
{
	std::string suite_glob;
	std::string suite;
	unsigned lo = 0;
	unsigned hi = 10;
	bool verbose = false;
	bool capture = false;  // renderdoc unavailable in the Vulkan-free host build.
};

struct Suite
{
	std::string name;
	bool (*func)(ReplayerState &state, const Arguments &args);
};

// Built by the re-hosted suite source files. Definitions live in
// rdp_conformance_host.cpp / vi_conformance_host.cpp.
std::vector<Suite> register_rdp_suites();
std::vector<Suite> register_vi_suites();

// Print every suite name (the --list-suites behaviour). Kept here so the A.0
// exe shares one implementation for both suite kinds.
static inline void list_suites(const std::vector<Suite> &suites)
{
	for (auto &suite : suites)
		LOGI("Suite: %s\n", suite.name.c_str());
}

// Run the matching suites against an already-initialized ReplayerState. Returns
// true iff at least one suite matched AND all matching suites passed. Mirrors
// the upstream main_inner() loop (run/skip logging, glob vs exact selection).
static inline bool run_suites(ReplayerState &state, const std::vector<Suite> &suites, const Arguments &args)
{
	bool did_work = false;
	for (auto &suite : suites)
	{
		bool cmp;
		if (!args.suite.empty())
			cmp = suite_compare(suite.name, args.suite);
		else
			cmp = suite_compare_glob(suite.name, args.suite_glob);

		if (cmp)
		{
			did_work = true;
			LOGI("\n");
			LOGI("================================================\n");
			LOGI("Running suite: %s\n", suite.name.c_str());
			LOGI("------------------------------------------------\n");

			if (!suite.func(state, args))
			{
				LOGE(" ... Suite failed.\n");
				return false;
			}
			else
				LOGI("====== PASSED ======\n");

			LOGI("\n\n");
		}
		else
			LOGI("Skipping suite: %s\n", suite.name.c_str());
	}

	if (!did_work)
	{
		LOGE("No suite matches.\n");
		return false;
	}

	return true;
}
}
