#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
#
# gen_n64_assets.py -- N64 demo asset / table code generator for the
# PicoSystem RDP port (sub-project 2, demo_core).
#
# Copyright (C) 2026  PicoSystem RDP port contributors
#
# This program is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 3 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program.  If not, see <https://www.gnu.org/licenses/>.
# A copy is provided alongside this file as tools/LICENSE.gpl3.
#
# ---------------------------------------------------------------------------
# This tool is a SEPARATE GPLv3 work.  It links no Angrylion code; its mere
# presence in the repo is aggregation.  Its GENERATED OUTPUT (the committed
# C headers in src/demo/generated/) is unencumbered data, like compiler
# output, and ships under the project's own (non-GPL) terms.
#
# The CI4 / TLUT export algorithm is ADAPTED from Fast64 (GPLv3):
#   fast64_internal/f3d/f3d_texture_writer.py
#     extractConvertCIPixel / getColorsUsedInImage /
#     getColorIndicesOfTexture / compactNibbleArray / writePaletteData
#   fast64_internal/utility.py  getRGBA16Tuple
# Adapted to read via PIL (top-down) instead of Blender (bottom-up).
#
# Matrix math mirrors libultra (SGI) guPerspectiveF / guLookAtF / guRotateF /
# guMtxCatF conventions (row-major storage, row-vector v' = v . M).
# ===========================================================================

import os
import sys
import math

try:
    from PIL import Image
except ImportError:
    sys.stderr.write("error: Pillow (PIL) is required\n")
    sys.exit(1)

# --- paths -----------------------------------------------------------------
SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
REPO_ROOT = os.path.normpath(os.path.join(SCRIPT_DIR, ".."))
ASSET_PNG = os.path.join(REPO_ROOT, "assets", "arrows.png")
OUT_DIR = os.path.join(REPO_ROOT, "src", "demo", "generated")

# --- fixed-point convention (MANDATED Q16.16, matches demo_core fixed.h) ---
DEMO_FIX_FRAC = 16
DEMO_FIX_ONE = 1 << DEMO_FIX_FRAC  # 65536

# --- animation periods (MANDATED by design) --------------------------------
PYRAMID_PERIOD = 30   # frames for a full pyramid rotation
CUBE_PERIOD = 40      # frames for a full cube rotation
ANIM_PERIOD = 120     # lcm(30, 40)

# Per-frame rotation degrees: a full 360 deg over the period.
#   gldemo render.c: PYRAMID_DEG_PER_FRAME = 12.0/60.0, CUBE_DEG_PER_FRAME =
#   9.0/60.0 (negative).  Baking one frame == one gldemo "12"/"9" step, so the
#   baked frame rate is 360/30 = 12 deg/frame (pyramid, +Y) and 360/40 = 9
#   deg/frame (cube, -axis(1,1,1)).  The numerators (12, 9) are exactly
#   gldemo's per-second rates -> periods 30 and 40 close the loop.
PYRAMID_DEG_PER_FRAME = 360.0 / PYRAMID_PERIOD   # 12.0, +Y
CUBE_DEG_PER_FRAME = -360.0 / CUBE_PERIOD        # -9.0, axis (1,1,1)

# --- screen / camera (gldemo render.c) -------------------------------------
# guPerspective(45 deg FOV, aspect, near=10, far=1000, scale=1.0)
# guLookAt(eye=(0,0,300), at=(0,0,0), up=(0,1,0))
# Aspect overridden to 1.0 for the 240x240 square framebuffer (device parity).
SCREEN_DIM = 240
FOVY = 45.0
ASPECT = 1.0
NEAR = 10.0
FAR = 1000.0
PERSP_SCALE = 1.0
EYE = (0.0, 0.0, 300.0)
AT = (0.0, 0.0, 0.0)
UP = (0.0, 1.0, 0.0)
PYRAMID_TRANSLATE = (-75.0, 0.0, 0.0)   # render.c drawPyramid guTranslate
CUBE_TRANSLATE = (75.0, 0.0, -50.0)     # render.c drawCube guTranslate
CUBE_AXIS = (1.0, 1.0, 1.0)             # render.c drawCube guRotate axis


