# Stream A.0 — M0 conformance executable + side-by-side comparator (host only).
#
# Ties together the already-merged streams:
#   rdp::conformance_utils  (A.2) — vendored Vulkan-free harness + ReplayerState
#   angrylion_oracle        (A.3) — bit-exact ground-truth ReplayerDriver
#   rdp_core                (A.0/A.5) — OUR renderer (rdpx_* C ABI)
#   replayer_driver_ours    (A.0) — our ReplayerDriver adapter over rdpx_*
#
# CRITICAL: this exe links BOTH the oracle (original Angrylion C names) AND
# rdp_core (everything external is rdpx_*-prefixed). No duplicate symbols.
#
# This directory is auto-added by the EXISTS-guarded foreach in tests/CMakeLists.

# ---- M0 side-by-side comparator (ctest) ----------------------------------
add_executable(rdp_conformance
    side_by_side_main.cpp
    replayer_driver_ours.cpp
)

# The adapter includes "n64video.h" (rdp_core public header) and the vendored
# harness headers (via rdp::conformance_utils PUBLIC includes).
target_link_libraries(rdp_conformance PRIVATE
    rdp::conformance_utils
    angrylion_oracle
    rdp_core
)

# The adapter compiles the rdpx_* RDRAM-aliasing path indirectly; pin
# -fno-strict-aliasing here too for consistency (harmless to the no-op stub).
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  target_compile_options(rdp_conformance PRIVATE -fno-strict-aliasing)
endif()

add_test(NAME conformance.m0_empty COMMAND rdp_conformance)
set_tests_properties(conformance.m0_empty PROPERTIES LABELS "conformance;m0")

# ---- LUT memcmp scaffold (gtest) -----------------------------------------
# Trivially asserting until Stream B fills the LUTs; wired now so the home exists.
if(TARGET gtest_main)
  include(GoogleTest)
  add_executable(rdp_lut_memcmp_test lut_memcmp_test.cc)
  target_link_libraries(rdp_lut_memcmp_test PRIVATE rdp_core gtest_main)
  gtest_discover_tests(rdp_lut_memcmp_test)
endif()
