# `musedit` — tracker-style editor for `.mus` tunes — implementation plan

> **Status: the editor is built, wired and shipped.** `musedit.asm`, `musedit.inc` and the
> generated `musedit-glyphs.inc` / `musedit-tables.inc` build to **103 pages of 109** (6 of
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
> at `$54A2` and the jiffies it has played in this pass at `$54A8`, the cursor is the playhead and
> its row is that count divided by the row's length, the window follows it a line at a time, every
> edit path reports `? stop first` while it runs,
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
> **8.11/8.12** (the audition is built and in use — what is left is the two checks: another
> task's music playing while the keyboard is hovered, and our own play afterwards), **9.5** (a
> fresh-machine sitting), and the parked **10.1b/10.2/10.3** (scroll REU cost, the tuned row
> clock, repeat-forever). 13.4d and 6.18 (phrase bodies) are **verified on hardware**. 6.13/6.14
> (mouse-only and keyboard note sessions) are still unbuilt. The `,` tie key and the bar's
> `DUR`/`TIE` click cells are still unwired; step 7 (the command panel) is untouched. Bug #192 (a
> `CALL`/`DEF` of phrases 16-23 was drawn as another command) is **fixed** and needs its check.
> **13.4f is built and verified:** rows become a function of the song tick with one segment per
> tempo, a bar is one whole note in the segment's rows (`rows × unit == tempo`), and the head,
> `SHIFT+I`, `SHIFT+U` and the playhead all read the segment instead of one global unit. The
> playhead reads it through the **tick each drawn line was walked at**, not through a segment the
> walk happens to be in, because the walk draws below the cursor and would otherwise hand the
> playhead a tempo the music has not reached (bug #193).
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

Two refusals. `? stop first` while playing. `? phrase n xc` when the row falls inside a **phrase
body**: those rows are the shared definition, so an insert there would move time in every call
site at once — and the message names the phrase and how many `CALL` sites play it. A row reached
through a `HED`/`TAL` repeat maps back to the body event, so an insert there changes every pass —
the status line names the pass (`repeat`) so it is not a surprise.

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
DUR 16. TIE PLY RST INS DEL CMD UND SAV Q
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
  mnemonic (`WAV=4`). A rest shows `...` at its onset. A tie is drawn after its duration as `TIE`,
  read straight from the format's tie bit on the event, so a note struck once and held shows its
  attack and then its ties. Each lane is drawn in its voice's colour —
  red, yellow, cyan, the same three the dots use — and the text colour goes back to white for the
  row numbers and everything outside the lanes.
- **Cursor**: the cell at `(row, voice)` is drawn in the highlight colour and the row number is
  prefixed `>`; the control bar names the voice. No reverse-video glyph is needed, so no cell
  has to be restored by a writer that cannot restore it.
- **Playhead**: the sounding row. The **cursor is the playhead**: the `>` in column 0 moves to
  the row the music is on, and stop leaves it there. The window follows it, always.
- **The control bar answers the mouse**: the item under the pointer is redrawn in reverse video
  and returns to plain when the pointer leaves it, and `PLAY` stays reversed for the whole pass.
  Its items are `DUR`, `TIE`, `PLY`, `RST`, `INS`, `DEL`, `CMD`, `UND`, `SAV` and `Q`, one code
  each in `muBarMap`, so every labelled cell has a handler, and the row is exactly 40 columns
  wide with the text filling it.
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
- **Glyphs**: 5 cell positions × 3 rows = **15 cells (15 B)**, because the pattern repeats
  every five cells. A cell is one byte rather than the eight a glyph blit takes: keys are
  vertical bars, so every pixel row of a cell carries the same pattern, and `muDrawPiano`
  expands it into the eight bytes at draw time. `tools/gen-musedit-glyphs.py` derives them from
  one geometry table, so a cell and the hit test can never disagree.
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
37-39, outside the scroll region, so nothing can scroll it away). Those three cells are also the
mode button: the pointer reverses them and a click toggles, so the readout is the control.
Overwrite is the default: entering a note replaces or splits. `INS` writes the note after the
event the cursor's line holds and opens the armed duration in all three voices, then leaves the
cursor on the new note, so a run of entries continues the tune forward. The mode changes nothing
but what the next note entry does — switching it does not touch the image and does not dirty the
tune.

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
behind 18 % per second. So the player publishes, in one contiguous block:

- **8 resident bytes at `$54A2-$54A9`** — `WorkNxtCmdLo[3]` at +0, `WorkNxtCmdHi[3]` at +3, then
  **`SID_POS_TICK`** at +6: a 16-bit count of the jiffies the player has played *this pass*. The
  kernel's own `tickCount` sits immediately after it at `$54AA`, pinned by an assert, so nothing
  may be inserted into the block.
- The pointers cost **two stores in the command-fetch path** of `sidplay.inc` (A already holds
  the value, so each is one `sta`), the counter one increment per jiffy in `sidTick` and two
  stores. `sid.asm` holds the `.const` aliases and `main.asm` the asserts.
- **The row is the count, less one jiffy, divided by the row's length.** A grid row is a fixed
  number of jiffies (`muUnitVal`, recomputed from the tempo in force), so `muPhSync` is one
  `kDiv16` of the tick by the unit — no anchor, no interpolation, no rate to drift and no window
  to fall outside of. The jiffy comes off because `sidInitSong` starts every voice's counter at 1
  and `sidTick` increments the count before it processes, so row 0 is fetched with the count
  already at 1: the raw quotient is one row boundary ahead of the row being played. A
  loop (`HED`/`TAL`) restarts the count with the music, and `sidInitSong` clears it where A is
  genuinely zero. See §13.2 for why every predicted-row scheme failed.
- The renderer still records `(offset -> row, pitch)` in a per-voice ring (`muHist*`, 32 entries)
  for every event it walks — but the ring feeds the **keyboard dots**, not the playhead.
- The block is read **only while our own child is inside `kSidPlay`**; otherwise it may be
  another task's play, or stale.

**The view rewinds with the music, never on its own.** `muRewindView` is the one place that takes
the score home: window top 0, the history ring and the cursor's row, then a full
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
the published tick has already advanced and the first row drawn is behind the music.

## 10. Files

`kReuFindFile` → `kMalloc` → `kReuLoad` on open. On save, `kReuOpen` first:

