# Sub-Project 2 — PC App + Demo (Design)

Part of the PicoSystem RDP port (see `2026-05-25-picosystem-rdp-port-overview-design.md`).
Follows sub-project 1 (renderer core), which is complete: full RDP+VI conformance bit-exact,
host + ARM CI green.

## Context

Sub-project 1 produced a bit-exact RDP renderer (`Rdp::rdp`) with a pure C ABI but **nothing
drives it interactively** — it's only exercised by the conformance harness. Sub-project 2 adds:

1. a **portable geometry frontend** that turns a 3D scene into an RDP command stream, and
2. a **host PC app** that drives the renderer with that stream (or a captured trace), presents
   the VI scanout, and provides an RDP debugger.

The frontend is portable on purpose: it is the same code the device demo will run
(sub-project 3). The PC app is host-only and exists both to show the renderer working and to be
the inspection tool that de-risks the device port.

Joining seam between the two new pieces and the existing renderer: the **RDP command word
stream** (`rdpx_rdp_cmd(0, words)`).

## Decisions

- **Purpose:** PC app is *both* a demo player *and* an RDP debugger.
- **Demo content:** faithful gldemo (rotating Gouraud pyramid + cube, Z-buffered) **plus** the
  cube textured with `assets/arrows.png` on every face.
- **Integration:** a **new standalone `rdp_viewer` host executable** + a **new portable
  `demo_core` library**. The template's `rdp_demo`/`app`/`gfx`/`platform` scaffold is left
  untouched.
- **Debugger scope:** all four bundles — playback & stepping, live state inspector, memory
  viewers (TMEM/color/depth/coverage), perf HUD — **plus** RDPDUMP trace replay as an alternate
  input source.
