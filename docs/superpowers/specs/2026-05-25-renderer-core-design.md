# Sub-Project 1 — Angrylion RDP Renderer Core (host-only, bit-exact)

> Part of the PicoSystem RDP Port. See `2026-05-25-picosystem-rdp-port-overview-design.md`
> for the overall decomposition and cross-cutting decisions.

## Context

We are porting the Angrylion RDP Plus software renderer to the Pimoroni PicoSystem
(RP2040: dual Cortex-M0+, 264 KB RAM, ~16 MB flash, 240×240 ST7789). The full effort is
too large for one spec, so it decomposes into sequential sub-projects:

1. **Renderer core (this spec)** — adapt Angrylion into an Orthodox C++ static library,
   bit-exact, passing the parallel-rdp conformance suite **on the host PC only**.
2. PC app — SDL3 + ImGui front-end + N64 demo (display-list generator based on
   `gldemo_n64`), integer upscaling. (Later spec.)
3. PicoSystem device port — memory budgeting (LUTs → flash `const`, single worker, small
   RDRAM arena), **RDP + VI both on core1** (core0 reserved for game logic / RSP-style
   matrix-math, lighting, clipping, shading), ST7789 driver from scratch, demo on device.
   (Later spec.) The renderer core built here keeps the RDP pipeline in one library so core1
   runs it; **the VI is the exception (see below) — the device gets a new reduced VI path,
   not the bit-exact one.**

