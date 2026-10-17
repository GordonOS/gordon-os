#!/usr/bin/env python3
"""gen-anim.py - pack animation frames into REU filesystem chunks.

An animation ships as a handful of files, never one file per frame: the REU FS
caps a filename at 11 characters, and a directory entry per frame runs the 512
entries out long before the data region runs out of space. So the frames are
packed PER_CHUNK to a file, each frame still a whole 10032-byte .pic record,
with a 64-byte manifest beside them describing the film:

    <base>NN.anm     chunks: PER_CHUNK consecutive .pic records (60888 B for 6)
    <base>.inf       the manifest

The frames are converted by tools/img2pic.py's own converter, imported rather
than re-implemented, so a frame packed here and a picture that tool writes are
the same bytes.

MANIFEST (little-endian, 64 bytes)

    +0   4   magic          'G','A','N','M'
    +4   1   version
    +5   1   mode           BMPMODE_MC (3)
    +6   1   frames per chunk
    +7   1   chunk number digits (the width used in the chunk names)
    +8   2   frame count
    +10  2   fps x 256      (24 fps -> 6144)
    +12  1   border colour
    +13  1   bg0 ($D021)    the film's paper
    +14  1   bg1
    +15  1   bg2
    +16  4   frame bytes    (10032)
    +20  4   chunk bytes
    +24  12  first chunk's name, SCREEN-CODED, NUL padded
    +36  28  reserved (zero)

The name is screen-coded because that is what the filesystem stores and what
kReuOpen takes (docs/lib-filesys.md), so the task hands it over unchanged and
reaches the NEXT chunk by incrementing the digits in place -- no number
formatting on the machine.

ONE --bg0 and ONE --border are used for every frame. The surface has a single
$D021 and a single $D020, so letting each frame choose its own would change the
paper under the film and flash the whole screen between frames. The paper is
also worth choosing against the tool's own "cells needing more colours than mc
allows" count: the commonest colour in a frame is often a DITHER ARTIFACT (a
pale sky) rather than the flat region a paper wants to be.
"""

import argparse
import os
import sys

from PIL import Image

# The converter, so a packed frame and a written picture are the same bytes.
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import img2pic

MAGIC = b"GANM"
VERSION = 1
MANIFEST_BYTES = 64
NAME_FIELD = 12            # 11 characters + NUL, the FS's own limit
FS_NAME_MAX = 11
CHUNK_EXT = ".anm"
MANIFEST_EXT = ".inf"
SIZE_LIMIT = 0xFFFF        # a directory entry's size is 16-bit


def chunk_name(base, index, digits):
    """The FS name of chunk `index`, zero-padded to `digits`."""
    return "{}{:0{}d}{}".format(base, index, digits, CHUNK_EXT)


def build_manifest(args, digits, frames, chunk_bytes, first_name, bg1, bg2):
    """The 64-byte manifest a film task reads instead of hardcoding the film."""
    m = bytearray(MANIFEST_BYTES)
    m[0:4] = MAGIC
    m[4] = VERSION
    m[5] = img2pic.MODE_MC
    m[6] = args.per_chunk
    m[7] = digits
    m[8:10] = frames.to_bytes(2, "little")
    m[10:12] = round(args.fps * 256).to_bytes(2, "little")
    m[12] = args.border
    m[13] = args.bg0
    m[14] = bg1
    m[15] = bg2
    m[16:20] = img2pic.FILE_BYTES.to_bytes(4, "little")
    m[20:24] = chunk_bytes.to_bytes(4, "little")
    code = img2pic.screen_code(first_name)[:FS_NAME_MAX]
    m[24:24 + len(code)] = code
    return bytes(m)


