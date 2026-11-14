#!/usr/bin/env python3
"""gen-musedit-msgs.py -- every string musedit prints, out of its code and into the REU.

The editor is a relocatable task, so a character of text costs a byte of the pool
pages it lives in -- and the status messages and screen chrome are ~360 bytes of
them. They move to one file in the REU filesystem (`msgs`) that the task reads a
slot at a time, which shrinks the code by more than the whole text ever cost: a
`muStr(label)` site is 12 bytes of code (two absolute-address stores) and a
`muMsg(index)` site is 5.

One file, fixed 64-byte slots: a slot is longer than the longest string, so a
message's offset is `index * 64` and there is no offset table to read first.
`muMsg` DMAs the front of a slot -- MU_MSG_BUF bytes, the longest message plus
its NUL -- into its buffer and points MU_STR at it, so a message needs no copy
and the record's padding is never read.

The same file carries the command mnemonics, in a block of their own after the
slots (assets/ui/musedit-mnems.txt): 49 fields of four bytes, one per muMnPrint id,
at a 256-byte boundary so a field's offset is `(id - 1) * 4` and its page is a
constant. The grid draws one field per command line, so the 196 bytes of text cost
the task nothing but a read it already knows how to make.

The .bin holds SCREEN CODES, not ASCII -- the editor prints it straight to the
screen, and the two differ for every letter. A lower-case letter is $01-$1A; an
upper-case one is its ASCII code ($41-$5A); everything else passes through. That is
exactly what `.text` under `.encoding "screencode_mixed"` emits, so a string reads
the same whether it is assembled into the task or loaded from the image.

Output, from assets/ui/musedit-msgs.txt and assets/ui/musedit-mnems.txt:

    assets/ui/musedit-msgs.bin      the slots and the mnemonics, shipped as `msgs`
    src/tasks/dynamic/musedit-msgs.inc   one M_* index and one MU_MN_* id each

Usage:
    python tools/gen-musedit-msgs.py            write both files
    python tools/gen-musedit-msgs.py -Check     exit 1 if either is not current
"""

import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SRC = os.path.join(ROOT, "assets", "ui", "musedit-msgs.txt")
MN_SRC = os.path.join(ROOT, "assets", "ui", "musedit-mnems.txt")
BIN = os.path.join(ROOT, "assets", "ui", "musedit-msgs.bin")
INC = os.path.join(ROOT, "src", "tasks", "dynamic", "musedit-msgs.inc")

SLOT = 64                       # bytes per message slot -- a message must fit
                                #   with its NUL (checked in build())
FIELD = 4                       # bytes per mnemonic: three characters and a space

LINE = re.compile(r'^([A-Z][A-Z0-9_]*)\s+"(.*)"$')
MN_LINE = re.compile(r'^([A-Z][A-Z0-9_]*)\s+"([^"]*)"\s*(?://.*)?$')


def screencode(text):
    """The bytes the screen wants, exactly as `.text` under `screencode_mixed`
    emits them: a lower-case letter is $01-$1A, and every other character --
    upper-case letters included -- is its own ASCII code ($20-$3F and $41-$5A).

    The file has to hold these rather than ASCII: the editor prints it straight to
    the screen, and ASCII $52 for 'R' is a graphics character.
    """
    return bytes((ord(c) - 96) if "a" <= c <= "z" else ord(c) for c in text)


# The mapping above is the assembler's, so it is checked against the values
# KickAssembler gives for the same text rather than trusted.
assert screencode("aZ") == b"\x01\x5a", "screencode does not match screencode_mixed"


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


