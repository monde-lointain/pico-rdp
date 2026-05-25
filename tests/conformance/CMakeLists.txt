# Stream A.0 — M0 conformance executable + side-by-side comparator (host only).
#
# Ties together the already-merged streams:
#   rdp::conformance_utils  (A.2) — vendored Vulkan-free harness + ReplayerState
#   angrylion_oracle        (A.3) — bit-exact ground-truth ReplayerDriver
#   rdp_core                (A.0/A.5) — OUR renderer (rdpx_* C ABI)
#   replayer_driver_ours    (A.0) — our ReplayerDriver adapter over rdpx_*
#
# CRITICAL: this exe links BOTH the oracle (original Angrylion C names) AND
# rdp_core (everything external is rdpx_*-prefixed). No duplicate symbols.
#
# This directory is auto-added by the EXISTS-guarded foreach in tests/CMakeLists.

# ---- M0 side-by-side comparator (ctest) ----------------------------------
add_executable(rdp_conformance
    side_by_side_main.cpp
    replayer_driver_ours.cpp
)

# The adapter includes "n64video.h" (rdp_core public header) and the vendored
# harness headers (via rdp::conformance_utils PUBLIC includes).
target_link_libraries(rdp_conformance PRIVATE
    rdp::conformance_utils
    angrylion_oracle
    rdp_core
)

# The adapter compiles the rdpx_* RDRAM-aliasing path indirectly; pin
# -fno-strict-aliasing here too for consistency (harmless to the no-op stub).
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  target_compile_options(rdp_conformance PRIVATE -fno-strict-aliasing)
endif()

add_test(NAME conformance.m0_empty COMMAND rdp_conformance)
set_tests_properties(conformance.m0_empty PROPERTIES LABELS "conformance;m0")

# ---- M1 fill suite runner (ctest) ----------------------------------------
# Drives the rasterization/load conformance suites (register_rdp_suites, A.2)
# side-by-side: Angrylion oracle vs OUR rdp_core. Same no-duplicate-symbol
# contract as rdp_conformance (links BOTH the oracle and rdp_core).
add_executable(rdp_suite_runner
    suite_runner_main.cpp
    replayer_driver_ours.cpp
)
target_link_libraries(rdp_suite_runner PRIVATE
    rdp::conformance_utils
    angrylion_oracle
    rdp_core
)
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  target_compile_options(rdp_suite_runner PRIVATE -fno-strict-aliasing)
endif()

# M1 fill conformance: each suite runs bit-exact vs the oracle over [0, 1000).
foreach(fill_suite fill-16 fill-8 fill-32 fill-16-ia fill-16-interlace fill-rect)
  add_test(NAME conformance.${fill_suite}
           COMMAND rdp_suite_runner --suite ${fill_suite} --range 0 1000)
  set_tests_properties(conformance.${fill_suite} PROPERTIES LABELS "conformance;m1;fill")
endforeach()

# ---- M2 copy-mode + tex-rect + texture-load suites (B.2) -----------------
# Same rdp_suite_runner, driving the copy-cycle / TEX_RECT / LOAD_{TILE,BLOCK,
# TLUT} command paths bit-exact vs the oracle over [0, 1000). Suite names match
# register_rdp_suites() (A.2). texture-load-block-4 is commented out upstream
# (not registered), so it is intentionally absent here.
foreach(copy_suite
    copy-32bpp-fb8 copy-32bpp-fb16
    copy-4bpp-fb8 copy-4bpp-fb16
    copy-4bpp-fb4 copy-8bpp-fb4 copy-16bpp-fb4 copy-32bpp-fb4
    copy-8bpp-fb8 copy-8bpp-fb16 copy-16bpp-fb8 copy-16bpp-fb16
    copy-4bpp-fb16-tlut copy-8bpp-fb16-tlut copy-16bpp-fb16-tlut
    copy-16bpp-fb16-alpha-test
    copy-8bpp-fb8-perspective copy-8bpp-fb16-perspective
    copy-16bpp-fb8-perspective copy-16bpp-fb16-perspective)
  add_test(NAME conformance.${copy_suite}
           COMMAND rdp_suite_runner --suite ${copy_suite} --range 0 1000)
  set_tests_properties(conformance.${copy_suite} PROPERTIES LABELS "conformance;m2;copy")
endforeach()

foreach(texrect_suite tex-rect tex-rect-i8)
  add_test(NAME conformance.${texrect_suite}
           COMMAND rdp_suite_runner --suite ${texrect_suite} --range 0 1000)
  set_tests_properties(conformance.${texrect_suite} PROPERTIES LABELS "conformance;m2;tex-rect")