**VI does not port to the device, and the device output seam is the RDRAM framebuffer (not
`vdac_*`).** The bit-exact VI allocates `prescale[H_RES_NTSC * V_SYNC_PAL]` ≈ 640×625×4 ≈
**1.6 MB** (6× the RP2040's 264 KB), and `vdac_*`/`n64video_update_screen` sit *downstream*
of it — so the device **skips VI entirely**. It reads the RDP color image straight from
RDRAM (it knows `fb_address`/`fb_format`/`fb_width` because core0 issued `SET_COLOR_IMAGE`)
and does its own RGBA5551→RGB565 blit to the ST7789. M8's bit-exact VI is therefore a
**host/conformance-only artifact**; the device path is new sub-project-3 code sharing only
the RDP core (acceptable: conformance is native-res on PC; device only displays).

**Seam design baked in now to avoid retrofitting the bit-exact core (verified against
`rdram.c`):**
- **Hidden-RDRAM must be boundable.** `rdram_hidden[RDRAM_MAX_SIZE/2]` is indexed by
  *absolute* RDRAM word, so it scales with `RDRAM_MAX_SIZE` even though hidden/coverage bits
  only matter for the *color framebuffer*. Device math: 240² color (~110 KB) + separate z
  (~110 KB) ⇒ RDRAM ~256 KB ⇒ hidden 128 KB ⇒ ~384 KB > 264 KB. So `RDRAM_MAX_SIZE` alone
  cannot fit the device. **Design now:** abstract hidden-RDRAM storage to be **offset/bounded
  to the FB region** (host = full, unchanged; device = ~FB-size/2) with **bit-exact-neutral
  remapped indexing**. Small `rdram.c` change now, painful retrofit later.
- **Device fit needs banded/tiled rendering — but the core already supports it.** Even with
  bounded hidden, 240² color+z ≈ 220 KB leaves too little. The device renders in horizontal
  bands (core0 issues per-band scissored display lists into a small FB/ZB), which the core
  handles via `fb_address`/`zb_address` + scissor — **no core change**. Bounded hidden +
  banding fit comfortably (240×60 band ≈ 70 KB). The memory-budget worksheet evaluates this
  banded scenario at the M6 gate.

This sub-project exists because **bit-exactness is the hardest, most foundational
requirement** and must be nailed on the host — where we can run the exact fuzz suite
against the Angrylion oracle byte-for-byte — before any hardware constraints enter.
Embedded-TDD discipline: build and prove the logic on host first; hardware code stays thin
behind the template's existing HAL in later sub-projects.

**CANONICAL ANGRYLION SOURCE (decided 2026-05-25, M0 finding):** The bit-exactness target is
**the fork the conformance oracle embeds — `angrylion-rdp-plus` @ `31bdb1f`, available at
`~/development/repos/parallel-rdp/angrylion-rdp-plus`**. ALL renderer ports (rdram, every
pixel/VI stage in Stream B/C) MUST be adapted from THIS fork. The standalone
`~/development/repos/angrylion-rdp-plus` checkout is a **newer, divergent lineage** (refactored
C API; `HB_CLEAN=4` hidden-bit semantics vs the oracle's seed-3 machine) and is **NOT**
bit-exact with the conformance suite — do not port from it. M0 surfaced this when post-init
hidden buffers differed by construction.

**Key constraints driving the design (from exploration):**
- Angrylion's core is already near-Orthodox **pure C** (no classes/exceptions/STL/lambdas;
  function-pointer dispatch; `malloc`/`free`). Adapting it is the realistic path to
  bit-exactness; a clean rewrite would be intractable to match across 20 texel formats,
  2-cycle combiner/blender, z-compression LUTs.
- **Bit-exactness forbids simplifying the pipeline.** Only *memory provisioning* (RDRAM
  size, worker count) may vary between host and device — never the pixel logic.
- The parallel-rdp conformance harness is a **two-driver comparator hardwired to Vulkan**.
  We substitute OUR renderer for the GPU driver, keep Angrylion as the reference oracle,
  and **strip all Vulkan/Granite** plumbing.
- Decisions locked: **adapt Angrylion source**; spec the **full renderer core including
  VI**; **extract parallel-rdp rdp-utils + Angrylion oracle** for the harness.

## Architecture

- **Our library is pure C ABI** (`n64video.h`-shaped), built as **one translation unit**
  preserving Angrylion's `#ifdef N64VIDEO_C` single-TU `#include "rdp/*.c"` pattern. This
  keeps `static` LUT locality + `inline` semantics identical (bit-exactness) and yields one
  object file (ideal for later flash residency).
- **The C++ `ReplayerDriver` interface lives only in the test/harness layer**, NOT the
  library. A thin adapter (`replayer_driver_ours.cpp`, modeled on
  `replayer_driver_angrylion.cpp`) implements it by calling our C ABI.
- **Orthodoxy enforcement applies ONLY to our `rdp` library target** — never to the
  oracle, vendored utils, or test/harness code (those use full C++ freely).
- **Testability vs single-TU (design tension).** The single-TU `static` design needed for
  bit-exactness means LUTs and stage functions (combiner/blender/coverage) are file-local
  and cannot be unit-tested in isolation, and the unmodified oracle's internals aren't
  exposed either. **Resolution:** testing is *primarily black-box via conformance*; add a
  narrow `#ifdef RDPX_TESTING` block in our TU exposing accessors (LUT pointers, RDRAM
  round-trip) for the pitfall guards only — `static` in production builds. Macros
  (`SIGN`/`CLAMP`) stay testable via header include; white-box golden bytes come from a
  snapshotted/independent copy of Angrylion's init, not the live oracle. A conscious
  departure from fine-grained embedded-TDD unit tests, justified by bit-exactness.
- **Single-threaded — and this is REQUIRED for bit-exactness, not just convenient.**
  `parallel.cpp` (std::thread/STL) is excluded; `parallel.h` calls in `rdp.c` stubbed to
  direct calls. The RDP noise RNG (`irand`/`rseed`, seed `3 + worker_id*13`, advances
  per-pixel-per-worker) makes the NOISE combiner input and noise-dither/gamma modes
  **worker-count-dependent**, so output is *not* worker-invariant. The oracle runs
  single-worker (it sets no `parallel`/`num_workers`; `n64video_config_init` defaults
  `parallel=true`, so we must *explicitly force single-worker* to match it). Threading is a
  later device-perf concern and would break noise bit-exactness if reintroduced naively.
- **Configurable memory, byte-identical logic**: host harness allocates 8 MiB RDRAM + hidden
  RDRAM + 4 KB TMEM and passes pointers in via `n64video_config` (existing Angrylion
  contract). `RDRAM_MAX_SIZE` becomes a compile-time knob; addressing math (XOR, mask)
  stays verbatim. **`PARALLEL_MAX_WORKERS = 1`** (default 64 → `state[64]` is a ~6.6 MB
  static array at ~103 KB/worker; single worker is output-neutral and required for device).
  **Hidden-RDRAM storage is abstracted to be boundable** (host = full `RDRAM_MAX_SIZE/2`;
  device = FB-region only) with bit-exact-neutral indexing — designed now (see seam analysis)
  so the device port doesn't require editing the bit-exact `rdram.c` later. `span[1024]` may
  shrink toward the active scanline count to cut working set (output-neutral).

### Renderer init contract (must match the oracle exactly)
Bit-exactness requires our `n64video_init` config to match
`replayer_driver_angrylion.cpp`'s: `dp.compat = DP_COMPAT_HIGH`, `vi.mode = VI_MODE_NORMAL`,
`vi.interp = VI_INTERP_LINEAR`. `gfx.vi_reg`/`gfx.dp_reg` are `uint32_t**` (arrays of
per-register pointers); the adapter owns backing `vi_regs[VI_NUM_REG]`/`dp_regs[DP_NUM_REG]`
+ pointer arrays, and `set_vi_register(idx,val)` writes them. Command entry is
`rdp_cmd(0, words)` per command. `flush_caches`/`invalidate_caches`/`idle` are **no-ops**
(direct-buffer software renderer). Reuse these signatures verbatim and copy the Angrylion
adapter for `replayer_driver_ours.cpp`.

**Symbol naming — avoid collision with the oracle.** The harness links the Angrylion
oracle AND our renderer into ONE side-by-side binary. Angrylion's internal LUTs/functions
are `static` (single TU) so they don't collide, but its ~5 public functions (`n64video_init`,
`n64video_process_list`, `n64video_update_screen`, `n64video_close`, `rdp_cmd`) and the
`vdac_*` hooks would. **Resolution:** give OUR renderer's exported symbols a distinct prefix
(e.g. `rdpx_*`, `rdpx_vdac_write`); the adapter calls the prefixed entry points; the test
layer defines both `vdac_write` (→ oracle iface) and `rdpx_vdac_write` (→ our iface). Match
the oracle's config *values* (`DP_COMPAT_HIGH` etc.), not its symbol *names*. Stays Orthodox
(plain prefixed functions, no namespace).

