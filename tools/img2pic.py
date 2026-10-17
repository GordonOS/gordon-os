#!/usr/bin/env python3
"""img2pic.py - convert an image into a GordonOS .pic picture file.

.pic format: a fixed 10032-byte image, uncompressed and mode-tagged, so a
load is three DMAs with no decoder on the machine.

    +0     4    magic        'G','P','I','C'   ASCII (reads in a hex dump)
    +4     1    version      1
    +5     1    mode         BMPMODE_STD (2) = hires, BMPMODE_MC (3)
    +6     1    flags        bit 0 = full screen
    +7     1    pad          0 (keeps the u16 fields on even offsets)
    +8     2    cells across 40                 u16 LE
    +10    2    cells down   25                 u16 LE
    +12    4    palette      border, bg0, bg1, bg2 -- VIC register values
    +16    16   label        NUL-terminated, SCREEN-CODED, zero-padded
    +32    8000 pixels       the framebuffer, byte for byte
    +8032  1000 matrix       the video matrix
    +9032  1000 colour RAM   the MC value-3 colour / the hires cell colour

The payload order is the display's own layout, so a loader sends the three
regions straight to $2000 / $0400 / $D800 (an off-screen load to a REU slot
uses the same offsets, +0 / +$2000 / +$2400).

Colour conventions -- these match the kernel's own painter (gfx.inc's
matrix write, text-blit.inc's "matrix byte = (fg<<4) | bg0"), so a picture
this tool writes and a picture the OS draws use one encoding:

    hires   pixel bit 1 = ink, 0 = paper;  matrix = (ink << 4) | bg0
            (the VIC colours a set bit from the high nibble, a clear bit
            from the low nibble)
    MC      pixel = a 2-bit code, 0 = $D021, 1 = matrix high nibble,
            2 = matrix low nibble, 3 = colour RAM;  matrix = (c1<<4) | c2

MC pixels are double-wide, so a source is sampled at 160x200 and each
sample becomes two screen pixels -- the 8000-byte bitmap is unchanged.

bg0 is the whole picture's paper (one $D021 for the surface), so hires
carries at most two colours per cell: the paper and that cell's own ink.
--dither [AMOUNT] (MC only) error-diffuses the samples over those four
colours in raster order instead of matching each one flat. A cell only ever
holds its own four, so the trade is flat patches for a mixed tone: a gradient
or a highlight the cell has no colour for becomes a stipple of the two it has,
which is what a photograph on this machine needs. Flat art, where the cell's
colours already cover its content, comes out unchanged either way. AMOUNT
scales the error that is passed on, 1 being ordinary Floyd-Steinberg: lower
values leave the picture flatter, with dots appearing only where a tone has
strayed far enough from the nearest colour.

--bg0 N and --border N set the two colours that belong to the SURFACE rather
than to a cell: the paper ($D021) and the border. Both are otherwise read from
the picture -- the commonest colour, and the commonest along its outer frame --
which is right for a single image. Frames played in sequence share ONE surface,
so they must be given one paper for all of them: let each frame pick its own
and the paper changes under the film, which shows as the whole screen flashing
from frame to frame. The other three MC colours are per cell (the matrix's two
nibbles and colour RAM), so only the paper and the border need agreeing on.

--gamma G curves the SOURCE before the match: above 1 darkens the mid-tones,
below 1 lightens them. A picture's overall lightness has nowhere else to go --
the palette is fixed, so a source left alone is held by the bright entries
(white, light grey, light green) however hazy it looks. Curving the samples first
moves the match onto the darker entries, and a cell's own four colours, being
chosen from its samples, follow.

--gray desaturates the SOURCE and RESTRICTS THE MATCH to the palette's grey ramp
(0, 1, 11, 12, 15). The restriction is the part that matters: an unweighted RGB
distance does not stay on the ramp, because the ramp is coarse (0, 68, 108, 149,
255) and the coloured entries sit in the luminance gaps between them -- a dark
neutral matches brown (9) and a bright one light green (13), so a "greyscale"
picture made from grey samples alone still comes out with browns and greens.

A slot a cell does not need is filled with an ink from elsewhere in the
picture -- never one the cell's own samples contain, so the bitmap cannot
change. What a cell's spare slots hold still matters: a paint program shows
them as the colours it offers to draw with.
The label is screen-coded exactly as tools/build-reu.ps1 encodes FS names
(a-z -> $01-$1A, A-Z -> $41-$5A, digits and punctuation as they are), so a
task can print it without conversion. It defaults to the output's base name --
the name `dir` shows -- and is capped at 15 characters. Nothing reads it yet:
a 1-bit glyph blit is unreadable on a multicolour surface, so grview does not
print it.

Every run reads the file back: the header fields and all three regions are
compared byte for byte before the tool reports success, so a file that a
viewer would mis-read is a failure here rather than on the machine.

Requires Pillow:
    python -m pip install Pillow

Examples:
    python tools/img2pic.py assets/pics/hires-src.png assets/pics/hires.pic
    python tools/img2pic.py assets/pics/mc-src.png assets/pics/mc.pic --mode mc
    python tools/img2pic.py photo.png out.pic --mode mc --dither
    python tools/img2pic.py shot.png out.pic --label "sunset" --preview p.png
"""

