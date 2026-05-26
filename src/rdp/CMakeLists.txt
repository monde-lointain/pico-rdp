# RDP renderer core — Orthodox C++ single-translation-unit static library.
#
# Target:  rdp_core   Alias: Rdp::rdp
# Platforms: BOTH (host sdl + picosystem) — no host-only dependencies here.
#
# Single-TU model: the Angrylion-derived pipeline compiles as ONE C++ TU
# (src/rdp/n64video.c, which #includes rdp/*.c + vi/*.c under N64VIDEO_C). That
# TU is added by Stream A.0 (stub) then filled by Stream B/C. Stream A.1 ships
# only the build-glue version stub so the empty library links cleanly; A.5 adds
# the public ABI headers (n64video.h / n64video_common.h) into this same dir.
#
# Compiled as C++ from the start (plan: catch C-vs-C++ init divergence early),
# exposing a pure C ABI (rdpx_* prefix — avoids symbol collision with the
# Angrylion oracle linked alongside in the conformance harness).

add_library(rdp_core STATIC
    rdp_version.cc
    n64video.c
    # Split-out stage TUs (formerly #included into n64video.c's unity build).
    rdp/rdram.cc
    rdp/dither.cc
    rdp/coverage.cc
    rdp/zbuffer.cc
    rdp/blender.cc
    rdp/combiner.cc
    rdp/fbuffer.cc
    rdp/tmem.cc
    rdp/tcoord.cc
)
add_library(Rdp::rdp ALIAS rdp_core)

# n64video.c is the renderer's single TU. Despite the .c suffix it is compiled
# as C++ (plan: catch C-vs-C++ init divergence early; the file exposes a pure C
# ABI via extern "C"). rdp/rdram.c is #included into it, not built standalone.
set_source_files_properties(n64video.c PROPERTIES LANGUAGE CXX)

target_include_directories(rdp_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(rdp_core PRIVATE project_warnings)

# RDPX_TESTING: when host tests are built, compile the rdpx_* test-accessor block
# (hidden-RDRAM / TMEM base pointers) into rdp_core so the conformance adapter can
# reach them. PUBLIC so consumers see the same declarations. The production
# picosystem build sets BUILD_TESTS OFF, so the accessors are absent there.
if(BUILD_TESTS)
  target_compile_definitions(rdp_core PUBLIC RDPX_TESTING)
endif()

# Bit-exactness: RDRAM is aliased through uint8/16/32* (UB under strict aliasing,
# required to match Angrylion). Pin -fno-strict-aliasing; never -ffast-math.
# Applied per-target (each stream that compiles aliasing code does the same).
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  target_compile_options(rdp_core PRIVATE -fno-strict-aliasing)
endif()

# Orthodox C++ enforcement (host/clang only; no-op otherwise). Applies to OUR
# renderer library exclusively — never the oracle/harness/test code.
orthodoxy_enforce(rdp_core)
