# `musedit` — branch review

A review of everything on the `musedit-stream` branch against `main`: the new editor task, the
kernel/library/shell changes it needed, the tools it added and the guide it ships.

**Scope.** 72 files, +22,712 / −677 lines. The bulk is `src/tasks/dynamic/musedit.inc`
(10,454 lines) plus four generated includes; the rest is small and surgical — 105 changed lines in
`shell.inc` (`sid.lib` arbitration plus a spawn check), 53 in `sid.asm` (the strict-1 stream mode),
21 in `sidplay.inc` (the published playhead), 11 in `main.asm` (asserts), and ~25 across five
kernel files of which exactly two lines are code (`inc tickCount` and its one-byte store).
Tools: `mus-sim.py` (the host model), three `gen-musedit-*` generators, four
`check-mus*`/`check-musedit-*` gates, `check-zp-indirect.ps1`, `vice-drive.py`.

**Method.** The diff was read directly; the gates in the tree were run (see the appendix); each
documented behaviour in `musedit.md` was traced to the routine that implements it; the format was
checked against `reference/sidplayer-mus-file-format.md` and against the nine bundled tunes with a
host probe; the pool figures come from `check-pool-budget.ps1` and the build output.

**Verdict.** The work is in good shape: the invariants that matter are asserted, gated or modelled,
and the one memory-corruption class this design could have had (writes sized from tune data) is
guarded at every site I could find. What was left after the first pass — a zero-page map collision,
a command prompt that could write a different number from the one it showed, and a shell argument
the new `sid.lib` check silently consumed — is **fixed and verified** (F14, F15, F16). The
remainder is doc/code housekeeping and a few robustness holes a malformed or foreign `.mus` could
walk into (F9, F10 and the doc items); nothing is a blocker.

F1-F13 are from the direct read; F14-F16 came from a second, independent pass over the same diff.

Severity: **Blocker** (must fix before merge) · **High** (fix soon) · **Medium** · **Low** ·
**Note** (worth knowing, no action required).

---

## Findings at a glance

| # | Sev | Where | Finding |
|---|---|---|---|
| F1 | Medium | `musedit.inc:373`, `:6770`, `musedit.md:551`/`:616` | `MS#` was documented three ways (999 / 767 / 1023) and the encoder capped at 767 — **fixed: the encoder carries the format's 999, asserted against its mask** |
| F2 | Low | `musedit.inc:8647`, `:6236`, `musedit-msgs.txt` | Two unreachable "`? not yet`" branches and one unused message (`? not a tune`) — **fixed: the branches are gone and the hover no longer lights a code with no action** |
| F3 | Low | `musedit.inc:403` | `MU_BAR_INS`'s comment said "toggles insert/overwrite"; the cell inserts one row — **fixed: the comment says what it does** |
| F4 | Medium | `musedit.md:700-717` | The guide documented 11 of the editor's 23 messages; twelve reachable ones were missing — **fixed: all 23 now have a row, with the trigger read off the code** |
| F5 | Medium | `.github/workflows/gates.yml` | The four pure-Python musedit gates never run in CI, so a PR can break the command tables or the timing model and stay green — **fixed: they run there now** |
| F6 | Medium | `tools/temp/symout/assemble-tasks.vs` | A 5,852-line generated VS64 symbol dump is committed; nothing reads it — **fixed: removed, and `tools/temp/` is ignored** |
| F7 | Low | `docs/development.md:637` | The generated-file table listed one of the three `gen-musedit-*` generators — **fixed: all three have a row, with their sources of truth** |
| F8 | Low | `tools/check-musedit-walk.py` | The walk gate compared 19 lines of one tune with one command a line — **fixed: a second capture adds three-command lines, and the gate says what no capture can see** |
| F9 | Low | `musedit.inc:679` (`muWinSeat`) | The "a command pair never straddles a window" proof needs every voice length to be even; nothing checks it (all nine bundled tunes are) |
| F10 | Low | `musedit.inc:723`, `:782` | The window fill/flush ignore the REU transfer's C flag, so a failed transfer draws garbage or silently drops a scratch write |
| F11 | Note | `musedit.inc:384`, `musedit.md` | `<base>.wrk` is a fixed 65,280 bytes per session and the guide never states the tune-size cap that implies |
| F12 | Note | `check-pool-budget` | `sid.lib`'s 15 pages are reserved for the whole session — 14% of the pool — for a feature used only on PLAY |
| F13 | Note | `musedit.inc:7021` | The undo stack's "no room" policy forgets the entire history; sound, but only because a record is bounded at 516 bytes |
| F14 | Medium | `musedit.inc:6541` | `MU_OBJMAXHI` was only written by the 14-bit command encoders, so after any `P-W`/`DTN`/`POR`/`MS#` the prompt stopped clamping byte-wide operands — it showed `300` and wrote `44` — **fixed: cleared on every redraw** |
| F15 | Medium | `musedit.inc:221` | `muInsFrom` (2 bytes, `+224`) **overlapped** `muObjValHi`/`muObjMaxHi`; latent, and invisible to the window asserts — **fixed: moved to `+227`, `MU_ZP_END` to `+229`, and a new gate checks the map** |
| F16 | Low | `shell.inc:1516` | `rfSidBusy` zeroed `kArg+2`, which `rfLoad` had just restored as `kDynLoad`'s program-RAM page count — **fixed: the routine saves and restores it** |

---

## 1. Issues

### F1 — `MS#` is documented three ways and capped at a fourth (Medium)

`MS#` is "aa 011110" + option, so the encoding carries 10 bits: 0-1023 (the format's own table in
`reference/sidplayer-mus-file-format.md` puts the limit at 999). The editor:

- **decodes** all four high-byte values (`muDecTab`, `musedit.inc:10283-10289`, has four `MS#`
  slots), and `muDec16` is a 5-weight 16-bit printer, so a file holding 1000 displays correctly;
- **wrote** at most 767: `mqShMaxHi`'s third byte `$02` (4095, 16383, 767);
- says **0-999** in the mnemonic's own comment (`:373`) and in the guide's command table
  (`musedit.md:616`), and **767** in the guide's `Values` paragraph (`musedit.md:551`).