# ===========================================================================
# Texture: CI4 + RGBA5551 TLUT  (adapted from Fast64, GPLv3)
# ===========================================================================

def get_rgba16_tuple(color):
    """Fast64 getRGBA16Tuple: RGBA5551 (R5 G5 B5 A1), float [0,1] in."""
    return (
        ((int(round(color[0] * 0x1F)) & 0x1F) << 11)
        | ((int(round(color[1] * 0x1F)) & 0x1F) << 6)
        | ((int(round(color[2] * 0x1F)) & 0x1F) << 1)
        | (1 if color[3] > 0.5 else 0)
    )


def extract_convert_ci_pixel(rgba, i, j, w):
    """Fast64 extractConvertCIPixel: quantize one pixel to RGBA5551."""
    r, g, b, a = rgba[j * w + i]
    return get_rgba16_tuple((r / 255.0, g / 255.0, b / 255.0, a / 255.0))


def get_colors_used(rgba, w, h):
    """Fast64 getColorsUsedInImage, in N64 top-down order (PIL is top-down)."""
    palette = []
    for j in range(h):
        for i in range(w):
            pc = extract_convert_ci_pixel(rgba, i, j, w)
            if pc not in palette:
                palette.append(pc)
    return palette


def get_color_indices(rgba, palette, w, h):
    """Fast64 getColorIndicesOfTexture: per-pixel palette index, top-down."""
    texture = []
    for j in range(h):
        for i in range(w):
            pc = extract_convert_ci_pixel(rgba, i, j, w)
            texture.append(palette.index(pc))
    return texture


def compact_nibble_array(texture, w, h):
    """Fast64 compactNibbleArray: pack 4-bit indices, first pixel -> high nibble."""
    data_size = (w * h) // 2
    nibble = [((texture[i * 2] & 0xF) << 4) | (texture[i * 2 + 1] & 0xF)
              for i in range(data_size)]
    if (w * h) % 2 == 1:
        nibble.append((texture[-1] & 0xF) << 4)
    return bytes(nibble)


def build_texture():
    im = Image.open(ASSET_PNG).convert("RGBA")
    w, h = im.size
    rgba = list(im.getdata())  # top-down, row 0 = top

    palette = get_colors_used(rgba, w, h)
    if len(palette) > 16:
        sys.stderr.write(
            "error: %s quantizes to %d RGBA5551 colors; CI4 holds <= 16\n"
            % (os.path.basename(ASSET_PNG), len(palette)))
        sys.exit(1)

    indices = get_color_indices(rgba, palette, w, h)
    packed = compact_nibble_array(indices, w, h)

    # round-trip self-check: index -> palette -> RGBA5551 == quantized pixel
    for k, idx in enumerate(indices):
        i = k % w
        j = k // w
        want = extract_convert_ci_pixel(rgba, i, j, w)
        got = palette[idx]
        assert got == want, "round-trip mismatch at (%d,%d)" % (i, j)

    # TLUT is 16 entries, big-endian RGBA5551; pad unused slots with 0.
    tlut = list(palette) + [0] * (16 - len(palette))
    return w, h, packed, tlut, len(palette)


# ===========================================================================
# Matrices (libultra-faithful, baked to Q16.16)
# ===========================================================================

def mat_identity():
    return [[1.0 if i == j else 0.0 for j in range(4)] for i in range(4)]


def mat_mul(a, b):
    """Standard row-major matrix product R = A * B (libultra guMtxCatF)."""
    r = [[0.0] * 4 for _ in range(4)]
    for i in range(4):
        for j in range(4):
            s = 0.0
            for k in range(4):
                s += a[i][k] * b[k][j]
            r[i][j] = s
    return r


def gu_perspective(fovy, aspect, near, far, scale):
    """libultra guPerspectiveF -> mf[4][4] (row-major)."""
    mf = mat_identity()
    fovy_rad = fovy * (3.1415926 / 180.0)
    cot = math.cos(fovy_rad / 2.0) / math.sin(fovy_rad / 2.0)
    mf[0][0] = cot / aspect
    mf[1][1] = cot
    mf[2][2] = (near + far) / (near - far)
    mf[2][3] = -1.0
    mf[3][2] = (2.0 * near * far) / (near - far)
    mf[3][3] = 0.0
    for i in range(4):
        for j in range(4):
            mf[i][j] *= scale
    return mf


