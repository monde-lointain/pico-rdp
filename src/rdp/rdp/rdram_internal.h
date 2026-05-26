#pragma once

// rdram_internal.h — RDRAM interface shared with the other RDP TUs.
//
// Declares the rdram access macros + the helper-function / storage signatures
// that rdram.cc defines. Consumers (fbuffer, vi/*) include this to get the
// RREAD*/RWRITE*/PAIR* macros (which expand to the helper calls) and the direct
// helper declarations (vi/restore calls rdram_read_idx16/32[_fast] and
// rdram_valid_idx16/32 directly).
//
// Symbol hygiene vs the Angrylion oracle: `nm` on the oracle shows it exports
// only `rdram_hidden`, `state`, `get_tmem`, `z_init_lut` — its idxlim*/rdram*/
// rdram_init/access-helpers are file-local (unity build), so OURS do not
// collide and keep their original names. Only `rdram_hidden` is renamed
// (rdpxi_-prefixed) because the oracle exports it.
//
// The helpers are de-inlined (plain functions, not STRICTINLINE) so they can be
// called across TU boundaries; cross-TU inlining is recovered by LTO
// (INTERPROCEDURAL_OPTIMIZATION on rdp_core). The rdram micro-tests bypass this
// header and #include rdram.cc directly with RDRAM_STANDALONE (all-static, so
// the full + bounded builds coexist in one executable).

#include <stdint.h>

#include "n64video.h"  // RDRAM_MAX_SIZE

#define RDRAM_MASK 0x00ffffff

// macros used to interface with AL's code
#define RREADADDR8(rdst, in) \
  { (rdst) = rdram_read_idx8((in)); }
#define RREADIDX16(rdst, in) \
  { (rdst) = rdram_read_idx16((in)); }
#define RREADIDX32(rdst, in) \
  { (rdst) = rdram_read_idx32((in)); }

#define RWRITEADDR8(in, val) rdram_write_idx8((in), (val))
#define RWRITEIDX16(in, val) rdram_write_idx16((in), (val))
#define RWRITEIDX32(in, val) rdram_write_idx32((in), (val))

#define PAIRREAD16(rdst, hdst, in) rdram_read_pair16(&(rdst), &(hdst), (in))

#define PAIRWRITE16(in, rval, hval) rdram_write_pair16((in), (rval), (hval))

#define PAIRWRITE32(in, rval, hval0, hval1) \
  rdram_write_pair32((in), (rval), (hval0), (hval1))

#define PAIRWRITE8(in, rval, hval) rdram_write_pair8((in), (rval), (hval))

// backing storage (defined in rdram.cc)
extern uint32_t idxlim8;
extern uint32_t idxlim16;
extern uint32_t idxlim32;
extern uint32_t* rdram32;
extern uint16_t* rdram16;
extern uint8_t* rdram8;
extern uint8_t rdpxi_rdram_hidden[RDRAM_MAX_SIZE /
                                  2];  // renamed: oracle exports `rdram_hidden`

void rdram_init(void);

// access helpers (defined in rdram.cc; de-inlined for cross-TU use, LTO
// inlines)
bool rdram_valid_idx8(uint32_t in);
bool rdram_valid_idx16(uint32_t in);
bool rdram_valid_idx32(uint32_t in);
uint8_t rdram_read_idx8(uint32_t in);
uint8_t rdram_read_idx8_fast(uint32_t in);
uint16_t rdram_read_idx16(uint32_t in);
uint16_t rdram_read_idx16_fast(uint32_t in);
uint32_t rdram_read_idx32(uint32_t in);
uint32_t rdram_read_idx32_fast(uint32_t in);
void rdram_write_idx8(uint32_t in, uint8_t val);
void rdram_write_idx16(uint32_t in, uint16_t val);
void rdram_write_idx32(uint32_t in, uint32_t val);
void rdram_read_pair16(uint16_t* rdst, uint8_t* hdst, uint32_t in);
void rdram_write_pair8(uint32_t in, uint8_t rval, uint8_t hval);
void rdram_write_pair16(uint32_t in, uint16_t rval, uint8_t hval);
void rdram_write_pair32(uint32_t in, uint32_t rval, uint8_t hval0,
                        uint8_t hval1);
