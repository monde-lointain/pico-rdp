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

# ---- D.1 snapshot / seed-load / capture acceptance tests -----------------
# These four exercise the real demo end-to-end with COMMITTED goldens. They are
# registered with plain add_test (run the whole gtest binary), NOT
# gtest_discover_tests: the goldens make the demo deterministic, so per-case
# discovery buys nothing and the post-build discovery step is the part the CI is
# currently fixing — plain add_test sidesteps it entirely. The golden directory
# lives under this source tree (no spaces in the path) and is passed to the C++
# via -D defines so the tests never depend on cwd.
set(DEMO_GOLDEN_DIR "${CMAKE_CURRENT_SOURCE_DIR}/golden")

# snapshot_stream_test: demo_build_frame(0) command-word stream vs golden.
add_executable(demo_snapshot_stream_test snapshot_stream_test.cc)
target_link_libraries(demo_snapshot_stream_test PRIVATE demo_core gtest_main)
target_compile_definitions(demo_snapshot_stream_test PRIVATE
  DEMO_GOLDEN_STREAM_PATH="${DEMO_GOLDEN_DIR}/demo_frame0_stream.words")
add_test(NAME demo_snapshot_stream_test COMMAND demo_snapshot_stream_test)

# snapshot_output_test: demo frame 0 driven through rdp_core, color FB vs golden.
add_executable(demo_snapshot_output_test snapshot_output_test.cc)
target_link_libraries(demo_snapshot_output_test PRIVATE demo_core Rdp::rdp rdp::test_support gtest_main)
target_compile_definitions(demo_snapshot_output_test PRIVATE
  DEMO_GOLDEN_OUTPUT_PATH="${DEMO_GOLDEN_DIR}/demo_frame0_color.rgba5551")
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  target_compile_options(demo_snapshot_output_test PRIVATE -fno-strict-aliasing)
endif()
add_test(NAME demo_snapshot_output_test COMMAND demo_snapshot_output_test)

# seed_load_sample_test: demo_init seeding (BYTE_ADDR_XOR) + the CI4/TLUT load
# path driven through OUR renderer (rdp_core, RDPX_TESTING on host). The expected
# palette IS the checked-in generated arrows table, so no separate golden file.
add_executable(demo_seed_load_sample_test seed_load_sample_test.cc)
target_link_libraries(demo_seed_load_sample_test PRIVATE demo_core Rdp::rdp rdp::test_support gtest_main)
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  target_compile_options(demo_seed_load_sample_test PRIVATE -fno-strict-aliasing)
endif()
add_test(NAME demo_seed_load_sample_test COMMAND demo_seed_load_sample_test)

# tmem_preview_test: the viewer's Memory->TMEM CI4 preview decode
# (Rdp::viewer_decode) vs the renderer's live TMEM after a real frame 0. Locks the
# TLUT stride + texel XOR swizzle. Plain add_test like the other end-to-end demo
# tests. Guarded on the decode lib (built with the host SDL viewer stack).
if(TARGET Rdp::viewer_decode)
  add_executable(demo_tmem_preview_test tmem_preview_test.cc)
  target_link_libraries(demo_tmem_preview_test PRIVATE
    demo_core Rdp::rdp rdp::test_support Rdp::viewer_decode gtest_main)
  if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(demo_tmem_preview_test PRIVATE -fno-strict-aliasing)
  endif()
  add_test(NAME demo_tmem_preview_test COMMAND demo_tmem_preview_test)
endif()

# viewer_anim_test: the viewer's runtime gldemo-rate model matrices
# (Rdp::viewer_anim) vs the baked tables at shared sample points, plus the
# fixed-timestep pacer. Plain add_test like the other end-to-end demo tests.
# Guarded on the anim lib (built with the host SDL viewer stack).
if(TARGET Rdp::viewer_anim)
  add_executable(demo_viewer_anim_test viewer_anim_test.cc)
  target_link_libraries(demo_viewer_anim_test PRIVATE
    demo_core Rdp::viewer_anim gtest_main)
  add_test(NAME demo_viewer_anim_test COMMAND demo_viewer_anim_test)
endif()

# capture_roundtrip_test: SDL-free gtest — demo_build_frame(0) stream through the
# .rdp RDPCommand record framing and back, asserting byte-identical replay.
add_executable(demo_capture_roundtrip_test capture_roundtrip_test.cc)
target_link_libraries(demo_capture_roundtrip_test PRIVATE demo_core gtest_main)
add_test(NAME demo_capture_roundtrip_test COMMAND demo_capture_roundtrip_test)

# capture round-trip end-to-end: run the verified viewer flag headless (no SDL in
# any gtest). SDL_VIDEODRIVER=dummy avoids opening a real window. Guarded on the
# viewer target existing (it is only built with the host SDL stack present).
if(TARGET rdp_viewer)
  add_test(NAME demo_capture_roundtrip_run
           COMMAND rdp_viewer --headless-capture-roundtrip 0)
  set_tests_properties(demo_capture_roundtrip_run PROPERTIES
    ENVIRONMENT "SDL_VIDEODRIVER=dummy")
endif()
