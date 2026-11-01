#!/usr/bin/env python3
"""check-mus-commands.py -- the encoder tables against the format's own generator.

musedit builds a command pair from a mnemonic id and an operand (muObjEnc and the
mq* tables). tools/gen-mus.py builds the same pair from the format's definition,
and is what the tunes in music/ were made with. Nothing kept the two in step, so
a table row could drift from the format and the only symptom would be a tune that
plays something other than what the grid says -- which is exactly what bug #192
was (a CALL/DEF of phrases 16-23 written as a different command).

So this gate sweeps every mnemonic the editor can encode, over every operand the
editor allows, and compares the pair it would write with the generator's. Both
phrase encodings are covered, because that is the pair bug #192 lived in.

Usage
    python tools/check-mus-commands.py            # exit 1 on a mismatch

The tables are read from src/tasks/dynamic/musedit.inc, so a row added for
another command is checked with no change here -- the only thing that has to be
kept up to date is REF, which says how the FORMAT encodes each mnemonic.
"""

import importlib.util
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
EDITOR = os.path.join(ROOT, "src", "tasks", "dynamic", "musedit.inc")
GEN = os.path.join(ROOT, "tools", "gen-mus.py")


def load_gen():
    spec = importlib.util.spec_from_file_location("gen_mus", GEN)
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


def strip(line):
    return line.split("//")[0]


def byte_values(text):
    """Every $xx / decimal byte in a run of .byte operands."""
    out = []
    for token in text.split(","):
        token = token.strip()
        if not token:
            continue
        if token.startswith("$"):
            out.append(int(token[1:], 16))
        else:
            out.append(int(token, 0))
    return out


def read_table(lines, label):
    """The bytes of `label:` -- a .byte run, including its continuation lines."""
    for i, line in enumerate(lines):
        m = re.match(r"^%s:\s*(.*)$" % re.escape(label), strip(line))
        if not m:
            continue
        values = []
        rest = m.group(1)
        if rest.strip().startswith(".byte"):
            values += byte_values(rest.strip()[5:])
        j = i + 1
        while j < len(lines):
            body = strip(lines[j]).strip()
            if not body:
                j += 1
                continue
            if not body.startswith(".byte"):
                break
            values += byte_values(body[5:])
            j += 1
        return values
    raise SystemExit("check-mus-commands: no table %r in %s" % (label, EDITOR))


def read_mnemonics(lines):
    """muMnTab -- five characters per mnemonic id, in id order (1-based)."""
    for i, line in enumerate(lines):
        if strip(line).startswith("muMnTab:"):
            text = []
            j = i
            while j < len(lines):
                body = strip(lines[j])
                found = re.findall(r'"([^"]*)"', body)
                if not found:
                    break
                text.extend(found)
                j += 1
            joined = "".join(text)
            table = [joined[k:k + 5].strip() for k in range(0, len(joined), 5)]
            return {n + 1: t for n, t in enumerate(table)}
    raise SystemExit("check-mus-commands: no muMnTab in %s" % EDITOR)


def editor_pair(cmd_b, opt_b, max_v, value, is_phrase, wide_b):
    """muObjEnc, line for line: (command byte, option byte) for one operand."""
    cmd = cmd_b
    if is_phrase:
        if value < 16:
            opt = (value << 4) | opt_b
        else:
            opt = ((value - 16 + 8) << 4) | wide_b
    else:
        opt = value | opt_b
    return (cmd, opt)


def reference(gen, mnemonic, value):
    """How the FORMAT encodes this mnemonic at this value, or None if unknown."""
    if mnemonic == "TEM":
        return gen.c_tem(value)
    if mnemonic == "UTL":
        return gen.c_utl(value)
    if mnemonic == "UTV":
        return (0xF6, value)
    if mnemonic == "HED":
        return gen.c_hed(value)
    if mnemonic in ("DEF", "CALL"):
        low = 0x03 if mnemonic == "DEF" else 0x0B
        if value < 16:
            return (0x01, (value << 4) | (0x06 if mnemonic == "DEF" else 0x02))
        # 16-23: bits 4-6 hold phrase-16, bit 3 picks CALL from DEF, bit 7 is set by
        # the +8 (which is also why the renderer tests bit 7 before calling it a
        # mnemonic at all -- see muDecOpt, bug #192).
        return (0x01, ((value - 16 + 8) << 4) | low)
    if mnemonic == "TAL":
        return gen.TAL
    if mnemonic == "HLT":
        return gen.HLT
    if mnemonic == "END":
        return (0x01, 0x2F)
    fn = SOUND.get(mnemonic)
    if fn is not None:
        return getattr(gen, fn)(value)
    return None


