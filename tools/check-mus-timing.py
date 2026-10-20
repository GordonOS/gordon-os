#!/usr/bin/env python3
"""check-mus-timing.py -- the duration arithmetic, checked against the player.

`musedit` shows rows on a grid, and a row is the shortest plain note the tempo
can express. Every number behind that comes from `sidplay.inc`'s own duration
routine, so this tool re-implements that routine in Python and checks it against
two things that were written independently of it:

  * the published tempo table in reference/sidplayer-mus-file-format.md (32 rows
    x 7 duration columns) -- a doc written from the original magazine chart;
  * the generated table copy musedit carries (tools/gen-musedit-tables.py), which
    must be `DataTempoTable` byte for byte.

It then sweeps all 256 tempo values x 8 duration classes x 4 dot/triplet states
(8,192 cases) so no combination is left unexamined, and prints the values the
C64 debug line must reproduce.

Usage:
    python tools/check-mus-timing.py          exit 0 when everything agrees
"""

import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
PLAYER = os.path.join(ROOT, "src", "libs", "sidplay.inc")
GENERATED = os.path.join(ROOT, "src", "tasks", "dynamic", "musedit-tables.inc")
DOC = os.path.join(ROOT, "reference", "sidplayer-mus-file-format.md")

# Note duration classes, from the format's bits 2-4.
CLASS_NAMES = {0: "64th", 1: "UTL/UTV", 2: "whole", 3: "half", 4: "quarter",
               5: "8th", 6: "16th", 7: "32nd"}
# Dotted / double-dotted / triplet, from bits 5 and 7 (bit 6 is the tie).
STATE_NAMES = (0x00, 0x20, 0x80, 0xA0)


def player_table():
    text = open(PLAYER, encoding="utf-8").read()
    m = re.search(r"^DataTempoTable:[ \t]*((?:\.byte[^\n]*\n?(?:[ \t]+\.byte[^\n]*\n?)*))",
                  text, re.M)
    if not m:
        raise SystemExit("check-mus-timing: DataTempoTable not found in sidplay.inc")
    values = [int(t, 16) for t in re.findall(r"\$([0-9A-Fa-f]{2})", m.group(1))]
    if len(values) != 32:
        raise SystemExit("check-mus-timing: DataTempoTable is %d bytes" % len(values))
    return values


TEMPO_TABLE = player_table()


def rol8(value, count):
    """A 6502 rotate-left through carry, done `count` times."""
    for _ in range(count):
        value = ((value << 1) | (value >> 7)) & 0xFF
    return value


def tempo_base(tempo):
    return TEMPO_TABLE[(tempo >> 3) & 0x1F]


def duration(cls, tempo, state, utl=42, utv=17):
    """The jiffies the player gives a note, or None when it refuses the length.

    Translated from `calculateNoteDuration` in src/libs/sidplay.inc: the class
    comes from bits 2-4 of the duration byte, `state` is bits 7 and 5 (triplet,
    dotted), and every shift that would drop a bit is an illegal duration.
    """
    if cls == 0:
        if state & 0x80:                      # triplet 64th: the table, rotated
            v = tempo_base(tempo)
            if v == 0 or (v & 0x3F):
                return None
            out = rol8(v, 2)
            return out if out else None
        if tempo & 0x3C:                      # only the 1-jiffy-scale tempos
            return None
        out = rol8(tempo, 2)
        return out if out else 4
    if cls == 1:                              # UTL, or this voice's UTV
        return utv if (state & 0x20) else utl
    if state == 0x80:                         # triplet 8th..32nd: the table
        v = tempo_base(tempo)
        if v == 0:
            return None
        for _ in range(cls - 2):
            if v & 1:
                return None
            v >>= 1
        return v
    # plain, dotted, double-dotted: the tempo itself, shifted right
    carry = 0 if tempo else 1                 # the tempo-0 path seeds the carry
    value = tempo
    for _ in range(cls - 2):
        if value & 1:                         # a bit would fall out of the byte
            return None
        value = ((carry << 7) | (value >> 1)) & 0xFF
        carry = 0
    if value == 0:
        return 0
    if state == 0x20 or state == 0xA0:
        if value & 1:                         # a dotted length needs an even base
            return None
        half = value >> 1
        if half == 0:
            return None
        total = value + half
        if total > 0xFF:
            return None
        if state == 0x20:
            return total
        if half & 1:                          # double-dotted needs a base/4
            return None
        total += half >> 1
        return total if total <= 0xFF else None
    return value


