# `musedit` — tracker-style editor for `.mus` tunes — implementation plan

> **Status: STEP 6 done; STEP 8 (playback) done.** `musedit.asm`, `musedit.inc` and the generated
> `musedit-glyphs.inc` / `musedit-tables.inc` exist and build: 58 code pages, 25 pages of
> margin (84 of 109 with `sprite`/`mouse`/`filesys`/`sid`). Verified working: the screen, the layout, the eight-octave
> piano, the key loop, the pointer, the pitch hover, the load/save path, the timing module,
> the resumable three-voice walk, the tracker grid, the editing seek, the length writer, the
> gap opener, **replace (6.1)**, **split (6.2)**, **fill (6.3)**, **append (6.4)** — on an
> empty voice too — **rest (6.5)**, **delete (6.6)**, **duration arming and its legality
> refusals (6.7)**, **the tempo flag (6.8)**, **undo/redo with the dirty guard
> (6.9-6.11)** — a range-diff stack, each action one or more `(offset, voice, oldN, newN,
> old, new)` records grouped by a bit-7 "group start" flag and replayed as single range
> replaces — **insert row / bar (6.15-6.17, 6.20)** via `muInsertTime` (`I` / `SHIFT+I`)
> and **insert mode (6.19)** (`M`, `ovr`/`ins`), which splice rows of silence into all three
> voices at the cursor as one grouped action -- the cursor voice taking the note in insert
> mode -- plus a clipped redraw so an edit repaints only its own rows, a status line
> counted in displayed frames, and **playback (Step 8)**: a music thread child plays the tune
> through `kSidPlay`, `SPACE` toggles it, `sidplay.inc` publishes each voice's position at
> `$54A2`, a column-39 playhead tracks the sounding row each frame, the window follows it, every
> edit path reports `? stop first` while it runs (6.12), `? no player` when there is no child,
> and the child is reaped before the surface goes.
>
> **Where the next session starts.** 6.18 (`? in a phrase` needs CALL-block detection) and
> 6.13/6.14 (mouse-only and keyboard note sessions) remain, plus the Step 8 checks not yet
> filmed (8.4 loops, 8.7 rapid stop/play, 8.10-8.12).
> The `,` tie key and the bar's `DUR`/`TIE` click cells are still unwired. Step 7 (the command
> panel) is untouched.
> **Two harness lessons:** the kernel's
> 16-entry key ring drops keys pressed faster than a full-grid redraw finishes, so
> `temp/probe.py` spaces keys with `KEY_GAP` (1.2-1.5s is safe); fast-pressed edits read as
> swallowed, which is dropped input, not a logic fault. And the VICE binary monitor stops the
> CPU on a read, so a probe must `resume()` between samples or the player sits paused and its
> published pointer looks frozen.
> **Two testing rules learned the hard way.** (1) **Reseed the REU and commit it** -- run
> `tools/build-reu.ps1` immediately before a test run, and NEVER `git checkout` the image
> afterwards: the task is loaded from the REU filesystem, so a reverted image silently runs the
> OLD binary and every new feature looks broken (a whole Step 8 test pass was wasted on the
> pre-Step-8 `musedit`, and the image is committed with each change, as `e22f4c6` did).
> (2) **A status message needs the display.** musedit prints `? ...` on its own surface, so
> staging a refusal while another task holds the screen proves nothing. The "note key does
> nothing on the test tune" mystery is solved and was never the key dispatch: `muSeek`'s
> halted-voice arm returned with `muSekKind` still 1 (set by `muSkRecord`), so a row past
> the last event of a halted voice — every tune's tail — ran the SPLIT planner beyond the
> event's end and corrupted the image (`len0` became garbage and an `HLT` was lost), after
> which every later key looked dead. The arm now branches to `muSkCovered`'s gap test, the
> seek primes a virtual zero-length event at the voice's start (so kind 3 is gone and an
> empty voice is an ordinary gap from row 0), and the fill is unparked. The fill instrument
> is `music/filltst.mus` (one quarter C, HLT, two empty voices — rows 0-3 are the note,
> row 4 on is the gap; `temp/gen-filltst.py` regenerates it). `tools/mus-sim.py` remains
> the host model for every check, and `temp/` holds the harness (`vicesim.py` screenshots,
> `probe.py` reads the live image out of RAM, `drive2.py` leaves VICE running on a copy of
> the REU).
> A prototype's skeleton, mouse layer and load/save path are also available in the history
> bundle `C:\Dev\gordon-os-history-2026-10-17.bundle` (`refs/heads/musedit`, `9b32695`).
>
> Authority for every byte in this plan:
> - the format: `reference/sidplayer-mus-file-format.md` and `tools/gen-mus.py`;
> - what a command *does*: `src/libs/sidplay.inc` (the player `sid.lib` runs) — not the doc,
>   which is known to carry errors in two option patterns;
> - the task/loader ABI: `src/kernel/task-header.inc`, `docs/kernel.md`, `docs/syscall.md`;
> - layout and palette: `src/tasks/dynamic/edit.inc` (a text task of the same shape).

## 1. What this is

`musedit` edits COMPUTE!'s Enhanced Sidplayer `.mus` files on GordonOS and plays them through
`sid.lib`. The screen is a **tracker grid**, not a score.

The organising idea: **the tracker is a lens over the file, not a container.** `musedit` keeps
the tune in RAM as the `.mus` image verbatim and computes a timeline from it — a *simulation*
of the player's own control flow. Rows, bars, note-lengths, loop points and the playhead are
all derived from that simulation; every edit is compiled back into the minimal 2-byte
event pairs the format already uses.

Two properties follow, and they are the reason to build it this way:

1. **Any well-formed `.mus` opens**, including the whole CGSC archive and everything
   `gen-mus.py` and `sidplay` already handle. There is no editor-private format and no
   import/export step.
