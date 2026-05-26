# Demo-core unit + conformance tests (host/BUILD_TESTS only; gtest/ctest).
#
# Stream A.1 ships trivial passing-gtest stubs; later streams replace the bodies
# by editing these same .cc files. The link graph here is the stable intended
# set so the build graph does not shift when real tests land:
#   demo_core               — the library under test (Rdp::demo)
#   rdp::conformance_utils  — vendored oracle/compare helpers (setup-oracle test)
#   rdp_core (Rdp::rdp)     — OUR renderer, carries RDPX_TESTING on host
#                             (seed/load-sample exercises the renderer end to end)
#
# Tests are NOT orthodox-enforced (gtest/C++ idioms). Sources that exercise RDRAM
# byte aliasing carry -fno-strict-aliasing, mirroring tests/rdp.

include(GoogleTest)

# ---- demo_core unit tests -------------------------------------------------
# fixed_math / emit_words / cmd_decode: pure demo_core logic.
foreach(t fixed_math_test emit_words_test cmd_decode_test)
  add_executable(demo_${t} ${t}.cc)
  target_link_libraries(demo_${t} PRIVATE demo_core gtest_main)
  gtest_discover_tests(demo_${t})
endforeach()

# rdram_xor_test: exercises demo_core's RDRAM byte I/O (aliasing path).
add_executable(demo_rdram_xor_test rdram_xor_test.cc)
target_link_libraries(demo_rdram_xor_test PRIVATE demo_core gtest_main)
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  target_compile_options(demo_rdram_xor_test PRIVATE -fno-strict-aliasing)
endif()
gtest_discover_tests(demo_rdram_xor_test)

# ---- oracle-backed setup test --------------------------------------------
# setup_oracle_test: validates demo_core setup IR against the vendored oracle
# helpers. Guarded on the host conformance utils being present.
add_executable(demo_setup_oracle_test setup_oracle_test.cc)
target_link_libraries(demo_setup_oracle_test PRIVATE demo_core gtest_main)
if(TARGET rdp::conformance_utils)
  target_link_libraries(demo_setup_oracle_test PRIVATE rdp::conformance_utils)
endif()
gtest_discover_tests(demo_setup_oracle_test)

# ---- snapshot stream/output tests ----------------------------------------
foreach(t snapshot_stream_test snapshot_output_test)
  add_executable(demo_${t} ${t}.cc)
  target_link_libraries(demo_${t} PRIVATE demo_core gtest_main)
  gtest_discover_tests(demo_${t})
endforeach()

# ---- renderer-backed seed/load + capture round-trip ----------------------
# seed_load_sample_test: drives OUR renderer (rdp_core, RDPX_TESTING on host) via
# demo_core and checks sampled output.
add_executable(demo_seed_load_sample_test seed_load_sample_test.cc)
target_link_libraries(demo_seed_load_sample_test PRIVATE demo_core Rdp::rdp gtest_main)
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  target_compile_options(demo_seed_load_sample_test PRIVATE -fno-strict-aliasing)
endif()
gtest_discover_tests(demo_seed_load_sample_test)

# capture_roundtrip_test: demo_core capture encode/decode round-trip.
add_executable(demo_capture_roundtrip_test capture_roundtrip_test.cc)
target_link_libraries(demo_capture_roundtrip_test PRIVATE demo_core gtest_main)
gtest_discover_tests(demo_capture_roundtrip_test)
