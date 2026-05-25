# PicoSystem RDP Port — Project Overview & Decomposition

## Goal

Port the **Angrylion RDP Plus** software renderer (the cycle/bit-accurate N64 Reality
Display Processor emulator) to the **Pimoroni PicoSystem** (RP2040: dual Cortex-M0+ @
133 MHz, 264 KB SRAM, ~16 MB flash, 240×240 ST7789 LCD). The port must be **bit-exact** with
Angrylion — it must pass the same conformance/fuzz suite that ParaLLEl RDP validates against
— while also cross-compiling for PC (Windows/Linux/macOS) for development and testing, and
ultimately running a small N64-style demo on the console with RDP emulation on the second
core.

This is a multi-month effort that is too large for a single spec. This document defines the
decomposition into sub-projects, their dependencies, and the cross-cutting decisions. Each
sub-project gets its own design spec → implementation plan → execution cycle.

## Why this is hard (the constraints that shape everything)

- **Bit-exactness forbids "simplifying" the pipeline.** Matching Angrylion across 20 texel
  formats, the 2-cycle combiner/blender, z-compression LUTs, coverage/AA, dither, and the
  noise RNG means the pixel logic must be preserved *verbatim*. Only *memory provisioning*
  (RDRAM size, worker count, LUT storage class) may vary between host and device.
- **Memory is the dominant device constraint.** Angrylion assumes 4–8 MB RDRAM, ~866 KB of
  static LUTs, ~103 KB/worker state, and a 1.6 MB VI prescale buffer. The RP2040 has 264 KB
  RAM. LUTs must move to flash `const`; the renderer must run single-worker; RDRAM is a small
  arena; the device renders in horizontal bands and skips the bit-exact VI entirely.
- **Performance is an unproven existential risk.** A bit-exact per-pixel pipeline on a
  133 MHz M0+ (no cache, no FPU, no HW divide) may be too slow for interactive frames. The
  project carries an explicit **go/no-go feasibility gate** (below) before the device port.
- **Strategy: adapt, don't rewrite.** Angrylion's core is already near-Orthodox **pure C**
  (function-pointer dispatch, `malloc`/`free`, no classes/exceptions/STL/lambdas). Adapting
  it into Orthodox C++ is the only realistic path to bit-exactness; a clean rewrite would be
  intractable to match.

## Cross-cutting decisions (apply to all sub-projects)

- **Build**: CMake ≥ 3.28, C++20, dual-target via the `picosystem-template` (`PLATFORM = sdl
  | picosystem`). Modern-CMake target discipline (see project `CLAUDE.md`).
- **Style**: Orthodox C++ enforced by the Orthodoxy clang plugin, scoped to *our* renderer
  library only — never to vendored/oracle/harness/test code. Enforcement is clang/Linux-only;
  MSVC/AppleClang compile the same source without the plugin.
- **Bit-exact oracle**: the real Angrylion (pinned via FetchContent `GIT_TAG`) is the
  reference; we never trust stored fixtures over the live oracle.
- **Conformance harness**: extract ParaLLEl RDP's `rdp-utils` (command builder, triangle
  converter, RDPDUMP2, `ReplayerDriver` interface, compare helpers) and strip all
  Vulkan/Granite. Our renderer implements the same driver interface; the harness compares
  RDRAM / hidden-RDRAM / VI output byte-for-byte against the oracle (RNG seed 1337).
- **TDD for embedded**: develop and prove all logic on the host first; hardware code stays
  thin behind the template's HAL. The single-TU `static` design (needed for bit-exactness)
  pushes testing toward black-box conformance plus narrow white-box accessors.
- **Build flags pinned for bit-exactness**: `-fno-strict-aliasing` (RDRAM is aliased through
  `uint8/16/32*`), never `-ffast-math`. Single-worker is *required* (the noise RNG seed is
  worker-dependent), not merely convenient.
- **Cross-platform commitment**: build **and pass bit-exact** on Linux/clang, Windows/MSVC,
  and macOS/AppleClang, gated by CI. This makes the harness RNG's float distribution
  (implementation-defined across stdlibs) something we must replace with a portable one so
  test vectors are identical on all three.
- **Licensing**: the renderer derives from Angrylion (MAME license — restrictive
  redistribution / non-commercial). Treated as a personal/non-commercial port; carry
  Angrylion's license + CREDITS in-tree.