def parse_mnems():
    """[(name, text)] in id order, id 1 first. `//` comments are skipped."""
    out = []
    seen = set()
    with open(MN_SRC, encoding="ascii", newline="\n") as fh:
        for n, line in enumerate(fh, 1):
            s = line.strip()
            if not s or s.startswith("//"):
                continue
            m = MN_LINE.match(s)
            if not m:
                raise SystemExit("%s:%d: not 'NAME \"text\"': %s" % (MN_SRC, n, s))
            name, text = m.group(1), m.group(2)
            if not name.startswith("MU_MN_"):
                raise SystemExit("%s:%d: %s is not an MU_MN_* id" % (MN_SRC, n, name))
            if name in seen:
                raise SystemExit("%s:%d: %s is declared twice" % (MN_SRC, n, name))
            # A space ends the printed field, so it cannot be part of the text.
            if not text or len(text) > FIELD or " " in text:
                raise SystemExit("%s:%d: %s is '%s', a field is 1-%d characters with no space"
                                 % (MN_SRC, n, name, text, FIELD))
            try:
                text.encode("ascii")
            except UnicodeEncodeError:
                raise SystemExit("%s:%d: %s is not ASCII" % (MN_SRC, n, name))
            seen.add(name)
            out.append((name, text))
    if not out:
        raise SystemExit("%s: no mnemonics" % MN_SRC)
    return out


def build_inc(msgs, mnems, mn_base):
    out = []
    out.append("// ==========================================")
    out.append("//  musedit-msgs.inc -- GENERATED, do not edit by hand.")
    out.append("//  Sources: assets/ui/musedit-msgs.txt, assets/ui/musedit-mnems.txt")
    out.append("//           (tools/gen-musedit-msgs.py)")
    out.append("// ==========================================")
    out.append("//  One index per string musedit prints. The text is not here: the REU image")
    out.append("//  holds it as `msgs`, one MU_MSG_SLOT-byte record per index, in this order,")
    out.append("//  each NUL terminated. muMsg DMAs the front of a record (MU_MSG_BUF")
    out.append("//  bytes, in musedit.inc) into its buffer and points the text at it, so a")
    out.append("//  string costs no code page at all.")
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
    out.append("// The command mnemonics, appended to the same file after the slots and on a")
    out.append("// page boundary: a field is MU_MN_FIELD bytes and id N sits at (N - 1) * 4,")
    out.append("// so the read's offset is that and its page is M_MN_BASE's own.")
    out.append(".const M_MN_BASE = %d" % mn_base)
    out.append(".const MU_MN_COUNT = %d" % len(mnems))
    out.append(".const MU_MN_FIELD = %d" % FIELD)
    for i, (name, text) in enumerate(mnems):
        out.append(".const %-12s = %d" % (name, i + 1))
    out.append("")
    return "\n".join(out)


def build(msgs, mnems):
    """(the file, the mnemonic block's offset): 64-byte slots, then the mnemonics.

    The block starts on the next 256-byte boundary so muMnPrint's offset high byte
    is a constant -- the low byte alone is `(id - 1) * 4`, which is 256 bytes of
    fields: four to a page.
    """
    blob = bytearray()
    for name, text in msgs:
        rec = screencode(text) + b"\0"
        if len(rec) > SLOT:
            raise SystemExit("%s: %s is %d characters and does not fit a %d-byte slot"
                             % (SRC, name, len(text), SLOT))
        blob += rec + b"\0" * (SLOT - len(rec))
    base = (len(blob) + 255) & ~255
    blob += b"\0" * (base - len(blob))
    for _, text in mnems:
        field = screencode(text)
        blob += field + b" " * (FIELD - len(field))
    return bytes(blob), base


def main():
    msgs = parse()
    mnems = parse_mnems()
    blob, mn_base = build(msgs, mnems)
    inc = build_inc(msgs, mnems, mn_base)

    check = "-Check" in sys.argv
    if check:
        bad = False
        for path, want in ((INC, inc.encode("ascii")), (BIN, blob)):
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
        print("gen-musedit-msgs: OK (%d messages, %d mnemonics, %d bytes)"
              % (len(msgs), len(mnems), len(blob)))
        return 0

    with open(INC, "w", encoding="ascii", newline="\n") as fh:
        fh.write(inc)
    with open(BIN, "wb") as fh:
        fh.write(blob)
    print("gen-musedit-msgs: wrote %d messages, %d mnemonics, %d bytes"
          % (len(msgs), len(mnems), len(blob)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
