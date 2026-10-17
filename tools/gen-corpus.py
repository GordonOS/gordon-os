#!/usr/bin/env python3
"""gen-corpus.py -- build tokenized Gordon Basic .bas files for the
REU-backed-BASIC corpus.

Mirrors the EhBASIC cruncher (lab_13a6) exactly:
  - keyword tables are PARSED from src/tasks/dynamic/ehbasic.asm at
    generation time (tab_1stc / tab_chrt / tab_*), so token values can
    never drift from the interpreter;
  - input bytes < '*' or in '0'..'<' or >= '{' are stored literally;
  - quotes are copied verbatim (scnquo/asrch alias);
  - after REM or DATA the rest of the line is copied verbatim;
  - '?' crunches to tk_print; '*', '^', '<', '>', '=', '+', '-', '/'
    to their operator tokens; keywords must be lowercase (as typed).

Output format = the exact program image LOAD expects (raw [B, svarl)):
  per line: next_ptr_lo, next_ptr_hi, line#_lo, line#_hi, crunched
  bytes..., 0x00. Next pointers are RELATIVE (0-based offsets from the
  program start) -- BASIC's LOAD relinks them to absolute addresses, so
  the file loads correctly regardless of where program RAM is placed.

Usage: python tools/gen-corpus.py
Writes: src/corpus/*.bas (seeded into the REU FS by tools/build-reu.ps1).
"""

import os
import re

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
EHBASIC = os.path.join(ROOT, "src", "tasks", "dynamic", "ehbasic.asm")
CORPUS_DIR = os.path.join(ROOT, "src", "corpus")
# Line links are stored relative (0-based); BASIC's LOAD relinks them.


def parse_consts(src):
    """tk_* constants -> {name: int}, resolving chains iteratively."""
    consts = {}
    for m in re.finditer(r"\.const\s+(tk_\w+)\s*=\s*([^\s]+)", src):
        consts[m.group(1)] = m.group(2)
    for _ in range(200):
        changed = False
        for name, val in list(consts.items()):
            if isinstance(val, str):
                try:
                    expr = re.sub(r"\$([0-9a-fA-F]+)", r"0x\1", val)
                    new = eval(expr, {"__builtins__": {}}, dict(consts))
                except (NameError, TypeError, SyntaxError):
                    new = val
                if isinstance(new, int):
                    consts[name] = new
                    changed = True
        if not changed:
            break
    assert all(isinstance(v, int) for v in consts.values()), "unresolved tk_ consts"
    return consts


def parse_tokens(src, consts):
    """tab_1stc/tab_chrt/tab_* -> {keyword(str): token(int)}."""

    def text_bytes(body):
        """decode a run of .text/.byte into bytes (uses consts for names)."""
        out = []
        pos = 0
        while pos < len(body):
            m = re.match(r"\s*\.text\s+\"((?:[^\"\\]|\\.)*)\"", body[pos:])
            if m:
                out.extend(m.group(1).encode("ascii"))
                pos += m.end()
                continue
            m = re.match(r"\s*\.byte\s+([^;\n]+)", body[pos:])
            if m:
                for v in m.group(1).split(","):
                    v = v.strip()
                    if not v:
                        continue
                    if v.startswith("$"):
                        out.append(int(v[1:], 16))
                    elif re.match(r"^\d+$", v):
                        out.append(int(v))
                    else:
                        out.append(consts[v])
                pos += m.end()
                continue
            pos += 1
        return out

    # tab_1stc: .text "<char>" entries then .byte $00
    i = src.index("tab_1stc:")
    j = src.index("tab_chrt:")
    firsts = [m.group(1).encode("ascii")[0]
              for m in re.finditer(r"\.text\s+\"(.)\"", src[i:j])]

    # tab_chrt: .word tab_xxx entries, one per first char
    i = src.index("tab_chrt:")
    j = src.index("\n\n", i) if "\n\n" in src[i:] else len(src)
    tables = [m.group(1)
              for m in re.finditer(r"\.word\s+(tab_\w+)", src[i:j])]
    assert len(tables) == len(firsts), "tab_chrt/tab_1stc mismatch"

    tokens = {}
    for first, tab in zip(firsts, tables):
        i = src.index(tab + ":")
        # table body ends at the next NON-lbb label (lbb_* are internal
        # keyword labels inside the table bodies)
        body_start = i + len(tab) + 1
        end = None
        for m in re.finditer(r"\n(\w+):", src[body_start:]):
            if not m.group(1).startswith("lbb"):
                end = body_start + m.start()
                break
        body = src[body_start:end] if end is not None else src[body_start:]
        raw = text_bytes(body)
        chars, entries = [], []
        for b in raw:
            if b >= 0x80:          # token ends an entry
                entries.append((bytes(chars), b))
                chars = []
            elif b == 0x00:        # table terminator
                break
            else:
                chars.append(b)
        for chars_part, tok in entries:
            word = bytes([first]) + bytes(chars_part)
            tokens[word.decode("ascii")] = tok
    return tokens


