#!/usr/bin/env python3
"""mus-sim.py -- the Sidplayer's control flow, modelled on the host.

musedit's grid is a timeline derived from the player's own walk of the .mus
stream, so the reference model lives here: this script reads a tune the way
src/libs/sidplay.inc does -- three voices, a per-voice countdown, HED/TAL,
DEF/CALL/END, HLT, and the global tempo plus UTL/UTV -- and reports when every
event plays.

The trace it prints is the trace musedit's `X` key prints, line for line, which
is how the two are compared:

    python tools/mus-sim.py music/tetris.mus --trace 20

The duration arithmetic is not re-derived here: it comes from
tools/check-mus-timing.py, so the editor, the player and this model share one
account of how long a note lasts.
"""

import importlib.util
import os
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
TIMING = os.path.join(ROOT, "tools", "check-mus-timing.py")

# sidplay.inc's sidInitSong: WorkTempo = $90, WorkUTLValue = $0c,
# WorkUTVValues = 1, WorkVoiceForCallPhrase = the voice index, and
# WorkPhraseCallStack = DataMinCallStack (0, 5, 10).
INIT_TEMPO = 0x90
INIT_UTL = 0x0C
INIT_UTV = 0x01
CALL_BASE = (0, 5, 10)          # DataMinCallStack's real values, 5 apart
CALL_MAX = (5, 10, 15)          # DataMaxCallStack

MAX_EVENTS = 200000             # a looping tune reaches this and is reported

CLS_NAMES = {0: "64th", 1: "UTL/UTV", 2: "whole", 3: "half", 4: "quarter",
             5: "8th", 6: "16th", 7: "32nd"}

# The editor's own note table: one letter per note index 1..7, and the two
# characters it puts on the grid for each semitone.
SEMITONES = (0, 0, 2, 4, 5, 7, 9, 11)
NOTE_NAMES = ("C-", "C#", "D-", "D#", "E-", "F-", "F#", "G-", "G#", "A-", "A#", "B-")


def timing():
    """The duration model, imported from the timing gate."""
    spec = importlib.util.spec_from_file_location("check_mus_timing", TIMING)
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


class Stop(Exception):
    """The player would have stopped the whole song here."""


class Voice:
    def __init__(self, index, start):
        self.index = index
        self.ptr = start            # image offset of the next event
        self.tick = 0               # when that event will be played
        self.on = True
        self.utv = INIT_UTV
        self.dep = CALL_BASE[index]
        self.ident = index          # WorkVoiceForCallPhrase starts as the voice #
        self.hed_count = 0
        self.hed_at = start
        self.hed_ident = index
        self.events = 0
        self.notes = 0


def le16(image, offset):
    return image[offset] | (image[offset + 1] << 8)


def command_name(cmd, opt):
    """The mnemonic for a command pair, for the trace's last column."""
    if cmd == 0x36:
        return "HED %d" % opt
    if cmd == 0x06:
        return "TEM $%02X" % opt
    if cmd == 0x16:
        return "UTL %d" % opt
    if cmd == 0xF6:
        return "UTV %d" % opt
    if cmd & 3 == 1:
        if opt == 0x4F:
            return "HLT"
        if opt == 0x0F:
            return "TAL"
        if opt == 0x2F:
            return "END"
        if opt & 7 == 3 and opt >= 0x80:
            return ("CALL" if opt & 8 else "DEF") + " %d" % (16 + ((opt >> 4) & 7))
        if opt & 0x0F == 0x02:
            return "CALL %d" % (opt >> 4)
        if opt & 0x0F == 0x06:
            return "DEF %d" % (opt >> 4)
        return "cmd $%02X $%02X" % (cmd, opt)
    return "cmd $%02X $%02X" % (cmd, opt)