def gu_lookat(eye, at, up):
    """libultra guLookAtF -> mf[4][4] (row-major)."""
    mf = mat_identity()
    xe, ye, ze = eye
    xa, ya, za = at
    xu, yu, zu = up

    xl, yl, zl = xa - xe, ya - ye, za - ze
    length = -1.0 / math.sqrt(xl * xl + yl * yl + zl * zl)
    xl *= length; yl *= length; zl *= length

    xr = yu * zl - zu * yl
    yr = zu * xl - xu * zl
    zr = xu * yl - yu * xl
    length = 1.0 / math.sqrt(xr * xr + yr * yr + zr * zr)
    xr *= length; yr *= length; zr *= length

    xu = yl * zr - zl * yr
    yu = zl * xr - xl * zr
    zu = xl * yr - yl * xr
    length = 1.0 / math.sqrt(xu * xu + yu * yu + zu * zu)
    xu *= length; yu *= length; zu *= length

    mf[0][0] = xr; mf[1][0] = yr; mf[2][0] = zr
    mf[3][0] = -(xe * xr + ye * yr + ze * zr)
    mf[0][1] = xu; mf[1][1] = yu; mf[2][1] = zu
    mf[3][1] = -(xe * xu + ye * yu + ze * zu)
    mf[0][2] = xl; mf[1][2] = yl; mf[2][2] = zl
    mf[3][2] = -(xe * xl + ye * yl + ze * zl)
    mf[0][3] = 0.0; mf[1][3] = 0.0; mf[2][3] = 0.0; mf[3][3] = 1.0
    return mf


def gu_normalize(x, y, z):
    length = math.sqrt(x * x + y * y + z * z)
    if length == 0.0:
        return x, y, z
    return x / length, y / length, z / length


def gu_rotate(a, x, y, z):
    """libultra guRotateF -> mf[4][4] (row-major)."""
    dtor = 3.1415926 / 180.0
    x, y, z = gu_normalize(x, y, z)
    a *= dtor
    sine = math.sin(a)
    cosine = math.cos(a)
    t = 1.0 - cosine
    ab = x * y * t
    bc = y * z * t
    ca = z * x * t
    mf = mat_identity()
    xxsine = x * sine
    yxsine = y * sine
    zxsine = z * sine

    t = x * x
    mf[0][0] = t + cosine * (1 - t)
    mf[2][1] = bc - xxsine
    mf[1][2] = bc + xxsine

    t = y * y
    mf[1][1] = t + cosine * (1 - t)
    mf[2][0] = ca + yxsine
    mf[0][2] = ca - yxsine

    t = z * z
    mf[2][2] = t + cosine * (1 - t)
    mf[1][0] = ab - zxsine
    mf[0][1] = ab + zxsine
    return mf


def gu_translate(tx, ty, tz):
    """libultra guTranslateF -> mf[4][4] (row-major, row-vector translation)."""
    mf = mat_identity()
    mf[3][0] = tx
    mf[3][1] = ty
    mf[3][2] = tz
    return mf


def to_q16(v):
    """float -> Q16.16 int32 with round-half-away-from-zero."""
    if v >= 0:
        q = int(math.floor(v * DEMO_FIX_ONE + 0.5))
    else:
        q = int(math.ceil(v * DEMO_FIX_ONE - 0.5))
    # clamp to int32 range (matrices here never approach the limit)
    assert -2147483648 <= q <= 2147483647, "Q16.16 overflow: %f" % v
    return q


def mat_to_q16(mf):
    """Row-major 4x4 float -> flat list of 16 Q16.16 int32, row-major."""
    out = []
    for i in range(4):
        for j in range(4):
            out.append(to_q16(mf[i][j]))
    return out


