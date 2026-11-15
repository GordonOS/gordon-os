#!/usr/bin/env python3
"""mus-sim.py -- the Sidplayer's control flow, modelled on the host.

musedit's grid is a timeline derived from the player's own walk of the .mus
stream, so the reference model lives here: this script reads a tune the way
src/libs/sidplay.inc does -- three voices, a per-voice countdown, HED/TAL,
DEF/CALL/END, HLT, and the global tempo plus UTL/UTV -- and reports when every
event plays.

Rows are counted the way the editor counts them: `row(tick)` is a piecewise
linear function of the song tick with one segment per tempo change, and a bar is
one whole note in the segment's own rows. `--grid` and `--lines` print what the
screen shows, so a change of tempo can be checked line for line against musedit.

The trace it prints is the trace musedit's `X` key prints, line for line, which
is how the two are compared:

    python tools/mus-sim.py music/tetris.mus --trace 20

`--walk` prints the same lines with the song tick each one was walked at -- the
value musedit records in MU_LNTICK and the playhead looks its own row up by -- so
a running editor's line ring can be compared against it line for line:

    python tools/mus-sim.py music/commodo.mus --walk 40

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


MARK = {
    "TEM": 1, "UTL": 2, "UTV": 3, "HED": 4, "DEF": 5, "CALL": 6,
    "TAL": 7, "HLT": 8, "END": 9, "F-C": 10, "PNT": 11, "RTP": 12,
    "HLD": 13, "P-S": 14, "F-S": 15, "SCA": 16, "VDP": 17, "VRT": 18,
    "AUT": 19, "TPS": 20, "PVD": 21, "PVR": 22, "MAX": 23, "P-W": 24,
    "DTN": 25, "POR": 26, "ATK": 27, "DCY": 28, "SUS": 29, "REL": 30,
    "VOL": 31, "RES": 32, "WAV": 33, "F-M": 34, "BMP": 35, "FLT": 36,
    "RNG": 37, "SNC": 38, "F-X": 39, "3-O": 40, "LFO": 41, "P&V": 42,
    "RUP": 43, "RDN": 44, "SRC": 45, "DST": 46, "MS#": 47, "FLG": 48,
    "AUX": 49,
}
NAMES = dict((ident, name) for name, ident in MARK.items())

#  The commands with a fixed command byte, indexed by command byte / 8. A zero is a
#  value of that eight that names no command, or one the grid does not draw. MS#
#  takes four indices, one per value of its high byte; JIF takes none, because its
#  value is the option * 4 plus those two bits.
EXACT = {0: "TEM", 1: "F-C", 2: "UTL", 3: "MS#", 4: "PNT", 5: "RTP",
         6: "HED", 8: "FLG", 9: "HLD", 10: "P-S", 11: "MS#", 12: "F-S",
         13: "SCA", 14: "VDP", 16: "VRT", 18: "AUT", 19: "MS#", 20: "TPS",
         22: "AUX", 24: "PVD", 26: "PVR", 27: "MS#", 28: "MAX", 30: "UTV"}
#  The eight whose option ends in 011, indexed by option / 16. The index is what
#  the player's own shift chain tests, which is why RNG sits at 2 even though its
#  comment at the source reads 0101n011.
SW = ["BMP", "FLT", "RNG", "SNC", "F-X", "3-O", "LFO", "P&V"]
NO_VALUE = (7, 8, 9)                     # TAL, HLT, END


def decode_cmd(cmd, opt):
    """(id, value) for a command pair, (0, 0) for a pair the grid does not draw.

    Mirrors muLnDec in musedit.inc, which mirrors the player's routing: the command
    byte picks the family and the option picks the command, in an order that keeps
    the overlapping patterns straight."""
    if cmd & 7 == 6:                     # fixed-byte commands, value = option
        name = EXACT.get(cmd >> 3)
        if not name:
            return 0, 0
        if name == "MS#":                # its high byte rides in bits 7-6
            return MARK[name], ((cmd >> 6) << 8) | opt
        return MARK[name], opt
    if cmd & 0x0F == 0x02:
        return MARK["P-W"], ((cmd >> 4) << 8) | opt
    if cmd & 0x0F == 0x0A:
        return MARK["DTN"], ((cmd >> 4) << 8) | opt
    if cmd & 3 == 3:
        return MARK["POR"], ((cmd >> 2) << 8) | opt
    if cmd & 3 != 1:                     # a 2-type command: nothing the grid shows
        return 0, 0
    if opt == 0x0F:
        return MARK["TAL"], 0
    if opt == 0x2F:
        return MARK["END"], 0
    if opt == 0x4F:
        return MARK["HLT"], 0
    low = opt & 7
    if low == 1:
        return MARK["RUP"], (opt >> 3) & 0x1F
    if low == 5:
        return MARK["RDN"], (opt >> 3) & 0x1F
    if low == 0:
        return MARK["DCY"], opt >> 4
    if low == 2:
        return MARK["RES" if opt & 8 else "CALL"], opt >> 4
    if low == 6:
        return MARK["VOL" if opt & 8 else "DEF"], opt >> 4
    if low == 4:
        return MARK["SUS" if opt & 0x80 else "ATK"], opt >> 4
    if low == 3:
        index = opt >> 4
        if index < 8:
            return MARK[SW[index]], (opt >> 3) & 1
        return MARK["CALL" if opt & 8 else "DEF"], 16 + (index & 7)
    if opt & 0x0F == 0x0F:               # the 1111 group
        if opt & 0x10:
            return MARK["SRC"], opt >> 5
        return MARK["DST"], (opt >> 5) & 3
    return MARK["F-M" if opt & 0x1F == 0x17 else "WAV"], opt >> 5


def command_name(cmd, opt):
    """The text the grid draws for a command pair, or None when it draws nothing."""
    ident, value = decode_cmd(cmd, opt)
    if not ident:
        return None
    name = NAMES[ident]
    if ident in NO_VALUE:
        return name
    return "%s  %d" % (name, value)


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
    temps = []                  # (tick, tempo) for every change the walk honours
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
    skipped = 0
    ended = False

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
                    if cmd == 0:
                        # The player's own rule (sidplay.inc, before its length
                        # logic): a duration byte of 0 skips the event -- no
                        # length, no gate -- so no time passes and the next pair
                        # is read. musedit's walk skips it the same way.
                        skipped += 1
                        continue
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
                    if opt != tempo:
                        temps.append((clock, opt))
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
                            # The player's stopSong: it sets SID_STATUS bit 5 and
                            # RETURNS, so the voice carries on at the pair after the
                            # END. It does not stop the song.
                            ended = True
                            continue
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
        "skipped": skipped,
        "error": error,
        "ended": ended,
        "capped": events >= MAX_EVENTS,
        "tempo": tempo,
        "utl": utl,
        "temps": temps,
        "details": details,
    }
    return result, trace


def segments(temps):
    """The row map: one segment per tempo change, as the editor walks it.

    row(tick) = row0 + (tick - tick0) / unit, one segment per tempo. A change takes
    effect at its own tick, so the map is continuous there; the segment's unit is
    the finest length that tempo can express, and a bar is one whole note counted
    in those rows, which makes `rows * unit == tempo`.
    """
    model = timing()
    unit, bar = model.unit_and_rows(INIT_TEMPO)
    out = [{"tick": 0, "row": 0, "unit": unit, "bar": bar, "tempo": INIT_TEMPO}]
    for tick, tempo in temps:
        here = out[-1]
        row = here["row"] + (tick - here["tick"]) // here["unit"]
        unit, bar = model.unit_and_rows(tempo)
        seg = {"tick": tick, "row": row, "unit": unit, "bar": bar, "tempo": tempo}
        if tick == here["tick"]:
            out[-1] = seg           # a change at a segment's own start replaces it
        else:
            out.append(seg)
    return out


def row_of(segs, tick):
    """The row a tick falls on, and the segment it is in."""
    found = segs[0]
    for seg in segs:
        if seg["tick"] <= tick:
            found = seg
        else:
            break
    return found["row"] + (tick - found["tick"]) // found["unit"], found


def bars(segs, last_row):
    """[(first_row, rows, unit)] for the bars up to LAST_ROW.

    A change starts a bar wherever it lands, so one that falls inside a bar cuts it
    short; after that the bars are the segment's own length.
    """
    out = []
    for index, seg in enumerate(segs):
        final = index + 1 == len(segs)
        end = last_row + 1 if final else segs[index + 1]["row"]
        row = seg["row"]
        while row < end and row <= last_row:
            out.append((row, seg["bar"]))
            row += seg["bar"]
    return out


def bar_of(bar_list, row):
    """The 1-based bar a row falls in, and the row's number inside it."""
    for index in range(len(bar_list) - 1, -1, -1):
        first, size = bar_list[index]
        if row >= first:
            return index + 1, row - first + 1
    return 1, row + 1


