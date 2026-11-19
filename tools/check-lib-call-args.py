#!/usr/bin/env python3
"""Tripwire: every call site of an entry that takes a kArg block input writes all of it.

The kArg block is a shared scratch: `kArg` .. `kArg+3` is the caller's argument
area, `kArg+4` is the DMA length high byte written by every REU transfer, and
`kArg+5`+ is whatever the last kernel or library call left there. So a caller that
sets only SOME of an entry's inputs passes another task's bytes as the rest -- and
nothing in the build can see it, because the entry's header is prose and the
assembler has no idea which bytes a routine reads.

That is how `kSidPlay`'s sixth input cost a release: `kArg+4`/`kArg+5` (the jiffies
to play silently before the music is heard) arrived with musedit's streamed player,
musedit set it, and the other five call sites -- sidplay's two, godzi, gortris,
grknoid -- kept setting only `kArg` .. `kArg+3`. Those players fast-forwarded a
garbage number of jiffies, so every tune started part-way in, and it looked like a
player bug until the call sites were read (docs/bugs.md #211).

Add an entry to ENTRIES when a routine gains a kArg-block input. The lookback stops
at the routine's own label, so a store that belongs to an earlier call cannot
satisfy this one.

Exit 1 on a violation, 0 when every call site is complete.
"""

import glob
import os
import re
import sys

# entry label -> the kArg offsets it reads. Keep this list short and real: a
# guessed requirement fails a correct call site, which is worse than no gate.
ENTRIES = {
    # kSidPlay: delay, repeats, transpose octaves, transpose semitones, start jiffy
    # (lo, hi). A = mode is a register, so a source scan cannot check it.
    "kSidPlay": (0, 1, 2, 3, 4, 5),
}

LOOKBACK = 60
SRC = ("src/tasks/**/*.asm", "src/tasks/**/*.inc")

LABEL_RE = re.compile(r"^[A-Za-z_][A-Za-z0-9_]*\s*:")
CONST_RE = re.compile(r"^\.(label|const)\b")
STORE_RE = re.compile(r"\bsta\s+kArg(?:\+(\d+))?\b", re.IGNORECASE)


def files():
    out = []
    for pattern in SRC:
        out.extend(glob.glob(pattern, recursive=True))
    return sorted(set(out))


def call_sites(path, entry):
    with open(path, encoding="utf-8") as fh:
        lines = fh.read().splitlines()
    jsr = re.compile(r"\bjsr\s+" + re.escape(entry) + r"\b", re.IGNORECASE)
    for idx, line in enumerate(lines):
        if jsr.search(line):
            yield idx, lines


def written_offsets(lines, call_idx):
    """The kArg offsets this routine writes before the call, scanning back to its label."""
    seen = set()
    for idx in range(call_idx - 1, max(call_idx - LOOKBACK, 0) - 1, -1):
        line = lines[idx]
        if LABEL_RE.match(line) or CONST_RE.match(line):
            break
        match = STORE_RE.search(line)
        if match:
            seen.add(int(match.group(1)) if match.group(1) else 0)
    return seen


def main():
    problems = []
    sites = 0
    for path in files():
        for entry, required in ENTRIES.items():
            for idx, lines in call_sites(path, entry):
                sites += 1
                seen = written_offsets(lines, idx)
                missing = [off for off in required if off not in seen]
                if missing:
                    names = ", ".join("kArg+%d" % off for off in missing)
                    problems.append(
                        "%s:%d: jsr %s does not write %s" % (path, idx + 1, entry, names)
                    )

    for problem in problems:
        print(problem)
    if problems:
        print("check-lib-call-args: FAILED (%d call site(s) incomplete)" % len(problems))
        return 1

    print("check-lib-call-args: OK (%d call site(s) complete)" % sites)
    return 0


if __name__ == "__main__":
    sys.exit(main())
