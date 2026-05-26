# Credits and Attribution

This project stands on the work of many others. Credits below are quoted or
summarized from each upstream project's own attribution files; see NOTICE for
the corresponding license texts and source paths.

## Angrylion RDP / Angrylion RDP Plus (renderer core)

The renderer core is derived from Angrylion RDP Plus, which is derived from
MESS / MAME source code. From the upstream `CREDITS.txt`:

> This little RDP plugin was initially based on MESS 0.128 source code.
> Many thanks to Ville Linde, MooglyGuy and other people who wrote the RDP
> implementation in MESS 0.128. The rest of the code is by me, angrylion.
> Many thanks to people who helped me in various ways: olivieryuyu,
> marshallh, LaC, oman, pinchy, ziggy, FatCat, LegendOfDragoon and other
> folks I forgot.
> The code comes under MAME license.

- angrylion — original RDP plugin author.
- Ville Linde, MooglyGuy, and other MESS 0.128 RDP authors.
- Angrylion RDP Plus fork maintainers (ata4 and contributors).
- MAME is the work of hundreds of developers, each owning the copyright to
  the code they wrote.

## ParaLLEl RDP

- Themaister (Hans-Kristian Arntzen) — ParaLLEl RDP, Copyright (c) 2020.
- Vendored (MIT) into `tests/conformance/vendor/`:
  `triangle_converter`, `primitive_setup`, `rdp_command_builder`, `rdp_dump`
  — used as test-only oracle / capture helpers.

## SDL (Simple DirectMedia Layer)

- Sam Lantinga and the SDL contributors — Copyright (C) 1997-2026
  Sam Lantinga.

## Dear ImGui

- Omar Cornut and the Dear ImGui contributors — Copyright (c) 2014-2026
  Omar Cornut. Used on the docking branch (`v1.92.8-docking`) for the
  `rdp_viewer` debug GUI.

## Raspberry Pi Pico SDK

- Raspberry Pi (Trading) Ltd. — Copyright 2020.

## PicoSystem SDK

- Pimoroni Ltd — Copyright (c) 2021.

## Adafruit ST7735 / ST7789 Library

- Written by Limor Fried/Ladyada for Adafruit Industries.

## Orthodoxy (build-time tooling)

- Orthodoxy clang-tidy plugin authors — used only as a development-time lint
  tool; not linked into or redistributed with the renderer.

## libultra (gbi.h, format reference)

- N64 libultra `gbi.h` — consulted only as a reference for GBI display-list
  bitfield layouts. No libultra code is copied into the project.

## Asset-generation tool (separate GPLv3 work)

- `tools/gen_n64_assets.py` is a standalone GPLv3 tool (see `tools/LICENSE.gpl3`)
  that may adapt Fast64 (GPLv3). It links no Angrylion code and is merely
  aggregated in this repo. Its *generated* data tables are unencumbered output
  (like compiler output) and ship under the project's normal terms.
