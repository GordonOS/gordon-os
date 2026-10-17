#!/usr/bin/env python3
"""
gen-mus.py -- write (and read) COMPUTE!'s Enhanced Sidplayer .mus tunes.

Why this exists
---------------
`sid2mus` does not exist (0 hits on GitHub, nothing on the web), and it could not
work anyway: a .sid is a *program* with its own player, while a .mus is a *score*
for one specific interpreter. So GordonOS authors its own tunes instead of
converting anybody else's.

The format
----------
`reference/sidplayer-mus-file-format.md` documents it, and every encoding below
was re-derived from the player body itself (`src/libs/sidplay.inc`) rather than
trusted from the doc -- the doc has two typos (DCY/REL option patterns) and the
routing labels in the source are the authority. File layout:

    +0   2 bytes  PRG load address (ignored by players; commodo.mus uses $C000)
    +2   2 bytes  voice 1 data size   |
    +4   2 bytes  voice 2 data size   | little-endian, counted in BYTES
    +6   2 bytes  voice 3 data size   |
    +8   ...      voice 1 data, voice 2 data, voice 3 data
    then          NUL-terminated description text

A voice's data is a stream of two-byte (command, option) pairs ending in HLT.
A pair is a NOTE when the command byte's bits 0-1 are 00:

    command byte = dur_code<<2 | double-dotted<<7 | tie<<6 | dotted<<5
                   dur codes: 2=whole 3=half 4=quarter 5=8th 6=16th 7=32nd 0=64th
    option  byte = accidental<<6 | (7-octave)<<3 | note
                   accidental: 2=normal 1=sharp 3=flat (bits 6-7)
                   note:  0=rest 1=C 2=D 3=E 4=F 5=G 6=A 7=B
                   octave is stored INVERTED (the frequency table starts at C-7
                   and the player halves it `octave` times)

Everything else is a command; see CMD_* below. Note durations are relative to the
TEM tempo: at tempo t a whole note lasts t>>0 jiffies, a half t>>1, a quarter
t>>2, an 8th t>>3, a 16th t>>4, a 32nd t>>5 (the player shifts the tempo byte),
so a tempo that is a multiple of 32 keeps 16ths exact and every duration even
enough to be dotted.

Usage
-----
    # write the shipped tune: melody and bass read from a MIDI, chords and
    # voicing derived, the SID colour applied here. The tune's form is A A B: the
    # MIDI writes the opening phrase once, and --repeat-first plays it twice
    python tools/gen-mus.py --from-midi "Korobeiniki Tetris Theme.mid" --bars 16

    # The MIDI is made from a fan piano sheet -- NinSheetMusic sheet 574,
    # "Tetris - A Theme (Korobeiniki)", piano arrangement by Olimar12345,
    # credited there to Hirokazu Tanaka's game arrangement, Nintendo (C) 1989.
    # Only the melody and the bass line come from it; the sheet, its PDF and the
    # MIDI are not redistributed. See NOTICE and reference/tetris/UPSTREAM.md.

    python tools/gen-mus.py --build  music/tetris.mus   # the hand-written score
    python tools/gen-mus.py --decode music/tetris.mus   # listing, for checking
    python tools/gen-mus.py --inspect-midi FILE         # tracks, ranges, timing
    python tools/gen-mus.py --midi-bars   FILE          # top voice bar by bar
    python tools/gen-mus.py --midi-phrases FILE         # melody + phrase labels
"""

import argparse
import sys

# ----------------------------------------------------------------------------
#  Encoding primitives
# ----------------------------------------------------------------------------

# Note duration codes (command byte bits 2-4).
DUR = {'x': 0, 'w': 2, 'h': 3, 'q': 4, 'e': 5, 's': 6, 't': 7}
DUR_NAME = {0: '64th', 2: 'whole', 3: 'half', 4: 'quarter', 5: '8th', 6: '16th', 7: '32nd'}

DOTTED, TIE, DDOTTED = 0x20, 0x40, 0x80

# Note field, option bits 0-2.
NOTE = {'C': 1, 'D': 2, 'E': 3, 'F': 4, 'G': 5, 'A': 6, 'B': 7}
NOTE_R = {v: k for k, v in NOTE.items()}

# Accidental field, option bits 6-7.
ACC_NORMAL, ACC_SHARP, ACC_FLAT = 2, 1, 3
ACC_NAME = {2: '', 1: '#', 3: 'b'}

# Waveform codes for the WAV command (SID voice control bits).
WAV_NOISE, WAV_TRI, WAV_SAW, WAV_TS = 0, 1, 2, 3
WAV_PULSE, WAV_TP, WAV_SP, WAV_TSP = 4, 5, 6, 7
WAV_NAME = {0: 'noise', 1: 'tri', 2: 'saw', 3: 'tri+saw',
            4: 'pulse', 5: 'tri+pulse', 6: 'saw+pulse', 7: 'tri+saw+pulse'}

# The player's own HALT pair, and the default tempo it starts with.
HLT = (0x01, 0x4F)
DEFAULT_TEMPO = 0x90


def parse_pitch(name):
    """'A4' / 'C#5' / 'Bb3' -> (note letter, accidental, octave)."""
    letter = name[0].upper()
    if letter not in NOTE:
        raise ValueError(f"bad note {name!r}")
    rest = name[1:]
    acc = ACC_NORMAL
    if rest and rest[0] in '#+-':
        acc, rest = ACC_SHARP, rest[1:]
    elif rest and rest[0] in 'bB':
        acc, rest = ACC_FLAT, rest[1:]
    if not rest.isdigit():
        raise ValueError(f"bad octave in {name!r}")
    return letter, acc, int(rest)


def pitch_to_midi(name):
    """Absolute pitch as a MIDI note number, for the harmony transposition."""
    letter, acc, octave = parse_pitch(name)
    semis = {'C': 0, 'D': 2, 'E': 4, 'F': 5, 'G': 7, 'A': 9, 'B': 11}[letter]
    semis += -1 if acc == ACC_FLAT else (1 if acc == ACC_SHARP else 0)
    return 12 * (octave + 1) + semis


def midi_to_pitch(n):
    """MIDI note number -> the sharp-spelled name this player wants."""
    names = ['C', 'C#', 'D', 'D#', 'E', 'F', 'F#', 'G', 'G#', 'A', 'A#', 'B']
    return f"{names[n % 12]}{n // 12 - 1}"


