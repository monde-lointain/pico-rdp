# Licensing

This document explains what derives from what in the PicoSystem RDP project,
why the project is bound by a restrictive (non-commercial) redistribution
constraint, and what that means in practice. It is prose context for the
machine-oriented `LICENSE`, `NOTICE`, and `CREDITS.md` files at the repository
root; where they disagree, the verbatim license texts in `NOTICE` are
authoritative.

## What this project is

PicoSystem RDP is a **personal, non-commercial** port of the Angrylion N64 RDP
renderer to the Pimoroni PicoSystem (RP2040). It is not a product, is not for
sale, and is not endorsed by or affiliated with MAME, Nicola Salmoria, or any
upstream author.

## What derives from what

```
MESS 0.128 RDP code  (Ville Linde, MooglyGuy, et al.)
        |
        v
Angrylion RDP plugin  (angrylion)            -- MAME license
        |
        v
Angrylion RDP Plus  (ata4 + contributors)    -- MAME license
        |
        v
PicoSystem RDP renderer core  (this project) -- inherits MAME license
```

The renderer core in this repository is a **derivative work** of Angrylion RDP
Plus, which is itself a derivative of MESS/MAME source. Angrylion's own
`CREDITS.txt` states plainly: *"The code comes under MAME license."* A
derivative of MAME-licensed code carries the MAME license forward — there is no
relicensing path, because (per the MAME license itself) there is no central
copyright authority to license the code from.

## The constraint the core imposes

The MAME license (reproduced verbatim in `NOTICE`) permits redistribution and
use of derivative works **only** under these conditions:

- **No commercial use, no sale.** Redistributions may not be sold, nor used in
  a commercial product or activity. This explicitly extends to "freebie"
  bundling, donations for porting work, and non-profit fundraising.
- **Complete corresponding source.** Modified redistributions must include the
  complete source code for everything needed to build the binary (excluding
  the normal OS/compiler/kernel components).
- **Notice reproduction.** The copyright notice, the conditions, and the
  disclaimer must be reproduced in accompanying materials.
- **No trademark use.** "MAME" and the MAME logo are a registered trademark of
  Nicola Salmoria and may not be used without permission.

Because this is the **most restrictive** term among all components, it governs
the project as a whole for redistribution purposes. Practically: this repo can
be shared freely for non-commercial, source-available use, but cannot be sold,
bundled into a commercial product, or used to solicit donations for the port.

## The permissive third-party components

Everything else the project bundles or depends on is permissive and imposes no
meaningful constraint beyond attribution/notice retention. None of these
loosen the MAME constraint; the MAME-derived core still governs.

- **ParaLLEl RDP** — MIT (Themaister). Used as a reference / conformance
  harness.
- **SDL** — zlib license. Used by the PC app (windowing/input/video).
- **Dear ImGui** — MIT (Omar Cornut). Used by the PC app (debug/settings GUI).
- **Raspberry Pi Pico SDK** — BSD-3-Clause. Device SDK for the RP2040 target.
- **PicoSystem SDK** — MIT (Pimoroni). Device hardware abstraction.
- **Adafruit ST7735/ST7789 Library** — MIT (Limor Fried/Adafruit). Reference
  for the ST7789 display driver.

## The one copyleft tool

- **Orthodoxy** (clang-tidy plugin) — GPL-3.0. This is used **only as a
  build-time lint tool**. It is never linked into or shipped with any build
  artifact, so its copyleft terms do not propagate to the project's binaries
  or source.

## Bottom line

Treat this repository as MAME-licensed: **non-commercial, source-available,
no sale, no trademark use, notices retained.** The permissive (MIT/zlib/BSD)
dependencies and the GPL-3.0 build tool do not change that — the
Angrylion-derived core is the governing license.

> Note: license interpretation here is a good-faith reading of the upstream
> texts, not legal advice. If anything about the MAME terms is unclear for a
> particular use, consult the verbatim text in `NOTICE` and seek qualified
> advice rather than relying on this summary.
