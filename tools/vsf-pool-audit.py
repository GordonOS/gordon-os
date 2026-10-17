#!/usr/bin/env python3
"""vsf-pool-audit.py -- read the pool's state out of a VICE save-state.

Why this exists: `pool` answers "who owns what" from the TABLES, so it cannot
see a placement fault at all -- a correct-looking total beside a failing load
means the free pages are not in one run, and the BITMAP is the only witness.
Worse, a page can be freed while a table still claims it (STOLEN: the next
allocation hands out someone's code -- the JAM / KERNAL READY crash that follows a
wrong page count) or marked used with no owner at all (STRANDED: pool space
lost until reboot). Neither appears in the tables.

This reads the pool bitmap and the owner tables -- dynamic tasks and their
program RAM, libraries, malloc blocks -- out of a snapshot's C64 RAM and
cross-checks them:

    STOLEN    a live owner's range that the bitmap says is free
    STRANDED  a page the bitmap says is used that no live owner claims

Two cases are deliberately excluded, or every ordinary swap-out would read as
a fault: a task in TA_SWAPPED/TA_DEAD holds no pages (its code went back to the
pool), and a malloc block with `mallocSwapped` set had its pages freed on
swap-out (kFreeImpl: "pages already freed (swapped) -- just clear").

Snapshot format (VICE .vsf, derived by reading the files -- Rule 9): the module
directory starts at the ASCII name `MAINCPU`; each entry is a 16-byte NUL-padded
name, a 2-byte version and a u32 LE size INCLUDING the 22-byte header. C64 RAM
is the `C64MEM` body at a small fixed offset, located here by comparing the
image at $0801 against the built PRG (`--ram-offset` overrides).

Usage:
    python tools/vsf-pool-audit.py SNAPSHOT.vsf
    python tools/vsf-pool-audit.py SNAPSHOT.vsf --list        # module directory
    python tools/vsf-pool-audit.py BEFORE.vsf --diff AFTER.vsf
"""

import pathlib
import re
import struct
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
RAM_SIZE = 0x10000
CONST = re.compile(r"^\.const\s+([A-Za-z_][A-Za-z0-9_]*)\s*=\s*(\$[0-9a-fA-F]+|\d+)")
LABEL = re.compile(r"^\.label\s+(\S+)=\$([0-9a-fA-F]+)")


# ---------------------------------------------------------------- repo facts
def load_constants():
    """Every `.const NAME = value` in the kernel sources, plus the derived pool geometry."""
    out = {}
    for path in sorted((ROOT / "src" / "kernel").glob("*")):
        if path.suffix not in (".inc", ".asm"):
            continue
        for line in path.read_text(errors="replace").splitlines():
            m = CONST.match(line)
            if not m:
                continue
            raw = m.group(2)
            val = int(raw[1:], 16) if raw.startswith("$") else int(raw)
            out.setdefault(m.group(1), val)
    out["POOL_PAGES"] = out["POOL_SIZE"] // out["POOL_PAGE_SIZE"]   # computed in pool.inc
    out["POOL_BITMAP_BYTES"] = (out["POOL_PAGES"] + 7) >> 3         # computed in pool.inc
    return out


def load_symbols():
    sym = {}
    for line in (ROOT / "build" / "main.sym").read_text().splitlines():
        m = LABEL.match(line)
        if m:
            sym[m.group(1)] = int(m.group(2), 16)
    return sym


def lib_names(C):
    return {C[k]: k.split("_", 1)[1].lower()
            for k in C if k.startswith("LIB_") and k != "LIB_COUNT"}


# ------------------------------------------------------------------ the file
def parse_vsf(path):
    """(raw bytes, {module name: (body offset, body length)})."""
    data = pathlib.Path(path).read_bytes()
    start = data.find(b"MAINCPU")
    if start < 0:
        sys.exit("%s: no MAINCPU module -- is this a VICE save-state? (try --list)" % path)
    mods = {}
    off = start
    while off + 22 <= len(data):
        name = data[off:off + 16].split(b"\x00")[0].decode("ascii", "replace")
        if not name or not name.isprintable():
            break
        size = struct.unpack("<I", data[off + 18:off + 22])[0]
        if size < 22 or off + size > len(data):
            break
        mods[name] = (off + 22, size - 22)
        off += size
    return data, mods


