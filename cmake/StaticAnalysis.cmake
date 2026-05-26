# clang-tidy target (host build only). Based on cpp-template's StaticAnalysis.cmake,
# adapted for this project: headers live under src/ (no include/), and pico-only and
# generated sources are excluded since they are not in the host compile DB.

find_program(CLANG_TIDY_EXECUTABLE NAMES clang-tidy)
if(CLANG_TIDY_EXECUTABLE)
  file(GLOB_RECURSE TIDY_SOURCE_FILES
    ${CMAKE_SOURCE_DIR}/src/*.cc
    ${CMAKE_SOURCE_DIR}/src/*.c)
  # platform_pico.cc / main_pico.cc compile only for the device; assets_gen.cc is
  # generated. None appear in the host compile_commands.json.
  list(FILTER TIDY_SOURCE_FILES EXCLUDE REGEX "(_pico|assets_gen)\\.(c|cc)$")
  # src/rdp/rdp/*.c and src/rdp/vi/*.c are #included into n64video.c (Angrylion
  # unity build), so they are not standalone TUs and absent from the compile DB.
  # They are still analyzed transitively through n64video.c via HeaderFilterRegex.
  list(FILTER TIDY_SOURCE_FILES EXCLUDE REGEX "src/rdp/(rdp|vi)/")
  # Headers are analyzed through the TUs that include them; HeaderFilterRegex in
  # .clang-tidy selects which (all of src/ except generated demo tables).

  add_custom_target(tidy
    COMMAND ${CLANG_TIDY_EXECUTABLE}
      -p ${CMAKE_BINARY_DIR}
      --warnings-as-errors=*
      ${TIDY_SOURCE_FILES}
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    COMMENT "Running clang-tidy (gated: findings fail the build)"
  )
endif()