def simulate(image, trace_limit=0, trace_cmd=False):
    """Walk the tune as the player does; return the report and the trace."""
    model = timing()
    lengths = [le16(image, 2), le16(image, 4), le16(image, 6)]
    starts, at = [], 8
    for size in lengths:
        starts.append(at)
        at += size
    voices = [Voice(i, starts[i]) for i in range(3)]

    tempo = INIT_TEMPO
    utl = INIT_UTL
    deffed = [None] * 24        # phrase address, None = not defined
    dfident = [0xFF] * 24       # WorkPhraseUnknown
    frlo = [0] * 15
    frhi = [None] * 15          # None is the DEF sentinel (the player stores hi = 0)
    frst = [0] * 15

    trace = []
    details = []
    clock = 0
    error = None
    events = 0

    try:
        while events < MAX_EVENTS:
            live = [v for v in voices if v.on]
            if not live:
                break
            voice = min(live, key=lambda v: (v.tick, v.index))
            clock = voice.tick
            while True:
                if voice.ptr + 1 >= len(image):
                    raise Stop("voice %d ran off the image at %d"
                               % (voice.index + 1, voice.ptr))
                cmd, opt = image[voice.ptr], image[voice.ptr + 1]
                at = voice.ptr
                voice.ptr += 2
                if trace_cmd or cmd & 3 == 0:
                    if len(trace) < trace_limit:
                        trace.append((clock, voice.index, at, cmd, opt))
                if cmd & 3 == 0:
                    value = model.duration((cmd >> 2) & 7, tempo, cmd & 0xA0, utl, voice.utv)
                    if value is None:
                        raise Stop("voice %d: %s at tempo $%02X is not a length"
                                   % (voice.index + 1, CLS_NAMES[(cmd >> 2) & 7], tempo))
                    if value == 0:              # a zero countdown reads as 256
                        value = 256
                    voice.tick += value
                    voice.events += 1
                    voice.notes += 1
                    events += 1
                    details.append((clock, voice.index, at, cmd, opt, value))
                    break
                if cmd == 0x36:                 # HED
                    voice.hed_count = opt
                    voice.hed_at = voice.ptr
                    voice.hed_ident = voice.ident
                elif cmd == 0x06:               # TEM
                    tempo = opt
                elif cmd == 0x16:               # UTL
                    utl = opt
                elif cmd == 0xF6:               # UTV
                    voice.utv = opt
                elif cmd & 3 == 1:
                    if opt == 0x4F:             # HLT
                        voice.on = False
                        break
                    if opt == 0x0F:             # TAL
                        repeat = voice.hed_count == 0
                        if not repeat:
                            voice.hed_count -= 1
                            repeat = voice.hed_count != 0
                        if repeat:
                            if voice.ident != voice.hed_ident:
                                raise Stop("voice %d: TAL in a different phrase than its HED"
                                           % (voice.index + 1))
                            voice.ptr = voice.hed_at
                    elif opt == 0x2F:           # END
                        if voice.dep == CALL_BASE[voice.index]:
                            raise Stop("voice %d: END with no call to return to"
                                       % (voice.index + 1))
                        voice.dep -= 1
                        index = voice.dep
                        if frhi[index] is not None:     # not the DEF sentinel
                            voice.ptr = frlo[index] | (frhi[index] << 8)
                            voice.ident = frst[index]
                    else:
                        kind, phrase = None, None
                        if opt & 7 == 3 and opt >= 0x80:
                            kind = "CALL" if opt & 8 else "DEF"
                            phrase = 16 + ((opt >> 4) & 7)
                        elif opt & 0x0F == 0x02:
                            kind, phrase = "CALL", opt >> 4
                        elif opt & 0x0F == 0x06:
                            kind, phrase = "DEF", opt >> 4
                        if kind == "DEF":
                            deffed[phrase] = voice.ptr
                            dfident[phrase] = voice.ident
                            if voice.dep == CALL_MAX[voice.index]:
                                raise Stop("voice %d: call stack full at DEF %d"
                                           % (voice.index + 1, phrase))
                            index = voice.dep
                            frhi[index] = None          # the DEF sentinel
                            frst[index] = voice.ident
                            voice.dep += 1
                        elif kind == "CALL":
                            if voice.dep == CALL_MAX[voice.index]:
                                raise Stop("voice %d: call stack full at CALL %d"
                                           % (voice.index + 1, phrase))
                            if deffed[phrase] is None:
                                raise Stop("voice %d: CALL %d is not a defined phrase"
                                           % (voice.index + 1, phrase))
                            index = voice.dep
                            frlo[index] = voice.ptr & 0xFF
                            frhi[index] = voice.ptr >> 8
                            frst[index] = voice.ident
                            voice.dep += 1
                            voice.ptr = deffed[phrase]
                            voice.ident = dfident[phrase]
                # anything else has no effect on when a note plays
    except Stop as why:
        error = str(why)

    if error is not None:
        for voice in voices:
            voice.on = False
    result = {
        "lengths": lengths,
        "starts": starts,
        "voices": voices,
        "clock": clock,
        "events": events,
        "error": error,
        "capped": events >= MAX_EVENTS,
        "tempo": tempo,
        "utl": utl,
        "details": details,
    }
    return result, trace