2. **A file that is opened and saved unchanged is byte-identical.** The editor is a view, so
   anything it does not interpret (`AUX`, `FLG`, trailing bytes after the description's NUL)
   survives by construction.

## 2. Scope

**In (first release):**

- load `<name>.mus` from the REU filesystem, or start a new empty tune;
- a three-lane time grid over **one pass of the linear stream**;
- note entry from the keyboard: pitch, octave, duration (whole…64th), dotted,
  double-dotted, triplet, tie, and the utility-length forms UTL/UTV;
- duration surgery: typing inside a longer note shortens it, splitting it, and fills the
  remainder with rests where a single duration cannot land on the row;
- command entry for **every command the player implements**, grouped, with values typed or
  stepped — envelope, waveform/pulse, frequency (vibrato, portamento, detune, transpose),
  filter, modulation, tempo/volume, repeat, phrase, misc;
- playback through `sid.lib` with a playhead that follows loops, driven by the player's own
  published position;
- save in place, or recreate the file when the image grew;
- undo/redo (64 actions) and a dirty guard on quit;
- **an on-screen piano keyboard** across the bottom of the screen: **all eight octaves**,
  C0 to B7, visible at once — 56 white keys over 40 cells, black keys as three-row bars;
- **the mouse from the first release** — hover names the pitch, a click on the keyboard enters
  it, a click on the grid places the cursor, the control bar is clickable. The mouse is
  **required**: entering a note is a click on a key, so with no 1351 the editor refuses to start.
- **insert as well as overwrite** — `I` opens one row at the cursor in all three voices and
  pushes everything below down, `SHIFT+I` opens a whole bar, and `M` makes note entry insert
  rather than replace, so a tune can be built from the top without moving events by hand.

**Out (this release):**

- description text editing (the existing description is preserved verbatim);
- block copy/paste/transpose, stream (hex) view, sections view, instrument macros, jam
  recording, mute/solo, play-from-cursor, `.wds` words, printing, multiple tunes;
- **deleting time across all three voices** (the inverse of insert row/bar). Insert is
  `muInsertTime(n)`; removal per voice is `SHIFT+INST/DEL`, and undo covers a mistaken insert,
  so a three-voice delete is deliberately left out of the first release.

**Never:** per-tick instrument tables (WF-arp, pulse-width programs, filter programs), hard
restart, per-track tempo, multispeed, subtunes, `SID2`/`SID3`, MIDI. The format has no
per-frame engine: those SID-Wizard concepts have no counterpart and are not faked.

## 3. The data model

The tune in RAM **is the `.mus` image**:

| offset | content |
|---|---|
| 0-1 | load address (ignored by the player; preserved) |
| 2-7 | three voice lengths, `len0/len1/len2` (little-endian words) |
| 8 … | voice 1 stream, voice 2 stream, voice 3 stream |
| … | description, NUL-terminated, then any trailing bytes |
| end |  |

**Invariants** (checked by every edit path):

1. every stream is a sequence of 2-byte pairs — `ProcessCurrentCmd` always advances 2;
2. each voice ends with exactly one `HLT` (`$01 $4F`), and it is last;
3. a voice is at least 2 bytes (a length word of 0 would make the next voice start inside
   this one);
4. `lenN = 2 × rowsN`;
5. voice *n*'s data starts at `8 + len0 + … + len(n-1)` — derived, never stored.

**The note option byte** is `accidental<<6 | (7-octave)<<3 | letter`. The octave is stored
**inverted**, because the player's frequency table starts at octave 7 and is halved once per
field: read a field as `7 - field` and write `7 - octave`. `tools/gen-mus.py` writes it that way
and `sidplay.inc` says so in its own words ("Octive is stored in EORed format so up is down").

Random access to row *i* of voice *n* is `voiceStart(n) + 2*i`. Insert/delete is a 2-byte
memmove of everything after the row (the rest of this voice, the later voices, the
description and any trailing bytes); only this voice's length word changes — the later
voices' words stay, while their data shifts.

Buffer: one `kMalloc` block, page-granular, **no realloc** (`A` = pages, pointer in `kPtr`,
`C=1` = no space). Size it to `ceil(image/256)` pages plus headroom, asked for as a retry
ladder (`needed+8`, `needed+4`, `needed`) so the block takes whatever slack the pool can
spare. When an edit outgrows the headroom: allocate the new block, copy, then free the old —
never free first. `? no space` leaves the image untouched. The `kMalloc` block count is
system-wide (8), so hold exactly one block (two during a grow).

## 4. Timing: the player's own duration arithmetic

Everything the grid does rests on one function: **how many jiffies does this note last?**
It is `sidplay.inc`'s `calculateNoteDuration` / `processNoteDurLogic` / `startNoteLenCalc`,
and it is not the naive `tempo × fraction`:

- the duration class (bits 2-4) is 2=whole … 7=32nd, 0=64th, 1=utility;
- `base = tempo >> (class-2)` for classes 2-7, legal only if no 1-bit is shifted out;
- at `TEM $00` the shift is seeded with carry, so the halving chain keeps its fraction (a half
  is 128, a quarter 64, …);
- a whole note at `TEM $00` stores 0, and the countdown reads 0 as **256 jiffies**;
- the 64th (class 0) is legal only when `tempo & $3C == 0` (`TEM $00/$40/$80/$C0`), and its
  length comes from the same rotate: 4/1/2/3;
- **dotted** (bit 5) is `base × 3/2`, legal only if `base` is even and ≥ 2;
- **double-dotted** (bits 5+7) is `base × 7/4`, legal only if `base` is a multiple of 4;
- **bit 7 alone** is a *triplet*, `2/3` of `base`: it is taken from a second table,
  `DataTempoTable[tempo >> 3]`, shifted right by `class-2`, and is **only available where that
  table is non-zero** — the eight-value blocks starting at `$18 $30 $48 $60 $78 $90 $A8 $C0
  $D8 $F0`. At `TEM $90` the triplet quarter is 24 jiffies (2/3 of 36).

Two consequences that the rest of the design uses:

- The **row unit** is the shortest *plain* legal duration at the tempo in force. Because the
  tempo chain is geometric, every plain length is an integer number of rows, and a whole note
  (one 4/4 bar) is **8/16/32/64 rows** depending on the tempo — the tracker grid falls out of
  the format. Examples: `TEM $90` → unit 9 jiffies (16th), 16 rows/bar; `TEM $A0` → unit 5
  (32nd), 32 rows/bar; `TEM $80` → unit 2 (64th), 64 rows/bar; `TEM $08` → unit 1, 8 rows/bar.
- Triplets and double-dots do **not** always divide the unit (a triplet 16th at `TEM $90` is
  6 jiffies against a 9-jiffy row). The grid draws such an onset on the row it falls in, with
  an off-grid marker, and `SHIFT+U` refines the unit (`plain → plain/2 → plain/3 → 1 jiffy`,
  skipping non-integral steps) so any onset can be made exact.

`musedit` carries a 32-byte copy of `DataTempoTable` (generated, asserted against
`sidplay.inc`) and implements these cases in about 60 bytes. `tools/check-mus-timing.py`
reimplements the player's routine in Python and checks all 256 `TEM` values × 8 classes × 4
dot/triplet states (8,192 cases) against (a) the editor's algorithm and (b) the published
tempo table in `reference/sidplayer-mus-file-format.md`, which matches the arithmetic
exactly. This is the plan's most important host-side gate: every row, bar and playhead
position is downstream of it.

**Correction carried by this plan:** the reference doc labels bit 7 "double dotted" and
mentions Thornton's triplet reading as an open disagreement. The player settles it: bit 7
alone *is* the triplet, bit 5+7 is the double dot. `reference/sidplayer-mus-file-format.md`
is corrected in the same commit as the timing module.

## 5. The simulator

`musedit` needs to know, for every event, **when it plays** — and the player's control flow is
not linear: `HED`/`TAL` loop, `CALL`/`DEF`/`END` jump, `HLT` stops a voice. So the editor
carries a small, resumable simulation of that control flow with no SID writes.

State (in the code image, which the loader copies per instance, so it is per-instance RAM):

| state | per voice | meaning |
|---|---|---|
| `muPtr` | 3 × 2B | current event address |
| `muTick` | 3 × 2B | this voice's next onset, in jiffies |
| `muHed` | 3 × 3B | repeat count + return address (`$00` = no head) |
| `muPhr` | 3 × (stack of 5/10/15 × 2B) | phrase call stack, mirroring `DataMaxCallStack` |
| `muOn` | 3 × 1B | voice still playing (cleared by `HLT`) |
| `muUtv` | 3 × 1B | `UTV` value |
| global | | `muTempo` (1B), `muUtl` (1B), `muClock` (2B) |

`muStep` advances the **earliest** voice's next event (ties broken by voice index, exactly as
the player processes voices 0,1,2 in a tick). It processes zero-duration events (commands) in
place and returns when a note/rest has been consumed and `muTick[v]` advanced by its length.

The structures it must mirror, from `sidplay.inc`:

- **HED n**: store n (0 = infinite) and the address after `HED`; **TAL**: if n ≠ 0, decrement
  and, when it reaches 0, fall through; otherwise jump back to the stored address;
