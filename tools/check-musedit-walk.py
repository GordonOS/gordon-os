"""Check the host walk model against what the editor actually drew.

`tools/mus-sim.py --walk` prints the editor's own line list: a lane per voice, one
line per structural command and one per time row. `tools/fixtures/` holds the same
lines as the EDITOR rendered them, captured from a running musedit. This compares
the two, so a change to the walk -- or to a tune -- cannot quietly stop the model
matching the machine.

The fixture is the editor's grid as text: lane 1 at column 4, lanes 12 apart, and a
row's number right-aligned in columns 1-2 on the row's own line.

usage: check-musedit-walk.py [--verbose]
Exit: 0 agreement, 1 a drift, 2 could not run.
"""
import re
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
SIM = ROOT / "tools" / "mus-sim.py"
CASES = [("immig", ROOT / "tools" / "fixtures" / "musedit-walk-immig.txt")]

LANES = re.compile(r"\|([^|]{11})\|([^|]{11})\|([^|]{11})\|")
ROW = re.compile(r"^\s+\S+\s+(\S+)\s+\|")


def fields(line):
    """(row number or blank, lane 1, lane 2, lane 3), with padding ignored.

    The two renderers disagree only on how a command's operand is padded (the model
    pads the mnemonic to four cells, the editor puts the value in a fixed column),
    so each lane is compared by its words: a different mnemonic, a different value
    or a note in the wrong lane still fails.
    """
    line = line.ljust(39)
    return (line[0:3].rstrip(),
            " ".join(line[4:15].split()),
            " ".join(line[16:27].split()),
            " ".join(line[28:39].split()))


def model_lines(tune, count):
    """The editor's grid as mus-sim renders it, in the editor's columns."""
    out = subprocess.run([sys.executable, str(SIM), str(ROOT / "music" / (tune + ".mus")),
                          "--walk", str(count)],
                         capture_output=True, text=True)
    if out.returncode != 0:
        raise SystemExit("check-musedit-walk: mus-sim failed for %s:\n%s"
                         % (tune, out.stderr.strip()))
    lines, row = [], 0
    for text in out.stdout.splitlines():
        if "tick" in text:          # the column header, not a line
            continue
        m = LANES.search(text)
        if not m:
            continue
        head = ROW.match(text)
        command = head is not None and head.group(1) == "-"
        if command:
            prefix = "    "
        else:
            #  a row's own line carries its number, zero-filled in columns 1-2
            prefix = " %02d " % ((row % 100) + 1)
            row += 1
        lines.append((prefix + m.group(1) + " " + m.group(2) + " " + m.group(3)).rstrip())
    return lines


def main(argv):
    verbose = "--verbose" in argv
    bad = 0
    for tune, fixture in CASES:
        want = [line.rstrip() for line in fixture.read_text().splitlines()]
        got = model_lines(tune, len(want))
        if verbose:
            print("--- %s: %d lines modelled, %d in the fixture" % (tune, len(got), len(want)))
        drift = None
        for i in range(max(len(got), len(want))):
            a = fields(got[i]) if i < len(got) else ("<no line>",)
            b = fields(want[i]) if i < len(want) else ("<no line>",)
            if a != b:
                drift = (i, a, b)
                break
        if drift is None:
            print("walk: %s matches the editor over %d lines" % (tune, len(want)))
            continue
        bad += 1
        i, a, b = drift
        print("walk: %s DRIFTED at line %d" % (tune, i))
        print("  model:  %s" % (a,))
        print("  editor: %s" % (b,))
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