def build_matrices():
    proj = gu_perspective(FOVY, ASPECT, NEAR, FAR, PERSP_SCALE)
    view = gu_lookat(EYE, AT, UP)

    # Per-frame MODEL = Rotate * Translate  (gldemo guMtxCatL(rot, trans, ...)),
    # row-vector convention v' = v . MODEL (rotate then translate).
    pyr_models = []
    for f in range(PYRAMID_PERIOD):
        rot = gu_rotate(PYRAMID_DEG_PER_FRAME * f, 0.0, 1.0, 0.0)
        trans = gu_translate(*PYRAMID_TRANSLATE)
        pyr_models.append(mat_mul(rot, trans))

    cube_models = []
    for f in range(CUBE_PERIOD):
        rot = gu_rotate(CUBE_DEG_PER_FRAME * f, *CUBE_AXIS)
        trans = gu_translate(*CUBE_TRANSLATE)
        cube_models.append(mat_mul(rot, trans))

    return proj, view, pyr_models, cube_models


# ===========================================================================
# Geometry (gldemo geometry.c)
# ===========================================================================

# Pyramid: 12 verts (4 tri faces). pos (x,y,z) short, rgba8.
PYRAMID_VERTS = [
    # Front
    (0, 50, 0, 0xff, 0x00, 0x00, 0xff),
    (-50, -50, 50, 0x00, 0xff, 0x00, 0xff),
    (50, -50, 50, 0x00, 0x00, 0xff, 0xff),
    # Right
    (0, 50, 0, 0xff, 0x00, 0x00, 0xff),
    (50, -50, 50, 0x00, 0x00, 0xff, 0xff),
    (50, -50, -50, 0x00, 0xff, 0x00, 0xff),
    # Back
    (0, 50, 0, 0xff, 0x00, 0x00, 0xff),
    (50, -50, -50, 0x00, 0xff, 0x00, 0xff),
    (-50, -50, -50, 0x00, 0x00, 0xff, 0xff),
    # Left
    (0, 50, 0, 0xff, 0x00, 0x00, 0xff),
    (-50, -50, -50, 0x00, 0x00, 0xff, 0xff),
    (-50, -50, 50, 0x00, 0xff, 0x00, 0xff),
]
PYRAMID_TRIS = [(0, 1, 2), (3, 4, 5), (6, 7, 8), (9, 10, 11)]

# Cube: 24 verts (4 per face). pos short, rgba8.
CUBE_VERTS = [
    # Top - green
    (50, 50, -50, 0x00, 0xff, 0x00, 0xff),
    (-50, 50, -50, 0x00, 0xff, 0x00, 0xff),
    (-50, 50, 50, 0x00, 0xff, 0x00, 0xff),
    (50, 50, 50, 0x00, 0xff, 0x00, 0xff),
    # Bottom - orange
    (50, -50, 50, 0xff, 0x80, 0x00, 0xff),
    (-50, -50, 50, 0xff, 0x80, 0x00, 0xff),
    (-50, -50, -50, 0xff, 0x80, 0x00, 0xff),
    (50, -50, -50, 0xff, 0x80, 0x00, 0xff),
    # Front - red
    (50, 50, 50, 0xff, 0x00, 0x00, 0xff),
    (-50, 50, 50, 0xff, 0x00, 0x00, 0xff),
    (-50, -50, 50, 0xff, 0x00, 0x00, 0xff),
    (50, -50, 50, 0xff, 0x00, 0x00, 0xff),
    # Back - yellow
    (50, -50, -50, 0xff, 0xff, 0x00, 0xff),
    (-50, -50, -50, 0xff, 0xff, 0x00, 0xff),
    (-50, 50, -50, 0xff, 0xff, 0x00, 0xff),
    (50, 50, -50, 0xff, 0xff, 0x00, 0xff),
    # Left - blue
    (-50, 50, 50, 0x00, 0x00, 0xff, 0xff),
    (-50, 50, -50, 0x00, 0x00, 0xff, 0xff),
    (-50, -50, -50, 0x00, 0x00, 0xff, 0xff),
    (-50, -50, 50, 0x00, 0x00, 0xff, 0xff),
    # Right - violet
    (50, 50, -50, 0xff, 0x00, 0xff, 0xff),
    (50, 50, 50, 0xff, 0x00, 0xff, 0xff),
    (50, -50, 50, 0xff, 0x00, 0xff, 0xff),
    (50, -50, -50, 0xff, 0x00, 0xff, 0xff),
]
# Two tris per face, matching render.c gSP2Triangles winding.
CUBE_TRIS = [
    (0, 1, 2), (0, 2, 3),        # Top
    (4, 5, 6), (4, 6, 7),        # Bottom
    (8, 9, 10), (8, 10, 11),     # Front
    (12, 13, 14), (12, 14, 15),  # Back
    (16, 17, 18), (16, 18, 19),  # Left
    (20, 21, 22), (20, 22, 23),  # Right
]