**Output is a `vdac_*` callback seam, not a return value.** Scanout is emitted by the
renderer calling global hooks `vdac_init/vdac_write(struct frame_buffer*)/vdac_sync/
vdac_close`; the integrator implements them. On host the test layer forwards `vdac_write` →
`iface.update_screen` (RGBA8888, what `compare_image` checks). On the device, the analogous
hook is bypassed in favour of reading the RDP color image directly from RDRAM (the bit-exact
VI does not run on-device). Expose `vdac_*` in our C ABI.

### Module layout (new project, cloned from `picosystem-template`)
```
src/rdp/                       # Orthodox C++ static library; orthodoxy_enforce(rdp)
  n64video.h                   # public C ABI (adapted from Angrylion)
  n64video_common.h            # common.h macros (endianness/inline/SIGN) verbatim
  n64video.c                   # the single compiled TU; #includes rdp/*.c
  rdp/  rdram.c dither.c blender.c combiner.c coverage.c zbuffer.c
        fbuffer.c tmem.c tcoord.c tex.c rasterizer.c
  vi/   vi.c vi_fetch.c vi_gamma.c vi_divot.c vi_restore.c vi_lerp.c
  luts/ luts.h                 # isolate large tables so device sub-project can swap storage
tests/                         # template's gtest tree; add our micro-tests
                               #   (LUT memcmp, RDRAM XOR, clamp boundaries, irand sequence)
tests/conformance/             # vendored harness (full C++, NOT orthodox-enforced)
  replayer_driver_ours.cpp     # C++ adapter: ReplayerDriver -> our C ABI
  rdp_conformance_host.cpp     # re-hosted, Vulkan-free suite runner
  conformance_compare.hpp      # compare_rdram/_hidden/_image extracted, Vulkan-free
```

