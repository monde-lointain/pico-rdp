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
FetchContent_Declare(
  SDL3
  GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
  GIT_TAG release-3.4.8
  GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(SDL3)

# ---- Dear ImGui (no upstream CMake; hand-rolled target + SDL3 backends) ---
# ImGui ships no CMakeLists, so fetch the source then build it ourselves.
# Docking branch tag: the viewer uses docking (ImGuiConfigFlags_DockingEnable is
# a runtime flag set by the viewer, NOT here). Multi-viewport is NOT enabled.
FetchContent_Declare(
  imgui
  GIT_REPOSITORY https://github.com/ocornut/imgui.git
  GIT_TAG v1.92.8-docking
  GIT_SHALLOW TRUE
)
FetchContent_GetProperties(imgui)
if(NOT imgui_POPULATED)
  FetchContent_Populate(imgui)  # populate only; no add_subdirectory (no CMakeLists)
endif()
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
  GIT_TAG v1.17.0
)
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)
