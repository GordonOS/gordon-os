#!/usr/bin/env python3
"""dis6502.py -- a compact recursive 6502 disassembler.

Why hand-written: this one walks control flow (following JSR/JMP/branches) and
reports the hardware touches, which a linear sweep does not. tools/dis6502-linear.py
runs py65 over the same bytes as the cross-check on the opcode table here. The table
is the documented NMOS 6502 set; anything undocumented prints as `???` and is treated
as one byte.

It walks from the given entry points, following JSR/JMP/branches, so embedded
data (screen art, charsets, tables) is not mistaken for code. Output order is:

  1. coverage: entry points, how much of the image decodes as code, and the
     address ranges nothing reaches (data, or code entered indirectly)
  2. hardware touches: every decoded instruction whose operand is a VIC/CIA/SID/
     KERNAL address, which is the port-relevant part
  3. the listing

Usage:
    python tools/dis6502.py FILE --entry 0881 [--entry XXXX ...] [--all] [--asm]
"""

import sys

IMP, ACC, IMM, ZP, ZPX, ZPY = 'imp', 'acc', 'imm', 'zp', 'zpx', 'zpy'
ABS, ABX, ABY, IND, IZX, IZY, REL = 'abs', 'abx', 'aby', 'ind', 'izx', 'izy', 'rel'
LEN = {IMP: 1, ACC: 1, IMM: 2, ZP: 2, ZPX: 2, ZPY: 2, IZX: 2, IZY: 2, REL: 2,
       ABS: 3, ABX: 3, ABY: 3, IND: 3}

