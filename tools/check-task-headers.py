#!/usr/bin/env python3
"""check-task-headers.py -- validate the built task/library headers, and report them.

The loader validates a header only when it loads it, and only as far as the load
needs. This is the build-time counterpart: it re-derives every field of every
binary in tasks-out/ straight from the bytes and checks the invariants the ABI
promises, so a header-layout change fails in the build rather than on the C64.

It exists because a layout change is a whole-tree event -- it moves fields
across the generator, the wrappers and the kernel at once -- and the failure
mode is silent: a reader that uses a stale offset loads a plausible but wrong
number.

Checks (any failure exits 1):
  * magic is HDR_MAGIC, not the byte-swapped literal
  * size == 256 + code_size + 2*fixup_count + 2
  * the trailer's fixup_offset word == 256 + code_size
  * the header page is zero from the end of the trailer to byte 255
  * every fixup offset is < code_size and the table is strictly increasing
  * the table is terminated by the $FFFF sentinel
  * origin is page-aligned

Usage:
  python tools/check-task-headers.py            # validate + report
  python tools/check-task-headers.py --quiet    # exit code only
  python tools/check-task-headers.py --dir DIR  # validate another tree
"""

import argparse
import pathlib
import re
import struct
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
HDR_INC = ROOT / "src/kernel/task-header.inc"
OUT_DIR = ROOT / "tasks-out"

PAGE = 256
SENTINEL = 0xFFFF


def parse_consts(path):
    """Pull the numeric .consts out of task-header.inc, so the tool has no
    second copy of the layout: the header file stays the single source."""
    text = path.read_text(encoding="ascii")
    consts = {}
    for m in re.finditer(r"\.const\s+([A-Z0-9_]+)\s*=\s*(\$[0-9A-Fa-f]+|\d+)", text):
        raw = m.group(2)
        consts[m.group(1)] = int(raw[1:], 16) if raw.startswith("$") else int(raw)
    return consts


def screen_code(c):
    """Header names are screencode_mixed, not PETSCII or ASCII: $01-$1A are the
    lower-case letters and only $20-$3F coincide with ASCII. Decoding them as
    latin-1 yields control characters, which read as blank names."""
    if 1 <= c <= 26:
        return chr(ord("a") + c - 1)
    if 0x20 <= c <= 0x3F:
        return chr(c)
    return "?"


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--quiet", action="store_true", help="print nothing on success")
    ap.add_argument("--dir", default=str(OUT_DIR),
                    help="directory of .bin files to validate (default: tasks-out)")
    args = ap.parse_args()

    c = parse_consts(HDR_INC)
    magic = c["HDR_MAGIC"]
    off_flags = c["HDR_FLAGS"]
    off_origin = c["HDR_ORIGIN"]
    off_code = c["HDR_CODE_SIZE"]
    off_data = c["HDR_DATA_SIZE"]
    off_zpb = c["HDR_ZP_BASE"]
    off_zpl = c["HDR_ZP_LEN"]
    off_libmask = c["HDR_LIBMASK"]
    off_namelen = c["HDR_NAME_LEN"]
    name_start = off_namelen + 1
    KF = {v: k[3:] for k, v in c.items() if k.startswith("KF_")}
    LIB = {v: k[4:] for k, v in c.items() if k.startswith("LIB_") and v < 16}

    bins = sorted(pathlib.Path(args.dir).glob("*.bin"))
    if not bins:
        print(f"no binaries in {args.dir} -- build the tasks first", file=sys.stderr)
        return 1

    problems = []
    rows = []
    for path in bins:
        b = path.read_bytes()
        if len(b) < PAGE:
            problems.append(f"{path.name}: only {len(b)} bytes, shorter than a header page")
            continue

        got_magic = struct.unpack_from("<H", b, 0)[0]
        flags = b[off_flags]
        origin = struct.unpack_from("<H", b, off_origin)[0]
        code = struct.unpack_from("<H", b, off_code)[0]
        data = b[off_data]
        zpb, zpl = b[off_zpb], b[off_zpl]
        libmask = struct.unpack_from("<H", b, off_libmask)[0]
        namelen = b[off_namelen]

        def bad(msg):
            problems.append(f"{path.name}: {msg}")

        if got_magic != magic:
            bad(f"magic is ${got_magic:04X}, expected ${magic:04X} "
                f"(a literal .word ${magic:04X} emits the bytes swapped)")
            continue

        name = "".join(screen_code(c_) for c_ in b[name_start:name_start + namelen])
        trailer = name_start + namelen
        fixcount = struct.unpack_from("<H", b, trailer)[0]
        fixoff = struct.unpack_from("<H", b, trailer + 2)[0]

        if origin % PAGE:
            bad(f"origin ${origin:04X} is not page-aligned")
        if namelen == 0:
            bad("name length is 0")
        if namelen > 11:
            bad(f"name '{name}' is {namelen} chars; the ABI caps names at 11")
        if (name_start + namelen + 4) > PAGE:
            bad("name + fixup trailer does not fit the header page")
        if any(b[trailer + 4:PAGE]):
            bad("header page is not zero-padded after the fixup trailer")
        if fixoff != PAGE + code:
            bad(f"fixup_offset {fixoff} != 256 + code_size ({PAGE + code})")
        if len(b) != PAGE + code + 2 * fixcount + 2:
            bad(f"size {len(b)} != 256 + code({code}) + 2*{fixcount} + 2 "
                f"({PAGE + code + 2 * fixcount + 2})")
            continue

        table = PAGE + code
        prev = -1
        for i in range(fixcount):
            (o,) = struct.unpack_from("<H", b, table + 2 * i)
            if o >= code:
                bad(f"fixup {i} offset {o} is outside the {code}-byte code body")
                break
            if o <= prev:
                bad(f"fixup {i} offset {o} is not above the previous {prev}")
                break
            prev = o
        (sentinel,) = struct.unpack_from("<H", b, table + 2 * fixcount)
        if sentinel != SENTINEL:
            bad(f"table sentinel is ${sentinel:04X}, expected ${SENTINEL:04X}")

        rows.append(dict(name=name, file=path.name, flags=flags, origin=origin,
                         code=code, data=data, zp=(zpb, zpl), libmask=libmask,
                         fixups=fixcount, size=len(b), pages=-(-code // PAGE)))

    if not args.quiet:
        print(f"{'name':<10} {'file':<15} {'flags':>5} {'origin':>7} {'code':>6} "
              f"{'data':>4} {'zp':>7} {'pages':>5} {'fixups':>6}  libMask")
        for r in sorted(rows, key=lambda r: r["name"]):
            libs = ",".join(LIB[i] for i in range(16) if r["libmask"] & (1 << i)) or "-"
            zp = f"{r['zp'][0]}/{r['zp'][1]}"
            print(f"{r['name']:<10} {r['file']:<15} ${r['flags']:>4X} ${r['origin']:>6X} "
                  f"{r['code']:>6} {r['data']:>4} {zp:>7} {r['pages']:>5} "
                  f"{r['fixups']:>6}  {libs}")
        total = sum(r["pages"] for r in rows)
        print(f"\n{len(rows)} binaries, {total} code pages total")

    if problems:
        print("", file=sys.stderr)
        for p in problems:
            print(f"FAIL {p}", file=sys.stderr)
        print(f"\n{len(problems)} header problem(s)", file=sys.stderr)
        return 1

    if not args.quiet:
        print(f"headers ok: {len(rows)} binaries validated against "
              f"{HDR_INC.relative_to(ROOT).as_posix()}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
