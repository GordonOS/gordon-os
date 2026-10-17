#!/usr/bin/env python3
"""gen-ui.py - ASCII art -> grpaint's UI band image.

    assets/ui/gpui.txt  ->  assets/ui/gpui.bin

The band is the canvas's bottom two cell rows, and a hires bitmap stores CELLS, not
rows: cell (row, col) of the 40x25 grid owns eight CONTIGUOUS bytes, which are that
cell's eight raster rows. A raster row of the band is therefore forty bytes eight
apart, and a run of eight bytes is eight raster rows of one cell. The file is written
in the order the band is actually addressed:

    +0   .. +639   the band's pixels: cell row 23's forty cells, then cell row 24's
    +640 .. +719   the band's matrix: the same two rows, one nibble a cell

which is exactly what $3CC0 and $0798 hold -- so grpaint reads the file straight into
them (640 bytes to the first address, 80 to the second) and needs nothing else: no
picture, no palette and no composition on the C64 side.

The palette in front of the tools is not in the source: it is always the sixteen
colours in order, one 8x8 cell each, colours 0-7 in the band's first cell row and
8-15 in its second. Each tool picture is 16x16 -- four cells, its border included --
and the tiles follow the palette, two cells across each. The LAST TWO pictures are the
band's arrows, and they are 16x8: two cells across but ONE band cell row each, so the
pair fits the band's last two columns (all that is left after the tiles and the four
slot chips) STACKED -- undo across the top cell row, redo across the bottom one. That
is why the picker reads the band's ROW there rather than its column. The chips
themselves are drawn here, because they exist in MC only.

The .txt is the source of truth and is committed alongside the generated .bin,
matching how assets/sprites/*.spr and src/banners/*.bnr are committed - so a fresh
clone builds without Python. `tools/build-reu.ps1` re-runs this generator before it
seeds, so an edit here reaches the image on the next REU build (F5 -> "fresh REU").
"""

import sys
from pathlib import Path
ROOT = Path(__file__).resolve().parent.parent
SRC = ROOT / "assets" / "ui" / "gpui.txt"

# The band is 720 bytes in BOTH modes -- 640 pixel bytes then 80 matrix bytes -- and the
# two modes differ only in what a byte MEANS and in which pictures are used:
#   hires  one bit a pixel, 0 the cell's background, 1 its ink; sixteen palette cells
#          show their colour with $ff (all ones).
#   MC     two bits a pixel (0 bg, 1 the cell's first colour, 2 its second, 3 the colour
#          RAM), so a byte holds four 2-bit pixels; the palette cell is $55 (all ones) and
#          a tile's face is value 1 with its border value 2 (the hires art is the other
#          way round, because there a cell's colour IS its nibble).
# The MODE tile comes in two faces, the 13th picture for the hires band and the 14th for
# the MC one; both land on the 13th TILE, because the band's order is the entry order.
#
# THE BAND'S COLOURS ARE THE SAME IN BOTH MODES -- only the PACKING differs (one bit a
# pixel in hires, two in MC). The tile tuple is matrix byte, the value the paper takes and
# the value the drawing takes:
#   paper   0     hires: the 0 bits show bg0 ($D021). MC: value 0 IS $D021. Both black.
#   drawing 1     hires: the set bits show the cell's nibble. MC: value 1 shows the high
#                 nibble. The nibble is 12 ($C0 above), so the drawing is the same grey the
#                 hires band draws in -- and gpMark's in-use mark writes $10, which turns
#                 the drawing WHITE in both, which is the whole feedback for "in use".
MODES = {
    "std": dict(out="gpui.bin",    palette=0xFF, tile=(0xC0, 0, 1), chips=False),
    "mc":  dict(out="gpui_mc.bin", palette=0x55, tile=(0xC0, 0, 1), chips=True),
}
MODE = "std"
for arg in sys.argv[1:]:
    if arg.startswith("--mode="):
        MODE = arg.split("=", 1)[1]
    elif arg.startswith("--mode"):
        MODE = sys.argv[sys.argv.index(arg) + 1]
if MODE not in MODES:
    raise SystemExit("usage: gen-ui.py [--mode std|mc]")
