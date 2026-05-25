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
)
add_library(Rdp::rdp ALIAS rdp_core)

target_include_directories(rdp_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(rdp_core PRIVATE project_warnings)

# Bit-exactness: RDRAM is aliased through uint8/16/32* (UB under strict aliasing,
# required to match Angrylion). Pin -fno-strict-aliasing; never -ffast-math.
# Applied per-target (each stream that compiles aliasing code does the same).
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  target_compile_options(rdp_core PRIVATE -fno-strict-aliasing)
endif()

# Orthodox C++ enforcement (host/clang only; no-op otherwise). Applies to OUR
# renderer library exclusively — never the oracle/harness/test code.
orthodoxy_enforce(rdp_core)
