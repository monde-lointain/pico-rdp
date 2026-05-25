# Stream A.3 — Angrylion oracle.
#
# Builds the UNMODIFIED Angrylion RDP Plus renderer (Themaister fork) as a
# host-side STATIC library and wraps it as a RDP::ReplayerDriver, mirroring
# parallel-rdp's `replayer_driver_angrylion.cpp` build (its `alp-core` target).
# This is the bit-exact ground-truth comparator; our own renderer is separate.
#
# Host-only. The top-level CMakeLists EXISTS-guard-includes this from the
# `PLATFORM STREQUAL "sdl"` block.
#
# ---- Pin -----------------------------------------------------------------
# The wrapper is a near-verbatim copy of parallel-rdp's driver and depends on
# that fork's C API: `n64video_update_screen(void)`, `vdac_write(frame_buffer*)`,
# global `rdp_cmd(uint32_t wid, ...)`, global `rdram_hidden[]`, `get_tmem()`.
# We pin to the exact commit of the Angrylion submodule that parallel-rdp ships
# (Themaister/angrylion-rdp-plus @ 31bdb1f), because that is the API contract
# the verbatim wrapper compiles against.
#
# NOTE / deviation from the stream brief: the brief suggested pinning to the
# HEAD of the local ~/development/repos/angrylion-rdp-plus checkout. That
# checkout is a *different, newer* fork whose API was refactored
# (n64video_update_screen now takes a frame_buffer*, tmem is per-worker with no
# global get_tmem(), rdram_hidden is file-static, rdp_cmd takes a state*). The
# verbatim parallel-rdp wrapper does not compile against it. Pinning to the fork
# parallel-rdp actually uses keeps the wrapper bit-for-bit faithful. See the
# A.3 report for the full rationale.

set(ORACLE_ANGRYLION_GIT_REPOSITORY
    "https://github.com/Themaister/angrylion-rdp-plus"
    CACHE STRING "Angrylion oracle source repository")
set(ORACLE_ANGRYLION_GIT_TAG
    "31bdb1f0a79dd726017a38432540c6b5db0fa117"
    CACHE STRING "Pinned Angrylion oracle commit (Themaister fork)")

include(FetchContent)

# Offline / local-checkout fallback (CMake-native, per the project convention in
# cmake/Dependencies.cmake). If network access to GitHub is blocked, configure
# with -DFETCHCONTENT_SOURCE_DIR_ANGRYLION_ORACLE=<path>. We default it to the
# parallel-rdp submodule checkout, which is already pinned at the commit above.
# The pinned GIT_TAG is still recorded for reproducibility on networked builds.
if(NOT DEFINED FETCHCONTENT_SOURCE_DIR_ANGRYLION_ORACLE)
  set(_oracle_local "$ENV{HOME}/development/repos/parallel-rdp/angrylion-rdp-plus")
  if(EXISTS "${_oracle_local}/src/core/n64video.c")
    set(FETCHCONTENT_SOURCE_DIR_ANGRYLION_ORACLE "${_oracle_local}"
        CACHE PATH "Local Angrylion oracle checkout (fallback when offline)")
  endif()
endif()

FetchContent_Declare(
  angrylion_oracle
  GIT_REPOSITORY "${ORACLE_ANGRYLION_GIT_REPOSITORY}"
  GIT_TAG        "${ORACLE_ANGRYLION_GIT_TAG}"
)
# Source only; this fork ships a plugin-style CMakeLists we do not want to run.
FetchContent_GetProperties(angrylion_oracle)
if(NOT angrylion_oracle_POPULATED)
  FetchContent_Populate(angrylion_oracle)
endif()

set(ORACLE_ALP_CORE "${angrylion_oracle_SOURCE_DIR}/src/core")

# ---- The oracle static library -------------------------------------------
# alp-core single-TU build: n64video.c (#defines N64VIDEO_C and #includes the
# rest of the renderer) + the C++ worker pool, plus our ReplayerDriver wrapper
# and its vdac_*/msg_* sink. We name the wrapper TU after the lib's source dir
# so the alp-core symbols stay in their original C names (no rdpx_* here).
add_library(angrylion_oracle STATIC
  "${ORACLE_ALP_CORE}/n64video.c"
  "${ORACLE_ALP_CORE}/parallel.cpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/tests/conformance/oracle/replayer_driver_angrylion_oracle.cpp")
add_library(rdp::angrylion_oracle ALIAS angrylion_oracle)

# Angrylion aliases RDRAM through uint8/16/32* — strict aliasing would miscompile
# it. Apply -fno-strict-aliasing to the whole oracle TU set. NEVER -ffast-math
# (would break bit-exactness against the reference).
target_compile_options(angrylion_oracle PRIVATE
  $<$<COMPILE_LANG_AND_ID:C,GNU,Clang>:-fno-strict-aliasing>
  $<$<COMPILE_LANG_AND_ID:CXX,GNU,Clang>:-fno-strict-aliasing>)

# n64video.c #includes "vdac.h"; the renderer sources live in src/core. Exposed
# PUBLIC so the wrapper TU (and any consumer) sees the Angrylion C API. SYSTEM so
# third-party warnings don't pollute our -Wall/-Wextra/-Wpedantic build.
target_include_directories(angrylion_oracle SYSTEM PUBLIC "${ORACLE_ALP_CORE}")

# ---- ReplayerDriver interface header resolution --------------------------
# The wrapper includes "replayer_driver.hpp" / "rdp_common.hpp" / "rdp_dump.hpp"
# / "logging.hpp". Prefer A.2's vendored copies; fall back to the upstream
# parallel-rdp checkout for compile-checking when A.2 has not landed yet.
set(_oracle_vendor "${CMAKE_CURRENT_SOURCE_DIR}/tests/conformance/vendor")
if(EXISTS "${_oracle_vendor}/replayer_driver.hpp")
  target_include_directories(angrylion_oracle SYSTEM PUBLIC "${_oracle_vendor}")
  message(STATUS "Oracle: using A.2 vendored ReplayerDriver interface (${_oracle_vendor}).")
else()
  set(_oracle_prdp "$ENV{HOME}/development/repos/parallel-rdp")
  if(EXISTS "${_oracle_prdp}/replayer_driver.hpp")
    target_include_directories(angrylion_oracle SYSTEM PUBLIC
      "${_oracle_prdp}"
      "${_oracle_prdp}/parallel-rdp"
      "${_oracle_prdp}/Granite/util")
    message(STATUS
      "Oracle: A.2 vendor headers absent; compile-checking wrapper against "
      "upstream parallel-rdp interface (${_oracle_prdp}). The real link target "
      "is owned by A.0/A.2.")
  else()
    message(WARNING
      "Oracle: neither A.2 vendored nor upstream parallel-rdp ReplayerDriver "
      "interface headers were found; the wrapper TU will fail to compile until "
      "A.2 lands tests/conformance/vendor/.")
  endif()
endif()

# Angrylion's worker pool (parallel.cpp) uses std::thread.
find_package(Threads REQUIRED)
target_link_libraries(angrylion_oracle PUBLIC Threads::Threads)

# Match the C standard the renderer expects; do not touch global flags.
target_compile_features(angrylion_oracle PUBLIC cxx_std_17)
