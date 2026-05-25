# Device Memory-Budget Worksheet (RP2040 / PicoSystem)

Living worksheet for the M6 go/no-go gate. Sums every RAM consumer against the
RP2040 264 KB SRAM budget for two scenarios:

- **Full-frame**: 240×240 framebuffer
- **Banded**: 240×60 strip (a 1/4-height band; frame composited in 4 passes)

Every number below traces to a cited source. Sizes from the Angrylion source are
GROUND TRUTH (actual array declarations). Struct byte sizes were computed with a
`sizeof` probe compiled for a **32-bit ILP32 target** (`gcc -m32`), matching the
Cortex-M0+ ABI exactly (`int` = 4, pointer = 4, same alignment), so they are not
estimates.

> Convention: 1 KB = 1024 bytes.

---

## 1. RP2040 SRAM budget

| Item | Size | Source |
|---|---|---|
| Total on-chip SRAM | **264 KB** | `~/Documents/datasheets/summaries/05-memory-bootrom.md:34` ("Total: 264 kB on-chip, physically 6 banks") |
| Physical layout | 4×64 KB main banks @ `0x20000000` + 2×4 KB small banks @ `0x20040000`/`0x20041000`; logically one contiguous 264 KB region | `05-memory-bootrom.md:40-43` |
| Optional extra (XIP cache as SRAM, cache disabled) | +16 KB @ `0x15000000` | `05-memory-bootrom.md:72` — **not counted**; disabling XIP cache would cripple flash LUT random reads (see §3) |
| Optional extra (USB DPRAM, USB unused) | +4 KB @ `0x50100000` | `05-memory-bootrom.md:73` — not counted (USB likely used for dev) |

### Reserved (not available to the renderer)

| Reservation | Assumed size | Note |
|---|---|---|
| Core 0 stack | ~4 KB | Pico SDK default `PICO_STACK_SIZE` = 0x800 (2 KB); rounded up. Per-core. |
| Core 1 stack | ~4 KB | Dual Cortex-M0+ (`05-memory-bootrom.md:9`, `03-cortex-m0plus.md`). The plan runs two render workers, one per core, so **both stacks are live**. |
| Core 1 scratch / bootrom workspace, `.data`/`.bss` of SDK + ST7789 driver + app | ~8–16 KB (TBD) | Placeholder until link map exists; refine from the actual `.map` once a build links. |
| **Reserved subtotal (working assumption)** | **~16–24 KB** | |

**Renderer working budget ≈ 264 − 20 ≈ 244 KB.** Treat 264 KB as the hard ceiling
and ~244 KB as the practical ceiling; flags below use the 264 KB hard ceiling.

---

## 2. Per-consumer RAM table

`W` = framebuffer width-in-pixels-per-row's height term; here the N64 FB is
240 wide, so a buffer is `240 × H × bytes`. RGBA5551 = 2 bytes/pixel (matches the
ST7789 RGB565/16bpp path and N64 16-bit FB size).

| Consumer | Formula | Full-frame (H=240) | Banded (H=60) | Source |
|---|---|---|---|---|
| Color framebuffer | 240·H·2 | 115,200 B (112.5 KB) | 28,800 B (28.1 KB) | N64 16-bit FB (`PIXEL_SIZE_16BIT`, `rdp.c:18`); 240×240 panel `~/Documents/datasheets/ST7789.pdf` |
| Z-buffer | 240·H·2 | 115,200 B (112.5 KB) | 28,800 B (28.1 KB) | N64 Z is 16-bit (`zbuffer.c` z_com encodes to `uint16_t`, `zbuffer.c:8`) |
| **Hidden-RDRAM — FULL** | RDRAM_MAX_SIZE/2 | 4,194,304 B (4096 KB) | 4,194,304 B (4096 KB) | `rdram.c:30` `static uint8_t rdram_hidden[RDRAM_MAX_SIZE/2]`; `n64video.h:6` `RDRAM_MAX_SIZE 0x800000` (8 MB) → 4 MB. **Indexed by absolute RDRAM word** (`rdram.c:124/138`). Physically impossible on RP2040. |
| **Hidden-RDRAM — FB-bounded** | 240·H·1 | 57,600 B (56.2 KB) | 14,400 B (14.1 KB) | Logical need: 1 hidden byte per FB pixel-word (carries the extra coverage/RGBA-bit; see `rdram.c:124-151`). Requires re-basing the index to the FB region instead of absolute RDRAM. |
| `rdp_state` (one worker) | `sizeof(struct rdp_state)` | 146,352 B (142.9 KB) | 146,352 B (142.9 KB) | `rdp.c:190-348`; probe = 146,352 B. **Includes** `span[1024]`, `tmem[0x1000]`, `cvgbuf[1024]`, and the two VI arrays — see breakdown below. |
| RDP command buffer | `1024 × (176/4) × 4` | 180,224 B (176.0 KB) | 180,224 B (176.0 KB) | `n64video.c:37` `CMD_BUFFER_SIZE 1024`, `:40` `CMD_MAX_SIZE 176`, `:118` `rdp_cmd_buf[1024][44]` uint32 |
| RDP-referenced textures / display list | RDRAM-resident on N64; on device must be staged from flash or host | (see §5) | (see §5) | DLs/textures live in N64 RDRAM; not a fixed static array — sized per-ROM. Out-of-line risk, not a fixed cost. |