import argparse
import hashlib
import os
import sys

from PIL import Image

# --- the format's constants (docs/pic-format.md) ---
MAGIC = b"GPIC"
VERSION = 1
MODE_STD = 2                      # BMPMODE_STD -- hires
MODE_MC = 3                       # BMPMODE_MC  -- multicolour
FLAG_FULLSCREEN = 0x01
CELLS_X, CELLS_Y = 40, 25
CELL_W, CELL_H = 8, 8
PIXELS_BYTES = 8000
MATRIX_BYTES = 1000
CRAM_BYTES = 1000
HEADER_BYTES = 32
FILE_BYTES = HEADER_BYTES + PIXELS_BYTES + MATRIX_BYTES + CRAM_BYTES   # 10032
LABEL_CHARS = 15                  # 16 bytes including the terminator

# The 16 VIC-II colours, Pepto's RGB values -- a host-side reference for the
# nearest-colour match. Colour 0 is the VIC's black and 15 its light grey.
PALETTE = [
    (0x00, 0x00, 0x00), (0xFF, 0xFF, 0xFF), (0x68, 0x37, 0x2B), (0x70, 0xA4, 0xB2),
    (0x6F, 0x3D, 0x86), (0x58, 0x8D, 0x43), (0x35, 0x28, 0x79), (0xB8, 0xC7, 0x6F),
    (0x6F, 0x4F, 0x25), (0x43, 0x39, 0x00), (0x9A, 0x67, 0x59), (0x44, 0x44, 0x44),
    (0x6C, 0x6C, 0x6C), (0x9A, 0xD2, 0x84), (0x6C, 0x5E, 0xB5), (0x95, 0x95, 0x95),
]

_nearest_cache = {}

#  A greyscale picture must be MATCHED against the grey ramp, not merely drawn
#  from grey samples. An unweighted RGB distance does not stay on the ramp: the
#  ramp is coarse (0, 68, 108, 149, 255) and the coloured entries sit in the
#  luminance gaps between them, so a dark neutral lands on brown (9) and a bright
#  one on light green (13) -- the greens and browns a greyscale picture must not
#  have. Restricting the candidates is what removes them.
GREY_RAMP = (0, 1, 11, 12, 15)
_candidates = tuple(range(16))


def set_match_set(indices):
    """Restrict (or restore) the palette entries the match may choose from."""
    global _candidates
    _candidates = tuple(indices)
    _nearest_cache.clear()