def note_event(pitch, dur='q', dotted=False, tie=False):
    """One (command, option) pair for a note or a rest (pitch None)."""
    code = DUR[dur]
    cmd = (code << 2) | (DOTTED if dotted else 0) | (TIE if tie else 0)
    if pitch is None:
        opt = ACC_NORMAL << 6 | 0 << 3 | 0
    else:
        letter, acc, octave = parse_pitch(pitch)
        if not 0 <= octave <= 7:
            raise ValueError(f"octave out of range in {pitch!r}")
        opt = (acc << 6) | ((7 - octave) << 3) | NOTE[letter]
    return (cmd, opt)


# ----------------------------------------------------------------------------
#  Commands (command byte, option byte) -- all patterns taken from the player
# ----------------------------------------------------------------------------

def c_tem(tempo):
    """TEM: tempo byte. Quarter-note jiffies = tempo>>2 (so $30 = 300 M.M.)."""
    return (0x06, tempo)


def c_vol(vol):
    return (0x01, (vol & 0x0F) << 4 | 0x0E)


def c_atk(v):
    return (0x01, ((v & 0x0F) << 3) | 0x04)


def c_dcy(v):
    return (0x01, (v & 0x0F) << 4)


def c_sus(v):
    return (0x01, ((v & 0x0F) << 3) | 0x84)


def c_rel(v):
    return (0x01, ((v & 0x0F) << 4) | 0x08)


def c_wav(wave):
    return (0x01, ((wave & 0x07) << 5) | 0x07)


def c_pw(width):
    """Pulse width 0-4095: high nibble in the command byte, low byte in the option."""
    return (((width >> 8) & 0x0F) << 4 | 0x02, width & 0xFF)


def c_ps(sweep):
    return (0x56, sweep & 0xFF)


def c_fc(cutoff):
    return (0x0E, cutoff & 0xFF)


def c_res(v):
    return (0x01, ((v & 0x0F) << 4) | 0x0A)


def c_fm(mode):
    return (0x01, ((mode & 0x07) << 5) | 0x17)


def c_vrt(rate):
    return (0x86, rate & 0x7F)


def c_vdp(depth):
    return (0x76, depth & 0x7F)


def c_utl(jiffies):
    return (0x16, jiffies & 0xFF)


def c_hed(count):
    """Start of a repeat section; 0 = repeat forever."""
    return (0x36, count & 0xFF)


TAL = (0x01, 0x0F)


def c_voice3_on(on=True):
    """3-O: voice 3 off in $D418 bit 7. The player starts with it OFF."""
    return (0x01, 0x53 | (0x08 if on else 0x00))


# ----------------------------------------------------------------------------
#  Decoder -- mirrors the player's routing so a real file can be checked
# ----------------------------------------------------------------------------

# Command bytes that the routing labels pin to exactly one command.
EXACT = {
    0x06: ('TEM', lambda o: f"tempo ${o:02X} (quarter = {o >> 2} jiffies)"),
    0x0E: ('F-C', lambda o: f"filter cutoff {o}"),
    0x16: ('UTL', lambda o: f"utility length {o}"),
    0x26: ('PNT', lambda o: f"release point {o}"),
    0x2E: ('RTP', lambda o: f"relative transpose {o}"),
    0x36: ('HED', lambda o: f"repeat section {'forever' if o == 0 else o}x"),
    0x46: ('FLG', lambda o: f"flag {o}"),
    0x4E: ('HLD', lambda o: f"hold time {o}"),
    0x56: ('P-S', lambda o: f"pulse sweep {o}"),
    0x66: ('F-S', lambda o: f"filter sweep {o}"),
    0x6E: ('SCA', lambda o: f"mod scale {o}"),
    0x76: ('VDP', lambda o: f"vibrato depth {o}"),
    0x86: ('VRT', lambda o: f"vibrato rate {o}"),
    0x96: ('AUT', lambda o: f"auto filter {o}"),
    0xA6: ('TPS', lambda o: f"transpose {o}"),
    0xB6: ('AUX', lambda o: f"aux {o}"),
    0xC6: ('PVD', lambda o: f"pulse vibrato depth {o}"),
    0xD6: ('PVR', lambda o: f"pulse vibrato rate {o}"),
    0xE6: ('MAX', lambda o: f"modulation max {o}"),
    0xF6: ('UTV', lambda o: f"utility length/voice {o}"),
}