### `rdp_state` internal breakdown (probe, 32-bit)

| Field | Size | Source |
|---|---|---|
| `struct span span[1024]` | 98,304 B (96.0 KB) | `rdp.c:200`; `sizeof(struct span)` = 96 B × 1024. **Sized for N64 max scanlines, not 240.** |
| `struct n64video_pixel viaa_array[0xa10<<1]` | 20,608 B (20.1 KB) | `rdp.c:346`; 5152 px × 4 B (`n64video.h:67-73`). VI post-filter stage. |
| `struct n64video_pixel divot_array[0xa10<<1]` | 20,608 B (20.1 KB) | `rdp.c:347` |
| `uint8_t tmem[0x1000]` | 4,096 B (4.0 KB) | `rdp.c:337` — N64 TMEM is exactly 4 KB |
| `uint8_t cvgbuf[1024]` | 1,024 B (1.0 KB) | `rdp.c:334` |
| `struct tile tile[8]` | 736 B | `rdp.c:242`; `sizeof(struct tile)` = 92 B × 8 |
| `struct other_modes` | 184 B | `rdp.c:228`; `sizeof` = 184 B |
| everything else (colors, combiner/blender ptr arrays, scalars) | ~896 B | remainder |
| **`sizeof(struct rdp_state)`** | **146,352 B (142.9 KB)** | probe |

> Note: upstream declares `struct rdp_state state[PARALLEL_MAX_WORKERS]` with
> `PARALLEL_MAX_WORKERS = 64u` (`rdp.c:350`, `parallel.h:10`) = **9.37 MB**.
> On the 2-core device only **2** workers exist, so this table counts **1** worker
> in the shared column and notes the ×2 below. Two full unmodified `rdp_state`
> = 285.8 KB — **alone exceeds the entire 264 KB SRAM.**

---

## 3. Lookup tables → MUST live in flash (`const`)

These are read-only after init and total ~868 KB of pre-computable data. They
cannot fit in SRAM and must be emitted as flash `const` (or generated into flash
at build time). Sum of the actual declarations:

| LUT | Decl | Bytes | Source |
|---|---|---|---|
| `z_com_table` | `uint16_t[0x40000]` | 524,288 (512 KB) | `zbuffer.c:8` |
| `deltaz_comparator_lut` | `uint16_t[0x10000]` | 131,072 (128 KB) | `zbuffer.c:10` |
| `tcdiv_table` | `int32_t[0x8000]` | 131,072 (128 KB) | `tcoord.c:35` |
| `z_complete_dec_table` | `uint32_t[0x4000]` | 65,536 (64 KB) | `zbuffer.c:9` |
| `bldiv_hwaccurate_table` | `uint8_t[0x8000]` | 32,768 (32 KB) | `blender.c:5` |
| `gamma_dither_table` | `uint8_t[0x4000]` | 16,384 (16 KB) | `vi/gamma.c:4` |
| `vi_restore_table` | `int[0x400]` | 4,096 (4 KB) | `vi/restore.c:3` |
| `special_9bit_clamptable` | `uint32_t[512]` | 2,048 | `combiner.c:3` |
| `special_9bit_exttable` | `int32_t[512]` | 2,048 | `combiner.c:4` |
| `log2table` | `int32_t[256]` | 1,024 | `tcoord.c:34` |
| `gamma_table` | `uint8_t[0x100]` | 256 | `vi/gamma.c:3` |
| `norm_point_table` | `int32_t[64]` | 256 | `tcoord.c:3` |
| `norm_slope_table` | `int32_t[64]` | 256 | `tcoord.c:14` |
| `z_dec_table` | `{u32,u32}[8]` | 64 | `zbuffer.c:12` |
| `maskbits_table` | `int32_t[16]` | 64 | `tcoord.c:33` |
| `bayer_matrix` | `uint8_t[16]` | 16 | `dither.c:3` |
| `magic_matrix` | `uint8_t[16]` | 16 | `dither.c:12` |
| **Total** | | **911,264 B (≈ 890 KB)** | |