endforeach()

foreach(load_suite
    texture-load-tile-4 texture-load-tile-8 texture-load-tile-16 texture-load-tile-32
    texture-load-block-8 texture-load-block-16 texture-load-block-32
    texture-load-tlut-4 texture-load-tlut-8 texture-load-tlut-16
    texture-load-tile-16-yuv texture-load-block-16-yuv)
  add_test(NAME conformance.${load_suite}
           COMMAND rdp_suite_runner --suite ${load_suite} --range 0 1000)
  set_tests_properties(conformance.${load_suite} PROPERTIES LABELS "conformance;m2;texture-load")
endforeach()

# ---- M3a/M3b interpolation suites (B.3) ----------------------------------
# Edge-walker (11.2) + RGBA/Z/STW interpolators, bit-exact vs oracle over
# [0, 1000). Suite names match register_rdp_suites() (A.2).
foreach(interp_suite
    rasterization-noaa
    interpolation-color interpolation-depth interpolation-color-depth)
  add_test(NAME conformance.${interp_suite}
           COMMAND rdp_suite_runner --suite ${interp_suite} --range 0 1000)
  set_tests_properties(conformance.${interp_suite} PROPERTIES LABELS "conformance;m3;interpolation")
endforeach()

# ---- M4 combiner suites (B.4) --------------------------------------------
# 1-cycle / 2-cycle combiner (9-bit signed clamp) + 2-cycle alpha-test paths,
# bit-exact vs oracle over [0, 1000).
foreach(combiner_suite
    combiner-1cycle combiner-2cycle
    combiner-2cycle-alpha-test-color combiner-2cycle-alpha-test-texture)
  add_test(NAME conformance.${combiner_suite}
           COMMAND rdp_suite_runner --suite ${combiner_suite} --range 0 1000)
  set_tests_properties(conformance.${combiner_suite} PROPERTIES LABELS "conformance;m4;combiner")
endforeach()

# ---- M4t textured-triangle suites (B.6, affine) --------------------------
# The TEXTURE_TEST set: interpolation-color-texture-<variant> covering every
# texel format/size/TLUT/sample-quad/mid-texel combination, plus the pipelined
# texel1 path. Bit-exact vs oracle over [0, 1000). Names match the
# TEXTURE_TEST / TEXTURE_TEST_MID_TEXEL macro expansions in register_rdp_suites().
foreach(tex_variant
    rgba4 rgba8 rgba16 rgba32
    yuv16
    rgba16-mid-texel
    ci4-mid-texel ci8-mid-texel ci16-mid-texel ci32-mid-texel
    ci4-tlut-mid-texel ci8-tlut-mid-texel ci16-tlut-mid-texel ci32-tlut-mid-texel
    ci4-tlut-mid-texel-no-quad ci8-tlut-mid-texel-no-quad
    ci16-tlut-mid-texel-no-quad ci32-tlut-mid-texel-no-quad
    rgba4-nearest rgba8-nearest rgba16-nearest rgba32-nearest
    ci4 ci8 ci16 ci32
    ia4 ia8 ia16 ia32
    i4 i8 i16 i32
    ci4-tlut ci8-tlut ci16-tlut ci32-tlut
    ci4-tlut-no-quad ci8-tlut-no-quad ci16-tlut-no-quad ci32-tlut-no-quad
    ia4-tlut ia8-tlut ia16-tlut ia32-tlut
    i4-tlut i8-tlut i16-tlut i32-tlut
    rgba4-tlut rgba8-tlut rgba16-tlut rgba32-tlut
    ci4-tlut-ia16 ci8-tlut-ia16 ci16-tlut-ia16 ci32-tlut-ia16)
  add_test(NAME conformance.interpolation-color-texture-${tex_variant}
           COMMAND rdp_suite_runner --suite interpolation-color-texture-${tex_variant} --range 0 1000)
  set_tests_properties(conformance.interpolation-color-texture-${tex_variant}
                       PROPERTIES LABELS "conformance;m4t;texture")
endforeach()

foreach(tex_suite interpolation-color-texture-pipelined-texel1)
  add_test(NAME conformance.${tex_suite}
           COMMAND rdp_suite_runner --suite ${tex_suite} --range 0 1000)
  set_tests_properties(conformance.${tex_suite} PROPERTIES LABELS "conformance;m4t;texture")
endforeach()

