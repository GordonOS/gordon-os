#!/usr/bin/env python3
"""audit-clobbers.py -- check the kernel's documented register contracts.

A routine's header is a contract: `Clobbers: A, Y` or "X preserved" tells every
caller what survives the call. KickAssembler cannot check that -- it sees no
registers -- so a header that lies is worse than no header at all: the caller
trusts it and keeps a live value in a register that is already gone. That is the
shape of the `kMalloc` page-count fault: `allocFromPool` promised X and clobbered it (its
`poolBitCheck`/`poolBitSet` helpers end in `tax`), so `kMallocImpl` recorded a
garbage `mallocPages`, which either stranded pages in the bitmap or freed the
pages above the block, i.e. another owner's, which the next allocation then
handed out as code.

This walks the EMITTED kernel and, for every preservation claim it can recognise
in `src/kernel/**` (X, Y and A), asks whether any path out of the routine leaves
that register clobbered:

  * a register writer makes the path dirty -- `tax`/`tsx`/`ldx`/`inx`/`dex` for X,
    `tay`/`ldy`/`iny`/`dey` for Y, and the accumulator reads for A (`txa` is NOT
    an X writer -- it copies X into A);
  * the save/restore idioms clean it again -- `stx addr` ... `ldx addr` (resp.
    `sty`/`ldy`), `pha` ... `pla` for A, and the `txa/pha` ... `pla/tax`
    (resp. `tay`) pairs -- per path, so a restore on only one exit does not count;
  * a `jsr` to a routine that clobbers the register makes it dirty (resolved
    recursively, following `$E000` jump-table entries);
  * SUSPECT when some exit (`rts`/`rti`/`brk`, or an indirect `jmp`) is reached
    dirty, and OK when no path reaches an exit dirty.

`--callers` turns the same analysis around and looks at the other half of the
contract: every `jsr` to a routine that clobbers X or Y, where the next access to
that register in the instructions that follow is a READ rather than a reload -- a
caller that kept a live value in a register the callee does not preserve. It scans
the addresses reachable from the symbol table, never the image linearly (a data
block would desynchronise the decode into phantom calls), and annotates each hit
with the callee's own header line naming that register, because the common false
positive is a register the callee *returns* (`kGetCursor` gives row in Y,
`kByteToDec` gives the length in Y, `scanKeyboardHw` gives the key index in X).
A is left out of that mode: nearly every instruction reads A, so no window test
can tell a carried value from a scratch one.

It judges CLAIMS, not code: a routine that makes no claim is not audited, an
unrecognised tail call is invisible, and only the phrasings it knows are matched.
Treat SUSPECT and the caller list as reading lists -- every hit so far has repaid
the read -- but confirm each by hand. A header can also be wrong in the safe
direction: `fontGlyph` lists Y among its clobbers and never writes it, which costs
a caller a needless reload and nothing else.

Usage:
    python tools/audit-clobbers.py              # audit build/gordon-os.prg
    python tools/audit-clobbers.py --all        # also list the claims that hold
    python tools/audit-clobbers.py --callers    # flag X/Y reads after a call
    python tools/audit-clobbers.py --probe NAME # one routine's per-register verdict
    python tools/audit-clobbers.py --selftest   # re-check the analysis itself
"""

import pathlib
import re
import sys

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
from dis6502 import LEN, OPS  # noqa: E402

ROOT = pathlib.Path(__file__).resolve().parent.parent
TERMINATORS = {"RTS", "RTI", "BRK"}
CLAIM_LINE = re.compile(r"^\s*(//|;)\s*(.*)$")
PRESERVE = re.compile(r"preserv(?:e|es|ed)|surviv(?:e|es|ed)|unchanged"
                      r"|not (clobber|touched|disturbed)", re.I)
LABEL = re.compile(r"^([A-Za-z_][A-Za-z0-9_]*):")

# Per-register model. The register token itself is matched CASE-SENSITIVELY (a
# case-insensitive \bA\b would match the English "a"), so the preserve words sit
# in a scoped case-insensitive group instead. Verb forms only -- "a surviving
# sharer" is prose, not a promise, and a gerund match made every sentence that
# began with "A ..." look like an A claim.
_PW = r"(?i:preserv(?:e|es|ed)|surviv(?:e|es|ed)|unchanged)"