### Template integration (verified against the template CMake)
The template is subsystem-based (`src/{game,gfx,app,platform}` → `picosystem_template::*`
libs → one per-platform exe), with SDL/ImGui/gtest/Orthodoxy already guarded behind
`PLATFORM STREQUAL "sdl"` / `BUILD_TESTS`. Sub-project 1 **adds** `src/rdp` (a library built
for BOTH platforms — no host deps, compiles under pico-sdk) and `tests/conformance/`, and
**leaves the template's `game/gfx/app/platform` scaffold untouched**: the template demo exe
and our `rdp_conformance` exe coexist. The harness + Angrylion oracle + gtest micro-tests
attach via the **same `if(PLATFORM STREQUAL "sdl")` guard** so they never enter the pico
build. Rename `project()`/targets `picosystem_template` → `rdp` (+ `Rdp::rdp` alias) per the
CMake guidance. The template's software gfx is superseded by the RDP renderer only later (the
sub-project-2 demo).

### Conformance harness vendoring (host/sdl build only)
- **Pull from parallel-rdp** (Vulkan-free): `rdp_command_builder`, `triangle_converter` +
  `primitive_setup.hpp`, `rdp_dump` (RDPDUMP2 capture/replay — wired for debugging),
  `replayer_driver.hpp` (abstract iface). Provide thin `logging`/`rdp_common` shims (the
  upstream ones may pull Granite).
- **Extract** `compare_rdram` / `compare_hidden_rdram` / `compare_image` out of the
  Vulkan-coupled `conformance_utils.hpp` into `conformance_compare.hpp`.
- **Re-host** `ReplayerState` (drop `Vulkan::Device`/`wait_idle`) and the ~150 seeded suite
  definitions + `run_conformance_*` helpers from `rdp_conformance.cpp` and
  `vi_conformance.cpp`; strip `state.device->begin/end_renderdoc_capture()` and
  `next_frame_context()`. RNG seed (1337) and compare logic stay identical.
- **Leave out**: Granite, parallel-rdp Vulkan renderer, `replayer_driver_parallel.cpp`,
  GUI replayer, bench, integration examples.
- **Oracle**: link real Angrylion as reference (ground truth, not stored fixtures), pinned
  via FetchContent `GIT_TAG` of `angrylion-rdp-plus`.
- **ctest**: reuse upstream's `add_rdp_test(NAME)` (`--range 0 1000`, the gate) and
  `add_rdp_test_reduced(NAME)` (`--range 0 100`, inner-loop) plus `add_vi_test`; each suite
  is its own test for granular failure. Default suite = 1000 randomized iterations (full run
  ≈ 150 suites × 1000). gtest target carries the pitfall micro-tests.

## Milestones (TDD order, dependency-minimal)

Each milestone has a smallest first **failing** test, then makes its suites pass. Stage
ordering follows the pipeline data dependencies so a mismatch is unambiguous.

**The conformance suite is comprehensive — passing it ≈ a feature-complete pixel pipeline.**
The ~60 suite registrations (≈150 ctest cases after macro expansion) exercise LOD/mipmapping
(frac/sharpen/detail), perspective correction, YUV + color-space convert, pipelined texel1,
all TLUT/texel formats (4/8/16/32), and every depth/coverage mode. So the done-condition is
well-defined and there is no meaningful pixel-level gap between "passes conformance" and
"renders real N64 content" (VI aside); the demo's feature needs in sub-project 2 are a
subset. This also means texturing is **not one milestone** — it is M2 + M4t/M4p/M4l/M4y
below.

