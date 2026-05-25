# M6 Performance Extrapolation + Feasibility Verdict (Host → RP2040 M0+)

The **go/no-go gate** for committing to the device sub-project (sub-project 3).
Combines the **host throughput probe** (this doc) with the **memory budget**
(`docs/memory-budget.md`, Stream D.2). This is an **assessment**: it does not
block the renderer core's completion — it informs whether/how the device port
proceeds.

> All host numbers are reproducible: `tests/perf/throughput_probe.cpp`
> (`perf.throughput_probe` ctest). The headline rate is from a **Release/-O2**
> build; the registered ctest uses a short window and only checks the pipeline
> runs clean.

---

## 1. Host throughput probe — what it measures

`tests/perf/throughput_probe.cpp` drives a **representative full-pipeline
workload** through OUR renderer's real per-command dispatch (`rdpx_rdp_cmd`),
using the same vendored `CommandBuilder` the conformance harness uses, so the
emitted RDP command words are byte-identical to what the device would replay:

- **2-cycle** combiner (textured RGBA5551 × shade, then × primitive)
- **alpha blending** against the framebuffer (src-over, 2-cycle blender)
- **AA / coverage** enabled, `image_read_enable` on (framebuffer coverage reads)
- **depth test + depth write**, Z opaque
- **perspective-correct** texture coords (exercises the `tcdiv_table` path)
- two large triangles tiling a **240×240** RGBA5551 framebuffer + 240×240 Z

This is deliberately the *heavy* representative path (the slowest realistic
per-pixel config short of pathological cases), so the extrapolation is
conservative.

### Measured (host)

| Metric | Value |
|---|---|
| Host CPU | AMD Ryzen AI 7 PRO 350, ~4.99 GHz (single thread) |
| Compiler / build | clang 18, `-O2` Release |
| **Mpixels/s** | **~34.4** |
| ms/frame, 240×240 full (57,600 px) | **~1.67 ms** |
| Effective host cost/pixel | ~145 host core-cycles/pixel @ 5 GHz |

Debug `-O0` (the `host` preset default) measures ~9.9 Mpix/s — used only for the
fast ctest sanity run; the ~34.4 Mpix/s `-O2` figure is the one extrapolated.

---

## 2. RP2040 Cortex-M0+ facts (cited)

| Fact | Value | Source |
|---|---|---|
| Cores / clock | Dual Cortex-M0+, up to **133 MHz** | `summaries/01-introduction.md:63`, `07-clocks-osc-pll.md:52` |
| **No FPU** | M0+ has no hardware float; floats go through the bootrom **fast-float library** in software | `summaries/05-memory-bootrom.md:315-340` |
| Float op cost (single) | `_fmul` 58–69, `_fadd` 71, `_fdiv` 71, `_float2int` ~40 **cycles each** | `05-memory-bootrom.md:328-332` |
| **No hardware divide** | only single-cycle `MULS` (32×32); divide is a software routine | `summaries/03-cortex-m0plus.md:43,75` |
| ALU / multiply | MOVS/ADDS/…/MULS = **1 cycle**; LDR/STR word = **2 cycles** (AHB) | `03-cortex-m0plus.md:175-185` |
| Flash via **XIP** | 16 MB window, transparent serial-flash reads; can execute/`const`-read in place | `summaries/05-memory-bootrom.md:81` |
| **XIP cache** | **16 KB, 2-way set-associative, 1-cycle hit**; miss = serial-flash transfer (tens–hundreds of cycles) | `05-memory-bootrom.md:89` |
| SRAM | single-cycle, 264 KB total | `05-memory-bootrom.md:34`, `:66` |

---

## 3. Extrapolation host → M0+

Two independent reductions bound the device per-pixel cost.

### 3.1 Clock-ratio floor (best case, ignores microarchitecture)

The host runs at ~4.99 GHz with wide superscalar OoO execution and a real FPU;
the M0+ runs at 0.133 GHz, in-order, single-issue, no FPU. The raw clock ratio
alone is **4990 / 133 ≈ 37.5×**. So even if the M0+ executed the *same
instruction stream at the same IPC* (it cannot), the floor is:

- `1.67 ms × 37.5 ≈ 63 ms/frame` (full 240×240), i.e. **~16 fps** — already
  below interactive for a full pipeline.

### 3.2 Microarchitecture-derated estimate (realistic)

