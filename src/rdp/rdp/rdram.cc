// rdram.cc — RDRAM memory interface (storage + access helpers + init).
//
// Ported VERBATIM from the canonical fork (parallel-rdp angrylion-rdp-plus
// @ 31bdb1f, src/core/n64video/rdp/rdram.c). The hidden-RDRAM seed (memset 3),
// XOR addressing, and simple pair read/write paths are preserved byte-for-byte.
//
// Two build modes:
//   default (rdp_core)     external linkage; helpers are plain functions
//                          (de-inlined, LTO recovers cross-TU inlining). The
//                          interface macros + helper/storage decls are in
//                          rdram_internal.h, which consumers include.
//   RDRAM_STANDALONE       everything file-static (helpers STRICTINLINE); the
//                          rdram micro-tests #include this file directly so the
//                          full + bounded modes coexist in one executable, each
//                          with a private rdram instance. The includer supplies
//                          a static `rdpxi_config` before #include-ing this.
//
// Symbol hygiene: only `rdram_hidden` is renamed (rdpxi_rdram_hidden) — the
// oracle exports it. idxlim*/rdram*/rdram_init/access-helpers are file-local in
// the oracle (unity build), so ours keep their names without colliding.
//
// Boundable hidden-RDRAM (RDP_HIDDEN_BOUNDED): hidden storage is a small buffer
// covering one contiguous region, remapped by rdpxi_rdram_hidden_base. Every
// hidden access routes through hidden_at(), so results are BYTE-IDENTICAL to
// the full-size array for any access confined to the bounded region.

#include <string.h>

#include "n64video.h"         // struct N64videoConfig, RDRAM_MAX_SIZE
#include "n64video_common.h"  // STRICTINLINE, BYTE_ADDR_XOR, WORD_ADDR_XOR

#ifdef RDRAM_STANDALONE
#define RDRAM_FN static STRICTINLINE
#define RDRAM_VAR static
#else
#define RDRAM_FN
#define RDRAM_VAR
extern struct N64videoConfig rdpxi_config;
#endif

#define RDRAM_MASK 0x00ffffff

// pointer indexing limits for aliasing RDRAM reads and writes
RDRAM_VAR uint32_t idxlim8;
RDRAM_VAR uint32_t idxlim16;
RDRAM_VAR uint32_t idxlim32;

RDRAM_VAR uint32_t* rdram32;
RDRAM_VAR uint16_t* rdram16;
RDRAM_VAR uint8_t* rdram8;

#ifdef RDP_HIDDEN_BOUNDED

#ifndef RDP_HIDDEN_BOUNDED_WORDS
#define RDP_HIDDEN_BOUNDED_WORDS (RDRAM_MAX_SIZE / 2)
#endif

RDRAM_VAR uint8_t rdpxi_rdram_hidden[RDP_HIDDEN_BOUNDED_WORDS];
RDRAM_VAR uint32_t rdpxi_rdram_hidden_base;  // hidden index of bounded entry 0

static STRICTINLINE uint8_t* hidden_at(uint32_t h) {
  // Remap absolute hidden index to the bounded buffer. Accesses confined to
  // the region [base, base + RDP_HIDDEN_BOUNDED_WORDS) hit the same byte they
  // would in the full-size array.
  return &rdpxi_rdram_hidden[h - rdpxi_rdram_hidden_base];
}

RDRAM_FN void rdram_hidden_set_base(uint32_t base) {
  rdpxi_rdram_hidden_base = base;
}

#else

// rdpxi_-prefixed (the oracle exports `rdram_hidden`); reached only via the
// rdpx_get_hidden_rdram() accessor.
RDRAM_VAR uint8_t rdpxi_rdram_hidden[RDRAM_MAX_SIZE / 2];

static STRICTINLINE uint8_t* hidden_at(uint32_t h) {
  return &rdpxi_rdram_hidden[h];
}

#endif  // RDP_HIDDEN_BOUNDED