So a value of 768-1023 survives a load and is drawn truthfully, but `C` on that line opens the
prompt at 767 and the clamp (`mqClamp`) quietly rewrites it lower. Three statements, three numbers.

**What the number should be — 999, and the fourth block says so.** `reference/…` states the limit
outright ("Highest value allowed is 999"), and the ported player's own block table
(`sidplay.inc:1349-1357`) enumerates it: blocks 1-3 run `$00-$FF`, and block 4 — `11 011110` — runs
`$00-$E7`, i.e. 768 + 231 = 999. `mqShCmdMask` for shape 7 is already `$03`, two bits, so the
encoder has always been able to write the fourth block; only the declared maximum stopped one block
short.

**One claim in the first draft of this finding was wrong.** It said "no bundled tune uses `MS#` at
all", and that is what made "keep 767" look reasonable. Decoding all nine tunes from offset 8:
**563 `MS#` pairs across six of them** — `katmand` 267, `tambrn` 156, `arose` 68, `commodo` 50,
`fsonata` 21, `immig` 1 — with the highest value **595** (`immig`, pair `9E 53`, block 3). Heavily
used, never at the cap, so nothing is broken today and the recommendation had no basis.

**Fixed, in two data bytes.** `mqShMaxHi`'s shape-7 entry `$02` → `$03` and `mqShMaxLo`'s `$ff` →
`$e7`; `mqClamp` is already a general 16-bit minimum, so it stops at exactly 999 with no routine
changed. The three maximums and their masks are now `.const`s that build both tables, so a maximum
and its mask cannot drift apart, and three `.assert`s (`MQ_WMAX_x <= MQ_WMASK_x`) fail the build if
one ever outgrows the other — a maximum the mask truncates is a display/write mismatch by
construction, which is F14's shape of bug. `docs/musedit.md:551` says 999, and the `mqEncWide`
header comment with it.

`tools/check-mus-commands.py` learned to resolve a `.const` in a table (its sweep limit comes from
these two tables), so the sweep grew by itself: **30,463 pairs → 30,695**, the 232 new ones being
`MS#` 768-999, all passing.

Verified on the built image with py65: `muObjEnc` + `mqClamp` report the maximum as **999** and
leave 768 → 768, 999 → 999, 1000 → 999, 1023 → 999, while `P-W`/`DTN` 4095, `POR` 16383 and the
byte-wide `MAX` 255 behave exactly as they did; `muLnDec` reads every `MS#` pair from 0 to 1023
back to id 47 with the value intact, so a foreign file's 800 still displays as 800. The assert was
proved to bite: with `MQ_WMAX_MS = $04` the assembler prints
`MS#: the maximum fits the command byte's mask=0 (1) -- ERROR IN ASSERTION!!!` and `kick.ps1`
aborts the build. `musedit` stays 17,850 bytes / 70 pages / 1,805 fixups (data only), and all ten
source gates pass.

### F2 — Dead scaffolding and an unused message (Low)

`M_NOT_YET` (`? not yet`) had exactly two uses and neither could run:

- `muClkNotR` was the bar-click dispatcher's fallthrough. Every code in `muBarMap`
  (`:10206-10209`) is `0` or one of `2..11`; `0` returns early, and `2..11` all branch
  to a handler. Nothing could reach the fallthrough.
- `mqRefuseCmd` was reached only when `mqShape` held a `9` ("a shape this build cannot
  write at all"). `mqShape` (`:6756-6762`) is 49 bytes of `0..7` and never holds a `9`.

`M_NOT_TUNE` (`? not a tune`, `assets/ui/musedit-msgs.txt`) is referenced nowhere.

Both placeholders are the same fossil: the bar's `INS` cell once meant *mode toggle* (see the
stale comment in **F3**) and the mode later moved to the row-1 button, while the sound commands'
last encoders were completed — and the "not yet" branches outlived both.

**Fixed, and the consequence handled where the user would meet it.** Deleting the fallthrough
alone would have left a hole: a bar code added later with no handler would still *light up* under
the pointer and then do nothing on a click — a dead button, which is worse than a silent column.
So the bar's two paths now read the same statement of "this code has an action":

- the click chain dispatches `MU_BAR_Q`..`MU_BAR_INS` and its fallthrough is the routine's exit;
- `muBarHover` ignores anything outside that range and stores `0` instead, and `muBarPaint`
  already skips `0` — so a code with no handler never lights;
- `MU_BAR_INS` is the one constant that says "the highest live code", read by both paths, so
  adding an item means adding its handler and bumping it, and forgetting the handler leaves an
  inert column rather than a lit dead button.

The invariant is stated where the map lives (`:10210-10213`) and `M_NOT_YET` is gone from the
message file. **`M_NOT_TUNE` is kept deliberately**: it is the message **F9** needs to reject a
`.tune` whose voice length is odd.

Verified: the click tail assembles to `cmp #11 / bne +$6E` with the target landing on the `rts` at
`$855E`; the hover test assembles to `cmp #2 / bcs / lda #0 / cmp #12 / bcc / lda #0` at `$678B`;
`? not yet` appears zero times in the code image and zero in `assets/ui/musedit-msgs.bin`, which is
now 31 slots and byte-identical to what the REU image ships (extracted with
`check-reu-fs.ps1 -Extract msgs`). `musedit` is 24 bytes smaller — 17,850 bytes, 1,805 fixups,
still 70 pages — and all ten source gates pass.

### F3 — Stale comment on a bar code (Low)

`musedit.inc:403`: `.const MU_BAR_INS = 11 // ...and this one toggles insert/overwrite`. The cell
inserts **one row** (`muBarIns:8801` → `muInsertTime` with A=1), which is what the guide says
(`INS` = "opens one row of silence in all three voices — the `I` key"). The mode toggle lives on
the mode row (`MU_MODE_ROW` → `muClkMode`). Fix the comment.

**Fixed.** The comment now reads "...and this one opens one row of time in every voice", in the
same form as the nine entries above it. Nothing else changed: the bar map, the dispatch chain and
the guide were already right, so the assembled task is byte for byte what it was.

