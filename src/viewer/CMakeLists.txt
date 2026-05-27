# RDP viewer — host-only (PLATFORM=sdl) developer executable.
#
# Target: rdp_viewer. Renders the demo scene through OUR renderer and presents an
# ImGui (docking) inspector over SDL3. NOT orthodox-enforced (mirrors rdp_demo /
# the host harness): it links third-party SDL3 + ImGui idioms directly.
#
# Stream A.1 ships compiling stub TUs only (main.cc = int main(){return 0;}); the
# panel/decode/playback bodies and their internal headers land in later streams
# by editing these same sources, so the source list is the stable intended set.

# Pure, SDL/ImGui-free TMEM decode for the Memory panel preview. Split out so the
# decode mirrors the renderer's sample-tile addressing in one place and is
# unit-testable headlessly (tests/demo/tmem_preview_test.cc links this).
add_library(Rdp_viewer_decode STATIC tmem_preview.cc)
add_library(Rdp::viewer_decode ALIAS Rdp_viewer_decode)
target_link_libraries(Rdp_viewer_decode PUBLIC Rdp::rdp Rdp::demo PRIVATE project_warnings)
target_include_directories(Rdp_viewer_decode PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
# TLUT read type-puns uint8_t* -> uint16_t*; this project has no global aliasing
# pin (cmake/Flags.cmake absent; core targets carry it PRIVATE only).
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  target_compile_options(Rdp_viewer_decode PRIVATE -fno-strict-aliasing)
endif()

# Pure, SDL/ImGui-free runtime playback support: gldemo-rate model matrices
# (demo_anim) + fixed-timestep pacer (frame_pacer). Split out so they stay in
# lockstep with the demo's baked angles and are unit-testable headlessly
# (tests/demo/viewer_anim_test.cc links this).
add_library(Rdp_viewer_anim STATIC demo_anim.cc frame_pacer.cc)
add_library(Rdp::viewer_anim ALIAS Rdp_viewer_anim)
target_link_libraries(Rdp_viewer_anim PUBLIC Rdp::demo PRIVATE project_warnings m)
target_include_directories(Rdp_viewer_anim PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

add_executable(rdp_viewer
    main.cc
    headless.cc
    windowed.cc
    viewer_common.cc
    renderer_host.cc
    dock_layout.cc
    cmd_decode.cc
    shadow_state.cc
    panel_log.cc
    playback.cc
    panel_state.cc
    panel_memory.cc
    panel_perf.cc
    source_dump.cc
    capture.cc
)

# rdp_core's RDPX_TESTING test-accessors (hidden-RDRAM / TMEM base pointers) back
# the memory panel. On host, BUILD_TESTS defaults ON, so rdp_core already carries
# RDPX_TESTING PUBLIC (see src/rdp/CMakeLists.txt) and the viewer inherits it via
# the link below. Re-assert it locally so the viewer still gets the accessors
# even if a developer configures the host build with -DBUILD_TESTS=OFF.
target_compile_definitions(rdp_viewer PRIVATE RDPX_TESTING)

target_link_libraries(rdp_viewer PRIVATE
    Rdp::rdp
    Rdp::demo
    Rdp::viewer_decode
    Rdp::viewer_anim
    SDL3::SDL3
    imgui
    project_warnings
    # C.5 .rdp replay source reads dumps through parallel-rdp's canonical
    # DumpPlayer (rdp_dump.cpp); the capture writer mirrors its byte layout.
    rdp::conformance_utils
)

target_include_directories(rdp_viewer PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
