# `musedit` — tracker-style editor for `.mus` tunes — implementation plan

> **Status: the editor is built, wired and shipped.** `musedit.asm`, `musedit.inc` and the
> generated `musedit-glyphs.inc` / `musedit-tables.inc` build to **93 pages of 109** (16 of
> margin, with `sprite`/`mouse`/`filesys`/`sid` counted by `tools/check-pool-budget.ps1`).
>
> **`main`'s streamed player is merged in.** A tune no longer has to be in RAM: `kSidPlay` takes a
> mode in `A` and, in mode 1, pages a file out of the REU through three page-aligned 256-byte
> windows. Two consequences reach this plan: the player publishes each voice's next command as a
> **file offset** at `$54A2` (it works in offsets now, and a paged tune has no address), so the
> playhead ring is keyed on offsets already; and **step 12 is what lets the editor itself stop
> holding the tune**, which is why it exists.
> Everything in steps 1-6 is verified, as is playback (step 8.1-8.9): a music thread child plays
> the tune through `kSidPlay`, `SPACE` toggles it, `sidplay.inc` publishes each voice's position
> at `$54A2`, the cursor is the playhead and is stepped by the tick clock between events, the
> window follows it a line at a time, every edit path reports `? stop first` while it runs,
> `? no player` when the child is gone, and the child is reaped before the surface goes. Release
> wiring (9.1-9.4) is done: `README.md`, `docs/features.md`, `docs/quick-start.md`,
> `tools/quickstart.binaries.md` and `publish-binaries.ps1`'s `$guides` all name it, and the
> guides ship.
>
> **A polish pass after the first sitting is done and in use** (see step 11): the note is written
> as its duration number instead of a bar, the keyboard is three rows, the lanes have no
> separators and each is drawn in its voice's colour, the control bar answers the mouse and shows
> which item is running, the head prints the tempo in decimal and its `BAR`/`R` follow the
> playhead, and PLAY/end-of-pass now take the view home so the notes on screen are the notes
> being played.
>
> **Where the next session starts.** Open: **8.10** (F-key away and back during playback),
> **8.11/8.12** (the audition — a piano click writes the note and plays nothing, so there is
> nothing to verify yet), **9.5** (a fresh-machine sitting), and the parked **10.1b/10.2/10.3**
> (scroll REU cost, the tuned row clock, repeat-forever). 6.18 (`? in a phrase` needs CALL-block
> detection) and 6.13/6.14 (mouse-only and keyboard note sessions) are still unbuilt. The `,` tie
> key and the bar's `DUR`/`TIE` click cells are still unwired; step 7 (the command panel) is
> untouched.
>
> **Three harness lessons:** the kernel's
> 16-entry key ring drops keys pressed faster than a full-grid redraw finishes, so
> `temp/probe.py` spaces keys with `KEY_GAP` (1.2-1.5s is safe); fast-pressed edits read as
> swallowed, which is dropped input, not a logic fault. The VICE binary monitor stops the
> CPU on every command, so a probe must `resume()` between samples or the player sits paused and
> its published pointer looks frozen — and a sampled screen can look "frozen" simply because the
> whole probe loop never resumed it.
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
| 2-20 | the grid — 19 rows; **this is the scroll region** (`kSetScrollRegion(2, 20)`) |
| 21-23 | the piano — all eight octaves, 40 cells, five cells per octave, three rows |
| 24 | the control bar: duration, dot state, tie, play, commands, undo, save, quit |

- **Columns**: 4 (row number) + 11 + 1 + 11 + 1 + 11 = 39; the spare column 39 is unused headroom
  marker. The lanes start at 4, 16 and 28 — each one directly under its header in row 1 — and are
  one width, all of it derived from `MU_LANE1_COL` and `MU_LANE_W` so a lane cannot drift off the
  header it belongs to.
- **Lane, 11 cells**: three for the note and the bar as far as it reaches; the last cell is `*`
  when the note carries commands, or `~` when its onset is off-grid (the bar then stops a cell
  short). A rest shows `...` at its onset; a command that fires where no note starts shows its
  mnemonic (`WAV=4`). A tie draws one continuous bar. Each lane is drawn in its voice's colour —
  red, yellow, cyan, the same three the dots use — and the text colour goes back to white for the
  row numbers and everything outside the lanes.
- **Cursor**: the cell at `(row, voice)` is drawn in the highlight colour and the row number is
  prefixed `>`; the control bar names the voice. No reverse-video glyph is needed, so no cell
  has to be restored by a writer that cannot restore it.
- **Playhead**: the sounding row. The **cursor is the playhead**: the `>` in column 0 moves to
  the row the music is on, and stop leaves it there. The window follows it, always.
