// Test translation unit for rdram.c (BOUNDED hidden mode).
//
// Same TU body as rdram_test_tu.cc but with RDP_HIDDEN_BOUNDED enabled and a
// distinct symbol prefix, so both modes link into one executable for the
// bit-exact-neutral equivalence test (hidden_bounded_equiv_test).
//
// Bounded buffer covers RDP_HIDDEN_BOUNDED_WORDS hidden entries starting at a
// runtime-settable base. Sized to comfortably cover the test's FB region.

#define RDP_HIDDEN_BOUNDED
#define RDP_HIDDEN_BOUNDED_WORDS (256 * 1024)

#define RDRAM_TEST_PREFIX(name) rdram_bounded_##name
#include "rdram_test_accessors.inc"
