# `musedit` phase 2 — what the editor does not do yet

> **Status: a draft to be revised.** The six items below are the gaps found while reviewing the
> editor against the `.mus` format (`docs/musedit-review.md`, § 6); the five carried over from the
> phase-1 plan's § 12 are the wishlist that plan recorded before it was retired. Two of the six are
> settled — `JIF` is won't-do (§ 3) and `MS#` stops at 999 (§ 4) — and one is built (§ 7). The
> order, the sizes and the remaining designs are open questions; § 12 lists them.
>
> **The phase-1 plan is closed.** It lives at `Documents/GordonOSPlans/musedit-plan.md`, outside
> this repo, and describes how the editor that exists today was built. This file is the only plan
> in the tree, and it describes work still to be done — the convention in `docs/development.md`.

## 1. Where phase 1 left the code

| | |
|---|---|
| code | **17,859 bytes**, 1,869 fixups, **70 code pages** (61 bytes under the next boundary) of a 256-byte granule (71 measured working) |
| pool | **104 pages of 109** (code 70, `sid.lib` 15, `filesys.lib` 10, mouse 2, sprite 1, `kMalloc` 6) |
| zero page | 229 bytes of the 234 available (`MU_ZP_END` = `MU_ZP_BASE+229`, one byte under `$F0`), every one of them declared |
| gates | 83 asserts; `check-mus-commands` (30,695 pairs), `check-mus-timing`, `check-mus-pitch` (224 options), `check-musedit-walk` (two captured grids), `check-zp-map`, `check-zp-indirect`, `check-pool-budget`, `check-ascii`, `check-links` |

Two numbers govern everything below, and neither is negotiable without a design change:

- **The editor can carry 317 more bytes of code.** `musedit` is 17,859 bytes, 70 code pages, and
  **71 pages has been measured working**: with `sid.lib` trimmed to 14 pages, a 71-page build
  (17,929 bytes, pool 104 of 109, margin 5) was run in VICE, loaded the tune and played it. So the
  usable ceiling is the **71-page** line, 18,176 bytes; a 72nd page lands on pool margin 4, which is
  the figure that refused to play before the trim, and is untested.
- **The wall moves with the pool, not with the byte count.** The voice-terminator check was first
  written at 136 bytes, which put the code at 71 pages; the pool budget gate printed `margin 4` and
  the machine refused to play — `sid.lib` did not fit. The budget gate adds pages up; a load needs
  them in one run. Every page of pool margin spent on the editor's code is one that `sid.lib` (14)
  and `filesys.lib` (10) then have to do without, so growth past 71 pages needs a funding lever
  (§ 2), not just spare bytes.

## 2. Paying for the work

Every item is budgeted, and every budget has to be met by a saving of its own or by one of these
levers. They are listed in the order I would try them: cheapest and most certain first.

| lever | yield | cost |
|---|---|---|
| **`muMnTab` at four bytes a field** — **done**, **54 bytes** | the table was 49 × 5 (`"TEM  UTL  …"`, 245 bytes) and is now 49 × 4 (196): every mnemonic is three characters and a padding space, except **`CALL`**, which fills the field. Four bytes is the only width that holds them all (three would not fit `CALL`). `muMnPrint` needs no padding byte: it stops at that space or at the fourth cell, so it emits the same cells as before for all 49 ids. The stride multiply became two `asl`s, which is the other **5** bytes. The space between a mnemonic and its operand was always the caller's, not the table's. Not free: the gate that parses this table (`tools/check-mus-commands.py`) slices it by field width, so its stride changed in the same commit. |
| **more shared tails** | measured: **3 bytes** for `muWinPtr` | extraction only pays for itself; it is not a strategy. Worth doing opportunistically, per item. |

**Done already — the undo history is out of the image.** There is no resident window page: every
stack byte is one `kReuReadAt`/`kReuWriteAt` of length 1 at the cursor, so nothing of the stack is in
the image and no page-boundary arithmetic is needed. The file is also deleted at exit, so a session
leaves nothing behind.