def decode_pair(cmd, opt):
    """(name, text) for one pair, or ('?', hex) when the routing is not modelled.

    Mirrors RouteCommand: the command byte is tested low bit first, and for the
    `01` family the OPTION byte is what selects the command, so the exact command
    bytes must be checked before the family tests or e.g. HLT ($01 $4F) reads as
    DST (whose option pattern is also 0F)."""
    if cmd & 0x03 == 0:
        dur = (cmd >> 2) & 0x07
        name = DUR_NAME.get(dur, f'dur{dur}')
        if cmd & DDOTTED:
            name = 'double-dotted ' + name
        if cmd & DOTTED:
            name = 'dotted ' + name
        acc = (opt >> 6) & 0x03
        note = opt & 0x07
        octave = 7 - ((opt >> 3) & 0x07)
        pitch = 'rest' if note == 0 else f"{NOTE_R[note]}{ACC_NAME.get(acc, '?')}{octave}"
        tie = ' ~' if cmd & TIE else ''
        return ('note', f"{pitch:<7} {name}{tie}")

    if cmd in EXACT:                        # commands with a fixed command byte
        n, f = EXACT[cmd]
        return (n, f(opt))
    signed = opt - 256 if opt > 127 else opt
    if (cmd & 0x3F) == 0x1E:                # MS#: 2 bits of measure + $1E
        return ('MS#', f"measure {(cmd >> 6) << 8 | opt}")
    if (cmd & 0x3F) == 0x3E:                # JIF: 2 bits + $3E
        return ('JIF', f"jiffy clock {signed}")
    if (cmd & 0x0F) == 0x02:                # P-W: high nibble + option
        return ('P-W', f"pulse width ${((cmd >> 4) << 8) | opt:03X}")
    if (cmd & 0x0F) == 0x0A:                # DTN: high nibble + option
        return ('DTN', f"detune {signed}")
    if (cmd & 0x03) == 0x03:                # POR: 6 bits + option
        return ('POR', f"portamento {((cmd >> 2) << 8) | opt}")
    if (cmd & 0x03) != 0x01:                # everything below is the 01 family
        return ('?', f"cmd ${cmd:02X} opt ${opt:02X} (unmodelled)")

    # --- the 01 family: the option byte selects the command -----------------
    if opt == 0x0F:
        return ('TAL', 'tail (end of repeat section)')
    if opt == 0x4F:
        return ('HLT', 'halt voice')
    if opt == 0x2F:
        return ('END', 'end phrase definition')
    if opt in (0x53, 0x5B):
        return ('3-O', f"voice 3 disconnect bit {'set' if opt == 0x5B else 'clear'}")
    if (opt & 0x07) == 0x04:                # ATK / SUS
        if opt & 0x80:
            return ('SUS', f"sustain {(opt >> 3) & 0x0F}")
        return ('ATK', f"attack {(opt >> 3) & 0x0F}")
    if (opt & 0x0F) == 0x00:
        return ('DCY', f"decay {opt >> 4}")
    if (opt & 0x0F) == 0x08:
        return ('REL', f"release {opt >> 4}")
    if (opt & 0x0F) == 0x0E:
        return ('VOL', f"volume {opt >> 4}")
    if (opt & 0x0F) == 0x06:
        return ('DEF', f"define phrase {opt >> 4}")
    if (opt & 0x0F) == 0x02:
        return ('CALL', f"call phrase {opt >> 4}")
    if (opt & 0x0F) == 0x0A:
        return ('RES', f"resonance {opt >> 4}")
    if (opt & 0x0F) == 0x0F and opt & 0x80 and opt & 0x08:
        return ('DST', f"mod destination {opt & 0x03}")
    if (opt & 0x0F) == 0x0B:
        if opt & 0x40:
            return ('RNG', f"ring mod {'on' if opt & 0x10 else 'off'}")
        return ('FLT', f"filter through {'on' if opt & 0x10 else 'off'}")
    if (opt & 0x0F) == 0x03:
        if opt & 0x40:                      # 0110c011 LFO / 0111c011 P&V
            if opt & 0x20:
                return ('P&V', f"port+vibrato {'on' if opt & 0x10 else 'off'}")
            return ('LFO', f"lfo {1 if opt & 0x10 else 0}")
        return ('SNC', f"sync {'on' if opt & 0x10 else 'off'}")
    if (opt & 0x1F) == 0x07:
        return ('WAV', f"waveform {WAV_NAME.get(opt >> 5, opt >> 5)}")
    if (opt & 0x1F) == 0x17:
        return ('F-M', f"filter mode {opt >> 5}")
    if opt == 0x1F:
        return ('SRC', f"mod source {opt >> 5}")
    return ('?', f"cmd ${cmd:02X} opt ${opt:02X} (unmodelled)")


def decode_file(path, verbose=False):
    """Walk a .mus and print a listing. Returns (voices, description)."""
    data = open(path, 'rb').read()
    load = data[0] | data[1] << 8
    sizes = [data[2 + i * 2] | data[3 + i * 2] << 8 for i in range(3)]
    print(f"{path}: {len(data)} bytes, load ${load:04X}, voice sizes {sizes}")
    pos = 8
    voices = []
    for v in range(3):
        stream = data[pos:pos + sizes[v]]
        pos += sizes[v]
        pairs = [(stream[i], stream[i + 1]) for i in range(0, len(stream) - 1, 2)]
        voices.append(pairs)
        print(f"\n  voice {v + 1}: {sizes[v]} bytes, {len(pairs)} pairs")
        if not pairs or pairs[-1] != HLT:
            print(f"    !! does not end in HLT: {pairs[-1] if pairs else None}")
        if verbose:
            for i, (cmd, opt) in enumerate(pairs):
                name, text = decode_pair(cmd, opt)
                print(f"    {i:4d}  {cmd:02X} {opt:02X}  {name:<5} {text}")
        else:
            from collections import Counter
            counts = Counter(decode_pair(c, o)[0] for c, o in pairs)
            print("    " + ", ".join(f"{k}x{n}" for k, n in sorted(counts.items())))
    desc = data[pos:]
    text = desc.split(b'\x00')[0].decode('latin-1') if desc else ''
    print(f"\n  description: {text!r}   ({len(desc)} bytes incl. terminator)")
    if b'\x00' not in desc:
        print("    !! description is not NUL-terminated")
    return voices, text


# ----------------------------------------------------------------------------
#  MIDI import
# ----------------------------------------------------------------------------
#  A .mid is an actual score (notes, timing, tempo), so when one exists it is the
#  source of truth and the hand-written table below is only a fallback. What a
#  MIDI cannot give is the SID colour -- waveforms, envelopes, pulse widths -- and
#  it usually has more parts than the three the player has, so the importer picks
#  and folds voices and the arrangement still has to be voiced by hand.

def _varlen(data, i):
    value = 0
    while True:
        byte = data[i]
        i += 1
        value = (value << 7) | (byte & 0x7F)
        if not byte & 0x80:
            return value, i


def read_midi(path):
    """-> (division, [{name, notes: [(start_tick, pitch, dur_ticks, velocity)]}])."""
    data = open(path, 'rb').read()
    if data[0:4] != b'MThd':
        raise ValueError(f"{path}: not a MIDI file")
    division = int.from_bytes(data[12:14], 'big')
    if division & 0x8000:
        raise ValueError("SMPTE time division is not supported")
    i, tracks = 14, []
    while i < len(data) - 8:
        if data[i:i + 4] != b'MTrk':
            i += 1
            continue
        length = int.from_bytes(data[i + 4:i + 8], 'big')
        end = i + 8 + length
        i += 8
        tick, running, name = 0, None, ''
        open_notes, done = {}, []
        while i < end:
            delta, i = _varlen(data, i)
            tick += delta
            status = data[i]
            if status & 0x80:
                i += 1
                running = status
            else:
                status = running
            kind = status & 0xF0 if status is not None else 0
            if kind in (0x80, 0x90):
                pitch, velocity = data[i], data[i + 1]
                i += 2
                if kind == 0x90 and velocity > 0:
                    open_notes[pitch] = (tick, velocity)
                else:
                    started = open_notes.pop(pitch, None)
                    if started:
                        done.append((started[0], pitch, tick - started[0], started[1]))
            elif kind in (0xA0, 0xB0, 0xE0):
                i += 2
            elif kind in (0xC0, 0xD0):
                i += 1
            elif status == 0xFF:
                meta = data[i]
                i += 1
                size, i = _varlen(data, i)
                payload = data[i:i + size]
                i += size
                if meta == 0x03 and not name:
                    name = payload.decode('latin-1', 'replace')
            elif status in (0xF0, 0xF7):
                size, i = _varlen(data, i)
                i += size
            else:
                i += 1
        tracks.append({'name': name, 'notes': sorted(done)})
        i = end
    return division, tracks