# The sound commands the FORMAT defines, as gen-mus.py's own encoder functions.
# A mnemonic missing from this table is reported, not silently skipped: it means
# the editor offers a command whose encoding this gate cannot check.
SOUND = {
    "F-C": "c_fc",
    "P-S": "c_ps",
    "VRT": "c_vrt",
    "VDP": "c_vdp",
    "VOL": "c_vol",
    "ATK": "c_atk",
    "DCY": "c_dcy",
    "SUS": "c_sus",
    "REL": "c_rel",
    "WAV": "c_wav",
    "RES": "c_res",
    "F-M": "c_fm",
    "P-W": "c_pw",
    "3-O": "c_voice3_on",
}


def decode_editor_wide(opt):
    """muDecOpt's wide-phrase branch, transcribed -- the renderer bug #192 fixed.

    It is the independent statement of the form the round-trip is checked against:
    low three bits $03 AND bit 7 set, phrase = 16 + bits 4-6, bit 3 picks CALL.
    """
    if (opt & 0x07) != 0x03 or not (opt & 0x80):
        return None
    phrase = 16 + ((opt >> 4) & 0x07)
    return ("CALL" if opt & 0x08 else "DEF", phrase)


def main():
    gen = load_gen()
    lines = open(EDITOR, encoding="utf-8").read().splitlines()
    cmd_b = read_table(lines, "mqCmdB")
    opt_b = read_table(lines, "mqOptB")
    max_v = read_table(lines, "mqMax")
    names = read_mnemonics(lines)

    if len(opt_b) != 2 * len(cmd_b):
        raise SystemExit("check-mus-commands: mqOptB holds %d bytes, expected two "
                         "blocks of %d (the plain phrases and the 16-23 form)"
                         % (len(opt_b), len(cmd_b)))
    if len(max_v) != len(cmd_b):
        raise SystemExit("check-mus-commands: mqMax holds %d bytes, mqCmdB %d"
                         % (len(max_v), len(cmd_b)))
    wide = opt_b[len(cmd_b):]

    checked = 0
    uncovered = []
    for row in range(len(cmd_b)):
        mnemonic = names.get(row + 1, "?")
        is_phrase = mnemonic in ("DEF", "CALL")
        for value in range(max_v[row] + 1):
            want = reference(gen, mnemonic, value)
            if want is None:
                if mnemonic not in uncovered:
                    uncovered.append(mnemonic)
                continue
            got = editor_pair(cmd_b[row], opt_b[row], max_v[row], value,
                              is_phrase, wide[row])
            if got != want:
                print("MISMATCH %s %d: the editor writes %02X %02X, the format %02X %02X"
                      % (mnemonic, value, got[0], got[1], want[0], want[1]))
                return 1
            if is_phrase and value >= 16:
                back = decode_editor_wide(got[1])     # gen-mus.py does not model this form
                if back != (mnemonic, value):
                    print("MISMATCH %s %d: the pair %02X %02X reads back as %s"
                          % (mnemonic, value, got[0], got[1], back))
                    return 1
            else:
                name, _text = gen.decode_pair(got[0], got[1])
                if name != mnemonic:
                    print("MISMATCH %s %d: the pair %02X %02X reads back as %s"
                          % (mnemonic, value, got[0], got[1], name))
                    return 1
            checked += 1

    print("commands: %d pairs checked against gen-mus.py, %d mnemonics covered"
          % (checked, len(set(names[r + 1] for r in range(len(cmd_b))))))
    if uncovered:
        print("commands: %d mnemonic(s) the editor offers but this gate cannot "
              "check yet: %s" % (len(uncovered), " ".join(uncovered)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
