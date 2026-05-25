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

# ---- M1 fill suite runner (ctest) ----------------------------------------
# Drives the rasterization/load conformance suites (register_rdp_suites, A.2)
# side-by-side: Angrylion oracle vs OUR rdp_core. Same no-duplicate-symbol
# contract as rdp_conformance (links BOTH the oracle and rdp_core).
add_executable(rdp_suite_runner
    suite_runner_main.cpp
    replayer_driver_ours.cpp
)
target_link_libraries(rdp_suite_runner PRIVATE
    rdp::conformance_utils
    angrylion_oracle
    rdp_core
)
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  target_compile_options(rdp_suite_runner PRIVATE -fno-strict-aliasing)
endif()

# M1 fill conformance: each suite runs bit-exact vs the oracle over [0, 1000).
foreach(fill_suite fill-16 fill-8 fill-32 fill-16-ia fill-16-interlace fill-rect)
  add_test(NAME conformance.${fill_suite}
           COMMAND rdp_suite_runner --suite ${fill_suite} --range 0 1000)
  set_tests_properties(conformance.${fill_suite} PROPERTIES LABELS "conformance;m1;fill")
endforeach()

# ---- LUT memcmp scaffold (gtest) -----------------------------------------
# Trivially asserting until Stream B fills the LUTs; wired now so the home exists.
if(TARGET gtest_main)
  include(GoogleTest)
  add_executable(rdp_lut_memcmp_test lut_memcmp_test.cc)
  target_link_libraries(rdp_lut_memcmp_test PRIVATE rdp_core gtest_main)
  gtest_discover_tests(rdp_lut_memcmp_test)
endif()

# ---- A.5 post-init hidden == oracle (gtest) ------------------------------
# Asserts (not just logs) that our renderer's hidden RDRAM is byte-identical to
# the Angrylion oracle immediately after init. Links BOTH the oracle and
# rdp_core, plus our ReplayerDriver adapter; same no-duplicate-symbol contract
# as rdp_conformance.
if(TARGET gtest_main)
  add_executable(rdp_post_init_hidden_oracle_test
      post_init_hidden_oracle_test.cc
      replayer_driver_ours.cpp
  )
  target_link_libraries(rdp_post_init_hidden_oracle_test PRIVATE
      rdp::conformance_utils
      angrylion_oracle
      rdp_core
      gtest_main
  )
  if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(rdp_post_init_hidden_oracle_test PRIVATE -fno-strict-aliasing)
  endif()
  gtest_discover_tests(rdp_post_init_hidden_oracle_test PROPERTIES LABELS "conformance;m0")
endif()