### F4 — The troubleshooting table covers under half the messages (Medium)

The editor's message file holds 23 strings that begin with `?`. Checking each one against the
guide as it stood, **11 were documented and 12 were not**: the troubleshooting table
(`musedit.md:700-717`) has 11 rows, two of which are not musedit's own — `? already running` is the
loader's and `? sid.lib in use` the shell's — and the body mentions `? dir full` (`:680`),
`? not a phrase` (`:431`) and a shortened `? unsaved` (`:106`). Missing and **reachable**:

| message | when |
|---|---|
| `? hlt ends the voice` | `SHIFT+INST/DEL` on the terminator |
| `? nothing to undo` / `? nothing to redo` | `Z` / `X` with an empty history |
| `? write failed` | a write into the REU filesystem failed part way |
| `? no fs` / `? no scratch` / `? no windows` | the pool or the filesystem refused at startup |
| `? no definition` / `? no call` | `SHIFT+D` on a `CALL` with no `DEF`, or a `DEF` with no call site |
| `? saved` | the save succeeded |
| `? unsaved - q again` | `Q` with unsaved edits — `:106` names only `? unsaved` |
| `? not a tune` | the file is shorter than a tune header |

`? not yet` no longer exists (F2), so it is not among the 23.

**Fixed.** The table is now 25 rows and covers all 23 `?` strings in `musedit-msgs.txt` (the other
two rows are the loader's `? already running` and the shell's `? sid.lib in use`): six added in the
editing group (`? hlt ends the voice`, `? nothing to undo`, `? nothing to redo`, `? no definition`,
`? no call`, `? not a phrase`), one at startup (`? no windows`), and seven in a new files group
(`? no fs`, `? no scratch`, `? dir full`, `? write failed`, `? not a tune`, `? saved`,
`? unsaved - q again`). Each trigger was read off the code rather than guessed, and the rows carry
what the reader needs to act on: `? no scratch` names the 255-page working file and its 65,280
bytes; `? no fs` says the shipped image always carries the signature, so the fix is to put that
image back rather than `format` it (the host builder seeds the files, and `format` makes an empty
filesystem); and `? write failed` says the working file still holds the image, so a second save is
safe. `? not a tune` is documented here as what it does today — refuse a file shorter than a
header — and F9 will give it its second trigger. The census was re-run against the edited guide
string by string: **11 of 23 before, 23 of 23 now.**

### F5 — The musedit gates are not in CI (Medium)

`.github/workflows/gates.yml` runs nine gates: fs offsets, lib ids, ZP indirect, header offsets,
generated task-asm, jump-table docs, call inventories, ASCII, links. `build-tasks.ps1` runs five
more that need **no assembler** and no binaries — they are plain Python over the tree:

| gate | what it protects |
|---|---|
| `check-mus-commands.py` | 30,463 pairs: the editor's command tables against `gen-mus.py` |
| `check-mus-timing.py` | the duration/unit/bar model against `sidplay.inc` (an 8,192-case sweep) |
| `check-musedit-walk.py` | the host walk against a captured screen |
| `gen-musedit-glyphs.py -Check`, `gen-musedit-tables.py -Check`, `gen-musedit-msgs.py -Check` | the four generated includes are current |

A PR that edits `mqCmdB`/`mqOptB`/`muDurTab`, or regenerates nothing, passes CI today. Adding six
steps is a few lines of YAML and closes the largest hole in the review (see also **F8**).

**Fixed.** The workflow now runs `check-mus-commands.py`, `check-mus-timing.py`,
`check-musedit-walk.py` and the three `gen-musedit-* -Check` steps after `check-links.py`, and its
header says why they belong there. `docs/development.md` gains a row for each in the gates table
(with `task build, and CI` in its *Runs in* column) and names them in the *Run by CI* paragraph.
Verified: all six exit 0 locally, and none of them touches `tasks-out/`, `build/` or the assembler.

### F6 — A generated symbol dump is committed (Medium)

`tools/temp/symout/assemble-tasks.vs`, 5,852 lines, produced by the VS64 build. Nothing in
`tools/` or `project-config.json` reads it. `.gitignore` ignores `/temp/` (the repository root) but
not `tools/temp/`, and its own comment says *"nothing in temp/ is worth committing"*.

**Action:** `git rm` it and add `tools/temp/` to `.gitignore`. It is 5,852 lines of the diff that
make every future review of this branch noisier than it needs to be.

**Fixed.** The file is removed from the index and the working tree, and `.gitignore` now ignores
`/tools/temp/` beside `/temp/` — `git check-ignore` confirms both the `.sym` and the per-task
`base.bin` intermediates are covered. The next build regenerates them locally and nothing commits
them.

### F7 — Two generators are undocumented (Low)

`docs/development.md:637` documents `gen-musedit-msgs.py` → `assets/ui/musedit-msgs.bin` and
`musedit-msgs.inc`. `gen-musedit-glyphs.py` (→ `musedit-glyphs.inc`: the piano cell table and the
two key maps) and `gen-musedit-tables.py` (→ `musedit-tables.inc`: the player's own tables, byte
for byte) are missing from that table, and from any other. Anyone editing the generated files by
hand gets no warning from the docs — only from the build.

### F8 — The walk gate is 19 lines deep (Low)

`check-musedit-walk.py` compares `mus-sim.py --walk` against
`tools/fixtures/musedit-walk-immig.txt`, which holds the **opening 19 lines** of `immig` — one
command per line. The parts of the editor most likely to drift are not in it: the playhead and its
follow, the scroll, the split, undo, and command authoring.

**Half of that was a bad ask, and the other half is fixed.** A text capture can never see the
playhead, the scroll, the split or undo: those are *behaviour*, and they leave no distinct grid
text — they need a scripted session that performs the edit and reads the framebuffer, not a
fixture. What a capture *can* add is more of the walk, and the gap that was real is
**multi-command lines**: `immig`'s opening has one command per line, so a line carrying three —
the shape a single-command fixture cannot see — was untested.