- **The control bar answers the mouse**: the item under the pointer is redrawn in reverse video
  and returns to plain when the pointer leaves it, and `PLAY` stays reversed for the whole pass.
  Hover and "doing something now" both read the same per-column hit map the click route uses, so
  a highlight cannot name a different cell than a click would act on. The reversed cells are
  framebuffer writes (`kBlitCharRev`), drawn only while the task holds the display; the plain
  bar goes through `kPrintCharAt`, so it travels with the surface either way. A cell that was
  reversed is put back with `kBlitChar`, never by the text writer alone: `kPrintCharAt` keeps a
  cell's background nibble, and a reversed cell's background is the text colour.

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
`kPtr` = the image, `kArg` = `MU_MUS_DELAY` (one tick per jiffy), `kArg+1` =
`MU_MUS_REPEATS` (**1 ? one pass**, because the editor's playhead has no way to follow a
restart; see step 10), `kArg+2/3` = 0/0 (no transpose of our own ? `kSidPlay` adds the
machine's pitch shift to `kArg+2` itself, so a stray value there plays the tune an octave off). Stop is `kSidHush`, sent twice (once directly, once a yield later to cover
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
- **Mapping**: the renderer writes `(pointer -> row)` into a per-voice ring (`muHist*`, 32
  entries) for every event it walks, so a row is a lookup rather than a second walk of the
  tune. A *change* of the published pointer means an event has just begun, and only then is the
  row taken (`muPhSync`); a forward match more than `MU_SYNC_WIN` (4) rows from the clock is a
  ring entry an earlier pass left behind and is ignored. A backwards match is taken only when
  the **walker's own row** agrees with it ? that is what makes a `HED`/`TAL` section loop
  followable while a stale entry is not.
- **Between anchors the clock interpolates**: a row is `unit x delay` scheduler ticks plus the
  time the player spends on its own work, which is charged as one extra tick every
  `MU_PH_SLOW` (4) rows. `tickCount` is the same CIA #1 Timer A clock the player's jiffy uses,
  so the two agree at any machine speed. This constant is *tuned*, not measured ? see step 10.
- The block is read **only while our own child is inside `kSidPlay`**; otherwise it may be
  another task's play, or stale.

**The view rewinds with the music, never on its own.** `muRewindView` is the one place that takes
the score home: window top 0, the history ring, the anchor and the cursor's row, then a full
redraw — so the head's `BAR` and `R` and the `>` all follow. It runs from `muPlayRewind` (a pass
ended), and from `muPtStart` through `muPlayHome`, because the child always plays from the tune's
first row. A stop is the exception that proves it: the cursor stays where the music stopped, which
is where editing resumes, and the next `SPACE` takes it home with the music rather than leaving the
window on rows the music has not reached yet.

**PLAY only repaints what is not already home.** `muPlayHome` asks three questions: window top 0?
cursor on the first row? If both are true — a fresh editor, or a pass that has just rewound — the
score on screen *is* the row 0 score and nothing is drawn at all. With the window at row 0 but the
cursor elsewhere it moves the marker and redraws the head and nothing more, because the rows below
are already right. Only a scrolled window has to be walked and repainted.