OUT = ROOT / "assets" / "ui" / MODES[MODE]["out"]

CW = 40          # cells across the band
PH = 2           # the band is two cell rows tall
PW = 8           # palette cells across: sixteen swatches, eight a row
TW = 2           # cells a tool tile is, across and down
CR = CW * 8      # 320 bytes: one cell row of the canvas
LEN = CR * PH    # 640: the band's pixel bytes
MLEN = CW * PH   # 80:  its matrix bytes
TOOL_MAX = (CW - PW) // TW   # how many tools fit beside the palette

PIC_W = 16       # a tile is sixteen pixels across
PIC_H = 16       # ...and sixteen down: four cells
CLOSE_H = 8      # the closing icons are 16x8 -- TWO cells across, ONE band cell row
NCLOSING = 2     # ...and there are two of them, UNDO above and REDO below, stacked
NTILES = 13      # twelve tools and the MODE tile: what the task's GP_PK_TN is
CHIP_C0 = PW + NTILES * TW   # 34: the first chip's cell column (the task's GP_PK_CHIP_C0)
CHIP_FRAME = 1   # the chip border's colour: the low nibble (GP_CHIP_FRAME)
ICON_C0 = CHIP_C0 + 4        # 38: the closing icons' first cell column (GP_PK_ICON_M)


def parse_pictures(text, src):
    """Pictures of 16 columns, 16 rows (a tile) or 8 (an icon); ';' comments."""
    pics, cur = [], []
    for lineno, raw in enumerate(text.splitlines(), 1):
        line = raw.rstrip()
        if line.startswith(";"):
            continue
        if not line.strip():
            if cur:
                pics.append(cur)
                cur = []
            continue
        if len(line) != PIC_W or set(line) - set(".#"):
            raise SystemExit("%s:%d: a picture row must be %d of '.' and '#'"
                             % (src.name, lineno, PIC_W))
        cur.append(line)
    if cur:
        pics.append(cur)
    for i, p in enumerate(pics):
        if len(p) not in (PIC_H, CLOSE_H):
            raise SystemExit("%s: picture %d is %d rows -- a picture is %d rows (a tile) "
                             "or %d (an icon)"
                             % (src.name, i + 1, len(p), PIC_H, CLOSE_H))
    return pics


def cell(r, c):
    """The first of the eight bytes of the band's cell (row r, column c)."""
    return r * CR + c * 8


def place_cell(pixels, matrix, bits, cr, cc, fat, face, border, mk):
    """One cell: its eight raster rows, into the band's cell row cr, column cc."""
    dst = cell(cr, cc)
    for y in range(8):
        row = bits[y]
        if fat:
            # one BYTE holds four 2-bit pixels, so a byte covers eight source columns:
            # the 2:1 downsample is a pair of source pixels a pixel, and its value is
            # the glyph's 2 over the face's 1
            byte = 0
            for fx in range(4):
                pair = row[fx * 2: fx * 2 + 2]
                byte |= (border if "#" in pair else face) << (6 - fx * 2)
        else:
            byte = 0
            for x, ch in enumerate(row):
                if ch == "#":
                    byte |= 0x80 >> x
        pixels[dst + y] = byte
    matrix[cr * CW + cc] = mk


def place_tile(pixels, matrix, pic, c0, fat, face, border, mk):
    """A 16x16 picture: four cells, rows 0-1 by columns c0 and c0+1."""
    for qr in range(2):
        for qc in range(2):
            bits = [r[qc * 8:qc * 8 + 8] for r in pic[qr * 8:qr * 8 + 8]]
            place_cell(pixels, matrix, bits, qr, c0 + qc, fat, face, border, mk)


def place_icon(pixels, matrix, pic, c0, row, fat, face, border, mk):
    """A 16x8 picture: TWO cells of ONE band cell row, at columns c0 and c0+1."""
    for qc in range(2):
        bits = [r[qc * 8:qc * 8 + 8] for r in pic]
        place_cell(pixels, matrix, bits, row, c0 + qc, fat, face, border, mk)