So the gate now has a second case, `tools/fixtures/musedit-walk-katmand.txt`, captured from the
running editor's band: **three commands on one line** (`VOL 10 | P-S 100 | P-S 20`), `FLG`, `TEM`,
and a 16-row bar. It matched the model over all 19 lines first time. The gate's docstring now says
how a capture is made (launch, `run musedit <tune>`, decode rows 2-20) and why a case must start at
walk line 1 — the row column is **bar-relative**, so a window taken further in would need the model
to reproduce the bar numbering, and nothing does that today.

Proved to bite rather than asserted: with `P-S 100` changed to `P-S 101` in the fixture the gate
reports `katmand DRIFTED at line 0` and exits 1; restored, both cases pass.

Found on the way, and fixed: `tools/shots.py`'s `rom_args()` **invents** a `-directory` search path
(`~/.local/share/vice:/usr/share/vice`) when `vs64.viceArgs` names none, and `-directory` *replaces*
VICE's own path — so on a Windows install whose ROMs sit beside the exe, every launch died with
`Couldn't load kernal ROM 'kernal-901227-03.bin'` and no window at all, which reads as "VICE will
not start". The path is now passed only when it exists.

### F9 — The window invariant assumes even voice lengths (Low)

`muWinSeat`'s header (`musedit.inc:679`) proves that a 2-byte command can never straddle a
256-byte window: windows are page-aligned, and "a command is a 2-byte pair at an **even** offset".
That holds only if every voice's stream starts at an even offset — header 8, then `8+len1`,
`8+len1+len2`. Each voice's length is a stream of pairs plus `HLT`, so a *well-formed* file is
always even; the format does not say so, and the editor does not check.

I probed all nine bundled tunes: every length is even and every stream starts even, so nothing in
the repository is exposed. A hand-written or foreign file with an odd length would read a pair
across the page boundary — the address still lands inside a window, so this is wrong data on
screen, not corruption — and everything after the first odd voice would be misparsed.

**Fix:** at open, reject a length with bit 0 set, saying `? not a tune` (the message already
exists and is unused). Six bytes in `muLoadImage`/`muParseHeader`.

### F14 — The command prompt's 16-bit clamp goes stale after a 14-bit command (Medium)

`mqClamp` (`:6415`) clamps `MU_OBJVAL` against the pair `MU_OBJMAXHI:MU_OBJMAX`, and
`muObjEnc` (`:6538`) re-derives only the **low** half on every redraw (`lda mqMax,y; sta muObjMax`).
The high half has exactly one writer in the file — `mqEncWide` (`:6607`) — reached for shapes 5-7
only, i.e. `P-W`, `DTN`, `POR` and `MS#`. Every other mnemonic inherits the last wide command's
high maximum.

So after authoring any of those four, the clamp for a byte-wide operand is skipped:

```
mqClamp:  lda muObjValHi
          cmp muObjMaxHi      ; stale $0F (P-W) or $3F (DTN/POR) or $02 (MS#)
          bcc mqClRts         ;1 < 15, so: no clamp at all
```

`mqValAcc` (`:6394`) builds the operand in 16 bits and `mqDraw` prints all of it, so the prompt
shows **300**; `mqEncVal` writes only the low byte into the option byte, so the file gets **44**.
That is the one thing the guide promises cannot happen — *"a value the command cannot hold is
clamped to its maximum rather than refused, so what the prompt shows is always what gets
written"* (`musedit.md:552`) — and it needs no malformed file, only a `P-W` edit followed by an
`F-C` edit in the same session.

**Fix:** three instructions in `muObjEnc`, right after `sta muObjMax`:

```asm
    lda #0
    sta muObjMaxHi          // shapes 5-7 set their own high half in mqEncWide
```

**Fixed** exactly so (4 bytes: `lda #0` + a zero-page `sta`). Verified twice on the built image:
statically there is now **one** `sta muObjMax / lda #0 / sta muObjMaxHi` sequence in the file
(`$7723`), inside `muObjEnc`; and `mqClamp` was emulated under py65 with the operand 300 against
max `255` — **255** with the high half cleared (the fix), **300** with a stale `$0F` (the bug,
reproduced), plus the boundary cases `P-W` 4095 / `MAX` 255 / "exactly the max" all correct.

### F15 — `muInsFrom` shares zero page with the command dialog's high half (Medium, latent)

`.label muInsFrom = MU_ZP_BASE+224 // 2 bytes` (`:222`) sits on top of `muObjValHi`
(`MU_ZP_BASE+224`) and `muObjMaxHi` (`MU_ZP_BASE+225`), which the command dialog uses for its
16-bit operand. Both names are written as pairs (`:5843`/`:5845`, `:5862`/`:5864` for the splice
row; `mqValAcc`/`mqEncWide` for the operand).

The window asserts cannot see it: `MU_ZP_END` (`:200`) pins the top of the region at `+227`, so a
duplicate at `+224` passes every one of them. Nothing fails today because the note splice finishes
in `muNoteInsMode` before any dialog opens, but the map is wrong, and the next edit that keeps an
operand live across a splice — or a splice row across a dialog — corrupts the other silently. The
fix is one line and a widened assert: move `muInsFrom` to `+227` (its second byte is `+228`, the
last byte the 229-byte window covers), set `MU_ZP_END = MU_ZP_BASE+229`, and the assert at `:224`
follows.

**Fixed**, and the class is now gated. `muInsFrom` is at `+227`/`+228` and `MU_ZP_END` at `+229`
(the two asserts follow it: `229 <= MU_ZP_LEN` and `>= 229` both hold, with the window's last byte
`$EF`). New gate `tools/check-zp-map.py` parses every `MU_ZP_BASE+N` name with the span from its
`// N bytes` comment and fails on an overlap; it runs in the task build and in CI. Verified: the
built image stores `muInsFrom` at `$EE`/`$EF`, disjoint from `muObjValHi` `$EB`, `muObjMaxHi` `$EC`
and `muCmdValHi` `$ED`; the map reads `196 names, top byte +228, OK`; and the gate **provably
fails** on the old map — re-pointing the label back to `+224` makes it report
`muInsFrom overlaps muObjValHi (MU_ZP_BASE+224, byte 224)` and exit 1.