def inspect_midi(path):
    division, tracks = read_midi(path)
    print(f"{path}: {len(tracks)} track(s), {division} ticks/quarter")
    for n, track in enumerate(tracks):
        notes = track['notes']
        if not notes:
            print(f"  track {n}: {track['name']!r} -- no notes")
            continue
        pitches = [p for _, p, _, _ in notes]
        start = min(s for s, _, _, _ in notes)
        end = max(s + d for s, _, d, _ in notes)
        print(f"  track {n}: {track['name']!r} -- {len(notes)} notes, "
              f"{midi_to_pitch(min(pitches))}-{midi_to_pitch(max(pitches))}, "
              f"{(end - start) / division:.1f} quarters")
        for s, p, d, v in notes[:10]:
            print(f"      tick {s:6d}  {midi_to_pitch(p):<4} dur {d:5d} "
                  f"({d / division:.2f} q)  vel {v}")
    return division, tracks


def midi_bar_map(path):
    """Print the MIDI a bar at a time: the top voice in eighth-note slots, plus
    the lowest note in the bar. Enough to see phrases, repeats and where the
    sections change without reading 1000 notes."""
    division, tracks = read_midi(path)
    notes = [n for track in tracks for n in track['notes']]
    bar_ticks = division * 4
    total = int(max(s + d for s, _, d, _ in notes) / bar_ticks) + 1
    print(f"{path}: {len(notes)} notes, {total} bars of 4/4 "
          f"({division * 4} ticks/bar)")
    for bar in range(total):
        lo, hi = bar * bar_ticks, (bar + 1) * bar_ticks
        inside = [n for n in notes if lo <= n[0] < hi]
        if not inside:
            continue
        slots = []
        for slot in range(8):                       # eighth-note positions
            t0 = lo + slot * (bar_ticks // 8)
            t1 = t0 + bar_ticks // 8
            in_slot = [n for n in inside if t0 <= n[0] < t1]
            slots.append(max(in_slot, key=lambda n: n[1])[1] if in_slot else None)
        top = ' '.join(midi_to_pitch(p) if p else ' . ' for p in slots)
        lowest = min(inside, key=lambda n: n[1])
        print(f"  bar {bar + 1:3d}  top {top}   lowest {midi_to_pitch(lowest[1])}")


def midi_phrases(path, block_bars=8):
    """Print the melody (top velocity class) bar by bar as pitch:length-in-8ths,
    with each block of bars labelled by comparing it with the blocks before it.

    The accompaniment has to be filtered out by velocity, not by register: this
    arrangement's left hand reaches up to A4, which is inside the melody's range,
    so "highest note" alone labels two statements of the same phrase as different
    ones.
    """
    division, tracks = read_midi(path)
    notes = [n for track in tracks for n in track['notes']]
    top_vel = max(v for _, _, _, v in notes)
    melody = [n for n in notes if n[3] >= top_vel - 4]
    bar_ticks = division * 4
    total = int(max(s + d for s, _, d, _ in notes) / bar_ticks) + 1
    eighth = bar_ticks // 8
    print(f"{path}: {total} bars of 4/4, melody = {len(melody)} of {len(notes)} "
          f"notes (velocity {top_vel - 4}+)")

    lines, blocks, labels = [], [], []
    for bar in range(total):
        lo, hi = bar * bar_ticks, (bar + 1) * bar_ticks
        inside = sorted([n for n in melody if lo <= n[0] < hi])
        parts, slots = [], [None] * 8
        for start, pitch, dur, _ in inside:
            eighths = max(1, round(dur / eighth))
            parts.append(f"{midi_to_pitch(pitch)}:{eighths}")
            for s in range(round((start - lo) / eighth), min(8, round((start - lo) / eighth) + eighths)):
                slots[s] = pitch
        lines.append((bar, parts, slots))
    for i in range(0, total, block_bars):
        sig = [p for _, _, slots in lines[i:i + block_bars] for p in slots]
        label = None
        for j in range(0, i, block_bars):
            if sig == [p for _, _, slots in lines[j:j + block_bars] for p in slots]:
                label = labels[j // block_bars]
                break
        if label is None:
            label = chr(ord('A') + len(set(labels)))
        labels.append(label)
    for (bar, parts, _), label in zip(lines, [labels[bar // block_bars] for bar in range(total)]):
        if not parts:
            continue
        if bar % block_bars == 0:
            print(f"\n  --- phrase {labels[bar // block_bars]} (bar {bar + 1}) ---")
        print(f"    bar {bar + 1:3d}  " + ' '.join(parts))
    print("\n  form: " + ' '.join(labels[::block_bars]))



#  Sixteen bars in A minor. The tune's phrase is eight bars; the game
#  arrangements play it twice and give the second time a different backing, and
#  that is what makes the loop feel like it goes somewhere -- so bars 9-16 are
#  the same melody over a warmer, higher-voiced harmony, a bass that moves twice
#  a bar, and more colour on the lead.
#
#  Every bar must add up to four beats, and `check_score` asserts it. That is not
#  decoration: bars 4 and 8 HOLD the phrase (C5, A4, then A4 for two beats), so
#  detached quarters there are wrong, and a wrong bar count is invisible until
#  heard.

MELODY_BARS = [
    # bar 1
    [('E5', 'q'), ('B4', 'e'), ('C5', 'e'), ('D5', 'q'), ('C5', 'e'), ('B4', 'e')],
    # bar 2
    [('A4', 'q'), ('A4', 'e'), ('C5', 'e'), ('E5', 'q'), ('D5', 'e'), ('C5', 'e')],
    # bar 3
    [('B4', 'q', True), ('C5', 'e'), ('D5', 'q'), ('E5', 'q')],
    # bar 4 -- the phrase HOLDS: C5, A4, then A4 for two beats (the gap would be
    #          audible, so no detached quarters here)
    [('C5', 'q'), ('A4', 'q'), ('A4', 'h')],
    # bar 5
    [('D5', 'q', True), ('F5', 'e'), ('A5', 'q'), ('G5', 'e'), ('F5', 'e')],
    # bar 6
    [('E5', 'q', True), ('C5', 'e'), ('E5', 'q'), ('D5', 'e'), ('C5', 'e')],
    # bar 7
    [('B4', 'q'), ('B4', 'e'), ('C5', 'e'), ('D5', 'q'), ('E5', 'q')],
    # bar 8
    [('C5', 'q'), ('A4', 'q'), ('A4', 'h')],
]

BAR_BEATS = 4.0
BEATS = {'w': 4.0, 'h': 2.0, 'q': 1.0, 'e': 0.5, 's': 0.25, 't': 0.125, 'x': 0.0625}

# One or two chords a bar; the second takes over at beat 3. A plain A minor
# chart for the tune: Am | Am C | E | Am | Dm F | C Am | E | Am, twice, with the
# last bar turning around on E so the loop leads back into bar 1.
CHORDS = [
    ('Am', None), ('Am', 'C'), ('E', None), ('Am', None),
    ('Dm', 'F'), ('C', 'Am'), ('E', None), ('Am', None),
    ('Am', None), ('Am', 'C'), ('E', None), ('Am', None),
    ('Dm', 'F'), ('C', 'Am'), ('E', None), ('Am', 'E'),
]
CHORD_PCS = {'Am': {9, 0, 4}, 'C': {0, 4, 7}, 'E': {4, 7, 11},
             'Dm': {2, 5, 9}, 'F': {5, 9, 0}}
BASS_ROOT = {'Am': 'A2', 'C': 'C3', 'E': 'E2', 'Dm': 'D3', 'F': 'F2'}

# How the harmony is voiced under the melody: the first interval whose note is a
# chord tone wins. Thirds sound best; the fifth is the escape hatch for notes
# like G over F, where no third below belongs to the chord. The second half
# prefers sixths, which is what gives bars 9-16 their wider sound.
HARMONY_ORDER_1 = (4, 3, 8, 9, 7, 5)
HARMONY_ORDER_2 = (8, 9, 4, 3, 7, 5)


def event_beats(ev):
    """A (pitch, dur[, dotted][, beat]) tuple in beats."""
    beats = BEATS[ev[1]]
    return beats * 1.5 if len(ev) > 2 and ev[2] else beats


def bars():
    """The 16 bars: the eight-bar phrase, then the same phrase again."""
    return MELODY_BARS * 2


def chord_at(bar, beat):
    first, second = CHORDS[bar]
    return second if (second and beat >= 2.0) else first


def harmonise(melody_pitch, pcs, order):
    """The chord tone that sits under the melody note at the best interval."""
    m = pitch_to_midi(melody_pitch)
    for interval in order:
        candidate = m - interval
        if candidate % 12 in pcs:
            return midi_to_pitch(candidate)
    return midi_to_pitch(m - 3)          # no chord tone below: a third it is


def chord_pitch_classes(root_pc, quality):
    """A triad's pitch classes: root, third and fifth. 'dim' is the triad on the
    leading note of a minor key (B-D-F in A minor), which is what that root is."""
    third = 3 if quality in ('min', 'dim') else 4
    fifth = 6 if quality == 'dim' else 7
    return {root_pc % 12, (root_pc + third) % 12, (root_pc + fifth) % 12}


def chord_hits(pcs, notes):
    return sum(1 for n in notes if n[1] % 12 in pcs)


def chord_name(root_pc, quality):
    names = ['C', 'C#', 'D', 'D#', 'E', 'F', 'F#', 'G', 'G#', 'A', 'A#', 'B']
    return names[root_pc % 12] + {'maj': '', 'min': 'm', 'dim': 'dim'}[quality]


def bars_with_beats(source):
    """[[(item, dur, dotted, beat) ...] per bar] from a bar list of events."""
    out = []
    for bar in source:
        events, beat = [], 0.0
        for ev in bar:
            pitch, dur = ev[0], ev[1]
            dotted = len(ev) > 2 and ev[2]
            events.append((pitch, dur, bool(dotted), beat))
            beat += event_beats(ev)
        out.append(events)
    return out


def melody_bars():
    return bars_with_beats(bars())


def harmony_bars():
    out = []
    for i, events in enumerate(melody_bars()):
        order = HARMONY_ORDER_2 if i >= 8 else HARMONY_ORDER_1
        out.append([(None if p is None else harmonise(p, CHORD_PCS[chord_at(i, beat)], order),
                     d, dot, beat) for p, d, dot, beat in events])
    return out


def bass_bars():
    """Eight eighths a bar -- root, octave, fifth, octave, twice -- following the
    mid-bar chord change, with a leading tone into the loop at the very end."""
    out = []
    for i, (first, second) in enumerate(CHORDS):
        def run(chord):
            r = pitch_to_midi(BASS_ROOT[chord])
            return [r, r + 12, r + 7, r + 12]
        notes = run(first) + run(second or first)
        if i == len(CHORDS) - 1:                     # G#3 leading back to A
            notes[-1] = pitch_to_midi(BASS_ROOT['Am']) + 11
        out.append([(midi_to_pitch(n), 'e', False, j * 0.5) for j, n in enumerate(notes)])
    return out


def check_score():
    """Kept for the hand-written score: build() checks the bars itself."""
    for i, bar in enumerate(bars(), 1):
        total = sum(event_beats(ev) for ev in bar)
        assert abs(total - BAR_BEATS) < 1e-9, f"bar {i} holds {total} beats, not 4"


# ----------------------------------------------------------------------------
#  Building a score from a MIDI
# ----------------------------------------------------------------------------
#  A MIDI is the source of truth for the NOTES; the arrangement -- which voice
#  gets what, the chords, the SID colour -- is still ours. This file's piano MIDI
#  carries the melody at velocity 113 and the accompaniment at 90, and that
#  accompaniment is the two-handed thing a piano needs: a bass root/octave pulse
#  plus (where it has one) an inner chord tone. So the SID bass comes from the
#  lowest sounding note, the harmony is voiced under the melody from the chords
#  those notes imply, and nothing here has to guess a melody again.

EIGHTHS = {8: ('w', False), 6: ('h', True), 4: ('h', False),
           3: ('q', True), 2: ('q', False), 1: ('e', False)}


DIATONIC_QUALITY = {0: 'maj', 2: 'min', 4: 'min', 5: 'maj', 7: 'maj', 9: 'min', 11: 'dim'}

# A natural minor plus the leading tone: the notes this tune uses. A chord that
# needs anything else is wrong here, however well it fits the two notes sounding.
KEY_PCS = {9, 11, 0, 2, 4, 5, 7, 8}


def best_chord(sounding_half, sounding_whole, bass_pc, previous=None):
    """Fit a triad to what a half bar is doing.

    NOT "a triad on the bass note": the bridge walks C - G/B - Am - E/G#, and a
    triad built on that G# is G#-B-D# -- a D sharp in A minor, plainly wrong.
    Fitting finds E major, which is what the bar is. The half bar is weighted
    over the bar it sits in, a candidate that leaves the key loses to one that
    does not (E major beats C# minor when only E and G# sound), a tie keeps the
    chord that was already playing (the last bar is a single G# resolving an E),
    and only then does the bass note want to be the root.
    """
    best, best_key = (bass_pc % 12, 'min'), None
    for pc in range(12):
        for quality in ('maj', 'min', 'dim'):
            pcs = chord_pitch_classes(pc, quality)
            key = (2 * chord_hits(pcs, sounding_half) + chord_hits(pcs, sounding_whole),
                   -len(pcs - KEY_PCS),
                   1 if (pc, quality) == previous else 0,
                   1 if pc == bass_pc % 12 else 0,
                   1 if DIATONIC_QUALITY.get(pc, 'min') == quality else 0)
            if best_key is None or key > best_key:
                best, best_key = (pc, quality), key
    return best


def midi_score(path, bars=16, bass_floor=45):
    """-> (melody_bars, harmony_bars, bass_bars, chord_chart) for the first N bars.

    Raises if a bar does not add up: the whole point of reading a real score is
    that its arithmetic is right, so a mismatch means this code is wrong.
    """
    division, tracks = read_midi(path)
    notes = [n for track in tracks for n in track['notes']]
    top = max(v for _, _, _, v in notes)
    melody_all = [n for n in notes if n[3] >= top - 4]
    accomp_all = [n for n in notes if n[3] < top - 4]
    unit = division // 2                        # ticks per eighth note
    bar_ticks = division * 4
    keep = [n for n in notes if n[0] < bars * bar_ticks]
    bass_low = min(n[1] for n in keep)
    bass_shift = 12 * ((bass_floor - bass_low) // 12) if bass_low < bass_floor else 0
    print(f"  bass: lowest note in the file is {midi_to_pitch(bass_low)}, "
          f"transposed +{bass_shift} semitones into the SID's bass register")

    mb, hb, bb, chart = [], [], [], []
    last_chord = None
    for bar in range(bars):
        lo, hi = bar * bar_ticks, (bar + 1) * bar_ticks
        melody = sorted(n for n in melody_all if lo <= n[0] < hi)
        accomp = [n for n in accomp_all if lo <= n[0] < hi]

        # --- melody: already monophonic in the file, one note per onset ------
        events, beat = [], 0.0
        for start, pitch, dur, _ in melody:
            eighths = max(1, round(dur / unit))
            assert eighths <= 8, f"bar {bar + 1}: a melody note is longer than a bar"
            code, dotted = EIGHTHS[eighths]
            events.append((midi_to_pitch(pitch), code, dotted, beat))
            beat += eighths / 2.0
        assert abs(beat - BAR_BEATS) < 1e-9, \
            f"bar {bar + 1}: the melody holds {beat} beats, not 4"
        mb.append(events)

        # --- chords: one per half bar, from the bass root and everything above
        # that is still SOUNDING (a note held across the half-bar still counts)
        bar_chords = []
        for half in (0, 1):
            t0 = lo + half * bar_ticks // 2
            t1 = t0 + bar_ticks // 2
            sounding = [n for n in accomp + melody if t0 <= n[0] < t1
                        or (n[0] < t0 < n[0] + n[2])]
            window = [n for n in accomp if t0 <= n[0] < t1]
            if not window:
                bar_chords.append(bar_chords[-1] if bar_chords else (9, 'min'))
                continue
            root = min(n[1] for n in window)
            whole = [n for n in accomp + melody
                     if lo <= n[0] < hi or (n[0] < lo < n[0] + n[2])]
            previous = bar_chords[-1] if bar_chords else last_chord
            bar_chords.append(best_chord(sounding, whole, root, previous))
        last_chord = bar_chords[-1]
        chart.append(bar_chords)

        # --- bass: the lowest note sounding in each eighth, shifted as a LINE so
        # the root/octave bounce of the arrangement survives the transposition ---
        slots, last = [], bass_floor + bass_shift
        for slot in range(8):
            t0 = lo + slot * unit
            in_slot = [n for n in accomp if t0 <= n[0] < t0 + unit]
            pitch = min(in_slot, key=lambda n: n[1])[1] + bass_shift \
                if in_slot else last
            last = pitch
            slots.append((midi_to_pitch(pitch), 'e', False, slot / 2.0))
        bb.append(slots)

        # --- harmony: a chord tone under every melody note ------------------
        events = []
        for pitch, code, dotted, beat in mb[-1]:
            root, quality = bar_chords[0] if beat < 2.0 else bar_chords[1]
            order = HARMONY_ORDER_2 if bar >= 8 else HARMONY_ORDER_1
            events.append((harmonise(pitch, chord_pitch_classes(root, quality), order),
                           code, dotted, beat))
        hb.append(events)
    return mb, hb, bb, chart


# ----------------------------------------------------------------------------
#  Song / file assembly
# ----------------------------------------------------------------------------

def emit(seq):
    """A sequence of (command, option) pairs -> bytes, with a closing HLT."""
    data = bytearray()
    for cmd, opt in seq:
        data.append(cmd & 0xFF)
        data.append(opt & 0xFF)
    data += bytes(HLT)
    return bytes(data)


#  Three timbres, one each: a SAWTOOTH lead, a SQUARE bass (pulse at 50% --
#  $0800 is the midpoint) and a TRIANGLE for the harmony, so the middle of the
#  chord stays soft and does not compete with either.
#  WARNING: NO PULSE-WIDTH MOVEMENT and no envelope changes mid-song: both were tried
#  and both read as "detuned" rather than expressive on a three-voice tune. The
#  one piece of movement is the melody's vibrato below, which is deliberately tiny.

#  MELODY VIBRATO. VRT/VDP modulate the note's 16-bit frequency register
#  (`WorkTargetNoteFreq + WorkVibratoDepthC/D`, sidplay.inc) with a signed offset
#  that ramps by `depth` every player call and flips sign every `rate` calls, so:
#      amplitude (frequency-register units) = rate * depth
#      period    (player calls)             = 2 * rate
#  The player is called once per `delay` jiffies, and the game plays the tune with
#  delay 2 at ~59 Hz, so rate 3 = 6 calls = 12 ticks = 0.2 s = ~5 Hz: quick, and
#  steady enough to read as richness on a held note.
#  Judge the amplitude in CENTS, not in register units -- one semitone is ~223
#  units at A3 but ~445 at A4, so the same offset is twice as wide an interval an
#  octave lower. 3 x 12 = 36 units is ~16 cents at A3 and ~8 at A4: a shimmer. The
#  setting rejected as "detuned" was rate 40 x depth 6 = 240 units -- over a whole
#  semitone at A3.
#  WARNING: VRT MUST BE SENT BEFORE VDP. VDP only *arms* the vibrato (depth != 0); the
#  first flip reloads its counter from the rate, and a rate still 0 leaves that
#  counter at 0, which the ramp then reads as 256 calls of drift in one direction.
MELODY_VIBRATO_RATE = 3
MELODY_VIBRATO_DEPTH = 12

#  WARNING: THE TUNE PLAYS ONCE AND STOPS -- it does NOT loop inside the file.
#  A Sidplayer tune can repeat itself: HED marks the top of a repeat section, TAL
#  jumps back to it, and a count of 0 means forever. This one has neither, so all
#  three voices reach their HLT, the player sees the song end, and its OWN repeat
#  counter decides what happens next:
#      * `gortris` calls kSidPlay with repeats = 0 -> a looping soundtrack;
#      * `run sidplay tetris` uses the default 1 -> the tune plays once and stops.
#  A loop in the FILE would make the second case impossible: TAL jumps back
#  *inside* the stream, so the voices never all halt and kSidPlay never reaches
#  the end test at all.
#  build() asserts that no stream carries HED/TAL, so it cannot creep back in.

#  The tune is written at the register the MIDI transcribes (lead A4-A5). The player
#  takes a tune an octave DOWN at the call while VICE runs at -speed 200, so writing it
#  here at the MIDI's own octave is what lands it an octave down on the chip -- the file
#  must NOT pre-compensate for the emulator speed as well.
#  The bass keeps its extra octave: the importer's register floor lands the line at A2,
#  whose octave bounce reaches E4 -- the very pitch the melody sings -- so it needs the
#  drop to read as a bass at all (roots A1-G#2, bounce E2/E3).
LEAD_TRANSPOSE = 0
HARMONY_TRANSPOSE = 0
BASS_TRANSPOSE = -12

#  Master volume, 0-15; 15 is the chip's maximum. VOL is a GLOBAL register
#  ($D418), not a per-voice one, so a single command in voice 1's stream sets it
#  for the whole tune -- the player otherwise leaves it at its default 8. The
#  option's high nibble carries the volume and the player ORs it into $D418's low
#  nibble only, so this cannot disturb the filter bits or bit 7 (voice 3 OFF -- a
#  bit that must stay CLEAR or the third voice is disconnected and silent).
VOLUME = 0x0F


def shift_pitch(name, semitones):
    """A pitch moved by whole semitones (spelled with sharps if it must be)."""
    return name if semitones == 0 else midi_to_pitch(pitch_to_midi(name) + semitones)


def lead_stream(mb, tempo=0x60):
    """The melody on a sawtooth: one fixed envelope, and a slight vibrato."""
    s = [
        c_tem(tempo),                       # written tempo: the game plays with
                                            # delay 2 = HALF speed, so $60 (150
                                            # M.M.) sounds as 75 M.M. -- c_tem(0x30)
                                            # is the twice-as-fast setting
        c_vol(VOLUME),                      # global: all three voices, up to max
        c_wav(WAV_SAW),
        c_atk(2), c_dcy(8), c_sus(10), c_rel(8),
        c_vrt(MELODY_VIBRATO_RATE),         # the melody only: a slight, quick
        c_vdp(MELODY_VIBRATO_DEPTH),        #   shimmer -- VRT FIRST, see above
    ]
    for events in mb:
        s += [note_event(shift_pitch(p, LEAD_TRANSPOSE), d, dot)
              for p, d, dot, beat in events]
    return s                                # ends in the HLT emit() appends --
                                            #   no HED/TAL: see the note above
    #  NOTE: no VOL command, and no 3-O command. commodo.mus -- a real, working
    #  file -- sends neither: the player's defaults are volume 8 and $D418 bit 7
    #  CLEAR, which is voice 3 CONNECTED, so all three voices sound without being
    #  asked. Setting the 3-O bit would *disconnect* voice 3 and silence it.


def harmony_stream(hb):
    """A chord tone under every melody note, on a triangle."""
    s = [
        c_wav(WAV_TRI),                     # soft: it sits between lead and bass
        c_atk(1), c_dcy(7), c_sus(6), c_rel(6),
    ]
    for events in hb:
        s += [note_event(shift_pitch(p, HARMONY_TRANSPOSE), d, dot)
              for p, d, dot, beat in events]
    return s                                # no HED/TAL: the caller repeats it


def bass_stream(bb):
    """Eighth notes on a SQUARE wave (pulse at $0800 = 50%).

    The envelope is SUNG, not plucked: a soft attack (2) and a high sustain (12)
    so each eighth holds until the next one takes the gate, with a long release
    (10) to join them up. A plucked bass (sustain 0, attack 0) makes every eighth
    a separate stab, and on a line that IS straight eighths that reads as
    syncopation -- there is no rest in the line, only in the sound."""
    s = [
        c_wav(WAV_PULSE),
        c_pw(0x0800),                       # 50% duty: a square wave
        c_atk(2), c_dcy(8), c_sus(12), c_rel(10),
    ]
    for events in bb:
        s += [note_event(shift_pitch(p, BASS_TRANSPOSE), d, dot)
              for p, d, dot, beat in events]
    return s                                # no HED/TAL: the caller repeats it


def repeat_first(bars, section):
    """Play the opening `section` bars twice: the theme's form is A A B, and the
    MIDI writes the A phrase once (bars 1-8) before the bridge (bars 9-16). Every
    voice is extended the same way, or they fall out of step with each other.
    A section longer than the score just repeats what is there; 0 disables it."""
    if not section:
        return bars
    section = min(section, len(bars))
    return bars[:section] + bars[:section] + bars[section:]


#  The description string the .mus carries (shown by `sidplay` when it lists the
#  FS). It names the sheet the notes came from as well as this repository: the
#  melody and bass line were read from that sheet, and its arranger and site are
#  credited in NOTICE and in reference/tetris/UPSTREAM.md.
DESCRIPTION = "KOROBEINIKI (TETRIS) - TRAD. / PIANO ARR. OLIMAR12345 " \
              "NINSHEETMUSIC.ORG / GORDONOS 2026"


def build(path, mb=None, hb=None, bb=None, tempo=0x60, chart=None, verbose=False):
    mb = melody_bars() if mb is None else mb
    hb = harmony_bars() if hb is None else hb
    bb = bass_bars() if bb is None else bb
    for name, bars_ in (('melody', mb), ('harmony', hb), ('bass', bb)):
        for i, bar in enumerate(bars_, 1):
            total = sum(event_beats(ev) for ev in bar)
            assert abs(total - BAR_BEATS) < 1e-9, \
                f"{name} bar {i} holds {total} beats, not 4"
    assert len(mb) == len(hb) == len(bb), "the voices are not the same length"
    seqs = [lead_stream(mb, tempo), harmony_stream(hb), bass_stream(bb)]
    for i, seq in enumerate(seqs, 1):
        #  A tune that loops ITSELF takes the loop away from its caller: TAL jumps
        #  back inside the stream, so the voices never all halt and kSidPlay's
        #  repeat counter is unreachable. See the note above the streams.
        assert not any(cmd == 0x36 or (cmd, opt) == TAL for cmd, opt in seq), \
            f"voice {i} repeats inside the file (HED/TAL)"
    streams = [emit(seq) for seq in seqs]
    load = 0xC000                                   # PRG-style, ignored by players
    out = bytearray()
    out += bytes([load & 0xFF, load >> 8])
    for s in streams:
        out += bytes([len(s) & 0xFF, len(s) >> 8])
    for s in streams:
        out += s
    out += DESCRIPTION.encode('ascii') + b'\x00'

    # --- self-checks: the loader trusts the header, so it must be right ------
    for i, s in enumerate(streams):
        assert len(s) < 65536, f"voice {i + 1} too long"
        assert s[-2:] == bytes(HLT), f"voice {i + 1} does not end in HLT"
    assert out[2] | out[3] << 8 == len(streams[0])
    assert 8 + sum(len(s) for s in streams) + len(DESCRIPTION) + 1 == len(out)

    open(path, 'wb').write(bytes(out))
    quarter = tempo >> 2                            # jiffies per quarter note
    seconds = len(bb) * BAR_BEATS * quarter / 60.0 * 2
    print(f"wrote {path}: {len(out)} bytes")
    print(f"  voice sizes  : {[len(s) for s in streams]}  (bytes)")
    print(f"  bars         : {len(bb)}, {sum(len(b) for b in mb)} melody notes")
    print(f"  tempo        : TEM ${tempo:02X} = {3600 // quarter if quarter else 0} M.M. "
          f"written, half that in the game (~{seconds:.0f}s per play)")
    print("  repeats      : none in the file -- it plays once and halts; the caller "
          "loops it (kSidPlay repeats = 0)")
    if chart:
        print("  chords       : " + ' '.join(
            chord_name(root, quality) for bar in chart for root, quality in bar))
    if verbose:
        decode_file(path, verbose=True)
    return bytes(out)


def main():
    ap = argparse.ArgumentParser(description="write/read Enhanced Sidplayer .mus tunes")
    ap.add_argument('--decode', metavar='FILE', help='print a listing of a .mus file')
    ap.add_argument('--build', metavar='FILE', help='write the built-in tune')
    ap.add_argument('--inspect-midi', metavar='FILE', help='summarise a MIDI file')
    ap.add_argument('--midi-bars', metavar='FILE', help='print a MIDI bar by bar')
    ap.add_argument('--midi-phrases', metavar='FILE', help='label a MIDI\'s phrases')
    ap.add_argument('--from-midi', metavar='FILE',
                    help='build the tune from a MIDI instead of the hand score')
    ap.add_argument('--bars', type=int, default=16, help='bars to take from a MIDI')
    ap.add_argument('--repeat-first', type=int, default=8, metavar='BARS',
                    help='play the first BARS bars twice (the tune is A A B); '
                         '0 keeps the MIDI\'s own form')
    ap.add_argument('--out', metavar='FILE', default='music/tetris.mus',
                    help='output path for --from-midi')
    ap.add_argument('--verbose', action='store_true')
    args = ap.parse_args()
    if args.inspect_midi:
        inspect_midi(args.inspect_midi)
    if args.midi_bars:
        midi_bar_map(args.midi_bars)
    if args.midi_phrases:
        midi_phrases(args.midi_phrases)
    if args.from_midi:
        mb, hb, bb, chart = midi_score(args.from_midi, bars=args.bars)
        mb = repeat_first(mb, args.repeat_first)
        hb = repeat_first(hb, args.repeat_first)
        bb = repeat_first(bb, args.repeat_first)
        if chart:
            chart = repeat_first(chart, args.repeat_first)
        build(args.out, mb, hb, bb, chart=chart, verbose=args.verbose)
    if args.decode:
        decode_file(args.decode, args.verbose)
    if args.build:
        build(args.build, verbose=args.verbose)
    if not (args.decode or args.build or args.inspect_midi or args.midi_bars
            or args.midi_phrases or args.from_midi):
        ap.print_help()
        return 1
    return 0


if __name__ == '__main__':
    sys.exit(main())