def locate_ram(data, mods, override=None):
    """C64 RAM inside the C64MEM body, validated against the built PRG at $0801."""
    if "C64MEM" not in mods:
        sys.exit("no C64MEM module in this snapshot")
    base, length = mods["C64MEM"]
    body = data[base:base + length]
    image = (ROOT / "build" / "gordon-os.prg").read_bytes()[2:]
    probe = image[0:16]
    candidates = [override] if override is not None else [4, 0, 8, 2, 6]
    for cand in candidates:
        if cand is None or cand + RAM_SIZE > len(body):
            continue
        if body[cand + 0x0801:cand + 0x0801 + len(probe)] == probe:
            return body[cand:cand + RAM_SIZE], cand, True
    # fall back to the pool-bitmap signature (boot: pages 0-18 used, the top 12 used)
    found = None
    for pat in (bytes([0xFF, 0xFF, 0x07, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0xFE, 0x1F]),
                bytes([0xFF, 0xFF, 0x0F, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0xFE, 0x1F])):
        i = body.find(pat)
        if i >= 0:
            found = i - SYM["poolBitmap"]      # the match IS poolBitmap
            break
    if found is None or found < 0 or found + RAM_SIZE > len(body):
        sys.exit("could not locate C64 RAM in the snapshot -- pass --ram-offset")
    print("WARNING: $0801 does not match build/gordon-os.prg -- a stale build or a"
          " foreign kernel; RAM located by the pool-bitmap signature instead")
    return body[found:found + RAM_SIZE], found, False


# ------------------------------------------------------------------- reading
def bit(ram, page):
    a = SYM["poolBitmap"] + (page >> 3)
    return (ram[a] >> (page & 7)) & 1


def u16(ram, a):
    return ram[a] | (ram[a + 1] << 8)


def runs_of(pages):
    out = []
    for p in pages:
        if out and out[-1][1] == p - 1:
            out[-1] = (out[-1][0], p)
        else:
            out.append((p, p))
    return out


def dyn_name(ram, i):
    p = ram[SYM["dynTaskNameLo"] + i] | (ram[SYM["dynTaskNameHi"] + i] << 8)
    if not (0x0400 <= p <= 0xFF00):
        return "-"
    text = "".join(chr(ram[p + k]) if 32 <= ram[p + k] < 127 else "\x00" for k in range(10))
    return text.split("\x00")[0] or "-"


def print_scalars(ram):
    print("-- scalars --")
    for name in ("curTask", "totalTasks", "kbFocus", "dynTaskCount", "mallocCount",
                 "dataHeap", "dataHeapEnd", "sprPubMask", "frameCount"):
        if name not in SYM:
            continue
        a = SYM[name]
        val = u16(ram, a) if name in ("dataHeap", "dataHeapEnd") else ram[a]
        print("   %-14s $%04X = %04X (%d)" % (name, a, val, val))


def print_tasks(ram, C):
    print("-- task slots --")
    f = ("taskState", "taskParent", "taskDataSize", "taskDataPtrLo", "taskDataPtrHi",
         "taskLo", "taskHi", "taskZpBase", "taskZpLen", "screenAlloc", "screenOwner", "savedSP")
    base = {n: SYM[n] for n in f}
    for i in range(C["MAX_TASKS"]):
        st = ram[base["taskState"] + i]
        if st == C["TA_DEAD"] and i > 3:
            continue
        print("   slot %2d state=%X parent=%02X size=%3d dataPtr=%04X task=%04X zp=%02X/%02X"
              " scr=%02X/%02X sp=%02X"
              % (i, st, ram[base["taskParent"] + i], ram[base["taskDataSize"] + i],
                 ram[base["taskDataPtrLo"] + i] | (ram[base["taskDataPtrHi"] + i] << 8),
                 ram[base["taskLo"] + i] | (ram[base["taskHi"] + i] << 8),
                 ram[base["taskZpBase"] + i], ram[base["taskZpLen"] + i],
                 ram[base["screenAlloc"] + i], ram[base["screenOwner"] + i],
                 ram[base["savedSP"] + i]))