def doc_chart():
    """The published tempo table: {tempo: [whole, half, quarter, 8th, 16th, 32nd, 64th]}."""
    text = open(DOC, encoding="utf-8").read()
    rows = {}
    for line in text.splitlines():
        m = re.match(r"\|\s*\$([0-9A-Fa-f]{2})\s*\|([^|]*)\|([^|]*)\|([^|]*)\|([^|]*)\|"
                     r"([^|]*)\|([^|]*)\|([^|]*)\|([^|]*)\|", line)
        if not m:
            continue
        tempo = int(m.group(1), 16)
        cells = []
        for cell in m.groups()[1:]:
            cell = cell.strip()
            cells.append(int(cell) if cell.isdigit() else None)
        if len(cells) != 8:
            continue
        rows[tempo] = cells[1:]               # drop the M.M. column
    return rows


def check_chart():
    chart = doc_chart()
    if len(chart) != 32:
        raise SystemExit("check-mus-timing: read %d tempo rows from the doc, expected 32"
                         % len(chart))
    bad = 0
    order = [2, 3, 4, 5, 6, 7, 0]             # whole .. 64th
    for tempo in sorted(chart):
        for i, cls in enumerate(order):
            want = chart[tempo][i]
            got = duration(cls, tempo, 0x00)
            if want != got:
                bad += 1
                print("  ! tempo $%02X %-8s doc %-5s player %s"
                      % (tempo, CLASS_NAMES[cls], want, got))
    print("chart: %d rows x 7 columns, %d disagreement(s)" % (len(chart), bad))
    return bad


def check_table_copy():
    if not os.path.exists(GENERATED):
        raise SystemExit("check-mus-timing: %s is missing (run gen-musedit-tables.py)"
                         % GENERATED)
    text = open(GENERATED, encoding="utf-8").read()
    body = "\n".join(line.split("//")[0] for line in text.splitlines()
                      if line.strip().startswith(".byte"))
    values = [int(t, 16) for t in re.findall(r"\$([0-9A-Fa-f]{2})", body)]
    if values != TEMPO_TABLE:
        raise SystemExit("check-mus-timing: musedit-tables.inc does not match "
                         "sidplay.inc's DataTempoTable")
    print("table: musedit-tables.inc matches sidplay.inc byte for byte")
    return 0


def unit_and_rows(tempo):
    """The row unit and the rows a bar has, at this tempo."""
    best = None
    for cls in range(2, 8):
        value = duration(cls, tempo, 0x00)
        if value:
            rows = 1 << (cls - 2)
            if best is None or value < best[0]:
                best = (value, rows)
    value = duration(0, tempo, 0x00)
    if value and (best is None or value < best[0]):
        best = (value, 64)
    return best


def sweep():
    """All 8,192 combinations, reported by class so a gap is visible."""
    legal = {}
    for tempo in range(256):
        for cls in range(8):
            for state in STATE_NAMES:
                value = duration(cls, tempo, state)
                if value is not None and value > 0xFF:
                    raise SystemExit("check-mus-timing: $%02X class %d state $%02X "
                                     "gave %d jiffies, which does not fit a byte"
                                     % (tempo, cls, state, value))
                legal[(tempo, cls, state)] = value
    total = len(legal)
    ok = sum(1 for v in legal.values() if v is not None)
    print("sweep: %d cases (%d legal lengths, %d refused)" % (total, ok, total - ok))
    # Every tempo must have a unit: a tempo with no legal length at all would
    # give the grid no row to draw.
    for tempo in range(256):
        if unit_and_rows(tempo) is None:
            raise SystemExit("check-mus-timing: tempo $%02X has no legal duration"
                             % tempo)
    print("unit: every one of the 256 tempos has a row unit")
    return legal


def show(tempo):
    """The two lines musedit's `B` key prints, for this tempo."""
    cells = []
    for cls, tag in ((2, "W"), (3, "H"), (4, "Q"), (5, "8"), (6, "16"),
                     (7, "32"), (0, "64")):
        value = duration(cls, tempo, 0x00)
        cells.append("%s:%s" % (tag, "-" if value is None else value))
    tri = duration(4, tempo, 0x80)
    unit = unit_and_rows(tempo)
    print("  $%02X  %s" % (tempo, " ".join(cells)))
    print("        TEM $%02X TQ=%s UNIT=%d ROW/BAR=%d"
          % (tempo, "-" if tri is None else tri, unit[0], unit[1]))


def main():
    bad = check_chart()
    check_table_copy()
    sweep()
    print("debug lines the C64 must reproduce:")
    for tempo in (0x90, 0xA0, 0x80, 0x60, 0x40, 0x00, 0x08):
        show(tempo)
    if bad:
        print("FAIL: %d chart disagreement(s)" % bad)
        return 1
    print("check-mus-timing: OK")
    return 0


if __name__ == "__main__":
    sys.exit(main())