def _claim_re(reg):
    r = re.escape(reg)
    peers = "X|Y"
    return re.compile(
        r"(?-i:\b%s\b)\s*(?:and\s+(?-i:%s)\s*)?(?:is\s+|are\s+)?%s"
        r"|(?i:preserv(?:e|es|ed))[:\s]+(?-i:\b%s\b)"
        r"|(?-i:\b%s\b)[^\n]{0,4}(?i:not\s+(?:clobber|touch|disturb))"
        % (r, peers, _PW, r, r))


def _veto_re(reg):
    return re.compile(r"Clobbers:?[^.;]*\b%s\b" % re.escape(reg))


def _input_re(reg):
    return re.compile(r"^\s*%s\s*=" % re.escape(reg))


REG_ORDER = ("X", "Y", "A")
CLAIM_RE = {r: _claim_re(r) for r in REG_ORDER}
VETO_RE = {r: _veto_re(r) for r in REG_ORDER}
INPUT_RE = {r: _input_re(r) for r in REG_ORDER}

REGS = {
    "X": dict(writers={"TAX", "TSX", "LDX", "INX", "DEX"}, load="LDX", store="STX",
              pop_tail="TAX",
              read_ops={"STX", "CPX", "TXA"}, read_modes={"zpx", "abx", "izx"}),
    "Y": dict(writers={"TAY", "LDY", "INY", "DEY"}, load="LDY", store="STY",
              pop_tail="TAY",
              read_ops={"STY", "CPY", "TYA"}, read_modes={"zpy", "aby", "izy"}),
    "A": dict(writers={"LDA", "ADC", "SBC", "AND", "ORA", "EOR", "ASL", "LSR", "ROL",
                       "ROR", "TXA", "TYA"}, load="PLA", store="PHA", pop_tail=None,
              read_ops=set(), read_modes=set()),
}

# Routines whose register behaviour is settled by hand. If the self-test stops
# agreeing with these, the ANALYSIS is wrong, not the kernel.
SELFTEST = {
    "X": (("allocFromPool", "freeFromPool", "markPagesUsed", "dlAllocCode",
           "setTextColor", "setCursorColor", "spriteCollide"),
          ("surfSlotToReu", "shareScreen", "takeScreenOwnership", "wrapClamp",
           "skOwnSurf", "blitChar", "blitCharSlot", "blitGlyphSlot",
           "blitGlyphFb", "dynRegister")),
    "Y": ((), ("charAt", "reuToC64", "reuFromC64", "reuFillReu", "fontGlyph")),
    "A": ((), ()),
}


def load_symbols():
    sym = {}
    for line in (ROOT / "build" / "main.sym").read_text().splitlines():
        m = re.match(r"^\.label\s+(\S+)=\$([0-9a-fA-F]+)", line)
        if m:
            sym[m.group(1)] = int(m.group(2), 16)
    return sym


SYM = load_symbols()
BY_ADDR = {}
for _n, _a in SYM.items():
    BY_ADDR.setdefault(_a, _n)

_prg = (ROOT / "build" / "gordon-os.prg").read_bytes()
IMG = _prg[2:]
BASE = 0x0801


def byte(addr):
    off = addr - BASE
    return IMG[off] if 0 <= off < len(IMG) else None


def decode(addr):
    """(name, mode, length, operand) or None."""
    op = byte(addr)
    if op is None or op not in OPS:
        return None
    name, mode = OPS[op]
    length = LEN[mode]
    arg = None
    if length == 2:
        arg = byte(addr + 1)
    elif length == 3:
        arg = byte(addr + 1) | (byte(addr + 2) << 8)
    return (name, mode, length, arg)


def entry_target(addr):
    """Follow a `$E000` jump-table entry to the routine it jumps to."""
    if addr is None:
        return None
    if addr in BY_ADDR:
        return addr
    if 0xE000 <= addr < 0xE120:
        j = decode(addr)
        if j and j[0] == "JMP" and j[1] == "abs":
            return j[3]
    return addr if byte(addr) is not None else None


_memo = {}
_busy = set()
_CODE = set()