Scope note: conformance injects commands via `rdp_cmd(0, words)` per command — the
`n64video_process_list` buffer parser and DMEM/XBUS path are NOT exercised here (needed
later for the device display list; don't over-build now). `config.gfx.dmem` is wired but
inert this sub-project.

- **M0 — Walking skeleton / infra.** Scaffold from template; vendor harness data-layer;
  C-ABI library stub (allocates RDRAM/hidden/TMEM, no-op command); ReplayerDriver adapter;
  side-by-side comparator loop green on empty buffers. Adapt `n64video.h`, `common.h`,
  `rdram.c`. **First red test:** `rdram_xor_endianness_test` (BYTE_ADDR_XOR=3,
  WORD_ADDR_XOR=1 byte placement). Plus **LUT-memcmp test**: after `n64video_init`, every
  major LUT byte-identical to expected init bytes (cheapest highest-leverage guard).
- **M1 — FILL_RECT + fb formats.** `n64video.c` dispatch, `rdp.c` state handlers
  (set_color_image/scissor/other_modes/fill_color/fill_rectangle), fill path of
  `rasterizer.c`, `fbuffer.c` (4/8/16/32-bit), `dither.c`. Suites: `fill-8/16/32`,
  `fill-16-ia`, `fill-16-interlace`, `fill-rect`. **Red:** `--suite fill-16`.
- **M2 — COPY mode + TEX_RECT + texture loads.** `tmem.c` (load_tile/block/tlut),
  `tex.c` (texel fetch/format), `tcoord.c` (clamp/mirror/wrap), copy-mode rasterizer path.
  Suites: `copy-*`, `tex-rect*`, `texture-load-tile/block-{4,8,16,32}`,
  `texture-load-tlut-{4,8,16}`. **Red:** `--suite copy-rgba16`.
- **M3a — Shade/color interpolation.** Edge-walker + RGBA interpolation in `rasterizer.c`
  (the 11.2 fixed-point coefficient math). Suites: `rasterization-noaa`,
  `interpolation-color`. **Red:** `--suite interpolation-color`.
- **M3b — Depth + texture-coord interpolation.** Z and S/T/W interpolators (the rest of the
  8-variant edge-walker). Suites: `interpolation-depth`, `interpolation-color-depth`.
- **M4 — Color combiner.** `combiner.c` (1- then 2-cycle, 9-bit signed clamp). Suites:
  `combiner-1cycle`, `combiner-2cycle`, `combiner-2cycle-alpha-test-*`. **Red:**
  `--suite combiner-1cycle`.
- **M4t — Textured triangles (affine, nearest/bilinear).** Wire texel0/texel1 into the
  combiner on the triangle path; tile sampling. Suites: `interpolation-color-texture-*`
  (affine), `interpolation-color-texture-pipelined-texel1`. **Red:**
  `--suite interpolation-color-texture-` (first variant).
- **M4p — Perspective-correct texturing.** `tcoord.c` perspective division (S/W, T/W via
  the reciprocal LUT). Suites: `interpolation-color-texture-perspective`,
  `...-pipelined-texel1-perspective`.
- **M4l — LOD / mipmapping.** Multi-tile LOD selection + `lod_frac`, plus sharpen and detail
  texture modes (`tcoord.c`/`tex.c` LOD path). Suites:
  `interpolation-color-texture-2cycle-lod-frac`, `...-perspective-2cycle-lod-frac`,
  `...-lod-frac-sharpen`, `...-lod-frac-detail`, `...-lod-frac-sharpen-detail`. **Red:**
  `--suite interpolation-color-texture-2cycle-lod-frac`. (Largest texture milestone.)
- **M4y — YUV + color-space convert.** YUV texel fetch, `SET_CONVERT` k0–k3 coefficients,
  the 2-cycle convert/bilerp path. Suites: `interpolation-color-texture-yuv16-nearest`,
  `texture-load-tile/block-16-yuv`, `interpolation-color-texture-2cycle-convert-bilerp`,
  `...-convert-factors`, `...-implicit-convert-factors[-bilerp]`, `texture-convert-*`.
- **M5 — Blender + fog.** `blender.c`. Suites: `blender-fog-color-1cycle/2cycle-*`.
- **M6 — Coverage / AA.** full `coverage.c`, AA edge coverage, hidden-bit write paths
  (`rdram_write_pair8/16/32`). Suites: `rasterization-aa`, `coverage-*`,
  `color-on-coverage`, `*-cvg-times-alpha`, `*-alpha-cvg-select`. `compare_hidden_rdram`
  now load-bearing. **Red:** `--suite rasterization-aa`. **← feasibility go/no-go gate
  evaluated here** (first milestone with representative opaque+blend+coverage cost).
- **M7 — Z-buffer.** `zbuffer.c` compress/decompress LUTs, z-mode/depth compare. Suites:
  Z-enabled rasterization variants + `set-prim-depth`.
- **M8 — VI stage.** `vi.c`, `vi/*.c` (fetch/gamma/divot/restore/lerp). Implement the
  per-scanline VI register surface the harness drives
  (`begin/set/end_vi_register_per_scanline`, `set_crop_rect`) in the adapter. Suites: all of
  `vi_conformance.cpp`. **Red:** simplest 16-bit no-filter VI fetch.

Note: our renderer, like the Angrylion oracle, is a **process-global singleton**
(file-static state, single TU). The harness instantiates exactly one of ours alongside one
oracle — never two of ours.

## Bit-exactness pitfalls → early regression tests

Encode as gtest micro-tests *before* the relevant stage so divergence is caught at the
table/primitive level, not via framebuffer diff:

1. **RDRAM byte/word XOR** (`common.h` `BYTE_ADDR_XOR=3`/`WORD_ADDR_XOR=1`) — write/read
   round-trip asserting swizzle (M0).
2. **Hidden/coverage bits** — `rdram_write_pair8` has a *delayed-hidden-write state machine*
   (`delayedhbwidx`, `rdram_hidden_old` ring): highest-divergence code. Replay fixed
   pair-write sequences (flip 0/1) vs in-process oracle (M0/M6).
3. **Static LUT init** (~866 KB) — `memcmp` each table after init against expected bytes
   from an independent/snapshotted copy of Angrylion's init routine (NOT a live oracle
   memcmp: oracle tables are file-static and unexposed). Requires test-only accessors to our
   tables (see Testability note) (M0).