**The rewind must run BEFORE the pass is armed** (`muPtStart` goes home, then sets `muPlayOn`, then
stamps `muPhLast`). The redraw is the slow part; if the child is already playing while it runs,
the clock is stamped several rows behind the music — and a first match more than `MU_SYNC_WIN` (4)
rows ahead is refused as a stale ring entry, so the playhead never catches up and the whole pass
runs out of step.

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
| screens | `kAllocScreen` mode 0, border `$0b`, then `kBlinkReset` inside an `SEI` window immediately before `kSwitchView` (the out-going surface's live blink block must be un-drawn before `surfSave` copies it), then `kDisableCursor` and `kSetScrollRegion(2,20)` — the grid only, so nothing can scroll the keyboard — and `kClearScreen` once after the switch, because a recycled surface keeps the previous occupant's bitmap |
| pointer | `mouse.lib` + `sprite.lib` + the existing `pointer.spr` (already seeded by `build-reu.ps1`); `mouseKind` required — 0 is the `? no mouse` refusal; two sprites resolved once, **before** anything is drawn; `sprOn(0)` and `MOUSE_DISABLE` before exit |
| exit | `kSwitchView` to the parent **before** `kFreeScreen`, then `SYS_EXIT`; drain the key queue by capacity (16 reads) first |

Footprint: code target **≤ 12 KB** (48 pages) + mouse 2 + sprite 1 + filesys 10 + sid 13 + the
largest tune 18 = **93 of 109 pages**, margin 16.
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
- [x] **1.4 The keyboard is still.** Move the grid window down and up with the cursor keys past the region's ends — the piano and the control bar never move and are never overdrawn (*inspection only*: the region is `kSetScrollRegion(2,20)` and every draw is fixed-cell).
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
> marks the first row where the walk's pointer equals the published one, and moves the cursor to it.
> The window follows the playhead: `muPhFollow` recentres `muTop` when the cursor voice leaves the
> window. **Harness note:** the VICE binary monitor stops the CPU on a
> read, so a probe that does not `resume()` between samples leaves the player paused and the
> pointer looks frozen -- resume between samples.

- [x] **8.1 The block.** *`main.asm` asserts the 6-byte block is free of slot-shadow code at `$54A2` (`SID_POS_BLOCK` in `sid.asm`); `verify-map.ps1` passes with 119 and the 3-byte `54A8-54AA` pocket; `sid.lib` is 3324 bytes (still 13 pages), relocation verification OK -- the two literal `sta $54A2,x` / `sta $54A5,x` stores are position-independent, not fixups.*
- [x] **8.2 It is live.** *Verified in VICE on `commodo`: while playing, `$54A2-$54A7` holds each voice's next-command pointer (valid tune-image addresses that advance over the run); on `SPACE` stop they freeze. Read only while our own child plays.*
- [x] **8.3 Play.** *Verified on `katmand`: `SPACE` plays and the column-39 marker tracks the sounding row down the grid (rows 8 -> 12 -> 15 -> 18 over the run). `filltst`'s single note holds the marker at row 0.*
- [x] **8.4 Loops.** *Verified on `immig`: voice 1's published pointer walks `$821c -> $9860 -> $a064 -> $a46a` and then jumps **back to `$a06c`** (the section's `HED` stream) at 18.1 s, and the frame captured at that sample has the column-39 marker on row `01` — the top of the section. The wrap repeats on the next pass.*
- [x] **8.5 Follow.** *Verified on `katmand`: the window scrolls to keep the marker in view (the row-number column changes as `muTop` advances). Follow is not optional — there is no key for it.*
- [x] **8.5a One-line scroll.** *Implemented and verified in VICE on `immig`: the window moves one line at a time with the playhead pinned on the centre line (`MU_FOLLOW_CENTER = 8`), `kScrollScreen` moves the band's pixels and matrix up through `REU_BOUNCE`, and `muGdLine` draws only the line that appeared -- the event starting on it, or the place the still-sounding event covers (`muLastEnd`/`muLastRest`). The playhead is a lookup in `muHist*`, the per-voice `(pointer -> row)` ring the renderer fills as it walks (`MU_HIST = 32`), so there is no second walk of the tune per frame. Captures after 12 s, 30 s and 45 s of playback show a coherent band, with the head bar and row following the cursor.*
- [x] **8.5b Tick clock between events.** *Verified on `immig`, whose voices open with whole-note rests: the player publishes a position once per **event**, and a whole note is 128 grid rows, so a pointer-only playhead sits still for ~15 s and then jumps. The cursor is now stepped from the kernel's own scheduler-tick counter (`tickCount`, `$54A8`, incremented by the CIA #1 Timer A handler — the same 59.2 Hz clock the player's jiffy uses, not the 50.1 Hz `frameCount`): `muPhAnchor` stores the tick and the stream pointer on every published event, `muPhStep` accumulates elapsed ticks and advances one row per `muUnitVal`, so the cursor descends line by line through the rests and re-anchors on the next event. Captures at 5 s and 12 s of playback show the window following through the opening rests with all three lanes drawn.*
- [x] **8.6 Stop.** *Verified: a second `SPACE` silences at once, and a new play works immediately. The cursor stays where the music stopped -- it is the playhead, so there is nothing to erase.*
- [x] **8.7 Stop/play in the same instant.** *Verified: twelve `SPACE` presses 0.15 s apart leave the tune stopped with `$D404/$D40B/$D412` all `0` and the published pointers frozen, and the next `SPACE` plays again (`$D404 = $40`, pointers advancing) — the child never wedges and every later toggle behaves. Note `kSidHush` silences by zeroing the three voice-control registers; `$D418` keeps the library's `$08` filter setting, so the voice-control registers are what a stop is checked against.*
- [x] **8.8 No player.** *Verified: with the player child gone (killed from the shell while playing), `SPACE` reports `? no player` on musedit's OWN head row, the playhead is cleared, and the editor keeps running -- further `SPACE` presses report again, a note key still edits, and `Q` still quits cleanly. This is the same state a failed `sid.lib` load leaves, because a failed lazy load prints `could not load sid.lib` and then EXITS the calling task (`libFailExit`), so `kSidPlay` never returns and the parent must detect the dead child itself (`muPlayTick`); for the same reason every parent-side `kSidHush` is gated by `muSidHushSafe` -- an unguarded hush with no `sid.lib` loaded would exit musedit. The player is per-session: once the child is gone, PLAY reports rather than respawning.*
- [x] **8.9 No orphan.** *Verified: quit while playing (`SPACE` then `q` on `filltst`) reaps the child in `muMusicBye` before `kFreeScreen` and returns cleanly to the shell prompt.*
- [ ] **8.10 ZP.** F-key away and back during playback — the music is unbroken and the other task still runs.
- [ ] **8.11 Audition.** With the tune stopped, click piano keys across the whole range — each sounds at the pitch named in the readout (compare a few against `sidplay` playing a tune in that register), and no note sticks after about ten displayed frames. *(Not implemented: a piano click writes the note into the image and nothing plays it — `muNoteClick` is the only entry path and it calls no player.)*
- [ ] **8.12 Audition stands down.** With `sidplay` playing from another screen, click keys — no sound, no error, the editor keeps working; then play our own tune and confirm it is unaffected. *(Follows 8.11.)*

