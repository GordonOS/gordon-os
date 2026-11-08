#!/usr/bin/env python3
"""check-mus-commands.py -- the encoder tables against the format's own generator.

musedit builds a command pair from a mnemonic id and an operand (muObjEnc and the
mq* tables). tools/gen-mus.py builds the same pair from the format's definition,
and is what the tunes in music/ were made with. Nothing kept the two in step, so
a table row could drift from the format and the only symptom would be a tune that
plays something other than what the grid says -- as when a CALL or DEF of phrases
16-23 is written as a different command.

So this gate sweeps every mnemonic the editor can encode, over every operand the
editor allows, and compares the pair it would write with the generator's. Both
phrase encodings are covered, because that is where the two forms can disagree.

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


def byte_values(text, consts):
    """Every $xx / decimal byte in a run of .byte operands, or a named .const."""
    out = []
    for token in text.split(","):
        token = token.strip()
        if not token:
            continue
        if token.startswith("$"):
            out.append(int(token[1:], 16))
        elif token[0].isdigit():
            out.append(int(token, 0))
        elif token in consts:
            out.append(consts[token])
        else:
            raise SystemExit("check-mus-commands: a table holds %r, which is "
                             "neither a number nor a .const" % token)
    return out


def read_consts(lines):
    """name -> value for every `.const NAME = value` in the sources."""
    out = {}
    for line in lines:
        m = re.match(r"^\.const\s+(\w+)\s*=\s*(\S+)", strip(line).strip())
        if not m:
            continue
        raw = m.group(2)
        try:
            out[m.group(1)] = int(raw[1:], 16) if raw.startswith("$") else int(raw, 0)
        except ValueError:
            pass               # `= LABEL` or an expression: not a table value
    return out


def read_table(lines, label, consts=None):
    """The bytes of `label:` -- a .byte run, including its continuation lines."""
    if consts is None:
        consts = read_consts(lines)
    for i, line in enumerate(lines):
        m = re.match(r"^%s:\s*(.*)$" % re.escape(label), strip(line))
        if not m:
            continue
        values = []
        rest = m.group(1)
        if rest.strip().startswith(".byte"):
            values += byte_values(rest.strip()[5:], consts)
        j = i + 1
        while j < len(lines):
            body = strip(lines[j]).strip()
            if not body:
                j += 1
                continue
            if not body.startswith(".byte"):
                break
            values += byte_values(body[5:], consts)
            j += 1
        return values
    raise SystemExit("check-mus-commands: no table %r in %s" % (label, EDITOR))


_EXACT_CACHE = {}


def EXACT_CMD(gen):
    """name -> command byte, inverted from the generator's EXACT decode table.

    Those commands are the ones whose whole option byte is the operand, so the
    format's statement of them is just (command byte, value) -- and taking it from
    the generator's own table keeps this gate independent of the editor's.
    """
    if not _EXACT_CACHE:
        for cmd, (name, _f) in gen.EXACT.items():
            _EXACT_CACHE.setdefault(name, cmd)
    return _EXACT_CACHE


def read_const(lines, name, default):
    """A .const NAME = 123 (decimal) from the sources, or a default."""
    for line in lines:
        m = re.match(r"^\.const\s+%s\s*=\s*(\S+)" % re.escape(name), strip(line).strip())
        if m:
            return int(m.group(1), 0)
    return default


def read_mnemonics(lines):
    """muMnTab -- four characters per mnemonic id, in id order (1-based)."""
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
            table = [joined[k:k + 4].strip() for k in range(0, len(joined), 4)]
            return {n + 1: t for n, t in enumerate(table)}
    raise SystemExit("check-mus-commands: no muMnTab in %s" % EDITOR)


def editor_pair(cmd_b, opt_b, max_v, value, is_phrase, wide_b,
                shape=0, mask=0x0F, shift=0, cmd_mask=0, cmd_shift=0):
    """muObjEnc, line for line: (command byte, option byte) for one operand."""
    cmd = cmd_b
    if shape >= 5:                              # the high byte rides the command byte
        return ((((value >> 8) & cmd_mask) << cmd_shift) | cmd_b, value & 0xFF)
    if is_phrase:
        if value < 16:
            opt = (value << 4) | opt_b
        else:
            opt = ((value - 16 + 8) << 4) | wide_b
    elif shape == 0:
        opt = value | opt_b
    else:
        #  shapes 1-3: the low bits of the value move into the option's top bits
        opt = ((value & mask) << shift) | opt_b
    return (cmd, opt)


def reference(gen, mnemonic, value):
    """How the FORMAT encodes this mnemonic at this value, or None if unknown."""
    fixed = EXACT_CMD(gen).get(mnemonic)
    if fixed is not None:
        return (fixed, value)
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
        # mnemonic at all -- see muDecOpt's wide-phrase branch).
        return (0x01, ((value - 16 + 8) << 4) | low)
    if mnemonic == "TAL":
        return gen.TAL
    if mnemonic == "HLT":
        return gen.HLT
    if mnemonic == "END":
        return (0x01, 0x2F)
    if mnemonic == "P-W":                       # 12 bits: high nibble + option
        return ((((value >> 8) & 0x0F) << 4) | 0x02, value & 0xFF)
    if mnemonic == "DTN":                       # 12 bits, as P-W with its own base
        return ((((value >> 8) & 0x0F) << 4) | 0x0A, value & 0xFF)
    if mnemonic == "POR":                       # 14 bits: high six bits + option
        return ((((value >> 8) & 0x3F) << 2) | 0x03, value & 0xFF)
    if mnemonic == "MS#":                       # 10 bits: top two in the command byte
        return ((((value >> 8) & 0x03) << 6) | 0x1E, value & 0xFF)
    if mnemonic in FLAGS:                       # one bit at option bit 3, index in 4-6
        return (0x01, (FLAGS[mnemonic] << 4) | ((value & 1) << 3) | 0x03)
    if mnemonic == "RUP":                       # a five-bit rate at option bit 3
        return (0x01, (value << 3) | 0x01)
    if mnemonic == "RDN":
        return (0x01, (value << 3) | 0x05)
    if mnemonic == "SRC":                       # the source is the option's top three
        return (0x01, (value << 5) | 0x1F)
    if mnemonic == "DST":
        return (0x01, (value << 5) | 0x8F)
    fn = SOUND.get(mnemonic)
    if fn is not None:
        return getattr(gen, fn)(value)
    return None


# The eight that act on one bit: the option's low three bits are 011, bits 4-6 name
# the command (moSw indexes them that way, and gen-mus.py's c_voice3_on writes $53/$5B
# for index 5 -- the same pair, from the other direction), and bit 3 is the bit acted on.
FLAGS = {"BMP": 0, "FLT": 1, "RNG": 2, "SNC": 3, "F-X": 4, "3-O": 5, "LFO": 6, "P&V": 7}

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
    """muDecOpt's wide-phrase branch, transcribed.

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
    shape = read_table(lines, "mqShape")
    sh_mask = read_table(lines, "mqShMask")
    sh_shift = read_table(lines, "mqShShift")
    sh_max_lo = read_table(lines, "mqShMaxLo")
    sh_max_hi = read_table(lines, "mqShMaxHi")
    sh_cmd_mask = read_table(lines, "mqShCmdMask")
    sh_cmd_shift = read_table(lines, "mqShCmdShift")

    if len(max_v) != len(cmd_b):
        raise SystemExit("check-mus-commands: mqMax holds %d bytes, mqCmdB %d"
                         % (len(max_v), len(cmd_b)))
    wide_at = read_const(lines, "MQ_WIDE", len(cmd_b))
    wide = opt_b[wide_at:]
    if len(wide) < 9:
        raise SystemExit("check-mus-commands: the wide phrase block holds %d bytes, "
                         "expected the structural nine" % len(wide))

    checked = 0
    uncovered = []
    pending = []
    for row in range(len(cmd_b)):
        mnemonic = names.get(row + 1, "?")
        limit = max_v[row]
        if row < len(shape) and shape[row] == 9:
            #  no encoder in this slice: muObjEnc writes nothing and mqHave refuses
            #  the id, so there is no pair to compare yet
            if mnemonic not in pending:
                pending.append(mnemonic)
            continue
        is_phrase = mnemonic in ("DEF", "CALL")
        sh = shape[row] if row < len(shape) else 0
        if sh >= 5:                             # the maximum comes from the shape
            limit = sh_max_hi[sh - 5] * 256 + sh_max_lo[sh - 5]
        for value in range(limit + 1):
            want = reference(gen, mnemonic, value)
            if want is None:
                if mnemonic not in uncovered:
                    uncovered.append(mnemonic)
                continue
            got = editor_pair(cmd_b[row], opt_b[row], max_v[row], value,
                              is_phrase, wide[row] if row < len(wide) else 0,
                              sh,
                              sh_mask[sh] if sh < len(sh_mask) else 0x0F,
                              sh_shift[sh] if sh < len(sh_shift) else 0,
                              sh_cmd_mask[sh - 5] if sh >= 5 else 0,
                              sh_cmd_shift[sh - 5] if sh >= 5 else 0)
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

    if checked == 0:
        #  A table this gate cannot read must not report success: it would pass a
        #  build having verified nothing at all.
        raise SystemExit("check-mus-commands: no pair was checked -- the mnemonic "
                         "table parsed as %d name(s), %d of them unrecognised"
                         % (len(set(names.get(r + 1, "?") for r in range(len(cmd_b)))),
                            len(uncovered)))
    print("commands: %d pairs checked against gen-mus.py, %d mnemonics covered"
          % (checked, len(set(names.get(r + 1, "?") for r in range(len(cmd_b)))) - len(pending)))
    if pending:
        print("commands: %d command(s) still have no encoder: %s"
              % (len(pending), " ".join(pending)))
    if uncovered:
        print("commands: %d mnemonic(s) the editor offers but this gate cannot "
              "check yet: %s" % (len(uncovered), " ".join(uncovered)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