# Per-face (u,v) texel coords mapping the 64x64 arrows texture across each cube
# face. CUBE_VERTS orders every face's 4 corners consistently as
#   corner 0 = (+a,+b)  corner 1 = (-a,+b)  corner 2 = (-a,-b)  corner 3 = (+a,-b)
# (a,b = the face's two in-plane axes), so one FACE_UV orients the texture
# identically on all six faces. With the Y-flipped (N64 RSP) viewport and our
# winding, U must run high->low across corners 0->1 to render the arrows
# unmirrored (matching gldemo's textured cube); the earlier (0,0)-at-corner-0
# layout mirrored the texture horizontally. V is already upright.
#   corner 0 -> (64,0)  corner 1 -> (0,0)
#   corner 2 -> (0,64)  corner 3 -> (64,64)
TEX_DIM = 64
FACE_UV = [(TEX_DIM, 0), (0, 0), (0, TEX_DIM), (TEX_DIM, TEX_DIM)]


# ===========================================================================
# Emit
# ===========================================================================

GEN_BANNER = (
    "/* GENERATED by tools/gen_n64_assets.py -- DO NOT EDIT BY HAND.\n"
    " *\n"
    " * This file is generated DATA (unencumbered, like compiler output);\n"
    " * the generator is a separate GPLv3 work (tools/gen_n64_assets.py).\n"
    " */\n"
)


def emit_texture(w, h, packed, tlut, ncolors):
    path = os.path.join(OUT_DIR, "arrows_ci4.h")
    lines = []
    lines.append(GEN_BANNER)
    lines.append("// clang-format off")
    lines.append("#ifndef DEMO_GENERATED_ARROWS_CI4_H")
    lines.append("#define DEMO_GENERATED_ARROWS_CI4_H")
    lines.append("")
    lines.append("#include <stdint.h>")
    lines.append("")
    lines.append("/* arrows.png -> 64x64 CI4 (4-bit palette indices, nibble-packed).")
    lines.append(" * %d unique RGBA5551 colors (<= 16, lossless CI4)." % ncolors)
    lines.append(" * Index k (pixel) high nibble = even pixel, low nibble = odd pixel,")
    lines.append(" * row 0 = top of image (N64 -Y / RDP T-down order). */")
    lines.append("#define ARROWS_CI4_WIDTH  %d" % w)
    lines.append("#define ARROWS_CI4_HEIGHT %d" % h)
    lines.append("#define ARROWS_CI4_BYTES  %d" % len(packed))
    lines.append("#define ARROWS_TLUT_COLORS %d  /* used palette entries */" % ncolors)
    lines.append("")
    lines.append("static const uint8_t arrows_ci4[%d] = {" % len(packed))
    for off in range(0, len(packed), 12):
        chunk = packed[off:off + 12]
        lines.append("    " + ", ".join("0x%02x" % b for b in chunk) + ",")
    lines.append("};")
    lines.append("")
    lines.append("/* 16-entry big-endian RGBA5551 TLUT (R5 G5 B5 A1); unused slots = 0. */")
    lines.append("static const uint16_t arrows_tlut[16] = {")
    lines.append("    " + ", ".join("0x%04x" % v for v in tlut))
    lines.append("};")
    lines.append("")
    lines.append("#endif /* DEMO_GENERATED_ARROWS_CI4_H */")
    lines.append("// clang-format on")
    write_file(path, "\n".join(lines) + "\n")


def emit_mat(lines, name, q):
    lines.append("static const int32_t %s[16] = {" % name)
    for r in range(4):
        row = q[r * 4:r * 4 + 4]
        lines.append("    " + ", ".join("%11d" % v for v in row) + ",")
    lines.append("};")