def main() -> int:
    ap = argparse.ArgumentParser(
        description="Pack animation frames into REU FS chunks plus a manifest")
    ap.add_argument("frames", help="directory of PNG frames, in name order")
    ap.add_argument("out", help="output directory for the chunks and manifest")
    ap.add_argument("--base", default="anm",
                    help="the film's FS base name (default anm); the manifest "
                         "is <base>.inf and the chunks are <base>NN.anm")
    ap.add_argument("--fps", type=float, default=24.0,
                    help="the rate the film stores (default 24)")
    ap.add_argument("--dither", type=float, default=0.45,
                    help="img2pic error-diffusion share (default 0.45)")
    ap.add_argument("--gamma", type=float, default=1.0,
                    help="curve every frame's mid-tones before the palette match "
                         "(default 1.0): above 1 darkens the film, which is the "
                         "only lever its overall lightness has, the palette being "
                         "fixed")
    ap.add_argument("--gray", action="store_true",
                    help="desaturate every frame before the palette match, so the "
                         "film is held by the palette's grey ramp alone")
    ap.add_argument("--bg0", type=int, default=10, choices=range(16),
                    help="the paper ($D021) shared by every frame (default 10)")
    ap.add_argument("--border", type=int, default=0, choices=range(16),
                    help="the border ($D020) shared by every frame (default 0)")
    ap.add_argument("--per-chunk", type=int, default=6,
                    help="frames per chunk file (default 6; 6 is the most that "
                         "fits the 16-bit size)")
    ap.add_argument("--digits", type=int, default=0,
                    help="chunk number width (0 = as few as the count needs, "
                         "never fewer than 2)")
    ap.add_argument("--pic", type=int, default=None, metavar="N",
                    help="also write frame N out as a standalone .pic, for "
                         "`run grview` (a chunk is NOT a picture)")
    ap.add_argument("--quiet", action="store_true")
    args = ap.parse_args()

    src = sorted(f for f in os.listdir(args.frames)
                 if f.lower().endswith(".png"))
    if not src:
        print("gen-anim: no .png frames in {}".format(args.frames),
              file=sys.stderr)
        return 1

    chunks = (len(src) + args.per_chunk - 1) // args.per_chunk
    digits = args.digits or max(2, len(str(chunks - 1)))
    chunk_bytes = args.per_chunk * img2pic.FILE_BYTES
    if chunk_bytes > SIZE_LIMIT:
        print("gen-anim: {} frames per chunk is {} bytes; a directory entry's "
              "size is 16-bit, so {} is the most"
              .format(args.per_chunk, chunk_bytes, SIZE_LIMIT // img2pic.FILE_BYTES),
              file=sys.stderr)
        return 1
    widest = chunk_name(args.base, chunks - 1, digits)
    if len(widest) > FS_NAME_MAX:
        room = FS_NAME_MAX - digits - len(CHUNK_EXT)
        print("gen-anim: chunk name '{}' is {} characters; the REU FS caps a "
              "name at {}. Shorten --base to {} characters or fewer, or lower "
              "--digits."
              .format(widest, len(widest), FS_NAME_MAX, room), file=sys.stderr)
        return 1

    os.makedirs(args.out, exist_ok=True)

    #  A greyscale film must be MATCHED against the grey ramp, not merely drawn
    #  from grey samples: an unweighted RGB match leaves the ramp at both ends
    #  (dark neutrals onto brown 9, bright ones onto light green 13).
    if args.gray:
        img2pic.set_match_set(img2pic.GREY_RAMP)

    # Every frame through img2pic's converter, with the film's own paper and
    # border. The records are held in memory: 360 of them is 3.6 MB, and the
    # chunks are written straight out of them.
    records = []
    over_sum = 0
    over_max = 0
    inks = [0] * 16
    for i, name in enumerate(src):
        path = os.path.join(args.frames, name)
        src_img = Image.open(path).convert("RGB")
        if src_img.size != (img2pic.CELLS_X * 4, img2pic.CELLS_Y * img2pic.CELL_H):
            print("gen-anim: {} is {}x{}; MC samples at exactly {}x{} (a "
                  "smaller source is point-sampled, not filtered -- scale it "
                  "with ffmpeg first)"
                  .format(name, src_img.width, src_img.height,
                          img2pic.CELLS_X * 4,
                          img2pic.CELLS_Y * img2pic.CELL_H), file=sys.stderr)
            return 1
        img = src_img.resize((img2pic.CELLS_X * 4, img2pic.CELLS_Y * img2pic.CELL_H),
                             Image.NEAREST)
        if args.gray:
            img = img2pic.apply_gray(img)
        img = img2pic.apply_curve(img, args.gamma)
        pixels, matrix, cram, rest, over = img2pic.convert_mc(
            img, args.dither, args.bg0)
        palette = [args.border] + rest
        label = "{}{:04d}".format(args.base, i)
        rec = img2pic.build(label, img2pic.MODE_MC, palette,
                            (pixels, matrix, cram))
        if len(rec) != img2pic.FILE_BYTES or rec[0:4] != img2pic.MAGIC:
            print("gen-anim: frame {} did not build a {}-byte record"
                  .format(name, img2pic.FILE_BYTES), file=sys.stderr)
            return 1
        records.append(rec)
        over_sum += over
        over_max = max(over_max, over)
        inks[rest[1]] += 1
        inks[rest[2]] += 1

    # A chunk is pure concatenated .pic records: no container header, so a
    # frame can still be cut out of a chunk and opened on its own.
    for c in range(chunks):
        data = b"".join(records[c * args.per_chunk:(c + 1) * args.per_chunk])
        with open(os.path.join(args.out,
                               chunk_name(args.base, c, digits)), "wb") as f:
            f.write(data)

    ranked = sorted(range(16), key=lambda i: (-inks[i], i))
    first_name = chunk_name(args.base, 0, digits)
    manifest = build_manifest(args, digits, len(records), chunk_bytes,
                              first_name, ranked[0], ranked[1])
    with open(os.path.join(args.out, args.base + MANIFEST_EXT), "wb") as f:
        f.write(manifest)

    if args.pic is not None:
        if not 0 <= args.pic < len(records):
            print("gen-anim: --pic {} is outside 0..{}"
                  .format(args.pic, len(records) - 1), file=sys.stderr)
            return 1
        out_pic = os.path.join(args.out, "{}-{}.pic".format(args.base, args.pic))
        with open(out_pic, "wb") as f:
            f.write(records[args.pic])
        print("wrote {} (a still, not part of the film: `run grview {}`)"
              .format(out_pic, os.path.basename(out_pic)))

    if not args.quiet:
        print("{} frames -> {} chunks of up to {} ({}) + {}{}"
              .format(len(records), chunks, args.per_chunk,
                      chunk_name(args.base, 0, digits), args.base, MANIFEST_EXT))
        print("  {} bytes of frames, {} byte chunk, {} byte manifest"
              .format(len(records) * img2pic.FILE_BYTES, chunk_bytes,
                      MANIFEST_BYTES))
        print("  paper bg0={} border={} dither={} gamma={} gray={} fps={} digits={}"
              .format(args.bg0, args.border, args.dither, args.gamma, args.gray,
                      args.fps, digits))
        print("  cells needing more colours than mc allows: mean {:.1f}, "
              "max {} of {}"
              .format(over_sum / len(records), over_max,
                      img2pic.CELLS_X * img2pic.CELLS_Y))
    return 0


if __name__ == "__main__":
    sys.exit(main())