### Step 9 — Gates and release

- [x] **9.1 Budget.** `check-pool-budget.ps1` exit 0, `musedit` at its measured
  footprint, with filesys, sid, mouse and sprite counted and the tune pages counted.
  The running figure is in section 11; the polish pass and the merge of the streamed
  player have each cost a page since.
- [x] **9.2 Builds.** *Verified: kernel 233 asserts 0 failed, tasks and REU exit 0, and `check-reu-current.ps1` reports the image current — no source changed since the build and all 49 binaries byte-identical.* Kernel, tasks and REU all exit 0; `check-reu-current.ps1` reports the image holds the binaries just built.
- [x] **9.3 Doc gates.** *Verified: `verify-map.ps1` all checks passed, `verify-jumptable-docs.ps1` clean, `verify-inventory-docs.ps1` current (198 addresses agree with the jump table; the call inventories were regenerated — 1270 kernel and 237 library call sites), `check-links.py` 0 unresolved, `check-ascii.py` OK, `scan-history.py` 0 flagged lines.* `verify-map.ps1`, `verify-jumptable-docs.ps1`, `verify-inventory-docs.ps1`, `check-links.py`, `check-ascii.py`, `scan-history.py` — all exit 0 (the last is advisory but read it).
- [x] **9.4 Wiring.** *Done: `tools/tasks.json` and `project-config.json` `sources` already named it; added to `README.md` (feature bullet + guide link), `docs/features.md` (Sound), `docs/quick-start.md` (the `run <filename>` task list), `tools/quickstart.binaries.md` (the binaries task table) and `publish-binaries.ps1`'s `$guides` — without that last one the guide would never ship. The task binary needs no publish wiring: it travels inside `reu/REU.bin`.* `tools/tasks.json`, `project-config.json` `sources`, `README.md`'s task table, `docs/features.md`, `docs/quick-start.md`, `tools/quickstart.binaries.md` and `publish-binaries.ps1`'s `$guides` all name `musedit` / `docs/musedit.md`.
- [ ] **9.5 A fresh machine.** Freshly seeded REU, one sitting: open, enter notes, play, save, quit, reopen, play — no leftover state.

### Step 10 ? Parked (deliberately not done)

Each item below is understood, costed and **not** in the tree. They are here so the reasoning is
not rediscovered from scratch.

- [x] **10.1a Rows that draw nothing are no longer walked.** *Done: with a note's duration written on its onset row, the rows an event covers are blank, so `muGdEvent` draws its onset and nothing else ? a whole note used to cost 128 iterations of the continuation loop, and the scroll path's continuation scan (which painted a blank row) is gone with it. `muLastEnd`/`muLastRest`/`muLastStart`, `muGdPastRow`, `muGdLeft` and the `muGdFirst`/`muGdRest` flags went with the code that read them; the task dropped a pool page (91 -> 90). What is left on an arriving line is the per-event walk (`muStep`) plus six or so `kPrintCharAt` calls per event that actually starts on it.*
- [ ] **10.1b The scroll itself is the remaining cost.** A scroll is `kScrollScreen`
  (one REU move of the band) plus `muGdLine`, and `muGdLine` writes the new line cell by cell ?
  up to 108 kernel calls across three lanes, ~30 ms. On a dense row that is more than a frame, so
  the band can dip behind the music even though nothing is repainted. The scroll itself is
  cheap; the character drawing is the limit.
  `kScrollScreen` moves the band's pixels ? 19 rows x 320 bytes ?
  through `REU_BOUNCE` in two passes, which is ~11.5k cycles (~11.5 ms at 1 MHz PAL) against a
  row period of ~17 ms at 58 rows/s. That is the floor this design has, and it is why a dense
  tune can still dip. Run-blitting was considered and has nothing to bite on: the duration
  readout removed the `=` runs, so the only repeated characters left are the pitch/duration
  cells, which are 3-6 calls per event.
  *Options if it ever needs to be faster:* (a) skip the matrix half of the scroll when the band's
  colours are uniform (they are ? the kernel does it for a general surface, not for us);
  (b) a **row ring** instead of a scroll ? draw the arriving row in place of the row that left,
  no movement at all, at the cost of the row numbers no longer reading top-to-bottom;
  (c) a row copy through `REU_BOUNCE` for rows that repeat the one above ? pointless now that
  covered rows are blank. Note that a row copy needs no kernel change if it is ever wanted: the
  shadow's row address is `gfxCellRowLutLo/Hi[row] + taskShadowHi[curTask]` (both task-visible)
  and the pixels come from `gfxRowLutLo/Hi` (`$1977`/`$1990`).