> The plan cites "~866 KB"; the delta is `deltaz_comparator_lut` (128 KB), which
> upstream fills at runtime (`zbuffer.c:10`, populated in init) rather than
> shipping as `const`. Excluding it gives **780,192 B (762 KB)**; including it
> as flash `const` gives 890 KB. Either way it must NOT be SRAM-resident.

### XIP random-access concern (512 KB z-table)

The 16 MB flash is reached via XIP through a **16 KB, 2-way set-associative
cache, 1-cycle hit** (`05-memory-bootrom.md:89`). `z_com_table` (512 KB) is
indexed by a 18-bit Z value (`z_com_table[z & 0x3ffff]`, `zbuffer.c:193`) — i.e.
**effectively random access across 512 KB through a 16 KB cache** → near-total
cache misses, each a serial-flash transfer (tens to hundreds of cycles). This is
the dominant per-pixel latency risk. Mitigations to evaluate at M6:
- Recompute the z-encode arithmetically instead of table lookup (it is a small
  exponent/mantissa transform — see `zbuffer.c:380-393`), trading flash reads for
  M0+ ALU ops.
- Pin a hot sub-range in SRAM; or shrink precision.
- Same concern, smaller, for `tcdiv_table`/`deltaz_comparator_lut` (128 KB each).

---

## 4. Two-layout SRAM summary

Using **1 worker's** `rdp_state` and one shared command buffer in each column
(then noting the ×2-worker variant). Hidden-RDRAM shown both FULL and FB-bounded.

### Full-frame 240×240

| Consumer | Bytes | KB |
|---|---:|---:|
| Color FB | 115,200 | 112.5 |
| Z-buffer | 115,200 | 112.5 |
| Hidden-RDRAM (FB-bounded) | 57,600 | 56.2 |
| `rdp_state` ×1 | 146,352 | 142.9 |
| Command buffer | 180,224 | 176.0 |
| **Subtotal (FB-bounded hidden, 1 worker)** | **614,576** | **600.2** |
| with FULL hidden (4 MB) instead | 4,751,280 | 4639.9 |
| add 2nd worker `rdp_state` (×2 total) | +146,352 | +142.9 |

**FLAG: full-frame OVER 264 KB in every variant** (min 600.2 KB ≫ 264 KB).

### Banded 240×60

| Consumer | Bytes | KB |
|---|---:|---:|
| Color FB | 28,800 | 28.1 |
| Z-buffer | 28,800 | 28.1 |
| Hidden-RDRAM (FB-bounded) | 14,400 | 14.1 |
| `rdp_state` ×1 | 146,352 | 142.9 |
| Command buffer | 180,224 | 176.0 |
| **Subtotal (FB-bounded hidden, 1 worker)** | **398,576** | **389.2** |
| with FULL hidden (4 MB) instead | 4,578,480 | 4471.2 |
| add 2nd worker `rdp_state` (×2 total) | +146,352 | +142.9 |

**FLAG: banded OVER 264 KB in every variant** (min 389.2 KB > 264 KB).

> Neither scenario fits with the **unmodified** Angrylion data structures. The two
> dominant consumers are NOT the framebuffers — they are the upstream
> `rdp_state` (143 KB, of which `span[1024]` = 96 KB) and the `rdp_cmd_buf`
> (176 KB). Banding shrinks only the FB/Z/hidden trio; it does not touch those two.

