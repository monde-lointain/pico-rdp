#pragma once

// Adapted from angrylion-rdp-plus src/core/common.h.
// Endianness / inline / SIGN machinery preserved VERBATIM
// (BYTE_ADDR_XOR=3, WORD_ADDR_XOR=1 under LSB_FIRST).

#include <stdint.h>

// endianness
#define LSB_FIRST 1
#ifdef LSB_FIRST
#define BYTE_ADDR_XOR 3
#define WORD_ADDR_XOR 1
#define BYTE4_XOR_BE(a) ((a) ^ BYTE_ADDR_XOR)
#else
#define BYTE_ADDR_XOR 0
#define WORD_ADDR_XOR 0
#define BYTE4_XOR_BE(a) (a)
#endif

#ifdef LSB_FIRST
#define BYTE_XOR_DWORD_SWAP 7
#define WORD_XOR_DWORD_SWAP 3
#else
#define BYTE_XOR_DWORD_SWAP 4
#define WORD_XOR_DWORD_SWAP 2
#endif

#define DWORD_XOR_DWORD_SWAP 1

// inlining
#define INLINE inline

#ifdef _MSC_VER
#define STRICTINLINE __forceinline
#elif defined(__GNUC__)
#define STRICTINLINE __attribute__((always_inline)) inline
#else
#define STRICTINLINE inline
#endif

// misc
#define UNUSED(x) (void)(x)

// irand: linear-congruential RNG used by the dither/noise paths.
// Defined in angrylion n64video.c; lifted here verbatim (math unchanged) so the
// sequence is unit-testable in isolation. Worker seed is 3 + worker_id*13.
static STRICTINLINE uint32_t irand(uint32_t* state) {
  *state = (*state * 0x343fd) + 0x269ec3;
  return ((*state >> 16) & 0x7fff);
}