def dirty_at_exit(entry, reg, depth=0):
    """True when some exit of the routine at `entry` is reached with `reg` clobbered."""
    key = (reg, entry)
    if key in _memo:
        return _memo[key]
    if key in _busy or depth > 40:
        return True                     # recursion: assume the worst
    _busy.add(key)
    bad, where = _walk(entry, reg, depth)
    _busy.discard(key)
    _memo[key] = bad
    _memo[("where", key)] = where
    return bad


def _walk(entry, reg, depth):
    spec = REGS[reg]
    stack = [(entry, False, frozenset())]
    seen = set()
    while stack:
        addr, dirty, saves = stack.pop()
        state = (addr, dirty, saves)
        if state in seen:
            continue
        seen.add(state)
        if len(seen) > 4000:
            return True, "runaway walk"
        ins = decode(addr)
        if ins is None:
            if dirty:
                return True, "unclassifiable byte at $%04X" % addr
            continue
        name, mode, length, arg = ins
        nxt = decode(addr + length)

        # a pop restores the register: `pha`...`pla` for A, and `txa`/`tya`+`pha`
        # ... `pla`+`tax`/`tay` for X/Y
        if name == "PLA":
            if spec["pop_tail"] is None:
                dirty = False
            elif nxt and nxt[0] == spec["pop_tail"] and nxt[1] == "imp":
                stack.append((addr + length + nxt[2], False, saves))
                continue
        elif name in spec["writers"]:
            if name == spec["load"] and mode in ("zp", "abs") and arg in saves:
                dirty = False                    # restore of a saved value
            else:
                dirty = True
        if name == spec["store"] and mode in ("zp", "abs"):
            saves = saves | {arg}
        if name in TERMINATORS:
            if dirty:
                return True, "exit at $%04X" % addr
            continue
        if name == "JSR":
            tgt = entry_target(arg)
            if tgt is None or dirty_at_exit(tgt, reg, depth + 1):
                dirty = True
            stack.append((addr + length, dirty, saves))
            continue
        if name == "JMP":
            tgt = entry_target(arg) if mode == "abs" else None
            if tgt is not None and byte(tgt) is not None:
                stack.append((tgt, dirty, saves))
                continue
            if dirty:
                return True, "indirect jmp at $%04X" % addr
            continue
        if name.startswith("B") and mode == "rel":
            off = arg - 256 if arg is not None and arg > 127 else (arg or 0)
            stack.append((addr + length, dirty, saves))
            stack.append((addr + length + off, dirty, saves))
            continue
        stack.append((addr + length, dirty, saves))
    return False, None


_SORTED = sorted((a, n) for n, a in SYM.items())


def owner_label(addr):
    """The last symbol at or below `addr` -- the routine a site sits in."""
    lo, hi, best = 0, len(_SORTED) - 1, "?"
    while lo <= hi:
        mid = (lo + hi) // 2
        if _SORTED[mid][0] <= addr:
            best = _SORTED[mid][1]
            lo = mid + 1
        else:
            hi = mid - 1
    return best


def is_code(line):
    """True for an emitted instruction -- what separates a header from a note."""
    s = line.strip()
    return bool(s) and not s.startswith(("//", ";", "."))


def claims_for(reg):
    """[(file, line, label, text)] for one register's preservation claims."""
    out = []
    for path in sorted((ROOT / "src" / "kernel").glob("*")):
        if path.suffix not in (".inc", ".asm"):
            continue
        lines = path.read_text(errors="replace").splitlines()
        for i, line in enumerate(lines):
            m = CLAIM_LINE.match(line)
            if not m:
                continue
            text = m.group(2)
            if INPUT_RE[reg].match(text) or "whatever" in text.lower():
                continue      # an input description, or an honest "inherits its callees"
            if (not PRESERVE.search(text) or not CLAIM_RE[reg].search(text)
                    or VETO_RE[reg].search(text)):
                continue      # no promise, or a clobber list that names the register
            for j in range(i + 1, min(i + 31, len(lines))):
                lm = LABEL.match(lines[j])
                if lm:
                    out.append((path.name, i + 1, lm.group(1), text.strip()))
                    break
                if is_code(lines[j]):
                    break     # code in between: a call-site note, not a header
    return out


def verdict(label, reg):
    return dirty_at_exit(SYM[label], reg)