def report(path, result, trace, trace_cmd):
    print("%s: %d bytes, voices at %d/%d/%d"
          % (os.path.basename(path), sum(result["lengths"]) + 8,
             result["starts"][0], result["starts"][1], result["starts"][2]))
    for voice in result["voices"]:
        print("  voice %d: %d events, %d notes, tick %d%s"
              % (voice.index + 1, voice.events, voice.notes, voice.tick,
                 "" if voice.on else ", stopped"))
    print("  total: %d ticks, %d events%s%s" % (result["clock"], result["events"],
                                               ", %d skipped (duration 0)" % result["skipped"]
                                               if result["skipped"] else "",
                                               ", CAPPED (the tune loops)" if result["capped"] else ""))
    print("  tempo $%02X, UTL %d, UTV %d/%d/%d"
          % (result["tempo"], result["utl"],
             result["voices"][0].utv, result["voices"][1].utv, result["voices"][2].utv))
    if result["error"]:
        print("  player would stop: %s" % result["error"])
    if result["ended"]:
        print("  an END outside any call flagged the tune's end (the player carries on)")
    if trace:
        print("trace (tick, voice, image offset, bytes%s):" % (", command" if trace_cmd else ""))
        for tick, index, at, cmd, opt in trace:
            line = "t=%d v%d o=$%04X %02X %02X" % (tick, index + 1, at, cmd, opt)
            if trace_cmd and cmd & 3:
                line += "  " + (command_name(cmd, opt) or "*")
            print(line)