### F16 — `rfSidBusy` clobbers the RAM page count the loader is about to read (Low, latent)

`rfLoad` parks the `run <name> <pages>` argument in `kArg+2` across `kDisableCursor` precisely so
that `kDynLoad` receives it — and `dynLoad`'s first act is `lda kArg+2; sta dlRamPages`
(`dynload.inc:485`). `rfSidBusy` (`shell.inc:1516`) is called **between** that restore and the
load, and on its header-read path it does `lda #0 / sta kArg+2 / sta kArg+3` (`:1521`) without
restoring. With a tune playing, a `KF_MALLOC_RAM` task would therefore be loaded with 0 pages and
refused with error 9.

Latent only because no task header sets `KF_MALLOC_RAM` today (I checked all 15 dynamic task
headers; `basic` is `KF_RELOCATABLE` alone), so `dlRamPages` is never consulted. It is still a
regression in the contract `rfLoad` is written to keep, and it would bite exactly when the chip is
busy — the case nobody suspects. Park the count in an instance slot (as `rfSidBusy` already does
with `TSKNAME`) or save/restore `kArg+2/+3` around the FS calls.

**Fixed** the second way: `rfSidBusy` pushes `kArg+2`/`kArg+3` before its first FS call and pops
them at its single exit, and its header now states the contract (`kPtr` and `kArg+2/+3` come back
as they arrived). `pha`/`pla` do not touch the carry flag, so the routine's `C` return is unchanged.
Verified on the built shell: the push sits at `$5E52` — after the two `(dataPtr),y` stores and
before the `libStartPage` test — and the matching pull at `$5ECB`, immediately before the `rts` at
the end of the routine, so every path is balanced.

### F10 — Window transfers ignore their failure flag (Low)

`muWinFill` (`:723`) and `muWinFlush` (`:782`) call `kReuReadAt`/`kReuWriteAt` and drop the C
flag. A refused read leaves the previous page in the window and the band draws another page's
bytes; a refused write loses an edit in the scratch copy (the `.mus` is not written until a save,
so the damage is contained, but `? saved` would still be reported later for a file that no longer
holds the edit).

The page-move routines do check (`muGapPage`: `bcs` on the load, `muGapPage` again on the write),
so this is an inconsistency rather than a blind spot. `? write failed` exists (`M_NO_WRITE`, used
at the save path) — route these two here as well.

### F11 — The tune-size cap is unstated (Note)

`MU_SCR_PAGES = 255` (`:384`): `<base>.wrk` is 65,280 bytes for every session, whatever the tune's
size, and `muImgRoom` (`:7451`) refuses to grow the image past it with `? no space`. The largest
bundled tune is `katmand` at 4,584 bytes, so the cap is 14× the real material — generous, but it is
a hard limit on what a user can build, and `musedit.md` never mentions it. One sentence in
*Limits* ("a tune can grow to about 65 KB, which is far more than the player's own files use")
closes it. The fixed size is a deliberate trade (no recreate-and-copy on every insert), and the
guide already explains that the space comes back on `compact`.

---

## 2. Stale code

| what | where | verdict |
|---|---|---|
| `muClkNotR` + `M_NOT_YET` | `musedit.inc:8654` | unreachable — **deleted** (**F2**) |
| `mqRefuseCmd` + `M_NOT_YET` | `musedit.inc:6263` | unreachable — **deleted**, with the shape test that reached it (**F2**) |
| `M_NOT_TUNE` | `musedit-msgs.txt` | unused — **kept on purpose**: it is the message F9's even-length check needs |
| `MU_BAR_INS` comment | `musedit.inc:403` | wrong — fix (**F3**) |
| `tools/temp/symout/assemble-tasks.vs` | committed artifact | **removed**, and `/tools/temp/` ignored (**F6**) |
| `MS#` numbers | `:373`, `:6770`, `musedit.md:551`/`:616` | three-way disagreement (**F1**) |
| generated-file table | `development.md:637` | two generators missing (**F7**) |
| the plan's tune references | `docs/plans/musedit-plan.md` | already pruned to the nine shipping tunes |

Nothing else: `tools/vice-step.ps1`, `vice-test.ps1` and `drive-string-tests.ps1` were deleted on
this branch and nothing in the tree refers to them; the message table has no other orphans; the
`? no pointer art` path is live and documented.

### Rule 14 residuals (from the project's own `scan-history.py`)

The advisory scanner reports eight lines; four are real and four are ordinary English:

| where | line | verdict |
|---|---|---|
| `tools/check-mus-commands.py:8,13,164,223` | "bug #192" ×4 | **violation** — Rule 14 keeps bug numbers out of `tools/`; reword to describe the *behaviour* (the wide-phrase encoding) instead of the bug |
| `src/tasks/dynamic/musedit.inc:9219` | "A screen row therefore **no longer** names a time row" | **violation** — say it as it is: "A screen row is not a time row: MU_LNTBLO/HI record which…" |
| `tools/gen-musedit-msgs.py:20` | "these strings **used to be**, so the screen shows what it always showed" | **violation** — describe the slot layout, not its history |
| `tools/check-pool-budget.ps1:101` | "length is **no longer** a pool question" | borderline — "length is not a pool question" |
| `src/tasks/dynamic/musedit.inc:7633` | "is only moved from the gap's low byte onwards" | false positive ("moved from" is ordinary English) |

That the scan is advisory is why these survived; the four in `check-mus-commands.py` are the ones
a reader would trip over, because they send a maintainer into `docs/bugs.md` for a mechanism that
is right there in the file.

---

## 3. Overengineering

Very little, given the size. In descending order of "would I cut this":

1. **The `mqShape == 9` scaffolding** (**F2**): a whole refusal path, a message and a documented
   "shape 9" convention that no table entry uses. It was clearly written for a future command that
   never arrived.