def audit(reg, show_all):
    claims = claims_for(reg)
    suspects, clean = [], []
    for path, line, label, text in claims:
        if label not in SYM:
            continue
        (suspects if verdict(label, reg) else clean).append((label, path, line, text))
    print("%s-preservation claims audited: %d" % (reg, len(claims)))
    print("   SUSPECT: %d     OK: %d" % (len(suspects), len(clean)))
    for label, path, line, text in suspects:
        print("   SUSPECT %-20s %s:%d   (%s)"
              % (label, path, line, _memo.get(("where", (reg, SYM[label])))))
        print("           claim: %s" % text[:110])
    if show_all:
        for label, path, line, _text in clean:
            print("   ok      %-20s %s:%d" % (label, path, line))
    return len(suspects)


def map_code():
    """Every instruction boundary reachable from a symbol -- real code only."""
    for name in SYM:
        _map_walk(SYM[name])
    return len(_CODE)


def _map_walk(entry):
    todo = [entry]
    while todo:
        addr = todo.pop()
        if addr in _CODE:
            continue
        ins = decode(addr)
        if ins is None:
            continue
        _CODE.add(addr)
        name, mode, length, arg = ins
        if name in TERMINATORS:
            continue
        if name == "JMP":
            tgt = entry_target(arg) if mode == "abs" else None
            if tgt is not None and byte(tgt) is not None:
                todo.append(tgt)
            continue
        if name == "JSR":
            tgt = entry_target(arg)
            if tgt is not None:
                todo.append(tgt)
            todo.append(addr + length)
            continue
        if name.startswith("B") and mode == "rel":
            off = arg - 256 if arg is not None and arg > 127 else (arg or 0)
            todo.append(addr + length)
            todo.append(addr + length + off)
            continue
        todo.append(addr + length)


_LABEL_DOC = None
_COMMENTS = None


def label_docs():
    """{label: [(file, line, text)]} -- the comment block above each source label."""
    global _LABEL_DOC
    if _LABEL_DOC is not None:
        return _LABEL_DOC
    docs = {}
    for path in sorted((ROOT / "src" / "kernel").glob("*")):
        if path.suffix not in (".inc", ".asm"):
            continue
        lines = path.read_text(errors="replace").splitlines()
        for i, line in enumerate(lines):
            lm = LABEL.match(line)
            if not lm:
                continue
            for j in range(i - 1, max(-1, i - 13), -1):
                m = CLAIM_LINE.match(lines[j])
                if m:
                    docs.setdefault(lm.group(1), []).append(
                        (path.name, j + 1, m.group(2).strip()))
                    continue
                if not lines[j].strip().startswith("."):
                    break                      # a blank line or code ends the block
    _LABEL_DOC = docs
    return docs


def all_comments():
    """[(file, line, text)] -- every comment line, WHOLE line, so a call-site
    comment (`jsr scanKeyboardHw // X = index`) is found by its callee name."""
    global _COMMENTS
    if _COMMENTS is not None:
        return _COMMENTS
    out = []
    for path in sorted((ROOT / "src" / "kernel").glob("*")):
        if path.suffix not in (".inc", ".asm"):
            continue
        for i, line in enumerate(path.read_text(errors="replace").splitlines()):
            if "//" in line or line.lstrip().startswith(";"):
                out.append((path.name, i + 1, line.strip()))
    _COMMENTS = out
    return out


def output_pat(reg):
    """`X = ...` and `returns/returns ... in Y` both name a value in the register."""
    r = re.escape(reg)
    return re.compile(
        r"(?-i:\b%s\b)\s*=" % r
        + r"|(?i:return|returns|returned|output)[^\n]{0,30}(?-i:\bin\s+%s\b)" % r)


def output_note(label, reg):
    """A comment naming `reg` as a value of the callee -- header first, then call site."""
    pat = output_pat(reg)
    for _file, _line, text in label_docs().get(label, []):
        if pat.search(text):
            return text
    for _file, _line, text in all_comments():
        if label in text and pat.search(text):
            return text
    return None


def impl_label(tgt):
    """A `$E000` jump-table label's implementation, so its own header can be found."""
    ins = decode(tgt)
    if ins and ins[0] == "JMP" and ins[1] == "abs":
        return BY_ADDR.get(ins[3], BY_ADDR.get(tgt, "$%04X" % tgt))
    return BY_ADDR.get(tgt, "$%04X" % tgt)