def d2(rgb, colour):
    """Squared RGB distance -- enough to rank candidates, no square root."""
    return ((rgb[0] - colour[0]) ** 2 + (rgb[1] - colour[1]) ** 2 +
            (rgb[2] - colour[2]) ** 2)


def nearest(rgb):
    """The palette index closest to an RGB triple (cached per distinct pixel)."""
    got = _nearest_cache.get(rgb)
    if got is None:
        got = min(_candidates, key=lambda i: d2(rgb, PALETTE[i]))
        _nearest_cache[rgb] = got
    return got


def curve_lut(gamma):
    """A 256-entry table for `gamma`: above 1 darkens, below 1 lightens."""
    return [min(255, int(round(255.0 * ((i / 255.0) ** gamma))))
            for i in range(256)]


def apply_curve(img, gamma):
    """img with `gamma` applied to every band; 1.0 returns it untouched."""
    if gamma == 1.0:
        return img
    return img.point(curve_lut(gamma) * 3)


def apply_gray(img):
    """img desaturated to pure grey: R = G = B = its luma.

    The match then has only the palette's own grey ramp to choose from (black,
    dark grey 11, grey 12, light grey 15, white 1), which is what makes a
    greyscale picture read as one: no cell can reach a colour at all.
    """
    return img.convert("L").convert("RGB")


def screen_code(text):
    """Screen-code a label the way tools/build-reu.ps1 encodes an FS name."""
    out = bytearray()
    for ch in text:
        c = ord(ch)
        if 0x61 <= c <= 0x7A:          # a-z -> $01-$1A (the unshifted set)
            out.append(c - 0x60)
        elif 0x41 <= c <= 0x5A:        # A-Z -> $41-$5A (the shifted set)
            out.append(c)
        elif 0x20 <= c <= 0x3F:        # space, digits, punctuation: same code
            out.append(c)
        else:
            out.append(0x3F)           # '?' -- a character a key cannot make
    return bytes(out)


def dominant(hist):
    """The most frequent index; ties go to the lowest index (deterministic)."""
    return max(range(16), key=lambda i: (hist[i], -i))


def edge_colour(img):
    """The dominant colour along the image's outer frame -- the border."""
    hist = [0] * 16
    for x in range(img.width):
        hist[nearest(img.getpixel((x, 0)))] += 1
        hist[nearest(img.getpixel((x, img.height - 1)))] += 1
    for y in range(img.height):
        hist[nearest(img.getpixel((0, y)))] += 1
        hist[nearest(img.getpixel((img.width - 1, y)))] += 1
    return dominant(hist)


def convert_hires(img, paper=None):
    """Render a hires picture: per-cell ink over one picture-wide paper.

    paper overrides $D021; None takes the picture's own commonest colour.
    """
    w, h = img.size
    hist = [0] * 16
    for y in range(h):
        for x in range(w):
            hist[nearest(img.getpixel((x, y)))] += 1
    bg0 = dominant(hist) if paper is None else paper

    pixels = bytearray(PIXELS_BYTES)
    matrix = bytearray(MATRIX_BYTES)
    cram = bytearray(CRAM_BYTES)
    ink_hist = [0] * 16
    over = 0

    for cy in range(CELLS_Y):
        for cx in range(CELLS_X):
            # The cell's ink is the palette entry that, beside the paper,
            # explains the cell's 64 pixels with the least total error.
            cell = [(cx * CELL_W + px, cy * CELL_H + py)
                    for py in range(CELL_H) for px in range(CELL_W)]
            best, best_err = bg0, None
            seen = set()
            for (x, y) in cell:
                rgb = img.getpixel((x, y))
                got = nearest(rgb)
                if got != bg0:
                    seen.add(got)
            if len(seen) > 1:
                over += 1          # more than one ink in the cell: quantised
            for c in range(16):
                err = 0
                for (x, y) in cell:
                    rgb = img.getpixel((x, y))
                    err += min(d2(rgb, PALETTE[c]), d2(rgb, PALETTE[bg0]))
                    if best_err is not None and err >= best_err:
                        break
                if best_err is None or err < best_err:
                    best, best_err = c, err
            ink = best
            ink_hist[ink] += 1

            base = cy * CELL_W * CELLS_X + cx * CELL_W
            for py in range(CELL_H):
                byte = 0
                for px in range(CELL_W):
                    x, y = cx * CELL_W + px, cy * CELL_H + py
                    rgb = img.getpixel((x, y))
                    if d2(rgb, PALETTE[ink]) < d2(rgb, PALETTE[bg0]):
                        byte |= 1 << (7 - px)
                pixels[base + py] = byte

            matrix[cy * CELLS_X + cx] = (ink << 4) | bg0
            cram[cy * CELLS_X + cx] = ink

    # bg1/bg2 hold the two commonest inks: the registers a mode change to MC
    # would take its colours 1 and 2 from before an edit fills the matrix.
    ranked = sorted(range(16), key=lambda i: (-ink_hist[i], i))
    return pixels, matrix, cram, [bg0, ranked[0], ranked[1]], over


