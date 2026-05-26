include(GoogleTest)

# Stream A smoke test: GoogleTest wiring + contract headers.
add_executable(smoke_test smoke_test.cc)
target_link_libraries(smoke_test PRIVATE
    picosystem_template::game picosystem_template::gfx picosystem_template::app gtest_main)
gtest_discover_tests(smoke_test)

# Per-subsystem test suites own their own tests/<sub>/CMakeLists.txt (no shared
# file contention). EXISTS-guarded so the scaffold builds before they land.
#   game/gfx/app  — template scaffold (untouched)
#   rdp           — renderer pitfall micro-tests        (Stream A.5/B, gtest)
#   demo          — demo_core unit + oracle/seed tests   (Stream A.1+, gtest/ctest)
#   conformance   — vendored harness + oracle comparator (Stream A.2/A.3/A.0, ctest)
#   perf          — throughput probe                     (Stream D.1)
foreach(sub game gfx app rdp demo conformance perf)
  if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/${sub}/CMakeLists.txt)
    add_subdirectory(${sub})
  endif()
endforeach()