def print_tables(ram, C, tag=""):
    """Print the owner tables and return {page: tag} for every LIVE owner."""
    own = {}
    pre = (tag + " ") if tag else ""

    def claim(first, count, tag):
        for p in range(first, min(first + count, C["POOL_PAGES"])):
            own.setdefault(p, tag)

    print("-- %sdynamic tasks --" % pre)
    for i in range(C["MAX_DYN_TASKS"]):
        tid = ram[SYM["dynTaskId"] + i]
        if tid == 0xFF:
            continue
        state = ram[SYM["taskState"] + tid] if tid < C["MAX_TASKS"] else C["TA_DEAD"]
        name = dyn_name(ram, i)
        start = ram[SYM["dynTaskStartPage"] + i]
        pages = ram[SYM["dynTaskPageCount"] + i]
        rs = ram[SYM["dynTaskRamStartPage"] + i]
        rp = ram[SYM["dynTaskRamPages"] + i]
        live = state not in (C["TA_DEAD"], C["TA_SWAPPED"])
        print("   dyn %d id=%02X state=%X start=%3d pages=%2d flags=%02X res=%02X ram=%d/%d"
              " name=%s%s"
              % (i, tid, state, start, pages, ram[SYM["dynTaskFlags"] + i],
                 ram[SYM["dynTaskResidency"] + i], rs, rp, name,
                 "" if live else "   (not resident -- holds no pages)"))
        if live:
            claim(start, pages, "dyn%d %s code" % (i, name))
            if rp:
                claim(rs, rp, "dyn%d %s ram" % (i, name))

    print("-- %slibraries --" % pre)
    names = lib_names(C)
    for i in range(C["LIB_COUNT"]):
        start = ram[SYM["libStartPage"] + i]
        pages = ram[SYM["libPages"] + i]
        print("   lib %d %-8s start=%3d pages=%2d ref=%d"
              % (i, names.get(i, "lib%d" % i), start, pages, ram[SYM["libRefCount"] + i]))
        if start != 0xFF and pages:
            claim(start, pages, "lib %s" % names.get(i, i))

    print("-- %smalloc blocks --" % pre)
    for i in range(C["MAX_MALLOC_BLOCKS"]):
        owner = ram[SYM["mallocOwner"] + i]
        if owner == 0xFF:
            continue
        start = ram[SYM["mallocStartPage"] + i]
        pages = ram[SYM["mallocPages"] + i]
        swapped = ram[SYM["mallocSwapped"] + i]
        print("   blk %d task=%02X start=%3d pages=%2d swapped=%d%s"
              % (i, owner, start, pages, swapped,
                 "   (pages already freed)" if swapped else ""))
        if not swapped:
            claim(start, pages, "blk%d task %02X" % (i, owner))
    return own


def print_pool(ram, C, own, tag="pool"):
    pages = C["POOL_PAGES"]
    used = [p for p in range(pages) if bit(ram, p)]
    free = [p for p in range(pages) if not bit(ram, p)]
    runs = runs_of(free)
    print("-- %s --" % tag)
    print("   bitmap: " + " ".join("%02X" % ram[SYM["poolBitmap"] + k]
                                   for k in range(C["POOL_BITMAP_BYTES"])))
    print("   %d used / %d free of %d; %d free run(s), largest %d"
          % (len(used), len(free), pages, len(runs), max((b - a + 1 for a, b in runs), default=0)))
    print("   free: " + "  ".join("%d-%d" % r if r[0] != r[1] else "%d" % r[0] for r in runs))
    stolen = sorted(p for p in own if not bit(ram, p))
    stranded = [p for p in used if p not in own]
    if stolen:
        print("   STOLEN -- marked free while a live table still claims it:")
        for p, tag in sorted(((p, own[p]) for p in stolen), key=lambda x: (x[1], x[0])):
            print("      page %3d  %s" % (p, tag))
    if stranded:
        print("   STRANDED -- marked used with no live owner:")
        for a, b in runs_of(stranded):
            print("      pages %d-%d (%d)" % (a, b, b - a + 1) if a != b
                  else "      page %d" % a)
    if not stolen and not stranded:
        print("   consistent: every used page has a live owner and every owner is marked used")
    return {"used": used, "free": free, "runs": runs, "stolen": stolen, "stranded": stranded}


