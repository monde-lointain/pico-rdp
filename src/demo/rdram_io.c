// XOR-correct RDRAM byte I/O — see rdram_io.h.
//
// All widths reduce to the per-byte swizzle `phys = off ^ BYTE_ADDR_XOR`
// (BYTE_ADDR_XOR=3), which is byte-equivalent to the renderer's halfword
// (`^ WORD_ADDR_XOR`, =1) and 32-bit (plain) accessors in src/rdp/rdp/rdram.c.
// We hardcode the constant rather than include renderer-private headers, so
// this stays freestanding and owned by demo_core.

#include "rdram_io.h"

#define RDRAM_BYTE_ADDR_XOR 3u

void rdram_write8(uint8_t *rdram, uint32_t off, uint8_t val) {
  rdram[off ^ RDRAM_BYTE_ADDR_XOR] = val;
}

uint8_t rdram_read8(const uint8_t *rdram, uint32_t off) {
  return rdram[off ^ RDRAM_BYTE_ADDR_XOR];
}

void rdram_write16(uint8_t *rdram, uint32_t off, uint16_t val) {
  // N64-native big-endian: high byte at the lower offset. Each byte is swizzled
  // independently so the renderer's `rdram16[(off/2) ^ 1]` read recovers `val`.
  rdram[(off + 0) ^ RDRAM_BYTE_ADDR_XOR] = (uint8_t)((val >> 8) & 0xffU);
  rdram[(off + 1) ^ RDRAM_BYTE_ADDR_XOR] = (uint8_t)(val & 0xffU);
}

uint16_t rdram_read16(const uint8_t *rdram, uint32_t off) {
  uint16_t hi = rdram[(off + 0) ^ RDRAM_BYTE_ADDR_XOR];
  uint16_t lo = rdram[(off + 1) ^ RDRAM_BYTE_ADDR_XOR];
  return (uint16_t)((hi << 8) | lo);
}

void rdram_write_block(uint8_t *rdram, uint32_t dst_off, const void *src,
                       uint32_t n) {
  const uint8_t *s = (const uint8_t *)src;
  for (uint32_t i = 0; i < n; ++i) {
    rdram[(dst_off + i) ^ RDRAM_BYTE_ADDR_XOR] = s[i];
  }
}

void rdram_read_block(const uint8_t *rdram, uint32_t src_off, void *dst,
                      uint32_t n) {
  uint8_t *d = (uint8_t *)dst;
  for (uint32_t i = 0; i < n; ++i) {
    d[i] = rdram[(src_off + i) ^ RDRAM_BYTE_ADDR_XOR];
  }
}