### What banding + FB-bounded buys vs. trimming the big two

| Consumer | Full-frame | Banded | Reduction from banding |
|---|---:|---:|---:|
| FB + Z + hidden(FB-bounded) | 225.2 KB | 70.3 KB | −154.9 KB |
| `rdp_state` (unchanged) | 142.9 KB | 142.9 KB | 0 |
| cmd buffer (unchanged) | 176.0 KB | 176.0 KB | 0 |

To actually fit 264 KB, the port must **resize the upstream statics** (these are
the levers, all in owned-by-other-streams source — listed here only as budget
targets, not edits):

| Lever | Upstream | If trimmed to band | Saving |
|---|---|---|---|
| `span[N]` depth | 1024 (`rdp.c:200`) → 60 (band height) | 60×96 = 5,760 B | −92.5 KB |
| VI `viaa_array`+`divot_array` | 0xa10<<1 each (`rdp.c:346-347`) → 240 px each | 240×4×2 = 1,920 B | −38.3 KB (or move VI off-worker) |
| `CMD_BUFFER_SIZE` | 1024 (`n64video.c:37`) → e.g. 128 | 128×44×4 = 22,528 B | −154.0 KB |

Illustrative banded budget **after** these trims, **1 worker**:
FB 28.1 + Z 28.1 + hidden(FB) 14.1 + rdp_state(trimmed ≈ 142.9−92.5−38.3 = 12.1)
+ cmd(128-deep) 22.0 ≈ **104.4 KB** → fits with headroom for a 2nd worker
(+~12 KB) and reserved (~20 KB). **This is the target configuration, not the
current one.**

---

## 5. Conclusion

- **As-is (stock Angrylion sizes): neither layout fits.** Full-frame ≈ 600 KB,
  banded ≈ 389 KB, both > 264 KB, even with FB-bounded hidden-RDRAM and a single
  worker. **M6 go/no-go: NO-GO without structural resizing.**
- **The framebuffers are not the problem.** The blockers are the upstream
  `rdp_state` (143 KB, dominated by `span[1024]` = 96 KB, sized for N64 scanline
  counts not 240) and the 176 KB `rdp_cmd_buf`. Banding alone saves ~155 KB off
  the FB/Z/hidden trio but touches neither blocker.
- **FB-bounded hidden-RDRAM is mandatory** (full 4 MB is physically impossible)
  and saves 4 MB → tens of KB; it requires re-basing `rdram_hidden`'s absolute
  word index (`rdram.c:124-151`) to the FB region.
- **All LUTs (~890 KB, or ~762 KB if `deltaz_comparator_lut` is recomputed) must
  be flash `const`.** The 512 KB `z_com_table` is the headline XIP-random-access
  risk through the 16 KB cache; favour recomputing the Z encode over the table.
- **Feasible target = banded 240×60 + FB-bounded hidden + resized `span`/VI/cmd
  buffers** (see §4 trims): ≈ 104 KB for one worker, leaving room for a second
  core's worker and reserved stacks under 264 KB.

### Residual risks
1. XIP cache thrashing on the 512 KB `z_com_table` (and 128 KB `tcdiv_table`,
   `deltaz_comparator_lut`) — likely the per-pixel performance wall, not a RAM wall.
2. Banding multiplies command/DL replay by the band count (4×) unless the command
   stream is partitioned per band — re-execution cost vs. RAM trade.
3. Reserved-RAM figure (~20 KB) is a placeholder; confirm from the linked `.map`.
4. RDP-referenced textures/display lists are per-ROM and unbounded here; need a
   streaming/staging budget once a representative ROM is profiled (§2 last row).
5. Two live workers double `rdp_state`; trimmed size must stay small enough that
   2× fits alongside FB/Z/cmd.

### Unresolved questions
- Actual reserved SRAM (stacks + SDK/driver `.data`/`.bss`): need a real link map.
- Final `span` depth, `CMD_BUFFER_SIZE`, and whether the VI post-filter stage runs
  on-device at all (its 40 KB of arrays could be dropped if VI is simplified).
- Is hidden-RDRAM even needed for the target feature set (coverage/AA), or can it
  be dropped to reclaim its FB-bounded 14–56 KB?
- One shared command buffer vs. per-band buffers under banding.