def report(path, result, trace, trace_cmd):
    print("%s: %d bytes, voices at %d/%d/%d"
          % (os.path.basename(path), sum(result["lengths"]) + 8,
             result["starts"][0], result["starts"][1], result["starts"][2]))
    for voice in result["voices"]:
        print("  voice %d: %d events, %d notes, tick %d%s"
              % (voice.index + 1, voice.events, voice.notes, voice.tick,
                 "" if voice.on else ", stopped"))
    print("  total: %d ticks, %d events%s" % (result["clock"], result["events"],
                                              ", CAPPED (the tune loops)" if result["capped"] else ""))
    print("  tempo $%02X, UTL %d, UTV %d/%d/%d"
          % (result["tempo"], result["utl"],
             result["voices"][0].utv, result["voices"][1].utv, result["voices"][2].utv))
    if result["error"]:
        print("  player would stop: %s" % result["error"])
    if trace:
        print("trace (tick, voice, image offset, bytes%s):" % (", command" if trace_cmd else ""))
        for tick, index, at, cmd, opt in trace:
            line = "t=%d v%d o=$%04X %02X %02X" % (tick, index + 1, at, cmd, opt)
            if trace_cmd:
                line += "  " + command_name(cmd, opt)
            print(line)


def pitch_name(opt):
    """The three characters musedit puts at a note's onset."""
    letter = opt & 0x07
    if letter == 0:
        return None                 # a rest
    semitone = SEMITONES[letter]
    accidental = opt & 0xC0
    if accidental == 0x40:
        semitone += 1
    elif accidental == 0xC0:
        semitone -= 1
    octave = 7 - ((opt >> 3) & 0x07)     # the field is stored inverted
    return "%s%d" % (NOTE_NAMES[semitone % 12], octave)


def grid(result, rows):
    """The first `rows` rows of the three lanes, as musedit draws them.

    The row unit is the shortest plain length the tune's opening tempo can
    express, which is the rule the editor uses: a note's cells run from its onset
    row to the row its length reaches, a rest shows only its onset.
    """
    model = timing()
    unit, per_bar = model.unit_and_rows(result["tempo"])
    print("grid: unit %d jiffies, %d rows per bar" % (unit, per_bar))
    blank, bar = 0, 1
    kinds = [[blank] * rows for _ in range(3)]
    text = [[""] * rows for _ in range(3)]
    row = [0, 0, 0]
    rem = [0, 0, 0]
    for tick, index, at, cmd, opt, length in result["details"]:
        total = rem[index] + length
        span = total // unit
        here = row[index]
        if (opt & 0x07) == 0:
            if here < rows:
                kinds[index][here] = 2
                text[index][here] = "..."
        else:
            if here < rows:
                kinds[index][here] = 3
                text[index][here] = pitch_name(opt)
            for step in range(1, span):
                if here + step < rows:
                    kinds[index][here + step] = bar
        row[index] += span
        rem[index] = total % unit
    for at in range(rows):
        line = []
        for index in range(3):
            if kinds[index][at] == bar:
                line.append("   " + "=" * 9)
            elif kinds[index][at] == blank:
                line.append(" " * 12)
            else:
                line.append(text[index][at].ljust(3) + "=" * 9)
        print("  %02d |%s|%s|%s|" % (at + 1, line[0], line[1], line[2]))



def main(argv):
    if len(argv) < 2:
        print("usage: mus-sim.py <file.mus> [--trace N] [--cmd] [--grid N]")
        return 2
    path = argv[1]
    limit = 0
    trace_cmd = False
    grid_rows = 0
    index = 2
    while index < len(argv):
        if argv[index] == "--trace":
            index += 1
            limit = int(argv[index])
        elif argv[index] == "--cmd":
            trace_cmd = True
        elif argv[index] == "--grid":
            index += 1
            grid_rows = int(argv[index])
        else:
            raise SystemExit("mus-sim: unknown option %s" % argv[index])
        index += 1
    image = open(path, "rb").read()
    if len(image) < 8:
        raise SystemExit("mus-sim: %s is too short to be a tune" % path)
    result, trace = simulate(image, limit, trace_cmd)
    report(path, result, trace, trace_cmd)
    if grid_rows:
        grid(result, grid_rows)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
