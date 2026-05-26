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

# ---- M8 VI conformance suite runner (Stream C) ---------------------------
# Drives register_vi_suites() (A.2) side-by-side: Angrylion oracle (reference)
# vs OUR rdp_core VI (gpu), comparing the VI scanout image (compare_image) each
# iteration. Same no-duplicate-symbol contract as rdp_suite_runner (links BOTH
# the oracle and rdp_core). gpu_scaled is wired to our no-op scaled stub.
add_executable(vi_suite_runner
    vi_suite_runner_main.cpp
    replayer_driver_ours.cpp
)
target_link_libraries(vi_suite_runner PRIVATE
    rdp::conformance_utils
    angrylion_oracle
    rdp_core
)
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  target_compile_options(vi_suite_runner PRIVATE -fno-strict-aliasing)
endif()

# M8 VI conformance: the 21 NATIVE aa-* suites (native-vs-native, bit-exact vs
# oracle) over the upstream VI default range [0, 32]. The runner defaults
# args.hi = 32 when --range is omitted.
#
# The four per-scanline-xh* suites are intentionally absent: they compare against
# a synthetic reference that exercises parallel-rdp's GPU-only per-scanline VI
# register table + upscaler, a feature the Angrylion-derived VI (our port AND
# the oracle) does not implement. They are N/A for a native renderer (see
# vi_suite_runner_main.cpp).
foreach(vi_suite
    aa-none-rgba5551 aa-none-rgba8888 aa-none-blank aa-none-reserved
    aa-extra-always aa-extra aa-scale aa-none
    aa-extra-dither-filter aa-extra-divot aa-extra-dither-filter-divot
    aa-extra-gamma aa-extra-gamma-dither aa-extra-nogamma-dither
    aa-none-randomize-xy-scale-bias aa-scale-randomize-xy-scale-bias
    aa-extra-randomize-xy-scale-bias
    aa-none-randomize-hv-start-end aa-none-randomize-hv-start-end-pal
    aa-none-serrate)
  add_test(NAME conformance.vi-${vi_suite}
           COMMAND vi_suite_runner --suite ${vi_suite})
  set_tests_properties(conformance.vi-${vi_suite} PROPERTIES LABELS "conformance;m8;vi")
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

# ---- M5 blender / fog suites (B.10) --------------------------------------
# Blender (2-mux) + fog-color blend, 1-cycle and 2-cycle, across fb formats,
# bit-exact vs oracle over [0, 1000). Names match the BLENDER_FOG_TEST /
# BLENDER_FOG_TEST2 macro expansions in register_rdp_suites() (A.2).
foreach(blend_cycle 1cycle 2cycle)
  foreach(blend_fb i4 i8 rgba5551 ia88 rgba8888)
    set(blend_suite blender-fog-color-${blend_cycle}-${blend_fb})
    add_test(NAME conformance.${blend_suite}
             COMMAND rdp_suite_runner --suite ${blend_suite} --range 0 1000)
    set_tests_properties(conformance.${blend_suite} PROPERTIES LABELS "conformance;m5;blender;fog")
  endforeach()
endforeach()

# ---- B.11 / M6 coverage + AA suites --------------------------------------
# Coverage/anti-aliasing: edge-coverage accumulation, the 4 coverage-overflow
# modes (clamp/wrap/zap/save) x {image-read off/on}, color-on-coverage, and the
# cvg*alpha / alpha-cvg-select interpolation variants. These are the FIRST
# suites to make compare_hidden_rdram load-bearing on rendered output
# (randomize_rdram + image_read_enable exercise the hidden 3-bit coverage
# write/read paths in rdram.c). Bit-exact vs oracle over [0, 1000). Names match
# the rasterization-aa / COVERAGE_MEMORY_TEST / color-on-coverage / cvg*alpha
# expansions in register_rdp_suites() (A.2). DEFER depth-compare/Z and VI.
foreach(aa_suite rasterization-aa rasterization-interlace-aa)
  add_test(NAME conformance.${aa_suite}
           COMMAND rdp_suite_runner --suite ${aa_suite} --range 0 1000)
  set_tests_properties(conformance.${aa_suite} PROPERTIES LABELS "conformance;m6;coverage;aa")
endforeach()

foreach(cvg_suite
    interpolation-color-depth-cvg-times-alpha
    interpolation-color-depth-aa-cvg-times-alpha
    interpolation-color-depth-aa-cvg-times-alpha-alpha-cvg-select
    interpolation-color-depth-aa-alpha-cvg-select)
  add_test(NAME conformance.${cvg_suite}
           COMMAND rdp_suite_runner --suite ${cvg_suite} --range 0 1000)
  set_tests_properties(conformance.${cvg_suite} PROPERTIES LABELS "conformance;m6;coverage;aa")
endforeach()

foreach(cov_suite
    coverage-clamp coverage-wrap coverage-zap coverage-save
    coverage-clamp-image-read coverage-wrap-image-read
    coverage-zap-image-read coverage-save-image-read
    color-on-coverage)
  add_test(NAME conformance.${cov_suite}
           COMMAND rdp_suite_runner --suite ${cov_suite} --range 0 1000)
  set_tests_properties(conformance.${cov_suite} PROPERTIES LABELS "conformance;m6;coverage;aa")
