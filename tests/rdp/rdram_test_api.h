#pragma once

// Prototypes for the rdram.c test accessors. Two parallel families:
//   rdram_full_*    — default full-size hidden mode (rdram_test_tu.cc)
//   rdram_bounded_* — RDP_HIDDEN_BOUNDED mode       (rdram_bounded_test_tu.cc)
// Generated from rdram_test_accessors.inc via the RDRAM_TEST_PREFIX macro.

#include <stdint.h>

extern "C" {

#define RDRAM_TEST_DECL(P)                                                     \
    void     P##init(uint8_t* backing, uint32_t size_bytes);                   \
    uint8_t  P##read_idx8(uint32_t in);                                        \
    uint16_t P##read_idx16(uint32_t in);                                       \
    uint32_t P##read_idx32(uint32_t in);                                       \
    void     P##write_idx8(uint32_t in, uint8_t v);                            \
    void     P##write_idx16(uint32_t in, uint16_t v);                          \
    void     P##write_idx32(uint32_t in, uint32_t v);                          \
    void     P##read_pair16(uint16_t* rdst, uint8_t* hdst, uint32_t in);       \
    void     P##write_pair8(uint32_t in, uint8_t rval, int flip,               \
                            int* delayedhbwidx);                               \
    void     P##write_pair16(uint32_t in, uint16_t rval, uint8_t hval,         \
                             int iscolor);                                     \
    void     P##write_pair32(uint32_t in, uint32_t rval, uint8_t hval0,        \
                             uint8_t hval1);                                   \
    void     P##complete_delayed_hbwrites(int delayedhbwidx);                  \
    uint8_t  P##backing_byte(uint32_t byte_off);                               \
    uint8_t  P##hidden(uint32_t hidx);

RDRAM_TEST_DECL(rdram_full_)
RDRAM_TEST_DECL(rdram_bounded_)

// bounded-only: set the hidden buffer base (in hidden-index units)
void rdram_bounded_set_hidden_base(uint32_t base);

#undef RDRAM_TEST_DECL

} // extern "C"
