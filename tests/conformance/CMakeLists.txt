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

# ---- M2 copy-mode + tex-rect + texture-load suites (B.2) -----------------
# Same rdp_suite_runner, driving the copy-cycle / TEX_RECT / LOAD_{TILE,BLOCK,
# TLUT} command paths bit-exact vs the oracle over [0, 1000). Suite names match
# register_rdp_suites() (A.2). texture-load-block-4 is commented out upstream
# (not registered), so it is intentionally absent here.
foreach(copy_suite
    copy-32bpp-fb8 copy-32bpp-fb16
    copy-4bpp-fb8 copy-4bpp-fb16
    copy-4bpp-fb4 copy-8bpp-fb4 copy-16bpp-fb4 copy-32bpp-fb4
    copy-8bpp-fb8 copy-8bpp-fb16 copy-16bpp-fb8 copy-16bpp-fb16
    copy-4bpp-fb16-tlut copy-8bpp-fb16-tlut copy-16bpp-fb16-tlut
    copy-16bpp-fb16-alpha-test
    copy-8bpp-fb8-perspective copy-8bpp-fb16-perspective
    copy-16bpp-fb8-perspective copy-16bpp-fb16-perspective)
  add_test(NAME conformance.${copy_suite}
           COMMAND rdp_suite_runner --suite ${copy_suite} --range 0 1000)
  set_tests_properties(conformance.${copy_suite} PROPERTIES LABELS "conformance;m2;copy")
endforeach()

foreach(texrect_suite tex-rect tex-rect-i8)
  add_test(NAME conformance.${texrect_suite}
           COMMAND rdp_suite_runner --suite ${texrect_suite} --range 0 1000)
  set_tests_properties(conformance.${texrect_suite} PROPERTIES LABELS "conformance;m2;tex-rect")
endforeach()

foreach(load_suite
    texture-load-tile-4 texture-load-tile-8 texture-load-tile-16 texture-load-tile-32
    texture-load-block-8 texture-load-block-16 texture-load-block-32
    texture-load-tlut-4 texture-load-tlut-8 texture-load-tlut-16
    texture-load-tile-16-yuv texture-load-block-16-yuv)
  add_test(NAME conformance.${load_suite}
           COMMAND rdp_suite_runner --suite ${load_suite} --range 0 1000)
  set_tests_properties(conformance.${load_suite} PROPERTIES LABELS "conformance;m2;texture-load")
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