The clock floor is wildly optimistic because the M0+ also loses on **IPC** and
on **float emulation**:

- **IPC / issue width.** The Ryzen sustains ~3–4 IPC on this integer-heavy loop;
  the M0+ is single-issue in-order (≤1 IPC), and every word load is 2 cycles vs
  the host's effectively-free L1 hits. Conservatively **4–6× more core-cycles**
  per pixel for the integer body.
- **No FPU.** The Angrylion per-pixel inner loop (span walk, 9-bit combiner,
  blender, coverage, Z compare/write) is **fixed-point integer** — float is
  concentrated in **per-triangle setup** (edge slopes, `1/w`) and the
  perspective `tcdiv`/`tcoord` path. Each emulated single-float op is **58–74
  cycles** (`§2`). Per-triangle setup with ~tens of float ops ≈ a few thousand
  cycles — amortized over a large triangle this is small per-pixel, but for many
  small triangles it dominates. Per-pixel perspective division uses the
  `tcdiv_table` LUT (integer) rather than `_fdiv`, *if* the LUT read is cheap —
  which lands us on the XIP problem below.
- **XIP flash LUT latency (the dominant unknown).** The renderer's LUTs total
  ~890 KB and **must live in flash** (`docs/memory-budget.md §3`). The per-pixel
  hot ones are random-access:
  - `z_com_table` 512 KB, indexed by an 18-bit Z (`z_com_table[z & 0x3ffff]`) —
    **effectively uniform-random across 512 KB through a 16 KB cache** → near-100%
    miss rate, each a serial-flash transfer of **tens to hundreds of cycles**.
  - `tcdiv_table` 128 KB and `deltaz_comparator_lut` 128 KB — same shape, smaller.
  A depth-tested + perspective frame touches `z_com_table` and `tcdiv_table`
  **per pixel**. If even one of these misses XIP cache per pixel at ~50–150
  cycles, that **single stall dwarfs the entire integer pixel body** (~tens of
  cycles).

Putting it together (full pipeline, depth + perspective, LUTs in flash, no
mitigation):

| Component (per pixel) | M0+ cycles (order-of-magnitude) |
|---|---|
| Integer pixel body (combine/blend/cvg/Z) | ~150–400 (host body × IPC derate) |
| XIP miss: `z_com_table` | ~50–150 |
| XIP miss: `tcdiv_table` (perspective) | ~50–150 |
| **Total/pixel** | **~250–700 cycles** |

At 133 MHz that is **~1.9–5.3 µs/pixel**, so for 240×240 = 57,600 px:

| Scenario | Estimated ms/frame | fps |
|---|---|---|
| **Optimistic** (250 cyc/px, LUTs mostly hit / recomputed) | **~108 ms** | ~9 |
| **Pessimistic** (700 cyc/px, full XIP thrash) | **~303 ms** | ~3 |
| Clock-ratio floor (§3.1, unreachable) | ~63 ms | ~16 |

**Banded 240×60** (one 1/4-height band = 14,400 px) scales ~linearly in pixels:

| Scenario | ms per band | ms/frame (×4 bands) |
|---|---|---|
| Optimistic | ~27 ms | ~108 ms |
| Pessimistic | ~76 ms | ~303 ms |

Banding **does not improve total pixel throughput** — it is a RAM-fit technique
(`docs/memory-budget.md §4`), and if the command stream must be re-replayed per
band, banding *adds* setup cost (×4 triangle setup). The per-frame pixel time is
unchanged or slightly worse.

### 3.3 Dual-core

Both M0+ cores can run a render worker (the budget already accounts for 2 live
`rdp_state` workers). A clean static split of the framebuffer roughly **halves**
pixel time: optimistic ~54 ms/frame (~18 fps), pessimistic ~150 ms/frame (~7 fps)
— **if** the XIP subsystem isn't the bottleneck. But there is **one shared XIP
cache and one serial-flash channel**: if both cores random-read the 512 KB
z-table, they contend for the same 16 KB cache and the same QSPI link, so the
LUT-miss stalls do **not** halve and may worsen (cache thrash across two access
streams). Dual-core helps the integer body, not the flash wall.

---

## 4. Verdict: **CONDITIONAL GO** (NO-GO as-is)

**Committing to the device sub-project is CONDITIONAL** — proceed only behind
the structural changes below; the **stock Angrylion port will not run on the
PicoSystem** for either RAM or speed.