2. **`muClkNotR`** (**F2**): a fallback message for a hit map that has no free codes.
3. **Two one-line generators.** `gen-musedit-tables.py` emits 32 bytes and
   `gen-musedit-glyphs.py` emits a 15-byte piano table plus two 40-byte key maps. A generator each
   is heavy for that, but both emit *committed* files that must agree with something else
   (`sidplay.inc`, and the piano's own geometry), and the `-Check` steps are what make the
   agreement enforceable. Keep them; document them (**F7**).
4. **`docs/plans/musedit-plan.md`** is 1,652 lines. That is a process choice, not a code one, and
   it is the record this project works from — but it is now longer than some of the code it
   describes, and its verification notes duplicate what `bugs.md` and the gates already say.

What is *not* overengineering, in case it looks it: the three image windows (the tune is never in
RAM, so edits are page-granular and the pool holds 70 code pages instead of a 65 KB tune), the
per-voice page cache, the `muHist` ring (32 slots × 3 voices — it removes a second walk of the
tune per frame), the six-page `kMalloc` block shared with the player child, and the `MU_SPIN` /
call-depth guards in the walk (they model the player's own limits). Each of those is load-bearing.

---

## 4. Are we doing everything right?

**Yes, in the ways that matter.** The pattern this branch follows — every invariant either an
`.assert`, a gate, or a host model compared against the machine — is why a 10,000-line 6502 program
reviewed cold shows so few defects:

- **Asserts pin the ABI.** 80 task asserts (ZP window under `$F0` and clear of `sid.lib`'s
  `$02-$0A`, instance buffer covered, lane columns, bar map against the bar text, undo file whole
  pages), plus the kernel's new `$54A2`/`$54AA` asserts.
- **Two independent models.** `mus-sim.py` is a host re-implementation of the *player*, and
  `check-mus-commands.py` compares the editor's encoder tables against `gen-mus.py`'s decoder over
  30,463 pairs. That is the strongest form of "does the grid agree with the music" available
  without hardware.
- **The kernel changes are minimal and defensive.** `tickCount` is one `inc` in the Timer A path
  at a published, asserted address; `sid.asm` now takes *only* an exact 1 as "stream" (a wrong
  mode used to DMA from a garbage FS offset — the comment says so); the shell refuses to start a
  `sid.lib` task while another task holds the library, and treats a spawn that came back
  non-alive as a failure instead of "the command did nothing".
- **Clobber discipline holds.** No `kArg+8`, no `$F0-$FF`, no `(zp),x` (gated by
  `check-zp-indirect.ps1`), and X is parked wherever a call would eat it.

**Where the process leaks:**

- **CI does not run the editor's own gates** (**F5**) — the biggest gap, and cheap to close.
- **The walk gate is shallow** (**F8**).
- **The guide is more complete than its troubleshooting table** (**F4**) and silent about the size
  cap (**F11**).
- **Three documents describe `MS#` differently** (**F1**) — a sign that the number was decided in
  code and never reconciled with the doc.

---

## 5. Is `musedit.md` up to date and consistent?

Checked claim by claim against the code. It is **substantially accurate** — every behaviour I
traced is implemented as described, including the awkward ones (the three-run split, the `?` mark
for an off-grid onset, the phrase-body refusals, the tie's "newly written only" rule, the
`OVR`/`INS` mode button, the dots' colours and the pitch that follows the declared machine speed).
The discrepancies are:

| # | claim | reality |
|---|---|---|
| 1 | `musedit.md:551` "`MS#` up to 767" vs `:616` "0-999" | the encoder capped at **767**; the format's limit is 999 (**F1**) — fixed: the encoder now carries 999 and both places say so |
| 2 | the troubleshooting table (`:700-717`) | 11 of 23 messages documented, counting the body text; twelve reachable ones were not (**F4**) — fixed: all 23 have a row |
| 3 | `:562` "**Every** command the picker offers is writable" | true (no shape 9), and `JIF` is correctly absent from the picker — this claim is *right* and worth keeping |
| 4 | the tune-size cap | never mentioned (**F11**) |
| 5 | `:39` "`? sid.lib in use` … printed by the shell before `musedit` starts" | correct, and the branch added exactly that (`shell.inc:1516`, `msgDynSidBusy`) |
| 6 | `:214` "`JIF` … no bundled tune uses it" | verified: zero `JIF` pairs in all nine tunes |
| 7 | `:88` dots "red / yellow / cyan" | `muVoiceCol: .byte 2, 7, 3` ✓ |
| 8 | `:106` "quits, and answers `? unsaved` first" | `M_UNSAVED` is `"? unsaved - q again"`; the wording now gives the whole text |
| 9 | `:552` "a value the command cannot hold is clamped to its maximum … what the prompt shows is always what gets written" | now true: the clamp's high half is re-derived on every prompt redraw (**F14**, fixed) |

Two structural notes:

- The guide's "Status: this guide describes the first release" banner is good practice; the
  *Limits* section is honest and matches the code (no per-frame instrument tables, no seek, no
  delete row/bar, no description editing, no `.wds`).
- The `?` markers in the text are the grid's own onset marker and the leading character of every
  diagnostic. That reads well, but it means `?` appears 31 times and a reader cannot grep for
  "error message" reliably — the troubleshooting table is the right place to anchor that.

---

## 6. Does it support the `.mus` format fully?

Almost. Measured against `reference/sidplayer-mus-file-format.md`:

| format element | editor | note |
|---|---|---|
| header: load address | preserved byte for byte | not editable (players ignore it) |
| header: three voice lengths | derived and maintained on every edit | |
| description + bytes after its terminator | preserved exactly | not editable (deliberate) |
| note pairs: 7 duration classes + 2 utility forms | ✓ | `$04`/`$24` appear 39 times in `fsonata`, `seffsng` and `tambrn` and are drawn as "no number + dot mark", which is the truth |
| dot / double-dot / triplet bits | ✓ | from the duration byte, not a side table |
| tie bit | ✓ | armable, and never on a rest |
| accidental / EORed octave | ✓ | including the B♯→next-C roll the player applies |
| `$00` "absolute pitch" pairs | treated as a 64th note | **as the player does**; the reference itself says the option byte's meaning is undocumented. The guide does not mention this |
| 48 of 49 command mnemonics | ✓ drawn, authored, clamped | verified against `gen-mus.py` over 30,463 pairs |
| `JIF` | not drawn, not authorable, preserved in the file | deliberate; see below |
| `MS#` 768-1023 | drawn correctly, clamped on edit | **F1** |
| `FLG` / `.wds` interlock | `FLG` drawn and authored | the `.wds` side has no counterpart (§ Limits) |
| `AUX`, `3-O`, `HLT`, `HED`/`TAL`, `DEF`/`CALL`/`END` | ✓ | phrases play inline, matching the player |

**What is missing, and how to add it.**

1. **`JIF`** (the only command with no line). Its value is `option*4 + bits 7-6 of the command
   byte`, a 10-bit signed number. To draw it: a 50th mnemonic id at `muDecTab[7]` (command byte
   `$38-$3F` currently maps to `0`), decoding `(opt << 2) | (cmd >> 6)` with a 10-bit sign
   extension, printed through `muDec16`. To author it: one new `mqShape` value whose encoder splits
   the value the other way (`opt = value >> 2`, cmd bits 7-6 = `value & 3`), roughly the shape of
   the existing 5-7 encoders. ~40 bytes of code, one table entry, one doc row. **Worth doing only
   if a tune ever needs it** — the player ignores the command in GordonOS, so a value written here
   has no effect on playback.
2. **`MS#` to 1023** (**F1**) — one byte of table plus a doc fix.
3. **Description editing** — the image already supports inserting and deleting bytes anywhere
   (`muInsGap`/`muDelGap`), so this is UI only: a text cursor over the NUL-terminated tail, drawn
   in row 0 (which the head already uses as a status line) while a description edit is open, and
   `INS`/`DEL` mapped to characters rather than events. ~200 bytes of code and no new data.
4. **Play from the cursor** — the format has no seek, so this needs either a player that can be
   started mid-tune or a dry-run. The cheapest shape: the editor already walks the tune with a
   simulator that tracks each voice's stream pointer and tick; for a target row it can hand the
   player a start tick plus the three offsets where each voice stands, which is one new `kSidPlay`
   mode (a 9-byte config it already builds) plus a start-tick field the player adds to
   `WorkSongTicks` instead of zeroing it. That also solves the "score rewinds on stop" annoyance.
5. **Delete row / delete bar** — the exact inverse of `muInsertTime`: `muDelGap` per voice with the
   same phrase-body guard `muPhBlock` already applies. Small, and the only reason it is absent is
   that it was never asked for.
6. **Block copy/paste** — a clipboard block in the REU (a second file, or the undo file's space),
   with the range-diff undo it already has. Medium.

The first three are hours each; the fourth is the only real design change.

---

## 7. Is pool space used efficiently?

**The numbers** (`check-pool-budget.ps1`, `tools/verify-map.ps1`):

| tenant | pages | notes |
|---|---|---|
| `musedit` code | 70 | 17,850 bytes, 1,805 fixups; ~256 B of it is the resident undo window (`muUndoBuf`) — a buffer living in the image |
| `sid.lib` | 15 | declared lazy, reserved at spawn, loaded on the first PLAY |
| `filesys.lib` | 10 | needed for `<base>.wrk` and `musedit.und`; touched at startup |
| mouse | 2 | |
| sprite | 1 | the pointer and the three dots |
| `kMalloc` block | 6 | 1,536 B: three 256 B image windows for the editor, three for the player child |
| **total** | **104 of 109** | margin 5, and `basic` fits the pool **exactly** — the gate warns on every build |

Kernel side: 116 bytes free across 21 pockets, and this branch spent carefully — `tickCount` went
into a 1-byte pocket that `verify-map.ps1` now pins, and the core grew only through re-pointed
jumps.

**Assessment.**

- **Code density is good.** 70 pages for a full tracker editor with mouse, piano, undo, command
  authoring, playback and a phrase-aware walk is tight. The ZP map (229 bytes of the 234 available)
  is why: the hot state is `zp,X` and one byte per reference.
- **The largest structural cost is `sid.lib`'s 15 pages held for the whole session** (**F12**) — 14%
  of the pool for a feature used only while PLAY is on. It cannot be released: `kLibHold` is
  one-way, and a released lazy library reloads through its cold stub uncredited, leaking its pages
  until reboot. The clean alternative is a spawn-on-demand player task that owns `sid.lib` (the
  shell's new `? sid.lib in use` arbitration already provides the lock), letting the editor drop
  the declaration entirely and freeing 15 pages for an editor that is idle. That is a real change
  to both tasks; not worth it today, but it is the one place a big win is available.
- **The 6-page malloc block is minimal:** the editor cannot share its windows with the child
  because it draws while the child streams, and both read the same file.
- **The instance buffer is 245 of 255 bytes** — the two 96-byte log arrays plus the scalars. Fine,
  and asserted.
- **`<base>.wrk` costs 65 KB of *REU filesystem*, not pool** (**F11**), and the space comes back on
  `compact`. Worth stating in the guide; not worth changing.

No waste worth chasing at margin 5, other than the 15 pages above.

---

## 8. Could anything corrupt memory?

I traced every path where a length, offset or index comes from tune data or user input. The design
is defensive, and the two places it was not have already been found and fixed during development
(the band repaint overrun, and the split planner reading past an event). What remains:

| path | what could go wrong | verdict |
|---|---|---|
| zero page | a label past `$F0` overwrites the IRQ's bytes; a label in `$02-$0A` fights `sid.lib`; two labels on one byte silently share storage | **safe, and gated** — `MU_ZP_LEN` asserted under `$F0`, the base clear of `$02-$0A`, indirect operands gated by `check-zp-indirect.ps1`, and duplicate names now gated by `check-zp-map.py` (which found the one that existed, **F15**) |
| instance buffer | a computed offset writes a neighbouring field | **safe** — every access is `ldy #MU_x`; the only computed index is the per-row command log, clamped to `MU_LOG_SLOTS` (`muLnLog:2412-2445`) |
| image windows | a read past a window | **safe by construction** — the address is `winHi : offsetLo`, always inside the 256-byte window. The *correctness* assumption is even voice lengths (**F9**) |
| gap move (`muGapMove`) | a move past the end of the scratch | **safe** — `muImgRoom` refuses growth past `MU_SCR_PAGES`, direction is chosen so no source byte is overwritten, and the file size never changes mid-move |
| undo stack | a record past the 2 KB file | **safe** — records are ≤ 516 bytes (`6+255+255`), and the room check runs before the write and resets to 0 when it fails |
| tune name / FS names | a long name overruns the 9- and 12-byte buffers | **safe** — 7 characters enforced before the copy (`muParseName:536`) |
| band drawing | a row/detail outside rows 2-20 | **safe** — every repaint call clamps to `MU_GRID_TOP`/`MU_GRID_BOT`; the one that did not is fixed |
| walk of a malformed file | an endless command run, or unbounded phrase nesting | **safe** — `MU_SPIN` bounds a command run and `muCmCall` models the player's own call-stack limit (`MU_ERR_STK`) |
| `kArg` | state left in the block across a call | **safe** — no `kArg+8`, and the block is used per call only |
| kernel calls | X/Y/C clobbered across a call | **safe where I checked**; the routines park X in the code image or on the stack, and `muPutC` re-reads the column after every blit |
| REU transfers | a failed transfer silently ignored | **not guarded** — **F10**, wrong data rather than corruption |
| the player child | a thread child outliving its parent's pages | **safe** — the child is reaped in `muMusicBye` before `kFreeScreen`, with a `SYS_KILL` fallback, and `childGate` covers the exit path |

The two things I would add anyway, because they are cheap and they convert "wrong data" into a
diagnosis: the even-length check at open (**F9**) and the transfer flags (**F10**).

---

## 9. Other observations

- **The playhead is the strongest piece of design here.** Publishing each voice's next-command
  *offset* (not an address) from inside the player, plus the played-jiffy count, means the grid's
  cursor is derived from the player's own state and cannot disagree with what is sounding — and it
  works identically for a RAM image and a paged one. The comment in `sid.asm` explaining *why* an
  address would be meaningless is the kind of thing that saves a future reader a day.
- **`sid.asm`'s strict-1 mode check is a genuine safety fix**, not a tidy-up: the old `and #1`
  made mode 3 stream from a garbage config. Worth keeping the paragraph.
- **The shell's `rfSidBusy` opens the target file twice** (once to sniff the libMask, once to
  load). That is the right trade — a load that fails on the pool never returns to the shell — but
  it is worth a line in `docs/development.md`'s loader section, because "every `run` reads the file
  header twice" is the kind of thing that surprises a later optimisation.
- **`musedit.inc` is 10,454 lines in one file.** The sectioning is excellent (each block has a
  header comment that states its contract), but a 6502 assembler has no scoping, so every label is
  global to the task. It has stayed disciplined; if it grows further, splitting the walk, the
  drawer and the command panel into `.import source` files with a unique prefix each (the pattern
  `musedit-glyphs.inc` already uses) would keep it navigable — remembering that every `.inc` must
  be listed in `project-config.json`'s `sources` for auto-rebuild.
- **`tools/mus-sim.py`'s `pitch_name` now models the editor's rolled enharmonics** (this branch
  fixed both sides). Worth a `-Check`-style assertion that the host model and `muPitchSplit` agree
  over all 224 options — the py65 harness used for the B♯ fix is not in the tree, and that check is
  reproducible.
- **Two behaviours are described only in code comments** and deserve a guide line each: that a
  utility-duration note (`$04`/`$24`) takes its length from `UTL`/`UTV`, and that a `.mus` whose
  voice length is odd is not a `.mus` this editor can trust.

---

## Appendix — what was run

| command | result |
|---|---|
| `python tools/check-mus-commands.py` | 30,695 pairs checked against `gen-mus.py`, 49 mnemonics — OK (the `MS#` sweep runs to 999 now, not 767) |
| `python tools/check-mus-timing.py` | 32×7 chart with 0 disagreements, tables byte-identical to `sidplay.inc`, 8,192-case sweep, all 256 tempos have a unit — OK |
| `python tools/check-musedit-walk.py` | `immig` matches the editor over 19 lines — OK |
| `powershell tools/check-zp-indirect.ps1` | 139 source files, every indirect operand is zero page — OK |
| `powershell tools/verify-map.ps1` | all checks passed; 116 bytes free across 21 pockets; `tickCount` pocket pinned |
| `powershell tools/build-tasks.ps1` | exit 0; 83 asserts, 0 failed; `musedit` 17,850 bytes / 70 pages / 1,805 fixups, `shell` 32 pages (margin 77); `msgs` 31 slots (1,984 bytes); pool 104 of 109, margin 5 (`basic` fits exactly — pre-existing) |
| `check-reu-fs.ps1 -Extract msgs` | the shipped `msgs` is 1,984 bytes and byte-identical to `assets/ui/musedit-msgs.bin`; layout OK, 141 entries |
| `python tools/check-zp-map.py` | `musedit.inc  196 names, top byte +228  OK`; and it fails on the pre-fix map (`muInsFrom overlaps muObjValHi … byte 224`, exit 1) |
| py65, `mqClamp` on the built image | 300 against max 255 → **255** with the high half cleared, 300 with a stale `$0F` (the reproduced bug); `P-W`/`DTN` 4095, `POR` 16383, `MS#` 999 (768 → 768, 1000 → 999, 1023 → 999) all correct |
| py65, `muLnDec` on the built image | every `MS#` pair from 0 to 1023 decodes to id 47 with its value intact — so 768-999 in a foreign file still draws as itself |
| `powershell tools/check-reu-current.ps1` | no `.asm`/`.inc` changed since the last task build; all 49 binaries byte-identical in the image |
| `python tools/check-links.py` | 49 files, 0 unresolved — OK |
| `python tools/check-ascii.py` | OK |
| host probe over `music/*.mus` | voice lengths all even, no `JIF`, no `$00` pairs, no `MS#` ≥ 768 (563 `MS#` pairs in six tunes, highest 595), 39 utility-duration pairs |
