#!/usr/bin/env python3
"""gen-musedit-msgs.py -- every string musedit prints, out of its code and into the REU.

The editor is a relocatable task, so a character of text costs a byte of the pool
pages it lives in -- and the status messages and screen chrome are ~360 bytes of
them. They move to one file in the REU filesystem (`msgs`) that the task reads a
slot at a time, which shrinks the code by more than the whole text ever cost: a
`muStr(label)` site is 12 bytes of code (two absolute-address stores) and a
`muMsg(index)` site is 5.

One file, fixed 64-byte slots: a slot is longer than the longest string (38), so a
message's offset is `index * 64` and there is no offset table to read first.
`muMsg` reads one whole slot in a single DMA and points MU_STR at the buffer, so a
message needs no copy and no terminator hunt beyond the NUL the slot carries.

Output, both from assets/ui/musedit-msgs.txt:

    assets/ui/musedit-msgs.bin      the slots, shipped in the REU image as `msgs`
    src/tasks/dynamic/musedit-msgs.inc   one M_* index per message

Usage:
    python tools/gen-musedit-msgs.py            write both files
    python tools/gen-musedit-msgs.py -Check     exit 1 if either is not current
"""

import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SRC = os.path.join(ROOT, "assets", "ui", "musedit-msgs.txt")
BIN = os.path.join(ROOT, "assets", "ui", "musedit-msgs.bin")
INC = os.path.join(ROOT, "src", "tasks", "dynamic", "musedit-msgs.inc")

SLOT = 64                       # bytes per message: longest is 38 + NUL

LINE = re.compile(r'^([A-Z][A-Z0-9_]*)\s+"(.*)"$')


def parse():
    """[(name, text)] in slot order. `//` comments and blank lines are skipped."""
    out = []
    seen = set()
    with open(SRC, encoding="ascii", newline="\n") as fh:
        for n, line in enumerate(fh, 1):
            s = line.strip()
            if not s or s.startswith("//"):
                continue
            m = LINE.match(s)
            if not m:
                raise SystemExit("%s:%d: not 'NAME \"text\"': %s" % (SRC, n, s))
            name, text = m.group(1), m.group(2)
            if name in seen:
                raise SystemExit("%s:%d: %s is declared twice" % (SRC, n, name))
            if len(text) >= SLOT:
                raise SystemExit("%s:%d: %s is %d characters, a slot is %d"
                                 % (SRC, n, name, len(text), SLOT - 1))
            try:
                text.encode("ascii")
            except UnicodeEncodeError:
                raise SystemExit("%s:%d: %s is not ASCII" % (SRC, n, name))
            seen.add(name)
            out.append((name, text))
    if not out:
        raise SystemExit("%s: no messages" % SRC)
    return out


def build_inc(msgs):
    out = []
    out.append("// ==========================================")
    out.append("//  musedit-msgs.inc -- GENERATED, do not edit by hand.")
    out.append("//  Source: assets/ui/musedit-msgs.txt   (tools/gen-musedit-msgs.py)")
    out.append("// ==========================================")
    out.append("//  One index per string musedit prints. The text is not here: the REU image")
    out.append("//  holds it as `msgs`, one MU_MSG_SLOT-byte record per index, in this order,")
    out.append("//  each NUL terminated. muMsg DMAs a record into its buffer and points the")
    out.append("//  text at it, so a string costs no code page at all.")
    out.append(".const MU_MSG_COUNT = %d" % len(msgs))
    out.append(".const MU_MSG_SLOT = %d" % SLOT)
    for i, (name, text) in enumerate(msgs):
        out.append(".const %-12s = %d" % (name, i))
    out.append("")
    out.append("// The length of each message, for a layout that depends on it: the bar's")
    out.append("// labels sit in fixed cells, so their width is not decoration.")
    for name, text in msgs:
        out.append(".const %-12s = %d" % (name + "_LEN", len(text)))
    out.append("")
    return "\n".join(out)


def main():
    msgs = parse()
    inc = build_inc(msgs)
    blob = bytearray()
    for _, text in msgs:
        rec = text.encode("ascii") + b"\0"
        blob += rec + b"\0" * (SLOT - len(rec))

    check = "-Check" in sys.argv
    if check:
        bad = False
        for path, want, binary in ((INC, inc.encode("ascii"), False),
                                   (BIN, bytes(blob), True)):
            try:
                with open(path, "rb") as fh:
                    have = fh.read()
            except FileNotFoundError:
                print("gen-musedit-msgs: %s is missing" % path)
                bad = True
                continue
            if have != want:
                print("gen-musedit-msgs: %s is stale -- run the generator" % path)
                bad = True
        if bad:
            return 1
        print("gen-musedit-msgs: OK (%d messages, %d bytes)" % (len(msgs), len(blob)))
        return 0

    with open(INC, "w", encoding="ascii", newline="\n") as fh:
        fh.write(inc)
    with open(BIN, "wb") as fh:
        fh.write(blob)
    print("gen-musedit-msgs: wrote %d messages, %d bytes (longest %d)"
          % (len(msgs), len(blob), max(len(t) for _, t in msgs)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
