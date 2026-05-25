# Orthodox C++ enforcement via the Orthodoxy clang plugin.
# OPTIONAL: only engages on a clang host build with the plugin installed and a
# matching major version. A plain gcc build (or missing plugin) silently skips,
# so the project still builds. Apply orthodoxy_enforce(<target>) to the
# platform-independent libraries (game/gfx/app) only — NOT tests, platform_sdl,
# or any third-party target.

set(ORTHODOXY_ACTIVE OFF)

if(PLATFORM STREQUAL "sdl" AND CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  find_package(orthodoxy CONFIG QUIET OPTIONAL_COMPONENTS plugin)
  if(orthodoxy_plugin_FOUND OR TARGET orthodoxy::plugin)
    set(ORTHODOXY_ACTIVE ON)
    message(STATUS "Orthodoxy plugin found: enforcement enabled on src/{game,gfx,app}")
  else()
    message(STATUS "Orthodoxy plugin not found: enforcement skipped (build still valid)")
  endif()
endif()

function(orthodoxy_enforce target)
  if(ORTHODOXY_ACTIVE)
    target_link_libraries(${target} PRIVATE orthodoxy::plugin)
  endif()
endfunction()