endforeach()

# ---- B.12 / M7 Z-buffer + depth-compare suites ---------------------------
# FINAL RDP sub-stage of the serialized pixel-pipeline spine. The depth-compare
# family exercises the z-encode/decode LUTs (z_com_table, z_complete_dec_table,
# deltaz_comparator_lut) and the four ZMode compares (opaque / interpenetrating
# / transparent / decal), each with and without prim-depth (SET_PRIM_DEPTH).
# z_mode/depth_compare/prim_depth all enabled; randomize_rdram + image_read make
# the hidden-RDRAM 18-bit depth + 3-bit coverage write/read paths load-bearing.
# Bit-exact vs oracle over [0, 1000). Names match the DEPTH_COMPARE_TEST macro
# expansion in register_rdp_suites() (A.2).
foreach(depth_suite
    depth-compare-opaque depth-compare-interpenetrating
    depth-compare-transparent depth-compare-decal
    depth-compare-opaque-prim-depth depth-compare-interpenetrating-prim-depth
    depth-compare-transparent-prim-depth depth-compare-decal-prim-depth)
  add_test(NAME conformance.${depth_suite}
           COMMAND rdp_suite_runner --suite ${depth_suite} --range 0 1000)
  set_tests_properties(conformance.${depth_suite} PROPERTIES LABELS "conformance;m7;zbuffer;depth")
endforeach()

# Remaining register_rdp_suites() entries not covered by M1-M6. With Z/depth in
# place the full RDP suite set is now registered. These exercise: the depth +
# alpha-test + RGB/alpha dither cross-product (DITHER_TEST, depth path on),
# fill ym-out-of-range edge clamp, nearest-sample YUV16, and the many-primitive
# stress suites (the -alias variant aliases the color + depth images and runs
# depth_compare). texture-load-block-4 stays commented out upstream (NOT
# registered) so it is intentionally absent. Bit-exact vs oracle over [0, 1000).
foreach(dither_suite
    interpolation-color-depth-alpha-test-dither-off
    interpolation-color-depth-alpha-test-dither-test
    interpolation-color-depth-alpha-test-dither-all
    interpolation-color-depth-alpha-test-dither-magic-pattern
    interpolation-color-depth-alpha-test-dither-magic-flip
    interpolation-color-depth-alpha-test-dither-magic-noise
    interpolation-color-depth-alpha-test-dither-bayer-pattern
    interpolation-color-depth-alpha-test-dither-bayer-flip
    interpolation-color-depth-alpha-test-dither-bayer-noise
    interpolation-color-depth-alpha-test-dither-off-pattern
    interpolation-color-depth-alpha-test-dither-off-flip
    interpolation-color-depth-alpha-test-dither-off-noise
    interpolation-color-depth-alpha-test-dither-noise-off
    interpolation-color-depth-alpha-test-dither-noise-pattern
    interpolation-color-depth-alpha-test-dither-noise-flip
    interpolation-color-depth-alpha-test-dither-noise-noise)
  add_test(NAME conformance.${dither_suite}
           COMMAND rdp_suite_runner --suite ${dither_suite} --range 0 1000)
  set_tests_properties(conformance.${dither_suite} PROPERTIES LABELS "conformance;m7;dither;depth")
endforeach()

foreach(misc_suite
    fill-ym-out-of-range
    interpolation-color-texture-yuv16-nearest
    rasterization-many-primitives
    rasterization-many-primitives-alias)
  add_test(NAME conformance.${misc_suite}
           COMMAND rdp_suite_runner --suite ${misc_suite} --range 0 1000)
  set_tests_properties(conformance.${misc_suite} PROPERTIES LABELS "conformance;m7;rasterization")
endforeach()

# rasterization-many-primitives{,-alias} replay 1k..5k primitives per iteration
# over [0, 1000): bit-exact vs oracle but far heavier than every other suite
# (minutes each, single-threaded). Raise the per-test ctest timeout well above
# the 1500s default so a slow host / parallel contention can't spuriously fail
# them on wall-clock alone (correctness is unaffected).
#
# Under sanitizers (RDP_SANITIZE set, Debug -O0 + ASan/UBSan) these run several
# times slower, so multiply the timeout when instrumented. Tune RDP_SANITIZE_TIMEOUT_MULT
# from the actual sanitizer wall time; 6x is a conservative starting point.
set(RDP_SANITIZE_TIMEOUT_MULT 6 CACHE STRING
    "Multiplier applied to heavy conformance test timeouts when RDP_SANITIZE is set")
if(RDP_SANITIZE)
    math(EXPR _rdp_heavy_timeout "7200 * ${RDP_SANITIZE_TIMEOUT_MULT}")
else()
    set(_rdp_heavy_timeout 7200)
endif()
set_tests_properties(
    conformance.rasterization-many-primitives
    conformance.rasterization-many-primitives-alias
    PROPERTIES TIMEOUT ${_rdp_heavy_timeout})

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