4. **9-bit signed clamp / `SIGN`/`SIGNF` macros** — boundary-value table vs oracle (M4).
5. **Z compress/decompress LUTs** — `memcmp` vs expected + depth round-trip sweep (M7).
6. **Dither matrices** — `memcmp` vs expected (M1).
7. **Perspective-division LUT** (`tcoord.c`) — `memcmp` + w-sweep (M4p).
8. **Saturating arithmetic** — keep Angrylion's exact clamp macros verbatim; do NOT
   substitute `std::clamp` (changes tie-breaking). Boundary tests at each clamp site.
9. **RDP noise RNG** (`irand` LCG + `rseed`/`vi_rseed`) — feeds the NOISE combiner input,
   noise-dither, and gamma-dither. Deterministic but **worker-count-dependent** (seed
   `3 + worker_id*13`, per-pixel advance). Must seed identically AND run single-worker to
   match the oracle. Test: a noise-combiner / noise-dither suite compared to the oracle
   (single-worker both sides); plus a unit test of `irand`'s sequence from seed 3.

General: drive stage tests through the same `compare_*` semantics as the suite; capture
goldens in-process from the oracle (no stored fixtures to drift). Build our core **as C++
from M0** (not C-then-C++) so LUT-memcmp catches any C-vs-C++ init divergence immediately.