### Why not a flat GO

- **RAM (hard wall): NO-GO as-is.** `docs/memory-budget.md` shows neither
  full-frame (~600 KB) nor banded (~389 KB) fits the 264 KB SRAM with stock
  structures, dominated by `rdp_state` (143 KB, `span[1024]`=96 KB) and
  `rdp_cmd_buf` (176 KB) — *not* the framebuffers.
- **Speed (soft wall): not interactive as-is.** Even the unreachable clock-ratio
  floor is ~63 ms/frame (~16 fps) full-frame; the realistic estimate is
  **~108–303 ms/frame (~3–9 fps)**, dominated by **XIP random-access misses on
  the 512 KB `z_com_table`**.

### Why not a flat NO-GO

The blockers are **addressable** and already enumerated:

- **RAM** is fixable by the budget's §4 trims: resize `span[1024]→band height`
  (−92 KB), bound the VI arrays or drop the on-device VI stage (−38 KB), shrink
  `CMD_BUFFER_SIZE 1024→~128` (−154 KB), FB-bound the hidden-RDRAM (4 MB→14–56
  KB). Result: ~104 KB for one worker — fits with a 2nd core and stacks.
- **Speed**'s dominant risk (`z_com_table` XIP thrash) is fixable by
  **recomputing the Z encode arithmetically** instead of the 512 KB LUT (it is a
  small exponent/mantissa transform, `zbuffer.c`), trading ~hundreds of flash
  cycles for ~tens of integer ALU cycles — and the M0+ has a single-cycle
  multiplier. Same lever for `tcdiv_table`. With the hot LUTs recomputed or
  pinned, the per-pixel cost drops toward the "optimistic" column.

### Conditions for GO (must hold before device work commits)

1. **Resize the upstream statics** (`span`, VI arrays, `CMD_BUFFER_SIZE`) and
   **FB-bound the hidden-RDRAM** to land under 264 KB for 2 workers
   (`docs/memory-budget.md §4`). *This is a structural port change, not a tweak.*
2. **Eliminate the per-pixel 512 KB `z_com_table` XIP dependency** — recompute
   the Z encode (preferred) or pin a hot sub-range in SRAM. Re-measure.
3. **Accept the performance envelope**: even after (1)+(2), expect roughly
   **single-digit to low-teens fps** full-frame for the *full* pipeline.
   The device target should therefore be **a reduced feature subset and/or
   lower fill** (e.g. simpler combine/no-AA for most draws, smaller dirty
   regions), not the full 2-cycle+AA+blend+perspective path at 240×240/60 Hz.
4. **Confirm the dual-core XIP-contention assumption** with a flash-resident-LUT
   micro-benchmark on real hardware before relying on a 2× core split.

### Key risks (carried forward)

1. **512 KB `z_com_table` XIP cache thrashing** — the headline per-pixel wall
   (`docs/memory-budget.md §3`, residual risk #1). Dominates §3.2.
2. **`rdp_state` (143 KB) + `rdp_cmd_buf` (176 KB) RAM shrink** required; these,
   not the framebuffers, are the RAM blockers (`memory-budget.md §4-5`).
3. **Banding re-replays the command stream per band** (×4 setup) and doesn't
   improve pixel throughput — RAM-fit only.
4. **No FPU**: triangle setup (float-heavy) costs ~58–74 cycles per float op;
   tolerable when amortized over large triangles, punishing for many small ones.
5. **Dual-core does not halve the flash-LUT stall** (shared XIP cache + QSPI).

---

## 5. Bottom line

| Axis | As-is | After conditions §4 |
|---|---|---|
| RAM fit (264 KB) | **NO** (~389–600 KB) | YES (~104 KB/worker, banded) |
| Full-pipeline 240×240 speed | **~3–9 fps** | low-teens fps (subset/region-limited) |
| **Verdict** | **NO-GO** | **GO (CONDITIONAL)** |

The renderer core is correct and bit-exact on the host (M1–M6 conformance,
incl. coverage/AA with load-bearing hidden-RDRAM compares). The **device port is
viable but only as a deliberately reduced, RAM-resized, LUT-recomputed
subset** — not a stock Angrylion drop-in. Proceed to sub-project 3 **gated on
the four conditions above**, starting with the `z_com_table` recompute spike and
the static-resize, then re-measure on hardware.