def convert_mc(img, dither=0.0, paper=None):
    """Render a multicolour picture: three per-cell colours over $D021.

    dither is the share of each sample's colour error passed on to the samples
    after it: 1.0 is ordinary Floyd-Steinberg, 0.0 matches every sample flat.
    paper overrides $D021; None takes the picture's own commonest colour.
    """
    w, h = img.size
    hist = [0] * 16
    for y in range(h):
        for x in range(w):
            hist[nearest(img.getpixel((x, y)))] += 1
    bg0 = dominant(hist) if paper is None else paper

    # Pass 1: what each cell's own samples ask for, commonest first. Value 0 is
    # bg0, so a cell carries three more.
    INKS = 3
    here_all = []
    present = []
    over = 0
    for cy in range(CELLS_Y):
        for cx in range(CELLS_X):
            counts = [0] * 16
            for py in range(CELL_H):
                for px in range(4):
                    counts[nearest(img.getpixel((cx * 4 + px,
                                                 cy * CELL_H + py)))] += 1
            counts[bg0] = 0
            here = [i for i in sorted(range(16), key=lambda i: (-counts[i], i))
                    if counts[i]]
            if len(here) > INKS:
                over += 1          # more colours in the cell than it can hold
            here_all.append(here)
            present.append(here[:INKS])

    # The picture's own inks: the colours its cells ask for, commonest first.
    used = [0] * 16
    for here in present:
        for i in here[:INKS]:
            used[i] += 1
    common = [i for i in sorted(range(16), key=lambda i: (-used[i], i)) if used[i]]

    # Pass 2: the four slots and the pixels. A slot nothing uses still has to
    # hold a colour -- a paint program shows them all and offers them as pens --
    # so a spare one takes an ink from elsewhere in the picture, and never a
    # colour this cell's samples contain. That last part is what keeps a spare
    # slot from capturing a sample: every sample's own colour is in the cell's
    # list, so it always matches at distance zero.
    pixels = bytearray(PIXELS_BYTES)
    matrix = bytearray(MATRIX_BYTES)
    cram = bytearray(CRAM_BYTES)
    slots_at = []
    for cy in range(CELLS_Y):
        for cx in range(CELLS_X):
            cell = cy * CELLS_X + cx
            base = cy * CELL_W * CELLS_X + cx * CELL_W
            slots = [bg0] + present[cell]
            for c in common:
                if len(slots) == INKS + 1:
                    break
                if c not in here_all[cell] and c not in slots:
                    slots.append(c)
            slots += [bg0] * (INKS + 1 - len(slots))
            slots_at.append(tuple(slots))

            if not dither:
                for py in range(CELL_H):
                    byte = 0
                    for px in range(4):
                        rgb = img.getpixel((cx * 4 + px, cy * CELL_H + py))
                        code = min(range(4), key=lambda k: d2(rgb, PALETTE[slots[k]]))
                        byte |= code << (6 - 2 * px)     # bit 7-6 is the leftmost
                    pixels[base + py] = byte

            matrix[cell] = (slots[1] << 4) | slots[2]
            cram[cell] = slots[3]

    if dither:
        diffuse_mc(img, pixels, slots_at, dither)

    return pixels, matrix, cram, [bg0] + (common + [bg0, bg0])[:2], over


