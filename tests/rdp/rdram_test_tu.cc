// Test translation unit for rdram.c (FULL-SIZE hidden mode = default).
//
// rdram.c is NOT a standalone library source; in the real renderer it is
// #included into n64video.c under N64VIDEO_C (single-TU model). Here we
// reproduce that minimal environment: define N64VIDEO_C, supply the `config`
// global and the common.h macros, #include rdram.c, then expose thin C
// accessors so gtest TUs (not orthodox-enforced) can exercise it.
//
// Compiled with -fno-strict-aliasing (RDRAM is aliased through uint8/16/32*).
// NOT subject to orthodoxy enforcement (test code).
//
// The accessor body lives in rdram_test_accessors.inc and is shared with the
// bounded-mode TU; RDRAM_TEST_PREFIX namespaces the C symbols so both TUs link
// into one executable for the equivalence test.

#define RDRAM_TEST_PREFIX(name) rdram_full_##name
#include "rdram_test_accessors.inc"