## Orthodoxy friction (library target only)

Angrylion C is nearly compliant. FINE as-is: function-pointer dispatch tables, plain
`enum`, POD/aggregate init, `static inline`/`STRICTINLINE`, C-style casts. Genuine items:
exclude `parallel.cpp` (std::thread/STL → `IncludeForbid`); keep library in global
namespace (no `namespace`/`new`/`auto`/lambda/references — Angrylion core has none). The
C++ harness/adapter layer uses references/namespaces/lambdas freely (not enforced).

## Risks, build config & early feasibility gates

Verified against source: the RDP+VI core is **pure integer** (no `float`/`double` in
`n64video/`), uses **no C99-isms hostile to C++** (no designated initializers, compound
literals, `restrict`, or `union` punning — command table is positional aggregate init), and
the harness utils carry **no Granite/muglm math dependency** (only `<stdint/utility/
algorithm/cmath/limits/assert>` + a `logging.hpp` we shim). So host cross-platform FP
divergence is a non-issue for the renderer and the C→C++ adapt is small (mainly casting
`malloc` returns). Remaining items:

1. **Cross-platform RNG reproducibility (mandatory).** Cross-platform scope is
   **build + pass bit-exact on Linux, Windows/MSVC, and macOS/AppleClang** (gated by CI).
   The harness `RNG` uses portable `std::mt19937{1337}` but
   `std::uniform_real_distribution<float>` for vertex/color randomization — that distribution
   is implementation-defined, so libstdc++/libc++/MSVC generate *different test vectors*.
   **Resolution (required):** replace the float `randomize()` path with a hand-rolled
   portable distribution (deterministic across stdlibs), seed unchanged, so identical vectors
   run on all three OSes. Angrylion already compiles under MSVC/AppleClang (`STRICTINLINE`
   per-compiler, no GCC-only constructs), so the C-as-C++ port is portable. **Orthodoxy is
   clang-only**: enforcement runs on the Linux/clang build; the MSVC/AppleClang builds
   compile the same source without the plugin (compile + conformance only).
2. **Strict aliasing.** `rdram.c` aliases one buffer through `uint32_t*`/`uint16_t*`/
   `uint8_t*` — required for bit-exactness, UB under strict aliasing. **Resolution:** pin
   `-fno-strict-aliasing` for all targets (host + device); never `-ffast-math`. Set in the
   toolchain/preset, documented at the alias site.
3. **Harness re-host is the bulk of M0**, not glue: `ReplayerState` + ~150 suite lambdas are
   entangled with `state.device` (Vulkan capture/frame-context). Coupling is shallow
   (mechanical strip) but high line count — budget M0 accordingly; it dominates this
   sub-project's setup cost.
4. **Orthodoxy enforcement can silently no-op.** The plugin builds against LLVM/Clang dev
   headers+libs (`llvm-config`, recent ~v21/22) and needs CMake ≥ 3.31 to build; the
   template wires it `find_package(... OPTIONAL)` and **skips silently if absent** — the
   build succeeds enforcing nothing. **Resolution:** explicit setup step (install LLVM dev +
   build the plugin) and a CI/`make` assertion that enforcement is active (e.g. a known
   violation must fail), so non-Orthodox code can't slip in unnoticed.
5. **Device performance feasibility (existential — gate early).** A bit-exact per-pixel
   pipeline (2-cycle combiner + blender + coverage + LUT lookups) on a 133 MHz Cortex-M0+
   (no cache, no FPU, no HW divide) may be far too slow for interactive 240×240 frames, and
   this host-only sub-project would not otherwise reveal it. **Resolution:** build a
   **host throughput probe** (Mpixels/s through the full pipeline) wired from M1 and
   re-measured at each milestone, with a documented cycle-cost extrapolation to the M0+.
   Define an explicit **go/no-go checkpoint after M6** (the first milestone with the
   representative opaque+blend+coverage cost) before committing to the device sub-project;
   optionally a fill-rect-on-real-hardware spike for a ground-truth number. Keep pixel logic
   byte-identical regardless — optimization is a later, separately-validated concern.