def emit_matrices(proj, view, pyr_models, cube_models):
    path = os.path.join(OUT_DIR, "baked_matrices.h")
    lines = []
    lines.append(GEN_BANNER)
    lines.append("// clang-format off")
    lines.append("#ifndef DEMO_GENERATED_BAKED_MATRICES_H")
    lines.append("#define DEMO_GENERATED_BAKED_MATRICES_H")
    lines.append("")
    lines.append("#include <stdint.h>")
    lines.append("")
    lines.append("/* All matrices: 16x int32 Q16.16 (16 fractional bits), ROW-MAJOR,")
    lines.append(" * libultra row-vector convention  v' = v . M  (translation in row 3).")
    lines.append(" * Vertex clip-space = v . MODEL . VIEW . PROJ.")
    lines.append(" *")
    lines.append(" * Sources (gldemo render.c):")
    lines.append(" *   PROJ = guPerspective(fovy=45, aspect=1.0 [240x240 square],")
    lines.append(" *                        near=10, far=1000, scale=1.0)")
    lines.append(" *   VIEW = guLookAt(eye=(0,0,300), at=(0,0,0), up=(0,1,0))")
    lines.append(" *   pyramid MODEL[f] = guRotate(12*f deg, +Y) * guTranslate(-75,0,0)")
    lines.append(" *   cube    MODEL[f] = guRotate(-9*f deg, (1,1,1)) * guTranslate(75,0,-50)")
    lines.append(" * rotation rates: gldemo PYRAMID/CUBE_DEG_PER_FRAME numerators (12, 9);")
    lines.append(" * periods 30 / 40 frames -> full 360 loop; lcm = 120 (DEMO_ANIM_PERIOD).")
    lines.append(" */")
    lines.append("#define DEMO_FIX_FRAC %d" % DEMO_FIX_FRAC)
    # DEMO_FIX_ONE also lives in demo/fixed.h (identical value); guard it so a
    # TU including both this header and fixed.h doesn't trip -Wmacro-redefined.
    lines.append("#ifndef DEMO_FIX_ONE")
    lines.append("#define DEMO_FIX_ONE  (1 << DEMO_FIX_FRAC)  /* %d */" % DEMO_FIX_ONE)
    lines.append("#endif")
    lines.append("#define DEMO_PYRAMID_PERIOD %d" % PYRAMID_PERIOD)
    lines.append("#define DEMO_CUBE_PERIOD    %d" % CUBE_PERIOD)
    lines.append("#define DEMO_ANIM_PERIOD    %d  /* lcm(%d,%d) */"
                 % (ANIM_PERIOD, PYRAMID_PERIOD, CUBE_PERIOD))
    lines.append("")
    emit_mat(lines, "demo_proj_matrix", mat_to_q16(proj))
    lines.append("")
    emit_mat(lines, "demo_view_matrix", mat_to_q16(view))
    lines.append("")
    lines.append("/* Per-frame pyramid MODEL matrices, indexed [frame %% DEMO_PYRAMID_PERIOD]. */")
    lines.append("static const int32_t demo_pyramid_model[DEMO_PYRAMID_PERIOD][16] = {")
    for f in range(PYRAMID_PERIOD):
        q = mat_to_q16(pyr_models[f])
        lines.append("    { /* frame %d */" % f)
        for r in range(4):
            row = q[r * 4:r * 4 + 4]
            lines.append("        " + ", ".join("%11d" % v for v in row) + ",")
        lines.append("    },")
    lines.append("};")
    lines.append("")
    lines.append("/* Per-frame cube MODEL matrices, indexed [frame %% DEMO_CUBE_PERIOD]. */")
    lines.append("static const int32_t demo_cube_model[DEMO_CUBE_PERIOD][16] = {")
    for f in range(CUBE_PERIOD):
        q = mat_to_q16(cube_models[f])
        lines.append("    { /* frame %d */" % f)
        for r in range(4):
            row = q[r * 4:r * 4 + 4]
            lines.append("        " + ", ".join("%11d" % v for v in row) + ",")
        lines.append("    },")
    lines.append("};")
    lines.append("")
    lines.append("#endif /* DEMO_GENERATED_BAKED_MATRICES_H */")
    lines.append("// clang-format on")
    write_file(path, "\n".join(lines) + "\n")