Plan-level budget: the `muMnTab` lever is spent and item 4 has taken `sid.lib` over its 14th page,
so the **317 bytes in hand** — 17,859 to the 71-page line at 18,176 — are the whole budget. `musedit`
is also only 61 bytes short of its own page boundary, and the pool sits at margin 5, so a change past
that boundary costs a page as well. Item 3 fits by taking that page; item 6 (blocks, 350-450 bytes)
needs a lever of its own.

**Where the player lives, and where it stays.** The music runs in a **thread child of the editor**:
it shares the editor's code pages and the editor declares `sid.lib` for the session, paging
`<base>.wrk` through three windows of the same six-page block. That is the architecture — one task,
one set of code pages, no spawn or reap per tune, and the tune it plays is the file the editor is
editing, so an edit is heard on the next pass with no copy. A separate player task is not a
candidate for making room; the budget below is the whole of it.

## 3. Item 1 — `JIF`: **won't do**

**Decision.** The editor will neither draw nor author `JIF`. Its only effect is the length of a
jiffy, and in GordonOS a jiffy **is** the scheduler tick — the kernel's IRQ drives it once for every
task, so no tune may own its length and there is no timer for the player to reload. That is a
property of a multitasking OS, not a gap to be filled later. The player decodes the pair, consumes
the option byte and returns, so a value written here can never change what is heard: the work would
cost ~70 bytes of pool pages and 15 bytes of tables for no benefit to the tune being edited. The
pair is left in the file untouched, so a foreign tune still round-trips byte for byte, and the grid
shows nothing where it sits — which is what `docs/musedit.md` states.