def pitch_name(opt):
    """The three characters musedit puts at a note's onset.

    The pitch read is the one the option SOUNDS, so an accidental that takes it past
    the octave rolls the way the player's note path does: a sharp on B is the next
    octave's C and a flat on C the octave below's B. At either end of the three-bit
    octave field there is no octave to move into, so only the semitone rolls -- which
    is what the player itself does with such an option.
    """
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
    if semitone < 0:
        semitone += 12
        octave = max(octave - 1, 0)
    elif semitone > 11:
        semitone -= 12
        octave = min(octave + 1, 7)
    return "%s%d" % (NOTE_NAMES[semitone], octave)


def grid(result, rows):
    """The first `rows` rows of the three lanes, as musedit draws them.

    A row is the finest length the tempo in force can express, so the map is one
    segment per tempo change and a note's cells run from its onset row to the row
    its length reaches; a rest shows only its onset.
    """
    segs = segments(result["temps"])
    print("segments: " + ", ".join(
        "tick %d row %d unit %d bar %d" % (seg["tick"], seg["row"],
                                           seg["unit"], seg["bar"]) for seg in segs))
    bar_list = bars(segs, rows)
    blank, bar = 0, 1
    kinds = [[blank] * rows for _ in range(3)]
    text = [[""] * rows for _ in range(3)]
    for tick, index, at, cmd, opt, length in result["details"]:
        here, _seg = row_of(segs, tick)
        reach, _end = row_of(segs, tick + length)
        if (opt & 0x07) == 0:
            if here < rows:
                kinds[index][here] = 2
                text[index][here] = "..."
        else:
            if here < rows:
                kinds[index][here] = 3
                text[index][here] = pitch_name(opt)
            for step in range(here + 1, min(reach, rows)):
                kinds[index][step] = bar
    for at in range(rows):
        line = []
        for index in range(3):
            if kinds[index][at] == bar:
                line.append("   " + "=" * 9)
            elif kinds[index][at] == blank:
                line.append(" " * 12)
            else:
                line.append(text[index][at].ljust(3) + "=" * 9)
        number, inside = bar_of(bar_list, at)
        print("  %03d.%02d |%s|%s|%s|" % (number, inside, line[0], line[1], line[2]))



DUR_NUM = (64, 0, 1, 2, 4, 8, 16, 32)   # by length class: muDurNum


