# Host (sdl) dependencies pulled via FetchContent from git (portable: builds on
# any machine with a network + SDL's system build deps).
#
# Offline / local-checkout override (per dep, CMake-native):
#   -DFETCHCONTENT_SOURCE_DIR_SDL3=/path/to/SDL
#   -DFETCHCONTENT_SOURCE_DIR_IMGUI=/path/to/imgui
# To share one clone across several games, set -DFETCHCONTENT_BASE_DIR=/path.

include(FetchContent)

# ---- SDL3 (static, no tests/examples) ------------------------------------
set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
set(SDL_TESTS OFF CACHE BOOL "" FORCE)
set(SDL_EXAMPLES OFF CACHE BOOL "" FORCE)
set(SDL_SHARED OFF CACHE BOOL "" FORCE)
set(SDL_STATIC ON CACHE BOOL "" FORCE)
# GIT_TAG pinned to the commit SHA (not a moving tag) for reproducibility.
# GIT_SHALLOW is omitted deliberately: a shallow fetch of an arbitrary commit
# SHA is server-dependent/fragile, so we take the full clone (cf. Oracle.cmake).
FetchContent_Declare(
  SDL3
  GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
  GIT_TAG d9d5536704d585616d4db3c8ba3c4ff6fc2757e1  # release-3.4.8
)
FetchContent_MakeAvailable(SDL3)

# ---- Dear ImGui (no upstream CMake; hand-rolled target + SDL3 backends) ---
# ImGui ships no CMakeLists, so fetch the source then build it ourselves.
# Docking branch tag: the viewer uses docking (ImGuiConfigFlags_DockingEnable is
# a runtime flag set by the viewer, NOT here). Multi-viewport is NOT enabled.
# Commit-SHA pin (= v1.92.8-docking); full clone (see SDL3 note above).
# ImGui ships no root CMakeLists, so MakeAvailable populates only -- it does NOT
# call add_subdirectory -- leaving ${imgui_SOURCE_DIR} for our hand-rolled target.
FetchContent_Declare(
  imgui
  GIT_REPOSITORY https://github.com/ocornut/imgui.git
  GIT_TAG b61e56346a92cfcaf1f43a545ca37b0b32239654  # v1.92.8-docking
)
FetchContent_MakeAvailable(imgui)
add_library(imgui STATIC
  ${imgui_SOURCE_DIR}/imgui.cpp
  ${imgui_SOURCE_DIR}/imgui_draw.cpp
  ${imgui_SOURCE_DIR}/imgui_tables.cpp
  ${imgui_SOURCE_DIR}/imgui_widgets.cpp
  ${imgui_SOURCE_DIR}/backends/imgui_impl_sdl3.cpp
  ${imgui_SOURCE_DIR}/backends/imgui_impl_sdlrenderer3.cpp
)
target_include_directories(imgui PUBLIC
  ${imgui_SOURCE_DIR}
  ${imgui_SOURCE_DIR}/backends
)
target_link_libraries(imgui PUBLIC SDL3::SDL3)

# ---- GoogleTest ----------------------------------------------------------
FetchContent_Declare(
  googletest
  GIT_REPOSITORY https://github.com/google/googletest.git
  GIT_TAG 52eb8108c5bdec04579160ae17225d66034bd723  # v1.17.0
)
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)