- **DEF n**: record the address after `DEF` as phrase *n* and **play the definition body
  inline** (the player's `DEF` pushes a sentinel frame and returns; the body follows);
- **CALL n**: push the return address and the current phrase identity, then continue at
  phrase *n*'s address; **END**: pop; at the outermost frame, `END` stops the song
  (`SID_STATUS` bit 5 — mirrored as "voice stopped");
- **HLT**: stop this voice;
- `TEM` writes the global tempo; `UTL` the global utility length; `UTV` this voice's;
  a duration class 1 note is `UTL` (bit 5 clear) or `UTV` (bit 5 set);
- `JIF` is **inert** in GordonOS: it writes a CIA reload value the scheduler never loads.
  The simulator ignores it, and the editor does not offer it as a speed control.

**Grid rows for structural commands.** `CALL n` contributes the length of phrase *n* to the
timeline but has no rows of its own beyond the call site, which is drawn as
`CALL n ████`. The phrase body keeps the rows of its inline (DEF) pass. While a call is
executing, the playhead marks the **call site**, not the body — a phrase may be called from
several places.

**Cost.** The simulator is resumed, not restarted, while the user scrolls down or playback
advances, so the common case is a handful of events per redraw. A backward scroll restarts
from tick 0: measured in step 4 on the largest bundled tune (`katmand`, 4,584 B) and given a
checkpoint index (every 64 events per voice, 5 B each) only if a redraw is visible. A loop
(`TAL`) resets the per-voice cursors and the clock as the *player* does, so the playhead
returns to the `HED` row.

## 6. Editing algebra

`muSeek(voice, row)` = run the simulator to the first event whose onset row ≥ `row`, then
report `(eventIndex, onsetTick, tickRemainingInThatNote)`.

**Note insert at (voice, row)** — one routine, four cases:

1. `row` is the onset of a note or rest → **replace** that pair (this is how a pitch is
   changed). Attached commands are untouched.
2. `row` falls **inside** a note/rest → shorten that event to the gap, then insert. The gap is
   a multiple of the row unit, so it is exactly fillable: take the largest legal duration ≤
   the gap, repeat with the remainder (greedy is exact because the duration chain is
   geometric and includes the unit). Shortening produces one note plus zero or more rests.
3. `row` is past the voice's last timed event, before its `HLT` → fill with rests, then insert
   immediately before `HLT`.
4. `row` is past the `HLT` row in display terms → clamp the cursor to the `HLT` row and apply
   case 3.

**Rest at the cursor** (`INST/DEL`): replace the note at the cursor with a rest of the same
duration — timing unchanged.

**Delete the event** (`SHIFT+INST/DEL`): remove the pair and close the gap. Refused on `HLT`
with `? last row`.

**Inserting time** (`I`, `SHIFT+I`, and insert mode) is the other direction: the cursor's row
is a boundary `T0`, and `muInsertTime(n)` opens `n` row-units of silence there. It writes to
**all three voices** — the timeline is shared and the format has no per-voice offset, so an
insert in one voice alone would slide the voices apart with no way back. Per voice, three
cases:

1. an event starts exactly at `T0` → insert the `n` units of rest immediately before it, and
   hand back the address of the **first rest written**, which is the row the cursor wanted;
2. an event **straddles** `T0` (onset < `T0` < onset + length) → shorten it to `T0 - onset`,
   insert the `n` units of rest after it, then write its remainder on the same pitch after
   the gap. The remainder is a **plain** note, not a tie: `sidplay.inc`'s `checkForNoteTie`
   keeps the *previous* event sounding, and here the previous event is the inserted rest, so
   a tie would extend the silence. The note resumes with a fresh attack.
3. the voice is stopped before `T0` (its `HLT` plays earlier) → nothing to move.

Every length is greedy-filled from the duration chain — a remainder that is not one legal
duration becomes two events, the second tied to the first (both are the same pitch, so the
split is inaudible) — so this shares its writer with the split case above.

`n = 1` is **insert row**; `n = rowsPerBar` is **insert bar** — 8/16/32/64 rows depending on
the tempo in force at the cursor, the same count the header's `BAR` column divides by, so an
inserted bar is always exactly one bar. Insert mode makes note entry call the same routine with
the armed duration before writing the note, which is the difference between pushing the rows
below down and overwriting them; the note then overwrites the first rest of the gap it just
opened, and the whole thing is one action.

Two refusals. `? stop first` while playing. `? in a phrase` when `T0` falls inside a `CALL`
block: those rows belong to the shared phrase definition, so an insert there would edit every
call site at once. A row reached through a `HED`/`TAL` repeat maps back to the body event, so
an insert there changes every pass — the status line names the pass (`repeat`) so it is not a
surprise.

Inserting is image growth: at most `2 bytes × 3 voices × n` (the worst case is every unit its
own rest — greedy filling takes the longest legal duration first, so it is usually two or three
events per voice). Saving therefore has to handle a grown image (step 2). Undo records every
voice's operations as **one** action, so a three-voice insert row is a single `Z`.

**Legality.** Every duration written is one the tempo accepts. If the armed duration is not
legal (a 32nd at `TEM $90`, a triplet at a non-triplet tempo, a dotted length whose base is
odd), the edit is refused with `ILLEGAL DURATION` and nothing is written. A `TEM` inserted
anywhere — **in any voice**, since the tempo is global — can make later rows illegal; those
rows are flagged, not silently kept.

**No edit while playing** (`? stop first`): the player reads the image directly and its
pointers would move under the edit.

**Undo.** A ring of 64 *user actions* in the code image. One action records its primitive
operations — `(voice, eventIndex, kind, oldPair, newPair)` — so a split (shorten + insert +
fill) undoes as one step. Redo is the same ring walked forward. Load and clear reset it.

## 7. Screen, pointer and the on-screen keyboard

Text mode only: no `gfx.lib`, no custom charset. Drawing goes through the **routed fixed-cell
writers** (`kPrintCharAt`, `kPrintGlyphAt`), so the same code works whether or not this task
owns the display and no writer ever advances a cursor. Colours come from `kSetTextColor`
before a cell or run. The bar glyph and the piano glyphs are 8-byte patterns drawn with
`kPrintGlyphAt` — font-independent and cheap.

```
commodo.mus TEM$90 BAR3 R048   C-4 16
ROW VOICE 1    |VOICE 2    |VOICE 3
>01 C-4=======*|A-3======= |...
 02    ========|   ========|...
 03    ========|E-4======= |...
...
 19 ...        |...        |...
[rows 20-23: piano, 8 octaves over 40 cells, black keys as three-row bars]
DUR 16. TIE-  PLAY  CMD  UNDO  SAVE  Q
```

| rows | content |
|---|---|
| 0 | name, tempo, bar, cursor row, and the **pitch readout** for the pointer or cursor |
| 1 | lane headers |
| 2-19 | the grid — 18 rows; **this is the scroll region** (`kSetScrollRegion(2, 19)`) |
| 20-23 | the piano — all eight octaves, 40 cells, five cells per octave |
| 24 | the control bar: duration, dot state, tie, play, commands, undo, save, quit |

- **Columns**: 4 (row number) + 11 + 1 + 11 + 1 + 11 = 39; the spare column 39 is the playhead
  marker.
- **Lane, 11 cells**: three for the note and the bar as far as it reaches; the last cell is `*`
  when the note carries commands, or `~` when its onset is off-grid (the bar then stops a cell
  short). A rest shows `...` at its onset; a command that fires where no note starts shows its
  mnemonic (`WAV=4`). A tie draws one continuous bar.
- **Cursor**: the cell at `(row, voice)` is drawn in the highlight colour and the row number is
  prefixed `>`; the control bar names the voice. No reverse-video glyph is needed, so no cell
  has to be restored by a writer that cannot restore it.
- **Playhead**: the sounding row, drawn as `>` in column 39 with the row number highlighted;
  the window follows it, always.

**The scroll region is what keeps the keyboard still.** It is set once after `kSwitchView`, so
no cursor-carrying writer — ours or the kernel's — can scroll the piano or the control bar
into the grid. All drawing is still fixed-cell (Rule 2 never applies), but the region makes
the layout structural instead of a convention.

**The piano.** The whole range the format allows — C0 to B7 — is on screen at once, because a
key does not have to be a whole cell.

- **Geometry**: 8 octaves × 7 white keys = 56 keys over 320 px = **5.7 px per key**, and an
  octave is exactly **40 px = 5 cells**, so eight octaves fill the 40-cell width exactly and the
  octave boundary always falls on a cell edge. White-key edges sit at px 0, 6, 11, 17, 23, 29,
  34, 40 within the octave, with a 1-px separator that leaves a 5-6 px body; the five black keys
  are ~4 px bars straddling the C|D, D|E, F|G, G|A and A|B boundaries, three rows tall (24 of
  32 px), with a 2-px gap between neighbours inside each group (C#-D# and F#-G#-A#) — which
  puts D# and A# one pixel right of their own boundary.
  The first cell of each octave carries a small C marker, so the octaves read at a glance.
- **Glyphs**: 5 cell positions × 4 rows = **20 glyphs (160 B)**, because the pattern repeats
  every five cells. `tools/gen-musedit-glyphs.py` derives them from one geometry table, so a
  glyph and the hit test can never disagree.
- **Hit test**: `oct = x / 40`, `p = x % 40`, and two 40-byte tables — one for the three
  black-key rows, one for the bottom row — give the semitone (0-11) and a "black" flag in bit
  7. Pitch = `oct × 12 + semitone`. 80 bytes of table, no per-pixel arithmetic, exact at any
  key width.
- **Hover**: the row-0 readout names the pitch under the pointer (`C-4`, `C#4`) and the cells
  the key touches are redrawn with ink and paper swapped, only when the hovered pitch changes.
  At 4 px a black key is a small target; the readout is what makes it a safe one.
- **Click** enters that pitch at the grid cursor through the same edit algebra as the keyboard
  path, advances the cursor, and auditions it.

Nothing here writes the framebuffer: the piano is glyphs through `kPrintGlyphAt`, exactly like
bars and the text.

**The pointer** is `mouse.lib` + `sprite.lib` + the existing `pointer.spr` (two frames driven
as two sprites, `grpaint`'s pattern). It is **required**: entering a note is a click on a key,
so when `mouseKind` (`$E530`) reads 0 the editor refuses (`? no mouse - mouse 1351 first`),
waits for a key and leaves — before anything is drawn, because the pointer comes up first.

- `MOUSE_ENABLE` at start **and on every regain** (the first delta after a long gap would
  otherwise alias); `MOUSE_DISABLE` at exit.
- The read loop is gated on `frameCount` changing, with **8 reads per displayed frame** (a 1351
  delta wraps at ±32). Deltas are folded into our own 16-bit position, y is **negated**, and
  the position is clamped to the surface. The de-noise is **minimum-of-window**, not a median:
  VICE's POT noise only ever adds.
- `MOUSE_READ` writes `kArg+0..3`; copy the axes and the button byte out **before any other
  call**.
- Buttons: left = PA4, right = PA0. **While we are not the display owner, skip the read loop
  and ignore buttons** — a click meant for another task must not act on a hidden editor.
- The pointer is two sprites (`pointer.spr` frames 0 and 1), each with its own sheet, resolved
  once after `kSwitchView` (`kSpriteSheet` is SEI'd and costs ~1,000-3,000 cycles, so never per
  frame). `kSpritePos` once per displayed frame; `x + 24` reaches 343, so the x high bit goes
  in **carry**. Colours are set before `sprOn(%00000011)`, and `sprOn(0)` runs before exit
  while we still hold the display.
- A missing sheet is a refusal screen (`? no pointer art`), as `grpaint`'s `gpNoArt` does.

**Audition.** A click on a key sounds that pitch on the selected voice while the tune is
stopped:

- the frequency comes from a 24-byte PAL base table (the 12 semitones at octave 6) shifted down
  per octave, which reproduces the player's own PAL path;
- a fixed audition patch (pulse waveform, a short ADSR), gate on, released on the next click
  and after about ten displayed frames;
- audition is skipped, not queued, if the SID is already owned by another task's player. Play
  re-initialises the chip through `sid.lib`, so audition state never leaks into a song.

**The computer keyboard** remains a complete second input path (fast entry, and the only path
with no 1351). Shifted letters arrive as distinct screen codes — `$41-$5A` — so the two layers
never collide; a typed key is a screen code, and `@`/`C=`/`CTRL` cannot be read.

| keys | action |
|---|---|
| `A W S E D F T G Y H U J K O L P ;` | piano: C C# D D# E F F# G G# A A# B C' C#' D' D#' E' |
| `1`…`7` | armed duration: whole, half, quarter, 8th, 16th, 32nd, 64th |
| `8` / `9` | armed duration: UTL length / UTV length |
| `.` | cycle dots: none → dotted → double-dotted → triplet |
| `,` | tie on/off for the next note |
| `-` / `+` | octave for the computer-keyboard note map (0-7) |
| cursor keys | move (row, voice); the window scrolls at its edges |
| `CLR/HOME` | row 0 of this voice |
| `SPACE` | play / stop |
| `INST/DEL` | rest at the cursor |
| `SHIFT+INST/DEL` | delete the event, closing the gap |
| `I` | insert one row at the cursor, pushing every voice's later events down |
| `SHIFT+I` | insert one bar (the rows per bar at this tempo) |
| `M` | insert/overwrite mode for note entry |
| `C` | command panel for the event at the cursor |
| `Z` / `X` | undo / redo |
| `V` | view (grid; stream and sections when they exist) |
| `SHIFT+U` | refine the row unit (triplets and double-dots) |
| `SHIFT+S` | save |
| `Q` | quit (warns when dirty; `Q` again discards) |

The note map is a **table**, not a chain of compares — SID-Wizard's three keyboard layouts are
the lesson. `Q R I Z X C V B N M` are the only letters not used by the piano.

**Insert/overwrite mode** is a byte, shown at the right of row 1 as `OVR` or `INS` (columns
37-39, outside the scroll region, so nothing can scroll it away). Overwrite is the default:
entering a note replaces or splits. `INS` pushes the rows below down by the armed duration
first, which is how a tune is built from the top. The mode changes nothing but what the next
note entry does — switching it does not touch the image and does not dirty the tune.

**Control bar** (row 24) is clickable through one hit table: a cell range plus a code, tested
in row order. The bar's `DUR` field has `-`/`+` cells and shows the armed value; `TIE` toggles;
`PLAY`, `CMD`, `UNDO`, `SAVE` and `Q` run the same routines their keys do. Errors and status
messages replace row 0 for a moment and are redrawn away on the next state change.

**Dirty guard**: a dirty byte set by every image change and a warn byte set by the first `Q`;
`SHIFT+S` clears both. Any edit after a warning clears warn, so the question is asked again
rather than answered by a stale warning.

## 8. Commands

Every command the player implements is enterable. The panel operates on **the event group at
the cursor** — the command pairs between the previous timed event and the next note:

```
COMMANDS   V1   ROW 048   C-4 16
 1 ATK   9
 2 DCY   D
 3 SUS   9
 4 REL   6
 5 PNT   4
 6 HLD   0
 7 WAV   NOISE
 8 P-W   0800
 9 VDP   04
10 VRT   20
 [A]dd  [DEL] remove  [RET] edit  [Q] back
```

- **Add** opens the group list — `TEMPO VOLUME WAVE FREQ FILTER MOD ENVELOPE REPEAT PHRASE
  MISC` — then the command list, then a value. Values are typed as up to four hex digits, or
  stepped with `-`/`+`; the panel shows the decoded signed/decimal form beside the field. Every
  row and button in the panel is also a click target, through the same hit table the control
  bar uses.
- **Edit** changes the value in place; **Delete** removes the pair. Neutral values are not
  stored: setting a field to its "no effect" state removes the pair, so the image stays
  minimal and a no-op session round-trips byte-identically.
- The 50 commands fall into five encoders: **fixed** (`TAL`, `END`, `HLT`), **opt8** (a fixed
  command byte with the value in the option), **bit in option** (`SNC`, `RNG`, `FLT`, `F-X`,
  `P&V`, `LFO`, `3-O`), **nibble in option** (`ATK`, `DCY`, `SUS`, `REL`, `VOL`, `BMP`, `WAV`,
  `F-M`, `RES`, `SRC`, `DST`, `RUP`, `RDN`, `CALL`, `DEF`), and **upper bits in the command
  byte** (`P-W` 12-bit, `POR` 14-bit, `DTN` 12-bit, `MS#` 10-bit, `JIF` 10-bit). Notes are
  their own encoder; `CALL`/`DEF` phrases 16-23 use the `$80 | ((n-8) << 4)` option form.
- `AUX` is offered but labelled **no effect**: the player's `jmp (AuxRoutineAddrLoHi)` hooks a
  pointer inside `sid.lib`'s own data, which no task can set. `JIF` is offered and labelled
  **no effect in GordonOS** for the reason in §5.
- The byte patterns are taken from `RouteCommand` in `sidplay.inc` (the doc has two wrong
  option patterns), and `tools/check-mus-commands.py` compares the editor's descriptor table
  against `tools/gen-mus.py`'s command definitions so the two cannot drift.

## 9. Playback and the playhead

Playback is `sid.lib` from a **thread child**, exactly as `gortris.inc`'s `gtMusicThread` does
it: the child declares the ZP window at spawn (`kArg+6` = `$02`, `kArg+7` = 9 — the child's own
declaration is what makes the player's state survive a switch), calls `kSidPlay` with
`kPtr` = the image, `kArg` = 1 (one tick per jiffy), `kArg+1` = 0 (repeat forever),
`kArg+2/3` = 0/0. Stop is `kSidHush`, sent twice (once directly, once a yield later to cover
the sliver before the child claims the SID). **The player counts as stopped only when the
child's own `playing` flag is clear**; edits, a new play and a resize all wait for it. Reap the
child (`quit`, hush, wait for the slot, `SYS_KILL` on timeout) **before** freeing the tune
block. `C=1` from `kSidPlay` means the SID is already claimed by a live task; report
`? no player`, as `sidplay` does.

**A failed `sid.lib` load does NOT return `C=1` — it exits the calling task.** A lazy library
that will not load prints `could not load sid.lib` and leaves through `libFailExit`, so the
child dies inside the stub and `kSidPlay` never comes back. Two rules follow, and both are in
the code: the parent **detects the dead child** (`muPlayTick` reads `taskState[muPlaySlot]`) and
reports `? no player` on its own head row, staying alive and editable; and **every parent-side
`kSidHush` is gated** by `muSidHushSafe` (`muSidOk`, set by the child just before `kSidPlay`,
plus a live child), because an unguarded hush with no library loaded would exit musedit instead
of the child. The player is per-session: a lost child is not respawned, so PLAY keeps reporting
until musedit is restarted.

**The playhead needs the player's position, and the editor's clock cannot supply it.** The
scheduler tick is 59.2 Hz and the display frame is 50.1 Hz, so a frame-counted playhead falls
behind 18 % per second. So the player publishes:

- **6 resident bytes at `$54A2-$54A7`** — `WorkNxtCmdLo[3]` then `WorkNxtCmdHi[3]`.
- Publication is **two stores in the command-fetch path** of `sidplay.inc`: after
  `sta WorkNxtCmdLo,x` and after `sta WorkNxtCmdHi,x`, A already holds the value, so each is a
  single `sta $54A2,x` / `sta $54A5,x`. **6 bytes of library code**; `sid.lib` is 3,318 bytes
  (13 pages of 3,328), so it stays 13 pages. `sid.asm` gains a `.const` alias and `main.asm`
  an exact-address assert, as `gfx.asm`'s pinned state already does.
- The block takes the 9-byte free pocket `$54A2-$54AA` down to 3 bytes:
  `tools/verify-map.ps1`'s `-ExpectTotal` default 125 → **119** and its pocket check becomes
  `54A8-54AA` = 3. `docs/memory-map.md` publishes its own total (127 B across 22 pockets,
  against the script's 125 — the two conventions differ and each is re-derived, not adjusted
  by 6).
- **Mapping**: the pointer is the *next* command, so the sounding event is `ptr - 2`; row =
  the simulator's onset for that event. A pointer outside the voice's own stream means the
  voice has halted: no playhead for it. Because the simulator is resumed, a forward pointer is
  a handful of events per frame; a `TAL` rewinds the pointer, and the simulator restarts from
  the `HED` address, which is also what the player did.
- The block is read **only while our own child is inside `kSidPlay`**; otherwise it may be
  another task's play, or stale.

## 10. Files

`kReuFindFile` → `kMalloc` → `kReuLoad` on open. On save, `kReuOpen` first:

- **same size or smaller** → `kReuWriteAt` in place, zero-filling the tail when shorter;
- **larger** → check free space, `kReuDelete`, `kReuCreate`, `kReuOpen`, `kReuWriteAt`. A
  delete is a tombstone that frees neither slot nor data, so a resized overwrite leaks the old
  extent until `compact` — and `compact` refuses while another live task exists, so it cannot
  run while `musedit` is open. **Every created file is sized to a 256-byte multiple**, zero
  filled after the description's NUL, so later growth inside the slack saves in place.

Name rules: the command line gives the base name, `<= 7` characters (the FS caps a name at 11
and `.mus` is appended). No name → a refusal screen of its own showing two lines:
`? name - run musedit <name>` and `  up to 7 characters`. A name that does not exist opens a
**new tune**: three voices of one `HLT` each, load address `$C000`, an empty description.

The name is copied **out of the argument area into the code image** at entry (`grpaint`'s
pattern): the loader writes the command line into the instance buffer's first 64 bytes and the
startup writes over that region.

`filesys.lib` is held for the task's life. `sidplay` releases it with `kLibHold` before
playing, but that is one-way (a refcount-0 lazy library can never be freed), and an editor
saves again.

## 11. Task shape and budget

| field | value |
|---|---|
| wrapper | `src/tasks/dynamic/musedit.asm` (header + entry), body `musedit.inc` |
| flags | `KF_RELOCATABLE \| KF_TAKES_ARGS \| KF_NOT_EVICTABLE` (`$49`); **not** `KF_RE_RUNNABLE` |
| ZP | **none in the header** — the thread child declares `$02`, len 9 at spawn |
| libMask | `(1 << LIB_FILESYS) \| (1 << LIB_SID) \| (1 << LIB_SPRITE) \| (1 << LIB_MOUSE)` = `$01A2`: sprite and mouse **eager** (3 pages), filesys and sid lazy |
| data size | ≤ 255; the name is copied out of the argument area (offsets 0-11) into the code image, then the instance is zeroed from 12 |
| screens | `kAllocScreen` mode 0, border `$0b`, then `kBlinkReset` inside an `SEI` window immediately before `kSwitchView` (the out-going surface's live blink block must be un-drawn before `surfSave` copies it), then `kDisableCursor` and `kSetScrollRegion(2,19)` — the grid only, so nothing can scroll the keyboard — and `kClearScreen` once after the switch, because a recycled surface keeps the previous occupant's bitmap |
| pointer | `mouse.lib` + `sprite.lib` + the existing `pointer.spr` (already seeded by `build-reu.ps1`); `mouseKind` required — 0 is the `? no mouse` refusal; two sprites resolved once, **before** anything is drawn; `sprOn(0)` and `MOUSE_DISABLE` before exit |
| exit | `kSwitchView` to the parent **before** `kFreeScreen`, then `SYS_EXIT`; drain the key queue by capacity (16 reads) first |

Footprint: code target **≤ 12 KB** (48 pages) + mouse 2 + sprite 1 + filesys 10 + sid 13 + the
largest tune 18 = **92 of 109 pages**, margin 17. `basic` is the tight resident case at 109;
with `musedit` resident and pinned, `run basic` fails with `? no space in pool` — the
documented cost of `KF_NOT_EVICTABLE`, which it needs because it holds unsaved work. The
three eager pages the pointer costs are part of that pin.

`tools/check-pool-budget.ps1` gains `'musedit' = @('filesys.lib', 'sid.lib')` in `$LiveLazy`
and a `$LiveMallocMusic` row (`'*'`: the largest bundled tune), so the gate's number is the
task's real footprint.

## 12. Phase 2 (designed, not built here)

| feature | shape |
|---|---|
| full-screen grid | a control-bar toggle that hides the piano and grows the grid from 18 to 22 rows; the piano is one routine and redraws on return |
| stream view | three columns of hex pairs, cursor + byte editing; the honest fallback for constructs the grid cannot show |
| sections view | `HED`/`TAL` blocks with repeat counts and `CALL`/`DEF` sites, editable as structure |
| description | a CR-separated text field grown/shrunk through the same memmove path |
| blocks | mark a tick range across all three voices; copy/paste/transpose (transpose rewrites the note option bytes, or writes `TPS`) |
| instruments | named command stacks in a sidecar REU file (`musedit.ins`), applied on note entry; no per-frame tables |
| jam/record | `3-O` the target voice (the player's per-tick register writer skips a voice whose `SID_STATUS` bit is off), write `$D400-$D406` directly for audition, splice on stop |

## 13. New tools and gates

| tool | job |
|---|---|
| `tools/check-mus-timing.py` | the duration arithmetic: 8,192 cases against the player's algorithm and the published tempo table |
| `tools/check-mus-commands.py` | the command descriptor table against `gen-mus.py`'s definitions |
| `tools/gen-musedit-tables.py` | emits `musedit-tables.inc` (`DataTempoTable` copy, note letters, mnemonic strings) with a `-Check` mode, like `gen-task-asm.ps1` |
| `tools/gen-musedit-glyphs.py` | one geometry table → the 20 piano glyphs and the two 40-byte pixel→semitone hit tables, emitted into `musedit-glyphs.inc` with a `-Check` mode, so a glyph and its hit test cannot drift apart |
| `tools/check-reu-fs.ps1 -Extract <fsName> -Out <path>` | round-trip tests: extract a saved file and hash it against `music/<name>.mus` |
| `tools/mus-sim.py` | Python reference simulator for the C64 one; prints `(tick, voice, event)` for a `.mus`, so a debug view can be diffed against it |

The C64 harness for the simulator is a `SHIFT+I` debug line printing `V1 clock/ptr/hed/depth`,
so the values can be compared against `mus-sim.py` before the grid exists.

## 14. Steps

A step passes when every box under it is ticked and the previous step's boxes still pass.
"*inspection only*" marks a box that cannot be reached by hand and is ticked by reading the
named code path.

### Step 1 — Screen, layout and pointer

Header, name parsing (7-char base, refusal screen on a missing name), screen allocation, the
grid scroll region, the header/lane/control-bar/strip lines, the piano drawn with its glyph
set, the mouse layer (required — a missing 1351 is a refusal), hover and click hit testing, the
control bar's hit map and status line, the per-frame key read (`Q` quits cleanly), and the exit
path.

- [x] **1.1 Build.** `tools/build-tasks.ps1` — exit 0, relocation verification OK, `musedit N pages` in the budget table.
- [x] **1.2 Seed.** Close VICE, `tools/build-reu.ps1`, then `check-reu-fs.ps1` and `check-reu-current.ps1` — both exit 0 and list `musedit.tsk` (no REU change: the pointer art is the existing `pointer.spr`).
- [x] **1.3 Open.** `run musedit commodo` — our screen, dark-grey border, row 0 with `commodo`, the lane headers on row 1, 18 empty grid rows, the piano across rows 20-23 and the control bar on 24; nothing else on the screen.
- [x] **1.4 The keyboard is still.** Move the grid window down and up with the cursor keys past the region's ends — the piano and the control bar never move and are never overdrawn (*inspection only*: the region is `kSetScrollRegion(2,19)` and every draw is fixed-cell).
- [x] **1.5 Refusals.** `run musedit` and `run musedit toolongnm` — each takes a screen of its own with only the usage line; a key exits; the shell's screen is untouched.
- [x] **1.6 No key leakage.** With the editor open, type letters and RETURN, then `Q` — nothing appears on the editor or on the shell's next line.
- [x] **1.7 F-key away and back.** F-key to the shell and back — the editor redraws, no cursor block is left on the shell's screen, and another task's screen is untouched.
- [x] **1.8 Single instance.** `run musedit commodo` again from the shell — `? already running`.
- [x] **1.9 Quit.** `Q` with nothing changed — immediate exit, both pointer sprites gone, the shell's screen intact.
- [x] **1.10 No screen** (*inspection only*). Read the `kAllocScreen` failure path: it exits silently with no surface taken.
- [x] **1.11 The mouse itself.** `tools/launch-vice.ps1 -Mouse1351` (or a "(1351 mouse)" F5 config); `mtest`, `l` toggles the direct hardware read and `mouse.lib` — the fields move in both modes, so a failure is VICE's grab, not musedit.
- [x] **1.12 The pointer.** Move slowly, quickly, diagonally and into all four corners — two sprites (black interior, white ring) track smoothly, never stutter, never show a seam between the two frames.
- [x] **1.13 The clamp.** Flick hard past every edge — the pointer stops at the surface and comes back; it never wraps or disappears.
- [x] **1.14 Eight octaves, every key.** Sweep the pointer across the whole width: row 0 names `C-0` through `B-7` with no gap and no repeat, all 56 white and 40 black keys are reachable, and the highlighted cells follow the named key. Check the five black keys in every octave and the B–C boundaries, where there is none.
- [x] **1.15 Click enters nothing yet.** A click on a key only records the pitch in the control bar (there is no tune model until step 2); a click on a grid cell moves the cursor and the readout follows.
- [x] **1.16 The control bar.** Click each of `DUR`, the duration value, the dot cell, `TIE-`,
  `PLAY`, `CMD`, `UNDO`, `SAVE` and `Q`, and the blank cells between them: every labelled
  cell reports `? not yet` on row 0 for about a second and then the head returns, `Q` quits,
  and a blank cell answers nothing.
- [x] **1.17 Hidden means deaf.** F-key to the shell and click around another task's screen; F-key back — nothing in musedit changed and its pointer is where it was left, with no jump on the first move.
- [x] **1.18 No mouse at all.** `mouse off`, then `run musedit commodo` — a blank screen reads
  `? no mouse - mouse 1351 first`, any key returns to the shell, and nothing else was drawn.
- [x] **1.19 Art missing** (*inspection only*). Read the `kSpriteSheet` failure path: hand the display back, report `? no pointer art`, exit — `grpaint`'s `gpNoArt` shape.

### Step 2 — Load and save

The image block, the load path, the new-tune image, save in place / delete+create with slack,
and the name rules.

Landed: the image is one `kMalloc` block holding the file verbatim; `muLoad` opens it with
`kReuOpen`, sizes the block from the record, `kReuLoad`s it and zeroes the block's slack;
a missing name builds the 15-byte blank tune (asserted) in a one-page block; `muSaveNow` writes
the file's **whole length** out of the block in place, and otherwise checks the free space
before deleting, then creates at a whole number of pages and writes that. Row 0 carries
`LEN v1/v2/v3` read back out of the image's own header, which is the load's own proof until
step 3 replaces the field with the tempo and bar. `SHIFT+S` and the bar's `SAVE` cell run the
same routine. Still open: the block is resized only when step 6 can grow the image.

⚠️ **A `kMalloc` block address must be stored as TWO bytes.** `kMallocImpl` ends with
`lda #0 / sta kPtr`, so `kPtr`'s low byte is *always* zero and the whole address is in
`kPtr+1` — a loader that copies only `kPtr` gives itself block `$0000` and its first write
destroys the zero page (the CPU port goes with it, the KERNAL ROM banks in, and the machine is
gone). Both load paths take the pair, and the byte that reads it back is `muImgToKPtr`.

- [x] **2.0 Extract tooling.** `tools/check-reu-fs.ps1 -Extract commodo.mus -Out temp/x.mus` writes a file whose hash equals `music/commodo.mus`.
- [x] **2.1 Load.** `run musedit commodo` — the header shows the size in bytes; a debug field shows `LEN 852/42/736`.
- [x] **2.2 New tune.** `run musedit mytune` on a missing name opens three one-`HLT` voices; `SHIFT+S`; `dir` lists `mytune.mus`; extracting and decoding it shows three `HLT` and an empty description.
- [x] **2.3 Byte-identical no-op.** `copy commodo.mus scratch.mus`, open `scratch`, save without editing, extract and hash — identical to `music/commodo.mus`.
- [x] **2.4 Trailing bytes survive.** The same with `fsonata` (which carries two bytes after the description's NUL) — identical.
- [x] **2.5 Name rules.** An 8-character base name is refused at start-up and creates nothing.
- [ ] **2.6 Grow saves.** After step 6 exists, add rows so the image grows, save, extract and decode — `dir` shows a new extent.
- [ ] **2.7 In-place save inside the slack.** Edit by less than the slack and save — no new extent, decode unchanged.
- [ ] **2.8 Full FS fails safely.** Fill the FS, save — `? no space` **before** anything is deleted, and the old file still decodes.

### Step 3 — Timing module

`muNoteJiffies` (the cases in §4), `muUnit`, and the generated tables.

Landed: `muNoteJiffies` is a translation of the player's `calculateNoteDuration` case for case —
the class is bits 2-4, a plain/dotted/double-dotted length is a carry-seeded `ror` chain on the
tempo itself (tempo 0 seeds the carry, which is what makes its half note 128), a triplet length
comes off `DataTempoTable[tempo >> 3]`, and every shift that would drop a bit is refused (`C=1`)
because the player refuses it. Class 0 (the 64th) is the rotate path and class 1 takes `muUl` or
the calling voice's `muUtv`. `muUnit` walks the shortest length the tempo can express and takes
the rows a bar has from which class won; the index lives in `muUnIdx` because `muNoteJiffies`
owns Y. The `B` key prints the two lines `tools/check-mus-timing.py` prints, so the two are
compared line for line.

⚠️ **The triplet field's duration byte is `$90`, not `$80`.** Class 4 occupies bits 2-4, so a
triplet *quarter* is `$10 | $80`; `$80` alone is class 0 carrying the triplet bit — the triplet
64th — and answers a different question entirely (1 jiffy at `$60` where the quarter is 16).

- [x] **3.1 The gate.** `tools/check-mus-timing.py` — exit 0, 8,192 cases (2,341 legal lengths, 5,851 refused), and the tempo table it derives agrees with the published one at all 32 rows.
- [x] **3.2 The table is current.** `tools/gen-musedit-tables.py -Check` — exit 0 from `tools/build-tasks.ps1`.
- [x] **3.3 The doc agrees.** `reference/sidplayer-mus-file-format.md`'s duration section matches the tool's output for all 32 tempo rows, and the tool's own table copy matches `sidplay.inc` byte for byte.
- [x] **3.4 Spot checks on the C64.** `B` on `tetris` prints `W:96 H:48 Q:24 8:12 16:6 32:3 64:-` and `TEM $60 TQ=16 UNIT=3 ROW/BAR=32`; on `commodo` (`$A0`) `W:160 H:80 Q:40 8:20 16:10 32:5 64:-` and `TEM $A0 TQ=- UNIT=5 ROW/BAR=32`; on `immig` (`$80`) `W:128 H:64 Q:32 8:16 16:8 32:4 64:-` and `TEM $80 TQ=- UNIT=2 ROW/BAR=64`. All three match the tool line for line.
- [x] **3.5 Unit.** `UNIT=5 ROW/BAR=32` at `$A0`, `UNIT=2 ROW/BAR=64` at `$80` and `UNIT=3 ROW/BAR=32` at `$60` on the C64; the tool's sweep covers the remaining 253 tempos.

### Step 4 — Simulator

The resumable three-voice walk, the structural commands, and the debug line.

Landed: `muSimInit` rewinds to tick 0 from the image's own header (three stream starts, the
player's initial `$90` / UTL 12 / UTV 1, the phrase identity = the voice index, the call stack
at its per-voice base), `muStep` executes whichever voice is earliest — commands cost no time,
so a step runs through them until a note or rest is consumed and its length added — and
`muSimCmd` mirrors `sidplay.inc`: `HED` records the count and the address after it, `TAL` jumps
back while the count lasts (identity-checked), `DEF` records a phrase and pushes a sentinel
frame so its body plays inline, `CALL` pushes a frame and jumps, `END` pops and ends the song at
the outermost frame, `HLT` clears the voice. `TEM`/`UTL`/`UTV` set the lengths later notes get,
and a class-1 note takes `muUl` or the voice's `muUtv`. An error — a length the tempo cannot
express, a command run with no note behind it, a full stack, an undefined phrase, a `TAL` in
another phrase — stops all three voices and leaves a code in `muErr`, which the `V` line prints.
`muNoteJiffies` reads `muJifTem` (the running tempo) rather than `muTempo` (the tune's opening
tempo), which is what lets a mid-tune `TEM` reach every voice. Debug keys: `B` the timing line,
`V` the walk's totals, `X` the first eighteen events, one per grid row, in the same shape
`tools/mus-sim.py --trace` prints.

- [x] **4.1 A linear tune.** `tetris`: `SIM T=2304 E=86/86/192 … HLT` against the model's 2304 ticks and 86/86/192 events.
- [x] **4.2 Loops.** `immig`: the model's and the C64's traces agree event for event over the first two sections (`t=0 v3 o=$0204`, back to `$0204` at `t=64`), and both report 7632 ticks with 231/215/663 events.
- [x] **4.3 Phrases.** `commodo`: 4040 ticks and 331/331/279 events, identical to the model; the trace shows the definition body's events arriving at the ticks the model gives.
- [x] **4.4 `DEF` plays inline.** `commodo`'s first eighteen events match the model line for line, which is the inline pass followed by the `CALL` sites.
- [x] **4.5 Tempo.** `katmand` carries four `TEM`s; the C64 reports 8568 ticks and 629/536/683 events, exactly the model's, and its final tempo byte is the same.
- [x] **4.6 UTL/UTV.** `fsonata` (four class-1 notes, `UTL 9`): 4368 ticks and 461/319/91 events against the model's identical figures, with `utv` still the default 1.
- [x] **4.7 HLT.** Every bundled tune ends with `ON=0` (all three voices stopped) and a clock the model agrees with, which is only reachable if a halted voice stops contributing while the others play on.
- [x] **4.8 Cost.** A whole walk of `katmand` (1848 events) takes **1.16 s of machine time** measured on the emulator at 200%. That is far too long to repeat for every redraw, so step 5's simulator needs the checkpoint index before a scrolled view can be drawn: a redraw resumes from the nearest checkpoint rather than rewinding to tick 0.

### Step 5 — Grid renderer

Read-only rendering of the window, the cursor, the readout, and scrolling.

Landed: the header carries the tune's tempo, the cursor's bar and its absolute row (`TEM$xx
BAR n Rnnn`), the row unit comes from `muUnitNow` (the plain unit for the tune's tempo, refined
by `SHIFT+U`), and `muDrawGrid` walks the same simulator the `V`/`X` pages use, drawing each
event as its onset (a three-character pitch, or `...` for a rest) followed by a `=` run as long
as the length's rows. A rest shows only its onset, so silence stays visibly silent; a `*` in the
lane's last cell says the event arrived with commands, a `?` that its onset fell between rows.
The row numbers are counted from each bar's first row (a bar is at most 64 rows, so two digits
hold it) and divided out with `kDiv16`; the window position is written out in the header. The
cursor keys move the `>` inside the window and step the window at its edges, redrawing through
`muCurRedraw`; the header is redrawn with the cursor so `BAR`/`R` never lag what is on screen.
`MU_GRID_STEPS` bounds one redraw's walk so a malformed tune cannot hold it.

Verified against `tools/mus-sim.py --grid`, which prints the same lanes from the same walk:
`commodo`'s first six rows agree cell for cell (`...`/`A-5` on row 1, blanks under the rest, `E-3`
from row 3 with its bar continuing), and a window scrolled nineteen rows shows the model's rows
5/7/9/11/13/15 with the header reading `BAR 1 R19`.

⚠️ **A redraw still walks from tick 0.** The step-4 measurement (1.16 s for `katmand`) means a
window deep in a long tune is slow to redraw; resuming from a checkpoint is the fix, and until
then `MU_GRID_STEPS` is what keeps a pathological tune from spinning.

- [x] **5.1 It reads as a tracker.** `run musedit commodo` — three lanes of bars, notes at their onsets, rests as `...`, commands as `*`, the row numbers and bar count matching `mus-sim.py --grid` for the rows compared.
- [x] **5.2 Bars are lengths.** Every `=` row is one unit of the note's length: `commodo` at `$A0` (unit 5) draws two-row notes as `C-2` plus one bar row, which is what the model's spans are.
- [x] **5.3 Cursor.** Cursor keys move the `>`; LEFT/RIGHT change voice; moving past the bottom or top row steps the window and redraws it.
- [x] **5.4 Click placement.** Clicking a lane cell sets the voice and the row; the lane header row, a separator and the spare column answer nothing.
- [ ] **5.5 `SHIFT+U` refines.** Needs a look on `fsonata` (it has triplets): the off-grid `?` should appear, and `SHIFT+U` should move every onset onto a row.
- [x] **5.6 Command markers.** A note carrying commands shows `*` in its lane's last cell.
- [x] **5.7 Nothing else is drawn.** `kClearScreen` at start-up and `muGridClear` on every redraw leave nothing behind; the debug pages (`B`/`V`/`X`) overwrite rows the next redraw restores.

### Step 6 — Editing algebra and undo

Seek, insert/replace/split/fill, rest, delete, legality, undo/redo, dirty. Every item is
performed with the pointer first; the same edits are reachable from the computer keyboard.

- [x] **6.1 Replace at an onset.** With the cursor on an existing note's row, click a piano key — the pair is replaced, the rows below do not move, and the decoder agrees. *Checked with the keyboard note map (the same writer a click uses): one byte of the image changed, the option `$00` -> `$A1` (C-4), the duration byte was already the armed one, and the edited image decodes to `C-4` at that row with the tie unchanged. The cursor starts on grid row 0 because `MU_ROW` holds a screen row (`MU_GRID_TOP`), not an offset.*
- [x] **6.2 Split.** With the cursor inside a longer note, click a key - the longer note is shortened, the new note lands on the row, and the total length is unchanged. *Verified on commodo: one row into its opening 10-jiffy event the pair becomes `1c 00` + `1c a1` (a 5-jiffy rest then a 5-jiffy C-4), the voice-1 length word goes 852 -> 854, the saved image decodes to `C-4` on that row with every later event unmoved, and a save of an image that outgrew its file goes through the create path with the pages behind the image zeroed.*
- [x] **6.3 Fill.** *Verified on `filltst` (quarter C on rows 0-3, HLT): a click on row 5 writes a 16th rest (`18 00`) for row 4 and the note (`18 a1`) on row 5, `len0` 4 -> 8, HLT and the later voices unmoved; a 3-row gap on an empty voice decomposes greedily (`14 00 18 00` -- an 8th rest then a 16th). The blocker was `muSeek`, not the key dispatch: its halted-voice arm kept `muSekKind` = 1 from `muSkRecord`, so a row past a halted voice's last event ran the split planner beyond the event's end and corrupted the image. The arm now takes `muSkCovered`'s gap test, and the seek primes a virtual zero-length event at the voice's start so kind 3 (before the first) is gone -- an empty voice is an ordinary gap from row 0 and the fill inserts at the voice's first pair. The host model agrees row for row, and the note-entry tails now end in `jmp muRedrawRows`/`muRedrawAll` (balanced tail calls) instead of `jmp muKeyIdle`, which leaked the `jsr muNoteClick` return address -- two stack bytes per note entered.*
- [x] **6.4 Append.** *Verified on `filltst`: a click on row 4 (right after the note) inserts `18 a1` before the `HLT` with no rests, `len0` 4 -> 6; on an empty voice a click on row 0 lands the note at the voice's first pair (`len1` 2 -> 4). Replace (6.1) and split (6.2) re-verified after the seek change: byte-identical to their recorded results.*
- [x] **6.5 Rest.** *Verified: `INST/DEL` on `filltst`'s note turns `10 99` into `10 00` -- the duration byte stays, so the length and everything after it is unchanged -- and on `commodo` the first note (`18 9b`, row 2 at its `TEM $a0` unit) becomes `18 00` with no other byte moved. The writer clears the tie bit (a rest is silence, not a held note). A row inside an event and a row in a gap are refused with `? no event here` and write nothing -- `commodo`'s row 1 sits inside its opening 10-jiffy rest and refuses, which is the seek agreeing with the timing.*
- [x] **6.6 Delete.** *Verified: `SHIFT+INST/DEL` ($88) on `filltst`'s note removes the pair, `len0` 4 -> 2, and the later voices and the description move down two bytes with their length words untouched (`muDelGap`, the inverse of `muInsGap`). Only an onset answers, and the HLT pair is never a timed event, so the terminator cannot be deleted: inside-an-event, in-a-gap and empty-voice presses all refuse byte-clean.*
- [x] **6.7 Legality.** *Verified: an armed 32nd at `TEM $90` (`filltst`, key `6`), a dotted 16th at `TEM $90` (odd 9-jiffy base, keys `5` `.`) and a triplet quarter at `TEM $a0` (`commodo`, whose `DataTempoTable` byte is 0 -- keys `3` `.` `.` `.`) each refuse with `? illegal duration` and write nothing. The positive controls write: a dotted quarter at `$90` enters as `30 a1` and a triplet quarter as `90 a1` -- the triplet byte is `$90`, class 4 bits 2-4 plus bit 7, never `$80` alone. Arming is keys `1`-`7` (plain classes, the dot state rides along), `8`/`9` (UTL/UTV, class 1 -- no dots, and `.` refuses to cycle there because bit 5 picks UTV), `.` cycles none -> dotted -> double-dotted -> triplet; `muDrawArm` repaints the bar's `DUR` value and dot cells through `kPrintCharAt`.*
- [x] **6.8 Tempo flags.** *Verified with two generated tunes (`temp/gen-temflag.py` -> `music/temflag.mus`, `music/temflg2.mus`): a `TEM $88` after a quarter note makes the following 16th illegal (136/16 = 8.5), the walk stops with `MU_ERR_LEN`, and `muGdFlag` draws `?` in the dying voice's first lane cell on the refused event's onset row (row 4 in both) -- with the `TEM` in voice 1 and in voice 2 alike, since the tempo is global. The voice and row are captured at the refusal inside `muStep` (`muErrV`/`muErrRow`), `muSimStop` now returns C=1 so no caller measures the event that never played, and the flag is re-derived by every redraw, so it clears as soon as the cause goes -- deleting the `TEM` itself awaits the command panel (step 7). The host model stops at the same event with the same diagnosis.*
- [x] **6.9 Undo/redo.** *Verified on `filltst`: a range-diff stack records every action as one `(offset, voice, oldN, newN, oldBytes, newBytes)` record, so undo and redo are each a single range-replace through `muInsGap`/`muDelGap` -- no multi-step offsets to keep in step as a gap shifts the tail. `a,DOWN,a,DOWN,a,z,z,z` returns byte-identical to the starting file; `...,x,x,x` rebuilds `18 a1 18 a1 18 a1` (the three edits). Replace, rest, delete, split and fill each undo and redo correctly. **Harness note:** the kernel's 16-entry key ring drops keys pressed faster than a full-grid redraw completes, so `temp/probe.py` must space keys (`KEY_GAP=1.2`); at machine-gun 0.45s spacing a redo looked swallowed -- it was dropped input, not a logic fault.*
- [x] **6.10 Undo granularity.** *A split is one record (`oldN=2`, `newN=2+k`), so `DOWN,a,z` on `commodo`/`filltst` restores the original event in a single `Z`.*
- [x] **6.11 Dirty.** *`muPushEdit` sets a dirty byte and clears the warn byte on every edit; `Q` while dirty prints `? unsaved - q again` and stays (the editor is still up after `a,q`), a second `Q` exits, and `SHIFT+S` clears both. The guard is in `muQuit`, before the key-drain.*
- [x] **6.12 No edit while playing.** *Verified on `filltst` and `commodo`: `SPACE` starts the child, then every mutating path -- note (keyboard and mouse click), rest, delete, undo, redo, `I`/`SHIFT+I` insert -- reports `? stop first` and leaves the image byte-identical (`muPlaying` guards each dispatch arm and `muNoteClick` for the mouse). A second `SPACE` stops; edits work again. The guard also keeps the tune's `kMalloc` block from moving under the child (an insert reallocates).*
- [ ] **6.13 A mouse-only session.** Enter twelve notes across three voices, change two pitches, rest one, undo two, save — never touching the computer keyboard — then decode. *Expect:* exactly those rows, and the same file as the same edits made from the keyboard.
- [ ] **6.14 From the keyboard.** The same twelve notes using the computer-keyboard note map
  instead of clicks — same file.
- [x] **6.15 Insert row.** *Verified on `instmv` (voice 1 a quarter C4, voice 2 an 8th rest then an 8th D4, voice 3 a quarter E4; all cross row 2): `i` at row 2 opens a 16th rest in all three voices -- voice 1 and 3 split their quarter (8th + 16th rest + 8th), voice 2 drops a 16th rest before the D4 onset -- everything below moves down one row, and the host model shows the blank row across all three. `muInsertTime` seeks and splices each voice in turn (a splice shifts the later voices, so each is re-seeked on the grown image), as one grouped undo action.*
- [x] **6.16 Insert bar.** *Verified: `SHIFT+I` on `filltst` at `TEM $90` (16 rows/bar) opens a whole-note rest (`08 00` = 144 jiffies = exactly 16 rows) before the note; the bar count comes from `muUnitBar`, so an inserted bar is always one bar.*
- [x] **6.17 Insert inside a note.** *The straddle case of `muInsertTime`: `i` one row into `filltst`'s quarter turns `10 99` into `14 99 18 00 14 99` -- the note ends on the cursor's row (8th), one 16th of silence, then the same pitch resumes with a fresh attack (plain, not tied, because the previous event is the rest). The decoder shows the split pairs; total length is the event plus the inserted row.*
- [ ] **6.18 Insert refuses.** `I` with the cursor inside a `CALL` block reports `? in a
  phrase` and writes nothing; `I` while playing reports `? stop first`.
- [x] **6.19 Insert mode.** *Verified on `filltst`: `M` toggles a mode byte shown as `ovr`/`ins` at the right of row 1 (cols 37-39, outside the scroll region). In `ins`, entering a note opens the note's length in all three voices (the cursor voice gets the note, the others rests) and pushes the rows below down -- `m,a` turns `10 99` into `18 a1 10 99`, and `RIGHT,m,a` appends the note to the empty voice 2 while pushing voice 1 down, all one grouped `Z`. `m,m` returns to `ovr`, where a note replaces (`m,m,a` gives `18 a1`, not an insert), and toggling the mode never touches the image. An empty/past-end voice takes the fill-then-note path (`muIvAppend`).*
- [x] **6.20 One action.** *Verified: `i` on `instmv` (three voices) undoes to the byte-identical original with a single `Z`, and redoes with one `X`. The undo stack groups the three per-voice splices via a bit-7 "group start" flag on the first record; undo unwinds the group newest-first (highest offset first, so closing a gap never invalidates a lower record), redo replays it oldest-first.*

### Step 7 — Command panel

The descriptor table and the five encoders; every command reachable.

- [ ] **7.1 The table gate.** `tools/check-mus-commands.py` — exit 0, every mnemonic's byte pattern matches `gen-mus.py`.
- [ ] **7.2 One of every command.** Place one of every command the panel offers, save, extract, `gen-mus.py --decode` — each appears as the documented pair, in the order placed.
- [ ] **7.3 It sounds.** Play a tune using a filter sweep (`F-C`/`F-S`), ring modulation (`RNG`), sync (`SNC`), portamento (`POR`), vibrato (`VDP`/`VRT`) and a waveform change — each is audible.
- [ ] **7.4 Values clamp.** Each field's `-`/`+` stops at its documented range; typed values outside it are refused.
- [ ] **7.5 Neutral removes.** Setting a field to its neutral value removes the pair; opening every menu and closing it re-saves byte-identically.
- [ ] **7.6 Utility notes.** `8`/`9` enter a note whose length is UTL/UTV; both round-trip.
- [ ] **7.7 Multi-byte values.** `P-W`, `POR`, `DTN`, `MS#` accept up to four hex digits and decode to the right pair.
- [ ] **7.8 Inert labels.** `AUX` and `JIF` are present and labelled no-effect; no other command is labelled that way.
- [ ] **7.9 The panel is clickable.** Click a command row to select it, `RETURN` to edit, click `[A]dd` then a group and a command, click `[DEL] remove`, click `[Q] back` — every panel action is reachable with the pointer alone. *(The control bar's `PLAY`/`SAVE`/`Q` cells are wired; `DUR`, `TIE`, `CMD` and `UNDO` still answer `? not yet`.)*

### Step 8 — Playback and playhead

The published block, the thread child, stop/reap, follow.

> **Playback is in and verified (Phases A-D).** A music thread child (`muMusicThread`, spawned
> at startup via `SYS_THREADSPAWN` with the child's own `$02-$0a` ZP window) reads the
> parent's `muPlayOn`/`muPlayQuit`/`muTuneLo`/`muTuneHi` code bytes -- a thread child does not
> share `dataPtr`, so the tune pointer is mirrored out of `MU_IMG` by `muTuneRefresh` at load
> and after every block move. `SPACE` toggles play/stop (`muPlayToggle`): play sets the flag,
> stop clears it and `kSidHush`es, with a second hush a slice later (`muPlayTick`) for the
> claim-inside-`kSidPlay` race. `muMusicBye` reaps the child (yield-wait, `SYS_KILL` fallback)
> before `kFreeScreen`. `sidplay.inc` publishes each voice's next-command pointer at `$54A2`
> (`SID_POS_BLOCK`); `muPlayhead` runs the grid's window-bounded walk each displayed frame and
> marks the first row where the walk's pointer equals the published one, in the spare column 39.
> The window follows the playhead: `muPhFollow` recentres `muTop` when the cursor voice leaves the
> window. **Harness note:** the VICE binary monitor stops the CPU on a
> read, so a probe that does not `resume()` between samples leaves the player paused and the
> pointer looks frozen -- resume between samples.

- [x] **8.1 The block.** *`main.asm` asserts the 6-byte block is free of slot-shadow code at `$54A2` (`SID_POS_BLOCK` in `sid.asm`); `verify-map.ps1` passes with 119 and the 3-byte `54A8-54AA` pocket; `sid.lib` is 3324 bytes (still 13 pages), relocation verification OK -- the two literal `sta $54A2,x` / `sta $54A5,x` stores are position-independent, not fixups.*
- [x] **8.2 It is live.** *Verified in VICE on `commodo`: while playing, `$54A2-$54A7` holds each voice's next-command pointer (valid tune-image addresses that advance over the run); on `SPACE` stop they freeze. Read only while our own child plays.*
- [x] **8.3 Play.** *Verified on `katmand`: `SPACE` plays and the column-39 marker tracks the sounding row down the grid (rows 8 -> 12 -> 15 -> 18 over the run). `filltst`'s single note holds the marker at row 0.*
- [x] **8.4 Loops.** *Verified on `immig`: voice 1's published pointer walks `$821c -> $9860 -> $a064 -> $a46a` and then jumps **back to `$a06c`** (the section's `HED` stream) at 18.1 s, and the frame captured at that sample has the column-39 marker on row `01` — the top of the section. The wrap repeats on the next pass.*
- [x] **8.5 Follow.** *Verified on `katmand`: the window scrolls to keep the marker in view (the row-number column changes as `muTop` advances). Follow is not optional — there is no key for it.*
- [ ] **8.5a One-line scroll.** The window moves one line at a time once the playhead is on its centre line, and only the line that appears at the bottom is drawn. *Blocked on a walk separation: `muPlayhead` re-walks the tune from row 0 every displayed frame and resets the simulator state, so the renderer cannot carry its position from one line to the next, and a one-line draw paints from the wrong rows (verified in VICE). The fix is a renderer that keeps its walk position plus a small per-voice `(pointer -> row)` record for the playhead — the playhead then costs a lookup instead of a full walk, which is also what removes the per-frame full walk. Note the walk is capped at `MU_GRID_STEPS = 3000` events, so a window far into a tune silently truncates.*
- [x] **8.6 Stop.** *Verified: a second `SPACE` silences at once, the marker is erased (`muPhClear`), and a new play works immediately.*
- [x] **8.7 Stop/play in the same instant.** *Verified: twelve `SPACE` presses 0.15 s apart leave the tune stopped with `$D404/$D40B/$D412` all `0` and the published pointers frozen, and the next `SPACE` plays again (`$D404 = $40`, pointers advancing) — the child never wedges and every later toggle behaves. Note `kSidHush` silences by zeroing the three voice-control registers; `$D418` keeps the library's `$08` filter setting, so the voice-control registers are what a stop is checked against.*
- [x] **8.8 No player.** *Verified: with the player child gone (killed from the shell while playing), `SPACE` reports `? no player` on musedit's OWN head row, the playhead is cleared, and the editor keeps running -- further `SPACE` presses report again, a note key still edits, and `Q` still quits cleanly. This is the same state a failed `sid.lib` load leaves, because a failed lazy load prints `could not load sid.lib` and then EXITS the calling task (`libFailExit`), so `kSidPlay` never returns and the parent must detect the dead child itself (`muPlayTick`); for the same reason every parent-side `kSidHush` is gated by `muSidHushSafe` -- an unguarded hush with no `sid.lib` loaded would exit musedit. The player is per-session: once the child is gone, PLAY reports rather than respawning.*
- [x] **8.9 No orphan.** *Verified: quit while playing (`SPACE` then `q` on `filltst`) reaps the child in `muMusicBye` before `kFreeScreen` and returns cleanly to the shell prompt.*
- [ ] **8.10 ZP.** F-key away and back during playback — the music is unbroken and the other task still runs.
- [ ] **8.11 Audition.** With the tune stopped, click piano keys across the whole range — each sounds at the pitch named in the readout (compare a few against `sidplay` playing a tune in that register), and no note sticks after about ten displayed frames. *(Not implemented: a piano click writes the note into the image and nothing plays it — `muNoteClick` is the only entry path and it calls no player.)*
- [ ] **8.12 Audition stands down.** With `sidplay` playing from another screen, click keys — no sound, no error, the editor keeps working; then play our own tune and confirm it is unaffected. *(Follows 8.11.)*

### Step 9 — Gates and release

- [ ] **9.1 Budget.** `check-pool-budget.ps1` — exit 0, `musedit` at its measured footprint, with filesys, sid, mouse and sprite counted and the tune pages counted.
- [ ] **9.2 Builds.** Kernel, tasks and REU all exit 0; `check-reu-current.ps1` reports the image holds the binaries just built.
- [ ] **9.3 Doc gates.** `verify-map.ps1`, `verify-jumptable-docs.ps1`, `verify-inventory-docs.ps1`, `check-links.py`, `check-ascii.py`, `scan-history.py` — all exit 0 (the last is advisory but read it).
- [ ] **9.4 Wiring.** `tools/tasks.json`, `project-config.json` `sources`, `README.md`'s task table, `docs/features.md`, `docs/quick-start.md`, `tools/quickstart.binaries.md` and `publish-binaries.ps1`'s `$guides` all name `musedit` / `docs/musedit.md`.
- [ ] **9.5 A fresh machine.** Freshly seeded REU, one sitting: open, enter notes, play, save, quit, reopen, play — no leftover state.

## 15. Open questions

1. **Simulator/player equivalence** is the central risk. The host simulator is the reference;
   the C64 one is a second implementation. If they disagree on a real tune, the C64 one is
   wrong. Mitigation: `mus-sim.py` on every bundled tune, and step 4's comparisons before any
   drawing code exists.
2. **`CALL` display.** A phrase called from two places has one set of rows (its `DEF` pass) and
   two playhead positions. The call-site rule in §5 is a decision, not a proof — check it on
   `commodo` and `immig` before step 5.
3. **Grow-by-copy of a large tune.** A resize needs the old and new blocks at once
   (48 code + 23 library + 18 tune + 18 copy = 107 of 109). The ladder caps the block at what
   can be doubled; a tune that cannot grow must fail cleanly rather than strand pages. Decide
   the cap before step 2.
4. **The playhead's startup skew.** The block is read only after the child is inside
   `kSidPlay`; until the first fetch the pointers are the voice starts. Confirm on the first
   play that no playhead is drawn early (the block's initial content is not "row 0").
5. **Keyboard maps.** The piano layout is a table; whether `;` and the shifted letters arrive
   as the codes the table lists is checked in step 1 on the real keyboard, and the table is
   corrected if not.
6. **`TEM` mid-tune and the grid.** A tempo change re-quantises rows from that point. Whether
   that reads well (or wants a per-section unit) is judged in step 5 on `immig`.
7. **The 127/125 map discrepancy.** `docs/memory-map.md` publishes 127 B and
   `verify-map.ps1` defaults to 125. Re-derive both when the block is pinned rather than
   assuming which convention is right.
8. **Audition against another task's player.** The audition writes `$D400-$D406` directly and
   the editor cannot read `sid.lib`'s `sidOwner`, so a player in another task could in
   principle be disturbed. Mitigation: audition only while stopped and displayed, and the
   other player rewrites its registers every tick. Confirm with a game's music running on
   another screen while keys are clicked.
9. **Key density.** 5.7 px per white key and about 4 px per black key is what eight octaves cost
   on one 320-px screen; the readout is what makes the small targets safe. If a 1-px separator
   blurs into its neighbours on real hardware, the fallback is a four-octave keyboard over 20
   cells (11.4 px per key) with paging — the generator makes either mechanical.
10. **Mouse required or optional.** It is required: entering a note is a click on a key, so a
    machine without a 1351 has nothing to enter with. The computer-keyboard note map stays as a
    second input path.

## 16. Documentation to update when it lands

`docs/musedit.md` (rewrite from "first release" to shipped), `README.md` (task table),
`docs/features.md`, `docs/quick-start.md`, `tools/quickstart.binaries.md`,
the gate list in `docs/development.md` if a new gate is added,
`docs/lib-sid.md` (the published position block), `docs/memory-map.md` (the pinned pocket and
the totals), `reference/sidplayer-mus-file-format.md` (the bit-7 correction), and
`publish-binaries.ps1`'s `$guides` (add `docs/musedit.md` — a new guide does not ship
otherwise).