6. **Device memory feasibility (gate early).** 240² color + 240² Z at 16bpp ≈ 230 KB of
   264 KB before TMEM (4 KB), command buffers, hidden RDRAM (`RDRAM_MAX_SIZE/2`), and span
   state (~56 KB), plus stacks. LUTs (~866 KB) must live in flash `const`, but the 512 KB
   z-compress table is indexed near-randomly per pixel → XIP cache thrashing (feeds back
   into risk 5); and init-time-built LUTs (`z_build_com_table`) must become **build-time
   codegen** to be flash-resident — itself a new bit-exactness surface. **Resolution:**
   maintain a **memory-budget worksheet** from M0 (sum every RAM consumer vs 264 KB; size
   LUTs for flash), keep the renderer's RAM footprint configurable via `RDRAM_MAX_SIZE` and
   minimal span buffers, and use the worksheet as input to the M6 go/no-go checkpoint so we
   never design into a corner. (Codegen + `const`-ifying themselves remain sub-project 3.)
   LUT init routines are config-independent (pure), so the device's flash-`const` tables come
   from a build-time codegen = **our own init compiled as a host tool**, byte-identical to the
   runtime fill (validated by the LUT micro-test) — confirms flash residency is viable. The
   FB-bounded hidden-RDRAM abstraction (Architecture) + banded rendering are the structural
   pieces that make 264 KB feasible; the worksheet validates the banded budget at the M6 gate.

## Verification

- `ctest -R conformance.` runs all vendored suites (RDP + VI) against the live Angrylion
  oracle; bit-exact pass = nonzero-exit-free across the suite range (seed 1337).
- `ctest` (gtest target) runs the pitfall micro-tests.
- Per-milestone: the named "first red test" must fail before the stage is implemented and
  pass after; prior milestones' suites must stay green (regression gate).
- `make tidy` / Orthodoxy plugin: clean on the `rdp` library target.
- Optional: capture an RDPDUMP2 from a real dump and replay vs oracle as a smoke check.
- **Feasibility gate**: host throughput probe (Mpixels/s) reported at each milestone; the
  M6 go/no-go checkpoint (perf extrapolation to M0+ + memory-budget worksheet) must pass
  before committing to the device sub-project.
- **Cross-platform gate**: CI builds + runs the full conformance suite on **Linux/clang
  (Orthodoxy-enforced), Windows/MSVC, and macOS/AppleClang**; all three must pass bit-exact
  (portable RNG guarantees identical test vectors across them).

## Resolved decisions

1. **Oracle pinning**: FetchContent the standalone `angrylion-rdp-plus` repo pinned to a
   specific `GIT_TAG` commit (reproducible; decoupled from the parallel-rdp submodule).
2. **Suite coverage**: run the full `--range 0 1000` per suite as the authoritative gate;
   define a small fast subset (one suite per milestone) for the inner dev loop.
3. **`luts/` split**: isolate the large-table *declarations* in `luts/luts.h` now (eases the
   later device flash-residency swap), but keep Angrylion's init-time generation identical —
   no `const`-ifying or storage-class changes in this sub-project.
4. **RDPDUMP2 capture**: wire capture/replay from M0 (low cost, no Vulkan) for debugging.
5. **Licensing**: the renderer is *derived from* Angrylion (MAME license — restrictive
   redistribution / non-commercial), combined with parallel-rdp (MIT-ish), SDL (zlib), ImGui
   (MIT). Treated as a personal/non-commercial port; the MAME terms govern the derived core
   and constrain any future distribution. Carry Angrylion's license + CREDITS in-tree.
