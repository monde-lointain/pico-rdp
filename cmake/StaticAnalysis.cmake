# clang-tidy target (host build only). Based on cpp-template's StaticAnalysis.cmake,
# adapted for this project: headers live under src/ (no include/), and pico-only and
# generated sources are excluded since they are not in the host compile DB.

find_program(CLANG_TIDY_EXECUTABLE NAMES clang-tidy)
if(CLANG_TIDY_EXECUTABLE)
  file(GLOB_RECURSE TIDY_SOURCE_FILES ${CMAKE_SOURCE_DIR}/src/*.cc)
  # platform_pico.cc / main_pico.cc compile only for the device; assets_gen.cc is
  # generated. None appear in the host compile_commands.json.
  list(FILTER TIDY_SOURCE_FILES EXCLUDE REGEX "(_pico|assets_gen)\\.cc$")

  add_custom_target(tidy
    COMMAND ${CLANG_TIDY_EXECUTABLE}
      -p ${CMAKE_BINARY_DIR}
      --warnings-as-errors=*
      ${TIDY_SOURCE_FILES}
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    COMMENT "Running clang-tidy (gated: findings fail the build)"
  )
endif()