def build(pics, closing):
    pixels = bytearray(LEN)
    matrix = bytearray(MLEN)
    pal, mk, face, border = MODES[MODE]["palette"], MODES[MODE]["tile"][0], MODES[MODE]["tile"][1], MODES[MODE]["tile"][2]
    fat = MODE == "mc"

    # the palette: sixteen whole cells, set, each showing its own colour
    for r in range(PH):
        for c in range(PW):
            pixels[cell(r, c):cell(r, c) + 8] = bytes([pal]) * 8
            matrix[r * CW + c] = (r * PW + c) << 4

    # the tools: four cells each, the picture's quadrants in bitmap order
    for t, pic in enumerate(pics):
        place_tile(pixels, matrix, pic, PW + t * TW, fat, face, border, mk)

    # the four slot chips, one cell each along the band's top row: a solid value-1 block
    # with a value-2 frame, so the block's colour is the matrix's HIGH nibble -- which is
    # what gpChips writes at run time, one slot's colour a chip. Nothing is drawn here in
    # hires: those columns are empty band, and their entries exist for the hit test alone.
    if MODES[MODE]["chips"]:
        for i in range(4):
            dst = cell(0, CHIP_C0 + i)
            for y in range(8):
                if fat:
                    if y in (0, 7):
                        pixels[dst + y] = 0xAA          # the top and bottom border: value 2
                    else:
                        pixels[dst + y] = 0x95          # left pixel 2, the rest value 1
                else:
                    pixels[dst + y] = 0xAA if y in (0, 7) else 0xC0
            matrix[CHIP_C0 + i] = (0 << 4) | CHIP_FRAME                # the run-time half
            matrix[CW + CHIP_C0 + i] = (0 << 4) | CHIP_FRAME           # ...and its row
    # the CLOSING icons: the two arrows the band ends with, STACKED -- undo across the
    # top cell row and redo across the bottom one, two cells each. They sit past the
    # chips, at the band's right edge, which is what lets the picker split them by ROW
    for i, pic in enumerate(closing):
        place_icon(pixels, matrix, pic, ICON_C0, i, fat, face, border, mk)
    return pixels, matrix


def main():
    text = SRC.read_text()
    pics = parse_pictures(text, SRC)
    if not pics:
        raise SystemExit("%s: no pictures" % SRC.name)
    # THE ROW COUNT IS WHAT SORTS THEM: a 16-row picture is a tool TILE (two cells
    # across, both band rows) and an 8-row one is a CLOSING icon (two cells across, ONE
    # band row), which is how the two arrows fit in the band's last two columns at all.
    tiles = [p for p in pics if len(p) == PIC_H]
    closing = [p for p in pics if len(p) == CLOSE_H]
    # TWO opening pictures, because that is all the band has room for: the icons are
    # placed by CELL ROW, so a third would be written past the band's two rows. What
    # the two pictures DRAW is the artist's -- nothing reads their pixels.
    if len(closing) != NCLOSING:
        raise SystemExit("%s: %d closing pictures, and the band ends with %d -- the undo "
                         "arrow above and the redo arrow below"
                         % (SRC.name, len(closing), NCLOSING))
    if MODE == "mc":
        # the MODE tile's MC face replaces the hires one, so both bands carry it as the
        # THIRTEENTH tile -- the band's order is the entry order the task's hit test uses
        tiles = tiles[:NTILES - 1] + [tiles[NTILES]]
    else:
        tiles = tiles[:NTILES]
    if len(tiles) > TOOL_MAX:
        raise SystemExit("%s: %d pictures, and the band holds %d beside the palette"
                         % (SRC.name, len(tiles), TOOL_MAX))
    pixels, matrix = build(tiles, closing)
    OUT.write_bytes(bytes(pixels) + bytes(matrix))
    live = (PW + len(tiles) * TW) * 8          # the tiles' own art, in pixels
    print("wrote %s: %d bytes (%s, %d tiles ending at %d = GP_PK_TW4, the chips and the "
          "two stacked arrows to the band's right edge)"
          % (OUT.relative_to(ROOT), len(pixels) + len(matrix), MODE, len(tiles), live))


if __name__ == "__main__":
    main()
