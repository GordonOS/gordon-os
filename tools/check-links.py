#!/usr/bin/env python3
"""Check that every relative link in the tracked Markdown resolves.

Fenced code blocks and inline code are blanked first (same length, newlines
kept), so a syntax example that contains brackets is not read as a link. The
two files published to the binaries repository are excluded: their links are
written for the published layout and are checked by publish-binaries.ps1
after the copy.
"""

import os
import re
import subprocess
import sys
from urllib.parse import unquote

EXCLUDE = {"tools/README.binaries.md", "tools/quickstart.binaries.md"}
FENCE = re.compile(r"```.*?```|~~~.*?~~~", re.S)
INLINE = re.compile(r"`[^`\n]*`")
LINK = re.compile(r"!?\[[^\]]*\]\(([^)\s]+)(?:\s+\"[^\"]*\")?\)")
ABSOLUTE = re.compile(r"^(?i:https?://|mailto:|data:|#)")


def blank(match):
    return re.sub(r"[^\n]", " ", match.group(0))


def check(path):
    with open(path, encoding="utf-8") as fh:
        text = fh.read()
    text = INLINE.sub(blank, FENCE.sub(blank, text))
    problems = []
    for m in LINK.finditer(text):
        target = m.group(1).strip("<>")
        if ABSOLUTE.match(target):
            continue
        local = unquote(target.split("#", 1)[0])
        if not local:
            continue
        resolved = os.path.normpath(os.path.join(os.path.dirname(path), local))
        if not os.path.exists(resolved):
            line = text.count("\n", 0, m.start()) + 1
            problems.append((line, target))
    return problems


def main():
    listed = subprocess.run(["git", "ls-files"], capture_output=True,
                            text=True, check=True).stdout.splitlines()
    files = [f for f in listed if f.endswith(".md") and f not in EXCLUDE]
    broken = 0
    for path in files:
        for line, target in check(path):
            print(f"{path}:{line}: {target}")
            broken += 1
    print(f"links: {len(files)} Markdown files checked, {broken} unresolved")
    return 1 if broken else 0


if __name__ == "__main__":
    sys.exit(main())
