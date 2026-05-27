# Shared host-side test scaffolding (config/fixture boilerplate). Host-only.
# Linked by the demo, perf, and conformance suites that bring up rdp_core over a
# caller-owned RDRAM arena. PUBLIC link to rdp_core so consumers inherit the
# n64video.h include path and the rdpx_* entry points.
add_library(rdp_test_support STATIC rdp_host_fixture.cc)
add_library(rdp::test_support ALIAS rdp_test_support)
target_include_directories(rdp_test_support PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(rdp_test_support PUBLIC rdp_core)
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  target_compile_options(rdp_test_support PRIVATE -fno-strict-aliasing)
endif()