## Sub-projects

### Sub-project 1 — Renderer core (host-only, bit-exact) — **specced now**

Adapt the full Angrylion RDP + VI pipeline into an Orthodox C++ static library (`src/rdp`,
pure C ABI, single TU) that passes the entire parallel-rdp conformance suite **on the host
PC**. This is the foundation: bit-exactness is nailed where it can be measured byte-for-byte,
before any hardware constraint enters. Adds `src/rdp` (built for both platforms) and
`tests/conformance/` (host-only); leaves the template's `game/gfx/app/platform` scaffold
untouched. Includes the host **performance throughput probe** and **memory-budget worksheet**
feeding the feasibility gate. Detailed in
`2026-05-25-renderer-core-design.md`.

**Done when**: the full RDP + VI conformance suite passes bit-exact on all three host OSes,
Orthodoxy enforcement is active and clean on the `rdp` library, and the M6 feasibility gate
has produced a go/no-go performance + memory verdict.

### Sub-project 2 — PC app + demo (host)

SDL3 + ImGui front-end that drives the renderer (via the same command-injection path the
harness uses) and presents its RGBA8888 VI scanout: integer upscaling of the 240×240 image,
minimal UI. Plus the N64-style **demo** — a display-list generator based on `gldemo_n64`,
emitting RDP commands through the libultra GBI (`gbi.h`) idioms. No console-unsupported
features (no mouse drag, etc.). This is where the template's software gfx is superseded by
the RDP renderer for the demo path.

**Depends on**: sub-project 1 (the renderer + its command/output seams).

### Sub-project 3 — PicoSystem device port

Run the renderer on hardware: memory budgeting (LUTs → flash `const` via build-time codegen,
single worker, small RDRAM arena, FB-bounded hidden-RDRAM, banded rendering), **RDP + VI both
on core1** (core0 reserved for game logic and RSP-style matrix math / lighting / clipping /
shading), an ST7789 driver written from scratch on the bare RP2040 SDK (referencing the
PicoSystem libraries), and the demo running on the console. The device reads the RDP color
image from RDRAM and does its own RGBA5551→RGB565 blit — it does **not** run the bit-exact VI
(1.6 MB prescale won't fit). Debugging only via `pico_stdio_usb` (no debug header).

**Depends on**: sub-project 1 (and its seams designed for this: boundable hidden-RDRAM, the
`vdac_*`/RDRAM output seam, configurable RDRAM/worker knobs, isolated LUT declarations),
sub-project 2 (the demo). **Gated by** the sub-project-1 feasibility verdict.

## Dependencies & order

```
[1 Renderer core] ──► [2 PC app + demo] ──► [3 Device port]
        │                                        ▲
        └──────── feasibility gate (go/no-go) ───┘
```

Sequential. Sub-project 1's seams are deliberately designed to absorb the needs of 2 and 3 so
neither reaches back into the bit-exact core. The **feasibility gate** (host perf
extrapolation to the M0+ plus the memory-budget worksheet, evaluated at sub-project 1's M6
milestone) is a project-level go/no-go for committing to sub-project 3.

## References

- Angrylion RDP Plus: `~/development/repos/angrylion-rdp-plus` (renderer source to adapt)
- ParaLLEl RDP: `~/development/repos/parallel-rdp` (conformance harness + oracle wrapper)
- Project template: `~/development/projects/picosystem/picosystem-template`
- Orthodoxy plugin: `~/development/repos/orthodoxy`
- SDL3: `~/development/repos/SDL` · ImGui: `~/development/repos/imgui`
- Raspberry Pi Pico SDK: `~/development/repos/pico-sdk` · PicoSystem SDK:
  `~/development/repos/picosystem`
- Embedded TDD guidelines: `~/books/technology/tdd-summary`
- N64 demo: `~/development/n64/gldemo_n64` · libultra GBI:
  `~/development/repos/libultra_modern/include/PR/gbi.h`
- ST7789 datasheet: `~/Documents/datasheets/ST7789.pdf` · Adafruit ST7789 driver:
  `~/development/repos/Adafruit-ST7735-Library`
- PicoSystem schematic / RP2040 summaries: `~/Documents/datasheets/`