def callers_report(limit=80):
    """Call sites that read X or Y after a call without reloading it."""
    map_code()
    hits = []
    for ad in sorted(_CODE):
        ins = decode(ad)
        if ins is None or ins[0] != "JSR":
            continue
        tgt = entry_target(ins[3])
        if tgt is None or tgt not in _CODE:
            continue
        callee = BY_ADDR.get(tgt, "$%04X" % tgt)
        doc = impl_label(tgt)
        for reg in ("X", "Y"):
            if not dirty_at_exit(tgt, reg):
                continue
            spec = REGS[reg]
            p, guard = ad + 3, 0
            while guard < 12:
                j = decode(p)
                if j is None:
                    break
                op, mode, length, arg = j
                if op == "JSR" or op in ("RTS", "RTI", "BRK", "JMP", "PLA", "PHA"):
                    break                      # leaves the block, or unknown
                if op in spec["writers"] or op == spec["load"]:
                    break                      # reloaded (or stashed): fine
                if op in spec["read_ops"] or mode in spec["read_modes"]:
                    hits.append((ad, owner_label(ad), reg, callee, p, op, mode, arg,
                                 output_note(doc, reg)))
                    break
                p += length
                guard += 1
    print("callers of an X/Y-clobbering routine that read the register first: %d"
          % len(hits))
    for ad, lbl, reg, callee, p, op, mode, arg, note in hits[:limit]:
        print("   $%04X [%s]  jsr %-16s -> %s read at $%04X (%s %s)"
              % (ad, lbl, callee, reg, p, op, mode))
        if note:
            print("              callee header names %s: %s" % (reg, note[:90]))
        else:
            print("              no header line for %s -- read the call" % reg)
    if len(hits) > limit:
        print("   ... %d more" % (len(hits) - limit))
    return len(hits)


def selftest():
    """The analysis must still agree with the hand-verified table."""
    bad = total = 0
    for reg, (clobbers, preserves) in SELFTEST.items():
        for label in clobbers:
            total += 1
            if label not in SYM:
                print("  selftest: %s missing from build/main.sym" % label)
                bad += 1
            elif not verdict(label, reg):
                print("  selftest FAIL: %s should clobber %s" % (label, reg))
                bad += 1
        for label in preserves:
            total += 1
            if label not in SYM:
                print("  selftest: %s missing from build/main.sym" % label)
                bad += 1
            elif verdict(label, reg):
                print("  selftest FAIL: %s should preserve %s (%s)"
                      % (label, reg, _memo.get(("where", (reg, SYM[label])))))
                bad += 1
    print("self-test: %d/%d routines classified as verified" % (total - bad, total))
    return bad


def stale_warning():
    """A stale image would be audited against CURRENT claims -- say so."""
    newest = max((p.stat().st_mtime for p in (ROOT / "src").rglob("*")
                  if p.suffix in (".inc", ".asm")), default=0)
    if newest > (ROOT / "build" / "gordon-os.prg").stat().st_mtime:
        print("WARNING: a source file is newer than build/gordon-os.prg --"
              " rebuild the kernel before trusting this\n")


def main():
    argv = sys.argv[1:]
    flags = {a for a in argv if a.startswith("--")}
    names = [a for a in argv if not a.startswith("--")]
    if not (ROOT / "build" / "main.sym").exists():
        sys.exit("build/main.sym not found -- build the kernel first")
    if "--selftest" in flags:
        return 1 if selftest() else 0
    stale_warning()
    if "--probe" in flags:
        if not names:
            sys.exit("--probe needs one or more routine names")
        for n in names:
            if n not in SYM:
                print("%-18s (not in build/main.sym)" % n)
                continue
            print("%-18s %s" % (n, "  ".join(
                "%s=%s" % (r, "CLOBBERED" if verdict(n, r) else "preserved")
                for r in REG_ORDER)))
        return 0
    for reg in REG_ORDER:
        audit(reg, "--all" in flags)
        print("")
    if "--callers" in flags:
        callers_report()
    return 0


if __name__ == "__main__":
    sys.exit(main())
