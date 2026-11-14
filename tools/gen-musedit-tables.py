#!/usr/bin/env python3
"""gen-musedit-tables.py -- musedit's copy of the player's tempo table.

`musedit` measures a note the way `sidplay`'s player does, and one branch of that
needs `DataTempoTable`, the 32-byte triplet base indexed by `tempo >> 3`. The
player owns that table (`src/libs/sidplay.inc`), so this tool COPIES it rather
than restating it: the source is parsed, and the emitted file carries the bytes
with the tempo each one answers for.

Output: src/tasks/dynamic/musedit-tables.inc

    muTempoTable   32 bytes: DataTempoTable[tempo >> 3], the triplet base

Usage:
    python tools/gen-musedit-tables.py            write the .inc
    python tools/gen-musedit-tables.py -Check      exit 1 if it is not current
"""

import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
PLAYER = os.path.join(ROOT, "src", "libs", "sidplay.inc")
OUT = os.path.join(ROOT, "src", "tasks", "dynamic", "musedit-tables.inc")

COUNT = 32


def player_table():
    """Read DataTempoTable out of the player source, as 32 bytes."""
    text = open(PLAYER, encoding="utf-8").read()
    m = re.search(r"^DataTempoTable:[ \t]*((?:\.byte[^\n]*\n?(?:[ \t]+\.byte[^\n]*\n?)*))",
                  text, re.M)
    if not m:
        raise SystemExit("gen-musedit-tables: DataTempoTable not found in %s" % PLAYER)
    values = []
    for token in re.findall(r"\$([0-9A-Fa-f]{2})", m.group(1)):
        values.append(int(token, 16))
    if len(values) != COUNT:
        raise SystemExit("gen-musedit-tables: DataTempoTable is %d bytes, expected %d"
                         % (len(values), COUNT))
    return values


def build(values):
    out = []
    out.append("// ==========================================")
    out.append("//  musedit-tables.inc -- GENERATED, do not edit by hand.")
    out.append("//  Source: tools/gen-musedit-tables.py   (run it, or -Check in the build)")
    out.append("// ==========================================")
    out.append("//  src/libs/sidplay.inc's DataTempoTable, byte for byte: the triplet base")
    out.append("//  the player indexes with tempo >> 3. A triplet length is this byte shifted")
    out.append("//  right, so a tempo whose block is 0 has no triplet lengths at all.")
    out.append("muTempoTable:")
    for row in range(0, COUNT, 4):
        chunk = values[row:row + 4]
        tempos = ", ".join("$%02X" % ((row + i) * 8) for i in range(len(chunk)))
        out.append("    .byte %s   // tempo %s"
                   % (",".join("$%02X" % b for b in chunk), tempos))
    out.append("")
    return "\n".join(out)


def main():
    values = player_table()
    text = build(values)
    if "-Check" in sys.argv:
        if not os.path.exists(OUT):
            print("gen-musedit-tables: %s is missing" % OUT)
            return 1
        if open(OUT, encoding="utf-8").read() != text:
            print("gen-musedit-tables: %s is stale -- run the tool" % OUT)
            return 1
        print("gen-musedit-tables: OK (%d bytes, current)" % COUNT)
        return 0
    with open(OUT, "w", encoding="utf-8", newline="\n") as f:
        f.write(text)
    print("gen-musedit-tables: wrote %s (%d bytes)" % (OUT, COUNT))
    return 0


if __name__ == "__main__":
    sys.exit(main())