OPS = {}
for _op, _m, _d in [
    (0x00, 'BRK', IMP), (0x01, 'ORA', IZX), (0x05, 'ORA', ZP), (0x06, 'ASL', ZP),
    (0x08, 'PHP', IMP), (0x09, 'ORA', IMM), (0x0a, 'ASL', ACC), (0x0d, 'ORA', ABS),
    (0x0e, 'ASL', ABS), (0x10, 'BPL', REL), (0x11, 'ORA', IZY), (0x15, 'ORA', ZPX),
    (0x16, 'ASL', ZPX), (0x18, 'CLC', IMP), (0x19, 'ORA', ABY), (0x1d, 'ORA', ABX),
    (0x1e, 'ASL', ABX), (0x20, 'JSR', ABS), (0x21, 'AND', IZX), (0x24, 'BIT', ZP),
    (0x25, 'AND', ZP), (0x26, 'ROL', ZP), (0x28, 'PLP', IMP), (0x29, 'AND', IMM),
    (0x2a, 'ROL', ACC), (0x2c, 'BIT', ABS), (0x2d, 'AND', ABS), (0x2e, 'ROL', ABS),
    (0x30, 'BMI', REL), (0x31, 'AND', IZY), (0x35, 'AND', ZPX), (0x36, 'ROL', ZPX),
    (0x38, 'SEC', IMP), (0x39, 'AND', ABY), (0x3d, 'AND', ABX), (0x3e, 'ROL', ABX),
    (0x40, 'RTI', IMP), (0x41, 'EOR', IZX), (0x45, 'EOR', ZP), (0x46, 'LSR', ZP),
    (0x48, 'PHA', IMP), (0x49, 'EOR', IMM), (0x4a, 'LSR', ACC), (0x4c, 'JMP', ABS),
    (0x4d, 'EOR', ABS), (0x4e, 'LSR', ABS), (0x50, 'BVC', REL), (0x51, 'EOR', IZY),
    (0x55, 'EOR', ZPX), (0x56, 'LSR', ZPX), (0x58, 'CLI', IMP), (0x59, 'EOR', ABY),
    (0x5d, 'EOR', ABX), (0x5e, 'LSR', ABX), (0x60, 'RTS', IMP), (0x61, 'ADC', IZX),
    (0x65, 'ADC', ZP), (0x66, 'ROR', ZP), (0x68, 'PLA', IMP), (0x69, 'ADC', IMM),
    (0x6a, 'ROR', ACC), (0x6c, 'JMP', IND), (0x6d, 'ADC', ABS), (0x6e, 'ROR', ABS),
    (0x70, 'BVS', REL), (0x71, 'ADC', IZY), (0x75, 'ADC', ZPX), (0x76, 'ROR', ZPX),
    (0x78, 'SEI', IMP), (0x79, 'ADC', ABY), (0x7d, 'ADC', ABX), (0x7e, 'ROR', ABX),
    (0x81, 'STA', IZX), (0x84, 'STY', ZP), (0x85, 'STA', ZP), (0x86, 'STX', ZP),
    (0x88, 'DEY', IMP), (0x8a, 'TXA', IMP), (0x8c, 'STY', ABS), (0x8d, 'STA', ABS),
    (0x8e, 'STX', ABS), (0x90, 'BCC', REL), (0x91, 'STA', IZY), (0x94, 'STY', ZPX),
    (0x95, 'STA', ZPX), (0x96, 'STX', ZPY), (0x98, 'TYA', IMP), (0x99, 'STA', ABY),
    (0x9a, 'TXS', IMP), (0x9d, 'STA', ABX), (0xa0, 'LDY', IMM), (0xa1, 'LDA', IZX),
    (0xa2, 'LDX', IMM), (0xa4, 'LDY', ZP), (0xa5, 'LDA', ZP), (0xa6, 'LDX', ZP),
    (0xa8, 'TAY', IMP), (0xa9, 'LDA', IMM), (0xaa, 'TAX', IMP), (0xac, 'LDY', ABS),
    (0xad, 'LDA', ABS), (0xae, 'LDX', ABS), (0xb0, 'BCS', REL), (0xb1, 'LDA', IZY),
    (0xb4, 'LDY', ZPX), (0xb5, 'LDA', ZPX), (0xb6, 'LDX', ZPY), (0xb8, 'CLV', IMP),
    (0xb9, 'LDA', ABY), (0xba, 'TSX', IMP), (0xbc, 'LDY', ABX), (0xbd, 'LDA', ABX),
    (0xbe, 'LDX', ABY), (0xc0, 'CPY', IMM), (0xc1, 'CMP', IZX), (0xc4, 'CPY', ZP),
    (0xc5, 'CMP', ZP), (0xc6, 'DEC', ZP), (0xc8, 'INY', IMP), (0xc9, 'CMP', IMM),
    (0xca, 'DEX', IMP), (0xcc, 'CPY', ABS), (0xcd, 'CMP', ABS), (0xce, 'DEC', ABS),
    (0xd0, 'BNE', REL), (0xd1, 'CMP', IZY), (0xd5, 'CMP', ZPX), (0xd6, 'DEC', ZPX),
    (0xd8, 'CLD', IMP), (0xd9, 'CMP', ABY), (0xdd, 'CMP', ABX), (0xde, 'DEC', ABX),
    (0xe0, 'CPX', IMM), (0xe1, 'SBC', IZX), (0xe4, 'CPX', ZP), (0xe5, 'SBC', ZP),
    (0xe6, 'INC', ZP), (0xe8, 'INX', IMP), (0xe9, 'SBC', IMM), (0xea, 'NOP', IMP),
    (0xec, 'CPX', ABS), (0xed, 'SBC', ABS), (0xee, 'INC', ABS), (0xf0, 'BEQ', REL),
    (0xf1, 'SBC', IZY), (0xf5, 'SBC', ZPX), (0xf6, 'INC', ZPX), (0xf8, 'SED', IMP),
    (0xf9, 'SBC', ABY), (0xfd, 'SBC', ABX), (0xfe, 'INC', ABX),
]:
    OPS[_op] = (_m, _d)