def layout(result, trace, count, walk=False):
    """The editor's display lines: a time row, or one structural command.

    A grid line is one of two things. A **time row** carries every voice's note that
    starts on it, so the three lanes line up. A **structural command** -- `TEM`,
    `UTL`, `UTV`, `HED`, `TAL`, `END`, `HLT`, `DEF`, `CALL` -- moves no time at all,
    so it gets a line of its own and the other two lanes are empty on it. A row's
    commands are drawn above it and its notes on the row's own line, which is the
    line the playhead stops on.

    The lanes are MU_LANE_W (11) cells: the pitch in 0-2, a space, the duration in
    4-5, and the editor's own mark in the last cell. A command line has its mnemonic
    from cell 0 and nothing else.

    `walk` adds the tick the editor records for the line (MU_LNTICK), which is what
    the playhead looks its row up by. muStep sets muClkLo/Hi to the clock of the
    event it runs and muLnRowDraw stores it, so a row's tick is its LAST event's
    clock -- and every event that starts on the row shares it. A row with no events
    runs no muStep, so its line keeps the previous row's value; a command line is
    never consulted (muPhSync skips a line that holds no row) and prints `-`.
    """
    model = timing()
    segs = segments(result["temps"])
    notes = {}
    cmds = [[[], []], [[], []], [[], []]]   # [voice][class]: the two never share a line
    out = []
    row = 0
    tick = None                 # the clock of the last event in the group being built
    last = 0                    # ...and the value the previous group's line holds

    def lane(voice):
        text, cmd_byte = notes[voice]
        cells = [" "] * 11
        cells[0:3] = list(text)
        number = DUR_NUM[(cmd_byte >> 2) & 7]
        if number:
            digits = str(number)
            cells[4:4 + len(digits)] = list(digits)
        return "".join(cells)

    def flush():
        nonlocal tick, last
        stamp = last if tick is None else tick
        for cls in (1, 0):          # the sound commands first, then the structural
            depth = max(len(cmds[voice][cls]) for voice in range(3))
            for k in range(depth):
                lanes = [(voice, cmds[voice][cls][k])
                         for voice in range(3) if k < len(cmds[voice][cls])]
                out.append((None, [(voice, (text + " " * 11)[:11]) for voice, text in lanes],
                            stamp))
        lanes = [(voice, lane(voice)) for voice in range(3) if voice in notes]
        out.append((row, lanes, stamp))
        last = stamp
        tick = None
        for voice in range(3):
            for cls in (0, 1):
                del cmds[voice][cls][:]
        notes.clear()

    for clock, voice, at, cmd, opt in trace:
        here, _seg = row_of(segs, clock)
        while row < here:
            flush()
            row += 1
        tick = clock
        if cmd & 3:
            ident, value = decode_cmd(cmd, opt)
            if ident:
                cls = 0 if ident < 10 else 1
                if len(cmds[voice][cls]) < 16:
                    cmds[voice][cls].append(command_name(cmd, opt))
        else:
            notes[voice] = (pitch_name(opt) or "...", cmd)
    flush()

    bar_list = bars(segs, row)
    if walk:
        print("walk: %d segments, %d lines (tick = MU_LNTICK, the row's own clock)"
              % (len(segs), len(out)))
        print("  %5s %-6s |%s|%s|%s|" % ("tick", "bar", " " * 11, " " * 11, " " * 11))
        for at, lanes, stamp in out[:count]:
            cells = [" " * 11] * 3
            for voice, text in lanes:
                cells[voice] = text
            if at is None:
                shown, label = "%5s" % "-", "-"   # a command line: no row, no tick
            else:
                number, inside = bar_of(bar_list, at)
                shown = "%5d" % stamp
                label = "%03d.%02d" % (number, inside)
            print("  %s %-6s |%s|%s|%s|" % (shown, label, cells[0], cells[1], cells[2]))
        return

    print("lines: %d segments, %d lines" % (len(segs), len(out)))
    for at, lanes, _stamp in out[:count]:
        cells = [" " * 11] * 3
        for voice, text in lanes:
            cells[voice] = text
        if at is None:
            label = "     "
        else:
            number, inside = bar_of(bar_list, at)
            label = "%03d.%02d" % (number, inside)
        print("  %s |%s|%s|%s|" % (label, cells[0], cells[1], cells[2]))


def main(argv):
    if len(argv) < 2:
        print("usage: mus-sim.py <file.mus> [--trace N] [--cmd] [--grid N] [--lines N]"
              " [--walk N]")
        return 2
    path = argv[1]
    limit = 0
    trace_cmd = False
    grid_rows = 0
    line_rows = 0
    walk_rows = 0
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
        elif argv[index] == "--lines":
            index += 1
            line_rows = int(argv[index])
        elif argv[index] == "--walk":
            index += 1
            walk_rows = int(argv[index])
        else:
            raise SystemExit("mus-sim: unknown option %s" % argv[index])
        index += 1
    image = open(path, "rb").read()
    if len(image) < 8:
        raise SystemExit("mus-sim: %s is too short to be a tune" % path)
    if line_rows or walk_rows:
        #  The layout needs every event, not a trace excerpt: notes and commands.
        limit, trace_cmd = 1000000, True
    result, trace = simulate(image, limit, trace_cmd)
    report(path, result, trace, trace_cmd)
    if grid_rows:
        grid(result, grid_rows)
    if line_rows:
        layout(result, trace, line_rows)
    if walk_rows:
        layout(result, trace, walk_rows, walk=True)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
