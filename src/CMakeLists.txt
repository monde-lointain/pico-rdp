# Subsystem wiring. Each subsystem owns its own CMakeLists.txt below; this file
# only wires them and builds the final per-platform executable.

# RDP renderer core (Rdp::rdp) — built for BOTH platforms. The template's
# game/gfx/app/platform scaffold is left untouched (superseded only later by the
# sub-project-2 demo); the template demo exe (rdp_demo) and our renderer coexist.
add_subdirectory(rdp)

add_subdirectory(game)
add_subdirectory(gfx)
add_subdirectory(app)
add_subdirectory(platform)

# Template demo executable (renamed picosystem_template -> rdp_demo to free the
# `rdp` name for the renderer library; one entry point per platform).
if(PLATFORM STREQUAL "picosystem")
  add_executable(rdp_demo main_pico.cc)
  target_link_libraries(rdp_demo PRIVATE
    picosystem_template::app picosystem_template::game picosystem_template::gfx picosystem_template::platform_pico)
  # Use the PicoSystem memory map (12 MiB user FLASH + 4 MiB FAT region), as the
  # Pimoroni PicoSystem SDK examples do via pico_set_linker_script.
  pico_set_linker_script(rdp_demo
    ${CMAKE_CURRENT_SOURCE_DIR}/platform/memmap_picosystem.ld)
  pico_enable_stdio_usb(rdp_demo 1)
  pico_enable_stdio_uart(rdp_demo 0)
  pico_add_extra_outputs(rdp_demo)
else()
  add_executable(rdp_demo main_sdl.cc)
  target_link_libraries(rdp_demo PRIVATE
    picosystem_template::app picosystem_template::game picosystem_template::gfx picosystem_template::platform_sdl project_warnings)
endif()

target_include_directories(rdp_demo PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
