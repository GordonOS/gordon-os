#!/usr/bin/env python3
"""gen-sprites.py - ASCII art -> C64 sprite sheets.

    assets/sprites/<name>.txt  ->  assets/sprites/<name>.spr

Frames are separated by one or more
blank lines and are emitted in order; the sheet is the frames concatenated, so
frame N starts at byte offset N*64 (linear sheets).

Two frame kinds, told apart by the characters used:

  hires   24 columns x 21 rows, one character per pixel
          '.' = transparent, '#' = sprite colour ($D027+n)
  mc      12 columns x 21 rows, one character per 2-bit pixel
          '0' = transparent, '1' = sprite colour, '2' = $D025, '3' = $D026

Both produce exactly 63 bytes of pixels + 1 pad byte = 64. The VIC interprets
the same block as 24 one-bit pixels or 12 two-bit pixels purely from $D01C, so
a sheet is mode-agnostic and can be shown either way.

The `.txt` is the source of truth and is committed alongside the generated
`.spr`, matching how charsets/*.fnt and src/banners/*.bnr are committed - so a
fresh clone builds without Python.
"""

import sys
from pathlib import Path

ROWS = 21
HIRES_COLS = 24
MC_COLS = 12


def parse_frames(text: str, src: Path) -> list:
    """Split the file into frames of 21 non-blank rows."""
    frames, cur = [], []
    for lineno, raw in enumerate(text.splitlines(), 1):
        line = raw.rstrip()
        if not line.strip():
            if cur:
                frames.append(cur)
                cur = []
            continue
        if line.lstrip().startswith(";"):        # comment line
            continue
        cur.append((lineno, line.strip()))
    if cur:
        frames.append(cur)
    if not frames:
        sys.exit(f"{src}: no frames")

    for i, frame in enumerate(frames):
        if len(frame) != ROWS:
            sys.exit(f"{src}: frame {i} has {len(frame)} rows, need {ROWS}")
    return frames


def is_mc(frames) -> bool:
    return any(c in "123" for frame in frames for _, row in frame for c in row)


def encode_hires(frame, src: Path) -> bytes:
    out = bytearray()
    for lineno, row in frame:
        if len(row) != HIRES_COLS:
            sys.exit(f"{src}:{lineno}: hires row is {len(row)} wide, need {HIRES_COLS}")
        if any(c not in ".#" for c in row):
            sys.exit(f"{src}:{lineno}: only '.' and '#' in a hires frame")
        for x in range(0, HIRES_COLS, 8):
            byte = 0
            for bit in range(8):
                if row[x + bit] == "#":
                    byte |= 0x80 >> bit
            out.append(byte)
    return bytes(out)


def encode_mc(frame, src: Path) -> bytes:
    out = bytearray()
    for lineno, row in frame:
        if len(row) != MC_COLS:
            sys.exit(f"{src}:{lineno}: mc row is {len(row)} wide, need {MC_COLS}")
        if any(c not in "0123" for c in row):
            sys.exit(f"{src}:{lineno}: only 0-3 in a multicolour frame")
        for x in range(0, MC_COLS, 4):           # 4 pixels x 2 bits = one byte
            byte = 0
            for i in range(4):
                byte |= int(row[x + i]) << (6 - 2 * i)
            out.append(byte)
    return bytes(out)


def convert(src: Path, dst: Path) -> int:
    frames = parse_frames(src.read_text(encoding="utf-8"), src)
    mc = is_mc(frames)
    body = bytearray()
    for frame in frames:
        body += encode_mc(frame, src) if mc else encode_hires(frame, src)
        body.append(0)                            # the 64th byte: pad, never fetched
    dst.write_bytes(bytes(body))
    kind = "mc" if mc else "hires"
    print(f"  {src.name} -> {dst.name}: {len(frames)} frames ({kind}), {len(body)}B")
    return len(frames)


def main() -> int:
    here = Path(__file__).resolve().parent
    art = here.parent / "assets" / "sprites"
    if not art.is_dir():
        sys.exit(f"no art directory: {art}")
    txts = sorted(art.glob("*.txt"))
    if not txts:
        sys.exit(f"no .txt art in {art}")
    total = 0
    for txt in txts:
        total += convert(txt, txt.with_suffix(".spr"))
    print(f"gen-sprites: {len(txts)} sheets, {total} frames")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