RDRAM_FN void rdram_init(void) {
  idxlim8 = rdpxi_config.gfx.rdram_size - 1;
  idxlim16 = (idxlim8 >> 1) & 0xffffffU;
  idxlim32 = (idxlim8 >> 2) & 0xffffffU;

  rdram32 = (uint32_t*)rdpxi_config.gfx.rdram;
  rdram16 = (uint16_t*)rdpxi_config.gfx.rdram;
  rdram8 = rdpxi_config.gfx.rdram;

  memset(rdpxi_rdram_hidden, 3, sizeof(rdpxi_rdram_hidden));
}

RDRAM_FN bool rdram_valid_idx8(uint32_t in) { return in <= idxlim8; }

RDRAM_FN bool rdram_valid_idx16(uint32_t in) { return in <= idxlim16; }

RDRAM_FN bool rdram_valid_idx32(uint32_t in) { return in <= idxlim32; }

RDRAM_FN uint8_t rdram_read_idx8(uint32_t in) {
  in &= RDRAM_MASK;
  return rdram_valid_idx8(in) ? rdram8[in ^ BYTE_ADDR_XOR] : 0;
}

RDRAM_FN uint8_t rdram_read_idx8_fast(uint32_t in) {
  return rdram8[in ^ BYTE_ADDR_XOR];
}

RDRAM_FN uint16_t rdram_read_idx16(uint32_t in) {
  in &= RDRAM_MASK >> 1;
  return rdram_valid_idx16(in) ? rdram16[in ^ WORD_ADDR_XOR] : 0;
}

RDRAM_FN uint16_t rdram_read_idx16_fast(uint32_t in) {
  return rdram16[in ^ WORD_ADDR_XOR];
}

RDRAM_FN uint32_t rdram_read_idx32(uint32_t in) {
  in &= RDRAM_MASK >> 2;
  return rdram_valid_idx32(in) ? rdram32[in] : 0;
}

RDRAM_FN uint32_t rdram_read_idx32_fast(uint32_t in) { return rdram32[in]; }

RDRAM_FN void rdram_write_idx8(uint32_t in, uint8_t val) {
  in &= RDRAM_MASK;
  if (rdram_valid_idx8(in)) {
    rdram8[in ^ BYTE_ADDR_XOR] = val;
  }
}

RDRAM_FN void rdram_write_idx16(uint32_t in, uint16_t val) {
  in &= RDRAM_MASK >> 1;
  if (rdram_valid_idx16(in)) {
    rdram16[in ^ WORD_ADDR_XOR] = val;
  }
}

RDRAM_FN void rdram_write_idx32(uint32_t in, uint32_t val) {
  in &= RDRAM_MASK >> 2;
  if (rdram_valid_idx32(in)) {
    rdram32[in] = val;
  }
}

RDRAM_FN void rdram_read_pair16(uint16_t* rdst, uint8_t* hdst, uint32_t in) {
  in &= RDRAM_MASK >> 1;
  if (rdram_valid_idx16(in)) {
    *rdst = rdram16[in ^ WORD_ADDR_XOR];
    *hdst = *hidden_at(in);
  } else {
    *rdst = *hdst = 0;
  }
}

RDRAM_FN void rdram_write_pair8(uint32_t in, uint8_t rval, uint8_t hval) {
  in &= RDRAM_MASK;
  if (rdram_valid_idx8(in)) {
    rdram8[in ^ BYTE_ADDR_XOR] = rval;
    if (in & 1) {
      *hidden_at(in >> 1) = hval;
    }
  }
}

RDRAM_FN void rdram_write_pair16(uint32_t in, uint16_t rval, uint8_t hval) {
  in &= RDRAM_MASK >> 1;
  if (rdram_valid_idx16(in)) {
    rdram16[in ^ WORD_ADDR_XOR] = rval;
    *hidden_at(in) = hval;
  }
}

RDRAM_FN void rdram_write_pair32(uint32_t in, uint32_t rval, uint8_t hval0,
                                 uint8_t hval1) {
  in &= RDRAM_MASK >> 2;
  if (rdram_valid_idx32(in)) {
    rdram32[in] = rval;
    *hidden_at(in << 1) = hval0;
    *hidden_at((in << 1) + 1) = hval1;
  }
}
