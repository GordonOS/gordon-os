#!/usr/bin/env python3
"""scan-history.py -- inventory history-flavoured comment content in CODE files.

Categories (Rule 14 forbids all of these inside code comments):
  planref   a pointer to a plan document (an active plan lives in docs/plans/ and
            is retired to Documents/GordonOSPlans once implemented, so a code
            pointer into either rots)
  bugref    a bug number (Bug #NN)
  date      a calendar date (2026-09-13) or "2026-08"
  phase     Phase/Step/Scope/decision markers
  narrate   change narration: used to / no longer / was moved / previously /
            originally / retired / legacy / tried and reverted / first version
Writes one report file per category plus a summary.
"""
import os
import re
from collections import Counter

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT = os.path.dirname(os.path.abspath(__file__))
SKIP_DIRS = {'.git', '.venv', 'build', 'tasks-out', 'reu', 'temp', '__pycache__',
             'node_modules', 'reference', 'docs', 'charset', 'music'}
CODE_EXT = {'.asm', '.inc', '.py', '.ps1', '.psm1', '.json', '.bat', '.cmd', '.sh'}

# This file's own docstring and patterns spell the categories out by name, so
# scanning it reports it on every run. It is the only file skipped by path.
SELF = os.path.join('tools', 'scan-history.py')

# Vendored ports keep their UPSTREAM text (Rule 14: provenance stays), so their
# comments are the original author's words, not narration we wrote. Scanning
# them reports upstream wording as if it were our history -- sidplay.inc alone
# contributes three such hits. Exempt by file name.
VENDORED = {'ehbasic.asm', 'sidplay.inc', 'basicedit.inc', 'filesys.asm',
            'sidobj64.asm', 'minimon.asm'}

# The patterns aim at the shapes a PLAN or a CHANGE NOTE uses, and deliberately
# not at ordinary English: a bare "Step 1:" is an algorithm label and a
# lowercase "phase 1:" is prose, so both are legitimate comments, and "used
# to" is how half the hardware notes in this tree read. A hit here should be
# something a reader would call a war story.
PATTERNS = [
    ('planref', re.compile(r'plans/|[a-z-]+-plan\.md')),
    ('bugref', re.compile(r'Bug #\d+|bug #\d+')),
    ('date', re.compile(r'\b20\d\d-\d\d(-\d\d)?\b')),
    # Plan markers only: a capitalised "Phase N", a "Scope N", a "decision N".
    # The "Step N.M" shape is NOT here, because it cannot tell a plan step
    # ("step 4.14") from an algorithm label ("Step 0.5: DMA the header"), and a
    # lint that reports the second is a lint people stop reading.
    ('phase', re.compile(r'\bPhase\s*\d|\bScope\s*\d|\bdecision\s+\d+')),
    # Change narration about THIS code. "used to" and "has been moved" are not
    # here: both read as ordinary English in a hardware or algorithm note ("the
    # ROM is used to select the bank", "the position has been moved by every
    # delta"), and a hit must mean a war story, not a sentence.
    ('narrate', re.compile(r'\bno longer\b|\bwas moved\b|\bmoved from\b|'
                           r'\bpreviously\b|\boriginally\b|\bretired\b|\blegacy\b|'
                           r'\btried and reverted\b|\breverted\b|\bfirst version\b|'
                           r'\bformerly\b|\bwas added\b|\bnow sits\b|'
                           r'\brenamed from\b|\bwas called\b|'
                           r'\bused to be\b|\bwhich used to\b', re.I)),
]

files = {}
for dirpath, dirnames, filenames in os.walk(ROOT):
    dirnames[:] = [d for d in dirnames if d not in SKIP_DIRS]
    for fn in sorted(filenames):
        if os.path.splitext(fn)[1].lower() not in CODE_EXT:
            continue
        path = os.path.join(dirpath, fn)
        rel = os.path.relpath(path, ROOT)
        if rel == SELF:
            continue
        if fn in VENDORED:
            continue
        with open(path, encoding='utf-8', newline='') as fh:
            for lineno, line in enumerate(fh.read().splitlines(), 1):
                # A phrase inside a double-quoted string is either emitted data
                # (.text) or a message a script prints -- printed text, which
                # Rule 14 exempts -- so the patterns run on the line with its
                # string literals blanked. Single quotes are NOT stripped:
                # apostrophes are everywhere in this tree's prose.
                code = re.sub(r'"[^"]*"', '""', line)
                hits = [name for name, rx in PATTERNS if rx.search(code)]
                for name in hits:
                    files.setdefault(name, []).append((rel, lineno, line.rstrip()))

summary = []
for name, _ in PATTERNS:
    rows = files.get(name, [])
    summary.append(f"{name:9s} {len(rows):5d} lines in "
                   f"{len({r[0] for r in rows}):3d} files")
    with open(os.path.join(OUT, f'hist_{name}.txt'), 'w', encoding='utf-8') as fh:
        last = None
        for rel, lineno, line in rows:
            if rel != last:
                fh.write(f"\n=== {rel} ===\n")
                last = rel
            fh.write(f"{lineno:5d}: {line}\n")

total = sum(len(v) for v in files.values())
summary.append(f"\nTOTAL flagged lines: {total}")
by_file = Counter(r[0] for v in files.values() for r in v)
summary.append("\ntop files:")
for rel, n in by_file.most_common(15):
    summary.append(f"  {n:4d}  {rel}")
open(os.path.join(OUT, 'hist_summary.txt'), 'w', encoding='utf-8').write(
    '\n'.join(summary) + '\n')
print('\n'.join(summary))