# Floyd-Steinberg's four shares, in the order the raster walk spends them.
DIFFUSE = ((1, 0, 7), (-1, 1, 3), (0, 1, 5), (1, 1, 1))


def diffuse_mc(img, pixels, slots_at, amount):
    """Write the bitmap by error diffusion over each cell's own four colours.

    The samples are walked in raster order, because the error one sample
    leaves has to reach the samples the walk has not visited yet -- so this is
    its own pass, not part of the per-cell one, whose cell-by-cell order would
    hand some of the error backwards to samples already written.

    amount is the share of that error actually passed on. Less than 1 leaves
    the picture flatter: a sample that lands between two of the cell's colours
    only starts stippling once the error it has not passed on has built up, so
    a gradient gets sparse dots instead of a full stipple.
    """
    w, h = img.size
    work = [[list(img.getpixel((x, y))) for x in range(w)] for y in range(h)]
    for y in range(h):
        for x in range(w):
            slots = slots_at[(y // CELL_H) * CELLS_X + x // 4]
            rgb = work[y][x]
            code = min(range(4), key=lambda k: d2(rgb, PALETTE[slots[k]]))
            base = (y // CELL_H) * CELL_W * CELLS_X + (x // 4) * CELL_W
            pixels[base + y % CELL_H] |= code << (6 - 2 * (x % 4))
            err = [(rgb[i] - PALETTE[slots[code]][i]) * amount for i in range(3)]
            for dx, dy, weight in DIFFUSE:
                nx, ny = x + dx, y + dy
                if 0 <= nx < w and 0 <= ny < h:
                    row = work[ny]
                    for i in range(3):
                        v = row[nx][i] + err[i] * weight / 16
                        row[nx][i] = 0 if v < 0 else (255 if v > 255 else v)


def build(label, mode, palette, regions):
    """The header followed by pixels, matrix and colour RAM."""
    pixels, matrix, cram = regions
    head = bytearray(HEADER_BYTES)
    head[0:4] = MAGIC
    head[4] = VERSION
    head[5] = mode
    head[6] = FLAG_FULLSCREEN
    head[7] = 0
    head[8:10] = CELLS_X.to_bytes(2, "little")
    head[10:12] = CELLS_Y.to_bytes(2, "little")
    head[12:16] = bytes((palette[0], palette[1], palette[2], palette[3]))
    code = screen_code(label)[:LABEL_CHARS]
    head[16:16 + len(code)] = code
    return bytes(head) + bytes(pixels) + bytes(matrix) + bytes(cram)


def verify(path, label, mode, palette, regions):
    """Read the file back and compare every header field and region."""
    with open(path, "rb") as f:
        data = f.read()
    pixels, matrix, cram = regions
    problems = []
    if len(data) != FILE_BYTES:
        problems.append(f"size {len(data)} != {FILE_BYTES}")
    if len(data) < FILE_BYTES:
        return problems, ""
    head = data[:HEADER_BYTES]
    if head[0:4] != MAGIC:
        problems.append(f"magic {head[0:4]!r} != {MAGIC!r}")
    if head[4] != VERSION:
        problems.append(f"version {head[4]} != {VERSION}")
    if head[5] != mode:
        problems.append(f"mode {head[5]} != {mode}")
    if head[6] != FLAG_FULLSCREEN or head[7] != 0:
        problems.append(f"flags/pad {head[6]:02x}/{head[7]:02x}")
    if int.from_bytes(head[8:10], "little") != CELLS_X:
        problems.append("cells across")
    if int.from_bytes(head[10:12], "little") != CELLS_Y:
        problems.append("cells down")
    if list(head[12:16]) != list(palette):
        problems.append(f"palette {list(head[12:16])} != {palette}")
    code = screen_code(label)[:LABEL_CHARS]
    want = code + b"\0" * (16 - len(code))
    if head[16:32] != want:
        problems.append(f"label {head[16:32]!r} != {want!r}")
    for name, want_region, off in (("pixels", pixels, HEADER_BYTES),
                                   ("matrix", matrix, HEADER_BYTES + PIXELS_BYTES),
                                   ("colour RAM", cram, HEADER_BYTES + PIXELS_BYTES + MATRIX_BYTES)):
        got = data[off:off + len(want_region)]
        if got != bytes(want_region):
            bad = sum(1 for a, b in zip(got, want_region) if a != b)
            problems.append(f"{name}: {bad} byte(s) differ")
    return problems, hashlib.sha256(data).hexdigest()


def preview(path, mode, palette, regions):
    """Decode the file's own bytes and render them through the palette."""
    pixels, matrix, cram = regions
    img = Image.new("RGB", (CELLS_X * CELL_W, CELLS_Y * CELL_H))
    put = img.load()
    for cy in range(CELLS_Y):
        for cx in range(CELLS_X):
            cell = cy * CELLS_X + cx
            base = cy * CELL_W * CELLS_X + cx * CELL_W
            if mode == MODE_STD:
                ink, paper = matrix[cell] >> 4, matrix[cell] & 0x0F
                for py in range(CELL_H):
                    byte = pixels[base + py]
                    for px in range(CELL_W):
                        on = byte & (1 << (7 - px))
                        put[cx * CELL_W + px, cy * CELL_H + py] = \
                            PALETTE[ink if on else paper]
            else:
                # Value 0 is $D021, not the border -- palette is (border, bg0, ...).
                slots = (palette[1], matrix[cell] >> 4, matrix[cell] & 0x0F, cram[cell])
                for py in range(CELL_H):
                    byte = pixels[base + py]
                    for px in range(4):
                        code = (byte >> (6 - 2 * px)) & 3
                        colour = PALETTE[slots[code]]
                        put[cx * CELL_W + 2 * px, cy * CELL_H + py] = colour
                        put[cx * CELL_W + 2 * px + 1, cy * CELL_H + py] = colour
    img = img.resize((img.width * 2, img.height * 2), Image.NEAREST)
    img.save(path)
    return img.size


def main() -> int:
    ap = argparse.ArgumentParser(
        description="Convert an image to a GordonOS .pic picture file")
    ap.add_argument("image", help="source image (PNG/JPG/...)")
    ap.add_argument("output", help="destination .pic file")
    ap.add_argument("--mode", choices=("hires", "mc"), default="hires",
                    help="hires = 320x200, 2 colours per cell (default); "
                         "mc = 160x200, 4 colours per cell")
    ap.add_argument("--label", help="screen-coded label, <= 15 chars "
                                    "(default: the output's base name; an "
                                    "empty string writes no label)")
    ap.add_argument("--dither", nargs="?", type=float, const=1.0, default=0.0,
                    metavar="AMOUNT",
                    help="mc only: error-diffuse the samples over each cell's own "
                         "four colours, so a tone the cell cannot hold flat becomes "
                         "a stipple of the two it can. AMOUNT (0-1, default 1) is "
                         "the share of each sample's error passed on -- lower is "
                         "flatter, with sparse dots instead of a full stipple")
    ap.add_argument("--bg0", type=int, choices=range(16), metavar="N",
                    help="paper colour ($D021) as a palette index 0-15 "
                         "(default: the picture's own commonest colour). "
                         "Frames played in sequence must share one value")
    ap.add_argument("--border", type=int, choices=range(16), metavar="N",
                    help="border colour as a palette index 0-15 (default: "
                         "the commonest colour along the outer frame)")
    ap.add_argument("--gamma", type=float, default=1.0, metavar="G",
                    help="curve the SOURCE before the match: above 1 darkens the "
                         "mid-tones, below 1 lightens them (default 1.0)")
    ap.add_argument("--gray", action="store_true",
                    help="desaturate the SOURCE to pure grey before the match, so "
                         "the picture is held by the palette's grey ramp"
                         " (black/dark grey/grey/light grey/white)")
    ap.add_argument("--preview", metavar="PNG",
                    help="also decode the file into a 2x scaled PNG here")
    args = ap.parse_args()

    label = args.label if args.label is not None else os.path.splitext(
        os.path.basename(args.output))[0]
    if len(screen_code(label)) > LABEL_CHARS:
        ap.error(f"--label is {len(screen_code(label))} characters; the header "
                 f"holds {LABEL_CHARS}")

    if args.gray:
        set_match_set(GREY_RAMP)     # hold the match to the grey ramp
    src = Image.open(args.image).convert("RGB")
    mode = MODE_STD if args.mode == "hires" else MODE_MC
    if args.dither and mode != MODE_MC:
        ap.error("--dither is multicolour only (--mode mc)")
    # MC is 160 double-wide pixels across, hires 320 single pixels.
    new_w = CELLS_X * CELL_W if mode == MODE_STD else CELLS_X * 4
    #  MC POINT-SAMPLES. A resampled blend along a hard edge is a colour the cell
    #  does not have -- a red/white edge blended to light grey -- and the cell's
    #  three-colour budget then holds that invention instead of the edge's real
    #  colour, which shows up as a stripe. Hires can filter: a blended pixel there
    #  is resolved to one of its cell's two colours.
    filt = Image.NEAREST if mode == MODE_MC else Image.LANCZOS
    img = src.resize((new_w, CELLS_Y * CELL_H), filt)
    print(f"{args.image}: {src.width}x{src.height} -> {new_w}x{img.height} "
          f"-> {args.mode}")
    #  Before the palette match and before the border is read off the frame, so
    #  both see the picture the curve produced rather than the file's samples.
    #  Desaturate FIRST, then curve: the curve then acts on the grey ramp that is
    #  actually matched rather than on the colours it came from.
    img = apply_gray(img) if args.gray else img
    img = apply_curve(img, args.gamma)
    if args.gamma != 1.0:
        print(f"curve: gamma {args.gamma} applied to the source")

    border = edge_colour(img) if args.border is None else args.border
    pixels, matrix, cram, rest, over = (convert_hires(img, args.bg0)
                                       if mode == MODE_STD else
                                       convert_mc(img, args.dither, args.bg0))
    palette = [border] + rest
    print(f"cells needing more colours than {args.mode} allows: {over} of "
          f"{CELLS_X * CELLS_Y}"
          + ("  (quantised -- keep section edges on the 8x8-cell grid)"
             if over else ""))
    data = build(label, mode, palette, (pixels, matrix, cram))
    with open(args.output, "wb") as f:
        f.write(data)
    print(f"wrote {args.output}: {len(data)} bytes, {args.mode}, "
          f"palette border={palette[0]} bg0={palette[1]} bg1={palette[2]} "
          f"bg2={palette[3]}, label \"{label}\"")

    # The format's own round trip: a viewer reads what this read back.
    problems, digest = verify(args.output, label, mode, palette,
                              (pixels, matrix, cram))
    for p in problems:
        print(f"  ROUND TRIP FAILED: {p}")
    if problems:
        return 1
    print(f"round trip: OK -- header fields and 8000/1000/1000 bytes "
          f"byte-identical, sha256 {digest}")

    if args.preview:
        size = preview(args.preview, mode, palette, (pixels, matrix, cram))
        print(f"wrote preview {args.preview}: {size[0]}x{size[1]}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
