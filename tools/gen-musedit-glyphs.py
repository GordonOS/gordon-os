#!/usr/bin/env python3
"""gen-musedit-glyphs.py -- the musedit piano: glyphs and hit tables from one geometry.

The keyboard is eight octaves over the 40-cell width. One octave is 40 pixels =
5 cells, so the octave boundary always falls on a cell edge and the whole pattern
repeats every five cells. A key is therefore 40/7 = 5.7 px wide -- a key does not
have to be a whole cell, because a glyph is 8x8 pixels.

Output: src/tasks/dynamic/musedit-glyphs.inc

    muPianoGlyphs   ROWS x CELLS bytes (index = row * 5 + cell): one byte per cell
    muKeyHi         40 bytes: semitone 0-11, bit 7 set for a black key
                    (the three rows the black keys reach)
    muKeyLo         40 bytes: the bottom row, white keys only

A cell is one byte rather than the eight a glyph blit takes: every pixel row of a
cell carries the same horizontal pattern (keys are vertical bars), so the task
expands it when it draws. That is 15 bytes of data instead of 120.

The geometry below is the single source: a glyph and the hit table it is tested
against are both derived from it, so they cannot disagree.

Usage:
    python tools/gen-musedit-glyphs.py            write the .inc
    python tools/gen-musedit-glyphs.py -Check      exit 1 if it is not current
"""

import os
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT = os.path.join(ROOT, "src", "tasks", "dynamic", "musedit-glyphs.inc")

OCT_W = 40                      # one octave = 5 cells = 40 px, exactly
CELLS = 5                       # cells per octave
ROWS = 3                        # keyboard height, in cell rows

# White-key edges inside one octave, in pixels. Each key runs to the next edge;
# the edge pixel itself is the separator (paper), so keys read as separate.
WHITE_EDGES = (0, 6, 11, 17, 23, 29, 34)

# (first px, last px + 1, semitone) -- the seven white keys that tile 0..40.
WHITE = (
    (0, 6, 0), (6, 11, 2), (11, 17, 4), (17, 23, 5),
    (23, 29, 7), (29, 34, 9), (34, 40, 11),
)

# The five black keys, 4 px wide, straddling the white-key boundaries. Neighbours
# inside a group keep a 2 px gap: C#-D# and F#-G#-A# are each evenly spaced, which
# puts D# and A# one pixel right of their own boundary and C#, F# and G# two pixels
# either side of theirs (32..35 and 9..12 would leave a 1 px gap beside G# and C#).
BLACK = ((4, 8, 1), (10, 14, 3), (21, 25, 6), (27, 31, 8), (33, 37, 10))

BLACK_ROWS = 2                  # the black keys reach the top two cell rows


def ink(p, row):
    """1 = ink (white key body), 0 = paper (separator or black key)."""
    if row < BLACK_ROWS:
        for lo, hi, _ in BLACK:
            if lo <= p < hi:
                return 0
    return 0 if p in WHITE_EDGES else 1


def glyph(row, cell):
    """One 8x8 glyph: bit 7 is the leftmost pixel, and every pixel row of the
    cell carries the same horizontal pattern -- keys are vertical bars."""
    b = 0
    for i in range(8):
        b |= ink(cell * 8 + i, row) << (7 - i)
    return [b] * 8


def semitone(p, top):
    """The key at pixel p of an octave: bit 7 set when it is a black key."""
    if top:
        for lo, hi, s in BLACK:
            if lo <= p < hi:
                return s | 0x80
    for lo, hi, s in WHITE:
        if lo <= p < hi:
            return s
    raise ValueError("pixel %d is outside the octave" % p)


def build():
    # The geometry must actually tile the octave, or a glyph and a hit test
    # would both be wrong in the same silent way.
    assert WHITE[0][0] == 0 and WHITE[-1][1] == OCT_W, "white keys do not tile"
    for (lo, hi, _), nxt in zip(WHITE, WHITE[1:]):
        assert hi == nxt[0], "white key gap at %d" % hi
    for lo, hi, _ in BLACK:
        assert 0 <= lo < hi <= OCT_W, "black key outside the octave"
    # Every gap inside a group, and only those, is 2 px: C#-D# and F#-G#-A#.
    for left, right in ((BLACK[0], BLACK[1]), (BLACK[2], BLACK[3]), (BLACK[3], BLACK[4])):
        assert right[0] - left[1] == 2, "black key gap is not 2 px at %d" % left[1]
    assert all(semitone(p, False) is not None for p in range(OCT_W))

    out = []
    out.append("// ==========================================")
    out.append("//  musedit-glyphs.inc -- GENERATED, do not edit by hand.")
    out.append("//  Source: tools/gen-musedit-glyphs.py   (run it, or -Check in the build)")
    out.append("// ==========================================")
    out.append("//  Eight octaves over 40 cells: one octave is 40 px = 5 cells, so the")
    out.append("//  pattern repeats every five cells and the octave boundary is a cell edge.")
    out.append("//  White-key edges: %s" % ", ".join(str(e) for e in WHITE_EDGES))
    out.append("//  Black keys: %s" % ", ".join("%d-%d" % (lo, hi - 1) for lo, hi, _ in BLACK))
    out.append("")
    out.append("// %d cells x 1 byte; index = row * %d + cell. A cell is ONE byte: every" % (ROWS * CELLS, CELLS))
    out.append("// pixel row of it is the same horizontal pattern, because keys are vertical")
    out.append("// bars, so the task expands it to the 8 bytes a glyph blit wants.")
    out.append("muPianoCell:")
    for row in range(ROWS):
        for cell in range(CELLS):
            out.append("    .byte $%02x   // row %d cell %d" % (glyph(row, cell)[0], row, cell))
    out.append("")
    out.append("// %d bytes per row of %d cells: the top three rows (black keys)" % (OCT_W, CELLS))
    out.append("muKeyHi:")
    hi = [semitone(p, True) for p in range(OCT_W)]
    for i in range(0, OCT_W, 8):
        out.append("    .byte %s" % ",".join("$%02x" % b for b in hi[i:i + 8]))
    out.append("")
    out.append("// ...and the bottom row (white keys only)")
    out.append("muKeyLo:")
    lo = [semitone(p, False) for p in range(OCT_W)]
    for i in range(0, OCT_W, 8):
        out.append("    .byte %s" % ",".join("$%02x" % b for b in lo[i:i + 8]))
    out.append("")
    out.append("// How many keyboard rows the black keys cover; the rest is white.")
    out.append("muKeyBlackRows:")
    out.append("    .byte %d" % BLACK_ROWS)
    out.append("")
    return "\n".join(out)


def main():
    text = build()
    check = "-Check" in sys.argv
    if check:
        try:
            with open(OUT, encoding="utf-8", newline="\n") as fh:
                have = fh.read()
        except FileNotFoundError:
            print("gen-musedit-glyphs: %s is missing" % OUT)
            return 1
        if have != text:
            print("gen-musedit-glyphs: %s is stale -- run the generator" % OUT)
            return 1
        print("gen-musedit-glyphs: OK (%d cells, 2 x %d hit entries)"
              % (ROWS * CELLS, OCT_W))
        return 0
    with open(OUT, "w", encoding="utf-8", newline="\n") as fh:
        fh.write(text)
    print("gen-musedit-glyphs: wrote %s (%d cells, 2 x %d hit entries)"
          % (OUT, ROWS * CELLS, OCT_W))
    return 0


if __name__ == "__main__":
    sys.exit(main())
