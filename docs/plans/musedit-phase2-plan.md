# `musedit` phase 2 — what the editor does not do yet

> **Status: a draft to be revised.** The six items below are the gaps found while reviewing the
> editor against the `.mus` format (`docs/musedit-review.md`, § 6); the five carried over from the
> phase-1 plan's § 12 are the wishlist that plan recorded before it was retired. Nothing here is
> built. The order, the sizes and some of the designs are open questions — § 12 lists them.
>
> **The phase-1 plan is closed.** It lives at `Documents/GordonOSPlans/musedit-plan.md`, outside
> this repo, and describes how the editor that exists today was built. This file is the only plan
> in the tree, and it describes work still to be done — the convention in `docs/development.md`.

## 1. Where phase 1 left the code

| | |
|---|---|
| code | **17,913 bytes**, 1,817 fixups, **70 code pages** of a 256-byte granule |
| pool | **103 pages of 109** (code 70, `sid.lib` 14, `filesys.lib` 10, mouse 2, sprite 1, `kMalloc` 6) |
| zero page | 229 bytes of the 234 available (`MU_ZP_END` = `MU_ZP_BASE+229`, one byte under `$F0`) |
| gates | 83 asserts; `check-mus-commands` (30,695 pairs), `check-mus-timing`, `check-mus-pitch` (224 options), `check-musedit-walk` (two captured grids), `check-zp-map`, `check-zp-indirect`, `check-pool-budget`, `check-ascii`, `check-links` |

Two numbers govern everything below, and neither is negotiable without a design change:

- **The editor has 7 bytes of code space.** `musedit` must stay at or under 17,920 bytes to keep
  70 code pages. Every item in this plan therefore arrives with a funding line (§ 2).
- **70 code pages is the measured wall, not 105 pool pages.** The voice-terminator check was first
  written at 136 bytes, which put the code at 71 pages; the pool budget gate printed `margin 4`
  and the machine refused to play — `sid.lib` did not fit. The budget gate adds pages up; a load
  needs them in one run, and the run it needed was no longer there. Treat **70 pages** as hard.

## 2. Paying for the work

Every item is budgeted, and every budget has to be met by a saving of its own or by one of these
levers. They are listed in the order I would try them: cheapest and most certain first.

| lever | yield | cost |
|---|---|---|
| **`muMnTab` at four bytes a field** | **49 bytes** | the table is 49 × 5 (`"TEM  UTL  …"`), the trailing spaces being what lets a command line print exactly one field. Every mnemonic is three characters except **`CALL`**, which is four — so four bytes a field holds them all (196 bytes instead of 245) and the print blits one space after the four cells, giving the same 5-cell output. Not three bytes: `CALL` would not fit. The panel's one table read (`:6776`) changes its ×5 to ×4. |
| **`muUndoBuf` out of the image** | **256 bytes** (exactly one page) | the undo stack's *window* page lives in the image (`:10517`); the records it windows already live in `musedit.und`. Moving the window to a page of that file costs one DMA per push/pop — undo is not a hot path — and takes the allocator's one in-image buffer away. |
| **more shared tails** | measured: **3 bytes** for `muWinPtr` | extraction only pays for itself; it is not a strategy. Worth doing opportunistically, per item. |
| **the player becomes its own task** | **14 pages of pool**, and the run the code loads into grows by the same 14 | the structural lever, and the only one that lifts the 70-page wall. `sid.lib` would be declared by a player task that exists only while playing; `musedit` drops the declaration and stops carrying 14 pages it uses for one feature. New task ~2 code pages + a 3-page window block, so the pool peak while playing is ~108 — margin 1. Needs the shell's `? sid.lib in use` arbitration (already there) and a player that pages the same `.wrk` file (already possible: `kSidPlay` mode 1). |

Plan-level budget, if the levers are taken in order: `muMnTab` + `muUndoBuf` ≈ **305 bytes** —
more than items 1, 3 and 5 together. Items 4 and 6 and the inherited wishlist are where the wall
comes back, and that is when the player-task split earns its place.

