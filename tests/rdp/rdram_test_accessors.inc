// Shared accessor body for the rdram.c test TUs. Included by both the
// full-size TU (rdram_test_tu.cc) and the bounded TU (rdram_bounded_test_tu.cc).
//
// Requires the includer to define:
//   RDRAM_TEST_PREFIX(name)  -> namespaced C symbol (e.g. rdram_full_##name)
// and optionally:
//   RDP_HIDDEN_BOUNDED [+ RDP_HIDDEN_BOUNDED_WORDS] before including this file.

#include <stdint.h>
#include <stdbool.h>
#include <string.h>

#include "n64video.h"
#include "n64video_common.h"

// Private per-TU config + rdram instance. rdram.cc is #included with
// RDRAM_STANDALONE so every rdram symbol is file-static — the full and bounded
// builds therefore link into one executable without colliding. rdram_init()
// reads rdpxi_config.gfx.rdram / .rdram_size (this static stand-in, supplied
// before the include so rdram.cc sees it).
static struct N64videoConfig rdpxi_config;

#define RDRAM_STANDALONE
#include "rdp/rdram.cc"
#undef RDRAM_STANDALONE

#define P(name) RDRAM_TEST_PREFIX(name)

extern "C" {

void P(init)(uint8_t* backing, uint32_t size_bytes)
{
    memset(&rdpxi_config, 0, sizeof(rdpxi_config));
    rdpxi_config.gfx.rdram = backing;
    rdpxi_config.gfx.rdram_size = size_bytes;
    rdram_init();
}

#ifdef RDP_HIDDEN_BOUNDED
void P(set_hidden_base)(uint32_t base)
{
    rdram_hidden_set_base(base);
}
#endif

uint8_t  P(read_idx8)(uint32_t in)               { return rdram_read_idx8(in); }
uint16_t P(read_idx16)(uint32_t in)              { return rdram_read_idx16(in); }
uint32_t P(read_idx32)(uint32_t in)              { return rdram_read_idx32(in); }
void     P(write_idx8)(uint32_t in, uint8_t v)   { rdram_write_idx8(in, v); }
void     P(write_idx16)(uint32_t in, uint16_t v) { rdram_write_idx16(in, v); }
void     P(write_idx32)(uint32_t in, uint32_t v) { rdram_write_idx32(in, v); }

void P(read_pair16)(uint16_t* rdst, uint8_t* hdst, uint32_t in)
{
    rdram_read_pair16(rdst, hdst, in);
}
void P(write_pair8)(uint32_t in, uint8_t rval, uint8_t hval)
{
    rdram_write_pair8(in, rval, hval);
}
void P(write_pair16)(uint32_t in, uint16_t rval, uint8_t hval)
{
    rdram_write_pair16(in, rval, hval);
}
void P(write_pair32)(uint32_t in, uint32_t rval, uint8_t hval0, uint8_t hval1)
{
    rdram_write_pair32(in, rval, hval0, hval1);
}

// raw byte peek of the visible backing (no XOR applied) for swizzle assertions
uint8_t P(backing_byte)(uint32_t byte_off)
{
    return rdram8[byte_off];
}

// peek a hidden entry by ABSOLUTE hidden index (routes through the same remap
// used internally, so it works identically in full and bounded modes)
uint8_t P(hidden)(uint32_t hidx)
{
    return *hidden_at(hidx);
}

} // extern "C"

#undef P