- [ ] **10.2 The row clock is a tuned constant, not a measurement.** `MU_PH_SLOW` (4) is the
  player's own per-jiffy overhead expressed as "one extra tick every N rows", tuned by eye at
  100 % and 200 %. It is right for the tunes and speeds tested and it *is* a pure ratio, so it is
  the single knob if a future tune drifts (lower = the display slows down, higher = it speeds
  up). **Verified working at 100 % and 200 %; do not touch without a reason.**
  *A measurement was tried and abandoned:* dividing the ticks since an anchor by the rows the
  clock had itself advanced is circular ? those rows are produced by the very value being
  measured, so the answer can only agree with whatever it started from, and subtracting the
  leftover biased it low (the display ran fast). If this is ever revisited, the independent
  observation to use is the **anchor rows from the tune** (`muValLo - muPhAnchorLo`, which is the
  tune's own spacing), with the leftover *not* subtracted, and only on a window of at least four
  rows so a one-row event cannot supply a noisy divisor.
- [ ] **10.3 Repeat-forever is not supported by the editor.** The child is spawned for one pass
  (`MU_MUS_REPEATS = 1`); when it returns the parent rewinds. With `repeats = 0` the tune really
  restarts, and **three separate things break**: the ring holds only the pass's last 32 events
  per voice, so the tune's opening events are gone and there is no row to anchor on; a backwards
  match is rejected because the rewind test compares it with the *walker's* row, which is at the
  end of the pass, not the start; and the walker itself never rewinds, so the arriving lines come
  in blank and the dots stop moving. *Fix:* detect the restart (published pointer below the
  clock while the walker is at the end) and run `muRewindView` ? zero `muHistN`, `muTop`,
  the accumulator and the anchor, put the cursor back on the first row, then repaint ? before
  continuing. Not wanted today; a tracker that loops in the editor would need it.

### Step 11 — The polish pass after the first sitting (done)

Everything here came out of using the editor with real tunes. Each item states what the editor
does now; the observations behind them live in the git log, not here.

- [x] **11.1 The note reads as its duration.** A grid cell shows the pitch and then the length in
  the format's own numbers (`C#2 16`, `G-4 4`), not a run of `=` bars: `muDurNum` maps the length
  class (whole..64th) to 1, 2, 4, 8, 16, 32, 64 and a utility length prints nothing. Only the
  onset row carries anything — the rows an event covers are blank — which is also what made the
  scroll cheap (10.1a) and left no runs for a run blitter (10.1b).
- [x] **11.2 A dot per voice on the keyboard.** `muDots` pins one sprite on the key each voice is
  sounding, placed from the pitch the **walker's** ring recorded for the event at the playhead
  (never the player's pointer-1), red / yellow / cyan for voices 1-3, naturals one row lower than
  accidentals, and the ninth x bit carried in `C` for `kSpritePos`. `kSpriteCollide` is not used:
  the dots are output, not input.
- [x] **11.3 The keyboard is three rows.** Four rows left the score too short, so
  `tools/gen-musedit-glyphs.py` emits `ROWS=3`, `BLACK_ROWS=2` and `muKeyBlackRows`, and the grid
  grew to rows 2-19.
- [x] **11.4 No lane separators.** The columns between lanes (15, 27) are spaces: a separator drawn
  only on a full redraw is a line that breaks as soon as a row scrolls in, so the lanes are told
  apart by their colours and their headers instead.
- [x] **11.5 Each lane wears its voice's colour.** `muGdCells` sets the text colour from
  `muVoiceCol` (2, 7, 3 — the same three the dots use) for the event it draws and restores white
  afterwards, so the row numbers and the head stay plain. The colour rides the matrix nibble, so
  it travels with a row when the band scrolls.
- [x] **11.6 The lanes sit under their headers.** `MU_LANE1_COL` (4), `MU_LANE_W` (11) and
  `MU_LANE_STEP` (12) generate `muLaneCol`/`muLaneW` *and* the click hit test, with asserts
  pinning 4/16/28 against "ROW VOICE 1     VOICE 2     VOICE 3". Voice 1's lane used to start a
  cell to the left of its own header.
- [x] **11.7 The control bar answers the mouse.** The item under the pointer is redrawn in reverse
  video and returns to plain when the pointer leaves; `PLAY` stays reversed for the whole pass.
  Hover and "running now" both read the same per-column hit map the click route uses, so they
  cannot disagree. Reversed cells are framebuffer writes (`kBlitCharRev`, drawn only while the
  task holds the display) and are put back with `kBlitChar`, because `kPrintCharAt` keeps a cell's
  background nibble and a reversed cell's background is the text colour.
- [x] **11.8 The head reads in decimal and follows the playhead.** `TEM` prints through `muNum16`,
  and `muPhHead` rewrites the `BAR`/`R` tail when the bar changes (the row alone otherwise). The
  row column is taken *after* ` R` is printed, which is the only order that puts the row in its
  own cells.
- [x] **11.9 The view rewinds with the music.** A pass that ends puts the cursor back on the first
  row as well as rewinding the score; a `SPACE` start goes home too (the child always plays from
  the tune's first row), and `muPlayHome` draws **only what is not already home**: nothing when
  the window is at row 0 with the cursor on the first row, the marker and the head when just the
  cursor is elsewhere, a full repaint only for a scrolled window. The rewind runs *before*
  `muPlayOn` is set — see §9 for why the order matters.

### Step 12 — Streaming the tune (unbounded tune size)

**Status: designed, not started.** Every number below was measured (see the note at the
end). The player's half is **built, released and merged** -- this is the editor's half,
and it is the reason the player's half was done first: it defines the entry point and
the window discipline that 12.1 and 12.7 below adopt.

**The problem.** `musedit` holds the whole `.mus` in one `kMalloc` block for its life: playback
hands those bytes to `sid.lib`, an edit changes them, a save writes them back. The block is
`ceil(size/256)` pages *on top of* the 93 pages of code and libraries, so the largest bundled tune
(18 pages) does not fit at all, and every page the editor grows costs a tune page. The goal is that
**tune size stops being a pool question**: the file ceiling is the filesystem's 16-bit size field,
64 KB, which is fourteen times the biggest tune we have.

**Measured, so the design is not guesswork:**

- The three voices are walked **linearly**: a 256-byte window per voice is reloaded 6/5/6 times
  across all of `katmand` (142 s, 2231 fetches) — 17 loads for a whole 18-page tune.
- A **single shared** window would thrash: the same tune would load 1724 times (~12/s), because
  the three streams start pages apart and each is fetched every jiffy. Per-voice windows are a
  requirement, not an optimisation.
- A full-band redraw **already** re-walks the tune from tick 0 (`muDgWalk` → `muSimInit`; the code
  says "a second or two of work on a long tune"), so the page traffic a walk adds is ~2 % of a cost
  the editor already pays.
- One page fetch is `kReuDmaRange` (kernel, 24-bit FS offset, multi-bank handled) ≈ 400 cycles for
  256 B; the write side is `kReuWriteAt` (filesys, already resident in this task).

**The design.**

- **A scratch file in the FS is the working image.** At open the tune is copied into a scratch
  file page by page (~15 ms); the real file is untouched until `SAVE`, so "quit without saving"
  stays safe. `SAVE` writes the *dirty pages* back and deletes the scratch; a *resize* is an FS
  create + page copy, and a save that leaves the scratch complete can **delete the original and
  `kReuRename` the scratch over it** (the resize path already does exactly that).
- **Windows, not a block:** one **page-aligned** 256-byte read window per voice (3 pages) plus one
  dirty write page, and they are page-aligned for the reason the player's are: the address of an
  offset is then simply `winHi : offsetLo`, with no per-access arithmetic and no `winOfs` to keep
  in step. It is safe for commands because every command is a 2-byte pair at an **even** offset,
  so a command and its option byte can never straddle the end of a window -- crossing a page
  refills the window before the next command is read, exactly as `sidWinSeat` does. The editor and
  the player therefore seat the *same* windows over the *same* file, which is what makes 12.7 a
  handover rather than a second implementation.
  loading a second page.
- **The accessors funnel** is `muPtrToK` today; every image access becomes "seat the window for
  this voice, then read `win + (ofs - winOfs)`". The walker keeps `muPtrLo/Hi,x` as **file
  offsets** rather than RAM addresses — which also means the published block `$54A2` and the
  editor's playhead ring agree by construction, and a block move can no longer invalidate either.
- **Range operations are page-chunked:** `muRecLoad`, `muInMoved`, `muDgMoved`, `muNcReplace`,
  `muSaveNow` and the grow-by-copy path all copy across a page boundary today; each becomes a loop
  over windows. Undo is unaffected — its range-diff records already keep the old bytes in RAM.

**The work.** In order, each a checkpoint that builds and passes the gates on its own:

- [x] **12.1 Window service.** `muWinSeat` (X = voice, `muWinOfs` = the offset -> `kPtr` =
      the address of that offset in the window), `muWinInit` (three pages, taken once),
      `muWinReset`, and `muWinCheck`. The page source is **the image block** for now, so the
      editor behaves exactly as it did and the refactor of the 60 accessors can be proved
      against a source we know is correct before the storage moves; `muWinFill` is the only
      routine that changes for the file. The **write** entry is deliberately NOT here: a window
      holds a copy, so a write must also reach the page it came from, and there is no such page
      yet -- it lands with 12.5 and the file behind it.
      `muWinCheck` runs at startup and compares each page against an independent pointer walk
      (`muWcPt`), because a lost carry between the block's high byte and the page number hands
      back another page's bytes and the tune still looks plausible.
      **Not `kReuDmaRange`:** the editor HOLDS `filesys.lib`, so it can use `kReuReadAt` /
      `kReuWriteAt` with a caller-owned record and never touch the kernel-global `reuFsPtr`
      (the player cannot, which is why it takes the other route).
- [ ] **12.2 Load.** `muLoad` copies the file into a scratch file (create + page loop) instead of
      `kMalloc`; `MU_IMG`/`MU_PAGES` become the record + size; the no-room refusal goes away
      (only "no scratch room" and "file bigger than 64 KB" remain).
- [x] **12.3 The walker.** DONE. `muPtrLo/Hi` hold a file offset per voice; `muPtrToK` seats
      that voice's window on the offset and returns the address; `muSimInit` seeds the three
      streams at 8 then each length word; `muPtrBump` was already offset-shaped. Three
      conversions disappeared with it -- the base subtraction in `muHistAdd` and in
      `muSkRecord`, and `muStep`'s rebuild of an address in `muAcc`. Code 17005 -> 16969 bytes.
      `muPtrToK` documented as clobbering A, Y, `kPtr`, `srcPtr` and returning C=0.

- [ ] **12.4 Drawing.** `muGdCells`/`muGdPitch`/`muNumDur`/`muCmdN` and the header readers
      (`muFindTempo`, `muUnitNow`, `muNameAt`, the description) through the window service.
- [ ] **12.5 Editing.** The 23 write sites (`muNcReplace`, `muInMoved`, `muDgMoved`, the length
      writer, the gap opener, `muPe*`) with page-chunked range ops and dirty marking.
- [ ] **12.6 Save and grow.** Dirty pages → the file; a resize creates, copies and renames;
      the round-trip byte-for-byte property is re-checked.
- [ ] **12.7 Playback.** `kSidPlay` in **mode 1** -- `A = 1` with `kPtr` at a 9-byte config: the
      scratch file's 3-byte FS offset, then the three window pointers. The windows are the
      editor's own (the caller keeps them; the player copies the config at entry), so the child
      pages the same scratch file the editor edits and an edit is heard on the next pass with no
      copy at all.
- [ ] **12.8 Budget and gates.** `musedit`'s tune block (up to 18 pages today) becomes a fixed
      ~4-6: three read windows, one write page, and the scratch record. `check-pool-budget.ps1`'s
      rows updated; `verify-map` / inventories / the guide.
- [ ] **12.9 Test pass.** Every bundled tune opens, plays and saves; a synthetic tune at the 64 KB
      ceiling opens and scrolls; undo/redo, insert, delete, transpose and the command panel behave;
      `quit` without saving leaves the file byte-identical.

**Risks.** The 60-site accessor refactor is where regressions would come from (the edit paths need
their own pass); a page that an edit straddles must be written back exactly once; and the
`kReuDmaRange` call clobbers `kArg..kArg+5`, so no kernel state may be live across it.

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

## 16. Step 13 -- finish the walk, then make the editor phrase-native

The streaming storage (steps 12.2-12.7) is in and `musedit` holds 100 of 109 pool pages. What
is left is not storage: it is the **walk** (the editor's copy of the player, which draws the
lanes) and the **playhead** that follows it. Two rules govern this step.

**No fix ships on a hypothesis.** Four candidate causes for `immig` drifting have been tried
and measured away (phrase errors fatal, phrase resolution newest-match, three-voice anchoring,
the undefined-call frame). Every one cost a build and a listening test. So each step below
starts with a measurement that can fail, and the measurement is kept as a gate rather than
thrown away.

**The walk and the player are two implementations of one specification**, and the host model
(`tools/mus-sim.py`) is the reference. Any disagreement is the C64's, and the way to find it is
a byte-level dump of what the walk read, compared with the file -- not an argument about the
format.

### 13.1 The walk's ring is the evidence (done first)

`muHistAdd` already records, for every event the grid walk visits, its image offset, the row
it was drawn on and its option byte -- that ring is *exactly* what the lanes were drawn from.
`SHIFT+L` prints it for the cursor's voice, newest first, as `RRRR OOOO OO`
(`muHistShow`). Read at bar 6 on `immig`, three outcomes are distinguishable with one keypress:

- **rows step by the length the file gives** -- the walk read the bytes correctly, and the
  fault is in the playhead's anchor, so 13.2 is the fix;
- **a row jumps much further than the file's length** -- a length was read or decoded wrong,
  and the offset on that line is the byte to look at;
- **nothing at all** -- the walk stopped for that voice, and `V`'s `ERRn` names why.

### 13.2 The playhead anchors structurally

**What the evidence says (immig, after 13.1's fixes).** The walk now reproduces `tools/mus-sim.py`
exactly — `v` reports `t=7632 e=231,215,663` for all three voices — and the playhead still
stalls around bar 6, jumps to the right place, stalls again and finally drifts. That rules out
the walk. What remains is the anchor.

`muPhSync` re-anchors only when the player's published pointer *changes* **and** `muHistFind`
finds that exact offset in the ring. Two things keep that from happening often:

- the ring is rebuilt only by a **grid redraw**, so between scrolls it is frozen while the
  music moves through the window;
- the ring holds only *stepped events*, so a published pointer that names a **command pair**
  (the player publishes the next command, which may be a `UTL`, a filter write, anything)
  matches nothing at all.

With no anchor the playhead free-runs on `muPhStep`, which is deliberately a quarter slow, so
it falls behind; when the view eventually scrolls, the redraw repopulates the ring, the anchor
lands and the marker jumps forward to where the music actually is. That is exactly the
"stops, picks up in the right place, stops again, then drifts" report.

**The fix is to stop requiring an exact offset match.** The playhead is *between* events; what
it needs is the row of the event sounding at it, which is the newest ring entry at or below
the playhead's row — the same rule `muDvLp` already uses for the keyboard dots. That anchor is
always available while the window holds the playhead, survives the ring going stale between
redraws, and cannot be defeated by a pointer that names a command. Acceptance: `immig` in step
from the first bar to the last, and the regression set (`tetris`, `commodo`, `katmand`,
`fsonata`) unchanged.

### 13.3 A gate that keeps them equal

`tools/mus-sim.py` gains `--walk`, which prints `row offset` for every event of a voice; the
editor's ring dump for the same window is compared with it by
`tools/check-musedit-walk.py`. Plan §15.1's equivalence risk is then closed by a test that can
be re-run, not by a one-off listening session.

### 13.4 Phrase-native editing

The format's structure commands are what make it a language rather than a list of notes, and
the editor currently treats every one of them as an opaque "this event has commands" mark
(`*`). In order:

- **a. Show it.** A `DEF n`, `CALL n`, `END`, `HLT`, `TAL`, `HED n`, `TEM`, `UTL` or `UTV`
  event draws its mnemonic in the pitch cells, keeping the length column, so a lane reads as
  music *and* structure.
- **b. Move through it.** `D` on a call row moves the cursor to that phrase's `DEF` (the walk
  already holds the address); `D` on a definition lists its call sites, nearest first.
- **c. Author it.** The `CMD` key (printed on the bar, not yet wired) opens the mnemonic menu
  for every command the player implements -- `TEM`, `UTL`, `UTV`, `HED`, `TAL`, `HLT`, `END`,
  `DEF`, `CALL`, the 2- and 3-type groups (envelope, waveform/pulse, frequency, filter,
  modulation, volume) and the 16-23 phrase form. Adding or removing a command pair is a
  two-byte gap move under the existing undo record.
- **d. Edit a definition knowingly.** Inside a `DEF` body an edit changes every call site, so
  the status line names the phrase and its call count; inside a *replay* region there are no
  bytes, so insert and delete stay refused and the cursor snaps to the body event.
- **e. Keep the accounting.** Budget: musedit's margin is 9 pages; a-c are ~2-3 pages
  (mnemonic table, menu, the `D` key). `check-pool-budget.ps1` is the gate, and `basic`-style
  exact fits are refused rather than warned about.

Order matters: 13.1-13.3 before 13.4, because a phrase editor built on a walk that is not
proven will hide the next defect behind a new feature.

## 17. Documentation

All of it is written and current: `docs/musedit.md` (the guide, and it is in
`publish-binaries.ps1`'s `$guides`, so it ships), `README.md` (the task table and a link),
`docs/features.md`, `docs/quick-start.md`, `tools/quickstart.binaries.md`, `docs/lib-sid.md` (the
published position block), `docs/memory-map.md` (the pinned pocket and the totals) and
`reference/sidplayer-mus-file-format.md` (the bit-7 correction). A new gate would also need a row
in `docs/development.md`; none was added.