## 3. Item 1 — `JIF`, the one command with no line

**What it is.** `aa 111110` + option. The command byte is `$3E`, `$7E`, `$BE` or `$FE` (bits 7-6
are the value's top two bits); the value is `option * 4 + bits 7-6`, a **10-bit two's complement
number**. `reference/sidplayer-mus-file-format.md` documents the range as **-200 to 757**, with
`3E 40` = 256, `7E BD` = 757 and `3E CE` = -200.

**Why the player can never honour it, and what that means for the editor.** `JIF`'s whole effect is
the CIA timer's reload value — the *length of a jiffy*. In GordonOS a jiffy **is** the scheduler
tick: the kernel's IRQ drives it once for every task, so no tune may own its length and there is no
timer for the player to reload. That is a property of a multitasking OS, not a gap to be filled
later, so a value written here has **no effect on playback, ever** — the player decodes the pair,
consumes the option byte and returns. `docs/musedit.md` already states this in the guide's command
table and in its "two commands that do nothing" note.

**What is missing.** `muDecTab` is indexed by command byte / 8 and its JIF slots — indices 7, 15,
23 and 31, i.e. `$38-$3F`, `$78-$7F`, `$B8-$BF`, `$F8-$FF` — are all `0`, so `muLnDec` answers
"nothing the grid shows" and the pair is drawn as nothing at all. `muMnTab` has no id for it (the
table's own assert pins 49 fields), and no shape encodes a 10-bit value split as `val & 3` into
the command and `val >> 2` into the option.

**To draw it.**
1. `MU_MN_JIF = 50`, and `"JIF  "` appended to `muMnTab`; the assert becomes 50 fields.
2. Four `muDecTab` entries, all `MU_MN_JIF`.
3. A JIF arm in `muLnDec`: the `muDecExact` family already covers a command byte whose low three
   bits are 6, so JIF is reached there; instead of the MS# high-byte rule it needs
   `value = (option << 2) | (cmd >> 6)`, and because the format's own examples include negatives,
   the printed form needs a minus sign — `muDec16` prints an unsigned 16-bit value, so a negative
   is `1024 + value` printed with a leading `-` (a sign flag and one conditional blit).

**To author it.**
4. A new shape (`8`) in `mqShape` with its own encoder branch before `mqEncWide`: the existing
   wide shapes shift a *masked high byte* into the command, which cannot express JIF's split, so
   this is `val & 3` into the command byte's bits 7-6 and `val >> 2` into the option.
5. Bounds: the value is **signed**, and `mqClamp` is a *minimum* clamp, so JIF needs its own
   two-sided test against -200 and 757 (refusing outside, like `muObjEnc` refuses a value the
   command cannot hold).

**Cost** ≈ 25 bytes drawing (decode + sign) + 30 bytes authoring (shape + bounds) + 15 bytes of
table (4 `muDecTab` slots, 5 `muMnTab` bytes, 1 `mqShape`). **~70 bytes**, and no new data
structures. Gate: a `check-mus-jif.py` in the shape of `check-mus-pitch.py` — encode all 958
values in range, decode them back, compare with `mus-sim.py`.

## 4. Item 2 — `MS#` to 1023: **closed, at 999**

The review's estimate was "one byte of table plus a doc fix", and that is what it cost — but the
number is **999**, not 1023, because 1023 is not in the format:

- `reference/sidplayer-mus-file-format.md` states it: *"aa = Hi Byte / bb = Lo byte of Measure #
  (Highest value allowed is 999)"*.
- The ported player's own block table confirms why: the fourth block is `11 011110` with an
  option range of `$00-$E7`, i.e. 768 + 231 = **999**. `$E8-$FF` in that block name other
  commands, so values 1000-1023 do not exist in the encoding.

Delivered in this branch: `mqShMaxHi`/`mqShMaxLo` for shape 7 carry `$03`/`$e7`, both places in
the guide say 999, and `muLnDec` still draws a 768-1023 value found in a foreign file truthfully
(it decodes every `MS#` pair from 0 to 1023 to the right id and value). **Nothing remains to do
here**; the item is recorded so the next reader does not re-open it looking for 1023.

## 5. Item 3 — Description editing

**What exists.** The `.mus` tail after the three voices is a NUL-terminated *Song Description*
(`reference/sidplayer-mus-file-format.md`), and the editor's image primitives are already
voice-agnostic: `muDelGap`'s own header says the tail behind a gap — "the rest of this voice, the
later voices, **the description**" — moves as one piece. So growing and shrinking the text needs
no new byte-moving code at all.

**What is missing is the UI.** There is no text cursor and no way to type into that region; a text
cursor over the NUL-terminated tail, drawn in row 0 (which already owns a status line and a
readout, and is repainted fixed-cell) while a description edit is open, with `INS`/`DEL` mapped to
character insertion/deletion rather than to row events.

**One trap in the primitives.** `muInsGap`/`muDelGap` take `A` = *the voice whose length word
grows or shrinks*, because every splice they were written for is inside a voice. The description
sits after all three voices, so inserting or deleting a character there changes **only**
`MU_LEN`: passing voice 3 would corrupt voice 3's length. The character path therefore needs the
gap primitives without that one store — an `A = $ff` sentinel skipping the voice word (the two
routines are already otherwise identical) is the cheap shape, ~15 bytes.

Each character is a 1-byte gap at the cursor offset, and the undo stack records it through
`muPushEdit` like every other splice, so a description edit is undoable for free — a record is a
short header plus the old and new bytes, and a character's are one byte each.

Three questions to settle (§ 12): **how much text is visible** (row 0 is 40 cells and the head's
`BAR`/readout live at its right edge — a scrolled 1-line window, or the head hidden while editing),
**which keys** (the editor's own key map is nearly full), and **how the panel shows the mode**
(`INS`/`OVR` is already the mode button, so the readout needs a third state that does not confuse
it with note-insert).

**What changes in the file.** A `SAVE` writes the whole image, so the description persists with no
new code; `muLdPages`/`muImgRoom` already count it in the image's length. **The pool budget does
not move**: the text is in the scratch file, not in the image.

**Cost** ≈ 200 bytes (a scrolled row-0 window, a character loop, key routing, the readout state).
Gate: a `py65` case that inserts, deletes and re-reads a description across a page boundary (the
interesting failure is a cursor pointing into a page that the insertion moved).

## 6. Item 4 — Play from the cursor

**Why it is not a one-liner.** The format has no seek: a voice is a stream of pairs and the player
arrives at a row by playing every pair before it. Two designs, and they differ in what the ear
hears, not just in code:

**(a) Offsets and a tick base — the cheap one.** `WorkNxtCmdLo/Hi` per voice already holds the next
command as a **file offset**, and the paged path seats a window on that offset's page, so the
player can be started at any offset. For a target row the editor's own walk already knows each
voice's next-command offset at that row (that is what the playhead's ring records) and the jiffy
count at the row start. Start there, and add the start tick to `WorkSongTicks` instead of zeroing
it (`sidplay.inc`'s `sidInitSong` clears it, so this becomes "clear it to the caller's base").
The score then starts where the cursor is and the playhead agrees from the first frame — which
also fixes the "the score rewinds on stop" annoyance.
*What it does not carry:* the sound state. Waveform, pulse, ADSR, filter, tempo and `TPS` are all
built by commands earlier in the stream, so a start at row 200 plays with the defaults until the
tune re-states them. **Honest, cheap, and wrong for most real tunes.**

**(b) A silent warm-up — the faithful one.** Start at the beginning and run the player for N
jiffies with the SID muted, then unmute at row N. Every command takes effect, so the state is
exactly what the tune built. The cost is wall-clock (row 400 at one jiffy a row is ~7 s) and it
needs an unmute the tune cannot undo: a muted flag in the player that suppresses the volume
writes' *low nibble*, or — the cheapest shape — the caller runs it muted and `kSidHush`es at the
target tick, then restarts with the offsets from (a) plus the state… which is (a) again.

**Recommendation.** (a) first, then (b) only as a "pre-roll" option on top of it: `PLAY FROM HERE`
uses (a); a checkbox or key "pre-roll" runs (b) for tunes whose sound state matters. Both keep the
editor's playhead code unchanged, because the published jiffy count is what it already reads.

**What it needs in the ABI.** The mode-1 config block is 9 bytes (a 3-byte FS offset plus three
window words); six more bytes (three start offsets) and two for the tick base fit in one page and
are read by `sidInitSong`'s caller in `sid.asm`. That is a `sid.lib` change — 14 pages of library
whose own budget has to be checked — and a mode number for it (`kSidPlay` only streams on an
exact `1`). Where the extra fields live matters: `kArg` is per-call scratch and full (delay,
repeats, transpose), so they belong in the **config block**, not in `kArg`.

**Cost** ≈ 100 bytes in `musedit` (find the three offsets and the tick for the cursor's row — the
walk already computes them — plus the config) and ≈ 60 bytes in `sid.asm` (`sidInitSong`'s
alternate entry, the three offsets, the tick base). Gate: a SID-register comparison is not
practical, so the gate is a **walk-model** one — `tools/mus-sim.py` already models the walk and
each voice's position, and a `--state N` report (what the streams have set by tick N) is the piece
to add on the host side; the player must agree with it on the first row it plays after a seek.

## 7. Item 5 — Delete row, delete bar

**What exists.**
- `muInsertTime` inserts N rows of silence by splicing a rest into *each* voice at the cursor's
  row, guarded by `muPhBlock` (a phrase body's timing is every call site's timing). The `INS` cell
  calls it with N = 1.
- `muDelKey` removes the cursor **voice's whole event** at the cursor row (`muDelGap` of its 2-byte
  pair, with `muPhHere` guarding a phrase body and an undo record), and `muDelKey2` extends that to
  a command pair on the same line. Both are single-voice edits.
- `muCurBar` already answers "the rows a bar has at the cursor's row" (the tempo's own table,
  through `muFit`), and `muSekEnd` gives the row a voice's event ends on.

**So the gap is symmetry, not primitive work.** `INS` opens a row in all three voices and nothing
closes one in all three; there is no delete-rows and no insert/delete-bar. The operation needs,
per voice, one of three cases:

1. **nothing sounds** (a gap) — nothing to delete in that voice;
2. **an event of exactly N rows** — `muDelGap` its pair, which is what `muDelKey` already does;
3. **a longer event** — shorten it. This needs the **inverse of `muRowsToJif`**: jiffies to a
   duration class and value. The editor only ever converts *forward* (a key picks the class, the
   length follows), so the inverse does not exist here — `mus-sim.py` has it and the player's own
   class lookup defines it, so it would be ~50 bytes plus a gate that holds the two together. A
   note that has to keep sounding after the deletion also needs a **split**: `muInsGap` a new pair
   so the tail keeps the note.

Deleting a **bar** is the same operation with N = `muCurBar`, so it costs the same code; the record
for a bar should be one grouped action (the `muInsFirst` convention), not N undo records.

**The cheap version worth building first:** cases 1 and 2 only, and a refusal that names the voice
whose event spans the row (`? voice 2 spans the row` — a new message slot, so the `msgs` binary and
the REU image are regenerated with it). That is the exact inverse of `INS` for rows the editor
itself opened, needs no jiffies-to-duration inverse, and is ~60 bytes.

**Cost** ≈ 60 bytes for the cheap version; ≈ 160 with the inverse and the split. Gate: extend
`check-musedit-walk.py`'s fixtures with an insert-then-delete round trip that must return the file
to its `muHist`-identical bytes, and a py65 case for the spanning-event refusal.

## 8. Item 6 — Block copy/paste

**What exists.** Nothing. There is no selection, no mark and no clipboard; `muGdClip` is a
*drawing* flag (a band repaint), not a block.

**The first design question is what a "block" is**, because the image has no rows: it has three
streams. A block has to be (a range of rows) × (a set of voices), and pasting it into a different
position means splicing each voice's span at the target row and re-timing the row count. The undo
machinery already does **range diffs** (`muPushEdit` records offset + old bytes + new bytes), so a
paste is undoable as one grouped action with no new undo code.

**Storage.** A clipboard needs somewhere to keep the spans between copy and paste. The `.und` file
is the undo stack; a second file (`<base>.clp`) is cleaner than borrowing its space, and the
editor's file layer already opens one file of its own (`muScrOpen`). A block is small: three
spans of a few hundred bytes plus a row count.

**Cost** ≈ 350-450 bytes (mark/extend, draw the marked rows reversed, copy out, paste in with
per-voice splices, the panel's readout). **The largest single item**, and the one that most needs
the funding from § 2 or the player-task split. Gate: a round trip — copy a block, paste it one bar
later, delete the pasted rows, and the file must be byte-identical to before (the same `muHist`
comparison the undo tests use).

## 9. Item 7 — Jump to a bar or a measure

Not a format gap — a navigation gap. Scrolling to bar 96 means holding a cursor key through ~90
screens, and nothing in the editor can name a position and go there.

**What exists to build on.** The walk already computes an **absolute bar number for every line** it
produces (`muLnBarLo/Hi`, "the bar each line's row falls in, from the walk" — a tempo change cuts a
bar short, so the bar is not a division of the row), `muCurBar` returns the rows a bar has at the
cursor, and `muBarOf` reads the cursor's bar out of the line table. The producer can be rewound
(`muSimInit`, tick 0) and stepped one event at a time (`muStep`), which is exactly what scrolling
already does — so a jump is **a scan with the producer it already has**, not a second walk.

**What is missing.** Two things, and they are not the same size:

- **Bars** need no new state at all: scan until `muLnBarLo/Hi` reaches the number asked for.
- **Measures** need a number the editor does not keep. It *draws* `MS#` (the decode table maps it
  to `MU_MN_MS`) but never stores its value, so the walk has to carry one measure number per voice
  as it produces lines — `MS#` is a **per-voice** command (`aa 011110` + option, voice column V),
  so the three voices may disagree about which measure a row is in. The jump's target is the
  cursor voice's number, and a tune that never writes `MS#` has no measures at all: it must say so
  rather than guess.

**Where the state goes.** Not zero page: it is 229 of the 234 bytes already (`MU_ZP_END` is one
byte under `$F0`). A per-voice measure number belongs in the line record beside `muLnBarLo/Hi`
(2 bytes over `MU_GRID_ROWS`), which also makes the scan a comparison against a line rather than
against a running variable.

**The prompt.** The control bar cannot take a new cell: `muBarMap` is 40 columns with the assert
`M_BAR_LEN == 40`, and the dispatch and hover range is `MU_BAR_Q..MU_BAR_INS` — so this is a
**key** plus a numeric prompt, reusing the command panel's value-entry path (`MU_OBJVAL` +
`muNum16`). "There is no such bar or measure" and "this tune has no `MS#`" are one new message
slot each, so the `msgs` binary and the REU image are regenerated with them.

**Cost** ≈ 140-180 bytes (scan loop, prompt, two refusals, and the head's readout if it shows the
measure). Gate: a py65 case that jumps to a bar and to a measure and checks the cursor's row
against `mus-sim.py --walk`'s bar and measure for that row — the same model the walk fixture
already uses.

**Independent of the player.** Nothing here reads the player's state: `MS#` is parsed from the
image by the editor's own walk, so the player's `MS#` handling can be anything at all — it keeps
two stores into work bytes nothing reads — without touching this feature.

## 10. Inherited from the retired phase-1 plan

These were recorded in that plan's § 12 and are still not built. They are **not** in the item
list above, and nothing here is scheduled — they are carried forward so the wishlist does not
leave the repo with the plan.

| feature | shape | note |
|---|---|---|
| full-screen grid | a control-bar toggle hides the piano and grows the grid from 18 to 22 rows | one routine, redraws on return |
| stream view | three columns of hex pairs, cursor + byte editing | the honest fallback for constructs the grid cannot show; also the way to *see* a `JIF` once item 1 exists |
| sections view | `HED`/`TAL` blocks with repeat counts and `CALL`/`DEF` sites, edited as structure | overlaps item 5's phrase guard |
| instruments | named command stacks in a sidecar REU file (`musedit.ins`), applied on note entry | no per-frame tables |
| jam/record | `3-O` the target voice, write `$D400-$D406` for audition, splice on stop | needs the mute/volume path item 4(b) also needs |

## 11. Order, and what "done" means

A suggested order — smallest risk first, and each one independently shippable:

| step | items | why here |
|---|---|---|
| 2.1 | `muMnTab` at 4 bytes, `muUndoBuf` to the file | buys ~305 bytes before anything grows; both are byte-neutral to the user |
| 2.2 | **item 1 (`JIF`)** | self-contained, ~70 bytes, one new gate, and it takes the drawn set from 49 to the format's full 50 |
| 2.3 | **item 5 (delete row / bar)** | no new UI, reuses `muDelGap`/`muInsGap`, and it is the inverse the editor is most obviously missing |
| 2.4 | **item 3 (description)** | UI-only, ~200 bytes, needs the keys decision |
| 2.5 | **item 4 (play from cursor)** | first `sid.lib` change; (a) then (b) |
| 2.6 | **item 7 (jump to a bar or measure)** | small, no format work, and it pays for itself the first time a tune runs past a screenful |
| 2.7 | **item 6 (blocks)** | the largest; needs the funding decision at that point |
| 2.8 | § 10 wishlist | re-ranked after 2.1-2.7 |

Each step is done when: the code builds with **no new code page** (or the page is paid for and the
pool gate still prints `margin >= 5`), every existing gate passes, the item's own gate passes and
has been shown to bite, the guide (`docs/musedit.md`) states the new behaviour, and `docs/bugs.md`
gained an entry for anything that surprised us. Sitting in VICE is required for anything the user
hears or types into; a py65 harness alone is not "verified".

## 12. Open questions for the revision

1. **Item 2** is closed at 999 — does the list want anything else under that heading, or should it
   come off the list?
2. **`JIF`** can never play in GordonOS (the jiffy is the scheduler tick), so the item is
   completeness only: draw it and author it so a foreign tune round-trips faithfully, or drop it
   and leave the pair untouched (`docs/musedit.md` says it is kept in the file as-is)?
3. **Description**: one scrolled line in row 0, or a full-screen text mode? Which keys?
4. **Delete row**: the cheap version (delete an event of exactly N rows, refuse the voice that
   spans the row) or the full one (a jiffies-to-duration inverse plus a split)? Is a bar delete
   wanted at all, given `muCurBar` makes it nearly free once rows work?
5. **Play from cursor**: offsets-only (cheap, wrong sound state) or a pre-roll too?
6. **Blocks**: is a block rows × voices, and does paste re-time the following events?
7. **Jump**: which key, and should the head show the measure beside `BAR`? For a tune with no
   `MS#`, is a refusal enough, or should the editor derive a measure from bars (8 rows at the
   default tempo, which is a different number and would have to say so)?
8. **Funding**: take the two cheap levers only, or plan the player-as-a-task split now — which
   would let the editor grow ~14 pages and make item 6 and the wishlist affordable?
