# RDP viewer — host-only (PLATFORM=sdl) developer executable.
#
# Target: rdp_viewer. Renders the demo scene through OUR renderer and presents an
# ImGui (docking) inspector over SDL3. NOT orthodox-enforced (mirrors rdp_demo /
# the host harness): it links third-party SDL3 + ImGui idioms directly.
#
# Stream A.1 ships compiling stub TUs only (main.cc = int main(){return 0;}); the
# panel/decode/playback bodies and their internal headers land in later streams
# by editing these same sources, so the source list is the stable intended set.

add_executable(rdp_viewer
    main.cc
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
    SDL3::SDL3
    imgui
    project_warnings
)

target_include_directories(rdp_viewer PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
