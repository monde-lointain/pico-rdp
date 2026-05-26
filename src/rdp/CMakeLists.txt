# RDP renderer core — Orthodox C++ static library of individual TUs.
#
# Target:  rdp_core   Alias: Rdp::rdp
# Platforms: BOTH (host sdl + picosystem) — no host-only dependencies here.
#
# The Angrylion-derived pipeline (n64video.cc + rdp/*.cc + vi/*.cc) was once a
# single unity TU (n64video.c #included every stage .c under N64VIDEO_C); it is
# now split into one TU per stage. Cross-TU internals are rdpxi_-prefixed where
# they would collide with the Angrylion oracle (linked alongside in the
# conformance harness); the public ABI (n64video.h) is rdpx_*. All sources are
# C++ (.cc), exposing a pure C ABI via extern "C".

add_library(rdp_core STATIC
    rdp_version.cc
    n64video.cc
    rdp/rdp.cc
    # Stage TUs (formerly #included into n64video.c's unity build).
    rdp/rdram.cc
    rdp/dither.cc
    rdp/coverage.cc
    rdp/zbuffer.cc
    rdp/blender.cc
    rdp/combiner.cc
    rdp/fbuffer.cc
    rdp/tmem.cc
    rdp/tcoord.cc
    rdp/tex.cc
    rdp/rasterizer.cc
    vi/gamma.cc
    vi/lerp.cc
    vi/divot.cc
    vi/video.cc
    vi/restore.cc
    vi/fetch.cc
    vi/vi.cc
)
add_library(Rdp::rdp ALIAS rdp_core)

target_include_directories(rdp_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(rdp_core PRIVATE project_warnings)

# Cross-TU inlining: the unity build inlined the per-pixel helpers for free;
# splitting into TUs relies on LTO to recover that. Enable IPO for non-Debug
# configs (so pico Release benefits; host Debug stays fast to build). Guarded by
# check_ipo_supported so toolchains without LTO still configure.
include(CheckIPOSupported)
check_ipo_supported(RESULT RDP_IPO_OK OUTPUT RDP_IPO_MSG)
if(RDP_IPO_OK)
  set_target_properties(rdp_core PROPERTIES
    INTERPROCEDURAL_OPTIMIZATION_RELEASE ON
    INTERPROCEDURAL_OPTIMIZATION_RELWITHDEBINFO ON)
else()
  message(STATUS "rdp_core: IPO/LTO unavailable (${RDP_IPO_MSG}); skipping")
endif()

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