- **same size or smaller** → `kReuWriteAt` in place, zero-filling the tail when shorter;
- **larger** → check free space, `kReuCreate` the replacement under `<base>.tmp`, `kReuOpen` +
  `kReuWriteAt` to fill it, then `kReuDelete` the old file and `kReuRename` the scratch into place.
  That order is what keeps a full directory honest: a delete is a tombstone that frees neither slot
  nor data, so deleting first would leave the create nothing to land in. A resized overwrite still
  leaks the old extent until `compact` — and `compact` refuses while another live task exists, so it
  cannot run while `musedit` is open. **Every created file is sized to a 256-byte multiple**, zero
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
| screens | `kAllocScreen` mode 0, border `$0b`, then `kDisableCursor` (before the switch, like every other display-taking task: `cursorOn` is already 0 when the surface comes up), then `kBlinkReset` inside an `SEI` window immediately before `kSwitchView` (the out-going surface's live blink block must be un-drawn before `surfSave` copies it), then `kSetScrollRegion(2,20)` — the grid only, so nothing can scroll the keyboard — and `kClearScreen` once after the switch, because a recycled surface keeps the previous occupant's bitmap |
| pointer | `mouse.lib` + `sprite.lib` + the existing `pointer.spr` (already seeded by `build-reu.ps1`); `mouseKind` required — 0 is the `? no mouse` refusal; two sprites resolved once, **before** anything is drawn; `sprOn(0)` and `MOUSE_DISABLE` before exit |
| exit | `kSwitchView` to the parent **before** `kFreeScreen`, then `SYS_EXIT`; drain the key queue by capacity (16 reads) first |

Footprint, as `tools/check-pool-budget.ps1` measures it: code **69** pages + mouse 2 + sprite 1 +
filesys 10 + sid 15 + malloc 6 = **103 of 109 pages**, margin 6. The six malloc pages are the
window service — three page-aligned 256-byte read windows for the editor and three more handed to
the music child in `kSidPlay`'s mode-1 config — so the tune's own length costs nothing: it is paged
out of `<base>.wrk` (step 12).
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
| `tools/check-musedit-walk.py` | the host walk model (`mus-sim.py --walk`) against the editor's own grid lines, held in `tools/fixtures/` and captured from a running musedit |
| `tools/gen-musedit-tables.py` | emits `musedit-tables.inc` (`DataTempoTable` copy, note letters, mnemonic strings) with a `-Check` mode, like `gen-task-asm.ps1` |
| `tools/gen-musedit-glyphs.py` | one geometry table → the 15 piano cells and the two 40-byte pixel→semitone hit tables, emitted into `musedit-glyphs.inc` with a `-Check` mode, so a cell and its hit test cannot drift apart |
| `tools/check-reu-fs.ps1 -Extract <fsName> -Out <path>` | round-trip tests: extract a saved file and hash it against `music/<name>.mus` |
| `tools/mus-sim.py` | Python reference simulator for the C64 one; prints `(tick, voice, event)` for a `.mus`, so a debug view can be diffed against it |

The C64 harness for the simulator is a `SHIFT+I` debug line printing `V1 clock/ptr/hed/depth`,
so the values can be compared against `mus-sim.py` before the grid exists.

## 14. Steps

A step passes when every box under it is ticked and the previous step's boxes still pass.
"*inspection only*" marks a box that cannot be reached by hand and is ticked by reading the
named code path.

**How a box reads.** `[x]` means built and verified, with the evidence in italics. An open box
says which of the two it is: *built — needs a check* (the code is in, the VICE pass is not) or
*not built*. No open box is left to be interpreted.

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
- [x] **1.16 The control bar.** Click each of `DUR`, the duration value, `PLAY`, `CMD`, `SAVE` and
  `Q`, and the blank cells between them: every labelled cell reports `? not yet` on row 0 for
  about a second and then the head returns, `Q` quits, and a blank cell answers nothing. The dot
  cell (column 6) is a blank cell in `muBarMap` — it is a readout and answers nothing either.
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
- [x] **2.6 Grow saves.** *Verified on hardware, both the first create and a page-count
  increase.* An image that outgrew its file takes `muSaveNow`'s create path: `muSvPages` rounds it
  to pages, `muFreeSpace` is asked **before** anything is written, then the create, the page write
  and the rename, and `MU_FLEN` becomes whole pages. Two cases: `arose` (1014 bytes at slot 70) grew
  by six command pairs to 1016 and came back as **1024 bytes in a new slot**, with slot 70 left
  tombstoned (flags `$80`, size kept) -- four pages, i.e. the same page count rounded up. Then, to
  raise the page count, a temp image held `big.mus` = `arose.mus` behind a **1024-byte record**: one
  inserted pair made the image 1016, which no longer fits that record, and the save created
  **1280 bytes = five pages** where the file was four, with the 1016-byte image followed by zeros to
  the extent and `nextFreeOffset` up by exactly 2048 + 65280 + 1280.
- [x] **2.7 In-place save inside the slack.** *Verified on hardware, and the sharpest of the
  three.* When the image's length word still fits the opened record's length, `muSvInPlace` writes
  it back, no create happens and the extent does not move. Proved on a filesystem with **no room to
  create anything** (the data area raised to leave 68096 bytes free, which the editor's own
  `musedit.und` plus `<base>.wrk` take down to 768): inserting a pair on `immig` (936 bytes against
  a 932-byte record) answered `? no space`, because a create would have needed 1024; deleting the
  pair and then the `ATK` command (930 bytes) answered `? saved`, which only the in-place path can
  do with 768 bytes free. Afterwards `immig.mus` is still at slot 75, data offset 189826, size
  **932**, flags 0 -- and `nextFreeOffset` grew by exactly 2048 + 65280, so the allocator never
  moved.
- [x] **2.8 Full FS fails safely.** *Verified on hardware, and the one hole in it is fixed.*
  `muFreeSpace` (capacity minus the allocator's high-water mark) is compared with the pages needed
  before anything is written, so **`? no space` leaves the tune untouched** -- verified on a
  raised-high-water image, where a growing save was refused with `immig.mus` still at its slot,
  flags 0, while a shrinking save answered `? saved` (that is 2.7's proof, and it is the same run).
  **`? dir full` was destructive and no longer is.** The create path now builds the replacement under
  `<base>.tmp`, deletes the old file only once that is whole, and renames it into place, so a
  directory with no free slot refuses at the create with nothing deleted. Verified with all 512
  slots carrying a size: `? dir full`, and `immig.mus` still at slot 75 with flags 0 and its 932
  bytes; before the change the same state left that slot a nameless tombstone. Note the limit is a
  *directory* one, not a data one -- the table is 512 slots of 32 bytes and `dir` reports bytes, so
  a filesystem can be slot-full and still print an ample figure; ordinary session use runs out of
  data first (65,280 bytes per session against one slot), so this needs many small files to reach.
  **`? no fs` cannot be reached on a booted machine**: `reuFsEnabled` comes from the REU size at
  boot and the binaries themselves arrive through the filesystem, so a machine without one never
  reaches the editor. It stays the defensive branch it is.
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
another phrase — stops all three voices and leaves a code in `muErr`.
`muNoteJiffies` reads `muJifTem` (the running tempo) rather than `muTempo` (the tune's opening
tempo), which is what lets a mid-tune `TEM` reach every voice. The editor carries no readout of
its own: the walk is checked against `tools/mus-sim.py`, whose `--trace` is the reference the
checklist rows below were compared with.

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
by `SHIFT+U`), and `muDrawGrid` walks the same simulator, drawing each
event as its onset (a three-character pitch, or `...` for a rest) followed by a `=` run as long
as the length's rows. A rest shows only its onset, so silence stays visibly silent; the lane's last
cell says `?` when the event's onset fell between rows. The row numbers are counted from each
bar's first row (a bar is at most 64 rows, so two digits
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

- [x] **5.1 It reads as a tracker.** `run musedit commodo` — three lanes, each event's pitch and
  length on its onset row, rests as `...`, commands as `*`, the row numbers and bar count matching
  `mus-sim.py --grid` for the rows compared.
- [x] **5.2 The length is the note's own number, and the rows it covers are blank.** `C-2 4` is a
  quarter: `muDurNum` maps the length class (whole..64th) to 1, 2, 4, 8, 16, 32, 64 and a utility
  length prints nothing. A note is therefore one row of cells, not a run of `=` bars — which is
  what makes the scroll cost only what starts on the arriving line. A lane that looks empty below
  a long note is the note still sounding, not a gap: the grid draws onsets, and the keyboard dot
  is what shows which pitch is held.
- [x] **5.3 Cursor.** Cursor keys move the `>`; LEFT/RIGHT change voice; at the bottom row the window
  shifts a line, at the top row it moves back a whole page.
- [x] **5.4 Click placement.** Clicking a lane cell sets the voice and the row; the lane header row, a separator and the spare column answer nothing.
- [x] **5.5 `SHIFT+U` refines.** *Verified on hardware: opens at 48 rows to the bar, and the
  three reachable states cycle.* `muUnitCycle` (bound to `SHIFT+U`, `$55`) walks the row unit plain
  -> half -> third -> one jiffy, skipping any refinement that does not divide the tempo's row
  exactly (`muUcValid`) and wrapping from the finest back to plain, so the reachable states
  **cycle**. At tempo 144 that is three: 16, 48 and 144 rows to the bar, because 9 jiffies does not
  halve -- and a triplet tune opens already at the third (48), so its first press goes to the finest
  and the second wraps to plain. It is refused while the player runs (`? stop first`), like every
  other edit: the grid is drawn in the unit that was in force when it was painted, so refining it
  mid-pass would leave the rows and the playhead apart. A tune that contains a triplet or a second
  dot gets the third **at load**: the startup path runs `muUnitNow`, then `muTrScan` (a pair's first
  byte is a length only when its low two bits are 00, so the test is `& $83 == $80`), sets
  `muUnitRef = 2`, checks it with the same `muUcValid` and applies it -- all **before** `muDrawAll`,
  which is the ordering that makes the band open refined rather than repaint into it. A tempo whose
  plain unit is not a multiple of three keeps plain, because no row grid can express a third of it.
  **No other bundled tune contains one**: a scan of the original twelve finds not a single note
  length with bit 7 set -- the bit-7 bytes they do contain are command bytes, which the player
  routes on the low two bits (`01`/`10`/`11` are commands, `00` a length).

- [x] **5.6 Command markers.** A note's lane carries no command marker: step 13.4a draws every
  command on a line of its own.
- [x] **5.7 Nothing else is drawn.** `kClearScreen` at start-up and `muGridClear` on every redraw leave nothing behind.

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
- [ ] **6.13 A mouse-only session.** *Built — needs the sitting.* Enter twelve notes across three voices, change two pitches, rest one, undo two, save, never touching the computer keyboard, then decode: exactly those rows, and the same file as the same edits made from the keyboard.
- [ ] **6.14 From the keyboard.** *Built — needs the sitting.* The same twelve notes through the computer-keyboard note map instead of clicks — same file.
- [x] **6.15 Insert row.** *Verified on `instmv` (voice 1 a quarter C4, voice 2 an 8th rest then an 8th D4, voice 3 a quarter E4; all cross row 2): `i` at row 2 opens a 16th rest in all three voices -- voice 1 and 3 split their quarter (8th + 16th rest + 8th), voice 2 drops a 16th rest before the D4 onset -- everything below moves down one row, and the host model shows the blank row across all three. `muInsertTime` seeks and splices each voice in turn (a splice shifts the later voices, so each is re-seeked on the grown image), as one grouped undo action.*
- [x] **6.16 Insert bar.** *Verified: `SHIFT+I` on `filltst` at `TEM $90` (16 rows/bar) opens a whole-note rest (`08 00` = 144 jiffies = exactly 16 rows) before the note; the bar count comes from `muUnitBar`, so an inserted bar is always one bar.*
- [x] **6.17 Insert inside a note.** *The straddle case of `muInsertTime`: `i` one row into `filltst`'s quarter turns `10 99` into `14 99 18 00 14 99` -- the note ends on the cursor's row (8th), one 16th of silence, then the same pitch resumes with a fresh attack (plain, not tied, because the previous event is the rest). The decoder shows the split pairs; total length is the event plus the inserted row.*
- [x] **6.18 Insert refuses inside a phrase.** *Verified on hardware, with 13.4d: `i` inside
  phrase 0's body on `immig` answers `? phrase 0 x4` and writes nothing; `SHIFT+INST/DEL` answers
  the same; a top-level insert is not refused.* `I` while playing reports
  `? stop first` (`muPlaying` guards every mutating path), and inside a phrase body it now
  **refuses** with `? phrase n xc` rather than editing silently: `muPhBlock` walks to the cursor's
  row once (the walk takes all three voices with it) and refuses when any voice the insert can
  write to is inside a body. `SHIFT+I` goes through the same door. `SHIFT+INST/DEL` is refused
  by `muPhHere` with the same message — it takes time *out* of the body. A command pair takes no
  time, so removing one is still allowed: it cannot desynchronise anything. The check is on
  `immig`: `i` and `SHIFT+INST/DEL` inside phrase 0's body (refused), `i` below its `END`
  (allowed).
- [x] **6.19 Insert mode.** *Verified on `ins1` and (the mode byte itself) on `filltst`: `M` toggles a mode byte shown as `OVR`/`INS` at the right of row 1 (cols 37-39, outside the scroll region). In `INS`, entering a note writes it **after** the event the cursor's line holds -- the boundary is that event's end row (`muSekEnd`), not the cursor's own line -- opening the note's length in all three voices, and the cursor then steps onto the new note. A run of entries therefore writes the tune forward: `a` in `OVR`, then `M`, then `s d f t` gives voice 1 `18 99 18 9a 18 9b 18 9c 18 5c 01 4f` (C-4 D-4 E-4 F-4 F#4, HLT last, the band reading `01 C-4` .. `05 F#4`), where a note-below-the-cursor-that-stays rule would have reversed it. Each entry is one grouped `Z`. `M` again returns to `OVR`, where a note replaces or splits, and toggling the mode never touches the image. A gap, a row past the last event and a row inside a longer event are entered on the cursor's own row (`muIvAppend`, `muIvStraddle`), and a voice that has already halted contributes nothing.*
- [x] **6.20 One action.** *Verified: `i` on `instmv` (three voices) undoes to the byte-identical original with a single `Z`, and redoes with one `X`. The undo stack groups the three per-voice splices via a bit-7 "group start" flag on the first record; undo unwinds the group newest-first (highest offset first, so closing a gap never invalidates a lower record), redo replays it oldest-first.*
- [x] **6.21 Dots and triplets.** *Verified (part of 6.7's run, and on screen in `arose`).* **Entering:** `1`-`7` arm the class (`muDurTab`: W H Q 8 16 32 64), then `.` cycles the dot state (`muDotCycle`) through plain -> dotted -> double-dotted -> triplet -> plain, held in `muDurArm` bits 5 and 7; `8`/`9` arm UTL/UTV instead, which take no dots. The armed value is what the next note is written with, so a dot is chosen before the note, not after it. **Displaying:** the bar's `DUR` value cells and its dot cell are repainted by `muDrawArm` through `kPrintCharAt` — cells 4-5 carry the duration (`16`, `W`, `Q`, `UL`, `UV` …) and the dot cell shows `muDotCh` for `(muDurArm & $a0) >> 5`: blank plain, `.` dotted, `:` double-dotted, `*` triplet. **On the note:** the same three marks follow the duration in the lane, from the event's own bits (6.22).
- [ ] **6.22 A dot on the note itself.** *Built — needs a check.* The lane prints the duration's value and then the dot mark, so a dotted eighth reads `8.` where a plain one reads `8` and a triplet reads `8*`; the characters come from the same `muDotCh` table the bar uses, indexed by `(duration & $a0) >> 5`, so the two readouts cannot disagree. The triplet mark is `*` and **not** a digit: appended straight after the value, a `3` would read as part of the number (`83` is a triplet eighth). Two rules make it fit: the lane's **last** column is the `?` that marks an off-grid row, so a squeezed event (`muPendSp = 0`) takes no mark; and without that mark the widest lane is pitch (3) + space + value (2) + mark (1) + ` TIE` (4) = 11 = `MU_LANE_W`. A utility length still prints no value, and takes no mark. Verified on screen in `arose`: `G-4 8.`, `E-5 2.`, `C-4 8.`, `D-5 8.`, plain `G-4 16`, a rest `... 4` with no mark, the bar reading `DUR 16.` and `DUR 16:` as it cycles, and **no `?`** anywhere at the refined unit. The remaining check is the `*` itself and a tied dotted note.



### Step 7 — Authoring commands

One prompt on row 0, nine structural commands, operand in digits. The format has 49 command
ids and the editor draws a line for every one of them (13.4a), but only the nine that set the
tune up — `TEM`, `UTL`, `UTV`, `HED`, `DEF`, `CALL`, `TAL`, `HLT`, `END` — can be *typed*: a
sound command's operand is a timing or a waveform number that belongs to the pattern rather
than to the song, so those lines are read and not written. `muObjEnc` turns (id, operand) into
the pair, `muObjFind` finds an existing pair at a boundary by id, `muMnPrint` draws the field.

- [x] **7.1 Values clamp.** *`mqMax` bounds each command: 255 for `TEM`/`UTL`/`UTV`/`HED`, 23 for `DEF`/`CALL`, none for `TAL`/`HLT`/`END`. The digit build stops at the maximum rather than wrapping (`mqValAcc` compares before it sets), and picking a mnemonic with a lower maximum clamps a value already typed (`mqDraw`). DEL narrows the value by division, so it needs no bound.*
- [x] **7.2 One of every command the prompt offers.** *Verified in VICE: each of the nine placed from the prompt, saved, extracted and decoded — each appears as the documented pair. A replaced operand changes exactly its own byte; an inserted pair moves the voice's length word by two and shifts the tail by two.*
- [x] **7.3 Removal.** *`SHIFT+INST/DEL` on a command line removes the pair through `muDelGap`, matching by id alone (the operand is not part of the match), and insert -> redo -> delete leaves the file byte-identical to the original.*
- [x] **7.4 Undo is one action.** *A command edit's undo record is the pair alone, so it undoes as one action and no row moves.*
- [x] **7.5 A command needs a boundary.** *A pair consumes no time, so its line only ever sits above a note or a rest: a row in the middle of a duration answers `? no event here` rather than splitting it, because the format has nothing to split it with. The prompt refuses there too — the editor offers no way to place what the format cannot hold.*
- [x] **7.6 Utility lengths.** *`8` and `9` arm the duration the next note is written with to UTL and UTV (`muArmKey`), and the armed value round-trips.*
- [x] **7.7 The sound commands.** *Built; verified on hardware* (`commodo`: `C` on voice 1's `MS# 1`
  line opens the prompt on `MS# 1`; CRSR walks to `P-W`, `1035` and `ENT` write it, and re-opening
  after moving the cursor away and back shows `P-W 1035` where the un-widened chain showed `11`;
  taking `999` on the same line replaces that pair rather than adding a second; `POR 5000` reads
  back as `POR 5000`). All 49 ids
  are writable and the gate sweeps 30,463 pairs across every one of them, including the four whose
  operand spans two bytes (`P-W` and `DTN` at 12 bits, `POR` at 14, `MS#` with its high two bits in
  the command byte). `C` on a sound line shows the operand it already has and takes a new one: the
  value is logged with both halves (`muLogHi`), `mchBlock` returns both, `mqHave` seeds
  `muObjVal`/`muObjValHi` from them, and `muObjFind` compares the pair 16-bit so the match is the
  command that is really there. The value is entered in decimal; the first digit typed starts
  afresh in both halves.

  **The design.** The 40 sound ids are eight encoding shapes, so the tables gain a `mqShape` byte
  per id and one encoder entry per shape instead of 40 bespoke routines:

  | shape | how the pair is built | ids |
  |---|---|---|
  | 0 | `opt = value \| base` | `F-C` `PNT` `RTP` `HLD` `P-S` `F-S` `SCA` `VDP` `VRT` `AUT` `TPS` `PVD` `PVR` `MAX` `FLG` `AUX` |
  | 1 | `opt = (value << 4) \| base` | `DCY` `REL` `VOL` `RES` |
  | 2 | `opt = (value << 3) \| base` | `ATK` `SUS` |
  | 3 | `opt = (value << 5) \| base` | `WAV` `F-M` `SRC` |
  | 4 | `opt = base \| (on ? bit : 0)` | `SNC` `FLT` `RNG` `LFO` `P&V` `3-O` `BMP` `F-X` `DST` |
  | 5 | `cmd = ((value >> 8) << 4) \| base`, `opt = value & 255` | `P-W` `DTN` |
  | 6 | `cmd = ((value >> 8) << 2) \| base`, `opt = value & 255` | `POR` |
  | 7 | `cmd = ((value >> 8) << 6) \| base`, `opt = value & 255` | `MS#` |

  The exact bases are `gen-mus.py`'s own encoders (`c_fc`, `c_ps`, `c_vrt`, `c_vdp`, `c_vol`,
  `c_atk`, `c_dcy`, `c_sus`, `c_rel`, `c_wav`, `c_res`, `c_fm`, `c_pw`, `c_voice3_on`) for the ids
  it has, and its `EXACT` decode table for the fixed-byte ones (`opt = value`, command byte as
  `EXACT` gives it). Slice 1 is shapes 0-3 (24 ids, all operands <= 255, so the existing decimal
  entry still fits and no 16-bit work is needed); slice 2 is shapes 5-7, which widen `MU_OBJVAL`
  and `MU_OBJMAX` to two bytes and add hex entry; slice 3 is shape 4 plus read-back, so `C` on an
  existing sound line shows its value and can change it.

  **The gate is in place** (`tools/check-mus-commands.py`, 7.9): it reads the tables out of
  `musedit.inc` and sweeps every value the prompt allows against `gen-mus.py`.

  **How the flag family was settled.** The eight one-bit commands all end in `011`; the option's
  bits 4-6 index the command and bit 3 is the bit acted on, and values 8-15 in that field are the
  wide 16-23 phrase form (which is why bit 7 is the phrase test). This came from the **verified read
  side** (`muDecOpt`/`moSw`, which real tunes exercise) and agrees with `gen-mus.py`'s
  `c_voice3_on` writing `$53`/`$5B` for index 5. **The vendored comments are contradictory**: RNG
  and 3-O both claim `0101n011`. The RNG comment is the wrong one; the dispatch index says
  `0010n011`. That note is now in `sidplay.inc` above the RNG handler, and `gen-mus.py`'s decoder
  follows the index instead of the comment (it also gained SRC/DST and RUP/RDN).

  **Cost, measured against the estimate:** ~2 code pages (380-500 bytes), no new pool pressure at
  the current margin of 10, plus two or three message records in the REU.
- [x] **7.8 The 16-23 phrase form.** *Verified on hardware, both halves.*
  `muObjEnc` encodes `DEF`/`CALL` below 16 with the plain nibble and 16-23 with the second form
  (`value - 16 + 8`, shifted into the high nibble, `$03`/`$0B` in the low byte), and `mqMax` caps
  both at 23. The player really does read that form — `ro_______11` in `sidplay.inc` falls through
  to `and #$07 / ora #$10` when the shifted option is 15, with the carry choosing CALL from DEF —
  so the encoding has to stay in step with `muCmPhr` (the walk) and `muDecOpt` (the renderer, which
  is what bug #192 was). `DEF 20` and `CALL 20` typed with `C` on `immig` draw as `DEF 20` and
  `CALL 20`, and the plain `DEF 0` beside them is unchanged. **The round trip also holds:** the two
  pairs were placed at the first boundary of `immig`, `SHIFT+S` answered `? saved`, and after `q`
  and `run musedit immig` the reopened band drew `DEF 20` and the `CALL 20` lines again, with the
  rest of the tune — rows, notes, `DEF 0` — unchanged. The renderer can only print 20 from the wide
  form, and it decides DEF from CALL by that low byte, so a reopen that shows both proves the
  encoder wrote the form the player reads. A phrase that CALLs its own body is legal but shows one
  command line per execution (the call stack's five frames): the pair placed at voice 1's first
  boundary is a self-call, and the band drew five `CALL 20` lines, which is the log being faithful
  rather than a fault. **The bytes agree.** `check-reu-fs.ps1 -Extract immig.mus` after the quit
  gives a 1024-byte file (the save recreates it at whole pages) whose voice 1 grew from 358 to 362
  bytes by exactly `01 C3 01 CB` at image offset 26, and every other byte is the source's — so the
  pair is `01 C3` = `DEF 20` and `01 CB` = `CALL 20`, the high nibble `(20 - 16 + 8) << 4` with the
  `$03`/`$0B` base. `gen-mus.py` could not decode that form (it printed `?`); it does now, which
  also revealed that `immig`'s voice 2 uses it — `DEF 18` and two `CALL 18` — three pairs the host
  model had been calling unmodelled.
- [x] **7.9 The table gate.** *Built.* `tools/check-mus-commands.py` — every mnemonic's byte pattern
  checked against `gen-mus.py`, so the encoder tables and the generator cannot drift. The tables are
  `mqCmdB`/`mqOptB`/`mqMax`, one row per structural id. It runs from `tools/build-tasks.ps1`.
- [x] **7.10 The bar's inert cells.** *Verified on hardware* (`commodo`: `DUR` steps the armed
  length, `TIE-` stays lit and writes bit 6 into the next note, `UNDO` undoes). `DUR` (code 5), `TIE-` (code 6)
  and `UNDO` (code 8) had nothing behind them and a click on any of them answered `? not yet`.
  `DUR` steps the armed length through `muDurTab`'s order (whole, half, quarter, 8th, 16th, 32nd,
  64th) keeping the dot mark, and starts at the whole note when the armed value is a utility length,
  which is not in that table. `TIE-` toggles a new armed flag (`muDurTie`, one zero-page byte) and
  shows lit while set; `muNcArm` ORs bit 6 into the duration it writes, so the tie reaches a note
  the armed length reaches — a newly written one — and `muRestKey` still clears the bit for a rest.
  `UNDO` is the `Z` key, refused while the music runs. The three handlers live past the click
  dispatch so no branch has to jump over them, and one `beq` that had been sitting exactly at 128
  bytes became the inverted form.

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
- [x] **8.5a One-line scroll.** *Implemented and verified in VICE on `immig`: the window moves one line at a time with the playhead pinned on the centre line (`MU_FOLLOW1`, which starts at `MU_FOLLOW_CENTER + 1` and is raised to the tune's own opening depth so a start never scrolls), `kScrollScreen` moves the band's pixels and matrix up through `REU_BOUNCE`, and `muGdLine` draws only the line that appeared -- the event starting on it, or the place the still-sounding event covers (`muLastEnd`/`muLastRest`). The playhead is a lookup in `muHist*`, the per-voice `(pointer -> row)` ring the renderer fills as it walks (`MU_HIST = 32`), so there is no second walk of the tune per frame. Captures after 12 s, 30 s and 45 s of playback show a coherent band, with the head bar and row following the cursor.*
- [x] **8.5b Tick clock between events.** *Verified on `immig`, whose voices open with whole-note rests: the player publishes a position once per **event**, and a whole note is 128 grid rows, so a pointer-only playhead sits still for ~15 s and then jumps. The cursor is now stepped from the kernel's own scheduler-tick counter (`tickCount`, `$54A8`, incremented by the CIA #1 Timer A handler — the same 59.2 Hz clock the player's jiffy uses, not the 50.1 Hz `frameCount`): `muPhAnchor` stores the tick and the stream pointer on every published event, `muPhStep` accumulates elapsed ticks and advances one row per `muUnitVal`, so the cursor descends line by line through the rests and re-anchors on the next event. Captures at 5 s and 12 s of playback show the window following through the opening rests with all three lanes drawn.*
- [x] **8.6 Stop.** *Verified: a second `SPACE` silences at once, and a new play works immediately. The cursor stays where the music stopped -- it is the playhead, so there is nothing to erase.*
- [x] **8.7 Stop/play in the same instant.** *Verified: twelve `SPACE` presses 0.15 s apart leave the tune stopped with `$D404/$D40B/$D412` all `0` and the published pointers frozen, and the next `SPACE` plays again (`$D404 = $40`, pointers advancing) — the child never wedges and every later toggle behaves. Note `kSidHush` silences by zeroing the three voice-control registers; `$D418` keeps the library's `$08` filter setting, so the voice-control registers are what a stop is checked against.*
- [x] **8.8 No player.** *Verified: with the player child gone (killed from the shell while playing), `SPACE` reports `? no player` on musedit's OWN head row, the playhead is cleared, and the editor keeps running -- further `SPACE` presses report again, a note key still edits, and `Q` still quits cleanly. This is the same state a failed `sid.lib` load leaves, because a failed lazy load prints `could not load sid.lib` and then EXITS the calling task (`libFailExit`), so `kSidPlay` never returns and the parent must detect the dead child itself (`muPlayTick`); for the same reason every parent-side `kSidHush` is gated by `muSidHushSafe` -- an unguarded hush with no `sid.lib` loaded would exit musedit. The player is per-session: once the child is gone, PLAY reports rather than respawning.*
- [x] **8.9 No orphan.** *Verified: quit while playing (`SPACE` then `q` on `filltst`) reaps the child in `muMusicBye` before `kFreeScreen` and returns cleanly to the shell prompt.*
- [ ] **8.10 ZP.** *Built — needs a check.* The player is a thread child with its own declared ZP window (`$02`, len 9) and its own copy of the tune pointer, so an F-key switch that changes the display and the running task cannot disturb it. The check is one switch away and back while playing.
- [ ] **8.11 Audition.** *Built — needs a check.* A pitch sounds as the pointer arrives on it, and a note key sounds as it is pressed. `muHovDot` (idle pass, after `muAudTick`) compares the key under the pointer with the last one it placed and, on a change of key **or** of the cursor's voice, sounds it through `muAudNote` — before anything is written, so the pitch is heard even when the entry is refused (`? illegal duration`). A piano click does not sound a second time: the hover has already played that key. Voice 1 is written directly — `$D400/$D401` from the player's own PAL table via `muAudFreq` (octave 6 values, doubled for octave 7 with B clamped at `$ffff`, exactly the player's arithmetic), `$D405/$D406` = 2 ms attack, max sustain, `$D418` with its filter bits kept and volume forced, `$D404` = sawtooth + gate. `muAudTick` releases the gate after `MU_AUD_FRAMES` = 5 **displayed** frames (100 ms), measured as elapsed frames because a pass is slower than a frame and an equality test would step over its own deadline. Drop-on-play: if `muPlayOn` is set the audition is forgotten without touching a gate, because clearing it would cut the player's own note. The exit path gates off too, so a note cannot outlive the editor. **The pitch to write travels in `muNoteLin`**, not in `MU_HOV`: `MU_HOV` is the pointer's published field and a note key must not be able to move it, or the readout would show a pitch the pointer is not on. **The voice dot:** while the pointer is on the keyboard, `muHovDot` also puts the cursor voice's own playback dot (`MU_DOT_SLOT0 + voice`, its own colour) on the key, so the voice a click would write to is visible; on a voice change the old slot is dropped from the enable mask and the new one placed, and off the keyboard `muDotsOff` hides them. The check is a hover on each voice, a voice change under the pointer, and a note key with the pointer off the keyboard.
- [ ] **8.12 Audition stands down.** *Partly built — needs a check.* The editor cannot read `sid.lib`'s `sidOwner` (it lives in the library's own pages), so the test is the chip itself: with no audition of ours sounding, any set gate bit in `$D404`/`$D40B`/`$D412` means another task is playing and the audition stays silent. A player caught mid-tick can still be interrupted once; it rewrites its voice controls every tick. `muHovDot` returns immediately while `muPlayOn` is set, so our own playback owns both the chip and the dots. The check is a game's music on another screen while the keyboard is hovered, then our own play afterwards.

### Step 9 — Gates and release

- [x] **9.1 Budget.** `check-pool-budget.ps1` exit 0, `musedit` at its measured
  footprint, with filesys, sid, mouse and sprite counted and the tune pages counted.
  The running figure is in section 11; the polish pass and the merge of the streamed
  player have each cost a page since.
- [x] **9.2 Builds.** *Verified: kernel 233 asserts 0 failed, tasks and REU exit 0, and `check-reu-current.ps1` reports the image current — no source changed since the build and all 49 binaries byte-identical.* Kernel, tasks and REU all exit 0; `check-reu-current.ps1` reports the image holds the binaries just built.
- [x] **9.3 Doc gates.** *Verified: `verify-map.ps1` all checks passed, `verify-jumptable-docs.ps1` clean, `verify-inventory-docs.ps1` current (198 addresses agree with the jump table; the call inventories were regenerated — 1270 kernel and 237 library call sites), `check-links.py` 0 unresolved, `check-ascii.py` OK, `scan-history.py` 0 flagged lines.* `verify-map.ps1`, `verify-jumptable-docs.ps1`, `verify-inventory-docs.ps1`, `check-links.py`, `check-ascii.py`, `scan-history.py` — all exit 0 (the last is advisory but read it).
- [x] **9.4 Wiring.** *Done: `tools/tasks.json` and `project-config.json` `sources` already named it; added to `README.md` (feature bullet + guide link), `docs/features.md` (Sound), `docs/quick-start.md` (the `run <filename>` task list), `tools/quickstart.binaries.md` (the binaries task table) and `publish-binaries.ps1`'s `$guides` — without that last one the guide would never ship. The task binary needs no publish wiring: it travels inside `reu/REU.bin`.* `tools/tasks.json`, `project-config.json` `sources`, `README.md`'s task table, `docs/features.md`, `docs/quick-start.md`, `tools/quickstart.binaries.md` and `publish-binaries.ps1`'s `$guides` all name `musedit` / `docs/musedit.md`.
- [ ] **9.5 A fresh machine.** *Needs the sitting.* Freshly seeded REU, one sitting: open, enter notes, play, save, quit, reopen, play — no leftover state.

### Step 10 ? Parked (deliberately not done)

Each item below is understood, costed and **not** in the tree. They are here so the reasoning is
not rediscovered from scratch.

- [x] **10.1a Rows that draw nothing are no longer walked.** *Done: with a note's duration written on its onset row, the rows an event covers are blank, so `muGdEvent` draws its onset and nothing else ? a whole note used to cost 128 iterations of the continuation loop, and the scroll path's continuation scan (which painted a blank row) is gone with it. `muLastEnd`/`muLastRest`/`muLastStart`, `muGdPastRow`, `muGdLeft` and the `muGdFirst`/`muGdRest` flags went with the code that read them; the task dropped a pool page (91 -> 90). What is left on an arriving line is the per-event walk (`muStep`) plus six or so `kPrintCharAt` calls per event that actually starts on it.*
- [ ] **10.1b The scroll itself is the remaining cost.** *Not built — parked.* A scroll is `kScrollScreen`
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
- [x] **10.2 A predicted row rate is not viable.** *Done: the row is the player's own published
  jiffy count divided by the row's length (§13.2), so there is no rate, no anchor and no tuned
  constant left. Every variant that predicted rows from a clock disagreed with the music by an
  amount whose *sign*, not its size, decided whether it read as fast or slow — 4, 4.5 and 5 ticks
  per row all failed that way. Never reintroduce a predicted rate between two observed events;
  publish the position instead.*
- [x] **10.3 Repeat is the tune's business, not the editor's.** *Resolved — no editor-side repeat.*
  The format's own repeat is the section: a `HED` opens one and a `TAL` closes it back to that
  `HED`, and `kSidPlay` honours that **inside a pass**. So a tune written to repeat repeats by
  itself, and the editor must not add a repeat of its own on top — a tune with no section must end
  at its last row and the score must rewind then, which is what it does.
  The walk needs nothing special for the looping case either: the walker executes the same
  `HED`/`TAL`, so the lines it draws loop with the music and each loop carries its own later clock,
  which is why `muPhSync` — a lookup of the player's published count against the tick each drawn
  line was walked at — keeps following a section round without a restart to detect.
  The child therefore stays spawned for one pass (`MU_MUS_REPEATS = 1`): a pass end still means the
  tune is over, the parent rewinds the score and the PLAY highlight drops. Had the editor looped
  instead, `repeats = 0` would have stranded the child inside `kSidPlay` for ever, where `SPACE`
  could only hush the chip — the player would keep advancing silently and the next `SPACE` could
  not restart from row 0.
- [x] **10.4 The window position is a ROW plus a line offset.** *Built; the repaint half verified on
  hardware.* `MU_TOP` names the row the window starts at, but a row is drawn as **one line per
  command plus its own line** — `immig`'s row 0 is ten command lines — so a row alone cannot say
  where the band's top line is. `MU_TOFF` is the second half: how many lines into that row's group
  the band starts. `muWalkToTop` walks to the row and then emits and discards exactly that many
  lines, so a draw lands on the line the band is showing; the offset is derived from the line that
  leaves the top of the band on every scroll (`muScrollBand` reads `MU_LNKIND` before the shift — a
  command line's successor is the same group one line further in, the row's own line is the group's
  last, so whatever arrives above it starts the next row). A page jump, a search jump and a rewind
  all set it to 0, because each of them names a row. `muWalkToTop` also repairs the pair when a
  row's group has fewer lines than the offset says — an edit can shorten it — by moving the window
  to the next row's first line. The three consequences are gone: a full repaint (an edit, undo,
  `SHIFT+U`) lands where the band already was; a partial repaint rewrites the band's table from the
  band's own top line, so the cursor's row and a click's row agree with the screen; and the page
  up's "already there" test is `MU_TOP == 0 && MU_TOFF == 0` rather than a flag, which is what the
  old `MU_WINTOP` byte stood in for. The offset costs no new zero page: it took `MU_WINTOP`'s byte.
  Verified: with `immig` scrolled one line so the band's top line is `DCY 9` (row 0's *second*
  command line), `SHIFT+U` repaints the whole grid and the top line stays `DCY 9` — before the
  change it snapped back to `ATK 1`; and from the same scrolled state the page up walks home to
  `ATK 1` with `R0`, which is the offset being cleared. The partial-repaint half rides the same
  `muWalkToTop` and the same offset, so it is exercised by ordinary use rather than by a dedicated
  run.

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
- [x] **11.10 The cursor's window edges move the band, not a repaint.** Down off the bottom row
  calls `muScrollBand`: `kScrollScreen` shifts the region's pixels and matrix up one line through
  the REU and `muGdLine` draws only the line that arrives, so the walker is stepped, not restarted.
  Up off the top row cannot shift anything in — the interpreter only walks forward, so the line
  above the window has never been computed — so it moves the window back a whole page (`muTop -
  MU_GRID_ROWS`, clamped at row 0) and repaints once, with the cursor landing on the page's last
  line. `muScrollBand` leaves the cursor where it is and the caller places it, because the follow
  centres it while a cursor key keeps it on its row.

### Step 12 — Streaming the tune (unbounded tune size)

**Status: built. Only 12.9, the test pass over every bundled tune, is open.** The tune is a
scratch file (`<base>.wrk`) that the editor pages through three 256-byte read windows and one
write path; `kSidPlay` is asked for **mode 1** and pages the same file through the same windows,
which is what makes an edit audible on the next pass with no copy. Tune size is therefore not a
pool question: `musedit` holds **6** pages for the window service whatever the tune's length, and
the ceiling is the filesystem's 16-bit size field, 64 KB.

**Measured, which is why the design is the shape it is:**

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

**The design, as it stands.**

- **A scratch file in the FS is the working image.** `muScrOpen` creates `<base>.wrk` at its full
  `MU_SCR_PAGES` (255 pages, 65,280 bytes) because `kReuWriteAt` refuses a write past EOF, so the
  image can grow through an edit without the file being recreated; `muScrDrop` deletes it on exit,
  so a session leaves nothing behind. The real `<base>.mus` is untouched until `SAVE`, which is
  what makes "quit without saving" safe.
- **Windows, not a block:** one **page-aligned** 256-byte read window per voice (3 pages) plus one
  write page, and they are page-aligned for the reason the player's are: the address of an offset
  is then simply `winHi : offsetLo`, with no per-access arithmetic and no `winOfs` to keep in
  step. It is safe for commands because every command is a 2-byte pair at an **even** offset, so a
  command and its option byte can never straddle the end of a window — crossing a page refills the
  window before the next command is read, exactly as `sidWinSeat` does. The editor and the player
  therefore seat the *same* windows over the *same* file, which is what makes the playback handover
  a handover rather than a second implementation.
- **The accessors funnel** is `muPtrToK`: every image access is "seat the window for this voice,
  then read `win + (ofs - winOfs)`". The walker keeps `muPtrLo/Hi,x` as **file offsets** rather
  than RAM addresses, which also means the published block `$54A2` and the editor's playhead ring
  agree by construction.
- **Writes reach the file through the window they came from** (`muWinFlush`, one `kReuWriteAt` of
  256 bytes per page), and page-spanning operations loop over windows. Undo is unaffected: its
  range-diff records keep the old bytes in RAM.

**The work.** Each a checkpoint that builds and passes the gates on its own:

- [x] **12.1 Window service.** *Verified: `muWinSeat` (X = voice, `muWinOfs` = the offset -> `kPtr` = the address of that offset in the window), `muWinFill` (one `kReuReadAt` of the page from `<base>.wrk`), `muWinInit`, `muWinReset`, `muWinFlush` (the write path) and `muWinCheck`, which runs at startup and compares each page against an independent pointer walk (`muWcPt`) because a lost carry between the file offset's high byte and the page number hands back another page's bytes and the tune still looks plausible. `muWinOk` is a flag rather than a page tag, since every value 0-255 is a real page and a tag cannot say "never filled". **Not `kReuDmaRange`:** the editor holds `filesys.lib`, so it uses `kReuReadAt`/`kReuWriteAt` with a caller-owned record and never touches the kernel-global `reuFsPtr` (the player cannot, which is why it takes the other route).*
- [x] **12.2 Load.** *Verified: `muScrOpen` + `muLoad` copy the file into `<base>.wrk` page by page; the tune is not in a `kMalloc` block at all (`kMalloc` buys the three window pages). A file longer than `MU_SCR_PAGES` is refused with `? no scratch`; a missing filesystem reports `? no fs`.*
- [x] **12.3 The walker.** *Verified: `muPtrLo/Hi` hold a file offset per voice; `muPtrToK` seats that voice's window on the offset and returns the address; `muSimInit` seeds the three streams at 8 then each length word; `muPtrBump` is offset-shaped. The base subtraction is gone from `muHistAdd` and `muSkRecord`, and `muStep` no longer rebuilds an address in `muAcc`. `muPtrToK` is documented as clobbering A, Y, `kPtr`, `srcPtr` and returning C=0.*
- [x] **12.4 Drawing.** *Inspection only: `muGdCells`/`muGdPitch`/`muNumDur`/`muCmdN` and the header readers (`muFindTempo`, `muUnitNow`, `muNameAt`, the description) all read through `muPtrToK`, so nothing addresses the image directly.*
- [x] **12.5 Editing.** *Inspection only: every write site reaches its page through `muWinSeat` + the window, and `muWinFlush` writes the page back; the range operations that cross a page boundary loop over windows.*
- [x] **12.6 Save and grow.** *Inspection only: `muSaveNow` writes in place when the image still fits the opened record, and otherwise takes the create path — pages rounded up, `muFreeSpace` asked **before** the delete, then `kReuDelete` + `kReuCreate` + the write. `MU_FLEN` records whole pages.*
- [x] **12.7 Playback.** *Verified: `muSidCfg` builds a 9-byte `kSidPlay` mode-1 config (`muSidCfgB`) — the scratch file's 3-byte FS offset, then the three window pointers — so the child pages the same scratch file the editor edits and an edit is heard on the next pass with no copy.*
- [x] **12.8 Budget and gates.** *Verified: `check-pool-budget.ps1` carries `'musedit' = 6` with the tune paged out of `<base>.wrk`, and the running total is 99 of 109 pages (code 65 + sprite 1 + mouse 2 + filesys 10 + sid 15 + malloc 6).*
- [ ] **12.9 Test pass.** *Needs the sitting.* Every bundled tune opens, plays and saves; a tune at the 64 KB ceiling opens and scrolls; undo/redo, insert, delete and transpose behave; `quit` without saving leaves the file byte-identical.

**Risks that remain.** A page that an edit straddles must be written back exactly once, and the
`kReuDmaRange` call clobbers `kArg..kArg+5`, so no kernel state may be live across it. Both are
covered by `muWinCheck` and by the page-scoped accessors; the open risk is 12.9's coverage, not
the mechanism.

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
7. **The map's free total.** `docs/memory-map.md` and `tools/verify-map.ps1` now publish the same
   figure (116 B across 21 pockets) and the script is the authority; re-derive it from the build
   map whenever a pocket changes rather than adjusting it by hand.
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
11. **`tools/gen-mus.py` calls bit 7 "double-dotted", and the player disagrees.** The player's
    own arithmetic settles it (`calculateNoteDuration`): bit 5 alone adds half the base (3/2), bit
    5 with bit 7 adds half and then a quarter (7/4), and **bit 7 alone** takes the separate
    `lookupTempoFromTbl` path — the triplet. The generator's `DDOTTED = 0x80` therefore labels a
    triplet as a double dot, its `note_event` cannot emit a triplet or a true double dot
    (`0x20 | 0x80`), and its `--decode` mislabels both. No bundled tune can tell the difference:
    none of the twelve carries a note length with bit 7 set. Fixing it is what would make a
    triplet test tune possible, which 5.5 and 6.22 both need.

## 16. Step 13 -- finish the walk, then make the editor phrase-native

The streaming storage (steps 12.2-12.7) is in and `musedit` holds 99 of 109 pool pages. What
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

- [x] **13.1 A dump of what the walk read.** *Verified: `muHistAdd` records each visited event's image offset, row and option byte; `SHIFT+L` prints it for the cursor's voice, newest first, as `RRRR OOOO OO` (`muHistShow`). Read at bar 6 on `immig`, the three outcomes it separates are one keypress apart: rows stepping by the length the file gives (the walk is right, the fault is in locating the player), a row that jumps much further than the file's length (a length read wrong, with the offset on that line naming the byte), or nothing at all (`muErr` names why). The walk's own count agrees with the host model — `v` reports `t=7632 e=231,215,663`, identical to `tools/mus-sim.py`.*

`muHistAdd` already records, for every event the grid walk visits, its image offset, the row
it was drawn on and its option byte -- that ring is *exactly* what the lanes were drawn from.
`SHIFT+L` prints it for the cursor's voice, newest first, as `RRRR OOOO OO`
(`muHistShow`). Read at bar 6 on `immig`, three outcomes are distinguishable with one keypress:

- **rows step by the length the file gives** -- the walk read the bytes correctly, and the
  fault is in the playhead's anchor, so 13.2 is the fix;
- **a row jumps much further than the file's length** -- a length was read or decoded wrong,
  and the offset on that line is the byte to look at;
- **nothing at all** -- the walk stopped for that voice, and `muErr` names why.

### 13.2 The playhead: publish the position, do not infer it

**Root cause, after every alternative was measured on the machine.** The editor's playhead is
derived from *proxies* for the player's position, and none of them is the position itself:

- a **clock** (`muPhStep`) predicting jiffies per row — the editor cannot know the player's
  `sysSleep`-to-tick relationship, so the rate is unobservable and any value is wrong at some
  tempo (measured: 4, 4.5 and 5 ticks a row all read as "fast" or "slow", and 10% flipped the
  symptom, because the sign of the error, not its size, decides whether the anchor helps);
- an **offset -> row cache** (the `muHist` ring), which is a side effect of *drawing*: it covers
  only the drawn window, holds 32 entries per voice, and is rebuilt per redraw. So exactness is
  intermittent by construction — a sparse passage, or the tune looping, leaves the player
  outside the cache and the playhead holds or drifts. Redrawing to refill it is both too slow
  during playback and visibly wrong (it repaints the tune's start).

The walk itself is proven correct (`v` reports `t=7632 e=231,215,663`, identical to
`tools/mus-sim.py`), so the fault is only in *locating* the player.

**The fix: the player publishes its own position in the units the grid is drawn in.** The grid's
row is a fixed number of jiffies (`unit`), and the player already counts jiffies per voice
(`WorkNoteJiffiesLeft`). Give `SID_POS_BLOCK` one more field — a 16-bit **song tick** counter
that the player increments once per jiffy and clears when it starts a pass — and the editor's
row becomes

    row = (songTicks - 1) / unit

which is exact by construction: no cache, no clock, no anchor, nothing to drift, no dependence on
what has been drawn, and correct across a loop because the counter restarts with the music. The
one jiffy comes off because row 0 is fetched with the count already at 1 (`WorkNoteJiffiesLeft`
starts at 1 on every voice and `sidTick` increments before it processes); without it the marker
stands on the next row for the last jiffy of every row. The
division is the same one the walk already performs (`muGdSpan`). Cost: two bytes in the published
block and an `inc/…` in the player's tick path; a `kDiv16` per frame in the editor.

This is the only design of the seven tried whose input *is* the quantity being displayed.

- [x] **13.2 The row is computed from the published position.** *Verified: `sidTick` publishes a 16-bit song-tick counter at `SID_POS_TICK` (`$54A8`), and `muPhSync` computes `row = (tick - 1) / muUnitVal` once per displayed frame from `muPtPlaying`, guarded by `muPhFrame` against `frameCount`. Measured in VICE: the row advances smoothly at ~19.5 rows/s on `seffsng`, and the follow holds the playhead's line on target there.*
- [ ] **13.2b The `immig` acceptance.** *Needs the sitting.* `immig` in step from the first bar, through bar 18, round its loop, with the regression set unchanged. The loop is where the counter restarts with the music, so it is the case worth watching by eye.

### 13.3 A gate that keeps them equal

- [x] **13.3 The walk gate.** *Built.* `tools/mus-sim.py --walk N` prints every line the walk draws
  with the song tick `MU_LNTICK` records for it — a row's own clock (the last event it ran), the
  previous row's value for a row that ran none, and `-` on a command line, which `muPhSync` never
  consults. `tools/check-musedit-walk.py` renders that list in the editor's own columns and compares
  it, lane by lane, against a grid captured from a running musedit and kept in `tools/fixtures/`
  — so the comparison is automated and runs in `build-tasks.ps1`, rather than being made by eye.
  It needed no hardware of its own: the fixture is the editor's grid as text (lane 1 at column 4,
  lanes twelve apart, a row's number zero-filled in columns 1-2 on the row's own line), which is
  exactly what a screen dump decodes to. `immig`'s 19-line band matches over all 19 lines — the nine
  command lines above row 1 (including the lone `DEF 0`), the empty rows `02`-`08`, and the `F#2 8`
  / `F#2 16` cells in lane 3. Padding inside a lane is ignored, so the two renderers may differ on
  where a command's operand starts; which lane holds which text is not.
- [ ] **13.3b One contiguous published block.** *Not built.* The player publishes two things a reader needs — each voice's next-command offset (`$54A2-$54A7`) and its song jiffies (`$54A8-$54A9`) — but the kernel's `tickCount` sits at the free space after them, so a reader doing arithmetic on the base reads the wrong thing (the off-by-one that makes a walk run ten times fast). The tidy layout moves `tickCount` to `$54AA` so `$54A2-$54A9` is the player's alone, gives the fields named offsets in `sid.asm` mirrored in `kernel-shared.inc`, and asserts `tickCount == $54AA`. It is its own change, on its own build, with nothing else in flight: done inline it broke a working playhead.

### 13.4 Phrase-native editing

The format's structure commands are what make it a language rather than a list of notes. In
order:

- [x] **13.4a Show it.** *Done.* A command event draws its mnemonic on a **line of its own**, above the row
  it belongs to, with no row number (it is not a point in time). Every command the grid can draw
  has such a line — all 49 ids, the nine structural ones, the 37 that colour the sound and the
  player's own `MS#`, `FLG` and `AUX` (`MS#` takes its value's high byte from the command byte's
  top two bits; the one command with no line is `JIF`, whose value is the option times four plus
  those two bits, and no bundled tune uses it). A line carries **one command
  from each lane**, so voices that fire on the same row share it and a voice with more commands
  continues on the lines below, and the two classes never share a line: each voice keeps a queue
  per class (`muLogBase` is indexed by `voice * 2 + class`), the sound commands are drawn first and
  the structural nine last, nearest the row's own line. The operand is a count, so it prints in
  decimal through `muDec16` (which covers the widest one, a portamento's 14 bits); the three
  commands that take no operand — `TAL`, `HLT`, `END` — print none. The band is therefore a list of
  lines
  rather than of time rows: the walk decodes each pair once (`muLnDec`, which mirrors the player's
  own routing) instead of each handler logging what it consumed, the producer
  (`muLnNext`/`muGrpOpen`) emits a row's command lines and then the row's own line, and
  `muLnTbLo/Hi` records which row each visible line holds
  (with `muLnKind` marking the row's own line) so the cursor, the header's `BAR`/`R`, the seek and
  the click path still read a time row. Playback skips a command line: the follow holds the
  playhead's line at `MU_FOLLOW1`, in LINES, so one tick of music may move the band several
  lines. `MU_FOLLOW1` starts at `MU_FOLLOW_CENTER + 1` (the pin is the centre, 8) and the
  producer raises it to the tune's own opening depth when it emits the first row's line: a
  row's own line sits as many lines down as that row has commands above it, so a tune whose
  opening is deeper than the centre would otherwise have the view scrolled off its first
  lines before a note played. `tools/mus-sim.py --lines N` is the host reference for the
  layout, and its `decode_cmd` mirrors `muLnDec`.
- [x] **13.4b Move through it.** *Done.* `SHIFT+D` on a `CALL` line moves the cursor to that phrase's
  `DEF`; on a `DEF` line it moves to the next `CALL` of the same phrase below the cursor, and
  wraps round to the first one when there is none below, so repeated presses **tour** the call
  sites rather than listing them. Two things the design had to settle: `D` alone is a piano key,
  so the binding is `SHIFT+D`; and a line carries one command per lane, so the key acts on the
  lane the cursor is in -- which is why the cursor marker now sits **in the gap left of that
  lane** (columns 3/15/27) instead of at column 0, so the lane it will act on is visible.
  Implementation: `muCmdHere` re-derives the command under the cursor by walking the cursor's row
  with the grid's own producer and replaying the draw's index arithmetic
  (`block = voice*2 + class`, depth, line index); `muFindCmd` walks for the partner command and
  returns the row **and the line index inside that row's group**, so the cursor lands on the line
  that carries the command; the jump puts that row at the window's top and the cursor follows the
  command into its own lane. `muWalkStart`/`muWalkToTop` are the producer's start and skip,
  shared with `muDrawGrid`. Three rules the work produced: the producer has **no**
  end-of-tune return, so a search must stop on the walk's own halt flags (`muOn`); a search
  clobbers `muTmpH` (`muLnLog` uses it for the mnemonic id), so a line index across a walk needs
  its own byte; and a search leaves the producer parked at the row it stopped on, so a refusal
  puts it back **silently** (`muBandRebuild`) -- repainting instead makes a one-second message
  flash the whole grid.
- [x] **13.4c Author it, for the nine structural commands.** *Verified on hardware, both halves — the structural nine and, through 7.7, every sound id.* `C` — and `SHIFT+C`, and the `CMD` cell on the bar — opens a
  **one-line prompt on row 0** rather than a full-screen panel: CRSR up/down picks one of the 49
  commands, `RETURN` takes it and the digits then build its operand, `STOP` cancels and
  the grid comes back untouched. The prompt writes at most **two bytes**: the pair is inserted at
  the boundary the cursor's row starts (the same `muInsGap` a note uses), or replaced where it
  stands when that boundary already carries the same command and the mode is `OVR`; `SHIFT+INST/DEL`
  on a command line removes the pair through `muDelGap`. Either way the undo record is the pair
  alone, so a command edit undoes as one action and no row moves. The pieces are `muObjEnc`
  (mnemonic + operand -> pair, from three id-indexed tables), `muObjFind` (a boundary's command
  pairs are contiguous before the event's own pair, so a backwards two-byte scan finds the one an
  id names) and `muMnPrint` (the mnemonic field, shared with the draw).
  Two rules the work settled. A command pair consumes no time, so its line only ever sits above a
  **note or a rest**: a row in the middle of a duration is refused (`? no event here`) rather than
  split, because the format has nothing to split it with — which is also why the editor offers no
  way to place one there. And the authoring surface is the nine structural commands, whose operands
  are the tune's own settings; a sound command's operand is a timing or a waveform number that
  belongs to the pattern, so those lines are drawn and not typed. Remaining: the sound
  commands (step 7.7) and the bar's three inert cells (7.10), which want a panel and hex operands.
  Checked in VICE against the saved `.mus`: a replaced operand changes exactly its own
  byte, an inserted pair moves the voice's length word by two and shifts the tail by
  two, `SHIFT+INST/DEL` removes it, and insert -> redo -> delete leaves the file
  byte-identical to the original. The referee was `muDelGap`, which had been moving
  the stream two bytes down instead of closing it, so **every** delete in the editor
  was a silent corruption rather than a delete (see `docs/bugs.md` #188).
- [x] **13.4d Edit a definition knowingly.** *Verified on hardware: `immig`, voice 3 inside phrase
  0's body — the status line says `phrase 0 x4`, `i` and `SHIFT+INST/DEL` answer `? phrase 0 x4`
  with the image unchanged, a note key is allowed and says so, and a top-level insert in voice 1
  is not refused.* Inside a `DEF` body an
  edit changes every call site, so the status line names the phrase and its call count, and an
  insert or delete there is refused. The premise, restated: the band shows each row once (a body
  is walked inline), so a body is not a *region* of the grid — what it is, is a byte range whose
  rows the walk can name. The walk already knows which phrase it is executing, so it tracks it:
  `muPhDf,x` is the phrase each voice is in (stored as **phrase + 1**, because 0 means the top
  level stream and phrase 0 is a real phrase), `muPhStk` the enclosing one per call frame, and
  the three handlers that push and pop frames (`muCmDef`, `muCmCall`, `muCmEnd`) maintain both.
  `muGrpOpen` completes the row's group before any of its lines is emitted, so the draw
  records the state in `muLnPh` — one byte per **visible line** per voice, shifted with the band
  and cleared with it. That is what makes a **cursor move** cheap: a move does not walk the tune
  at all (it only moves the glyph), so a hook inside the walk could never fire on one — measured
  on hardware, that version announced nothing on a move. `muPhWatch`, from the idle pass, reads
  the three bytes for the cursor's line and announces a change, and its value is taken at the
  group's **end**, so a row that holds a `DEF` and the body's first event — the same row, since
  neither takes time — reads as the body, and an `END`'s own row reads as the stream it returns
  to: exactly the answer "is an insert here inside a body?" needs. `muPhCount` counts `CALL n`
  **sites** with one structural pass over the three contiguous streams (a tally of the walk
  would be inflated by every `TAL` pass), decoding the two forms `muSimCmd` routes on, and
  `muPhNum` prints ` n xc` after a message with `muNum16`.
  `muPhBlock` refuses an insert when any voice the insert can write to is inside a body (a halted
  voice takes no insert), `muPhHere` refuses `SHIFT+INST/DEL` from the seek that delete already
  makes, and `muPhArm` announces the phrase on a note entry, which is the edit that is *allowed*
  there. `muPhTick` puts the message up from the idle pass, because the walk is mid-draw when the
  state is known and both the message and row 0 want the REU windows. Two new messages, `phrase`
  and `? phrase`, join `msgs`, so the text costs no code page. **Verified on hardware** (`immig`:
  9 bodies, 14 `CALL` sites, phrase 0 called 4 times — the host model and the 6502 agree):
  switching into voice 3 inside phrase 0's body says `phrase 0 x4`, switching out and back says
  it again, a note key inside says it, `i` inside answers `? phrase 0 x4` with the grid byte-for-
  byte unchanged, `SHIFT+INST/DEL` answers the same, and an insert at the top level in voice 1 is
  not refused. Cost: 69 code pages (pool 103 of 109, margin 6) and 87 bytes of data.
- [x] **13.4e Keep the accounting.** *Verified: the figure is 65 code pages, 99 of 109 with the libraries and the six window pages, and `check-pool-budget.ps1` is the gate.* The
  figure is unchanged by 13.4f only because of 13.4g below: without it 13.4f cost two pages
  and the editor sat at 106 of 109. Budget: musedit is 65 code pages of the pool's 109, a margin of 10
  (b cost 597 bytes and a page, c 913 bytes and four), and the printed text is
  already out of the code -- every string it prints is a record in the REU file `msgs`, so
  adding a message costs no page (see `tools/gen-musedit-msgs.py`). The editor's own
  readout pages went with the timing line once the walk was proven, which is where the
  last three of those pages came from, and the undo stack (2 KB, the largest single item the
  task held) is 8 pages of the REU file `musedit.und` behind a 256-byte window rather than 8
  pages of code. The tune is a scratch file behind three windows (step 12), so its length costs
  no pages either. b and d are the ones that
  need the pages (a and the command-line pass used 6 between them: the
  46-field mnemonic table, the
  two decode tables, the ladder that formats each class of value, the per-class queues — and
  then gave 412 bytes back by folding six repeated idioms into helpers). `check-pool-budget.ps1`
  is the gate, and `basic`-style exact fits are refused rather than warned about.

**13.4f The grid follows the tempo timeline.** *Built; verified on hardware* (`twotem`: the rows follow across the change, the playhead stands on the note's own row in both sections, and `immig` is unchanged; bugs #193 and #194 were both found by that run). The way a grid row is
counted has to survive a mid-tune `TEM`, because the format allows one anywhere.

**What the format says.** It has **one** tempo: `TEM` writes the single byte `WorkTempo` (and
`sidplay.inc`, and there is no per-voice tempo to have. What a `TEM` therefore means is "from this
point, for everyone": the duration routine reads the tempo when a note is **struck**, so every note
struck after the change re-times in all three lanes, while a note already sounding keeps the
countdown it was given and finishes at the old tempo. Order decides only what happens inside the
tick a change lands on — the tick loop runs lane 1, 2, 3 (`ldx #$00 … cpx #$03`), each lane
running all its commands for that tick before the next lane looks, and the editor's walk mirrors
it (`muSimNext` breaks ties to the lower voice). So three `TEM`s on one line — one per lane — all
execute in that tick: lane 1's note is struck at lane 1's value, lane 2's at lane 2's, lane 3's at
lane 3's, and the tempo that **survives** for everything after is the last one executed, lane 3's.
The other two values are overwritten before the next tick. That much is the player working as
designed, and the editor's `muSekTem` (the tempo recorded for the event the cursor is on) already
judges a new duration at the tempo in force at that row, so editing after a change is correct.

The problem is the grid's arithmetic. There is **one row unit**, derived from the tempo at the
**first note** (`muTempo` → `muUnit` → `muUnitApply`), and everything that turns rows into time
uses it: the walk's tick → row division, `muUnitBar` (a bar is a fixed count of rows), the `BAR`
and `R` figures in the head, `SHIFT+U`'s refinement, `SHIFT+I`'s "one bar", and the playback
follow's division (`muPhSync`). Past a mid-tune `TEM` the music re-times and the grid does not, so
the `BAR` figures stop agreeing with the tune's own metre, `SHIFT+I` inserts the *old* tempo's bar,
and nothing on screen says a change happened at all.

**The decision.** A row is the finest length the tempo in force can express, and a bar is one whole
note measured in those rows. The second half is what makes `BAR`, `R`, `SHIFT+I` and the row numbers
keep meaning what they say; the first is forced, because a row coarser than the segment's own finest
length has two events inside it and the grid cannot draw the second. The consequence is that a bar's
**row count changes at a change**, and the tempo byte tells you both numbers: `rows × unit == tempo`,
so `$90` is 16 rows of 9 jiffies while `$C0` is 64 rows of 3. A `TEM` that does not land on a bar
line therefore cuts one bar short.

**The shape.** Rows become a piecewise-linear function of the song tick, one segment per tempo:

    row(tick) = segRow0 + (tick - segTick0) / segUnit

The walk already visits events in exactly the player's order, so it opens a segment wherever it
executes a `TEM` whose value differs: `segRow0` is the group row it is drawing (`muGrpRow`, which
*is* the row of that tick), `segTick0` is the event's tick, `segUnit` the new tempo's unit. Nothing
else about the row axis changes, and the per-event span stays the repeated-subtraction divide it is
today — only its divisor becomes the segment's. The one thing that must be re-seeded is each voice's
position, because a voice's next event may be *after* the change while its row was accumulated under
the old unit: at a change, every live voice gets `muVRow = segRow0 + (tick - segTick0) / segUnit` and
`muVRem` the remainder, a single small divide per voice. That makes the drawing exact rather than
one row out for the rest of the segment.

**What else has to follow.** The bar a row falls in can no longer be a division, so the walk records
it per line (`muLnBar`, 2 bytes × 19) along with the tempo in force on the line (`muLnTem`), and the
head, `muBarOf`, the row numbers and `SHIFT+I` read the line instead of dividing. `muRowsToJif` and
`SHIFT+I` need the unit and the bar size *at the cursor*, derived from `muLnTem` by a pure routine
that leaves the grid's own state alone. The playhead records the **tick** of each line
(`muLnTick`, 2 bytes × 19) and takes the row of the deepest line the music has reached: it reads
what was drawn rather than a segment the walk is standing in, which is the only way it can be right
when the walk has drawn several rows beyond the cursor (bug #193).

Reference note: the tempo table is `DataTempoTable` in `sidplay.inc`, and `muTempoTable` in the
editor.

**13.4g The state moves to zero page.** *Built; verified on hardware* (playback starts on both tunes, which it did not before this). 13.4f cost 625 bytes and
two pages, which put the editor at 106 of the pool's 109 and broke playback: `sid.lib` is lazy, its
fifteen pages did not fit, and a failed lazy load **exits** the child, so the parent reported
`? no player`. The pages were there in total (18 free) but not in one run (14 + 4), which is the
lesson: a lazy library needs a **contiguous** fit, and `check-pool-budget.ps1` only adds up totals.

The fix was not to trim the feature but to stop keeping 1.3 KB of state in the code image. The
header now declares `zp_base`/`zp_len` = `$0b`/224, and 193 declarations -- every scalar and array
the walk, the draw and the key loop touch hardest -- became `.label`s inside that window. Three
classes stay in the image by necessity: **address-taken** labels (a pointer to a ZP byte is not
relocatable), **state the music child shares** (`muPlayOn`/`muPlayEnd`/`muPlayQuit`/`muPlayRefd`/
`muSidOk` -- zero page is per *slot*, so a thread child would read its own copy), and the read-only
tables. `$02-$0a` stays clear for `sid.lib`'s scratch, which the parent's hush uses and the child
declares in its own spawn arguments, and an assert holds the window below the kernel's `$f0`.
The block must sit at the **top** of `musedit.inc`: KickAssembler chooses `zp` over `abs` from the
symbol's value at parse time, so a label defined at the bottom would silently assemble absolute.

Result: 18,317 -> **16,614 bytes**, 72 -> **65 code pages**, pool 106 -> **99 of 109** (margin 10),
and fixups 3,120 -> 1,641 because most references are no longer relocatable addresses.

**The instance buffer took the two biggest arrays that fit.** `MU_DATA_SIZE` is one byte, so the
buffer caps at 255; it now holds `muLogId` and `muLogLo` (`MU_DATA_SIZE` 53 -> 245) at
`MU_LOGID`/`MU_LOGLO`, addressed through two zero-page pointers that `muHeapPtrs` fills from
`dataPtr` at entry, so every access stayed a single `zp,Y` instruction. `muLogHi` could **not** go:
`MU_LOG_SLOTS` is 16 and there are six blocks, so each of those three arrays is 96 bytes, not 36 --
all three are 288 and only 202 were free. An assert (`MU_LOGID_END <= MU_DATA_SIZE`, and
`MU_DATA_SIZE <= 255`) now makes that arithmetic fail the build instead of passing silently.
16,614 -> **16,456 bytes**, still 65 pages.

**Left for a day the pool runs out** (all measured, none needed at margin 10): the undo window's
**256-byte resident page** (`muUndoBuf`, the largest single item in the image) can become a 64-byte
chunk window -- the accessors already go through `muUndoIo`/`muUndoWind`, so it is a granule change
and a chunk index in place of a page index, about +35 bytes of code for -192 of data; the keyboard
dots' history rings (`muHistRLo/RHi/Opt`) are **288 bytes for nine references**, the best ratio in
the file, but they cannot fit a 255-byte buffer until they shrink; and the line tables
(`muLnPh` 57 bytes, then the dozen 19-byte ones) need an `X`-indexed access converting to `Y`,
which costs code at each site rather than saving it.

Order matters: 13.1-13.3 before 13.4, because a phrase editor built on a walk that is not
proven will hide the next defect behind a new feature.

## 17. Documentation

All of it is written and current: `docs/musedit.md` (the guide, and it is in
`publish-binaries.ps1`'s `$guides`, so it ships), `README.md` (the task table and a link),
`docs/features.md`, `docs/quick-start.md`, `tools/quickstart.binaries.md`, `docs/lib-sid.md` (the
published position block), `docs/memory-map.md` (the pinned pocket and the totals) and
`reference/sidplayer-mus-file-format.md` (the bit-7 correction). A new gate would also need a row
in `docs/development.md`; none was added.