def build_tokens():
    src = open(EHBASIC, encoding="utf-8").read()
    src = re.sub(r"//.*", "", src)  # strip comments
    consts = parse_consts(src)
    tokens = parse_tokens(src, consts)
    # spot checks against the const table
    assert tokens["print"] == consts["tk_print"]
    assert tokens["?"] == consts["tk_print"]
    assert tokens["+"] == consts["tk_plus"]
    assert tokens["<"] == consts["tk_lt"]
    assert tokens[">"] == consts["tk_gt"]
    assert tokens["="] == consts["tk_equal"]
    assert tokens["rem"] == consts["tk_rem"]
    assert tokens["data"] == consts["tk_data"]
    assert tokens["then"] == consts["tk_then"]
    assert tokens["until"] == consts["tk_until"]
    assert tokens["mid$("] == consts["tk_mids"]
    return tokens, consts


TOKENS, CONSTS = build_tokens()


def crunch(line):
    """Crunch one line (text AFTER the line number) like lab_13a6."""
    out = bytearray()
    i, n = 0, len(line)
    while i < n:
        c = line[i]
        # lab_13a6 byte-classification (ASCII): '"' copies the quoted string
        # verbatim; '0'..';', < '*', and >= '{' are literal; '*'..'/' and
        # '<'..'z' are crunch candidates.
        if c == '"':
            out.append(ord(c))
            i += 1
            while i < n and line[i] != '"':
                out.append(ord(line[i]))
                i += 1
            if i < n:
                out.append(ord(line[i]))
                i += 1
            continue
        if c >= "{" or "0" <= c <= ";" or c < "*":
            out.append(ord(c))
            i += 1
            continue
        # crunch candidate: longest keyword match at this position
        match = None
        for word in sorted(TOKENS, key=len, reverse=True):
            if line.startswith(word, i):
                match = word
                break
        if match is None:
            out.append(ord(c))
            i += 1
            continue
        out.append(TOKENS[match])
        i += len(match)
        if out[-1] == CONSTS["tk_rem"] or out[-1] == CONSTS["tk_data"]:
            out.extend(line[i:].encode("ascii"))  # verbatim to end of line
            break
    return bytes(out)


def build_image(src_lines):
    """src_lines: [(lineno:int, text:str)] -> raw program image.

    The file mirrors BASIC's SAVE layout: a leading 0 byte (the pre-text
    byte at B), then the program starting at offset 1 (B+1). Line links
    are relative 0-based offsets from the program start; BASIC's LOAD
    relinks them (line_start + length), so the image is position-
    independent.
    """
    encoded = [(lineno, crunch(text) + b"\x00") for lineno, text in src_lines]
    out = bytearray([0])      # pre-text byte (B) -- program starts at offset 1
    addr = 0                  # relative to offset 1 (program start = B+1)
    for idx, (lineno, body) in enumerate(encoded):
        nxt = addr + 4 + len(body) if idx + 1 < len(encoded) else 0
        out += bytes([nxt & 0xFF, nxt >> 8, lineno & 0xFF, lineno >> 8])
        out += body
        addr = nxt or addr
    return bytes(out)


# --- corpus programs (lowercase keywords, as typed) ---
# Ships a single demo (basdemo) in the production REU. Add entries here
# and re-run `python tools/gen-corpus.py` to seed more .bas programs.
CORPUS = {
    "basdemo": [
        (10, 'rem gordon basic demo'),
        (20, 'print "hello world"'),
        (30, 'end'),
    ],
}


def main():
    os.makedirs(CORPUS_DIR, exist_ok=True)
    for base, lines in CORPUS.items():
        img = build_image(lines)
        with open(os.path.join(CORPUS_DIR, base + ".bas"), "wb") as f:
            f.write(img)
        print("%-12s %4d bytes" % (base + ".bas", len(img)))
    print("wrote %d corpus files to src/corpus/" % len(CORPUS))


if __name__ == "__main__":
    main()
