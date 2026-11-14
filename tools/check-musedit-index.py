#!/usr/bin/env python3
"""check-musedit-index.py -- every `(dataPtr),y` access sets Y where it uses it.

`musedit` keeps its state in the instance buffer, reached as `(dataPtr),y`, so Y is
not a free register: it is the index, and setting it is half of the access. That
makes one class of edit silently wrong -- replacing the load of a field with a zero
page read while leaving the store that shared the same Y:

    ldy #MU_CURCOL          ; the index, for BOTH statements
    lda (dataPtr),y
    clc
    adc #1
    sta (dataPtr),y         ; converted to `sta muCurCol`?  if not, this writes
                            ;   the COLUMN into the ROW, because Y now holds
                            ;   whatever the next `ldy` set it to

That is exactly what a zero page conversion did to five cursor steps: the text
cursor stopped advancing (every character of a run landed on one cell) and the
cursor's row byte was overwritten. It compiles, it passes every other gate, and
only running the editor shows it.

So the rule: an access may only inherit a Y that was set for it. Concretely, the
nearest preceding Y-writer must be

  * `tay` / `ply` / `iny` / `dey` -- a computed index, always fine;
  * a plain `ldy` within LIMIT statements, and no `iny`/`dey` in between; or
  * nothing at all, in a routine listed in Y_INPUTS, which take Y from the caller.

`iny`/`dey` between the `ldy` and the access marks a loop over the buffer, which
is the one case where a distant Y is the point.

Usage:
    python tools/check-musedit-index.py [file]   exit 0 when every access is sound
                                                (the argument names another file to
                                                check, which is how the gate is tested)
"""

import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SOURCE = os.path.join(ROOT, "src", "tasks", "dynamic", "musedit.inc")

#  How far a plain `ldy` may sit from the access it serves. The editor's own
#  convention is one statement (`ldy #field` / `lda (dataPtr),y`); a read-modify-
#  write of one field is two to four. Anything past this is a different statement
#  group, and Y has almost certainly been left over from the one before.
LIMIT = 5

#  Routines that take Y as an ARGUMENT: their caller sets it and the routine
#  never does, so there is no in-routine setter to find. Both are internal
#  helpers of the pointer code, called immediately after their own `ldy`.
Y_INPUTS = {
    "muSettle",    # uPrev - sRun + sMin, three fields an `iny` apart
    "muAxis",      # one axis's signed delta folded into a 16-bit position
    "muAxisNeg",   # ...the other sign of it
}

ACCESS = re.compile(r"\b(lda|sta|cmp|adc|sbc|and|ora|eor)\s+\(dataPtr\),y$")
YSET = re.compile(r"^(ldy|tay|ply|iny|dey)\b")
COMPUTED = re.compile(r"^(tay|ply|iny|dey)\b")
STEPPED = re.compile(r"^(iny|dey)\b")
LABEL = re.compile(r"^([A-Za-z_@][\w@]*):")
END = re.compile(r"^(rts|jmp)\b")


def statements(path):
    """(line number, code) for every non-blank line, comments stripped."""
    out = []
    for i, line in enumerate(open(path, encoding="utf-8"), 1):
        code = line.split("//")[0].strip()
        if code:
            out.append((i, code))
    return out


def check(path):
    body = statements(path)
    problems = []
    routine = "?"
    for k, (line, code) in enumerate(body):
        m = LABEL.match(code)
        if m:
            routine = m.group(1)
        if not ACCESS.match(code):
            continue

        j, distance, setter = k - 1, 1, None
        while j >= 0:
            earlier = body[j][1]
            if END.match(earlier):
                break                      # the routine's start: no setter inside it
            if YSET.match(earlier):
                setter = (earlier, body[j][0], distance)
                break
            j -= 1
            distance += 1

        if setter is None:
            if routine not in Y_INPUTS:
                problems.append((line, routine, code,
                                 "no Y is set in this routine, and it is not one "
                                 "that takes Y from its caller"))
            continue

        text, setter_line, distance = setter
        if COMPUTED.match(text):
            continue                       # a computed index
        if any(STEPPED.match(body[q][1]) for q in range(
                next(q for q in range(k) if body[q][0] == setter_line) + 1, k)):
            continue                       # a loop walking the buffer
        if distance > LIMIT:
            problems.append((line, routine, code,
                             "`%s` at line %d is %d statements back"
                             % (text, setter_line, distance)))
    return problems


def main():
    path = sys.argv[1] if len(sys.argv) > 1 else SOURCE
    if not os.path.exists(path):
        print("check-musedit-index: no musedit.inc at %s" % path)
        return 2
    problems = check(path)
    accesses = len([1 for _l, c in statements(path) if ACCESS.match(c)])
    if problems:
        print("%d (dataPtr),y access(es) inherit a stale Y:" % len(problems))
        for line, routine, code, why in problems:
            print("  %s:%d  in %s" % (os.path.basename(path), line, routine))
            print("      %s" % code)
            print("      %s" % why)
        print("\nAn access may only use a Y set for it: `ldy #field` immediately above, "
              "a computed index (tay/iny), or a caller-supplied Y in a routine listed "
              "in Y_INPUTS.")
        return 1
    print("musedit index: OK (%d (dataPtr),y accesses, each with a Y set for it)" % accesses)
    return 0


if __name__ == "__main__":
    sys.exit(main())