ADDR = {
    0xD011: 'VIC ctrl1', 0xD012: 'VIC raster', 0xD016: 'VIC ctrl2',
    0xD018: 'VIC ptrs', 0xD019: 'VIC irqflag', 0xD01A: 'VIC irqmask',
    0xD020: 'border', 0xD021: 'background', 0xD800: 'colour RAM',
    0x0400: 'screen RAM', 0xDC00: 'CIA1 PA kbd', 0xDC01: 'CIA1 PB kbd',
    0xDC04: 'CIA1 timerA', 0xDC0D: 'CIA1 icr', 0xDC0E: 'CIA1 cra',
    0xDD00: 'CIA2 PA vicbank', 0xD400: 'SID v1', 0xD418: 'SID vol',
    0x0314: 'IRQ vector', 0x0318: 'NMI vector', 0xFFE4: 'GETIN',
    0xFFD5: 'LOAD', 0xFFD8: 'SAVE', 0xFFBA: 'SETLFS', 0xFFBD: 'SETNAM',
    0xFFC0: 'OPEN', 0xFFC3: 'CLOSE', 0xFFC6: 'CHKIN', 0xFFCF: 'CHRIN',
    0xFFD2: 'CHROUT', 0xFFE1: 'STOP', 0xFFC9: 'CHKOUT', 0xFFB7: 'CLRCHN',
}

# Vectors installed as a lo/hi store pair (KERNAL and hardware). A store to one of
# these is a code pointer, so reading them extends the walk into handlers nothing
# else references - an IRQ or NMI entry is otherwise unreachable, which is exactly
# how a whole display routine hides from a walker that follows JSR/JMP only.
VECTOR_PAIRS = {
    0x0314: 0x0315,   # KERNAL IRQ
    0x0318: 0x0319,   # KERNAL NMI
    0xFFFA: 0xFFFB,   # hardware NMI
    0xFFFC: 0xFFFD,   # RESET
    0xFFFE: 0xFFFF,   # hardware IRQ
}


def decode(b, addr, off):
    """Decode one instruction at file offset off / address addr."""
    op = b[off]
    if op not in OPS:
        return '???', 1, None, None
    mnem, mode = OPS[op]
    n = LEN[mode]
    raw = b[off:off + n]
    if len(raw) < n:
        return '???', 1, None, None
    lo = raw[1] if n > 1 else 0
    hi = raw[2] if n > 2 else 0
    w = lo | (hi << 8)
    if mode == IMM:
        text = f"{mnem} #${lo:02X}"
    elif mode == ZP:
        text = f"{mnem} ${lo:02X}"
    elif mode == ZPX:
        text = f"{mnem} ${lo:02X},X"
    elif mode == ZPY:
        text = f"{mnem} ${lo:02X},Y"
    elif mode == IZX:
        text = f"{mnem} (${lo:02X},X)"
    elif mode == IZY:
        text = f"{mnem} (${lo:02X}),Y"
    elif mode == ABS:
        text = f"{mnem} ${w:04X}"
    elif mode == ABX:
        text = f"{mnem} ${w:04X},X"
    elif mode == ABY:
        text = f"{mnem} ${w:04X},Y"
    elif mode == IND:
        text = f"{mnem} (${w:04X})"
    elif mode == REL:
        target = (addr + n + ((lo ^ 0x80) - 0x80)) & 0xFFFF
        text = f"{mnem} ${target:04X}"
    elif mode == ACC:
        text = f"{mnem} A"
    else:
        text = mnem
    return text, n, mode, w


def selftest():
    cases = [
        (0x0881, b'\xa9\x0b\x8d\x11\xd0', ['LDA #$0B', 'STA $D011']),
        (0x6A00, b'\x20\xd2\xff', ['JSR $FFD2']),
        (0x1000, b'\x6c\x14\x03', ['JMP ($0314)']),
        (0x2000, b'\xbd\x00\x04', ['LDA $0400,X']),
        (0x3000, b'\xb1\xfb', ['LDA ($FB),Y']),
        (0x4000, b'\x01\xfb', ['ORA ($FB,X)']),
        (0x5000, b'\xa2\xf3\xec\x12\xd0', ['LDX #$F3', 'CPX $D012']),
        (0x6000, b'\xd0\xfe', ['BNE $6000']),
        (0x7000, b'\x85\x01\x60', ['STA $01', 'RTS']),
    ]
    bad = 0
    for addr, data, want in cases:
        off = 0
        got = []
        while off < len(data):
            text, n, _, _ = decode(data, addr + off, off)
            got.append(text)
            off += n
        if got != want:
            bad += 1
            print(f"  SELFTEST FAIL at ${addr:04X}: got {got} want {want}")
    print(f"  selftest: {len(cases) - bad}/{len(cases)} ok")