# ---- M4p perspective-correct texture suites (B.7) ------------------------
# Perspective-division reciprocal-LUT tcoord path, bit-exact vs oracle.
foreach(persp_suite
    interpolation-color-texture-perspective
    interpolation-color-texture-pipelined-texel1-perspective)
  add_test(NAME conformance.${persp_suite}
           COMMAND rdp_suite_runner --suite ${persp_suite} --range 0 1000)
  set_tests_properties(conformance.${persp_suite} PROPERTIES LABELS "conformance;m4p;texture;perspective")
endforeach()

# ---- M4l LOD / mipmapping suites (B.8) -----------------------------------
# tcoord LOD level/frac computation + sharpen/detail variants, bit-exact vs
# oracle over [0, 1000). Names match register_rdp_suites() (A.2).
foreach(lod_suite
    interpolation-color-texture-2cycle-lod-frac
    interpolation-color-texture-perspective-2cycle-lod-frac
    interpolation-color-texture-perspective-2cycle-lod-frac-sharpen
    interpolation-color-texture-perspective-2cycle-lod-frac-detail
    interpolation-color-texture-perspective-2cycle-lod-frac-sharpen-detail)
  add_test(NAME conformance.${lod_suite}
           COMMAND rdp_suite_runner --suite ${lod_suite} --range 0 1000)
  set_tests_properties(conformance.${lod_suite} PROPERTIES LABELS "conformance;m4l;lod")
endforeach()

# ---- M4y YUV + color-space convert suites (B.9) --------------------------
# Combiner color-space convert (K0..K5 YUV->RGB) + texel-convert factors,
# bit-exact vs oracle over [0, 1000). The texture-convert-* family is the
# full cross product of {1cycle,2cycle} x {YUV,YUV-conv,RGBA,RGBA-conv} x
# {plain,quad,mid,quad-mid,bilerp,quad-bilerp,bilerp-mid,quad-mid-bilerp}
# (TEXTURE_TEST_CONVERT expansion in register_rdp_suites(), A.2).
foreach(conv_name plain quad mid quad-mid bilerp quad-bilerp bilerp-mid quad-mid-bilerp)
  foreach(conv_cycle 1cycle 2cycle)
    foreach(conv_fmt YUV YUV-conv RGBA RGBA-conv)
      set(conv_suite texture-convert-${conv_cycle}-${conv_fmt}${conv_name})
      add_test(NAME conformance.${conv_suite}
               COMMAND rdp_suite_runner --suite ${conv_suite} --range 0 1000)
      set_tests_properties(conformance.${conv_suite} PROPERTIES LABELS "conformance;m4y;convert")
    endforeach()
  endforeach()
endforeach()

foreach(conv_suite
    interpolation-color-texture-2cycle-convert-bilerp
    interpolation-color-texture-2cycle-convert-factors
    interpolation-color-texture-2cycle-implicit-convert-factors
    interpolation-color-texture-2cycle-implicit-convert-factors-bilerp)
  add_test(NAME conformance.${conv_suite}
           COMMAND rdp_suite_runner --suite ${conv_suite} --range 0 1000)
  set_tests_properties(conformance.${conv_suite} PROPERTIES LABELS "conformance;m4y;convert")
endforeach()

# ---- LUT memcmp scaffold (gtest) -----------------------------------------
# Trivially asserting until Stream B fills the LUTs; wired now so the home exists.
if(TARGET gtest_main)
  include(GoogleTest)
  add_executable(rdp_lut_memcmp_test lut_memcmp_test.cc)
  target_link_libraries(rdp_lut_memcmp_test PRIVATE rdp_core gtest_main)
  gtest_discover_tests(rdp_lut_memcmp_test)
endif()

# ---- A.5 post-init hidden == oracle (gtest) ------------------------------
# Asserts (not just logs) that our renderer's hidden RDRAM is byte-identical to
# the Angrylion oracle immediately after init. Links BOTH the oracle and
# rdp_core, plus our ReplayerDriver adapter; same no-duplicate-symbol contract
# as rdp_conformance.
if(TARGET gtest_main)
  add_executable(rdp_post_init_hidden_oracle_test
      post_init_hidden_oracle_test.cc
      replayer_driver_ours.cpp
  )
  target_link_libraries(rdp_post_init_hidden_oracle_test PRIVATE
      rdp::conformance_utils
      angrylion_oracle
      rdp_core
      gtest_main
  )
  if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(rdp_post_init_hidden_oracle_test PRIVATE -fno-strict-aliasing)
  endif()
  gtest_discover_tests(rdp_post_init_hidden_oracle_test PROPERTIES LABELS "conformance;m0")
endif()
