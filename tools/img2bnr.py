#!/usr/bin/env python3
"""img2bnr.py - convert an image into a GordonOS .bnr banner file.

.bnr format: byte 0 = columns c, byte 1 = rows r, then c*r glyph
definitions of 8 bytes each (one byte per pixel row, bit 7 = leftmost
pixel, 1 = ink, 0 = background). The shell's `banner <file> <row> <col>`
command draws these via kPrintGlyph. Every .bnr in src/banners/ is seeded
into the REU filesystem by tools/build-reu.ps1 (base name <= 7 chars).

Usage:
    python tools/img2bnr.py <image> <output.bnr> [options]

Options:
    --threshold N   brightness cutoff for ink, 0-255 (default 100)
    --ink MODE      "dark"   = pixels darker than the threshold are ink
                    "bright" = pixels brighter than the threshold are ink
                    (default "dark" - matches the bundled banners)
    --cells C       banner width in 8-pixel cells, 1-40 (default 31,
                    i.e. 248 px - the largest multiple of 8 below 250)
    --preview PNG   also render a 4x scaled PNG preview here

The image is converted to grayscale, resized to --cells*8 pixels wide
(aspect ratio preserved, height rounded to a multiple of 8), thresholded,
and packed into 8-byte glyphs.

Requires Pillow:
    python -m pip install Pillow

Examples:
    python tools/img2bnr.py logo.png src/banners/logo.bnr
    python tools/img2bnr.py logo.png src/banners/logo150.bnr --threshold 150
    python tools/img2bnr.py logo.png src/banners/neg.bnr --ink bright --preview neg.png
"""

import argparse
import sys

from PIL import Image


def main() -> int:
    ap = argparse.ArgumentParser(
        description="Convert an image to a GordonOS .bnr banner")
    ap.add_argument("image", help="source image (PNG/JPG/...)")
    ap.add_argument("output", help="destination .bnr file")
    ap.add_argument("--threshold", type=int, default=100,
                    help="brightness cutoff for ink, 0-255 (default 100)")
    ap.add_argument("--ink", choices=("dark", "bright"), default="dark",
                    help="dark = darker pixels are ink; bright = brighter "
                         "pixels are ink (default dark)")
    ap.add_argument("--cells", type=int, default=31,
                    help="banner width in 8-pixel cells, 1-40 (default 31)")
    ap.add_argument("--preview", metavar="PNG",
                    help="also render a 4x scaled PNG preview here")
    args = ap.parse_args()

    if not 0 <= args.threshold <= 255:
        ap.error("--threshold must be 0-255")
    if not 1 <= args.cells <= 40:
        ap.error("--cells must be 1-40")

    img = Image.open(args.image).convert("L")
    w, h = img.size
    new_w = args.cells * 8
    new_h = max(8, round(h * new_w / w / 8) * 8)
    img = img.resize((new_w, new_h), Image.LANCZOS)
    rows = new_h // 8
    print(f"{args.image}: {w}x{h} -> {new_w}x{new_h} -> "
          f"{args.cells}x{rows} grid")

    if args.ink == "dark":
        is_ink = lambda v: v <= args.threshold
    else:
        is_ink = lambda v: v > args.threshold

    glyphs = bytearray()
    for ry in range(rows):
        for cx in range(args.cells):
            for py in range(8):
                byte = 0
                for px in range(8):
                    if is_ink(img.getpixel((cx * 8 + px, ry * 8 + py))):
                        byte |= 1 << (7 - px)
                glyphs.append(byte)

    with open(args.output, "wb") as f:
        f.write(bytes((args.cells, rows)) + glyphs)
    print(f"wrote {args.output}: {2 + len(glyphs)} bytes")

    if args.preview:
        pimg = Image.new("1", (new_w, new_h), 0)
        ppx = pimg.load()
        for i, byte in enumerate(glyphs):
            cy, cx = divmod(i // 8, args.cells)
            py = i % 8
            for px in range(8):
                if byte & (1 << (7 - px)):
                    ppx[cx * 8 + px, cy * 8 + py] = 1
        pimg = pimg.resize((new_w * 4, new_h * 4), Image.NEAREST)
        pimg.save(args.preview)
        print(f"wrote preview {args.preview}: {pimg.size[0]}x{pimg.size[1]}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