def main():
    args = sys.argv[1:]
    if not args:
        print(__doc__)
        return 1
    path = args[0]
    entries = []
    show_all = '--all' in args
    show_asm = '--asm' in args
    i = 1
    while i < len(args):
        if args[i] == '--entry':
            entries.append(int(args[i + 1], 16))
            i += 2
        else:
            i += 1
    b = open(path, 'rb').read()
    load = b[0] | (b[1] << 8)
    end = load + len(b) - 3

    def off_of(addr):
        return addr - load + 2

    if not show_asm:
        print("=" * 78)
        print(f"{path}   load ${load:04X}  end ${end:04X}")
        selftest()

    starts = list(dict.fromkeys(entries))
    visited = {}          # address -> length (instruction start)
    hardware = []         # (addr, text, comment)

    def walk(seeds):
        """Decode from every seed, following JSR/JMP/branches."""
        work = list(seeds)
        while work:
            pc = work.pop()
            while load <= pc <= end:
                if pc in visited:
                    break
                off = off_of(pc)
                text, n, mode, w = decode(b, pc, off)
                visited[pc] = n
                comment = ''
                if mode in (ABS, ABX, ABY) and w in ADDR:
                    comment = ADDR[w]
                elif mode in (ABS, ABX, ABY) and 0xD000 <= w < 0xE000:
                    comment = 'IO'
                elif mode in (ABS, ABX, ABY) and w >= 0xFF00:
                    comment = 'KERNAL'
                if comment:
                    hardware.append((pc, text, comment))
                mnem = text.split()[0]
                if mnem in ('JSR',) and mode in (ABS, ABX, ABY):
                    work.append(w)
                elif mnem == 'JMP' and mode in (ABS, ABX, ABY):
                    work.append(w)
                    break
                elif mnem in ('RTS', 'RTI', 'BRK') or text == '???':
                    break
                elif mode == REL:
                    work.append(int(text.split('$')[1], 16))
                pc += n

    def vector_seeds():
        """Handlers installed through a lo/hi store pair, found in known code."""
        found = []
        lows = {}
        prev_imm = None
        for pc in sorted(visited):
            text, n, mode, w = decode(b, pc, off_of(pc))
            if text.startswith('LDA #$'):
                prev_imm = int(text.split('$')[1], 16)
                continue
            if prev_imm is not None and mode == ABS and text.startswith('STA $'):
                if w in VECTOR_PAIRS:
                    lows[w] = prev_imm
                else:
                    for lo, hi in VECTOR_PAIRS.items():
                        if w == hi and lo in lows:
                            found.append(lows[lo] | (prev_imm << 8))
            prev_imm = None
        return found

    def plausible(addr, limit=128):
        """Last-resort filter: a real entry point decodes to a terminator, data rarely does.

        The vector-pair test in pattern_seeds() is what does the real work - it
        rejected almost every raw hit. This only catches the freak data match that
        happens to spell the idiom, so the limit is generous: a raster handler can
        run for a long time before its first RTS or JMP.
        """
        pc = addr
        for _ in range(limit):
            if not load <= pc <= end:
                return False
            text, n, mode, w = decode(b, pc, off_of(pc))
            if text == '???':
                return False
            if text.split()[0] in ('RTS', 'RTI', 'BRK', 'JMP'):
                return True
            pc += n
        return False

    def pattern_seeds():
        """Handlers installed by an in-line lo/hi store pair, found ANYWHERE.

        A program that installs a different handler per screen keeps those
        installs inside code the walk has not reached yet, so they cannot be
        harvested from decoded instructions. This scans the whole image for the
        ten-byte `LDA #lo / STA vec / LDA #hi / STA vec+1` idiom instead; random
        data rarely reproduces it, and plausible() rejects what it does.
        """
        found = []
        for off in range(len(b) - 10):
            if b[off] != 0xA9 or b[off + 2] != 0x8D or b[off + 5] != 0xA9 \
                    or b[off + 7] != 0x8D:
                continue
            lo_v = b[off + 3] | (b[off + 4] << 8)
            hi_v = b[off + 8] | (b[off + 9] << 8)
            if VECTOR_PAIRS.get(lo_v) != hi_v:
                continue
            found.append(b[off + 1] | (b[off + 6] << 8))
        return found

    walk(starts)
    while True:
        extra = [a for a in vector_seeds() + pattern_seeds()
                 if load <= a <= end and a not in visited and plausible(a)]
        if not extra:
            break
        walk(extra)

    code_bytes = sum(visited.values())
    if not show_asm:
        print(f"  code: {len(visited)} instructions, {code_bytes} bytes "
              f"of {len(b) - 2} ({100 * code_bytes // max(1, len(b) - 2)}%)")

    # coverage gaps
    covered = set()
    for a, n in visited.items():
        for k in range(n):
            covered.add(a + k)
    gaps = []
    run = None
    for a in range(load, end + 1):
        if a not in covered:
            if run is None:
                run = a
        else:
            if run is not None:
                gaps.append((run, a - 1))
                run = None
    if run is not None:
        gaps.append((run, end))
    gaps = [(s, e) for s, e in gaps if e - s >= 4]
    total_gap = sum(e - s + 1 for s, e in gaps)
    if not show_asm:
        print(f"  unreached: {total_gap} bytes in {len(gaps)} run(s) of >=5 bytes")
        for s, e in gaps:
            print(f"    ${s:04X}-${e:04X}  ({e - s + 1} bytes)")

    if not show_asm:
        print(f"\n  hardware touches ({len(hardware)}):")
        for pc, text, comment in sorted(hardware):
            print(f"    ${pc:04X}  {text:<20} ; {comment}")

    if show_asm:
        # A listing meant to be read: code where the walk reached it, .byte runs
        # everywhere else, addresses in a comment on every line.
        print(f"; {path}")
        print(f";   load ${load:04X}  end ${end:04X}")
        print(f";   entry {', '.join(f'${a:04X}' for a in starts)}")
        print(';')
        print('; Mechanical disassembly from tools/dis6502.py --asm. Instructions are the')
        print('; code the walk reached from the entry points and from handlers installed')
        print('; through the KERNAL/hardware vectors; every other byte is emitted as .byte,')
        print('; so code entered only through an indirect jump appears as data. The address')
        print('; and raw bytes are in the comment on each line.')
        print(';')
        print(f'; hardware touches ({len(hardware)}):')
        for pc, text, comment in sorted(hardware):
            print(f';   ${pc:04X}  {text.lower():<20} {comment}')
        print()
        pc = load
        while pc <= end:
            if pc in visited:
                off = off_of(pc)
                n = visited[pc]
                raw = ' '.join(f"{x:02X}" for x in b[off:off + n])
                text, _, mode, w = decode(b, pc, off)
                comment = ADDR.get(w, '') if mode in (ABS, ABX, ABY) else ''
                line = f"        {text.lower():<20} // ${pc:04X}  {raw}"
                print(f"{line}  {comment}" if comment else line)
                pc += n
            else:
                run_end = pc
                while run_end + 1 <= end and (run_end + 1) not in visited:
                    run_end += 1
                print(f"        // data ${pc:04X}-${run_end:04X}"
                      f" ({run_end - pc + 1} bytes)")
                a = pc
                while a <= run_end:
                    chunk = b[off_of(a):off_of(a) + min(16, run_end - a + 1)]
                    print(f"        .byte "
                          f"{','.join(f'${x:02X}' for x in chunk)}")
                    a += len(chunk)
                pc = run_end + 1
        return 0

    if show_all:
        print("\n  listing:")
        for pc in sorted(visited):
            off = off_of(pc)
            n = visited[pc]
            raw = ' '.join(f"{x:02X}" for x in b[off:off + n])
            text, _, mode, w = decode(b, pc, off)
            comment = ADDR.get(w, '') if mode in (ABS, ABX, ABY) else ''
            print(f"    ${pc:04X}  {raw:<9} {text:<20} {comment}")
    else:
        print("\n  (pass --all for the full listing, --asm for a listing to keep)")
    return 0


if __name__ == '__main__':
    sys.exit(main())
