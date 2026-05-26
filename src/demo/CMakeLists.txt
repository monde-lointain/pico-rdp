# Demo scene core — Orthodox C++ static library, freestanding (no SDL/host deps).
#
# Target:  demo_core   Alias: Rdp::demo
# Platforms: BOTH (host sdl + picosystem). It builds the fixed-point math, setup
# IR, RDP command emitter, RDRAM byte I/O, and scene/driver logic shared by the
# device demo (rdp_demo) and the host viewer (rdp_viewer).
#
# Stream A.1 ships compiling stub TUs only; the PUBLIC headers
# (demo.h / fixed.h / setup_ir.h / cmd_sink.h, owned by A.2) and the real bodies
# land in later streams by editing these same sources, so the source list is the
# stable intended set.

add_library(demo_core STATIC
    fixed_math.c
    setup.c
    emit.c
    rdram_io.c
    scene.c
    demo.c
)
add_library(Rdp::demo ALIAS demo_core)

# PUBLIC: consumers (rdp_demo / rdp_viewer / tests) include the demo_core headers
# that A.2 lands alongside these sources.
target_include_directories(demo_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(demo_core PRIVATE project_warnings)

# Bit-exactness: the RDRAM byte I/O aliases through uint8/16/32* (UB under strict
# aliasing, required to match Angrylion). Pin -fno-strict-aliasing; never
# -ffast-math. Mirrors src/rdp/CMakeLists.txt.
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  target_compile_options(demo_core PRIVATE -fno-strict-aliasing)
endif()

# Orthodox C++ enforcement (host/clang only; no-op otherwise). Applies to OUR
# demo library exclusively — mirrors rdp_core.
orthodoxy_enforce(demo_core)
