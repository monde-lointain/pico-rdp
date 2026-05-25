/* Portable, deterministic RNG for the parallel-rdp conformance harness (A.2).
 *
 * Upstream conformance_utils.hpp defines:
 *
 *     struct RNG {
 *         std::mt19937 rnd {1337};
 *         std::uniform_real_distribution<float> udist{0.0, 1.0};
 *         float generate(float lo, float hi) { return udist(rnd) * (hi-lo) + lo; }
 *         bool  boolean() { return (rnd() & 1) != 0; }
 *     };
 *
 * std::mt19937 is fully specified by the C++ standard, so rnd() is identical on
 * libstdc++, libc++ and MSVC. std::uniform_real_distribution, however, is NOT:
 * the standard leaves its algorithm unspecified, so generate() yields different
 * float test vectors per stdlib. Because the conformance suite hashes test
 * vectors into bit-exact RDRAM compares, that divergence makes a vector
 * generated on one toolchain fail on another.
 *
 * This RNG keeps std::mt19937{1337} verbatim (same engine, same seed, same
 * call sequence) but replaces the distribution with a hand-rolled one that
 * consumes exactly one rnd() draw per generate() call -- the same number of
 * draws the original made -- and maps it to [0,1) deterministically. Engine
 * state therefore advances identically to upstream, and the float mapping is
 * defined purely in terms of standardized integer output, so vectors match on
 * every stdlib.
 *
 * Mapping: take the top 24 bits of the 32-bit mt19937 word (float has a 24-bit
 * significand) and divide by 2^24, giving a value in [0,1) with no rounding
 * surprises. boolean() is unchanged.
 *
 * NOT orthodox-enforced (vendored host-only test infrastructure).
 */

#pragma once

#include <random>
#include <stdint.h>

namespace RDP
{
struct RNG
{
	std::mt19937 rnd{1337};

	// Deterministic uniform float in [0, 1). One mt19937 draw, integer-only
	// mapping -> identical across libstdc++ / libc++ / MSVC.
	inline float canonical()
	{
		// mt19937 produces a uniform 32-bit word; keep the high 24 bits.
		uint32_t bits = uint32_t(rnd()) >> 8;        // 24 random bits, [0, 2^24)
		return float(bits) * (1.0f / 16777216.0f);   // / 2^24 -> [0, 1)
	}

	inline float generate(float lo, float hi)
	{
		return canonical() * (hi - lo) + lo;
	}

	inline bool boolean()
	{
		return (rnd() & 1) != 0;
	}
};
}
