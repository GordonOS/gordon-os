"""Check a task's declared zero-page map for two labels on one byte.

A task declares its zero-page region as `.label name = MU_ZP_BASE+N`, and states a
span in the line's comment (`// 2 bytes`). Nothing else can see a duplicate: the
assembler gives two labels the same address without complaint, the task's own
asserts pin only the TOP of the window (`MU_ZP_END`), and the fault shows up as a
logic bug in whichever routine the other one corrupts -- two routines that never
overlap in testing share a byte silently until the day they do.

usage: check-zp-map.py [file ...]      default: src/tasks/dynamic/musedit.inc
       check-zp-map.py --base NAME     the base symbol (default MU_ZP_BASE)
Exit: 0 clean, 1 an overlap or a label outside the map, 2 nothing to check.
"""
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
DEFAULT = [ROOT / "src" / "tasks" / "dynamic" / "musedit.inc"]

LABEL = re.compile(r"^\s*\.(?:label|const)\s+(\w+)\s*=\s*(\w+)\+(\d+)")
SPAN = re.compile(r"(\d+)\s*bytes?\b")


def spans(path, base):
    """[(offset, length, name, lineno)] for every name based on `base`."""
    out = []
    for lineno, line in enumerate(path.read_text(encoding="utf-8",
                                                 newline="").splitlines(), 1):
        m = LABEL.match(line)
        if not m or m.group(2) != base:
            continue
        if m.group(1).endswith("_END"):
            continue            # the map's end marker, not storage
        offset = int(m.group(3))
        #  A declared span lives in the trailing comment; the search starts after
        #  the `//` so a number in the name or the offset cannot be misread.
        comment = line.split("//", 1)[1] if "//" in line else ""
        s = SPAN.search(comment)
        length = int(s.group(1)) if s else 1
        out.append((offset, length, m.group(1), lineno))
    return out


def check(path, base):
    if not path.exists():
        print("check-zp-map: %s does not exist" % path)
        return 2
    found = spans(path, base)
    if not found:
        print("check-zp-map: no %s+ labels in %s" % (base, path.name))
        return 2
    owner = {}
    bad = 0
    for offset, length, name, lineno in found:
        for byte in range(offset, offset + length):
            if byte in owner:
                other, other_line = owner[byte]
                print("%s:%d: %s overlaps %s (%s+%d, byte %d)"
                      % (path.name, lineno, name, other, base, byte,
                         offset))
                print("  %s:%d: %s" % (path.name, other_line, other))
                bad += 1
                break
            owner[byte] = (name, lineno)
    return 1 if bad else 0


def main(argv):
    base, files = "MU_ZP_BASE", []
    i = 0
    while i < len(argv):
        if argv[i] == "--base":
            base = argv[i + 1]
            i += 2
            continue
        files.append(Path(argv[i]))
        i += 1
    if not files:
        files = DEFAULT
    worst = 0
    checked = 0
    for path in files:
        rc = check(path, base)
        worst = max(worst, rc)
        if rc != 2:
            checked += 1
            top = max((o + l for o, l, _, _ in spans(path, base)), default=0)
            print("%-20s %2d names, top byte +%d  %s"
                  % (path.name, len(spans(path, base)), top - 1,
                     "OK" if rc == 0 else "OVERLAP"))
    if not checked:
        print("check-zp-map: nothing checked")
        return 2
    return 1 if worst == 1 else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
