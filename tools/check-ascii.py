#!/usr/bin/env python3
"""check-ascii.py -- Rule 19: every code file is plain 7-bit ASCII.

The toolchain is CP1252-hostile: KickAssembler runs under a Java default
charset, PowerShell 5.1 round-trips text through the console codepage, and some
consoles cannot encode characters the sources would like to use. A comment that
survives one host tool and not the next reads as garbage in an editor, a diff or
a terminal -- and a non-ASCII byte inside a `.text` string is emitted data, so it
becomes a WRONG SCREEN CODE on the C64, which is a behaviour bug rather than a
cosmetic one.

In scope (Rule 19): src/**, removed/**, tools/** and reference code. Data files
are exempt and stay byte-exact: src/corpus/*.bas, charsets/*, src/banners/*,
music/*, reference/**/*.md and the REU image.

Exit 0 = clean. Exit 1 = at least one code file has a byte above 126.
"""
import pathlib
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent

# The same shapes (and the same skip set) the documented one-liner uses, so the
# document and the gate cannot drift.
EXTS = {'.asm', '.inc', '.py', '.ps1', '.psm1', '.json', '.bat', '.cmd', '.sh'}
SKIP_PARTS = {'build', 'temp', 'tasks-out', '.venv', '.git', '__pycache__',
              'node_modules'}

bad = []
for path in sorted(ROOT.rglob('*')):
    if not path.is_file() or path.suffix.lower() not in EXTS:
        continue
    rel = path.relative_to(ROOT)
    if SKIP_PARTS & set(rel.parts):
        continue
    data = path.read_bytes()
    if any(b > 126 for b in data):
        offset = next(i for i, b in enumerate(data) if b > 126)
        line = data[:offset].count(b'\n') + 1
        bad.append(f"{rel}:{line}")

if bad:
    print("non-ASCII code files (Rule 19) -- a comment must be plain 7-bit ASCII:")
    for entry in bad:
        print("  " + entry)
    print("Repair: replace the character (-- for a dash, -> for an arrow, ... for")
    print("an ellipsis). Never put a non-ASCII character in a .text string.")
    sys.exit(1)

print("ASCII check: OK (no non-ASCII bytes in any code file)")
