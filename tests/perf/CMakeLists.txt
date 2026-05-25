# Stream D.1 / M6 — host throughput probe.
#
# Drives a representative full-pipeline workload (textured + combined + blended +
# AA/coverage + depth) through OUR renderer's per-command dispatch and reports
# host Mpixels/s. Feeds docs/perf-extrapolation.md (the M6 device go/no-go).
#
# This directory is auto-added by the EXISTS-guarded foreach in tests/CMakeLists.
#
# Links rdp_core (the rdpx_* C ABI under test) and rdp::conformance_utils (the
# vendored CommandBuilder that assembles the RDP command words). It does NOT
# link the oracle: the probe measures throughput, it does not compare.

add_executable(rdp_throughput_probe throughput_probe.cpp)
target_link_libraries(rdp_throughput_probe PRIVATE
    rdp::conformance_utils
    rdp_core
)

# The probe drives the rdpx_* RDRAM-aliasing path; pin -fno-strict-aliasing for
# consistency with the conformance executables.
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  target_compile_options(rdp_throughput_probe PRIVATE -fno-strict-aliasing)
endif()

# Registered as a ctest so CI builds + runs it (short measure window keeps it
# fast); it asserts nothing about the rate, only that the pipeline runs clean.
add_test(NAME perf.throughput_probe
         COMMAND rdp_throughput_probe --warmup 20 --frames 200)
set_tests_properties(perf.throughput_probe PROPERTIES LABELS "perf;m6")
