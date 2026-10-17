#!/usr/bin/env python3
"""scan-prg.py -- targeted reverse-engineering scan of a C64 PRG.

Complements tools/dis6502.py: that decodes instructions (trustworthy but only for
code it can reach), this matches operand byte pairs so nothing is missed, at the
price of false positives from data that happens to hold a similar pair.

Rather than a full disassembly, this answers the port questions with byte
patterns and readable strings:

  * the BASIC stub's line chain, so the SYS entry point is visible
  * hits on the addresses that decide the port: VIC ($D011/$D012/$D016/$D018),
    CIA and the IRQ vectors, SID, screen/colour RAM, keyboard, and the KERNAL
    file vectors ($FFBA..$FFD8) that say whether LOAD/SAVE are used
  * every ASCII string of 6+ characters

Usage:  python tools/scan-prg.py "path/to/file.prg" [...]
"""

import sys

INTERESTING = {
    0xD011: "VIC control 1 (ECM/BMM/DEN/RSEL/YSCROLL)",
    0xD012: "VIC raster line",
    0xD016: "VIC control 2 (MCM/CSEL/XSCROLL)",
    0xD018: "VIC pointers (screen/charset/bitmap)",
    0xD019: "VIC IRQ flag",
    0xD01A: "VIC IRQ mask",
    0xD020: "border colour",
    0xD021: "background colour",
    0xD800: "colour RAM",
    0x0400: "default screen RAM",
    0xDD00: "CIA2 port A (VIC bank / IEC lines)",
    0xDC00: "CIA1 port A (keyboard column select)",
    0xDC01: "CIA1 port B (keyboard row read)",
    0xDC04: "CIA1 timer A low",
    0xDC0D: "CIA1 interrupt control",
    0xDC0E: "CIA1 control register A",
    0xD400: "SID voice 1 frequency",
    0xD418: "SID volume / filter mode",
    0xA000: "BASIC ROM / RAM at $A000",
    0x2000: "RAM $2000 (charset/bitmap candidate)",
    0x3000: "RAM $3000 (charset/bitmap candidate)",
    0x3800: "RAM $3800 (charset/bitmap candidate)",
    0x0314: "KERNAL IRQ vector (RAM)",
    0x0318: "KERNAL NMI vector (RAM)",
    0xFFD5: "KERNAL LOAD",
    0xFFD8: "KERNAL SAVE",
    0xFFBA: "KERNAL SETLFS",
    0xFFBD: "KERNAL SETNAM",
    0xFFC0: "KERNAL OPEN",
    0xFFC3: "KERNAL CLOSE",
    0xFFC6: "KERNAL CHKIN",
    0xFFCF: "KERNAL CHRIN",
    0xFFE4: "KERNAL GETIN",
    0xFFE1: "KERNAL STOP",
}


def basic_stub(b):
    """Walk the BASIC line chain at the load address; return its text lines."""
    print("  BASIC line chain:")
    i = 2
    while i + 4 <= len(b):
        nxt = b[i] | (b[i + 1] << 8)
        lineno = b[i + 2] | (b[i + 3] << 8)
        i += 4
        body = []
        while i < len(b) and b[i] != 0:
            body.append(b[i])
            i += 1
        i += 1
        text = ''.join(chr(c) if 32 <= c < 127 else '.' for c in body)
        hexs = ' '.join(f"{c:02X}" for c in body[:24])
        print(f"    line {lineno}  next=${nxt:04X}  {hexs}")
        print(f"      text: {text}")
        if nxt == 0 or nxt <= 0x0801:
            break


def strings(b, minimum=6):
    print(f"  ASCII strings >= {minimum} chars:")
    run = []
    start = 0
    for i, c in enumerate(b):
        if 32 <= c < 127:
            if not run:
                start = i
            run.append(c)
        else:
            if len(run) >= minimum:
                print(f"    +{start:5d}: {''.join(chr(x) for x in run)}")
            run = []
    if len(run) >= minimum:
        print(f"    +{start:5d}: {''.join(chr(x) for x in run)}")


def scan(b, load):
    for addr in sorted(INTERESTING):
        lo, hi = addr & 0xFF, addr >> 8
        sites = [i for i in range(len(b) - 1) if b[i] == lo and b[i + 1] == hi]
        if not sites:
            continue
        print(f"\n  ${addr:04X}  {INTERESTING[addr]}  ({len(sites)} hit(s))")
        for i in sites:
            a = load + i - 2
            c0, c1 = max(0, i - 3), min(len(b), i + 3)
            ctx = ' '.join(f"{x:02X}" for x in b[c0:c1])
            print(f"    at ${a:04X} (file offset {i:5d}):  {ctx}")


def main():
    for path in sys.argv[1:]:
        b = open(path, 'rb').read()
        load = b[0] | (b[1] << 8)
        end = load + len(b) - 3
        print("=" * 78)
        print(f"{path}")
        print(f"  load ${load:04X}  size {len(b)}  ends ${end:04X}")
        basic_stub(b)
        scan(b, load)
        print()
        strings(b)
        print()


if __name__ == '__main__':
    main()
