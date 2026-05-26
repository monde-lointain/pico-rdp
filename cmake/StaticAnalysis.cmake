# clang-tidy target (host build only). Based on cpp-template's StaticAnalysis.cmake,
# adapted for this project: headers live under src/ (no include/), and pico-only and
# generated sources are excluded since they are not in the host compile DB.

# Prefer the newest versioned binary available; fall back to the unversioned one.
find_program(CLANG_TIDY_EXECUTABLE NAMES clang-tidy-22 clang-tidy)
if(CLANG_TIDY_EXECUTABLE)
  file(GLOB_RECURSE TIDY_SOURCE_FILES
    ${CMAKE_SOURCE_DIR}/src/*.cc
    ${CMAKE_SOURCE_DIR}/src/*.c)
  # platform_pico.cc / main_pico.cc compile only for the device; assets_gen.cc is
  # generated. None appear in the host compile_commands.json.
  list(FILTER TIDY_SOURCE_FILES EXCLUDE REGEX "(_pico|assets_gen)\\.(c|cc)$")
  # NB src/rdp/rdp/*.cc and src/rdp/vi/*.cc are now standalone TUs (the Angrylion
  # unity build was split), so they ARE in tidy's scope. `make tidy` runs
  # --warnings-as-errors=* and therefore stays nonzero on the vendored renderer's
  # narrowing conversions (unfixable without risking bit-exactness; CI does not
  # gate on tidy) — tidy is inspection-only here, not a green gate.
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