**The format, for anyone revisiting this.** `aa 111110` + option. The command byte is `$3E`, `$7E`,
`$BE` or `$FE` (bits 7-6 are the value's top two bits), so the value is `option * 4 + bits 7-6`, a
**10-bit two's complement number**; `reference/sidplayer-mus-file-format.md` gives the range as
**-200 to 757** (`3E 40` = 256, `7E BD` = 757, `3E CE` = -200). The grid draws nothing for it
because `muDecTab`'s slots for `$38-$3F`, `$78-$7F`, `$B8-$BF` and `$F8-$FF` are all `0`, and
`muMnTab` has no id for it.

**What drawing and authoring would have taken**, recorded so the decision is cheap to revisit:
a 50th mnemonic id (`MU_MN_JIF = 50`, `"JIF  "` appended to `muMnTab`, its assert raised to 50
fields); four `muDecTab` entries; a JIF arm in `muLnDec` decoding `(option << 2) | (cmd >> 6)` and
printing a negative as `1024 + value` behind a minus sign, because `muDec16` prints unsigned; a new
`mqShape` whose encoder splits the value the other way (`val & 3` into the command, `val >> 2` into
the option — the existing wide shapes shift a masked high byte and cannot express it); and a
two-sided bounds test against -200 and 757, since `mqClamp` is a minimum clamp only.

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

## 6. Item 4 — Play from the cursor — **done**

`SPACE` now starts the pass **at the cursor's row**: the editor hands the player the tick that row
starts at (the walk already records it per line, in `muLnTick`/`muLnTickHi`), and the player
**fast-forwards** — it plays the pass from the first row with the volume nibble masked, one jiffy
after another as fast as the CPU allows, then lifts the mask at that tick. Every command takes
effect on the way, so the waveform, pulse, ADSR, filter, tempo and transposes are the tune's own;
the published jiffy count is already the start tick, so the playhead picks up exactly there.

**What was rejected, and why the shape changed.** The plan's (a) — start the three streams at
captured offsets — is cheap but carries *no* sound state, so a mid-tune start plays with the
defaults. Its (b) — the same idea in real time — was measured to cost the tune's own elapsed time
(13-29 s for a row an ordinary tune reaches in the middle, up to 145 s at its end). The measured
per-jiffy cost of the player made a third shape obvious: **~1,318 cycles a jiffy** (7.9 % of a
16,667-cycle jiffy) and **~400-460 cycles per command**, so a silent fast-forward is ~13× quicker
than real time and needs *no* seek data at all — the streams arrive where they belong.

| target (katmand) | real-time warm-up | **fast-forward, built** |
|---|---|---|
| row 100 | 5 s | 0.4 s |
| row 400 | 20 s | 1.6 s |
| row 800 | 41 s | 3.2 s |
| last row | 145 s | 11 s |

**What it needs.** `kArg+4`/`kArg+5` = the jiffies to play silently first (latched at the call),
`VolMask` in `sidplay.inc` (`$FF`, `$F0` while folding) and the `and` on the one volume store,
the fold loop in `sid.asm`, and in `musedit` `muPlayBase` (the cursor line's tick, plus presetting
the playhead's row) and the hold in `muPhSync` that keeps the score still until the music arrives.
`muPlayHome` — the "take the view home for a pass" repaint — is gone, because the view no longer
moves.

**What the ear loses**: only the SID's own envelope generators, which run at the compressed speed, so
a note already sounding when the mask lifts is caught earlier in its envelope. Everything the tune
sets is exact.

**Cost**: `sid.lib` **+63 bytes** (3,548 → 3,611), which took it from 14 to **15 pages** — it had 36
bytes of room in its 14th page and the fold needs more; `musedit` **+32 bytes** (17,827 → 17,859,
still 70 pages). Pool 103 → **104 of 109, margin 5**, the configuration this plan measured working.
Every task that declares `sid.lib` pays the page with it (`gortris` 51 → 52, `sidplay` 30 → 31,
`godzi` 33 → 34, `grknoid` 47 → 48).

**Verified in VICE**: `SPACE` at row 1 starts instantly; a stop at row 594 holds R594 for the whole
fold and the music starts there **3.2 s** later, then continues from that row; and the per-voice
next-command offsets after a folded pass are **byte-identical to a natural pass at the same ticks**
(22 of 25 sampled ticks exactly, the other three being one-command sampling races at event
boundaries) — which is the evidence that every command before the start row was applied.

## 7. Item 5 — Delete row, delete bar — **done**

`muDelRows` (`R` = one row, `SHIFT+R` = a bar) deletes N rows at the cursor from every voice. Per
voice it seeks with `muSeekAt` and takes one of two cases: **nothing sounds** on the row (kind 2) is
left alone, and an event **of exactly one row** has its pair spliced out by `muDelPair` — the 2-byte
`muDelGap` delete `muDelKey` uses, extracted so both share it. All three voices are **checked before
any is spliced**, so a refused delete leaves the image byte-identical, and an event that spans the
row refuses it with `? a note in voice N spans the row` (three new `msgs` slots, chosen by index so
the voice needs no dynamic text).

The action is **one `Z`**: the first push opens the group (`muRecCont = 0`) and every push after it
appends (`muRecCont = 1`, which `muPushEdit` clears again, so the flag is closed at the end of the
operation and the next edit starts its own action). A row with nothing on it at all answers
`? no event here`.

Cost: **259 bytes** — code 69 → 70 pages, pool 102 → 103 of 109, margin 6 — of which
the `muDelPair` extraction paid back 26. The inverse-duration and split cases (item 3 in the list
below) are **not** built: they need the jiffies-to-duration inverse, which does not exist in the
editor.

**What existed, for the record.**
- `muInsertTime` inserts N rows of silence by splicing a rest into *each* voice at the cursor's
  row, guarded by `muPhBlock` (a phrase body's timing is every call site's timing). The `INS` cell
  calls it with N = 1.
- `muDelKey` removes the cursor **voice's whole event** at the cursor row (`muDelGap` of its 2-byte
  pair, with `muPhHere` guarding a phrase body and an undo record), and `muDelKey2` extends that to
  a command pair on the same line. Both are single-voice edits.
- `muCurBar` already answers "the rows a bar has at the cursor's row" (the tempo's own table,
  through `muFit`), and `muSekEnd` gives the row a voice's event ends on.

**Still open, if the long form is ever wanted** — shortening a note that runs past the row instead of
refusing it needs the **inverse of `muRowsToJif`** (jiffies to a duration class and value; the editor
only ever converts forward) and a **split** (`muInsGap` a new pair so the tail keeps the note), ≈ 160
bytes in all.

**Gate as built**: `check-pool-budget` (103 of 109), the 83 asserts, and a VICE round trip — insert N
rows, delete N rows, save, and the `.mus` must come back byte-identical to the file that was opened.

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
the funding from § 2. Gate: a round trip — copy a block, paste it one bar
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
| stream view | three columns of hex pairs, cursor + byte editing | the honest fallback for constructs the grid cannot show, `JIF` included |
| sections view | `HED`/`TAL` blocks with repeat counts and `CALL`/`DEF` sites, edited as structure | overlaps item 5's phrase guard |
| instruments | named command stacks in a sidecar REU file (`musedit.ins`), applied on note entry | no per-frame tables |
| jam/record | `3-O` the target voice, write `$D400-$D406` for audition, splice on stop | needs the mute/volume path item 4(b) also needs |

## 11. Order, and what "done" means

A suggested order — smallest risk first, and each one independently shippable:

| step | items | why here |
|---|---|---|
| 2.1 | ~~**`muMnTab` at 4 bytes**~~ — **done**, 54 bytes | byte-neutral to the user: the same characters in the same cells |
| 2.2 | ~~**item 5 (delete row / bar)**~~ — **done**, `R` and `SHIFT+R` | it was the inverse the editor was most obviously missing |
| 2.3 | **item 3 (description)** | UI-only, ~200 bytes, needs the keys decision |
| 2.4 | ~~**item 4 (play from cursor)**~~ — **done**, the silent fast-forward | it cost `sid.lib` a page, which is why it wanted to be after the byte work |
| 2.5 | **item 7 (jump to a bar or measure)** | small, no format work, and it pays for itself the first time a tune runs past a screenful |
| 2.6 | **item 6 (blocks)** | the largest; needs the funding decision at that point |
| 2.7 | § 10 wishlist | re-ranked after 2.1-2.6 |

Items 1 and 2 are **closed** and are not steps: `JIF` is won't-do (§ 3), and `MS#` stops at 999,
which is the format's own maximum (§ 4).

Each step is done when: the code builds with **no new code page** (or the page is paid for and the
pool gate still prints `margin >= 5`), every existing gate passes, the item's own gate passes and
has been shown to bite, the guide (`docs/musedit.md`) states the new behaviour, and `docs/bugs.md`
gained an entry for anything that surprised us. Sitting in VICE is required for anything the user
hears or types into; a py65 harness alone is not "verified".

## 12. Open questions for the revision

1. **Item 2** is closed at 999 — does the list want anything else under that heading, or should it
   come off the list?
2. **`JIF`** (item 1) is **closed as won't do** (§ 3): the player can never honour it, so drawing
   or authoring it would spend pool pages on a value nothing can hear. The pair stays in the file
   untouched and the grid shows nothing there.
3. **Description**: one scrolled line in row 0, or a full-screen text mode? Which keys?
4. **Delete row** (item 5) is closed at the cheap version: it refuses a note that spans the row.
   Does anything need the long form (a jiffies-to-duration inverse plus a split, ~160 bytes)?
5. **Play from cursor**: offsets-only (cheap, wrong sound state) or a pre-roll too?
6. **Blocks**: is a block rows × voices, and does paste re-time the following events?
7. **Jump**: which key, and should the head show the measure beside `BAR`? For a tune with no
   `MS#`, is a refusal enough, or should the editor derive a measure from bars (8 rows at the
   default tempo, which is a different number and would have to say so)?
8. **Funding**: the **317 bytes** in hand — the `muMnTab` lever is spent — are the whole budget,
   enough for items 3 and 7. Item 6 (blocks) needs a lever of its own or a cheaper design;
   which does the list prefer?
