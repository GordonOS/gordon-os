# `musedit` — tracker-style editor for `.mus` tunes — implementation plan

> **Status: STEP 1 complete.** `musedit.asm`, `musedit.inc` and the generated
> `musedit-glyphs.inc` exist and build: 2,428 bytes of code, 196 fixups, 10 code pages,
> 8,192-case glyph tables checked in the build. The screen, the layout, the eight-octave
> piano, the key loop, the two-sprite pointer (required, not optional), the pitch hover, the
> click that places the cursor, the control bar's hit map and the status line are in; the tune
> model, the timing module and everything from step 2 on are next.
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
  the window follows it (`SHIFT+F` toggles follow).

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
| `SHIFT+F` | follow the playhead on/off |
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
block. `C=1` from `kSidPlay` means busy *or* "the library would not load"; report `? no player`
for both, as `sidplay` does.

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

- [ ] **2.0 Extract tooling.** `tools/check-reu-fs.ps1 -Extract commodo.mus -Out temp/x.mus` writes a file whose hash equals `music/commodo.mus`.
- [ ] **2.1 Load.** `run musedit commodo` — the header shows the size in bytes; a debug field shows `LEN 852/42/736`.
- [ ] **2.2 New tune.** `run musedit mytune` on a missing name opens three one-`HLT` voices; `SHIFT+S`; `dir` lists `mytune.mus`; extracting and decoding it shows three `HLT` and an empty description.
- [ ] **2.3 Byte-identical no-op.** `copy commodo.mus scratch.mus`, open `scratch`, save without editing, extract and hash — identical to `music/commodo.mus`.
- [ ] **2.4 Trailing bytes survive.** The same with `fsonata` (which carries two bytes after the description's NUL) — identical.
- [ ] **2.5 Name rules.** An 8-character base name is refused at start-up and creates nothing.
- [ ] **2.6 Grow saves.** After step 6 exists, add rows so the image grows, save, extract and decode — `dir` shows a new extent.
- [ ] **2.7 In-place save inside the slack.** Edit by less than the slack and save — no new extent, decode unchanged.
- [ ] **2.8 Full FS fails safely.** Fill the FS, save — `? no space` **before** anything is deleted, and the old file still decodes.

### Step 3 — Timing module

`muNoteJiffies` (the cases in §4), `muUnit`, and the generated tables.

- [ ] **3.1 The gate.** `tools/check-mus-timing.py` — exit 0, 8,192 cases, and it prints the tempo table it derived next to the published one with no differences.
- [ ] **3.2 The table is current.** `tools/gen-musedit-tables.py -Check` — exit 0; change one byte by hand and it fails.
- [ ] **3.3 The doc agrees.** The corrected `reference/sidplayer-mus-file-format.md` duration section matches the tool's output for all 32 tempo rows.
- [ ] **3.4 Spot checks on the C64.** A debug line prints `TEM $90: W144 H72 Q36 8:18 16:9 32:- 64:-` and `triplet Q=24`; it matches the tool.
- [ ] **3.5 Unit.** The same line prints `UNIT 9  ROWS/BAR 16` for `$90`, `UNIT 5 / 32` for `$A0`, `UNIT 2 / 64` for `$80`.

### Step 4 — Simulator

The resumable three-voice walk, the structural commands, and the debug line.

- [ ] **4.1 A linear tune.** On `tetris` (no `HED`/`CALL`) the debug line's clock after the last event equals `mus-sim.py`'s total.
- [ ] **4.2 Loops.** On `immig` (21 `HED`, 20 `TAL`) the simulator's first `TAL` returns to the `HED` row, and the tick sequence for the first two iterations matches `mus-sim.py`.
- [ ] **4.3 Phrases.** On `commodo` (7 `DEF`, 7 `CALL`) a `CALL` advances the clock by exactly the phrase's length, and the playhead would mark the call site (checked while stepping).
- [ ] **4.4 `DEF` plays inline.** The definition body's events are at the rows `mus-sim.py` gives for the inline pass.
- [ ] **4.5 Tempo.** A `TEM` in voice 1 changes the lengths of later notes in all three voices; a `TEM` in voice 3 does too (the tempo is global).
- [ ] **4.6 UTL/UTV.** A class-1 note takes `UTL` (bit 5 clear) or `UTV` (bit 5 set), including a `UTV` that differs per voice.
- [ ] **4.7 HLT.** A halted voice stops contributing and the others continue.
- [ ] **4.8 Cost.** Scroll through `katmand` — a redraw is under a displayed frame's worth of visible lag; if not, add the checkpoint index and re-measure (*inspection only* for the number, the lag is judged by eye).

### Step 5 — Grid renderer

Read-only rendering of the window, the cursor, the readout, and scrolling.

- [ ] **5.1 It reads as a tracker.** `run musedit commodo` — three lanes of bars, notes at their onsets, rests as `...`, commands where they fire, the row numbers and bar count matching `mus-sim.py`.
- [ ] **5.2 Bars are lengths.** Each bar's cell count equals its note's rows in the current unit (checked against `gen-mus.py --decode` for eight consecutive notes).
- [ ] **5.3 Cursor.** Cursor keys move the highlight and the `>`; LEFT/RIGHT change voice; moving past an edge scrolls by one row.
- [ ] **5.4 Click placement.** Click any lane cell — the cursor lands on that row and voice, the readout follows, and clicking the lane header row or a separator does nothing.
- [ ] **5.5 `SHIFT+U` refines.** A tune with triplets (`fsonata` unless it has none) shows the off-grid marker; `SHIFT+U` to the finer unit makes every onset land on a row.
- [ ] **5.6 Command markers.** A note carrying commands shows `*`; the control bar lists the mnemonics when the cursor is on it.
- [ ] **5.7 Nothing else is drawn.** No stale glyphs after loading two different tunes in one session (*inspection only*: `kClearScreen` at start-up).

### Step 6 — Editing algebra and undo

Seek, insert/replace/split/fill, rest, delete, legality, undo/redo, dirty. Every item is
performed with the pointer first; the same edits are reachable from the computer keyboard.

- [ ] **6.1 Replace at an onset.** With the cursor on an existing note's row, click a piano key — the pair is replaced, the rows below do not move, and the decoder agrees.
- [ ] **6.2 Split.** With the cursor inside a longer note, click a key — the longer note is shortened, the new note lands on the row, and the total length is unchanged.
- [ ] **6.3 Fill.** Click a key on a row the current duration cannot reach — rests are inserted so the new note lands exactly there; the decoder shows the rest events.
- [ ] **6.4 Append.** Click past the last event — rests fill to the cursor and the note goes before `HLT`.
- [ ] **6.5 Rest.** Click `DEL rest` (or `INST/DEL`) — the note becomes a rest of the same length; the timing of every later event is unchanged.
- [ ] **6.6 Delete.** `SHIFT+INST/DEL` removes the event and closes the gap; refused on `HLT`.
- [ ] **6.7 Legality.** An armed 32nd at `TEM $90`, a dotted length with an odd base, and a triplet at a non-triplet tempo are each refused with `ILLEGAL DURATION` and nothing is written.
- [ ] **6.8 Tempo flags.** Insert a `TEM` that makes a later row illegal — that row is flagged on the grid; delete the `TEM` and the flag clears. Repeat with the `TEM` in another voice.
- [ ] **6.9 Undo/redo.** 50 mixed actions, undo all, save, extract, hash against the starting file — identical; redo all and it is the edited file.
- [ ] **6.10 Undo granularity.** A split undoes in one step.
- [ ] **6.11 Dirty.** `Q` with unsaved changes warns; `SAVE` clears; `Q` twice exits and the file on disk is the pre-edit file; an edit after a warning re-arms it.
- [ ] **6.12 No edit while playing.** Every edit path reports `? stop first` while the child is inside `kSidPlay`.
- [ ] **6.13 A mouse-only session.** Enter twelve notes across three voices, change two pitches, rest one, undo two, save — never touching the computer keyboard — then decode. *Expect:* exactly those rows, and the same file as the same edits made from the keyboard.
- [ ] **6.14 From the keyboard.** The same twelve notes using the computer-keyboard note map
  instead of clicks — same file.
- [ ] **6.15 Insert row.** Put the cursor on a row where all three voices are empty and press
  `I` — one blank row appears, everything below moves down one row, and `gen-mus.py --decode`
  shows one extra rest in each voice that had content there.
- [ ] **6.16 Insert bar.** `SHIFT+I` at `TEM $90` — 16 rows appear (a whole bar), the later
  events move down exactly that many rows, and the bar numbers after the insert are unchanged.
- [ ] **6.17 Insert inside a note.** Cursor in the middle of a long note, `I` — the note ends on
  the cursor's row, one row of silence follows, the same pitch resumes after it with a fresh
  attack, and the rest of the voice is one row later. The decoder shows the split pairs.
- [ ] **6.18 Insert refuses.** `I` with the cursor inside a `CALL` block reports `? in a
  phrase` and writes nothing; `I` while playing reports `? stop first`.
- [ ] **6.19 Insert mode.** `M` shows `INS`; four clicks on the piano leave the earlier notes in
  place and push the rest down each time; `M` again shows `OVR`, entering a note replaces;
  toggling the mode twice and saving produces a byte-identical file.
- [ ] **6.20 One action.** An insert row on a three-voice tune is a single `Z`, and a `Z` after
  it restores the exact bytes.

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
- [ ] **7.9 The panel is clickable.** Click a command row to select it, `RETURN` to edit, click `[A]dd` then a group and a command, click `[DEL] remove`, click `[Q] back` — every panel action is reachable with the pointer alone.

### Step 8 — Playback and playhead

The published block, the thread child, stop/reap, follow.

- [ ] **8.1 The block.** A `main.asm` assert places `$54A2`; `verify-map.ps1` passes with 119 and the 3-byte `54A8-54AA` pocket; `sid.lib` is still 13 pages.
- [ ] **8.2 It is live.** With a tune playing, peek `$54A2-$54A7` in the VICE monitor several times — the three pointers advance; hush and they stop.
- [ ] **8.3 Play.** `SPACE` on `commodo` — the tune plays and the playhead tracks the sounding row on all three voices.
- [ ] **8.4 Loops.** On `immig` the playhead returns to the `HED` row at each `TAL`.
- [ ] **8.5 Follow.** The window scrolls with the playhead while following; `SHIFT+F` stops it and the view stays put.
- [ ] **8.6 Stop.** `SPACE` again — silence at once, the playhead is not drawn, and a new play works immediately.
- [ ] **8.7 Stop/play in the same instant.** Repeatedly — it always stops, the child never wedges, play always works afterwards.
- [ ] **8.8 No player.** With `sidplay` running, `SPACE` — `? no player`.
- [ ] **8.9 No orphan.** Quit while playing — `ps` shows no child and `pool` shows the tune's pages returned.
- [ ] **8.10 ZP.** F-key away and back during playback — the music is unbroken and the other task still runs.
- [ ] **8.11 Audition.** With the tune stopped, click piano keys across the whole range — each sounds at the pitch named in the readout (compare a few against `sidplay` playing a tune in that register), and no note sticks after about ten displayed frames.
- [ ] **8.12 Audition stands down.** With `sidplay` playing from another screen, click keys — no sound, no error, the editor keeps working; then play our own tune and confirm it is unaffected.

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