def report(path, override, sym):
    global SYM
    SYM = sym
    C = load_constants()
    data, mods = parse_vsf(path)
    ram, off, verified = locate_ram(data, mods, override)
    print("=" * 78)
    print(pathlib.Path(path).name)
    print("   %d modules; C64 RAM at C64MEM body +%d (%s)"
          % (len(mods), off, "verified against the PRG" if verified else "unverified"))
    print_scalars(ram)
    print_tasks(ram, C)
    own = print_tables(ram, C)
    return print_pool(ram, C, own)


def report_diff(path_a, path_b, override, sym):
    global SYM
    SYM = sym
    C = load_constants()
    da, ma = parse_vsf(path_a)
    db, mb = parse_vsf(path_b)
    ra, _, _ = locate_ram(da, ma, override)
    rb, _, _ = locate_ram(db, mb, override)
    print("=" * 78)
    print("%s  ->  %s" % (pathlib.Path(path_a).name, pathlib.Path(path_b).name))
    differing = [i for i in range(RAM_SIZE) if ra[i] != rb[i]]
    print("\n-- C64 RAM differences: %d byte(s) --" % len(differing))
    for a, b in runs_of(differing)[:40]:
        print("   $%04X-$%04X (%d)" % (a, b, b - a + 1))
        if b - a + 1 <= 16:
            print("      before: " + " ".join("%02X" % x for x in ra[a:b + 1]))
            print("      after : " + " ".join("%02X" % x for x in rb[a:b + 1]))
    if len(runs_of(differing)) > 40:
        print("   ... %d more run(s)" % (len(runs_of(differing)) - 40))

    print("\n-- scalars --")
    for name in ("curTask", "totalTasks", "kbFocus", "dynTaskCount", "mallocCount",
                 "dataHeap", "dataHeapEnd", "sprPubMask"):
        if name not in SYM:
            continue
        a = SYM[name]
        before = u16(ra, a) if name in ("dataHeap", "dataHeapEnd") else ra[a]
        after = u16(rb, a) if name in ("dataHeap", "dataHeapEnd") else rb[a]
        if before != after:
            print("   %-14s %04X -> %04X" % (name, before, after))

    own_a = print_tables(ra, C, "BEFORE")
    own_b = print_tables(rb, C, "AFTER")
    print("\n-- owner table changes --")
    changed = False
    for page in sorted(set(own_a) | set(own_b)):
        if own_a.get(page) != own_b.get(page):
            print("   page %3d  %s -> %s" % (page, own_a.get(page, "(unowned)"),
                                             own_b.get(page, "(unowned)")))
            changed = True
    if not changed:
        print("   none")
    print("")
    print_pool(ra, C, own_a, "BEFORE pool")
    print("")
    return print_pool(rb, C, own_b, "AFTER pool")


# ---------------------------------------------------------------------- main
def main():
    argv = sys.argv[1:]
    paths = [a for a in argv if not a.startswith("--")]
    flags = {a for a in argv if a.startswith("--")}
    override = None
    for a in argv:
        if a.startswith("--ram-offset="):
            override = int(a.split("=", 1)[1], 0)
    if not paths:
        sys.exit("usage: vsf-pool-audit.py SNAPSHOT.vsf [--list] [--diff OTHER.vsf]")
    if "--list" in flags:
        data, mods = parse_vsf(paths[0])
        print("%s:" % paths[0])
        for name, (_off, length) in mods.items():
            print("   %-14s body %8d bytes" % (name, length))
        return 0
    if "--diff" in flags:
        if len(paths) < 2:
            sys.exit("--diff needs a second snapshot: BEFORE.vsf --diff AFTER.vsf")
        report_diff(paths[0], paths[1], override, load_symbols())
        return 0
    report(paths[0], override, load_symbols())
    return 0


if __name__ == "__main__":
    sys.exit(main())
