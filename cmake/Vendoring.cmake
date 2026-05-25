# Vendoring.cmake (Stream A.2) -- host-only conformance harness library.
#
# Builds rdp_conformance_utils: parallel-rdp's rdp-utils (command builder,
# triangle converter, dump player) plus the side-by-side driver, the byte-compare
# helpers, the portable RNG, the Vulkan-free ReplayerState, and the re-hosted
# suite registries -- all with Vulkan/Granite stripped out.
#
# Consumers (the A.0 conformance executable) link this target, inject concrete
# ReplayerDriver factories (A.3 oracle, A.0 our-renderer adapter) into
# ReplayerState, and own main(). This module deliberately does NOT reference the
# oracle / renderer targets, keeping streams decoupled.
#
# Included from the top-level CMakeLists inside the `PLATFORM STREQUAL "sdl"`
# block (host only), EXISTS-guarded. Vendored sources are NOT orthodox-enforced.

set(RDP_CONFORMANCE_VENDOR_DIR ${CMAKE_CURRENT_LIST_DIR}/../tests/conformance/vendor)

add_library(rdp_conformance_utils STATIC
    ${RDP_CONFORMANCE_VENDOR_DIR}/rdp_command_builder.cpp
    ${RDP_CONFORMANCE_VENDOR_DIR}/triangle_converter.cpp
    ${RDP_CONFORMANCE_VENDOR_DIR}/rdp_dump.cpp
    ${RDP_CONFORMANCE_VENDOR_DIR}/side_by_side.cpp
    ${RDP_CONFORMANCE_VENDOR_DIR}/rdp_conformance_host.cpp
    ${RDP_CONFORMANCE_VENDOR_DIR}/vi_conformance_host.cpp
)
add_library(rdp::conformance_utils ALIAS rdp_conformance_utils)

# Public: consumers include "replayer_state.hpp", "suite_runner.hpp",
# "conformance_compare.hpp", "portable_rng.hpp", "rdp_command_builder.hpp", etc.
target_include_directories(rdp_conformance_utils PUBLIC
    $<BUILD_INTERFACE:${RDP_CONFORMANCE_VENDOR_DIR}>
)

# Re-hosted suite registries are pure C++17; the rest is plain C++.
target_compile_features(rdp_conformance_utils PUBLIC cxx_std_17)

# Vendored third-party code: do NOT apply the project's -Werror/-Wall warning
# set (project_warnings is for our own targets only).

# ---- portable RNG golden-vector unit test (gtest) ------------------------
# Wired here (not tests/conformance/CMakeLists.txt, which Stream A.0 owns).
# Guarded on GoogleTest being available (host sdl deps) + tests enabled.
if(BUILD_TESTS AND TARGET gtest_main)
  include(GoogleTest)
  add_executable(rdp_portable_rng_test
      ${RDP_CONFORMANCE_VENDOR_DIR}/tests/portable_rng_test.cc
  )
  target_link_libraries(rdp_portable_rng_test PRIVATE rdp_conformance_utils gtest_main)
  gtest_discover_tests(rdp_portable_rng_test)
endif()
