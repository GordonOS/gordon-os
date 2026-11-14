#!/usr/bin/env python3
"""check-mus-pitch.py -- the editor's pitch split against the host model, on the machine.

`muPitchSplit` turns a note's option byte into a semitone and an octave, and rolls an
accidental past the octave the way the player's note path does: a sharp on B is the
next octave's C, a flat on C the octave below's B, and at either end of the octave
field only the semitone rolls. `mus-sim.py`'s `pitch_name` models the same rule.

Two statements of one rule can drift, and this pair did: the spelling of a sharp on
B was wrong on both sides at once, so neither could catch the other. This runs the
ASSEMBLED routine under py65 over every note option the format allows (224 of them)
and compares its answer with the model's, which makes the pair self-checking.

Nothing is stubbed: the routine reads only its own tables, which travel inside the
task image.

Usage
    python tools/check-mus-pitch.py            # exit 1 on a disagreement
    python tools/check-mus-pitch.py -v         # list every option

Exit: 0 agreement, 1 a disagreement, 2 could not run. Needs tasks-out/musedit.bin,
so it is a task-build check rather than a CI one.
"""
import importlib.util
import os
import sys

from py65.devices.mpu6502 import MPU

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
BIN = os.path.join(ROOT, "tasks-out", "musedit.bin")
SIM = os.path.join(ROOT, "tools", "mus-sim.py")

#  sta muTmpH, then six lsr, then tay: the first six instructions of muPitchSplit.
#  The stored operand is muTmpH's zero-page address, which is not fixed here, so
#  the two bytes are wildcards.
SIGNATURE_TAIL = bytes([0x4A, 0x4A, 0x4A, 0x4A, 0x4A, 0x4A, 0xA8])


def load_model():
    spec = importlib.util.spec_from_file_location("mus_sim", SIM)
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


def load_task(path):
    """(code, its load address) of a relocatable task binary."""
    raw = open(path, "rb").read()
    origin = raw[3] | (raw[4] << 8)
    size = raw[5] | (raw[6] << 8)
    if len(raw) < 256 + size:
        raise SystemExit("check-mus-pitch: %s is shorter than its header says" % path)
    return raw[256:256 + size], origin + 256


def find_routine(code, base):
    hits = [base + i for i in range(len(code) - len(SIGNATURE_TAIL))
            if code[i + 2:i + 2 + len(SIGNATURE_TAIL)] == SIGNATURE_TAIL
            and code[i] == 0x85]                     # sta <zero page>
    if len(hits) != 1:
        raise SystemExit("check-mus-pitch: %d candidates for muPitchSplit in the image, "
                         "expected exactly one -- has the routine been rewritten?"
                         % len(hits))
    return hits[0]


def split_at(mpu, code, base, entry, opt, sentinel=0x1FFF):
    """Run the routine once: (semitone, octave, the C flag)."""
    for a, b in enumerate(code):
        mpu.memory[base + a] = b
    mpu.pc = entry
    mpu.a = opt
    mpu.sp = 0xFD
    mpu.memory[0x01FE] = (sentinel - 1) & 0xFF
    mpu.memory[0x01FF] = (sentinel - 1) >> 8
    for _ in range(2000):
        mpu.step()
        if mpu.pc == sentinel:
            return mpu.a, mpu.x, bool(mpu.p & 0x01)
    raise SystemExit("check-mus-pitch: the routine did not return for option $%02X" % opt)


def main(argv):
    verbose = "-v" in argv or "--verbose" in argv
    if not os.path.exists(BIN):
        return 2
    sim = load_model()
    code, base = load_task(BIN)
    entry = find_routine(code, base)
    mpu = MPU()
    bad = 0
    checked = 0
    for opt in range(1, 256):
        if opt & 0x07 == 0:
            continue                                  # a rest: the routine is not asked
        semitone, octave, rolled = split_at(mpu, code, base, entry, opt)
        want = sim.pitch_name(opt)
        got = "%s%d" % (sim.NOTE_NAMES[semitone], octave) if semitone < 12 else "?"
        raw = sim.SEMITONES[opt & 0x07] + (1 if opt & 0xC0 == 0x40 else
                                          -1 if opt & 0xC0 == 0xC0 else 0)
        want_rolled = raw < 0 or raw > 11
        checked += 1
        if got != want or rolled != want_rolled:
            bad += 1
            print("DISAGREE option $%02X: the editor says %s (roll %d), the model %s (roll %d)"
                  % (opt, got, rolled, want, want_rolled))
        elif verbose:
            print("  $%02X  %s" % (opt, got))
    print("pitch: %d note options, %d disagreements" % (checked, bad))
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