def emit_geometry():
    path = os.path.join(OUT_DIR, "geometry.h")
    lines = []
    lines.append(GEN_BANNER)
    lines.append("// clang-format off")
    lines.append("#ifndef DEMO_GENERATED_GEOMETRY_H")
    lines.append("#define DEMO_GENERATED_GEOMETRY_H")
    lines.append("")
    lines.append("#include <stdint.h>")
    lines.append("")
    lines.append("/* gldemo geometry.c: positions int16 model-space (half-extent 50),")
    lines.append(" * colors RGBA8. Winding matches render.c (G_CULL_BACK, N64 Y-down).")
    lines.append(" * Cube faces are textured with the 64x64 arrows image; per-vertex")
    lines.append(" * (u,v) are texel coords [0,64] (top-left origin, T-down). */")
    lines.append("")
    lines.append("struct DemoVtx {")
    lines.append("    int16_t x, y, z;       /* model-space position */")
    lines.append("    uint8_t r, g, b, a;    /* RGBA8 vertex color */")
    lines.append("};")
    lines.append("")
    lines.append("struct DemoVtxT {")
    lines.append("    int16_t x, y, z;       /* model-space position */")
    lines.append("    uint8_t r, g, b, a;    /* RGBA8 vertex color */")
    lines.append("    int16_t u, v;          /* texel coords [0,64] */")
    lines.append("};")
    lines.append("")
    lines.append("struct DemoTri { uint8_t a, b, c; };")
    lines.append("")
    # Pyramid
    lines.append("#define DEMO_PYRAMID_VTX_COUNT %d" % len(PYRAMID_VERTS))
    lines.append("#define DEMO_PYRAMID_TRI_COUNT %d" % len(PYRAMID_TRIS))
    lines.append("static const struct DemoVtx demo_pyramid_vtx[DEMO_PYRAMID_VTX_COUNT] = {")
    for (x, y, z, r, g, b, a) in PYRAMID_VERTS:
        lines.append("    { %4d, %4d, %4d, 0x%02x, 0x%02x, 0x%02x, 0x%02x },"
                     % (x, y, z, r, g, b, a))
    lines.append("};")
    lines.append("static const struct DemoTri demo_pyramid_tri[DEMO_PYRAMID_TRI_COUNT] = {")
    for (a, b, c) in PYRAMID_TRIS:
        lines.append("    { %d, %d, %d }," % (a, b, c))
    lines.append("};")
    lines.append("")
    # Cube (textured)
    lines.append("#define DEMO_CUBE_VTX_COUNT %d" % len(CUBE_VERTS))
    lines.append("#define DEMO_CUBE_TRI_COUNT %d" % len(CUBE_TRIS))
    lines.append("static const struct DemoVtxT demo_cube_vtx[DEMO_CUBE_VTX_COUNT] = {")
    for k, (x, y, z, r, g, b, a) in enumerate(CUBE_VERTS):
        u, v = FACE_UV[k % 4]
        lines.append("    { %4d, %4d, %4d, 0x%02x, 0x%02x, 0x%02x, 0x%02x, %3d, %3d },"
                     % (x, y, z, r, g, b, a, u, v))
    lines.append("};")
    lines.append("static const struct DemoTri demo_cube_tri[DEMO_CUBE_TRI_COUNT] = {")
    for (a, b, c) in CUBE_TRIS:
        lines.append("    { %d, %d, %d }," % (a, b, c))
    lines.append("};")
    lines.append("")
    lines.append("#endif /* DEMO_GENERATED_GEOMETRY_H */")
    lines.append("// clang-format on")
    write_file(path, "\n".join(lines) + "\n")


def write_file(path, text):
    with open(path, "w", newline="\n") as f:
        f.write(text)
    sys.stderr.write("wrote %s\n" % path)


def main():
    if not os.path.isdir(OUT_DIR):
        os.makedirs(OUT_DIR)
    w, h, packed, tlut, ncolors = build_texture()
    emit_texture(w, h, packed, tlut, ncolors)
    proj, view, pyr_models, cube_models = build_matrices()
    emit_matrices(proj, view, pyr_models, cube_models)
    emit_geometry()
    sys.stderr.write("palette: %d colors; CI4 %dx%d -> %d bytes\n"
                     % (ncolors, w, h, len(packed)))


if __name__ == "__main__":
    main()