- **UI:** docked-IDE layout, requiring Dear ImGui on its **docking branch (`v1.92.8-docking`)**.
- **Texture:** `arrows.png` (13 colors after RGBA5551 quantization) → **64×64 CI4** (4-bit
  indices, 2048 bytes = the low 2 KB of TMEM) + a **16-entry RGBA5551 TLUT** (upper 2 KB, where
  the renderer reads it — `tmem.c:5` `tlut = &tmem[0x800]`). CI4 is lossless here and the only
  encoding that keeps full 64×64 *with* a palette (64×64 CI8's 4 KB would overwrite the TLUT).
- **Clipping:** match gldemo — **near-plane frustum clipping** (as F3DEX2 performs); screen edges
  handled by the RDP hardware scissor. (The controlled camera won't actually trigger it, but it's
  implemented for faithfulness.)
- **Shading:** texels + vertex colors only, **no dynamic lighting** (pyramid = Gouraud vertex
  colors; cube = CI4 texels via TLUT, optionally modulated by a constant/prim color).
- **Camera:** **fixed (gldemo-style)** with auto-rotating objects; **no interactive/mouse camera**
  (keeps demo_core a pure function of frame index + device parity; aligns with the overview's
  "no mouse drag" constraint).
- **Geometry math:** **fixed-point** throughout the mini-"RSP" (RSP-faithful, deterministic
  across host OSes → portable goldens, fast on the FPU-less M0+). Matrix/transform conventions
  follow libultra (`guPerspective`/`guLookAt` integer+fraction split).
- **Emitter:** demo_core ships its *own* portable/Orthodox RDP command emitter, unit-tested
  against the vendored `triangle_converter` as an oracle (the converter stays test-only).
- **RDPDUMP replay:** the viewer can **capture its live demo stream to a `.rdp`** (DumpPlayer's
  format) and reload it, and also load external `.rdp` files. Gives the replay path an in-repo
  data source and regression captures.
- **Step-command:** per-command stepping renders up to the selected command on demand (no caching
  of every intermediate framebuffer).
- **Plan shape:** one plan, executed in the staged milestones below — `demo_core` lands and is
  test-validated (V2–V3) before the viewer's debugger panels (V4–V6).

## Architecture

```
demo_core (portable, Orthodox C++, host+device)        rdp_viewer (host-only, SDL3+ImGui)
  scene (pyramid + textured cube, animation)             owns renderer lifecycle + RDRAM/regs
  mini-"RSP": 4x4 matrix stack, transform,               input source: demo_core | .rdp dump
    perspective divide, viewport, backface cull          pump stream -> rdpx_rdp_cmd
  triangle setup + RDP command emitter        ──cmds──►  run VI -> RGBA8888 scanout -> SDL tex
  emits into a command sink (buffer/callback)            ImGui docked debugger panels
        │ (also consumed by tests)                                 ▲
        └──────────────────── unit-tested vs vendored triangle_converter (oracle)
```

### Component 1 — `demo_core` (new, `src/demo/`, builds for BOTH platforms, Orthodox-enforced)

- **mini-"RSP"** (the work the real N64 RSP/F3DEX2 does, reimplemented in plain C):
  fixed-point 4×4 matrix math + a small matrix stack (model/view/projection), vertex transform to
  clip space, **near-plane frustum clipping** (matching gldemo/F3DEX2), perspective divide,
  viewport map to **240×240 square** screen space (1:1 projection aspect, for device parity),
  backface culling. Screen edges are handled by the RDP hardware scissor. **Freestanding/device-
  ready:** no `malloc`/`printf`/host-only deps — baked tables are `static const` (→ flash on
  device), it works on the RDRAM arena passed to `demo_init`, fixed buffers only.
- **fixed-point math kernel:** the M0+ has no FPU, but each core has an **8-cycle hardware integer
  divider** (SIO; the SDK's AEABI maps C `/` and `%` to it). So the kernel is plain fixed-point
  multiply (64-bit intermediates) + **fixed-point integer division** for the perspective divide —
  exact and deterministic across host and device (integer `/`), and HW-accelerated on-device. No
  reciprocal LUT needed. **No runtime trig:** the periodic per-frame model matrices are **baked at
  build time** (pyramid 30 frames, cube 40, loop = 120) — matching N64 reality (CPU computes
  matrices, RSP only transforms), keeping the runtime pipeline trig-free and deterministic. The
  baked matrix/geometry tables are **committed as checked-in generated source** (not float-baked
  per build) so values are identical across build machines. **No dynamic lighting** — vertex
  colors are baked (gldemo style); the textured cube samples the CI4 texture (via TLUT) modulated
  by a constant/vertex color.
- **fixed-point setup IR + command emitter:** the mini-"RSP" produces a **`PrimitiveSetup`-shaped
  fixed-point IR** (edge coeffs `x_a/dxdy_*`, `y_lo/mid/hi`, per-attr `c/dcdx/dcde/dcdy`, `z`,
  `u/v/w` + derivatives — mirroring `tests/conformance/.../primitive_setup.hpp` as a plain Orthodox
  struct). A deterministic **IR→words** step emits **raw RDP command words** (6-bit IDs, e.g.
  `0x3f` SET_COLOR_IMAGE — *not* GBI opcodes like `G_SETCIMG=0xff`), mirroring the known-good
  `CommandBuilder::submit_clipped_primitive`; `gbi.h` is referenced for bitfield layouts only.
  Also emits the non-triangle commands (SET_COLOR/DEPTH/TEXTURE_IMAGE, SET_COMBINE/OTHER_MODES,
  SET_TILE/SET_TILE_SIZE/**LOAD_TILE** (row-by-row — avoids LOAD_BLOCK's error-prone `dxt`),
  **LOAD_TLUT + CI4 tile (palette field) + TLUT-enable other-mode**, FILL_RECT, sync). We keep our
  own implementation (rather than reusing the **MIT-licensed** vendored converter/builder) only
  because ours is **fixed-point** (device, no FPU) and **Orthodox** — the vendored code is float +
  non-Orthodox; it serves as the **test oracle**.
- **init + frame API + sink (contract):** `demo_init(uint8_t *rdram, uint32_t size)` lays out the
  RDRAM arena and **seeds the static assets** — the CI4 texels and the RGBA5551 TLUT — into RDRAM
  (textures reach TMEM by DMA *from RDRAM*, so they must pre-exist there; commands only reference
  addresses). Then `demo_build_frame(uint32_t frame_index, struct CmdSink *sink)` emits one
  complete frame (clears, state, draws, sync) **one command at a time** into a sink
  (`void (*emit)(void *ctx, const uint32_t *words, uint32_t n)`) referencing those addresses.
  Per-command granularity drives step-command, the log, and capture. Playback advances the index;
  step-frame rebuilds; step-command builds then feeds the first K commands. Deterministic,
  snapshot-friendly, no internal animation state beyond the passed index.
- **RDRAM byte-order (hard rule):** RDRAM is byte-swizzled (`rdram.c`: `[idx ^ BYTE_ADDR_XOR]`,
  `BYTE_ADDR_XOR=3`/`WORD_ADDR_XOR=1`). Every host write *into* RDRAM (texture, TLUT) and read
  *out* (color/depth for the viewers, device blit later) **must apply the same XOR** — a plain
  `memcpy` reads back scrambled. Use one shared XOR-correct copy helper for all RDRAM I/O.
- **render flags:** **AA off** (1-cycle, opaque, sharp edges) for both objects — simplest, avoids
  coverage subtleties (the coverage viewer then shows trivial/full coverage). Can enable later.
- **render state (must be exact, not approximate):** pyramid = 1-cycle, combine = shade-only,
  Z-compare + Z-write, no texture; cube = 1-cycle, combine = texel × shade (CI4 sampled via TLUT),
  Z-compare + Z-write. Exact combine mux / cycle-type / other-modes / Z-mode values are written in
  the source with gldemo's render modes as the reference for the shaded path. **Backface cull
  winding matches gldemo** (`G_CULL_BACK`, N64 Y-down); cube per-face (u,v) mapping defined in the
  geometry data.
- **scene:** rotating pyramid (Gouraud, vertex-colored) + cube (arrows-textured), Z-buffered,
  frame-indexed animation matching gldemo's rotation rates.
- emits into a command sink so the viewer and the tests consume an identical stream. No SDL/
  ImGui/platform dependencies.

### Component 2 — `rdp_viewer` (new, host-only exe, `PLATFORM=sdl`; SDL/ImGui Orthodoxy-exempt)

- owns the renderer: `rdpx_video_init` with caller-allocated RDRAM + VI/DP register arrays,
  installs `rdpx_vdac_set_scanout_cb` to capture the RGBA8888 scanout into an SDL texture. Built
  with `RDPX_TESTING` so the TMEM/hidden-RDRAM accessors are available to the memory viewers.
- **VI/framebuffer contract:** demo_core owns a fixed **RDRAM layout** (color FB 240×240 RGBA5551
  ≈115 KB, Z buffer same size, CI4 texture 2 KB + TLUT) within an arena the viewer allocates
  (≈1–2 MB RDRAM). demo_core declares the color-FB address; the viewer programs the VI registers
  (VI_ORIGIN/WIDTH/STATUS/scale) to scan out that buffer at exactly 240×240. For `.rdp` replay the
  dump carries its own register writes. (Open sub-choice during impl: filtering VI vs
  `VI_MODE_COLOR` direct read — pick whichever yields clean 240×240 without overscan padding.)
- **input source switch + reset:** live `demo_core` stream, capture-to-`.rdp`, or replay a `.rdp`
  via the in-tree `DumpPlayer` (`tests/conformance/.../rdp_dump.*`). The capture writer **byte-
  matches DumpPlayer's read format** (8-byte header, rdram/hidden sizes, register-write + command +
  rdram-region records). Replay **re-inits the renderer with the dump's RDRAM/hidden sizes** (which
  can differ from the demo arena). Switching source / resetting re-inits cleanly (`rdpx_video_close`
  → `rdpx_video_init`, RDRAM re-zeroed) — verify the file-static singleton tolerates re-init
  mid-process (the conformance harness already create/destroys per test, so expected OK).
- per frame: feed command words to `rdpx_rdp_cmd(0, …)`, run `rdpx_video_update_screen`, upload
  the scanout texture.
- **ImGui docked UI (Layout A):** the dock layout is **seeded programmatically** (`DockBuilder`)
  on first run so Layout A appears docked out of the box (default is floating windows). Surfaces
  the renderer's **pipeline-crash latch** if it trips (likely on malformed `.rdp` replay) with a
  reset (close→init) to clear it. Central scanout viewport (integer upscale); command log with
  per-word decode + playback/stepping (play/pause/step-frame/step-command); live state inspector
  built from **viewer-side shadow state** decoded from the command stream (combine/blend/
  other-modes, tiles, image addrs, colors, scissor) — the renderer exposes no accessor for these,
  and the shadow also works for `.rdp` replay; memory viewers (TMEM via the `RDPX_TESTING`
  accessor; color/depth/coverage read from viewer-owned RDRAM, rendered as textures — depth needs
  z-decompression); perf HUD that **times the live demo frame and counts its pixels** (via a
  **pixel counter incremented in the rasterizer under `RDPX_TESTING`** — the renderer exposes no
  count today, so this small accessor must be added; host-viewer only), then applies the host→M0+
  extrapolation *model* from `docs/perf-extrapolation.md` (it does not re-run the synthetic
  `throughput_probe`). A single shared command decoder feeds both the log and the shadow state.

### Component 3 — asset + build

- **arrows.png:** **64×64 CI4** (4-bit indices, 2048 bytes) + a **16-entry RGBA5551 TLUT**.
  Asset-gen (extend `tools/gen_assets.py`) quantizes and emits the nibble-packed index array +
  palette as C arrays, **adapting Fast64's CI algorithm** (`f3d_texture_writer.py`:
  `extractConvertCIPixel`/`getRGBA16Tuple`/`getColorIndicesOfTexture`/`compactNibbleArray`/
  `writePaletteData`). The generated tables are **committed as checked-in source** so they're
  identical across build machines (keeps golden snapshots reproducible). The emitter LOAD_TLUTs the
  16 entries, LOAD_TILEs the CI4 data, sets a CI4 tile with TLUT enabled + the tile `palette`
  field, CLAMP, mapped per cube face. (asset-gen asserts the color count and fails loudly if a
  future asset exceeds the palette size.)
- **Dear ImGui → docking branch:** update `cmake/Dependencies.cmake` `GIT_TAG` to
  **`v1.92.8-docking`** (verify the tag resolves at configure) and set
  `ImGuiConfigFlags_DockingEnable` **only** — not multi-viewport (`ViewportsEnable`), which the
  SDL_Renderer3 backend doesn't fully support. The docking branch is a source-compatible superset,
  so the existing `rdp_demo`/`platform_sdl` ImGui usage keeps working.
- new `src/demo/CMakeLists.txt` (`demo_core`, both platforms, **Orthodoxy-enforced like
  `rdp_core`** — setup IR is a plain POD struct, sink is a function pointer not `std::function`,
  fixed-point math is plain C) and `rdp_viewer` executable (host-only, Orthodoxy-exempt like
  `platform_sdl`) wired in `src/CMakeLists.txt`. The host `rdp_core` for the viewer is built with
  `RDPX_TESTING` so the TMEM/hidden accessors are present.
- **Licensing (new deps):** Dear ImGui (MIT, docking branch) and SDL3 (zlib) are permissive;
  vendored `triangle_converter`/`primitive_setup`/`rdp_command_builder` are MIT (oracle reuse OK).
  **Fast64 is GPLv3, and Angrylion's MAME license is non-commercial → GPL-incompatible, so the
  project can't be relicensed GPLv3.** Resolution: the **asset-gen tool is a separate GPLv3 work**
  (mere aggregation in the repo, links no Angrylion code) that may adapt Fast64; its **generated
  data tables are unencumbered output** (like compiler output) and ship in the project, which stays
  under Angrylion's non-commercial MAME terms. `gbi.h` used as a format reference. Carry the
  ImGui/SDL/MIT notices in `CREDITS.md`/`NOTICE` and a `tools/LICENSE.gpl3` for the asset tool.

## Testing (TDD; demo_core is fully testable, the UI is not)

- unit tests: fixed-point matrix/vertex math + perspective integer-divide against known transforms.
- **RDRAM seed/XOR round-trip:** seed the CI4 texture via `demo_init`, run the LOAD_TILE + a
  texel sample, assert expected texels — locks the `BYTE_ADDR_XOR` byte-order rule directly.
- **setup-vs-oracle (tolerance):** `tests/demo` links the existing `rdp::conformance_utils` target
  (no promotion needed); feed identical float clip-space triangles to the vendored
  `triangle_converter` and the same geometry to demo_core; compare the fixed-point `PrimitiveSetup`
  coefficients within a small tolerance (theirs is float-derived, ours is fixed-point — *not*
  bit-identical). **Cover fill, Gouraud-shade, Z, and textured-perspective variants.** The IR→words
  step is separately checked bit-exactly against `submit_clipped_primitive`.
- **command-stream snapshot:** golden trace for demo frame 0 — now portable across host OSes
  because the geometry is fixed-point/deterministic.
- **rendered-output snapshot:** run demo frame 0 through the renderer, compare scanout against a
  stored golden (regenerated from the renderer, not hand-authored).
- **capture round-trip:** capture the demo stream to `.rdp`, replay it, assert identical scanout.
- viewer: manual smoke (init + render one frame); headless single-frame run if feasible.

## Milestones (incremental, each independently runnable)

- **V1** `rdp_viewer` skeleton: renderer lifecycle + scanout→SDL texture + docking shell, proven
  with a tiny **hardcoded clear sequence** (set color image + fill rect) — demo_core not yet needed.
- **V2** demo_core math + pyramid (Gouraud, Z) → live spinning pyramid.
- **V3** textured cube (arrows.png CI4 TMEM load + perspective-correct texturing) → full scene.
- **V4** command log + playback/stepping.
- **V5** state inspector + memory viewers.
- **V6** perf HUD + capture-to-`.rdp` / replay source.

## Critical files

- `cmake/Dependencies.cmake` — imgui docking branch + docking config flag.
- `src/demo/` (new) — `demo_core` (matrix, scene, triangle setup, emitter, command sink).
- `src/viewer/` (new) — `rdp_viewer` main + ImGui panel modules.
- `src/CMakeLists.txt` — wire `demo_core` (both platforms) + `rdp_viewer` (host-only).
- `src/rdp/rdp/rasterizer.c` + `n64video.c` — add an `RDPX_TESTING`-gated pixel counter + accessor
  (behavior-neutral, host-viewer only; does not affect the bit-exact/device build).
- `.github/workflows/ci.yml` — add `demo_core` to the `pico-build` cross-compile; demo tests to host.
- `assets/arrows.png` + `tools/gen_assets.py` — 64×64 CI4 nibble array + 16-entry RGBA5551 TLUT.
- `tests/demo/` (new) — math, setup-vs-oracle, stream + output snapshots, capture round-trip.
- **reference (read, do not modify):** `src/rdp/n64video.h` (rdpx_* ABI),
  `tests/conformance/.../{triangle_converter,primitive_setup,rdp_command_builder,rdp_dump}.*`
  (MIT — reusable as oracle; `submit_clipped_primitive` is the setup→words reference),
  `~/development/n64/gldemo_n64/src/main/*`, `~/development/repos/libultra_modern/include/PR/gbi.h`,
  `~/development/repos/fast64/fast64_internal/f3d/f3d_texture_writer.py` (CI/TLUT export algorithm).

## Risks & watch-items

- **Computing the fixed-point setup coefficients is the hardest, riskiest code** in demo_core
  (edge slopes + per-attr DcDx/DcDe/DcDy). Failure mode is *garbage, not crash*. Mitigated by the
  tolerance oracle test — but only for cases fed to it, so oracle inputs **must cover the demo's
  actual fill / shade / Z / textured-perspective combos**. The IR→words encoding is the *easy*
  part (deterministic, bit-checked vs `submit_clipped_primitive`).
- **Perspective-correct texturing** (the textured cube's S/T/W + persp-normalize) is the
  finickiest emitter path and is *new* relative to gldemo (which is untextured). Treat it as its
  own milestone slice (V3) with dedicated oracle cases.
- **Depth-buffer viewer** must decode the RDP's z-compression (not a raw 16-bit read).
- **VI 240×240 exactness** — overscan crop / scale must yield exactly 240×240; pick the VI mode
  that does so without surprise padding.
- Fixed-point geometry removes the cross-platform golden-snapshot fragility that float would have
  introduced (the sub-project-1 portable-RNG problem); keep all demo math integer/fixed.

## Verification

- `cmake --preset host` builds `demo_core` + `rdp_viewer` + tests; `ctest --preset host` passes
  (math, setup-vs-oracle, snapshots, capture round-trip). **`rdp_viewer` is build-only in CI**
  (SDL/ImGui needs a display; headless runners only link-check it, like the existing `rdp_demo`);
  only demo_core's tests run.
- `cmake --preset pico` builds `demo_core` for ARM (portability guard; the viewer is host-only).
  The CI `pico-build` job is **extended to also cross-compile `demo_core`** (it's device-targeted,
  same ARM/ILP32 trap surface as `rdp_core`).
- Launch `rdp_viewer`: spinning Gouraud pyramid + arrows-textured cube; docked panels populate;
  switch source to a conformance `.rdp` dump and replay it.
- Orthodoxy clean on `demo_core`; `rdp_viewer` exempt (SDL/ImGui), as `platform_sdl` already is.

## Deferred to implementation (empirical, low-stakes)

These get settled by writing code against the oracle/inspecting output, not by more specifying:
- exact fixed-point *scales* for the IR coefficients + the oracle numeric *tolerance* (tuned vs
  `triangle_converter`);
- the precise VI register values that yield a clean 240×240 (by inspection);
- the depth/coverage visualization color-mapping (cosmetic).

## Unresolved questions

None — all scoping and architectural choices resolved.
