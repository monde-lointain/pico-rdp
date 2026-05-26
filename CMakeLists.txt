cmake_minimum_required(VERSION 3.28)

# ---- Platform selection --------------------------------------------------
set(PLATFORM "sdl" CACHE STRING "Target platform: sdl | picosystem")
set_property(CACHE PLATFORM PROPERTY STRINGS sdl picosystem)

if(PLATFORM STREQUAL "picosystem")
  # pico-sdk must be imported BEFORE project(); default PICO_SDK_PATH to the
  # local checkout if the environment did not set it.
  if(NOT DEFINED ENV{PICO_SDK_PATH} AND NOT PICO_SDK_PATH)
    set(PICO_SDK_PATH "$ENV{HOME}/development/repos/pico-sdk")
  endif()
  set(PICO_BOARD pimoroni_picosystem CACHE STRING "")
  include(cmake/pico_sdk_import.cmake)
  project(rdp VERSION 0.1.0 LANGUAGES C CXX ASM)
  pico_sdk_init()
else()
  project(rdp VERSION 0.1.0 LANGUAGES C CXX)
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_C_STANDARD 11)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(BUILD_TESTS "Build host unit tests" ON)

# Tests + Orthodoxy only make sense on the host (sdl) build.
if(PLATFORM STREQUAL "picosystem")
  set(BUILD_TESTS OFF)
endif()

# ---- Warning flags (our targets only; never third-party / SDK) -----------
add_library(project_warnings INTERFACE)
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  target_compile_options(project_warnings INTERFACE -Wall -Wextra -Wpedantic)
endif()

# ---- Pinned build flags for bit-exactness (A.4; all targets) -------------
# -fno-strict-aliasing (RDRAM aliasing), never -ffast-math. EXISTS-guarded so
# the substrate configures before Stream A.4 lands cmake/Flags.cmake.
if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/cmake/Flags.cmake)
  include(cmake/Flags.cmake)
endif()

# ---- Orthodox C++ enforcement (clang host only, scoped, optional) --------
include(cmake/Orthodoxy.cmake)

# ---- Sanitizers (ASan/UBSan, opt-in via RDP_SANITIZE, host-only) ---------
# Defines the always-present Rdp::sanitizers interface target (no-op when off).
# Linked into project_warnings so the demo exe / viewer inherit it too; rdp_core
# and demo_core link it PUBLIC in their own CMakeLists.
include(cmake/Sanitizers.cmake)
target_link_libraries(project_warnings INTERFACE Rdp::sanitizers)

# ---- Host dependencies (SDL3 + ImGui + GoogleTest via FetchContent) ------
if(PLATFORM STREQUAL "sdl")
  include(cmake/Dependencies.cmake)
  # clang-tidy target needs the host compile DB; host-only.
  include(cmake/StaticAnalysis.cmake)
  # Conformance harness (A.2) + Angrylion oracle (A.3): host-only, EXISTS-guarded
  # so the substrate configures before those streams land their cmake modules.
  if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/cmake/Vendoring.cmake)
    include(cmake/Vendoring.cmake)
  endif()
  if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/cmake/Oracle.cmake)
    include(cmake/Oracle.cmake)
  endif()
endif()

add_subdirectory(src)

if(BUILD_TESTS AND PLATFORM STREQUAL "sdl")
  # PRE_TEST defers gtest discovery to ctest run-time, avoiding the MSBuild
  # build-time custom command that mis-splits "C:/Program Files/CMake/..." on
  # the windows-latest runner. POST_BUILD is CMake's default and does NOT help.
  set(CMAKE_GTEST_DISCOVER_TESTS_DISCOVERY_MODE PRE_TEST)
  enable_testing()
  add_subdirectory(tests)
endif()
