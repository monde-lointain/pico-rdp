#pragma once

// XOR-correct RDRAM byte I/O for the demo arena.
//
// The RDP renderer accesses RDRAM through a big-endian-in-little-endian byte
// swizzle (see src/rdp/rdp/rdram.c, src/rdp/n64video_common.h): a logical byte
// at offset `i` physically lives at `rdram8[i ^ BYTE_ADDR_XOR]` with
// BYTE_ADDR_XOR=3 (halfwords at `rdram16[i ^ WORD_ADDR_XOR]`, WORD_ADDR_XOR=1;
// these are byte-equivalent to the byte XOR). So a plain memcpy into the arena
// reads back scrambled by the renderer's LOAD_TILE / LOAD_TLUT / FB reads.
//
// These helpers copy a contiguous host byte stream INTO and OUT OF the arena so
// that contiguous source byte `i` written at byte offset `off` lands at
// `rdram[(off + i) ^ 3]` — i.e. the renderer's own read formula recovers it.
// Freestanding: no malloc / printf / host deps; safe for device builds.

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Store one byte so the renderer's `rdram8[off ^ 3]` read recovers it.
void rdram_write8(uint8_t *rdram, uint32_t off, uint8_t val);

// Load one byte as the renderer would via `rdram8[off ^ 3]`.
uint8_t rdram_read8(const uint8_t *rdram, uint32_t off);

// Store one N64-native BIG-ENDIAN halfword at byte offset `off` (must be even)
// so the renderer's `rdram16[(off/2) ^ WORD_ADDR_XOR]` read recovers `val`.
// On N64, RDRAM halfwords (RGBA5551 pixels, TLUT entries) are big-endian: the
// high byte sits at the lower byte offset. (A little-endian byte pair would be
// read back byte-swapped, because the per-byte `^3` swizzle is the transpose of
// the per-halfword `^1` swizzle.)
void rdram_write16(uint8_t *rdram, uint32_t off, uint16_t val);

// Load one N64-native big-endian halfword from byte offset `off` (must be even);
// inverse of rdram_write16, matching the renderer's `rdram16[(off/2) ^ 1]` read.
uint16_t rdram_read16(const uint8_t *rdram, uint32_t off);

// Copy `n` contiguous source bytes INTO the arena starting at byte offset
// `dst_off`, applying the byte XOR per byte: src[i] -> rdram[(dst_off+i) ^ 3].
// Any alignment / length.
void rdram_write_block(uint8_t *rdram, uint32_t dst_off, const void *src,
                       uint32_t n);

// Copy `n` bytes OUT OF the arena starting at byte offset `src_off` into a
// contiguous destination, undoing the byte XOR: dst[i] = rdram[(src_off+i) ^ 3].
// Inverse of rdram_write_block.
void rdram_read_block(const uint8_t *rdram, uint32_t src_off, void *dst,
                      uint32_t n);

#ifdef __cplusplus
}  // extern "C"
#endif
