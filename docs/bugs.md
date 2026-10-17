# GordonOS — Known Bugs & Code Issues

> Comprehensive bug tracker with root cause analysis and fix suggestions.
> Generated from thorough code review, 2026-07-25. Updated 2026-07-28.

---

## Legend

| Icon | Meaning |
|---|---|
| 🔴 | Confirmed bug — produces incorrect behaviour (Critical/High) |
| 🟡 | Potential bug — triggers under specific conditions (Medium) |
| 🟢 | Low severity or won't fix |
| ⚫ | Code quality — duplicate, dead code, or concision opportunity |

---

# ACTIVE BUGS


---

## Bug #183 — `grpaint`'s flood fill ran on the press that dismissed the UI band, so a click on the palette filled the canvas 🟠 ✅ FIXED (2026-10-11)

**Symptom.** In fill mode, with the palette band up, a left click on the band ran the
flood fill — the press did two things at once: it put the band away *and* filled the
canvas from where the click landed. Reported as *"while the UI is visible I can click in
this area and it assumes that the UI is part of the image. Fill mode should not activate
unless the UI is invisible."*

**Root cause.** `gpDraw` opens with the band guard (`ldy #GP_MENU / bne gpDrawRts`), but
`gpPicker` runs **first in the same frame**, and on a left press that lands off the band's
entries `gpPickTake` falls through to `gpPickAway`, which clears `GP_MENU` before `gpDraw`
reads it. The dismissal press therefore reaches the tools as a canvas press. That is
deliberate for the pencil and the shapes (**#164b**: the press that takes the band down
acts where it lands — a stroke, an anchor or the first click), because those read `GP_PREV`
for a press *edge*. It is wrong for the flood fill: the fill decides its region from the
surface's own pixels, and the band's two cell rows **are canvas rows** — so a press made
while the UI was up treated the click as an image click. The flag the guard reads answers
"is the band up now", and the dismissal has already made that "no" by the time `gpDraw`
sees it.

**Fix.** `GP_MENU0` — one byte of instance data (`GP_DATA_SIZE` 171 -> 172) — is `GP_MENU`
copied at the top of every displayed frame, **before** `gpPicker` can put the band away, so
it is the band's state the press actually arrived in. `gpShapeFlood` refuses when it is set:
the dismissal only dismisses, and the **next** press fills. The pencil and the shapes are
untouched (they read `GP_PREV`, not `GP_MENU0`), and no other path turns the band off
inside a frame — `gpPickOpen` puts it up, and `gpUndoPushBand`/`gpPickTakeSave` put it back
up, so `gpDraw` returns on `GP_MENU` either way. `grpaint` 7729 -> 7743 bytes, **71 pool
pages (unchanged)**, 64 asserts.

**Transferable:** a guard that must tell *"the UI was up when the press landed"* from
*"the UI is up now"* has to read the frame's **starting** state. A flag that means "is up
now" is cleared by the very dismissal the guard exists to detect, and the clearing routine
runs earlier in the frame than the guard.

---

## Bug #182 — `allocFromPool` Clobbers X, so Every `kMalloc` Recorded a Wrong Page Count: pages leaked, or another owner's pages freed ✅ FIXED (2026-10-06)

**Severity:** Critical — silent pool corruption. The recorded count is `(startPage + pages - 1) & 7`, an arbitrary 0-7, so a block is freed too short (pages stranded in the bitmap with no owner) or too long (`freeFromPool` clears the pages *above* the block, marking another task's or library's pages free while they are in use). The second direction is the fatal one: the next allocation hands those pages to a new owner and the loader writes code over a live task's code or a library's pages, so the CPU jumps into overwritten memory — a JAM, and VICE falls back to the KERNAL's BASIC READY prompt.

**Reported as:** "run `sidplay` to play a tune, run `edit`, quit it, wait for sidplay to finish, then run `sidplay` again — the system crashes out of GordonOS into the KERNAL BASIC ROM with the READY prompt showing." Determined by two save-states (one before, one after) rather than by reading.

### Root cause: a documented clobber list that was not true

`allocFromPool`'s header promised `Clobbers: A, Y, kArg+4..kArg+6`, i.e. **X preserved**, and its callers believed it. But the routine opens with `stx kArg+4` / `txa` (its own X write) and then runs `poolBitCheck`/`poolBitSet` per candidate page and per page marked — and **all three bitmap helpers end in `tax`**, which their own headers say ("Clobbers: A, X, Y"). The last `poolBitSet` of a successful allocation is the one that sticks, and its A is `startPage + pages - 1`:

```
mallocPages = (startPage + pages - 1) & 7
```

`kMallocImpl`'s `kmSlotFound` then stored that value with `txa`, commented "pages -- from X, not kArg: kmEvictAlloc's `dlEvict` clobbers `kArg..+7` (X is set)". The comment was right about `kArg` and wrong about X: the author reached for the one register the allocator had promised, and the allocator does not keep it. `kFreeImpl` and `freeMallocBlocks` then free exactly `mallocPages`.

### Evidence (the second save-state)

| | before | after |
|---|---|---|
| pool bitmap | 31 used, 78 free, **one run (19,96)** | 33 used, 76 free, **runs (19,56) + (59,96)** |
| `dynTaskCount` / live tasks | 1 (shell, 19 pages at pg 0) | 1 (same) |
| `dataHeap` | `$1DC3` | `$1E93` (+208 = `edit`'s instance buffer) |
| slot 2 record | size 26 (dead `banner`) | size 208, dataPtr `$1DC3` (`edit`) |
| `mallocStartPage[0]` | 25 | **55**, pages 0 |

`edit`'s 36-page code block landed at 19-54, so its 4-page document chunk was at **55-58** — and `58 & 7 = 2`, so the chunk was *recorded* as 2 pages. `freeFromPool(55, 2)` cleared 56 then 55, leaving **57-58 stranded** and splitting the free run exactly at 56/59 — which is what the bitmap shows. No table owned those two pages, which is why `pool` (whose rows come from the tables) reported a clean-looking state.

Both snapshots are read by **`tools/vsf-pool-audit.py`**, which parses a VICE `.vsf`, locates C64 RAM inside the `C64MEM` module (validated against the built PRG at `$0801`), and cross-checks the pool bitmap against the owner tables — dynamic tasks and their program RAM, libraries, malloc blocks — reporting a live owner's page the bitmap calls free as **STOLEN** and a used page no live owner claims as **STRANDED**. This case reads **STRANDED pages 57-58** with free runs `19-56` + `59-96`, and `--diff` prints **`owner table changes: none`** between the two snapshots, which is the whole point: the tables cannot see it.

**This also explains the pre-existing mystery in Bug #78** ("~26+ pages held by something no live task owned", after `edit` and a `compact`): the same leak, from a different task's `kMalloc`.

### Why the trigger is `edit`

The recorded count is arbitrary for *every* `kMalloc`, so the exposure is proportional to how much a task allocates. `edit` allocates a 4-page chunk per 24 lines and grows more as the document grows, so it leaks and steals the most; `.com` command tasks allocate nothing and are unaffected, and `run sidplay` twice with no editor survives because its 4-page tune block's theft or leak lands in slack. Once `edit` has run, the free space is short and split — and both reported symptoms follow from that: `run sidplay`'s load corrupts and dies (instant, in the load/spawn path, before the new task's code runs), and `run edit` again is *silently* refused because the shell is sacrificed (`dlSelfEvictOk` → `dlSelfEvictedExit`), the one path that can swallow a command with no message at all.

### Fix

The count has exactly one valid home, and `allocFromPool` already puts it there: `kArg+4`, stored at entry and re-installed by every call — including the `kmEvictAlloc` retry — so it is valid after an eviction, where `kArg` is not.

```asm
    lda kArg+4                   ; kMallocImpl, kmSlotFound -- was `txa`
    sta mallocPages,y
```

2 bytes, in the code chain's own growth tail (`malloc.inc` is inside `$ED5D-$FC3E`, where nothing is pinned by an exact-address assert). Also corrected, all comment-only: the same false `Clobbers:` line in **`freeFromPool`**, **`markPagesUsed`** and **`dlAllocCode`** (+ the `dlRegKill` inline comment that claimed `freeFromPool` preserves X); **`setTextColor`** / **`setCursorColor`** ("Clobbers: A" — they `ldx curTask`); **`spriteCollide`** ("Clobbers: Y" — X is an *output* mask). `kmEvictAlloc`'s header, which justified the wrong source, now says where the count really lives, and the defensive table-full path in `kMallocImpl` reads `kArg+4` too (same size, `ldy kArg` → `ldy kArg+4`).

**Cost:** free total 127 → **125 B across 22 pockets**; the code chain's growth tail `$FC3B-$FC3E` → `$FC3D-$FC3E` (2 B). 232 asserts green; `verify-map.ps1` re-pinned (its chain-tail range and total). Byte-verified in the PRG: exactly one `AD 04 40 / 99 25 CB` (`lda kArg+4 / sta mallocPages,y`) at `$F721`, and no `8A / 99 25 CB` (`txa / sta mallocPages,y`) anywhere. No task or library binary changed, so no REU rebuild is needed.

### The sweep it prompted

A wrong clobber list is worse than none — it is an attractive nuisance — so every X-preservation claim in the kernel was audited against the emitted code and the call graph (claims from the headers, checked with `tools/dis6502.py`'s opcode table, with `stx addr … ldx addr` and `txa/pha … pla/tax` windows modelled). **49 claims: 7 were false (the list above), 42 held** — including the ones a crude scan flags, which all bracket their calls: `surfSlotToReu`, `shareScreen`, `takeScreenOwnership`, `wrapClamp`, `skOwnSurf` (`txa/pha … pla/tax` around `kMul16` / `surfFreeSlot` / `surfDisplayed`) and `blitChar`/`blitCharSlot`/`blitGlyphSlot`/`blitGlyphFb` (`stx kArg+2` … `ldx kArg+2`, or the shared `blitFbCommon` tail). Only `allocFromPool` was also a *code* bug; the other six were documentation.

That pass left a keeper, **`tools/audit-clobbers.py`**: it re-runs the check from the built image with a path-sensitive DFS over the control-flow graph (an X writer makes the path dirty; `stx addr … ldx addr` and `txa/pha … pla/tax` clean it again; a `jsr` to an X-clobbering routine dirties it, resolved recursively through the `$E000` jump table; an exit reached dirty is a suspect). On the corrected tree it audits **24 header claims and reports none suspect**, and `--selftest` re-checks its own analysis against the 17 routines settled by hand in this pass — the seven above must still classify as clobbering, and `surfSlotToReu`, `shareScreen`, `takeScreenOwnership`, `wrapClamp`, `skOwnSurf`, `blitChar`/`blitCharSlot`/`blitGlyphSlot`/`blitGlyphFb` and `dynRegister` as preserving — so a toolchain change cannot quietly turn the screener into a rubber stamp. Two headers were also given the X contract they really have: `shareScreen` and `takeScreenOwnership`.

### Transferable

* **A clobber list is a contract, and the outermost one is the only one a caller reads.** A routine that calls a `tax`ing primitive has *already* clobbered X, so its own header must say so (or it must restore X). Same class as Bug #16 (`kPrintAt` documented as preserving X when it does not).
* **Keep a value that must cross a call in memory, not in a register.** `kArg+4` is the allocator's own echo of its input for exactly this reason; "it worked when the pool had room" is not a contract.
* **A count that is stored and later used to free is an invariant worth a byte check:** `AD 04 40` is the difference between a healthy pool and a JAM ten minutes later, and nothing in the build or the tests would have said a word.

### Hardening declined

The other direction — making the contract *true* by restoring X inside `allocFromPool` — was **declined**. `allocFromPool` sits inside the sealed loader band (`$4310-$529D`), where a size change is an ABI change that only a relocation can pay for, and the 9 bytes of loader headroom after `sprInstall` (`$5337-$533F`) are already earmarked for the mus-editor plan. The header now states the truth instead, and callers keep the count in `kArg+4`, which the allocator does guarantee. Note what this means for the audit tool: with no claim left to test, `allocFromPool` is no longer audited at all — the header and the `kArg+4` rule protect it, not the screener.

**Retest:** boot → `run edit` → press `q` → `pool` must read **31 used, 78 free, one run (19,96)** — then the original repro: `run sidplay <tune>`, `run edit`, quit, let the tune finish, `run sidplay <tune>` again.

---

## Bug #118 — `mousetest` Read OSC3/ENV3 Instead of the Paddles: the Mouse Looked Dead While the Buttons Worked ✅ FIXED (2026-09-27)

**Status:** ✅ FIXED and **verified on the machine** (VICE, 1351 in control port 2): with the registers corrected the readout tracks the mouse. Before the fix it printed constant zeros however the mouse moved.

**Symptom.** `mtest` ran, the field table was laid out correctly, and the buttons
decoded exactly as measured (left `101111`, right `111110`) — but the `raw`, `dec`,
`delta` and `acc` fields never changed, and the pointer was captured (the host
cursor was hidden inside the emulation window), so the position was genuinely
being fed. Live buttons with a dead position is the signature this bug hides
behind: the two travel by different paths, so the buttons working says nothing
about the read.

**Root cause.** The reader sampled `$D41B`/`$D41C` as "the x and y axis of control
port 2". Those are **OSC3 and ENV3** — the voice-3 read addresses `sprdemo.inc`
and `sidplay.inc` use. **The SID has one pair of paddle registers, `$D419` (POT X)
and `$D41A` (POT Y), and the PRA `$DC00` select decides which *control port* they
carry** — there is no per-port pot register (`set_joyport_pot_mask((b >> 6) & 3)`;
VICE returns the pots only `if (chipno == 0 && (addr == 0x19 || addr == 0x1a))`).
With voice 3 idle, OSC3 and ENV3 read **0 forever**, so every value printed zero
and moved never — while `$DC00`'s low six lines, which are not SID registers at
all, kept answering perfectly.

The wrong address came from a table row written into `docs/lib-mouse.md` and the
plan during step 3.1's research ("port 1 `$D419`/`$D41A`, port 2 `$D41B`/`$D41C`"),
which was then propagated into the task, the plan's decision list and
`src/libs/mouse.asm`. `$D41B`/`$D41C` sit immediately after the paddle registers,
which is what makes the mistake so easy to make and so hard to see: the code reads
a real SID register and gets a plausible, stable byte.

**Fix.** `src/tasks/cmd/mousetest.inc` reads `$D419`/`$D41A`; the register labels,
the header comment and every doc that carried the bogus row were corrected in the
same commit. **`docs/lib-mouse.md` now states the trap explicitly**, because the
next reader of this protocol is one register away from repeating it.

**Related, and now settled from VICE's source rather than by argument:** the hold
after the select must exceed **512 cycles**. VICE resamples the pots every 512
cycles (`pot_cycle`, `(maincpu_clk ^ pot_cycle) & ~511`) and returns an unsettled
value within 511 cycles of a select change (`port_changed_diff_clk > 511`). The
select write alone is sufficient — no POT write is needed to latch.

---

## Bug #111 — `run basic` Never Starts After `edit` Has Run (FIXED 2026-09-16)

**Status:** ✅ FIXED and **verified in VICE** (2026-09-16): `run edit` then `run basic` prints
`? no space in pool`, and a fresh boot's `run basic` still starts BASIC. **Pre-existing** — the kernel from before the
`kPrintCharAt` routing change fails identically, so it was not a regression from that work.

**Root cause — two defects on one path:**

1. **The fit test was a page COUNT, but a load needs a contiguous RUN.** `dlPreEvict`
   stopped evicting as soon as `free >= dlPreNeed`, and `dlSelfEvictOk` allowed sacrificing
   the caller on the same count. After `edit` (36-page code block + its 4-page document
   chunk) the free pages are split into runs smaller than BASIC's 99-page footprint: the
   count said yes, the placement said no, the loop stopped early, the allocation then failed
   on placement — by which time the caller's code pages were already freed.
2. **A failure after the caller was evicted had nowhere to return to.** Only the *success*
   path checked `dlEvictedSelf`; the error paths still ended at `dlRts: sec; cli; rts`,
   returning into pages the load had freed. `dlPreEvicted` never returns to the caller by
   design (it restores the parked tasks, so the caller cold-restarts) and the error code in
   `kArg` dies with `dlRestore` — so a failed `run basic` printed **nothing at all**.

That second defect is why every diagnostic came back blank. The whole symptom set is one
failed load that sacrificed the caller and then reported nothing to anyone: no message (a
count-based error path never ran, and the two that did return could not reach the shell),
`ps` empty, `pool` clean, and `run nosuchfile` still printing because it never sacrifices
the caller.

**Fix:** the pre-evict loop must satisfy **both** tests before it stops evicting — the page count
(`free >= dlPreNeed`, unchanged) **and** a contiguous run of that size (`dlProbeFit`, new) — and
`dlSelfEvictOk` refuses to sacrifice the caller unless the space left can host the load once the
caller's pages cover the shortfall. With the sacrifice refused, the failure reaches a **live**
caller: `dlPreFail` returns error 4, `dlRts` sees `dlEvictedSelf == 0` and returns normally, and
the shell prints its own `? no space in pool`.

**What this does and does not change in eviction:** the *mechanism* is untouched — `dlEvict`'s
victim choice and order, the swap/restore pair (`dlEvDo`/`dlRestore`), `dlCallerPages`'s
accounting, and `dlFreePages` are all as they were, and the count test is still the loop's
primary test, which is what keeps a fresh boot on exactly its old path (78 free < 99 needed ⇒
evict everything ⇒ sacrifice the caller, which the guard allows there because a 68-page run
fits the 78 free). What *does* change is the **sufficiency test**: the loop now evicts for
placement as well as for count, and the guard can refuse a sacrifice it once allowed. That is
deliberate — it is the only way the shell survives to report the error.

**Cost:** free total 189 -> **173 B across 21 pockets**; `dlProbeFit` is 15 B in the `$FFE9` run
(the gfx scratch's growth room) and the loop's growth consumed the code chain's tail (the chain
now ends flush at `$FC3E`). 212 asserts green, `verify-map.ps1` re-pinned.

**Consequence:** after `edit`, `run basic` reports `? no space in pool` — the loader now refuses
a sacrifice that cannot help, so the shell survives to print it. Making that state actually
*fit* would need a
pool defragmenter, and that is **declined**: relocating live tasks means freezing them, parking
them and cold-restarting them, tasks like `edit` cannot be evicted at all, and every restart
loses that task's data — a tool that costs more than the fragmentation it repairs. `basic`
needs a 99-page contiguous run and after `edit` the free pages are split into smaller runs, so
the honest outcome is the error message. Free total **173 B across
21 pockets**, `dlProbeFit` 15 B in the `$FFE9` run, the code chain ends flush at `$FC3E` (its
growth room is spent), 212
asserts green, `verify-map.ps1` re-pinned.

**Open follow-up:** `dlSpawnErr` (errors 12/13) still ends in its own `rts` rather than
funnelling through `dlRts`, so those two codes do not get the `dlEvictedSelf` check.

**Repro (user-verified):**
1. Boot → `run basic` → works.
2. Boot → `run edit boot.bat` (any file, new or existing) → the editor loads and works.
3. Quit the editor → `run basic` → the shell prints its newline and **BASIC never appears**.
   Repeating `run basic` fails the same way.
4. `run gortris` → quit → `run basic` **works**, so it is `edit` having run specifically,
   not "a big task ran first".

**What is already ruled in:** the shell stays alive and its *internal* commands keep
working, so neither the shell nor the display is lost. Whatever `edit` leaves behind blocks
the next BASIC (or its load) and is never reclaimed.

**Open leads (none confirmed):**
- **The screen mutex (semaphore 0).** A held lock blocks every task that prints, which is
  exactly the "cursor advances, nothing appears" shape — and `seClean` only reclaims a
  lock whose owner is the dying task. But `edit` with a filename never calls `kLockScreen`
  (only the no-argument usage path does, and it pairs its calls), so the holder would have
  to be another task.
- **`KF_MALLOC_RAM` / `kMalloc` bookkeeping.** `edit` is the only task in the repro with a
  *loader-allocated* RAM block; `gortris` (which `kMalloc`s from inside the task) is the
  control that works.
- **A stale per-slot record** in the Bug #108 shape (a record that points at freed state
  and is reused by the next spawn into that slot).

**Discriminators, run 2026-09-16 (user):**
1. `ps` after the failure: **`basic` is not listed** — nothing is alive-but-blocked, so the
   spawn or the task's own startup failed.
2. `pool` after the failure: **clean** — `edit`'s pages and its 4-page chunk are free again
   and the `pg` column matches a fresh boot.
3. `run clock` after the failure: **works** — the screen mutex is free, and small tasks
   spawn and print normally.
4. `run nosuchfile` after the failure: **prints `? no fs or not found`** — the shell's error
   printer works in that state, and *every* load error code has its own message, so a
   failing `run basic` that prints **nothing** means `kDynLoad` returned **success**.

So the lock is not the cause, nothing is leaked, and the load is not failing: what is left
is Bug #106's pattern — from the shell's point of view the load succeeded, and the task then
died at startup without printing.

**Ruled out by the boot sequence (2026-09-16):** `boot.bat` already runs `banner` — a small
`KF_MALLOC_RAM` task — and `run basic` works after it. So "a small program-RAM task before
BASIC" is not the trigger, and the `KF_MALLOC_RAM` path is intact from a fresh boot. The
trigger is state that only `edit` leaves.

⚠️ **`basic` was carrying a stale `KF_TAKES_ARGS` flag** (`basic.asm:35`, removed
2026-09-16): BASIC takes no arguments — it reads the keyboard — and its instance buffer holds
only the 5-byte working-file record plus the line-buffer control bytes, so nothing ever read
an args string. The flag is what made `run basic <n>` *appear* to work as a page-count knob.
It was not inert either: `dlCopyArgs` caps its copy at `data_size - 1` = 11 bytes and writes
it at instance-buffer offset 0, so passing an argument DMA'd the arg string over the first 11
bytes of BASIC's 12-byte instance buffer (the working-file record) at spawn.
Every repro in Bugs #66/#67/#68 written as `run basic 4` / `run basic 10` / `run basic 15` is
therefore **void** and needs re-measuring before it is trusted again. A second build-time
assert (`basic.asm:1369`) had pinned the flags byte to `$09` — it now pins `$01`, so a
re-introduced `KF_TAKES_ARGS` on a task that ignores args fails the build instead of shipping.

**Next discriminator — which half of edit's life?** Run **`run edit`** (no argument: it
prints its usage message on the shell's screen and exits — no screen allocation and no
document chunk, but it does take the screen lock) and then **`run basic`**:

- **fails** ⇒ the trigger is in edit's shared-screen path: its 207 B instance buffer, its
  lazy filesystem use, or the `kLockScreen`/`kUnlockScreen` pair;
- **works** ⇒ the trigger is edit's own-screen path: `kAllocScreen` + `kSwitchView` +
  `kFreeScreen`, or the 4-page document chunk's `kMalloc`.

`dynFree` does clear `dynTaskRamPages`/`dynTaskRamStartPage` per slot (`dynload.inc:332-346`),
so a plain stale-record theory is out. The remaining suspects are the **eviction**
bookkeeping (`dlSwapMalloc`/`dlRestoreMalloc`, `dlSm*`/`dlRm*`) and the kMalloc table's
owner/cap state — precisely the failure kinds Bug #106 deliberately did **not** hook for
evict-and-retry ("only the pool-full site is hooked").

---

## Bug #110 — A Background Task's `kPrintCharAt` Drew on the Displayed Screen 🔴 ✅ FIXED (2026-09-16)

**Severity:** Medium — wrong surface. A task whose surface is not displayed writes its
fixed-cell output onto whatever surface *is* on screen, corrupting another task's display.

⚠️ **Reachability is narrow, and worth stating precisely.** The only task-side callers are
`edit` (status bar / usage message) and BASIC (`PRINTAT` via `gxPatLoop`, plus the line
editor). `edit` guards every draw with `edIsDisplayed` and idles while backgrounded, so it
never draws off-surface — and BASIC at 99 pages cannot co-exist with a pinned display
owner (`gortris` 23 + 13 sid + 4 tune, `godzi` 5 + 13 sid + its kMalloc'd tune), so there
is no task left to hold the display while a BASIC program runs in the background. The live
trigger is therefore the **focus race during a redraw**: `edit`'s status bar is drawn cell
by cell, and an F-key switch part-way through used to send the remaining cells onto the new
owner's screen (now: into edit's own slot). The rest of the value is future-proofing — any
task that prints fixed cells while backgrounded now behaves exactly like `kPrintString`.

**Root cause: a primitive that predates the routing rule.** `printChar` and `printGlyph`
both route on `surfDisplayed` — framebuffer when the task's surface is displayed, the
task's own REU slot when it is not (`blitCharSlot`/`blitGlyphSlot`). `printCharAt` calls
`blitChar` unconditionally: no `surfDisplayed` test, no slot route. `edit` and the BASIC
line editor are unaffected (both share or take the display, so their surface *is* the
displayed one), which is why it went unnoticed.

**Why it sat in the roadmap instead of being fixed.** The fix is ~20 B in the band
`$ECDC-$FC3E` (`gfx.inc` -> `text-blit.inc` -> `screen-mgmt.inc` -> ... -> `lib.inc`), and
that band is packed solid against the `$FC3F` gfx.lib scratch anchor: zero free bytes, and
no free run in the tree is large enough for the 42 B matrix-nibble block that a
deduplication would have to host. So it was a relocation decision, not a byte count.

**Fix — fund the chain by moving code OUT of it.**

1. **`paRoute` (31 B), the surface router, in the pocket under `gfx.inc`'s `$ECDC` anchor**
   (`$ECBD-$ECDB`, which was `scroll-region.inc`'s growth room). It mirrors the char
   shadow, then routes: displayed -> C=1, nothing drawn; background -> glyph and matrix
   nibble go into the task's REU slot via `blitCharSlot`, C=0, cell already written. It
   copies `printChar`'s proven order (shadow mirror *before* the routing) and skips
   `eraseCursor` on the slot path — correct because the blink block only exists on the
   displayed surface, and the whole call runs under `SEI`, so `surfDisplayed`'s answer
   cannot go stale mid-call.
2. **`printCharAt`'s inline shadow-mirror block (8 B) folded into `paRoute`**, paid for by
   `jsr paRoute` + `bcc paDone` (5 B): **-3 B** in the chain.
3. **`blitGlyphFb` (9 B) moved out of the chain** into the `$CDF5` pocket. One caller
   (`printGlyph`), no ABI assert, `blitFbCommon` still its tail by `jmp`: **-9 B**.

Chain net **-12 B**, so the chain's growth room is now `$FC33-$FC3E` (12 B). `printChar`
and the ABI-pinned `$E11A-$EA70` band are untouched, and no label pinned by an
exact-address assert moved. Free total 240 -> **212 B across 21 pockets**.

**Verified:** 212 asserts / 0 failed, three of them new (`paRoute` fits the pocket under
`$ECDC`, `blitGlyphFb` is at `$CDF5`, `blitGlyphFbEnd <= $CE00`); emitted code checked
byte-for-byte — `jsr $ECBD` reaches `paRoute`, its `bcc` lands on `paDone` (`$F027`), its
`bcs` on its own `rts` (`$ECDB`), and `blitGlyphFb` jumps to `blitFbCommon` at its new
`$F089`; `tools/verify-map.ps1` re-pinned (`paRoute` occupies `$ECBD-$ECDB`, `blitGlyphFb`
`$CDF5-$CDFD`, chain room `$FC33-$FC3E`, total 212/21). Runtime check in VICE: pending.

**Lesson:** when a rule is introduced, audit its siblings for the old shape — a fixed-cell
writer with no `surfDisplayed` test looks exactly like its correct siblings at the call
site. And when a fix needs bytes in a packed band, the funding question is "what can
leave the band", not "where can I put bytes".

---

## Bug #109 — A Paste Into Existing Text Wrote Zero-Page Garbage Instead of the Clipboard 🔴 ✅ FIXED (2026-09-16)

**Severity:** High — silent content loss. A yank/cut pasted *between* existing text
overwrites the document with whatever bytes happen to live at a small address, and the
clipboard content is gone; only not saving recovers it. Pasting at the end of the document
works, which is what made it look intermittent.

**Reported (2026-09-16):** mark a region, `STOP+Y` (yank) or `STOP+X` (cut). Paste it into
empty space at the bottom — correct. Paste the same clipboard between existing text —
corrupted, and not the yanked text.

**Root cause — `ED_PTR` was both a pointer and a scaler.** `edPaste` keeps its clipboard
**read pointer** in `ED_PTR` (instance offset 42) and its remaining count in
`ED_STCOL`/`ED_STIDX`, then loops: `edInsChar` per byte, `edReturn` per CR. `edReturn` calls
`edInsLine` **unconditionally** to open the line, and a line opened above the last one
shifts the records below it with `edRunDn`/`edRunUp` — which used `ED_PTR` as their
**16-bit length scratch** (`count x 41` = 41/82/164/...). From the first CR onward the loop
read its clipboard bytes from address `count x 41` — zero page / the BASIC area — inserting
whatever bytes happen to live there, with any `$0D` among them opening further lines. The
countdown in `ED_STCOL` was untouched, so the paste terminated instead of hanging, which is
why the symptom read as "the wrong text" rather than as a crash.

**Why appending at the bottom was immune:** `edInsLine` opens with a zero-shift guard
(`L == ED_LINES` -> `edInsRecDone`), so inserting the last line runs no shift, never calls
`edRunDn`, and never touches `ED_PTR`. The trigger is "records follow the insertion point",
**not** "the clipboard is multi-line" — a single-line paste that overflows its line into a
new record hits it too.

The headers claimed the opposite: `edPasteAdv` documented "ED_PTR survives
edInsChar/edReturn/edInsLine". A mechanical sweep settled which field was to blame — of the
13 `ED_*` fields the insert path writes, the intersection with the five the paste loop keeps
live (`ED_PTR`, `ED_STCOL`, `ED_STIDX`, `ED_CB`, `ED_CBSZ`) was exactly `ED_PTR`.

**Fix (option 1 of 3, user-chosen): stop the shift routines borrowing a pointer field.**
The length now goes on the **task's own stack** (`lda kPtr / pha / lda kPtr+1 / pha`), and
each routine's second DMA reuses the `kArg+3`/`kArg+4` the first one already holds —
`reuSetup` only reads them and `kReuFromC64`/`kReuToC64` clobber `A` alone, so the reload was
redundant. Neither routine touches any `ED_*` field now, which makes `edPasteAdv`'s
invariant true for every caller. Options 2 and 3 were declined: a private paste pointer pair
leaves the aliasing trap in place for other `ED_PTR` users, and a save/restore at each
insert site is a band-aid per call site in a task with no spare bytes.

**Verified:** `edit` **9227 -> 9181 bytes of code (-46)**, exactly the instruction
arithmetic (-5 prologue, -5 first-DMA tail, -13 second-DMA tail, per routine), which also
brings the task back from 37 to **36 pool pages** and returns one page to the pool; 21
asserts / 0 failed; relocation verification OK; REU reseeded and `check-reu-current.ps1`
reports all 40 built binaries byte-identical in the image. **User-confirmed in VICE
(2026-09-16):** yank and cut both paste correctly into existing text — the case where
records follow the insertion point.

**Lesson:** a field named for a *pointer* is not a scaler. `ED_PTR` read as a length is a
silent aliasing bug that only bites the path which keeps the pointer live across the writer
— and the writer's header was not wrong, just useless: it listed `A, X, Y, kPtr,
kArg..kArg+5` and never mentioned the instance-field pointer it silently destroyed.

---

## Bug #108: The Shell Lost Track of Its Foreground Child after `compact` 🔴 ✅ FIXED (2026-09-15)

**Severity:** High — the console goes deaf: keys still echo, but every line reads
back empty, so no command ever runs again (internal ones or `.com`s, `run`
included). The screen keeps the last output and a blinking cursor; only a reboot
recovers.

**Location:** `src/kernel/warm-boot-reset.inc` (new `warmClearBufRecs`) —
context: `src/kernel/malloc.inc` (`dynTableReset`), `src/kernel/init.inc`
(`start:`'s heap rewind), `src/kernel/syscall.inc` (`sysSpawn`'s instance-buffer
allocator: `scAlloc` / `scZeroBuf`), `src/kernel/keyboard.inc` (`readString`)

**Symptoms (reported 2026-09-15):**

- `compact` runs, then the console stops responding — but only *after a warm
  reboot* earlier in the session; a cold start is unaffected.
- "every shell command I type is ignored, and I can't run any task with the
  `run` command either. It simply ignores everything I type (although what I
  type is echoed on the screen) and returns an empty line."
- Intermittent, and tied to `compact`: when `compact` found something to do, the
  next `exit` sometimes "prints garbage as well".

**Root cause:** a slot's instance-buffer ownership record outlived a warm reboot
while the heap it points into was rewound, so two slots ended up owning one
buffer.

`start:` rewinds the per-instance heap to `$1D40` on every boot
(`lda #<$1D40; sta dataHeap`), and `init.inc` clears the orphan free list for
exactly that reason — *“stale entries from before a warm reboot could point at
ranges the fresh bump allocator hands out again (double-own)”*. But the records
a spawn consults **first** — `taskDataSize` / `taskDataPtrLo/Hi` — are kernel
data too, and `dynTableReset` (the warm-reboot reset of “task slots 1..”)
cleared `taskState`, the dyn table and the malloc table, **not those three
arrays**. So after a warm reboot:

- a spawn whose `dataSize` matches its stale record takes `scAlloc`'s reuse path
  (`lda taskDataSize,x / cmp kArg+1 / jmp scZeroBuf`) and is handed the *old*
  buffer — **without advancing `dataHeap`**, so nothing reserves that range;
- the next spawn whose size does *not* match its own stale record takes
  `scFresh` and is issued `dataHeap` — an address inside the range the reused
  buffer already occupies;
- both slots now hold the same `taskDataPtrLo/Hi`, and `scZeroBuf` zeroes the
  whole buffer of whichever spawns second.

The shell's instance buffer begins with `LINEBUF` (offset 0), so its line buffer
is wiped: `readString` still returns a length (the shell prints its blank line),
but `processCommand` reads a zero first byte and returns silently. That is the
signature — **keys echo, every line is empty**.

Why a warm reboot is required: a cold start has zeroed RAM, so every record is
`(0, 0)` and every spawn allocates cleanly. Why `compact` exposes it: in the
reported sequence nothing grows the heap past the reused buffers until a task
with a *new* size is loaded — `compact` (240B, after the earlier `run basic`) is
the first `scFresh` allocation after the reboot, so it lands on the shell's
reused buffer and zeroes it.

**Fix applied:** `warmClearBufRecs` (`src/kernel/warm-boot-reset.inc`, 17B in the
free run at `$CA1B`, entered by a `jsr` from `warmBootReset`) zeroes
`taskDataSize` / `taskDataPtrLo` / `taskDataPtrHi` for every slot before
`jmp start`, so the rewound heap and the records agree again. It is a separate
routine rather than three stores inside `dynTableReset` because that block sits
in the pinned tail band: 9 bytes there shifted the gfx.lib scratch aliases
(`ffStkXLo`, `kCircleCxLo`, …) and pushed a block into the `$FF7E` lib-entry
block. Cost: 17B + the 3B `jsr` = **20B** — free total 223B → **203B across 19
pockets** (`verify-map`'s pocket pins updated), core headroom untouched at 20B.

**Related hardening in the same pass — NOT the cause of this bug:**

- `surfDisplayed` (`screen-io.inc`) no longer writes `kArg+4`: the scheduler calls
  it on every context switch and `sysExit` calls it with a parked task's `kArg`
  live, so a kArg byte there silently corrupted another task's scratch. It keeps
  `resolve(kbFocus)` in its own kernel-data byte (`surfResolved`, the reserved
  byte after `randomSeed`; same size, same address, no label moved).
- the shell no longer parks its foreground child slot in `kArg+4` — the DMA
  length hi, written by any task's DMA — but in its own instance byte `PCFG`.
  The duplicate empty-args tails in `pcFgPs` merged to pay for it
  (**4864 → 4860 bytes**, still 19 pool pages).

**Verification:** 211 asserts / 0 failed; `verify-map` green (203B across 19
pockets, `1839-183F` and `CA2C-CA2E` pins updated); PRG starts `01 08`; REU
rebuilt.

---

## Bug #102: `pause` / `prio <id> 0` Overwrote `TA_SWAPPED` and `TA_DEAD` 🔴 ✅ FIXED (2026-09-14)

**Severity:** High for a swapped victim — a `TA_SWAPPED` task whose state byte is
rewritten to `TA_PAUSED` loses the *only* record that its code pages were returned
to the pool (`taskState` is the residency proof: `sysPause`'s write is not paired
with a reload). The next scheduler pass then runs a task whose code is not
resident, i.e. whatever the pool bitmap has handed out since.

**Root cause (`src/kernel/syscall.inc`):** `sysPause` wrote the state
unconditionally:

```asm
sysPause:
    lda #TA_PAUSED
    sta taskState,x
```

and `sysSetPrio` had its **own duplicate** of that store (`spPause`) for the
`priority == 0` path. Neither looked at the current state, so:

- `pause` on a **swapped** task → `TA_SWAPPED` (4) → `TA_PAUSED` (2): the
  eviction's residency record is destroyed and `resume` can put a non-resident
  task back in the run queue.
- `pause` on a **dead** task → `TA_DEAD` (0) → `TA_PAUSED` (2): a dead slot looks
  alive again, and `findFreeSlot` / the TCB reuse path share that slot.
- `prio <id> 0` reached the same store through `spPause`, so the bug had two
  doors — reachable **from the shell**: `ps` (pick a swapped id) → `prio <id> 0`
  → the id is now `TA_PAUSED`, and `resume <id>` runs freed pages.

**Fix:** the write is now guarded on the *current* state, and `sysSetPrio`'s
duplicate block was deleted (`priority 0` jumps to `sysPause` — one store, one
guard, and 5B back in the core's headroom):

```asm
sysPause:
    lda taskState,x
    beq pauseDone               // DEAD — leave it dead
    cmp #TA_SWAPPED
    beq pauseDone               // SWAPPED — code not resident
    lda #TA_PAUSED
    sta taskState,x
    cpx curTask
    bne pauseDone
    jmp reschedule
pauseDone:
    cli
    rts
```

**Rule:** a state transition may only ever move a task between the states that
are legal *for that task's current state*. `TA_SWAPPED` and `TA_DEAD` are
terminal for a given slot incarnation — nothing may overwrite them except the
code that owns the transition (the scheduler's restore, and `dynFree`).

**Verified:** builds green, 209 asserts, 0 failed; `tools/verify-map.ps1` all
checks pass.

---

## Bug #103: `surfFreeSlot` Clobbers the Kill Cascade's Root (`kArg`) 🟡 ✅ FIXED (2026-09-14, and it *freed* bytes)

**Severity:** Low — needs a `kill` cascade whose victim has a **thread child that
owns an unshared surface**, so it is latent today. When it triggers, the cascade
loses track of the root task and can leave the subtree partially torn down
(alive children of a freed parent — the case Bug #101's gate then *holds*).

**Root cause (`src/kernel/syscall.inc`, `skKillChild`):** the cascade keeps its
root in `kArg` (`skCascade: stx kArg`) and its fixpoint "changed" flag in
`kArg+1`:

```asm
    tya; pha                    // save child index (surfFreeSlot clobbers Y)
    tya; tax
    jsr surfFreeSlot            // ⚠️ clobbers kArg..kArg+3
    pla; tay
```

`surfFreeSlot` was written as a standalone helper and its header does not list
`kArg`, but it uses `kArg` for its own parameters — and it is called *inside* the
cascade, where `kArg` is live state. `kArg+1`'s clobber is benign (the next
`skCascadePass` re-zeroes the flag, so the worst case is one extra pass); the
`kArg` clobber is not.

**Why it is not fixed yet — a measured byte arithmetic, not an oversight:** the
save/restore is 8B, and it sits between `skCascadePass` and
`bne skCascadePass` at the fixpoint check:

```
Error: relative address is illegal (jump distance is too far: -129)   (syscall.inc:518)
```

so the fix also needs the invert-and-jump form of that branch (+3B) = **11B**,
and the core's growth headroom is **12B** (`$13C8-$13D3`; the `kernelCoreEnd <=
$13D4` assert in `main.asm` is what would fire). Tried, measured, reverted; the
call site carries a ⚠️ comment pointing here.

**Recipe as originally written** (kept because option 2 is still the fallback, and
because option 1 was WRONG — see the fix below):

1. ~~Move the cascade root out of `surfFreeSlot`'s reach into another `kArg`
   slot~~ — the *idea* (a dedicated home) was right, the *place* was not:
   `dynFree` clobbers `kArg+3..+7` and this cascade calls it per victim, so
   `kArg+6` is no safer than `kArg`; **or**
2. take a relocation for the band and do the honest save/restore (+11B).

**Do not** "fix" this by deleting the `surfFreeSlot` call — that leaks the
victim's surface, which is the bug it was added to stop.

### Fix applied (2026-09-14) — and why it is free

The root and the fixpoint flag now live in two **dead kernel-data bytes**:
`dlCascadeRoot` / `dlCascadeFlag`, repurposed from `cursorLast` / `kbCursCol` in
`kernel-vars.inc` (declared, referenced *nowhere* — kernel, libs, tasks,
`main.asm` and docs all checked), so the byte count and every offset after them
stay put. Nothing on that path can reach them: `surfFreeSlot` clobbers
`kArg..+3`, `dynFree` `kArg+3..+7`, and `transferFocusAway` writes `kArg` as
`fromTask`.

That also made the root's save/restore around `transferFocusAway`
(`lda kArg; pha` / `pla; sta kArg`, 8B) unnecessary, so the fix is **−8B in the
core band** rather than the +11B the in-place save/restore needed — that version
was only ever "needed" because the root lived in a scratch block everything else
uses. Measured: `kernelCoreEnd` `$13C8` → `$13C0`, core growth headroom 12B →
**20B**, free total 201 → **203**.

---

## Bug #105: A Thread Child in Task Slot 16+ Stored Its Name Over Another Child's 🟡 ✅ FIXED (2026-09-14)

**Severity:** Low — cosmetic (`ps` name collision between two live thread children), no corruption. Found while reviewing the #104 changes, not by a repro.
**Location:** `src/kernel/syscall.inc` (`sysThreadSpawn`, the "compute + store REU address" block), against `REU_THREAD_NAME = $C000` (`kernel.inc`) and `kGetTaskName` (`task-util.inc`).

### What it was

The name address is `REU_THREAD_NAME + slot × 16`, computed as a ×16 shift plus a base add:

```asm
    txa
    asl; asl; asl; asl          // A = slot×16 lo, C = bit 8 (slot 23 → $170)
    clc; adc #<REU_THREAD_NAME  // ← $00: a no-op add — and the clc destroys C
    ...
    lda #0
    adc #>REU_THREAD_NAME       // → always $C0, never $C1
```

`REU_THREAD_NAME`'s low byte is `$00`, so the add contributed nothing; the only thing the `clc` did was throw away the shift's overflow — which *is* the high byte's carry. So the high byte was always `$C0`: slots 0-15 were correct (`$C000-$C0FF`), and slots 16-23 addressed `$C0xx` instead of `$C1xx`, i.e. the name slot of the child 16 task slots earlier. Two live thread children that far apart therefore showed each other's name in `ps` (each child's own name still read back consistently, because `kGetTaskName` reads the same stored pair — the later write wins).

The table is genuinely sized for it (24 × 16B = `$C000-$C17F`) and nothing else claims `$C100-$C17F`, so the arithmetic was the only defect. Note the sibling idiom a few lines above — `clc; adc #<nameBuf; lda #>nameBuf; adc #0` — is the case that **needs** its `clc`: `nameBuf` is a C64 address with a real low byte, so there the low add genuinely carries.

### Fix (3 bytes *smaller*)

Drop the no-op add and let the carry reach the high byte — `STA` leaves C alone, which is what carries it:

```asm
    asl; asl; asl; asl
    sta taskThreadNameReuLo,x
    sta kArg
    lda #>REU_THREAD_NAME
    adc #0                      // + the ×16 carry
    sta taskThreadNameReuHi,x
    sta kArg+1
```

Two asserts now pin what the code depends on: the base is page-aligned (`(REU_THREAD_NAME & $FF) == 0`) and the table needs at most one carry (`MAX_TASKS*16 <= 512`). Slot check: 0 → `$C000`, 15 → `$C0F0`, 16 → `$C100`, 23 → `$C170`.

**Verified:** 211 asserts, 0 failed; `kernelCoreEnd` `$13CB` → `$13C8`; core growth headroom 9B → **12B**; free total 186 → **189**; `tools/verify-map.ps1` re-pinned.

---

## Bug #104: The 2026-09-14 Code Review — 6 Fixed, 1 Half-Fixed, 2 Refuted, 1 Parked 🟡 ✅ MOSTLY CLOSED

**Severity:** Mixed; each item was confirmed by reading the code, so each carries
the trigger that makes it reachable. **Swept 2026-09-14:** **M4, M5, M6, M7, L8
and L10 are fixed** (four of them for free), **L11's tag clear is in** (its two
remaining unwind gaps are parked with measured costs), **two of my own findings
turned out to be WRONG and are refuted below** (L9, M5b — the lesson is in each
row: re-read the code that *decides*, not the code that *looks* wrong), and only
L11's unwind gaps are still open.

**The fixes:**

- **M4 (record identity) — 13 bytes back.** The record no longer caches a name
  *pointer*; identity is derived from the live instance by a new helper
  `findLiveByName` (scan the dyn slots for one whose `dynTaskId != $FF`,
  `dynTaskResidency != $FF` and whose own name buffer matches). It replaces
  `dlReent`'s key loop *and* `dlRestore`'s `dlrAttScan` — both of which walked the
  records comparing against the cached pointer. Why that is enough is the whole
  point: `dlRegister`'s `dlCopyName` gives every instance its own buffer and a
  slot's buffer can only be rewritten once its dyn entry is free, so a tagged live
  slot *is* the name. `resNameLo/Hi` degrade to the live/tombstone marker they had
  effectively become (no write site changed, no data moved, nothing task-facing
  shifted). Net effect on the loader band: **−12B** (the helper is 13B cheaper than
  the two scans it replaces; 1B went back for the `tay` below), so `$532F-$533F`
  is 17B and the free total went 189 → **201**.
- **M4's index contract, and the lockup it caused (found by the user's repro,
  2026-09-14).** `dlReentFound` indexes the record with **Y**; `findLiveByName`
  returns the index in **A** and leaves Y = the *instance's dyn slot*. Those
  coincide only while the record's origin still owns its original slot — true for
  the boot shell (record 0 IS dyn slot 0), which is why the first `run shell`
  worked. Kill the boot instance, load anything (it takes the freed dyn slot), then
  `run shell`: the spawn read a *different*, tombstoned record — entry `$0000`, size
  0 — started a task at address 0 and locked the machine up. One `tay` fixes it, and
  the helper's header now states the contract, because the failure is invisible
  until the two indices diverge.
- **M4's small display consequence, fixed with it.** `ps` marked an instance `(shr)`
  whenever it was *attached* to a residency record — and M4 made a **lone** attached
  instance reachable (kill the record's origin while a co-instance lives; the
  survivor, and anything that attaches to that record later, owns no pages of its own
  because the record does). So `ps` showed `(shr)` for a task that was the only one
  running. The marker now requires `resRefCount >= 2` — attached *and* actually shared
  — via a new task-side alias (`resRefCount = $CB02`, asserted in `main.asm`). `pool`
  was already correct: it counts the records' pages separately (shell.inc), so its
  totals never depended on which instance was named in the record's key.

- **M6 (self-pause) — 0 bytes.** `sysPause`'s self path now jumps to `sysYield`
  instead of `reschedule`. `reschedule` = `findNextAlive` + `svRestore` and saves
  NOTHING, so a task that paused itself came back at its **last preemption point**
  and silently replayed everything it had done since. `sysYield` builds the same
  fake interrupt frame the timer path uses, so the task resumes at the instruction
  after `jsr syscall` (A=0, Y=syscall#*2, like a yield) — and because its state is
  `PAUSED`, `findNextAlive` will not pick it up until something resumes it, so the
  "never returns until resumed" contract is unchanged. Only the jump target moved.
- **L8 (F-key switch ate a keystroke) — 0 bytes net.** `kbFKey` wrote the
  **screen number** into `keybLast`, and `keybLast` holds a **key code** — so after
  switching to screen N, pressing the key whose code is N (screen codes 0-7 =
  `@ A B C D E F G`) was read as "same key still held" and either swallowed
  (`keybTimer == 0`) or answered with a stale repeat of the previous key. The dedup
  was redundant: `kbSwitchToScreen` already ends in `cpy kbFocus / beq kbsDone`, so
  a held F-key costs one scan per tick and changes nothing. The three dedup
  instructions (8B) are gone; the 8B come back as an inert NOP sled because this
  band's labels are pinned ABI (`keyrpt.com` pokes `keybRepeatLda`, and gfx.lib's
  scratch aliases live here — 13 asserts fire if the band shifts). A fresh press
  now always re-asserts the screen through that guard.
- **M5 (eviction cascade killed kDynLoad children) — +5B of loader headroom.**
  `dlEvKillDoChild` now checks `taskThreadNameReuHi,y` and **skips a `kDynLoad`
  child**. Such a child owns its own code pages *and* its own dyn entry, and
  `dlEvKillTask` frees neither — so the old cascade leaked marked pages plus a
  phantom `dynTaskId` (`pool` shows it, a later `kill <id>` can land on a recycled
  slot, and it consumes the 8 dyn slots until `run` fails with "no free dyn slot").
  Nothing is lost: only a thread child shares the victim's pages (the reason this
  path exists), and surviving a parent is what `exit` already does and what
  `kill`'s own cascade documents. Cost: the loader band's free run `$5336-$533F`
  10B → 5B, free total 191 → 186; `tools/verify-map.ps1` re-pinned.

- **M7 (malloc-restore misalignment) — 0 bytes.** The "pool full" path in
  `dlRestoreMalloc` now jumps to `dlRmBn` (the routine's exit tail) instead of
  skipping the block and continuing the walk. Skipping left the shadow cursor
  behind, so every *later* block was DMA'd from the wrong offset; and their
  contents do not matter, because a restored task is cold-restarted and
  re-`kMalloc`s. Bailing out keeps the rest of the data in the shadow with
  `swapped` still set; those entries are cleared when the task dies
  (`freeMallocBlocks`), so nothing leaks. The band it lives in is pinned against
  its neighbour, so a byte-hungry fix was never available — this one is free.
- **L10 (`allocFromPool(0)`) — 3B.** The allocator now refuses a zero-page
  request (`txa / beq afpFull`) instead of returning success with `startPage 0`
  and marking pool page 0 used. Still unreachable (every caller guards), but the
  contract now lives in the allocator rather than in its call sites.
- **L11(a) (`dlRegUnwind`'s stale tag) — 3B.** The failed-registration path now
  clears `dynTaskResidency` alongside `dynTaskId` (it already had `$FF` in A), so
  a half-torn-down slot cannot look live to anything. (b) freeing the dropped
  program-RAM block there measures ~14B and (c) tombstoning the record ~10B;
  both are parked — see the row.

| # | Where | Failure | Trigger |
|---|---|---|---|
| **M4** ✅ FIXED (−12B of loader band) | `dynload.inc` residency record (`resName`) | The record was keyed by a **pointer** into a per-slot `dynTaskNameBuf` (`resAddRecord` stored the `kPtr` that `dlCopyName` had just written = `&dynTaskNameBuf[dlRegSlot]` of the load that CREATED the record). ⚠️ **Only the ORIGIN instance's death exposes it** — that is the whole subtlety. The record is keyed once, at the first load (`reentFirst`); a shared instance's load clears `reentFirst` defensively and never re-keys it, and killing a co-instance just decrements `resRefCount` (`dfSharedAlive`), leaving the origin's buffer untouched. So the flaw needs the **first-loaded** instance of that name to die while a co-instance survives (the co-instance then holds pages that no reachable record describes), AND its freed dyn slot to be re-used. Then the key reads the new occupant's name: `run shell` matches nothing and loads a **full second shell** (19 more pages, a second record) instead of sharing; with a second reentrant binary in the tree the record could instead share the **wrong entry** (the dangerous half). A tombstone-on-origin-death "fix" is worse than the bug — the detached survivor keeps running pages nothing owns, so they leak for good. `dlRestore`'s attach scan compares against the same key. Fix options: give each record its own 12B name (`MAX_RESIDENTS` = 4 → 48B of kernel data; the data band's biggest free run is 17B, so that is a relocation decision), or rebuild the identity from the record's own live instances (~30B of code in the loader band, which has 5B left). The 18B of dead data declarations (`hexChars` 16B + `cursorLast` + `kbCursCol`) covers neither — so the fix derives identity from the live instance instead (`findLiveByName`; see *The four fixes* above), which needs no data at all. | **kill the FIRST/boot instance**, then load any dynamic task (it takes the freed dyn slot), then `run shell` |
| **M5** ✅ FIXED (+5B) | `dlEvKill` (eviction band) | The subtree cascade killed dynamic descendants **without `dynFree`**, so their pages were never returned and their dyn entries kept a **recycled** id — a later `kill <id>` could then hit the wrong task. | eviction of a task with dynamic children |
| **M5b** ❌ REFUTED | `dlEvDo` shared-instance path | **Not a bug.** The claim was that a sibling's `dfSharedAlive` could reach 0 and free the same range twice. It cannot: `dlEvMarkShared` sets `dynTaskResidency = $FF` for **every** instance of the record in the same loop that swaps them ("detach — dynFree treats as single"), so a later `dynFree` takes the `dfSingle` path, sees `DYN_FLAG_SWAPPED` and skips `freeFromPool` entirely. The stale `resRefCount`/`resStartPage` left on the tombstone are harmless — `resAddRecord`'s `resFill` overwrites every field when the slot is reused, and `dlRestore` now decides by the instance's own residency tag plus `findLiveByName`, so a tombstone is simply a record no live instance points at. My original entry read the `dfSharedAlive` branch without checking that nothing reaches it. | eviction of one of two shared instances |
| **M6** ✅ FIXED (0B) | `sysPause` self path (was `jmp reschedule`) | Pausing *self* jumped to the scheduler **without saving the outgoing context**, so the task resumed at its last preemption point, not at the pause. | `pause` a task from itself (any task pausing itself) |
| **M7** ✅ FIXED (0B) | `dlRestoreMalloc` | Real but **overstated in the first review**: the pool-full path does leave the shadow cursor behind, so the *following* malloc blocks are restored from the wrong offset. It cannot corrupt accounting — a skipped block stays `swapped=1` with `mallocStartPage=0`, so it owns no pages (no leak, no double free, `kFree`/`freeMallocBlocks` just clear the entry). And the contents are stale anyway: a restored task is **cold-restarted** at its entry, so it re-`kMalloc`s. `dlRestore`'s fit check covers code + program RAM only, so the pool-full case *is* reachable. Fix = advance `dlMallocHi` by the skipped block's size (~13B) inside a band that is exactly pinned against its neighbours. | restore of a malloc block when the pool cannot hold it |
| **L8** ✅ FIXED (0B net) | `kbFKey` | Wrote a **screen number** into `keybLast`, colliding with screen codes 0-7 (`@ A B C D E F G`) — the next such keypress was swallowed or repeated a stale key. | F-key press then that key |
| **L9** ❌ REFUTED | `readString` | **Not a bug.** `lda #0 / sta blinkTick` is deliberate — it suppresses a blink *during the dispatch window*, while the cursor is being moved. Every dispatch path ends in `jsr blinkReset`, which sets `blinkTick = 1` ("redraw ON at the current cursor next tick"), so there is no 255-tick gap: the countdown is re-armed at 1 before the routine loops. I read the `#0` store and stopped one call short of the shared tail. | every `readString` |
| **L10** ✅ FIXED (3B) | `allocFromPool(0)` | A zero-page-count request returns success with `startPage = 0` and marks pool page 0 used — but **every** caller guards it already (`kMallocImpl` rejects 0 pages, `dlLoadCore`'s RAM path errors 9, and `allocFromPoolTop` — used by libraries — is a 5B shim *into* `allocFromPool`, so one guard would cover both). The leak is one pool page, and the "block" is 0 pages long. Parked on **reachability, not bytes**: 3B would harden `allocFromPool` against a future caller, but every current caller guards it, so the invariant currently rests on the call sites (the loader band now has 18B, so the byte objection is gone — it is simply not worth changing live code for a case the tree cannot reach). | a zero-size restore/lib request |
| **L11** ✅ HALF — (a) FIXED (3B); (b) ~14B, (c) ~10B parked | `dlRegUnwind`, `dlRegister`'s defensive path | Unwinds are **incomplete**: after a failed eager-lib load of a **reentrant** task the residency record survives pointing at freed pages (a later load of that name would spawn into them), and the defensive dyn-table-full path drops the program-RAM block. Needs a reentrant task that declares an **eager** library *and* does not fit — no shipped task does (the shell is the only reentrant task and its libs are lazy), and the defensive path is pre-checked unreachable. | a load that fails after allocation |

**Verified clean in the same review** (recorded so the next sweep does not
re-audit them): page and library accounting is exactly-once, the IRQ re-`sei`
discipline after every `cli` window, stack balance on all error paths, eager
refcount rollback, `dlAllocHi` (libraries top-down), `clearSlotShadows`, malloc
shadow geometry, v3 fixup bounds, and `warmBootReset`.

**Not swept:** the REU filesystem internals (`reu-fs.inc`, `reu.inc`) and the
task/command layer (`src/tasks/**`).

---

## Bug #101: An Exit-Orphan (a Thread Child Whose Parent Exited) Could Run Freed Code 🟡 ✅ FIXED (core case) — ✅ Residual #2 (shared-eviction cleanup gap) also FIXED 2026-09-16

**Severity:** High when it happened — the child kept its TCB, stayed `TA_ALIVE`, and the scheduler picked it, so it executed whatever the pool bitmap handed out next (possibly the library code its parent had just released).
**Status:** ✅ FIXED 2026-09-14 — `childGate` (22B — `$13B0` after Bug #105's 3B — the last thing before `kernelCoreEnd`) plus 5B in `findNextAlive`. Cost: the core's growth headroom went **28B → 4B**, then the review's #102/#103 fixes gave 5B back and Bug #105 3B more (**12B** — see *Trade* below). ✅ Residual #2 (a narrower gap in shared/reentrant eviction, found and parked 2026-09-16) is now FIXED — see *Residual #2* below.
**Location:** `findNextAlive` (`src/kernel/scheduler.inc`) against `sysExit` (`src/kernel/syscall.inc`, `jsr dynFree` with no cascade — by design).

### Why it is dangerous

A thread child has **no dyn entry** and runs its **parent's** code pages (`sysThreadSpawn` never registers it), so `sysExit` → `dynFree` hands those pages back to the pool bitmap while the child is still `TA_ALIVE`. Nothing reaps it: only `sysKill` cascades, and the eviction path has its own subtree kill (below).

### The eviction case is already safe — deliberately

Do not "fix" eviction for this: `dlEvDo` scans the victim's task-slot children and, if it finds a live **thread** child (`dlEvIsDynTask` keeps `kDynLoad` children out — they have their own pages), takes `jmp dlEvKill` instead of the swap path. `dlEvKill` kills the victim, runs a `$FE`-sentinel fixpoint over its whole descendant subtree, and drops the victim's dyn entry. So a parent with live thread children is **killed, not swapped** — the safe outcome the child's page-sharing requires.

### Residual, after the gate

The gate tests the **parent's state at scheduling time**, so it covers every case the cascade sites miss — including the co-instance hole above, which was the reason to prefer it over adding a second and third cascade. What it does *not* do is **reclaim** an exit-orphan: that child is simply never scheduled again, so it keeps its TCB slot and its `ps` row until something kills it (`kill <id>` works and cascades from there). Reaping children before `exit` therefore remains the contract, and `sysExit` still does not cascade.

### Fix as implemented (2026-09-14)

One site is enough: every scheduling decision goes through `findNextAlive`.

```asm
childGate:                          ; X = candidate slot → C=0 run, C=1 hold
    lda taskThreadNameReuHi,x
    beq cgYes                       ; not a thread child
    lda taskParent,x
    bmi cgNoSet                     ; $FF/$FE: hold (see H3 below)
    tax
    lda taskState,x
    beq cgNoSet                     ; parent DEAD → hold
    cmp #TA_SWAPPED
    bcs cgNo                        ; parent SWAPPED → hold (C already set)
cgYes:
    clc
    rts
cgNoSet:
    sec
cgNo:
    rts
```

plus 5B in `findNextAlive` (`bne fnaNext` / `jsr childGate` / `bcs fnaNext` replacing its `beq fnaDone`).

**H3 (2026-09-14 review): `$FF`/`$FE` must HOLD, not run.** The first version of the
gate read `bmi cgYes` — "no usable parent, so there is nothing to test against,
let it run". That is backwards for a *thread* child, which is the only kind that
reaches this branch: `scOrphan` clears a stale `taskParent` to `$FF` when the
slot is recycled, and `$FE` is the kill cascade's own sentinel. In both cases the
task is a thread child whose parent **link** is gone — it is not proof that the
parent's pages are still its own, it is proof they are *not*. Running it is
exactly the exit-orphan this gate exists to prevent; holding it costs a TCB slot
and a `ps` row until something kills it. The fix is a branch **target** change
(`bmi cgNoSet`), so it is 0 bytes.

**Trade:** it fitted only by spending the core's growth headroom — `13B5-13D3` (31B, 28B after Bug #97) → `13D0-13D3` (**4B**), free total 215B → **186B across the same 18 pockets**, kernelCoreEnd `$13B4` → `$13D0`, 209 asserts green, `tools/verify-map.ps1` re-pinned to the new figures. Later the same day Bug #102's state guard was paid for by deleting `sysSetPrio`'s duplicate `spPause` block (**-5B net**), so the run went to `13CB-13D3` (**9B**) and the free total to **191B** — then Bug #105's 3B saving made them `13C8-13D3` (**12B**) and **189B**, then the parked sweep moved them to `13C0-13D3` (**20B**) and **203B**. It is the only headroom the core has. The next core change that needs more than 20B must therefore **relocate** rather than grow — and note that the roomy-looking pockets below `$0801` (`$02A1-$02BF` 31B, `$020D-$021E` 18B) can never hold kernel code at all (the PRG-block/autostart rule). It was first drafted and reverted ("unreachable, don't spend the last 4B"), then reinstated on the user's call: an unreachable bug is still a bug, and the co-instance hole made it reachable the moment anyone writes a shared task that spawns threads.

### Residual #2 (found 2026-09-16, ✅ FIXED 2026-09-16): shared-eviction only cascaded the picked slot's thread children

`dlEvDo`'s shared/reentrant path (`dlEvMarkShared`) swaps out **every** instance attached to a resident record together — correct, since they all share the same code pages. But the thread-child safety check used to run only once, after the loop, keyed to **one** instance: the slot the largest-page-count scan happened to pick (`dlEvictBest = dynTaskId,dlEvictSlot`). If a *different* co-instance under that same record spawned a thread child (`sysThreadSpawn` uses `curTask`, i.e. whichever instance was running), that child's parent was never the picked slot, so it was never included in the kill cascade.

**Why this was not the same danger as the original bug:** `childGate` (the fix above) tests every scheduling decision against the parent's *current state*, not against which cascade ran. Once `dlEvMarkShared` sets a swapped co-instance's `taskState` to `TA_SWAPPED`, `childGate`'s `cmp #TA_SWAPPED / bcs cgNo` already holds any of its thread children — they cannot run on the freed code. So there was no crash and no freed-code execution, only a leak.

**What it leaked:** unlike the picked slot (whose thread children were properly killed and reaped), an unpicked co-instance's thread children became permanently gated zombies — they kept a TCB slot and a `ps` row forever, since nothing but `kill <id>` reaps a gated child (same accepted contract as the original exit-orphan residual above).

**Fix (`dynload.inc`):** the check now runs **per instance**, inside the same loop that swaps each one, instead of once afterward for a single fixed id. Two new subroutines replace the old fixed-id inline logic:
- `dlEvSwapOrKill` (input: `dlEvictBest` = task id, `dlEvictSlot` = dyn slot) — scans for a live thread child of *that* task and either kills it (cascade via the existing `dlEvKill`) or marks it `DYN_FLAG_SWAPPED`/`TA_SWAPPED`, returning C=1/C=0 respectively. This folds in what used to be a separate `dlEvHasKids` scan.
- `dlEvMaybeSelf` — the "set `dlEvictedSelf` if this task is `curTask`" check, previously duplicated at both call sites, now one shared routine.

Both the single-instance path (`dlEvFreeSingle`/`dlEvMark`) and the shared-instance loop (`dlEvMarkShared`) call `dlEvSwapOrKill` per task, so a shared record's co-instances are each independently checked and killed-instead-of-swapped if *they* have a live thread child — not just the one the eviction scan happened to pick. `dlEvictBest` is stashed/restored across the shared loop's nested call since the loop also needs it to hold the residency index for its own matching.

**Cost:** fit inside the loader band's existing budget with 2 bytes to spare — `dlLibsEnd` (the `.assert "dynload fits before font-help ($5340)"` boundary in `main.asm`) moved from its prior value to `$533e`, under the `$5340` ceiling. The subroutine-splitting (inlining the kid-scan into `dlEvSwapOrKill` instead of a separate `jsr`'d routine, and factoring the duplicated self-check into `dlEvMaybeSelf`) paid for the extra per-instance logic — a first draft that kept them as separate calls overshot the ceiling by 11B before this consolidation. 212/212 kernel asserts green, task build unaffected (kernel-only file).

---

## Bug #100: Sprite Control Bytes Leaked From One Owner to the Next 🟡 ✅ RESOLVED (2026-09-19)

> **RESOLVED 2026-09-19.** There is no shared shadow to leak through any more: a task's
> control bytes live in its own per-task block, which `clearSlotSprite` zeroes when the
> slot is reused, so a successor starts with no enable, no expansions, no priority and no
> MC mode. The task-side `sprShadowInit()` claim this entry introduced is gone with it —
> see `docs/lib-sprite.md`

**Severity:** Low visually and harmless to the hardware, but it makes the *next* sprite task's picture wrong in a way that is maddening to attribute, because the culprit has already exited.
**Status:** ✅ FIXED 2026-09-13 (task-side): `sprShadowInit` claims on a clean shadow in `sprdemo` and `godzi`. ⏳ The kernel-side invariant is **OPEN** — blocked on the gate's pocket, see *Why the kernel-side fix is deferred*.
**Location:** `src/kernel/sprite.inc` (`spriteOwnerGate`'s DEAD-owner repair, `rasterCommit`'s publish) against `src/tasks/sprite-shadow.inc` and the two demos' startup

### Symptom

`run godzi`, quit with `q`, then `run sprdemo`: **two of the eight balls are drawn X-expanded.** Reported 2026-09-13.

### Root cause

The seven control bytes (`$D015` enable, `$D017` Y expand, `$D01B` priority, `$D01C` MC mode, `$D01D` X expand, `$D025`/`$D026` MC colours) are **per-slot bitmasks — one byte each for all eight sprites** — and when the owner dies the kernel repairs exactly one of them:

```asm
sprGateDead:
    lda #$ff
    sta sprShOwner              ; the claim is expired …
    lda #0
    sta sprShEnable             ; … and the enable is zeroed — and nothing else
```

Everything else stays exactly as the dead task set it, and `rasterCommit` publishes the whole set unconditionally every frame (`sprShYExp → $D017`, `sprShPriority → $D01B`, `sprShMc → $D01C`, `sprShXExp → $D01D`, `sprShMc0/Mc1 → $D025/$D026`). So a sprite task **inherits every control byte it does not itself set**:

- `godzi` X-expands its walker's two layers only — its own note says the reference's `$D01D = $03`, "sprites 0 and 1" — and it never touches `$D01D` again.
- `sprdemo` never touches `$D01D` at all, and enables all eight balls.

Two bits set, eight enabled → **sprites 0 and 1 are drawn double-width.** The enable byte is the only one that was safe precisely because the gate does repair it, which is why the symptom is "the expansions leaked" rather than "the previous task's sprites are still there" (that older symptom is Bug #89).

### Fix (task-side)

`sprShadowInit()` — a macro in `src/tasks/sprite-shadow.inc` that writes zero to all seven control bytes through `kSpriteReg`, called by both demos before they set their own regs. Zeroing them through `kSpriteReg` **is also the claim**: a successful shadow write stamps `sprShOwner`, which is what the refusal checks and the gate read.

Cost, measured: `sprdemo` 1132 → **1183 B**, `godzi` 827 → **876 B** (+49/+51 B, 7 calls each) and **no new fixups** — a kernel call is an absolute reference into the kernel, not an intra-binary one. Byte-verified in `tasks-out/sprdemo.bin`: seven `A9 00 / A2 $20…$26 / 20 <kSpriteReg>` triples in offset order (`$20` enable, `$21` YExp, `$22` priority, `$23` MC, `$24` XExp, `$25`/`$26` MC colours), stopping exactly before the owner byte (`$27`).

### Why the kernel-side fix is deferred

The invariant is what *should* happen: the gate's DEAD-owner repair should clear the whole intent, and it is affordable — **+5B net**, replacing `lda #0 / sta sprShEnable` with

```asm
    lda #0
    ldx #6                      ; all seven control bytes, Enable..Mc1
sgClearLp:
    sta sprShEnable,x
    dex
    bpl sgClearLp
```

It has nowhere to live. The gate is **47B in a 47B pocket with both neighbours pinned** (Bug #89's cost note), and the largest free run in the whole tree is 31B, so relocating a 52B gate is impossible without reclaiming bytes from a pinned neighbour — the same *relocation decision, not a byte count* that blocks Bug #89's per-slot generation counter. Keeping the repair task-side also matches how the rest of this subsystem is already managed (the refusal check is per task too, and a task that never calls `sprShadowInit` is one that never sets sprite control bytes at all). The gate's comment now states the gap and points here.

**Lesson:** when a shared register set is *single bytes of per-slot bitmasks*, "the task's intent" is **every** control byte, not just the one that switches the feature on. Repairing a subset leaves the rest as an inheritance — and the inheritance outlives the task that left it.

---

## Bug #99: The kArg Shadow Is Stale Across Slot Reuse  ⛔ WON'T FIX (benign)

**Severity:** Benign by contract. `kArg` is a calling-convention scratch: every kernel call documents which fields it reads (the caller must set them) and documents the block as clobbered, so nothing may depend on its initial contents. Recorded here for the mechanism and for the day a relocation makes the fix free.
**Status:** ⛔ **WON'T FIX** — settled 2026-09-13 at the user's direction: it has no failure mode (see *Why it is benign*). This is a decision, not an oversight: do not "fix" it opportunistically, and do not let it block a change that matters.
**Location:** `src/kernel/scheduler.inc` (both kArg DMA gates) against `src/kernel/kernel.inc` (`REU_KARG_BASE = $9700`)

### Why it is benign

Every reader of `kArg` in the tree is handed its inputs before the call — `kMalloc`, `kReuFindFile`/`kReuLoad`/`kReuCreate`, `kAllocScreen`, the `kReuFill*` pair, `kMul16`/`kDiv16`, `kStrcmp`, `kSetScrollRegion`, `sysSleep`'s `kArg+1`, `sysThreadSpawn`'s `kArg+6/+7`, and the loader's origin/size (which the loader pushes on the stack, because the spawn clobbers them) — so the staleness cannot be observed by correct code, and code that reads a `kArg` field nobody set would be wrong regardless of what the initial value happened to be.

Two more properties make it *private* as well as unobservable:

* The save/restore gates make `kArg` per task at all times, so a new task's stale restore can never corrupt a running task's block — when the spawning task resumes it gets **its own** values back.
* `sysSpawn` runs SEI'd, so no context switch can occur between the spawn and the loader's next `kArg` read.

**Contrast Bug #97, which was fixed**, and the difference is not "state vs scratch" hand-waving: the ZP window is documented as the task's *own* ("declare `zp_base`/`zp_len` and the scheduler preserves it") with no rule to initialise it before use — `docs/programmers-guide.md`'s own example is `inc $02`, a read-modify-write on declared ZP — so a task inheriting a predecessor's window inherits a counter or pointer it reasonably believed was its own. No such invitation exists for `kArg` anywhere.

### What it is

The **exact same per-slot-stale shape as Bug #97**, one shadow over: `REU_KARG_BASE + slot × 8` is addressed by slot and only ever *written* by the switch-away DMA, while the restore gate reads it unconditionally — so a task spawned into a recycled slot has the previous occupant's `kArg`/`kResult` block DMA'd in on its first switch-in. Bug #97 fixed the ZP half (`clearSlotShadows`, 32B at `$5482`); this is the half that was left.

### Why it was deferred

1. **It is benign by contract.** Every kernel routine that reads `kArg` documents it as an input the caller must set, and every routine that writes it documents it as clobbered — so no task can depend on a stale value, and the dangerous case (a routine reading an input the caller never set) does not exist in the tree. Contrast the ZP window, which is *task state* (pointers!), where inherited bytes are arbitrary misbehaviour.
2. **No bytes.** The fix is ~28B — `txa / asl / asl / asl` plus `lda #>REU_KARG_BASE` for the aligned address, then a second 8-byte `kReuFillReu` — and the loader band's last free run is down to **9B** after Bug #97. It needs a free run elsewhere: the same relocation decision as Bug #89's generation counter.

### The fix, if it ever becomes a problem

Extend `clearSlotShadows` (`src/kernel/slot-shadow.inc`) with this **before** the ZP fill (X = slot on entry, and `kReuFillReu` stages its fill byte at `kArg+5`):

```asm
    txa                          ; A = slot
    asl
    asl
    asl                          ; slot × 8 = the kArg shadow's low byte
    sta kArg                     ; (the base is page-aligned — see the kArg note
    lda #>REU_KARG_BASE          ;  in switchTask; MAX_TASKS × 8 ≤ 256 is asserted)
    sta kArg+1
    lda #8
    sta kArg+3                   ; length
    lda #0
    sta kArg+2                   ; bank 0
    sta kArg+4                   ; length hi
    jsr kReuFillReu              ; A = 0 → zeros
```

…and find it a home first — that is the whole task. The largest free run in the tree is 17B (three of them, at least one earmarked as the gfx scratch's growth room), the 28B run is the core's growth headroom — which the project's own rule reserves for core changes — and `clearSlotShadows`' pocket is down to 9B, so this is the same *relocation decision, not a byte count* that blocks Bug #89's generation counter. **Won't fix unless a symptom appears or a relocation happens for another reason**, at which point it folds in for the cost of the code above.

---

## Bug #98: The Intermittent Freeze / Garbage After a `kill` 🟡 ✅ FIXED

**Severity:** High when it happened (the whole machine: no cursor, the shell unresponsive — or garbage on the screen), but it was intermittent and could not be reproduced on demand.
**Status:** ✅ FIXED 2026-09-14 (user-confirmed) — `kill.com` now saves its `dataPtr` across the kill. The clobbering site is `dlRestore`, which `dynFree` runs at the end of every kill.
**Location:** `src/tasks/cmd/kill.inc`, against `dlRestore` (`src/kernel/dynload.inc`), reached from `sysKill` → `dynFree` → `dfClearDone`.
**Reported as:** "Running `sidplay` and killing it sometimes causes unpredictable behaviour..." — the title kept the `sidplay` reproducer, but the mechanism is *any* `kill`, from `kill.com`.

### Symptom

"Running `sidplay` and killing it sometimes causes unpredictable behaviour if I try to run some other task afterwards — it can print garbage on the screen or hang the system." The SID keeps droning after the kill unless something silences it: `sidrst` used to be the only way, and since 2026-09-14 `kill` clears the chip itself for a task that declared `sid.lib` (see the root cause below). Failing runs were followed by `gfxdemo`/`godzi`/`sprdemo` or `gortris`. `kill` is by id.

### What was ruled out first, and how

* **Page/lib accounting on the kill path** — `sysKill` → `dynFree` calls `freeMallocBlocks` (the tune buffer), `dlLibRelease` (sid.lib at refcount 0), and frees the code pages; `libFree` restores every patched jump entry to its cold stub, so a patched entry can never point into pages the bitmap calls free (the Bug #92 failure mode).
* **Resurrection by the sleep path** — `irq.inc`'s tick loop is gated on `taskState == TA_SLEEPING`, and kill writes 0, so a killed sleeper cannot be woken.
* **`kSidPlay`'s ownership heal** — `$FF` is tested before the `taskState,x` read (no out-of-range read), and a DEAD owner is taken over.
* **Display hand-off on the kill** — `transferFocusAway` returns immediately when the victim holds no focus, and sidplay has no surface at all.
* **The switch path's `fromTask = $FF`** — `doGfxSwitch` guards it (`cpy #MAX_TASKS / bcs`) and the `screenAlloc[y] == 0` case, so there is no out-of-range shadow read.
* **Surface-slot leak on restore** — `allocScreen`'s `asReuse` path keeps a restored task's existing surface, closing the 2026-08-16 "v1 limitation".
* **`kill <id>` hitting the wrong task** — `ps` prints the raw slot index and `kill` bounds-checks it against `totalTasks` (and refuses an already-DEAD slot), so it cannot shoot a neighbour.
* **The ZP-shadow leak** found during this investigation was real and is now fixed (Bug #97) — it is a plausible contributor, but it is a *state inheritance*, not a mechanism that freezes the machine, so it is not claimed as the cause.
* **`kill.com` reading its own scratch after the kill** — this one was **real** and is the cause: see *Root cause* below.

### Root cause: `kill.com` used `dataPtr` after `SYS_KILL`

`sysKill` calls `jsr dynFree` **inside the syscall** (`syscall.inc:493`), and `dynFree` ends with `jsr dlRestore` (`dynload.inc:348`) — a full task restore, which is precisely the *only* code in this path that runs because some task sits in `TA_SWAPPED`. `dlRestore`'s own header (`dynload.inc:1466`) lists **`dataPtr`** among its clobbers, and `dlrNameSetup` really does write it:

```asm
    ldy dlRegSlot
    lda dynTaskNameLo,y; sta dataPtr      ; ← the CALLER's dataPtr, i.e. kill.com's
    lda dynTaskNameHi,y; sta dataPtr+1
```

So when `kill.com`'s `jsr syscall` returned, its `dataPtr` pointed at the **loader's per-slot name buffer** instead of its own 8-byte instance buffer — and every remaining instruction reads or writes `(dataPtr),y`: the saved name pointer (`KILL_NAMLO/HI`), the decimal scratch (`KILL_DECBUF`, plus `sta (kPtr),y` to null-terminate), and the `kPrintString` of the victim's name. That is "garbage on the screen" by construction; "hang the system" is the same bytes going somewhere worse (the null-store lands in the loader's name buffer, and a garbled name pointer sends `kPrintString` wherever it leads).

It is intermittent for exactly the reason this entry kept circling: `dlRestore` only *does* anything when a swapped-out task exists **and** fits the pages the kill just freed — and the failing session's `pool` output (`string.lib` 84, `filesys.lib` 99) is proof that session had been through the evict/restore path. The deliberate reproduction did not fail for the same reason: at that moment nothing was swapped.

**Fix applied 2026-09-14** (as part of the `kill`/`sid.lib` work): `kill.inc` pushes `dataPtr`/`dataPtr+1` on the task's own stack across `jsr syscall` and restores them after. The stack is per-task and only the victim is torn down, so it survives. 12 bytes. `dlRestore` also clobbers `kPtr`, `srcPtr` and `kArg..+7` — `kill` re-points `kPtr` before every print, so `dataPtr` was the whole exposure.

**Confirmed 2026-09-14** — the user exercised the rebuilt `kill` on the new `sid.lib` hush path and it behaves: the chip is silenced, the victim's name prints correctly, and no garbage appeared. An intermittent fault cannot be *proved* gone by a clean run, so this is "fixed as far as it can be settled": the mechanism is proven by reading, the exposure is closed, and if the symptom ever recurs the entry reopens at the border-trace plan below. ⚠️ The general rule it leaves behind: **any task that keeps state across `SYS_KILL` must save it — the kill path is not a plain `rts`.**

### Why the hunt pointed at `dlRestore` — and was right

`dynFree` ends with `jsr dlRestore`, i.e. a **full task restore executed inside the killing syscall**: `reuFindFile` → header DMA → `dlLoadCore` (pool allocations, the fixup loop) → `libLoad` for the restored task's eager libraries → spawn → `transferFocusAway`. It is the only code in the kill path that runs *purely* because some task was in the `TA_SWAPPED` state, which is exactly the shape of an occasional failure: it needs an evicted task to exist.

The suspicion was correct, and for the right reason — the failure really did *need* the restore to run (that session's `pool` output proves it had been through the evict/restore path) — but the defect was not inside `dlRestore`. It was `dlRestore` clobbering its **caller's** `dataPtr`, which the caller then used. In other words the precondition was right and the blamed routine was right; what took the 2026-09-13 session by surprise was that the *victim's* kill path could damage the *killer's* registers. That is the transferable lesson in `.github/copilot-instructions.md` now.

### If it recurs

* Look for the precondition: any task shown as swapped in `ps` **before** the kill.
* If it recurs, a temporary **border-colour freeze trace** is the tool: the machine freezes, so the border is the only channel that survives. Trace `dlRestore`'s entry, its "found a swapped task" branch, its return after the load, and `gfxSlotAlloc`'s failure exit (`kAllocScreen` returning C=1) — the last one distinguishes "surface slots exhausted" from a kernel spin.
* A deliberate reproduction (`run basic` → `kill` it → `run sidplay commodo` → `kill` it → `run gfxdemo`) did **not** fail — because it never left a task swapped out, which is the precondition. A repro attempt from now on must create one first (`run` enough to force an eviction, confirm with `ps`, then `kill`).

---

## Bug #97: A New Task Inherited the Previous Occupant's ZP Window 🟡 ✅ FIXED

**Severity:** Medium — a task could start with another task's zero-page state, pointers included; how bad that is depends entirely on whether the task re-initialises the window before it uses it.
**Status:** ✅ FIXED 2026-09-13 — `clearSlotShadows` (`src/kernel/slot-shadow.inc`, 32B at `$5482`) zeroes the slot's ZP shadow from `scSetAlive`
**Location:** `src/kernel/syscall.inc` (the spawn path) against `src/kernel/scheduler.inc` (the switch gates) and `src/kernel/init.inc`

### Symptom

None that points at itself: whatever the inherited bytes are, they surface as the *successor's* misbehaviour. Found while investigating "`run sidplay`, `kill` it, and running another task afterwards sometimes prints garbage or hangs the machine" — `sidplay` is the sharpest case in the tree because it declares ZP `$02-$0A` (base $02, len 9) and leaves the Sidplayer's live state, pointers included, in that window.

### Root cause

Both per-slot REU shadows are addressed per **slot** but only ever *written* by the switch-away DMA, so their content belongs to whoever last ran in the slot and it outlives that task:

```asm
    ldx curTask                 ; switchTask, SAVE gate
    lda taskZpLen,x
    beq svZpDone
    ... DMA taskZpBase..+len → taskZpReu[slot] ...
svZpDone:
    ...
    ldx curTask                 ; switchTask, RESTORE gate — unconditional
    lda taskZpLen,x
    beq svZpRestDone
    ... DMA taskZpReu[slot] → taskZpBase..+len ...   ; ← the new task's own
                                                      ;    window, filled with
                                                      ;    the OLD task's bytes
```

`init.inc` precomputes the shadow **addresses** at boot (`REU_ZP_BASE + slot × MAX_ZP_LEN`); nothing ever cleared the **content** — not spawn, not kill, not `warmBootReset`. A task spawned into a recycled slot therefore had that slot's leftovers DMA'd into its declared window on its first switch-in — and because ids are recycled lowest-first (`findFreeSlot`), "the slot the task you just killed was using" is the normal case, not a corner.

The stack image is the contrast that makes the omission plain: the spawn path DMAs the new task's initial frame over the slot's stack image (`initTCB_ptr`), so the stack is the new task's from the first tick. The ZP window had no equivalent initialisation.

### Fix

`clearSlotShadows`, called from `scSetAlive` **after** `taskZpBase`/`taskZpLen` are stored so the declared window is known:

```asm
clearSlotShadows:
    lda taskZpReuLo,x
    sta kArg                    ; shadow address lo
    lda taskZpReuHi,x
    sta kArg+1
    lda taskZpLen,x             ; declared window (≤ 240 → the hi byte is 0)
    beq cssRts                  ; none declared → no DMA at all
    sta kArg+3
    lda #0
    sta kArg+2                  ; bank 0 — the ZP shadows live in bank 0
    sta kArg+4
    jsr kReuFillReu             ; A = 0 → zeros
cssRts:
    rts
```

Zeros are the only correct starting state — the one value that is not some other task's data — and no task may rely on undocumented stale ZP. A task with no declared window still does **no DMA**, which is the zero-overhead rule the feature was written with. Runs SEI'd (the whole spawn path is), so the fill cannot race the scheduler's DMAs, and `kReuFillReu` preserves X, which the spawn path still needs.

**Cost.** 32B, in the loader band's **last free run** `$5482-$54AA` (41B → 9B) — *not* in the core's growth headroom, which is exactly what that rule protects. Only the `jsr` touches the core band: 3B, headroom 31B → 28B. Free total 250B → **215B** across the same 18 pockets, now pinned by `tools/verify-map.ps1` together with the new `54A2-54AA` pocket. Byte-verified in the PRG: `BD D5 41 8D 00 40 BD ED 41 8D 01 40 BD BD 41 F0 0E 8D 03 40 A9 00 8D 02 40 8D 04 40 20 72 E0 60` at the `$5482` block, and `jsr $5482` at the call site (right after `tya`/`sta taskZpLen,x`).

**Same class, deliberately not fixed — recorded as Bug #99 and settled as benign (2026-09-13):** the **kArg shadow** (`REU_KARG_BASE + slot × 8`) has the identical per-slot-stale shape — a fresh task's first `kArg` is the previous occupant's — and is not cleared because `kArg` is a *calling-convention scratch* that every kernel call documents as clobbered, so no caller may rely on it across calls, and the 8-byte fill does not fit the 41B run. If it is ever wanted it is ~28B more (the aligned `slot × 8` trick plus a second fill) and needs a free run — a relocation decision, the same shape of problem as Bug #89's generation counter.

**Lesson:** per-slot storage addressed by a *recycled* id is not per-task storage. Anything the kernel keeps "for task N" must be re-initialised when N is handed out, or the successor silently runs on the predecessor's state — and the failure will present as the successor's own bug.

---

## Bug #96: `DEL` in the Hi-Score Name Editor Cleared the Wrong Cell 🔴 ✅ FIXED

**Severity:** Medium — the name typed after a delete was displayed one cell to the right of the cell it was stored in, so the field on screen stopped matching the name that was saved.
**Status:** ✅ FIXED 2026-09-13 — `gtHnDel` repositions a second time, onto the cell it just cleared
**Location:** `src/tasks/dynamic/gortris.inc` (`gtHnDel`), against `kPrintChar` in `src/kernel/screen-io.inc`

### Symptom

Reported as "DEL deletes not the character before the cursor but the one before that". A *sequence* of deletes with no typing in between behaves correctly — each one re-derives its cell from `gtHiLen` — so the fault only appears once a character is typed *after* a delete: the letter echoes one cell right of where the field says it is, the placeholder it should have overwritten stays on screen, and the next DEL then clears that placeholder and leaves the letter standing. The stored name (`gtHnDone` writes `gtHiNameBuf`) is correct throughout; only the display drifts.

### Root cause

The editor's invariant is "the cursor is always on `gtHiNameCol + gtHiLen`, the cell the next character goes in". Every path preserved it except the delete path, because **`kPrintChar` always advances one cell** (`pcAdvance` — `inc cursorCol`, wrap at 40):

```asm
gtHnDel:
    ldy gtHiLen
    beq gtHnLoop
    dey
    sty gtHiLen                 ; gtHiLen is now the cell being taken back
    jsr gtHnCursor              ; ok: cursor onto that cell
    lda #$2e                    ; '.'
    jsr kPrintChar              ; restores the placeholder … AND ADVANCES
    jmp gtHnLoop                ; so the cursor is left on gtHiLen+1, one past
                                ;   the cell the next character belongs on
```

The echo of a key is placed by the *cursor*, not by `gtHiLen`, so the two then disagree by one — and they keep disagreeing, because every subsequent add advances by one as well. The second DEL is the one that makes it visible: it derives its cell from `gtHiLen` (correct), clears that cell (the placeholder), and leaves the just-typed letter untouched one cell to its right.

### Fix

Come back onto the cleared cell after the placeholder print — the invariant is restored at the only place it was broken, +3 bytes and no fixup (`gtHnCursor` is code-relative):

```asm
    lda #$2e
    jsr kPrintChar              ; clears the cell and advances one past it
    jsr gtHnCursor              ; back onto the cell this DEL emptied
    jmp gtHnLoop
```

Positioning *here* is safe, unlike in `gtHnAdd`: the earlier warning that "positioning between the key and the echo prints the row" applies to positioning **before** the echo, because `gtHnCursor` ends in `tay` and leaves A = the row (row 19 = screen code `$13` = `s`). Here A is dead — the key is long since echoed and the loop re-reads.

**Lesson:** an echo whose position comes from the *cursor* and a buffer index that comes from a *counter* are two separate pieces of state; any path that moves one and not the other drifts silently. `kPrintChar` cannot be asked not to advance, so a repositioning path has to account for the advance it just caused.

---

## Bug #95: A Binary `asl` Fed a Decimal-Mode Add — the Score Printed `:` 🟡 ✅ FIXED

**Severity:** Low visually, but it is a wrong score as well as a wrong glyph.
**Status:** ✅ FIXED 2026-09-13 — the hard drop adds 2 a row, one row at a time, so every addend is valid BCD
**Location:** `src/tasks/dynamic/gortris.inc` (`gtHardDrop` → `gtAddScoreY`)

### Symptom

"While the game plays, printing the score sometimes shows `:` as its last character instead of a digit." Only sometimes: it needs a hard drop of 7, 15 or 23 rows with the score's units digit at 6.

### Root cause

`gtAddScoreY` adds in **decimal mode** — it must be handed a valid BCD byte. The hard drop handed it a doubled *binary* count:

```asm
    lda gtHardRows
    asl                         ; 2 a row … but this is a BINARY double
    tay
    jsr gtAddScoreY             ; sed / adc — the addend has to be BCD
```

`asl` on 7 rows gives `$0E`, which is not BCD. The chain from there to the glyph:

1. `adc` in decimal mode adds nibble-wise and applies **one** `+6` correction to a low-nibble sum over 9.
2. That correction only repairs sums up to `$0F`. With an invalid operand the low-nibble sum reaches `$10+` — units 6 + `$E` = `$14` — and `$14 + 6 = $1A`, so the result nibble is `$A`, not a digit.
3. `gtPrintDigit` prints `$30 + digit`, and `$30 + $A = $3A` = `:` (the same mechanism gives `;` `<` `=` `>` `?` for nibbles `$B-$F`).

The points were wrong too, and for every drop over 4 rows: 20 rows paid `$28` — read as decimal 28 — instead of 40.

### Fix

Add the points a row at a time with the valid BCD constant 2, which also fixes the total:

```asm
    ldx gtHardRows
    beq gtHdSettle              ; already resting: no points
gtHdScoreLp:
    ldy #2                      ; valid BCD, so the decimal add is well defined
    jsr gtAddScoreY             ; X is preserved by gtAddScoreY
    dex
    bne gtHdScoreLp
    jsr gtPrintScore            ; print AFTER the loop: the printer clobbers X
```

Worst case is 24 iterations (~700 cycles) on the frame the key is pressed, and the loop costs 4 bytes over the `asl`. Verified by inspection of every other addend too: the soft drop adds `1`, the line-clear table holds `$00,$00,$40` / `$00,$01,$00` / `$00,$03,$00` / `$00,$12,$00`, and the line counter adds `gtLinesMade` (1-4) — all valid BCD, so the hard drop was the only offender.

**Lesson:** decimal mode is only *defined* for BCD operands. A `+6` correction is applied once per nibble, so an invalid operand can leave an invalid nibble in the result — and a digit printer that assumes 0-9 turns that into punctuation. Anything feeding a `sed`/`adc` sequence has to be BCD, and `asl`/`adc` on a binary count never is.

---

## Bug #94: `kIpcPost`'s Backpressure Retry Lost the Channel — `SYS_SLEEP` Returns With `X = curTask` 🟡 ✅ FIXED

**Severity:** High had it shipped — on the multi-channel `ipc.lib` a task pair on one channel would have frozen *and* a different channel's write index would have been silently corrupted.
**Status:** ✅ FIXED 2026-09-13 — caught by inspection while adding the four channels, before the multi-channel test ever ran (`ipctest` passes: 256 in-order bytes on channels 1 and 3)
**Location:** `src/libs/ipc.asm` (`ipcPost`), against `src/kernel/syscall.inc` (`sysSleep`)

### Symptom

None observed — which is the reason to record it. With four channels it would have been: a producer that blocks on a full ring resumes with the wrong channel index, writes its bytes into the *correct* ring (the pointer `kPtr` is right, and it is saved per task) while advancing the *wrong* channel's `ipcHead` — so the real channel's head never moves and its pair stalls. The corruption is the dangerous half: the frozen pair is on channel *N* while the damage lands on whichever neighbouring channel the task *slot* number happens to index — depending on which task slots are free, a task pair could be broken by a third party that never posted to their channel.

### Root cause

```asm
ipcPostWait:
    lda #1
    sta kArg+1
    lda #SYS_SLEEP
    jsr syscall
    jmp ipcPostFull          ; ← ipcHead,x / ipcTail,x / ipcRingLo,x
```

`X` is the channel on entry, and on the first pass through the loop it still is — so the single-channel version this came from was correct as long as nothing blocked. But `sysSleep` opens with `ldx curTask`, and the dispatcher's "X is preserved for kill/pause/resume/setprio/semWait/semSignal" cannot help: it is the *handler* that has already overwritten X. On every retry, `X` is the task slot, and three X-indexed reads then address a neighbouring channel's indices. Note how the failure hides: `kPtr` is unaffected, so the byte still lands in the right ring and the symptom looks like a stall rather than the corruption it is.

### Fix

Park the channel beside the byte that was already being parked for the yield, and re-derive it after the wait. The byte goes on *top* so the store's `pla` recovers it without touching `X`:

```asm
ipcPost:
    cpx #IPC_CHANNELS
    bcs ipcPostBad
    tay                          ; park the byte in Y for a moment…
    txa
    pha                          ; …park the CHANNEL (lower on the stack)…
    tya
    pha                          ; …then park the byte on top of it
    ...
ipcPostWait:
    lda #1
    sta kArg+1
    lda #SYS_SLEEP
    jsr syscall
    tsx                          ; X is curTask now — take the channel back off
    lda $0102,x                  ;   our own stack ($0102,x = one below the top,
    tax                          ;   because the byte sits above it)
    jmp ipcPostFull
```

The stack is the one place a preemptive switch cannot reach: `switchTask` saves and restores SP per task, so the pair is part of the suspended image. The fast path pays nothing — a push that never waits parks nothing it was not already parking — and `ipcPostBad` deliberately has no `pla`, because the range check runs before anything is parked.

Rejected alternatives: **`kArg+2`** works and is 6 bytes smaller (the scheduler saves and restores the whole `kArg..kArg+7` block per task, so it survives the sleep) but it silently consumes a caller's argument slot; **a self-modified `sta abs,y` operand** is not just bigger but *wrong* — the operand would be shared, so two tasks preempting inside `ipcPost` would clobber each other's channel, whereas per-task `kPtr` has no such race.

**Lesson:** `SYS_SLEEP` clobbers X, and "the dispatcher preserves X" is a statement about the dispatcher, not about every handler. **Any routine that sleeps must re-derive its register inputs afterwards**, or park them where a context switch cannot reach. The same applies to `sysYield`, and to any future blocking call added to the syscall table.

---

## Bug #93: A Voluntary `yield` Was Swallowed at Priority > 1 🟡 ✅ FIXED

**Severity:** Medium — a task that raised its priority lost cooperative hand-over, and `semWait`'s "yield and retry" turned into a polling loop that saved and restored the full context on every retry.
**Status:** ✅ FIXED 2026-09-12 — `sysYield` forces `taskSliceLeft = 1` before switching
**Location:** `src/kernel/syscall.inc` (`sysYield`) against `scheduler.inc` (`switchTask`'s slice test)

### Symptom

None at the default priority, which is why it went unnoticed. Raise any task with `prio <id> 2..5` and its `yield`s stop switching away: a task waiting on a semaphore (`semWait`'s busy path is literally `lda #SYS_YIELD / jsr syscall / jmp semWait`, `semaphore.inc:34`) polls instead of blocking, re-entering itself through a full context save/restore — REU stack DMA, ZP shadow, `kArg`, cursor state — on every retry.

### Root cause

`switchTask` decides whether to advance or to re-enter the same task from a **per-task slice counter**, and both the timer preemption *and* a voluntary yield reach it:

```asm
    ldx curTask
    dec taskSliceLeft,x
    bne svSameTask          ; slices remain → restore the SAME task
    lda taskPriority,x
    sta taskSliceLeft,x
    jsr findNextAlive
```

"Stay on the same task while slices remain" is exactly right for a *preemption* and exactly wrong for a *yield*, which is a request to leave **now**. With priority 1 (the default) the counter sits at 1, the decrement lands on 0 and the yield behaves as documented — so the defect is invisible until someone raises a priority. `sysSleep` had already hit the same trap and solved it the same way (`lda #1 / sta taskSliceLeft,x` before switching), which is why sleep always blocked correctly.

### Fix

`sysYield` sets the counter to 1 before `jsr switchTask`, so the decrement lands on 0 and the switch is forced:

```asm
    ldx curTask                 // the caller's X/Y are already on the frame
    lda #1
    sta taskSliceLeft,x         // force switch-away
    jsr switchTask
```

The behaviour is bounded either way — the timer still rotates a spinner away after its quantum, so this was never a deadlock — but it turned cooperative waiting into busy work, and it made `yield` violate its own documented contract ("voluntarily give up the remainder of the current timeslice").

**Cost, and the relocation it forced.** The fix is 7B in `syscall.inc`, i.e. in the `$0fcc-$13be` core block — and that block had **3 bytes** of room, because `warmBootReset` had been parked at the bottom of the core's growth headroom the day before. The build failed with a block overlap. Rather than hunt for 7 bytes, 10 were found and the routine was moved:

- **Page-aligning `REU_KARG_BASE` (`$9680` → `$9700`)** removed 5B from each of the two kArg-shadow address computations in `scheduler.inc`. With the base's low byte zero and `MAX_TASKS × 8 ≤ 256` (both now asserted), `task × 8` *is* the low byte and the high byte is a constant — so the `clc / adc #<base` before the store and the carry-propagating `adc #>base` after it are simply deleted. 10B, and the carry chain that had to be reasoned about is gone.
- **`warmBootReset` moved to `$181D`** (25B of the 35B run between `spawnCore`'s relocated half and `spriteOwnerGate`), which hands the core band **21B** of growth room instead of 3B. It is the ideal tenant for a move: one re-pointed `jmp`, no other references.

Net: free total 237 → **240B** across 18 pockets, with the core's headroom restored and now pinned by `verify-map.ps1`. Byte-verified in the PRG: the 12-byte kArg form (`8A 0A 0A 0A 8D 04 DF A9 97`) at both sites (`$0B06`, `$0D8B`), and the force-switch (`A6 FD A9 01 9D B1 40`) at `$1228`.

**Lesson:** a routine that shares a decision path with something else inherits that path's assumptions. `yield` and the timer preemption are the same nine instructions; the counter that serves one silently defeats the other. And a growth pocket is not storage — parking code at its bottom is how a band loses the room that its next change will need.

---

## Bug #92: A Warm Reboot Did Not Reclaim the Libraries 🟡 ✅ FIXED

**Severity:** Medium — a reboot did not return to the state a reboot means: the library tables and every patched call entry survived it.
**Status:** ✅ FIXED 2026-09-12, **verified in VICE** the same day — `libResetAll` (`src/kernel/lib-reset.inc`) is now the warm-reboot entry point
**Location:** `src/kernel/scheduler.inc` (`findNextAlive`'s "no alive tasks" reboot) — it jumped straight to `start`

### Symptoms

Exit the last shell (which warm-reboots the OS) and run `pool`: it still lists a library that is no longer loaded. Found while testing the Bug #91 fix — `kill sidplay` left `sid.lib` pinned at refcount 0 (see #91), and the warm reboot that followed listed it anyway.

### Root cause

`start:` re-initialises the pool bitmap, the dyn tables, the residency records, the surfaces and the sprite state — but it does **not** touch the library system, because the library system is *kernel data*, and kernel data survives `jmp start`:

```asm
    // no alive tasks
    jmp start        ; ← the library tables were left exactly as they were
```

Two consequences, and the second is the dangerous one:

1. **No reclaim.** `libStartPage`/`libPages`/`libRefCount` kept their pre-reboot values, so a library the reboot was supposed to sweep up (in the report: one stranded at refcount 0, which no release path will ever decrement) stayed listed in `pool` and held its pool pages.
2. **A stale library is a live grenade.** `libFree`'s job includes re-pointing every `libJumpLo/Hi` call entry back at its **cold stub**. Those entries are absolute `jmp`s into the library's pool pages, and `start:` clears the pool bitmap — so after a warm boot the fresh bitmap called those pages **free** while the entries still pointed into them. The next task loaded there is entered through a library call site — i.e. into its own code from the wrong entry point — with no cold stub left to trap it. A silent memory-corruption class, not a cosmetic leak.

### Fix

A warm boot now goes through `warmBootReset` (`$181D`, `src/kernel/warm-boot-reset.inc`), which drops the sprite intent, walks every lib id calling the loader's own `libFree` (pages back, entries re-pointed at their cold stubs, `libStartPage = $FF`) and clears the refcount, then does the `jmp start` the reboot used to do:

```asm
warmBootReset:
    lda #0
    sta sprShEnable             // the sprite intent does not survive a reboot either
    ldx #MAX_LIBS-1
lraLoop:
    txa
    pha
    jsr libFree                 // X = lib id: pages back, cold stubs re-pointed
    pla
    tax
    lda #0
    sta libRefCount,x           // ownership goes with the pages
    dex
    bpl lraLoop
    jmp start                   // the reboot itself
```

It walks **every** id rather than only the ones with a nonzero refcount, because the case it must also repair is precisely a *loaded* library sitting at refcount 0 (#91). `libFree` returns immediately when `libStartPage = $FF`, so never-loaded ids cost two instructions and nothing can be freed that the library does not own. It runs in an IRQ context (the scheduler), so IRQs are already off — which is what `libFree` wants — and no task switch can land mid-walk. Because it is reached by **re-pointing the reboot's existing `jmp start`**, the call costs the core band nothing. *(It was first placed at `$13B8`, the bottom of the core's growth headroom; Bug #93 moved it to `$181D` so that pocket could go back to being headroom — see that entry for why.)* That first placement is worth recording for the second reason too: putting it in `init.inc` instead would shift `osName`/`osVersion` by 3 bytes (breaking two task-side alias asserts) and eat 6 bytes of the sprite band's deliberate relocation slack at `$0FC0`. Both are documented "leave alone" regions; nothing here touches either.

Cost: free total 267 → **247B across 19 pockets** at the time (the pocket then held the routine). Bug #93 moved `warmBootReset` to `$181D` the same day and page-aligned the kArg shadow, so the current figures are **250B across 18 pockets** (240B until 2026-09-13, when `sysThreadSpawn` stopped clearing `kArg+6`/`kArg+7` so a thread can declare a ZP window, returning 10B to the core band) with the core's growth headroom restored; `tools/verify-map.ps1` pins the totals and both pockets.

**The sprite half of the same class landed the same day** (see #89's follow-up): `sprShEnable` is the one sprite byte the per-frame gate refuses to destroy, so a reboot is the only thing that may clear it — and it must, because otherwise the next task to wear the dead task's id inherits its sprites. `warmBootReset` zeroes it (5B). The routine was renamed from `libResetAll` and the file from `lib-reset.inc` to `warm-boot-reset.inc`, because it is no longer only about libraries. Current state: `warmBootReset` is 25B at `$13B8-$13D0` (20B libraries + 5B sprite intent), the gate is 47B (its pocket now exactly full), and the free total is **237B across 18 pockets**.

**Lesson:** a reboot is a *re-initialisation contract*. Anything the OS keeps in RAM that the reboot does not reset is by definition in the state the previous session left it in — and if it holds absolute pointers into memory the reboot re-issues as free, that is not a stale counter, it is a stale code path.

---

## Bug #91: `kLibHold` Released the Library It Was Told to Keep 🟡 ✅ FIXED

**Severity:** Medium — `sidplay`'s tuning pages never came back, `sid.lib` could never be freed again, and the feature's whole purpose (releasing `filesys.lib` early) silently did nothing.
**Status:** ✅ FIXED 2026-09-12, the same day the routine was written — a regression introduced by the change that added it
**Location:** `src/kernel/lib-hold.inc` (`kLibHold`, stub `$FFE6`)

### Symptoms

Run `sidplay <tune>`, let it play, `kill` it: `sid.lib` stays resident (and can never be released afterwards). Meanwhile the hold had no effect on the pacing it was written for, since `filesys.lib` was never given back either.

### Root cause

`dlLibRelease` walks the mask **stored in the task's dyn slot** (`dynTaskLibMaskLo/Hi,y`). The first version of `kLibHold` stored the kept id as that mask *before* calling it:

```asm
    lda bitMasks,y
    sta dynTaskLibMaskLo,y      ; mask := {sid}   ← sets up the WRONG walk
    jsr dlLibRelease            ; → releases sid, not the others
```

So it decremented **sid** (1 → 0 ⇒ `libFree`: sid.lib unloaded, its entries restored to cold stubs) and never touched `filesys.lib`. The next `kSidPlay` call reloaded sid.lib through its cold stub, and that reload is credited to nobody — refcount 0 — which the release path deliberately refuses to decrement below (Bug #78's guard). sid.lib was therefore pinned for the rest of the session, and every `run sidplay` also leaked one `filesys.lib` reference.

The header comment stated the intent ("narrow to the kept id") accurately — which is precisely why the code looked right.

### Fix

Three steps, in this order, all inside the SEI window: clear the kept bit in the stored mask (`and ~keep`), call `dlLibRelease` so the walk sees exactly the others, then store the kept bit alone. Both things the call clobbers — the dyn slot in Y and the keep bit — go on the stack, because it also clobbers `dlLibTmp`/`dlLibTmp2` (the obvious scratch). Verified in the built binary: `BD 3D 45` (`lda bitMasks,x`) is followed immediately by `49 FF` (`eor #$FF`) with **no `tya` between them**, and the `tya`/`pha` that stash the slot come *after* the mask store.

**The second attempt still failed, and for a reason worth recording.** The first fix stashed the slot with `tya` *before* the `eor`, so the complement was applied to the **dyn slot** rather than the keep bit — the release then walked a garbage mask (all bits, for slot 0), decremented every library that had a reference, freed `sid.lib` early and left it pinned at refcount 0 via its cold-stub reload. Identical symptom, opposite cause. Feeding the release a *one-shot override mask* (so `kLibHold` never has to rewrite the task's declaration) would have avoided the whole class — but `dlLibRelease`'s band is **exactly full**: the +24 bytes failed the build with `dynload fits before font-help ($5340)=0` and a memory-block overlap, so the fix has to live in `kLibHold`'s own pocket.

Cost: the routine is now **42B, filling the `$1D16-$1D3F` pocket exactly** (free total 271 → 267B across 19 pockets, one pocket fewer because this one is now full). The `cmp #8 / bcs` guard became `and #$07`: four bytes the pocket did not have, and a bad id alias-maps onto a valid bit rather than reading past `bitMasks` (8 bytes) — there is no failure return any more, which the call sites and docs now state.

**Lesson:** a wrapper that "just calls the library system's own release" is only correct if it hands that call the state the call actually reads — and if it *changes* that state, it must restore or re-establish it before returning. Both failed versions had the arithmetic right on paper and the registers wrong in the instruction stream, so the fix was verified by decoding the emitted bytes, not by reading the source.

**Follow-up (2026-09-19): the input is now a keep MASK.** A task can need two
libraries at once -- a sprite demo keeps `sid.lib` for its music child and
`sprite.lib` for its per-frame calls -- and "keep one id" cannot express that.
`A` is now a mask (bit n = library n), which also removed the `and #$07` guard and
both `bitMasks,x` lookups: the routine is 33B, and the pocket's remaining 9B are
held as an inert `.fill 9, $ea` sled so the end address and the pinned map totals
do not move. See Bug #119.

---

## Bug #90: Two Concurrent Sprite Tasks Silently Share the Register Set and the Art Slots ✅ RETIRED (2026-09-19)

> **RETIRED 2026-09-19 by per-task sprite state.** The register set is still one global
> resource, but it is no longer *shared*: the kernel keeps exactly one task's sprite state
> live — the display owner's sheet records, 39-byte shadow and frame indices (71 bytes) —
> and `sprInstall` swaps them, art included, when the display changes hands. Each task's
> copy waits in its own 128-byte REU block (`REU_SPR_BASE + slot × 128`), zeroed when the
> slot is reused, so nothing is inherited and nothing can be corrupted by a neighbour.
> There is no allocation, no claim, no `? sprites busy` startup guard and no
> single-instance rule: **`run godzi` beside `run sprdemo` is the acceptance test**.
> The calls are `sprite.lib` (lib 7, four entries); `kSpriteCollide` stays resident.
> The analysis below is kept as the record of why masking could not solve it — *which is
> exactly why the fix is a swap rather than an ownership rule*.

**Severity:** Medium — no crash and no error message: both tasks keep running and both screens render the wrong thing.
**Status:** ✅ RETIRED 2026-09-19 — see `docs/lib-sprite.md` and `docs/kernel.md` → *Sprites*
**Location:** `sprite.inc` — `rasterCommit`'s unmasked control-byte publishes, and the eight fixed `SPRITE_SLOTn` art blocks that `kSpriteSheet`/`kSpriteFrame` load **in place**

### Symptoms

Run `sprdemo` and `godzi` at the same time and switch between their screens: each shows the other's sprite *definitions* (frame art) and width/height *expansions* — `sprdemo`'s balls pick up `godzi`'s MC mode and expansions, or `godzi`'s two layers render with the balls' art and mode.

### Root cause

Two global resources, neither of which the ownership gate can mask:

1. **The control bytes are published unmasked, and they are per-slot bitmasks.** `rasterCommit` applies `sprPubMask` to the enable only:
   ```asm
       lda sprShEnable
       and sprPubMask              ; the ONLY masked publish
       sta $d015
       lda sprShYExp;     sta $d017   ; unmasked — bitmask over all 8 slots
       lda sprShPriority; sta $d01b   ; unmasked — bitmask over all 8 slots
       lda sprShMc;       sta $d01c   ; unmasked — bitmask over all 8 slots
       lda sprShXExp;     sta $d01d   ; unmasked — bitmask over all 8 slots
       lda sprShMc0;      sta $d025   ; unmasked — global, not per-slot
       lda sprShMc1;      sta $d026   ; unmasked — global
   ```
   `sprShYExp`/`sprShXExp`/`sprShMc`/`sprShPriority` are **one byte each covering all eight slots**, so the last writer sets them for every slot.
2. **The art is shared while the pointers are static.** `kSpriteSheet` DMAs a sheet into the 64-byte block of the slot **the caller names**, and `sprPtrBytes` pins slot *n*'s `$07F8` pointer to block *n*. Both tasks name slots 0/1, so the second load replaces the first's frame data in place — hence "definitions leak".

The focus gate (`spriteOwnerGate`) only decides *whose* sprites are published, and **a slot bit cannot be split between two tasks**, so no ownership rule can separate them. Bug #89 made the gate expire a dead owner; that is a different failure and is fixed.

### Resolution — declined, then documented

Making this correct needs per-task sprite state: allocated disjoint slot runs, per-task index/mask translation through the whole API (`kSpritePos`, `kSpriteSheet`, `kSpriteFrame`, `kSpriteReg`, plus right-shifted masks out of `kSpriteCollide`), a new header field, a slot bitmap with liveness validation and release hooks on exit/kill/evict. A plan was written and **withdrawn at the user's direction on 2026-09-12** as far more expensive than the limitation justifies.

What was done instead:

* Both sprite tasks now **check for an existing owner at startup** and refuse loudly: `lda sprShadow + SPR_SHD_ENABLE / beq free` — one published byte (the shadow's enable) answers "is anybody driving sprites". On a refusal they borrow the spawner's surface (`gettid` → `taskParent` → `kShareScreen`) to print `? sprites busy`, then exit — a silent exit reads exactly like a crash.
* `sprdemo` takes the **one sound exception**, deliberately, as the worked example: another *sprdemo* shares identical definitions (one sheet, `ball.spr`, loaded once and never re-resolved mid-run), so `sprdClaim` compares the recorded owner's **name** with its own via `kGetTaskName` and allows a match. Its drawing loop also re-asserts the claim once per frame (`kSpriteReg` re-stamps `sprShOwner`), because the kernel zeroes the shared enable when the owner dies — without that, killing one instance would darken the survivor. It stays re-runnable (`flags = $21`) on that basis.
* `godzi` refuses **everyone**, including a second `godzi`: its two looks are different art in the same slots and switchable mid-run, so it cannot satisfy the precondition. It stays single-instance (`flags = $09`) and checks the enable byte alone.
* The rule and the exception (with the fixed-sheet precondition) are documented in `docs/kernel.md` (Sprites), `docs/programmers-guide.md` (Sprites → "One sprite task at a time", plus *Limitations to design around*), `docs/writing-relocatable-tasks.md` (the flags table), `docs/quick-start.md` (the demo list), `src/tasks/sprite-shadow.inc` (rule 3, the task-side ABI) and `.github/copilot-instructions.md`.

### Residual

The check is a **guard, not a lock**: it cannot see a task that has sprites but has never enabled any (it never wrote the shadow), so two tasks started in the same instant can both pass it, and a task killed inside the one frame the gate needs still looks busy (a spurious refusal — the safe direction). Making it airtight needs the per-task sprite state of the declined plan. The same-task exception has its own precondition to respect: **a task may only allow another instance of itself if its definitions never change while it runs** — resolve the sheet once and never swap it; a task with runtime art switching must refuse unconditionally (that is exactly why `godzi` does).

**Lesson:** a register set that is global *by hardware* — per-slot bitmasks packed into one byte, art fetched from fixed blocks — cannot be made per-task by masking the publish. Masking the enable hides sprites; it does not give them an owner.

---

## Bug #89: A Recycled Task Id Made a Dead Task's Sprites Reappear 🟡 ✅ FIXED (superseded 2026-09-19)

> **SUPERSEDED 2026-09-19.** The gate this entry describes is gone: sprite state is now
> keyed by **slot and ownership of the display**, not by an id compare, and a recycled
> slot's block is zeroed at spawn (`clearSlotSprite`). A successor therefore has nothing
> to inherit and no id to mistake for its own — the whole failure class is structurally
> impossible. Kept as the record of the id-recycling trap

**Severity:** Medium — kill `sprdemo`, later run anything that acquires the screen (e.g. `editor`), and the dead task's sprites are drawn frozen at their last positions.
**Status:** ✅ FIXED 2026-09-12 (`spriteOwnerGate` tests the owner's liveness **before** the focus compare and expires the claim — 47B, the pocket exactly full)
**Location:** `sprite.inc` `spriteOwnerGate` (`$1840`), against `spawnCore`'s slot allocator (`syscall.inc`)

### Symptoms

`run sprdemo`, let it draw, kill it, then `run editor`. The editor's screen shows sprites where `sprdemo` left them — not moving, because nothing updates them.

### Root cause

The gate published the shadow only while `sprShOwner == kbFocus`, and that is a **task-id** compare — but task ids are recycled: `findFreeSlot` hands the lowest DEAD slot to the next spawn. `sprdemo`'s slot was freed on kill while `sprShOwner` still held its id, so the successor inherited the id, matched the compare, and the commit published the dead owner's `sprShEnable` together with its stale `sprShReg`/`sprShXHi` positions — sprites on a screen that never asked for them.

The gate's own comment claimed this could not happen ("a task killed while unfocused leaves no stale mask behind"). That reasoning held only while ids stayed unique.

### Fix

The liveness test must run **before** the focus compare, not behind it:

```asm
spriteOwnerGate:
    lda sprShOwner
    tax                         ; X = the recorded owner id
    bmi sprGateDead             ; $FF = "no owner" (no real id has bit 7 — this
                                ;  also keeps the taskState read in range)
    lda taskState,x             ; 0 = DEAD
    bne sprGateLive
sprGateDead:
    lda #0
    sta sprShEnable             ; the intent dies with its owner
sprGateOff:
    sta sprPubMask              ; publish nothing…
    sta sprCollSs               ; …and drop the evidence
    sta sprCollBg
    rts
sprGateLive:
    cpx kbFocus
    beq sprGateOwn
    jmp sprGateOff              ; live, not displayed: mask only
sprGateOwn:
    lda #$ff
    sta sprPubMask
    rts
```

The byte that matters for the *commit* is `sprShEnable`: it is what the commit publishes, and it is the task's *intent*, so a successor must not inherit it. Live owners are untouched — only *publication* is masked, so handing the screen over and back still restores the sprites (`docs/kernel.md`, "Focus policy and the shadow ABI").

**The follow-up that made it complete (2026-09-12): the CLAIM is expired too, not just the pixels.** Zeroing the enable fixes what reaches the VIC, but `sprShOwner` is also *published to tasks* as `SPR_SHD_OWNER` — the byte the guide's recipes read (`sprdemo`'s per-frame re-assert, `sprdClaim`'s owner lookup, any single-instance guard written the same way). Leaving a leftover id there is only harmless to the *commit*: a task that wears the recycled id passes its own compare, concludes the claim is still held, skips re-claiming and stays dark. So the gate now stores `$FF` — the one value no task can wear, since 0 is a real id — and `$FF` is thereby the documented meaning of "free". Cost: 5B, which filled the gate's 47B pocket exactly.

**Why not simply clear the claim where the task dies (the obvious fix)?** Because there is no single death site. `sysExit` and `sysKill` both funnel through `transferFocusAway`, but **pool eviction kills tasks through `dlEvKillTask`** (`dynload.inc:1437`), which never calls it — and `dlEvKillTask`'s band is exactly full, so a `jsr` to a shared release routine would not fit there either. The per-frame gate covers every death path, including ones added later; a death hook would have covered the two that could be reached. That is also why the mechanism stays in the gate rather than moving to the focus paths.

**The first attempt at this fix failed, and the reason is worth keeping:** it tested liveness *behind* the compare (`sprGateLive`), i.e. only while `sprShOwner == kbFocus`. But killing a focus-holding task moves `kbFocus` away (`transferFocusAway`), so the compare failed from that frame on — the gate took the early return, the claim was never expired, and by the time `editor` spawned the recycled id was ALIVE again. A per-frame test must run before the branch that would skip it.

### Residual — narrow, low severity, deliberately unfixed

The gate sees the death on the next timely `rasterCommit` (~20 ms: it runs from the VIC raster IRQ, once per displayed frame). A successor that takes the id **and** the display inside that window is never seen as stale — and never will be, because it is ALIVE and owns the display, so the liveness test passes *legitimately* and the dead task's `sprShEnable` is published with its stale positions. ⚠️ So the effect is not a one-frame glitch: **it persists** until something writes the shadow again. Polling for a transition cannot see a transition whose evidence the successor overwrote.

**Severity, in proportion:** reaching it needs a *programmatic* spawn plus a display grab inside one display frame of the owner's death. `run` cannot do it (the REU load and fixup pass outlast the window), so it is not reachable from the shell at all; a task already running in a `threadSpawn`-style loop could, and no shipped task does. If hit, the damage is **cosmetic plus one false refusal** — frozen sprites on one screen, and the sprite registry still looks owned so a new sprite task prints `? sprites busy`. No corruption, no crash, no kernel state lost. That is the same severity class as the one-sprite-task limitation (Bug #90), and it is documented rather than fixed for the same reason.

**The exact fix (identified, deferred):** a per-slot **generation counter** — `findFreeSlot` bumps `taskEpoch[slot]` when it hands the slot out, the claim stores `(id, epoch)`, and the gate treats an epoch mismatch as "not my claim". A recycled slot can then never be mistaken for the original owner, however fast the reuse. Costs: 24 B for the table (storage exists, e.g. the 31 B run at `$02A1-$02BF`), 1 B for the epoch beside the claim (the shadow pocket has 7 B free), ~3 B to bump it in `findFreeSlot`, ~7 B for the compare in the gate. The last one is the blocker: **the gate is 47 B in a 47 B pocket and both neighbours are pinned** (`spawnCore`'s relocated piece below, `taskLo`/`taskHi` above — 48 B, and no free run in the map is that large). So closing this is a relocation decision, not a byte count — not worth it for a cosmetic residual.

### Follow-up (same day, found by the user): the A register

The restructured gate introduced a *second* bug, and its symptom was "always the same sprite leaks onto the switched screen" with a single sprite task running (`run sprdemo`, then F1 to the shell):

```asm
sprGateLive:
    cpx kbFocus
    beq sprGateOwn
    jmp sprGateOff          ; ← before the fix: A still holds `taskState[x]`
sprGateOff:
    sta sprPubMask          ; ← and this tail *assumed* A = 0
    sta sprCollSs
    sta sprCollBg
```

The liveness test left `taskState[x]` (= `TA_ALIVE` = `$01`) in A, and the shared tail stored it: `sprPubMask = $01`, so the commit published `sprShEnable & $01` and **VIC sprite 0 stayed enabled on every screen that was not the owner's** — always the same slot, hence always the same sprite. The same `$01` landed in both collision latches, so every task's first `kSpriteCollide` also reported a collision that never happened. The monitor dump that named it: `sprPubMask = $01`, `sprShEnable = $FF`, `sprShOwner = 02` (sprdemo), `kbFocus = 01` (shell), `$D015 = 01`, `sprCollSs/Bg = 01 01`.

Fix: `lda #0` before the `jmp`, plus a comment on the tail stating that A must be zero on every path into it. Cost: +2B (the gate is 42B of its 47B pocket; free total 271B).

**Lesson:** the first version of this gate stored `#0` explicitly at each site; collapsing those into one shared tail created an *implicit* precondition (A = 0) that one of the two paths silently violated. When a tail is shared, the value it depends on must be established where the tail is entered — not inherited from whatever the previous test left behind — and the invariant belongs in a comment at the label.

**Lesson:** an id is a handle, not an identity — never treat a recycled index as proof that state still belongs to whatever set it.

---

## Bug #88: A Task Spawned by Another Task Shared the Wrong Surface — Its Output Went Nowhere 🟡 ✅ FIXED

**Severity:** Medium — the second instance of a task pair ran perfectly and printed nothing anywhere, which reads exactly like a crash.
**Status:** ✅ FIXED 2026-09-12 (the pattern is now documented in `kernel.md` and `programmers-guide.md`; the proof task that hit it was temporary and has been removed)
**Location:** task code that shares a surface — the `gettid` → `taskParent` → `kShareScreen` recipe

### Symptoms

A two-instance task (`semtest`, the throwaway proof for the re-instated semaphores) printed its report from the instance the shell started, and **nothing at all** from the instance that one had loaded — "i never see sem 3". A print at entry changed nothing, yet the first instance's own measurement proved the second had run: its `semWait` waited **4 frames**, which only the partner holding the lock can cause.

### Root cause

The recipe every `.com` uses to get its output onto the shell's screen is:

```asm
    lda #SYS_GETTID
    jsr syscall
    tax
    lda taskParent,x            ; the shell
    tax
    jsr kShareScreen
```

That is correct **only while the parent owns a surface**. The second instance's parent is the *first instance*, which owns nothing — it merely shares the shell's surface — so `kShareScreen` was handed a task with no screen, returned `C=1`, and the task's own "cannot report on anything" path (`SYS_EXIT`) made it vanish silently before it printed a character.

`taskParent` answers *who spawned me*, not *who owns the display*. For a one-level relationship the two coincide — which is why every command and every `run`-loaded task works — and they diverge the moment a task loads a task.

### Fix

Walk up the ancestor chain to the task that actually owns a surface, knowing how far to walk. In the proof the depth was known from the argument the loader passed (the shell is the primary's parent and the secondary's grandparent):

```asm
    ; 1 level for the primary (parent = shell), 2 for the secondary
    ldx semTid
semGenLoop:
    lda taskParent,x
    cmp #$ff
    beq semNoScreen              ; ran out of ancestors — nothing owns a surface
    tax
    dec semGen                   ; generations left to walk
    bne semGenLoop
    jsr kShareScreen             ; X = the surface owner
```

A general task should **probe** rather than count: walk up while `screenAlloc[slot]` is zero, and treat `$FF` or exhaustion as "no surface".

### Lessons

1. **`taskParent` is not the display owner.** The share recipe in the docs is a *one-level* idiom, and it now says so.
2. **A silent exit is indistinguishable from a crash.** The "no surface ⇒ exit" path was right in spirit and invisible in practice; a diagnostic task must never have one.
3. **Two observables beat one.** The instance that *could* print also measured the other's effect (a 4-frame wait), which is what proved the partner was alive while it printed nothing.

---

## Bug #87: `ipc.lib` Handed the Consumer a Slot That Had Never Been Written 🟡 ✅ FIXED

**Severity:** Medium — the new IPC pipe shifted every byte by one slot, so no
consumer could ever see a correct stream (caught by the proof before anything
shipped on it).
**Status:** ✅ FIXED 2026-09-12 (`src/libs/ipc.asm` — one instruction; needs a task + REU rebuild)
**Location:** `src/libs/ipc.asm` — `ipcPost` (the write index)

### Symptoms

The two-task proof (`ipcpro` posting `0…255`, `ipccon` taking 256 bytes with one
tick of sleep per byte) reported, on two consecutive boots, byte-identical
results:

```
IPC PROOF BAD
saw=00 00 01 02
ok=0100          (lo,hi → okLo=$01, okHi=$00 → only 1 of 256 matched)
got=0001         (lo,hi → 256 taken)
exp=01 recv=00
```

`got` said all 256 bytes arrived, `ok` said exactly **one** of them matched the
expected counter, and the first four bytes seen led with a **duplicated `00`**.

### Root cause

The write index and the read index meant different things. `ipcPick` consumed
`ipcRing[ipcTail]` and *then* advanced `ipcTail`, so `ipcTail` named the slot to
read — but `ipcPost` stored to `ipcRing[(ipcHead+1) & 127]` and set `ipcHead` to
that same value, so `ipcHead` named the slot *just written*. The reader was
permanently one slot behind the writer, and its first pick returned slot 0,
which had never been written and still held the `.fill` zero:

| operation | `ipcHead` | `ipcTail` | write | picked |
|---|---|---|---|---|
| post `0` | 1 | 0 | `ring[1] = 0` | |
| post `1` | 2 | 0 | `ring[2] = 1` | |
| … post `126` | 127 | 0 | `ring[127] = 126` (full) | |
| pick 1 | 127 | 1 | — | `ring[0]` = **0 (never written)** |
| pick 2 | 127 | 2 | — | `ring[1]` = 0 |
| pick 3 | 127 | 3 | — | `ring[2]` = 1 |
| pick 4 | 127 | 4 | — | `ring[3]` = 2 |

That is the phantom `00` in front of a stream that is otherwise one slot late,
which is why every byte after the first was compared against a value one ahead
of it — 1 match in 256, exactly as reported.

### Fix

Write at `head`, not at `head+1` — `ldx ipcHead` where the code had `tax`:

```asm
    lda ipcHead
    clc
    adc #1
    and #IPC_MASK                // the slot AFTER head: the one that stays free
    cmp ipcTail
    beq ipcPostWait              // no room: head+1 == tail
    ldx ipcHead                  // write at HEAD — head is the next slot to fill
    pla
    sta ipcRing,x
```

Both state tests are unchanged: `head == tail` is still empty,
`(head+1) & 127 == tail` is still full, and the usable capacity is still 127
bytes. The convention is now documented in `ipc.asm`'s header and in
[`lib-ipc.md`](lib-ipc.md).

### Why it took two runs — the first diagnosis was wrong

The duplicated first byte was first blamed on **two producers** sharing the ring
(the proof tasks were `KF_RE_RUNNABLE`, and a second `run ipcpro` had been
suggested by hand), and the tasks were switched to single-instance to rule it
out. That change is a correct design decision — a one-producer pipe *should*
refuse a second copy — but it was not the cause, and the second run printed the
identical four bytes.

The tell was in the first run already: **a genuine two-producer interleaving
would differ between boots.** Byte-identical output is a code path. One
producer, one consumer and a fixed index convention produce a fixed wrong
stream, which is what `00 00 01 02` on two separate boots meant.

### Lessons

1. **Both indices of the pair must be the same kind of thing.** "The slot I will
   use next" on one side and "the slot I used last" on the other is an off-by-one
   that only shows at the seam — the very first read.
2. **A correct *count* can mask an ordering fault.** `got=256` was right, so the
   transport "worked"; only `ok=1` exposed that the *stream* was wrong. A count of
   bytes is not evidence that they arrived in order.
3. **Identical failure output across runs ⇒ deterministic bug.** Reach for the
   code path, not the race.

### Verified

`ipclib: 212 bytes of code, 15 fixups`, relocation verified, 19 asserts; the
proof then read `IPC PROOF OK`, `saw=00 01 02 03`, 256 taken and 256 matched
(2026-09-12). The proof tasks were removed afterwards; [`lib-ipc.md`](lib-ipc.md)
is the record. See also Bug #85 — the same "the proof is the feature" lesson for
`sid.lib`.

---

## Bug #86: `godzi` Retried a Refused `sidplay` Spawn Forever and Lost the Handle 🟡 ✅ FIXED

**Severity:** Medium — the demo could end up with no music while a `sidplay` ran
untracked, and could act on a player it never started.
**Status:** ✅ FIXED 2026-09-12 (`src/tasks/dynamic/godzi.inc`; needs a task + REU rebuild)
**Location:** `src/tasks/dynamic/godzi.inc` (`gzMusic` / `gzMusicStart`)

### Symptoms

*"when sidplay is running it tried to start it again and despite it not being
rerunable I can see two sidplay tsks in ps."* — the demo retrying a refused spawn
was real; the two-row part was a misreading of `ps`, confirmed by the user on
2026-09-12.

### Root cause

`sidplay` is single-instance, so `kDynLoad` refuses a second copy with "already
running". `gzMusicStart` treated *every* failure the same way — drop the handle
and retry on the cadence:

```asm
    jsr kDynLoad
    bcc gzMusicUp
    lda #GZ_NO_SID
    sta GZ_SID                    ; forget the slot
```

For "already running" that is wrong twice over:

- **It forgot a running player.** `GZ_SID` went back to "nothing playing" while
  the real instance kept running, so the demo had no music *and* no handle on the
  player it had started.
- **It retried a refusal on the cadence** (`GZ_ANIM` wraps once per 40 frames,
  ~0.8 s). A refused `kDynLoad` is not a transient error, so the attempt could
  never succeed while that player lived — visible as "it tried to start it again".

**Not reproduced.** The two-row observation was a misreading, so no second live
copy was ever involved — `ps` skips `TA_DEAD` and lists only live tasks, which is
worth remembering before reading a duplicate into its output. The defect worth
fixing here is the one above: a refused spawn dropped the handle, and a refusal
is not transient, so it was retried forever with no music.

### Fix

`sidplay` is the single instance, so the demo now **looks for a running player
and adopts that slot** instead of asking for another copy — but only when the
player is its own:

- `gzFindSid` scans `taskState`/task names for a running `sidplay`.
- `gzOwns` compares that task's `taskParent` against the demo's own slot
  (`GZ_SELF`, captured once in `gzSetup`), so a `sidplay` the *user* started from
  the shell is never adopted, watched or killed (Rule 13). The cadence keeps
  trying, and once that player is gone the demo starts its own.
- `gzSidStop` (mode switch, and `q`) applies the same ownership gate before
  `SYS_KILL` — it can only stop a player this demo started.

**Verified:** builds green (godzi 708 bytes, 34 fixups, relocation verified, 19
asserts). The runtime check is the user's: `run godzi`, `1`/`2` to switch songs,
`q` to leave — and, while its player runs, `run sidplay commodo 2` from the shell
must still be refused with `? already running`.

> **OBSOLETE 2026-09-13 — the whole arrangement is gone, and the lesson is not.**
> `godzi` no longer spawns a player at all: it calls `sid.lib` directly from a
> thread child (the `gortris` arrangement), so `gzFindSid`/`gzOwns`/`gzSidStop`,
> the adopt-the-slot logic and the retry cadence no longer exist. Kept because the
> finding stands: **a refused spawn is not transient**, and a handle you cannot
> validate is not ownership — a spawner that retries on a cadence can retry
> forever with nothing to show for it.

---

## Bug #85: `sidplay` Played Nothing and Exited Silently — the Library Never Called `sidInitSong` 🟡 ✅ FIXED

**Severity:** High — the `.mus` player was completely dead: no music, and no way
to tell why.
**Status:** ✅ FIXED 2026-09-12 (`src/libs/sid.asm`; needs a task + REU rebuild)
**Location:** `src/libs/sid.asm` (`sidPlay`)

### Symptoms

*"run sidplay commodo 2 does not work... nor does it give any error message."*
The task started and vanished within a tick — `ps` never caught it, `pool` never
showed `sid.lib` loaded, `dir` showed the correct 540-byte `sidplay.tsk`, and
other tasks ran fine. Nothing ever appeared on screen.

### Root cause

When the Sidplayer body moved out of `sidplay.tsk` into `sid.lib` on 2026-09-12,
`sidPlay`'s loop began going straight into `sidTick` **without initialising the
song**:

```asm
sidLoop:
    lda sidStop
    bne sidStopNow
    jsr sidplay.sidTick          ; runs against uninitialised Work* state
```

The old all-in-one task called `sidInitSong` *before* its tick loop and again on
a repeat. Moving the body kept the **restart** call (in the repeats path) and lost
the **initial** one, so on a first play `sidTick` found no voices started:

- `SID_STATUS & 7` reads 0, so the loop concludes "the song has finished"
- with `repeats: 1` (the default), `dec sidRepeats` reaches 0 → `sidStopNow`
- the player hushes, releases the SID and returns `C=0` — a *successful* call
  that played nothing — and the wrapper freed its buffer and exited

Neither `sidSongInit` nor any failure path was ever reached, which is why the
first instrumented probes (border colours frozen at each exit) kept pointing away
from the real fault: the player returned "normally".

### Fix

`sidPlay` now calls `sidSongInit` before entering the loop as well as on restart
(3 bytes, 467 fixups). `sidSongInit` is exactly "start (or restart) the song", so
both call sites are the same operation.

### Why it was invisible

Two compounding gaps, both closed:

1. **`kSidPlay`'s carry was ignored.** The wrapper treated `C=1` ("refused", or
   "`sid.lib` failed to load") as success and exited quietly. It now reports it.
2. **A headless task cannot show anything.** `libLoadFail` prints its message to
   the *calling task's* screen, and `sidplay` has no screen — so a genuine load
   failure would have been silent too. The wrapper now takes a screen on failure,
   prints `? tune <name>` / `? load <name>` / `? no memory` / `? no player`,
   waits for a key and hands the display back (edit.inc's `edGiveDisplay` recipe,
   plus Bug #83's rule that the live blink block must be un-drawn, SEI'd, before
   `kSwitchView` takes the display).

### Lesson

When a body moves into a library, move **every** entry-side call with it. An init
that a restart path also performs is easy to lose, because the moved code still
looks complete. And a task that *cannot* report a failure should be made able to
before it is trusted — the silence here is what turned a two-line bug into a day
of diagnosis.

**Verified:** user-confirmed in VICE — `run sidplay commodo 2` plays again, with
both probes removed. The failure reporting itself is new and unverified; the
quick check is `run sidplay nosuch`, which should print `? tune nosuch.mus` on a
temporary screen and wait for a key.

---

## Bug #84: Balls Passed Through Each Other Near x=256 — `sprdClosingX` Compared Only the Low Byte 🟡 ✅ FIXED

**Severity:** Medium — a demo-visible defect: two balls intersecting instead of
bouncing, on the right-hand side of the arena.
**Status:** ✅ FIXED 2026-09-12 (`sprdemo.inc`; needs a task + REU rebuild)
**Location:** `src/tasks/dynamic/sprdemo.inc` (`sprdClosingX`)

### Symptoms

*"On occasion the collisions intersect the sprites."* Intermittent, and only in
the rightmost ~64px of the ball arena — the pair had to straddle x = 256.

### Root cause

`sprdClosingX` gates the reflection ("are they approaching on this axis?") and
decided which ball was on the left by comparing the **low byte only**:

```asm
    lda sprdXL,x
    cmp sprdXL,y
    bcc sprdClxLeft          ; "i is to the left"
```

x is `XH:XL:XF` over 24..320. A ball at x = 262 has `XL = 6`; one at x = 250 has
`XL = 250` — so `6 < 250` reported "i is to the left" when i was 12px to the
*right*. The verdict chooses which velocity sign means "approaching", so for every
pair straddling 256 it inverted:

- approaching pair → judged "not closing" → `bcs sprdHout` skipped the reflection
  → **no bounce at all; the balls passed through each other**, overlapping for
  several frames;
- separating pair → judged "closing" → both velocities negated → they turned back
  into each other and jittered while sliding past.

Every other x reader in the file handles the 9th bit — placement, integration,
`sprdPut`, both wall clamps and the hit test itself (which is why *detection*
always worked and only the response was wrong). This was the third x-width site
missed, after the store/place mismatch and `sprdHdxNeg`'s bound-before-negate
hazard; all three are now listed in the file's header.

### Fix

Compare `XH` first and only fall back to `XL` when the high bytes are equal
(+10 bytes, +2 fixups; `sprdemo` 931 → 941B). The velocity-sign test below it was
already correct — `VxL` carries the sign for any speed under 128 px/frame.

### Verification

User-observed 2026-09-12 (intermittent, right-hand side). VICE re-test needed.

---

## Bug #83: A Display Switch Can Bake the Blink Cursor Block Into the Outgoing Surface 🟡 ✅ FIXED (2026-09-23)

**Severity:** Medium — a cursor-shaped block that is not the cursor, and that
only a screen clear removes.
**Status:** ✅ MITIGATED 2026-09-12 in `sprdemo` (the reliably reproducible
trigger). The **kernel path is still open** for F-key switches — Option A below.
**Location:** `src/kernel/gfx.inc` (`doGfxSwitch` → `surfSave`, `dgsDone`); trigger
sites are `doGfxSwitch`'s callers (`viewScreen`, `switchView`, `transferFocusAway`
×2). Found from `sprdemo`'s `kSwitchView`.

### Symptoms

After `run sprdemo` and switching back to the shell (F-key, or `q`/`kill`),
*sometimes* a cursor-shaped block sits at the cell the shell's cursor was on when
ENTER was pressed. It does not blink in step with the real cursor, and only `cls`
— i.e. writing pixels there — removes it. User-confirmed 2026-09-12: the position
and the `cls` behaviour are both diagnostic (a *state* bug would move the block,
not need a pixel write).

### Root cause

The block cursor is drawn **into the framebuffer pixels**: `kbDraw` XORs the 8
glyph bytes at the cell and sets the matrix nibble to the cursor colour. `surfSave`
copies the framebuffer verbatim (pixels, matrix, colour RAM) into the outgoing
task's REU slot — and **`doGfxSwitch` never un-draws a live block**. `dgsDone`
then clears `cursorVisible`, so the kernel forgets a block is on screen and the
cell can never be restored: the *saved image* now contains the block, so the next
ON phase saves that block as the "original" glyph and every OFF phase writes it
back. When the real cursor later moves on, the block is left behind.

Every other framebuffer writer erases first — `printCharAt`, `blinkResetK`,
`scroll-region.inc` (×2) and `screen-io.inc` (×2) all call `eraseCursor`, and
`printCharAt`'s comment states the rule (*"erases a live blink block first … so
the next blink OFF can't resurrect stale glyph bytes"*). The whole-screen copy
was the one that did not.

The window is about half the blink period (`blinkRate` ticks ON, then OFF), which
is why it is intermittent. It is most reproducible from a **task-initiated**
switch: while the displayed task is not the running task, the blink takes the
`kbTailOn` path (`keyboard.inc:135`) and draws the block from the framebuffer
matrix tail, so the outgoing task's block is on screen right up to the instant
`kSwitchView` runs.

### Mitigation applied — fix B, `sprdemo` (2026-09-12)

The demo erases the live block, SEI-wrapped, before taking the display:

```asm
    php
    sei
    txa; pha
    jsr kBlinkReset        // un-draw a live blink block (no-op if OFF)
    pla; tax
    jsr kSwitchView
    plp
```

`kBlinkReset` (`$E0C0`) erases at the **displayed** surface's saved position — the
shell's, at that moment. The SEI is required: an IRQ tick between the erase and the
switch draws the block straight back, and no syscall may be used inside the window
(the dispatcher re-enables IRQs). X needs the stack because `kBlinkReset` clobbers
A, X, Y, `kPtr` and `kArg+7`. `kBlinkReset` is a jump-table entry, so this costs **0
fixups**; `sprdemo` went 921 → 931 bytes.

### Fix still open — Option A (kernel, the real one)

Add `jsr eraseCursor` at the top of `doGfxSwitch`, before `surfSave`. `toTask`
arrives in X and `eraseCursor` clobbers X *and* `kArg+7`, so X must be stashed in
`kArg+6` around the call (~9-12 bytes at `$EE3B`). Cost is layout, not size: that
address is mid-band, so everything after it shifts up to `$FF7D`, which moves the
pinned gfx.lib ABI scratch (`ffStkXLo $FC3F` … `kEllTemp $FF6F`, `kCircle*`
`$FF70+`) and needs `main.asm`'s asserts + `gfx.asm`'s mirrors +
`kernel-fragmentation.md`/`memory-map.md`, then a task **and** REU rebuild.
Invariant to assert: `cursorVisible = 1` implies the tail describes the *displayed*
surface (`dgsDone`'s reset maintains it).

**Exposure:** the code path is shared by every display-taking task — `edit`,
`basic`, `gfxdemo`, `maze`, `fpdemo` and the shell all call `kSwitchView` — so the
bake is *permitted* anywhere a switch lands inside the blink's ON window. **But it
has only ever been observed from `sprdemo`** (user, 2026-09-12: *"I have not
experienced this problem with anything other than the sprdemo"*). That is evidence
against equal exposure, and it is unresolved. The most likely difference is
*noticing* rather than *occurring*: the baked cell is the shell's cursor cell, and
the shell's first keystroke there restores the character from the char shadow
(`rsFocused`) — so a task you return from and immediately type at heals the block
before it is seen, while `sprdemo` is the one you return to and *look* at. If the
next keystroke does **not** heal a phantom, that explanation is wrong and something
else is going on — see the open question below.

**Open question (2026-09-12):** after a phantom appears, does typing one character
at the shell erase it? Yes → the mechanism is shared and the demo is simply where
it gets seen; no → there is a second cause still to find.

**Exposure:** any display switch made while the outgoing task's block is drawn —
in particular an F-key switch at the shell prompt, which fix B does not cover.

**Kernel path closed (2026-09-23).** The kernel's own switches now un-draw the block before the
framebuffer is copied: `doGfxSwitch` calls `gfxSurfPrep` for the **running** task (the only one whose
block can be in the framebuffer) immediately before `surfSave`, erasing via `eraseCursor` — which
restores at the block's SAVED position and is a no-op when nothing is drawn — then persisting
cursorCol/Row/On exactly as before. Two facts worth keeping:

- **`toTask` moved from `kArg+7` to `kArg+6`.** `eraseCursor` clobbers `kArg+7` (its documented
  clobber list), and `kArg+6` is safe here: `surfSave`/`surfLoad` stop at `kArg+4` (`+5` survives by
  design) and `loadSlotRegion` documents `kArg..kArg+5`.
- **The three cursor stores moved into a pocket, so the chain did not pay for the fix.**
  `gfxSurfPrep` is 16 B at `$CD5C` (1 B left) with a 13 B tail at `$FFE9` (4 B left), reached by
  `jmp` — the same shape as `printGlyphAt`. Kernel free total 201 -> 184 B across 25 pockets.

**Verified:** 228 asserts green, PRG starts `$01 08`, `tools/verify-map.ps1` green with the two new
pocket pins. ⚠️ **Documentation drift:** the code chain shifted, so `docs/memory-map.md`'s section
spans from roughly `$EE3B` upward still need re-deriving from the build's Memory Map block.

---

## Bug #82: Exiting the Editor Could Leave the Shell Rendering into Its REU Slot 🟡 ✅ FIXED

**Severity:** Medium-High — the machine looked frozen: the editor's frame stayed
er put, `run`/`dir` produced no visible output, and only an F-key display switch
(or a reboot) brought the shell back.
**Status:** ✅ FIXED 2026-09-11 (task-side; needs a task + REU rebuild)
**Location:** `src/tasks/dynamic/edit.inc` (`edGiveDisplay`, `edQuit`,
`edInitFail`) — context: `src/kernel/syscall.inc` (`sysExit` →
`transferFocusAway`), `src/kernel/dynload.inc` (`dlRestore`'s `dlrNone`
hand-back)

### Symptoms

After editing a document past 24 rows and exiting, *sometimes* the screen did
not return to the shell, and everything the shell printed afterwards was
invisible (no echo of `dir`, `ver`, …).

### Root cause

`edit` owns the framebuffer while it runs (`kSwitchView` at startup), and
`edQuit` released that surface **before** `SYS_EXIT`:

```asm
edQuit:
    jsr edFreeChunks
    jsr kFreeScreen      // surface gone — screenAlloc/owner/kind cleared
    lda #SYS_EXIT
    jsr syscall
```

`sysExit` marks the task DEAD and then calls `transferFocusAway`. That scan
looks for the first **ALIVE** slot with `screenAlloc != 0 && screenOwner == $FF`;
when it finds none it takes `tfaNone` — **blank the display and set
`kbFocus = $ff`**. Because the dying editor was already surfaceless, and because
the only other candidate (the shell) can be `TA_SWAPPED` at that instant (a pool
eviction — see Bug #81) or outside `totalTasks`, the blank path is reachable on
exit. `surfDisplayed` then reports "not displayed" for the shell
(`resolve(shell) != resolve($ff)`), so every shell write goes to its REU slot and
nothing appears. The existing hand-back in `dlRestore` (`dlrNone`) only fires on
a *restore*, so with the shell already restored nothing re-establishes the
display: the blindness is permanent until an F-key switch or a reboot.

### Fix applied

Hand the display over **while the surface still exists**, before freeing it:

```asm
edGiveDisplay:              // no-op unless we hold the keyboard focus
    lda #SYS_GETTID; jsr syscall
    cmp kbFocus
    bne edGdRts             // not holding the display — sysExit decides
    tax
    lda taskParent,x        // the shell (our spawner)
    cmp #$ff; beq edGdRts
    tax
    lda taskState,x
    beq edGdRts             // parent dead — leave it to sysExit
    jsr kSwitchView         // display + kbFocus move to the parent
```

Called before both `kFreeScreen` sites (`edQuit`, `edInitFail`). Because the
framebuffer decision keys on the **surface** (`resolve()`), this works even when
the shell is swapped out — the display and `kbFocus` are consistent before any
surface is released, so `transferFocusAway` never has to guess.

### Fix applied — part 2: the kernel reclaims an unclaimed display

`edGiveDisplay` alone is not enough, and this was confirmed in the field: it is a
**no-op whenever `kbFocus` is already `$ff`** (the display was blanked *earlier*
— e.g. a task exit while nothing looked ALIVE-owned, or the shell being swapped
at that instant), and in that state the shell stays blind because nothing ever
reclaims an unclaimed display. `transferFocusAway` (`syscall.inc`) now also runs
when `kbFocus = $FF`, whatever is exiting:

```asm
transferFocusAway:
    cpx kbFocus
    beq tfaGo                   // the dying task holds the focus
    lda kbFocus
    cmp #$ff
    bne tfaRts                  // a live focus holder exists — never steal it
    ldx #$ff                    // blank display: no fromTask surface to save
tfaGo:
```

So every `sysExit`/`sysKill` (and `dlRestore`'s `dlrNone` hand-back, which
already called it with X = `$FF`) reclaims the display for the first ALIVE task
with an owner surface — the shell — as soon as anything exits. With
`fromTask = $FF` the save is skipped and only the incoming surface is loaded. A
*live* focus holder is still never stolen (that is what `dlRestore` relies on).

### Verification

Build: `edit: v3, 9209 bytes of code, 471 fixups`, relocation verification OK
(kernel unchanged from Bug #81: 146 asserts, 0 failed).
**Runtime (verified in VICE):** after `run edit <file>` → type past 24 rows →
STOP+Q/X, the shell reappears immediately and echoes commands. Not reproducible
by the reporter after the fix (2026-09-11) — the `kbFocus = $ff` reclaim covers
the case `edGiveDisplay` alone could not.

### Residual / watch items

- `transferFocusAway`'s `tfaNone` still blanks the display when no ALIVE task
owns a surface at that moment. Recovery is `dlRestore`'s `dlrNone` hand-back or
an F-key switch. A generic "re-display when the display is unclaimed and a
surface-owning task becomes ALIVE" belongs in the scheduler/IRQ path and was
deliberately not attempted here.
- **`edit.tsk` is now 9209 of 9216 bytes — 7 bytes below the 36-page boundary.**
Any further growth moves it to 37 pages and changes its pool footprint (and the
Bug #81 layout arithmetic). Check `edit:` in the task build output before adding
code there.

### Lesson

A task that owns the display must hand it over **before** releasing its surface.
"Free then exit" makes the dying task look surfaceless to the kernel's hand-off,
and the fallback path (blank + `kbFocus = $ff`) is a state nothing later
reclaims.

---

## Bug #81: A Failed `run basic` Misfiled the Libraries and Permanently Fragmented the Pool 🔴 ✅ FIXED

**Severity:** High — after one such failure, large loads (`run basic`,
`compact`) refuse with `? no space in pool` **forever**, while `pool` reports a
correct-looking `31 used / 78 free` and killing tasks changes nothing. Only a
reboot heals it.
**Status:** ✅ FIXED 2026-09-11 (kernel-only — no task/REU reseed needed)
**Location:** `src/kernel/dynload.inc` (`allocFromPool` direction, new
`dlAllocHi`, `dlLoadCore`'s two code-alloc sites), `src/kernel/lib.inc`
(`libLoad` → `dlLibCore`), new `src/kernel/pool-top.inc`

### Symptoms / repro

`run edit boot.bat` → type past 24 rows (a second 4-page chunk) → exit →
`run basic` → `? no space in pool`. From then on every `run basic` (and
`compact`) fails, even though `pool` lists only the shell and its two libraries.
Previously reported (and mis-diagnosed) as Bug #78's "stranded pages": the
symptom looks exactly like a leak, but killing tasks cannot release anything
because nothing is leaked.

### Evidence (2026-09-11, user)

| State | `pool` pg values |
|---|---|
| after a boot | shell `0`, `filesys.lib` `19`, `string.lib` `29` |
| after the failure | shell `0`, `filesys.lib` **`59`**, `string.lib` **`77`** |

The libraries **moved** — and a library can only move if it was freed and
re-loaded (`libFree` → `libLoad`). Their only declarer is the shell
(`libMask $0012`), so `libRefCount` could only reach 0 by **evicting the shell**.
`shell pg 0` is the signature of the self-evict path: the shell is restored by
`dlRestore`, whose first-fit lands it back in its own just-freed hole.

### Root cause — two mechanisms

1. **The pre-evict sacrifices the caller to no avail.** `basic`'s footprint is
   `code 65 + eager libs (gfx 26 + fp 8) = 99` pages, which never fits beside a
   resident shell (19) + `filesys` (10) + `string` (2) = 130 > 109, so `basic`
   *must* evict the shell and take its libraries with it. `dlPreEvict` runs when
   the free pages are short; the editor is pinned (`KF_NOT_EVICTABLE`), so the
   only eligible victim is the **calling shell**, and `dlSelfEvictOk` decided
   whether to sacrifice it with `free + dlCallerPages() >= need` — where
   `dlCallerPages()` counted only the caller's **code** pages. With libraries
   parked at the top of the pool that is 78 + 19 = **97 < 99**, so it refused,
   printed `? no space in pool` and evicted nothing — while evicting the shell
   also frees `filesys` + `string` (its refcount-1 libs, 12 pages), i.e.
   78 + 19 + 12 = **109 ≥ 99**, which fits.
2. **Both tasks and libraries were allocated first-fit from the bottom.**
   When the freed libraries re-loaded, first-fit put them in the lowest hole —
   which was *above* the still-resident task (`filesys 59`, `string 77`). After
   the editor exited, the free space was split into **40 / 8 / 30** pages:
   40 < `basic`'s 65-page code block, and the pool can never coalesce again.
   No pages were lost — they were **misplaced**, which is why `pool`'s totals
   (derived from the tables) stayed correct.

This is why Bug #78's refcount fix did not help: that fix made the refcounts
exact, but the damage here is done by the *placement* of a lib that is freed and
re-loaded legitimately.

### Fix applied — libraries allocate from the TOP

One exhaustive search, two placements (`dlAllocHi` selects which end the
candidate walk starts from):

- **lowest fit** (`dlAllocHi = 0`) — task code, program RAM, `kMalloc`. Tasks
grow up from the bottom, so an exiting task's pages always rejoin the
contiguous task region.
- **highest fit** (`dlAllocHi = 1`) — libraries, entered only from `libLoad` via
the new `dlLibCore` wrapper (same size as the plain `jsr dlLoadCore` it
replaced) → `dlAllocCode` → `allocFromPoolTop`. `dlAllocCode` consumes the
flag on success (so a task's program-RAM block still grows up) and leaves it
set on failure (so `dlLoadCore`'s evict-and-retry keeps the placement);
`dlLibCore` clears it unconditionally, so the direction can never leak into a
later task load.

**No pool page is reserved and nothing is taken from the task side:** both
directions walk the same candidate set, so `allocFromPool` succeeds exactly when
it did before — only the placement differs. The new code lives in the **free
kernel pocket after font-help** (`$53F2-$54AA`), not in the pool.

### Fix applied, part 2 — the caller's libraries count (2026-09-11)

Placement alone was not enough: `dlCallerPages` under-counted what sacrificing
the caller frees, because eviction releases the caller's refcount-1 libraries
too. `dlCallerAddLibs`/`dlCallerLibPages` (`pool-top.inc`) now add exactly those
pages: for every lib in the caller's mask with `libRefCount == 1`, add
`libPages` (shared libs are skipped — `dlLibRelease` keeps them). The test then
reads `free + code + liberatable libs`, which is exact rather than optimistic:

| state (user's measurement) | before | after |
|---|---|---|
| free 78 (`shell 0-18`, `filesys 99-108`, `string 71-72`) | 97 < 99 → refuse | 109 ≥ 99 → sacrifice, then load |

Where this shows up without an editor too: `run gfxdemo` → `kill` → `run basic`.
`gfxdemo` leaves a freed `gfx.lib` block (26 pages) between the shell and the
top-parked libs, so free = `19-70` (52) + `73-98` (26) = 78, and `basic`'s
65-page code fits neither run nor the sacrifice under the old count.

### Verification

- Build: **146 asserts, 0 failed**; `fs-offset lint: OK (102 source files)`.
- Layout: `dlLibsEnd` `$5310` → `$5332` → `$5336` (bound `$5340`, font-help
  pinned), `pool-top.inc` `$53F2-$5456` (bound `$54AB`),
  `fontHelpEnd`/`cursorHelpEnd` unchanged, **pool `$5700-$C3FF` untouched**.
- Emitted bytes decoded from `build/gordon-os.prg`: both candidate walks
  (`inc` + `cmp #$6E` bound up; `dec` → `beq afpFull` down), the shared bit-set
  loop, and the pocket's three routines with their jumps.
- **Runtime (verified in VICE):** after `run edit` on a multi-page document
  (more than one `kMalloc`) → exit, `run basic` loads, and `pool` shows the
  libraries parked at the **top** (shell `0`, libs at high page numbers).

### Lesson

`pool`'s used/free come from the **tables**, so a clean listing with a failing
load means *placement*, not shortage — verify with the `pg` column against a
fresh boot. And when two kinds of allocation share one first-fit allocator, the
one that can be freed and re-loaded must grow from the opposite end, or it will
eventually sit between two of the other kind and strand the space.

### Follow-up — the `pg` column widened every row (2026-09-11)

Parking libraries at the top means page numbers ≥ 100, and the `pg` column used
`pcDec2`, which space-pads to **two** chars — so from 100 up it printed three
digits and pushed the rest of the row (and the `owner` header above it) one
column right. `pcDec2` is now `pcDec3Z`: a fixed-width **zero**-padded 3-char
field (`000`, `019`, `099`, `106`), and `msgPoolHdr2`/`msgListSep` grew one
column to match (`"pg  len type   owner"`, 30 dashes). Emitted bytes verified in
`tasks-out/shell.bin` (`C9 64 B0 .. 48 A9 30 20 .. E0 68 C9 0A B0 ..`).
**Shell code 4851 → 4864 bytes — exactly the 19-page boundary**, so its pool
footprint is unchanged (and the exact numbers above still hold), but the next
byte of shell code moves it to 20 pages.

---

## Bug #80: Loader Read a Task's Fixup Table One REU Bank High Once a Library Moved Above FS `$10000` 🔴 ✅ FIXED

**Severity:** High — on a freshly built REU image, `run basic` at boot
warm-rebooted the machine before BASIC's screen appeared. `run fpdemo`
(also an `fp.lib` declarer) failed the same way.
**Status:** ✅ FIXED 2026-09-11
**Location:** `src/kernel/poolevict.inc` (`dlPreEvict` — the retired
partial `reuFsPtr` stash), `src/kernel/dynload.inc` (`dlSizeOk` /
`rlAligned` — the 24-bit save/restore)

### Symptoms

`run basic` at boot warm-rebooted before BASIC's screen appeared; `pool`
showed only the shell resident. The *same* kernel and the *same*
binaries built into the *old* REU layout worked, and so did the new
binaries in a layout where every library still sat below FS `$10000` —
the failure tracked the **filesystem layout**, not the kernel or the
binaries (kernel PRG and REU filesystem were byte-identical to
`main`@HEAD in the failing case).

### Root Cause

`dlPreEvict` stashed the task's filesystem offset around its eager-lib
scan, but only two of the three bytes (`reuFsPtr` lo+hi) — with the
comment "`reuFsPtr+2` is not stashed: every loader file lives in bank 5,
so the hi byte is always 0".

`dlEagerLibPages` → `dlPeekLibPages` → `reuFindFile` overwrites
`reuFsPtr` with the **library's** offset. That was harmless while every
file lived below FS `$10000` (bank byte 0) and stopped being harmless the
moment the layout changed: `fp.lib` now sits at FS 68084, so `reuFsPtr+2`
was left holding `1` and the caller's own code/fixup-table reads
continued one bank (`$10000`) too high. The load then failed with loader
**error 1** ("bad task file") and, because the pre-evict had already
swapped the shell out to make room, nothing was left to print it: the
scheduler found no ALIVE task and warm-rebooted (the Bug #71 shape).

### Evidence

An instrumented kernel froze at the load stage; a VICE monitor dump at
`$0330-$033E` read `10 a4 fe 40 | 4b 07 | 0f 44 01 | 10 a4 10 00 1e 02`:
failing fixup offset `$A410`, code size `$40FE` (16638 — correct), 1867
fixups outstanding, `reuFsPtr = $01440F` = **82703**, while the correct
fixup-table offset was 17167 — exactly `$10000` less. The staged chunk bytes
at `$C400` (`10 A4 10 00 1E 02`) matched what the filesystem holds at
82703 byte for byte; the table at 17167 reads `1F 00 39 00 45 00 4F 00`.

### Fix Applied

The complete 24-bit offset is now saved once, at `dlSizeOk`, before
anything can clobber it, and re-established in `rlAligned` immediately
after `dlPreEvict` returns:

    dlSizeOk:  lda reuFsPtr;   sta dlFsSave      // all three bytes
               lda reuFsPtr+1; sta dlFsSave+1
               lda reuFsPtr+2; sta dlFsSave+2
    ...
    rlRamOk:   jsr dlPreEvict
               lda dlFsSave;   sta reuFsPtr      // layout-independent
               lda dlFsSave+1; sta reuFsPtr+1
               lda dlFsSave+2; sta reuFsPtr+2

- It is a **variable (`dlFsSave`)**, not a stack stash: the loader has
  error paths that `rts` without unwinding, so a stack stash would leak.
- It sits **outside `dlLoadCore`**, which is shared with `libLoad` — a
  restore inside the core would give lib loads the *caller's* offset.
- `dlPreEvict`'s partial stash is deleted (replaced by size-neutral
  padding), so the invariant has exactly one home and no copy of the
  wrong pattern is left to imitate.

### Verification

- Build green (**144 asserts, 0 failed**), including the four
  exact-address `gfx.lib` asserts that guard `poolevict.inc`'s size.
- The failing layout (`fp.lib` at 68084) is now the *default* built image,
  so it is a permanent regression case: `run basic` and `run fpdemo` both
  start normally with it.

### Audit — Other Filesystem-Offset Consumers

All task-side references to `reuFsPtr` (`banner`, `type`, `shell`,
`setfont`) write or copy **all three** bytes, and the kernel/lib
consumers handle 24 bits correctly: `reuDmaRange` (reads 3, preserves 3),
`reuFindFile` (3), `fontGlyph` (computes 3), the loader's header/chunk
advances (3 each, with carry), `compact.inc` (3-byte directory-entry offsets
at `reuDirBuf+17/+18/+19`, 3-byte instance fields, 9 carry-chained `adc #0`
sites, 24-bit hex helpers). No other 2-of-3-byte pattern exists in the tree.

### Caveat — the layout is only a de-facto tripwire

This bug was caught because the built image happens to have files above the
boundary: `fp.lib` at 68084 (2548 bytes above `$10000`), plus `string.lib`,
the three `.fnt` fonts, `boot.bat`, `logo320.bnr`, `basdemo.bas` and both
`.mus` files — so 2-of-3-byte handling fails loudly at `run basic` /
`run fpdemo`.

That canary is **data, not code**, and ordinary use can delete it: `compact`
repacks the FS from the start, so a file's offset is the sum of the sizes
below it. `fp.lib` only needs to move down 2548 bytes — deleting `edit.tsk`
(10371 B) and running `compact` is more than enough. Below the boundary a
2-byte stash works perfectly and the bug goes silent. (`format` + reseed
restores the canonical layout, so a freshly built image always has the
canary; a long-lived compacted FS may not.)

The code therefore must not rely on the layout at all: the `dlFsSave`
save/restore in `dlSizeOk`/`rlAligned` copies all three bytes unconditionally,
and the loader's `dlLoadCore` contract is that `reuFsPtr` is its live cursor
and nothing on the load path may call `reuFindFile`.

### Guard — `tools/check-fs-offsets.ps1`

`build-kernel.ps1` and `build-tasks.ps1` run a source lint before assembling.
It fails the build when a kernel/lib/task source:

- **R1** copies `reuFsPtr` into a stash (`lda reuFsPtr` + `pha`/`sta <other>`)
  without a `reuFsPtr+2` reference within the next 8 statements — exactly this
  bug's shape;
- **R2** forces the bank byte to a constant (`lda #0` / `sta reuFsPtr+2`);
- **R3** does 24-bit arithmetic (`sta reuFsPtr+1` straight after an `adc`/`sbc`)
  without the carry step into `reuFsPtr+2` — Bug #73's shape, which 16-bit
  wrapped the offset mid-load and read the second fixup chunk from the wrong
  place.

All three are fixture-verified: each fires on its bug's byte pattern and stays
quiet on the correct form (3-byte copy, in-place advance, carry-propagating
add). A deliberate case is annotated with an `fs-lint: allow` comment.

It is a pattern tripwire, not a type system — it cannot prove an offset is
handled correctly, and it cannot see, for example, a new hand-rolled REU
transfer window that forgets to clamp to its bank. That rule is the table
below.

**The shapes now have one home.** Everything that used to hand-write a 24-bit
operation calls the macros in `src/kernel/fs-offset.inc` instead — `FSCopy`,
`FSAdd16`, `FSAddPage`, `FSToReuAddr` — so the carry-correct sequence exists
once, and the awkward part (`FSToReuAddr`'s `(O + $D000)` wrap rule) is named
rather than re-derived at each site. Converted: the loader (`dlSizeOk`,
`rlAligned`, the header-page advance, the v3 fixup-table move and the v3 chunk
advance), `font-help.inc` (the boot font record + `fontGlyph`'s computed
offset), `reu-fs.inc` (`reuDmaRange`), `filesys.lib` (`fsxWriteRange`,
`fsxCreateZero`) and the `setfont`/`basic` font-record copies.

The conversion was verified by building before and after and comparing the
binaries: the kernel PRG and **every task and library binary are byte-identical**,
so the refactor provably changed no code.

Sites that stay hand-written, deliberately:

- `type`, `banner` and `shell` store the offset **hi byte in a separate
  instance field** (`TYPE_OFF2`, `BNR_POS2`, `BATOFF2`), so no triple-taking
  macro can express their layout (R3 covers the carry).
- `compact` keeps its own instance-field representation and sits at 114 of the
  115 fixups a v2 header allows — touching it risks the header, not the logic.
- `fstest` computes a window *index* (`$2FF8 − base16`), not a 24-bit offset.

### Stage 3 — the loader stops sharing the DMA cursor

The last weak point was that a filesystem operation nested inside a load could
move the loader's offset — precisely Bug #80's mechanism, where the lib-header
peek in `dlPreEvict` re-found a library through `reuFsPtr` and left a stale bank
byte behind for the task's own fixup table.

The fix is ownership: a load's offset lives in its own variable (`dlFsSave`),
and `dlInstallCursor` copies it into `reuFsPtr` immediately before every DMA the
loader issues (`dlFindMaster` does the find + cursor assignment for the restore
and lib-load paths). A clobber can no longer survive to the next transfer, so
the save/restore around `dlPreEvict` is gone, `dlPreEvict` is free to clobber
`reuFsPtr`, and the loader band *gained* headroom (the helpers live in the
`$FF70` pocket).

**What was deliberately not done.** Giving the FS offset "a single home" in the
sense of one variable per purpose sounds tidier than it is: after mapping the
real usage, `reuFsPtr` is not just a DMA argument — `filesys.lib` has ~30
references to it and `gfx.lib` uses it too, as their *working* offset and record
scratch (inherited from when the FS code lived in the kernel). Splitting that is
a module-boundary refactor of the FS layer — each component owning its own FS
state — not a rename, and it would touch `filesys.lib`, `gfx.lib`, five task
sources and the `kReuFindFile`/`kReuDmaRange` contracts. The hazard that
actually bit is closed; the structural tidy-up is recorded here as future work.

### Bank-crossing files are legal, and deliberately exercised

A file may straddle an FS bank boundary (`$10000`, `$20000`, …). The REU
address space is *linear* in the FS offset (`addr = (O + $5D000) mod $10000`,
`bank = 5 + (O >> 16) + carry`), but the DMA length register is 16-bit, so a
transfer must be clamped to the end of its bank. Every transfer primitive
already does exactly that:

| Primitive | Direction | Clamping |
|---|---|---|
| `reuDmaRange` | REU → C64 | explicit chunk loop: `chunk = $10000 − addr`, then `addr = 0; bank++` |
| `fsxWriteRange` | C64 → REU | multi-bank chunk loop (24-bit FS offset) |
| `fsxCreateZero` | fill | multi-bank, `kReuFillReu` per chunk |
| `cmMove` (`compact.com`) | REU ↔ REU | "each chunk is clamped so it cannot cross a bank" |

Every FS consumer goes through those: the loaders and `fontGlyph` use
`kReuDmaRange`, `banner`/`type`/`shell`/`runbatch` stream through it, in-place
file I/O uses `fsxWriteRange`/`fsxCreateZero`, and `compact` clamps.

Seeding can already *opt out* per file — `Seed-File -noSplit` bank-aligns a
file that would straddle (the `.fnt` fonts use it). Making that the default
was considered and rejected: it wastes up to 64 KB per crossing file, it would
need the same rule in the runtime allocator (`fsxCreate`) and in `compact` (a
dynamic task with a fixup budget), and it would *delete* live coverage rather
than fix anything — the built image deliberately relies on it: `gfx.lib`
(60996 + 7088 = 68084) crosses `$10000` mid-file, so every graphics load
exercises the chunk loops.

The invariant is therefore "clamp at the primitive", not "never cross".

### Runtime self-test — `fstest`

The source tripwire keeps the *shapes* out of the tree; it cannot tell whether
the paths actually work. `fstest.com` (filesys.lib) closes that gap: it reads a
16-byte window that spans a bank wrap, writes the complement of those bytes
back over it, re-reads to prove the write landed exactly there, then restores
the original bytes — exercising `kReuReadAt`/`reuDmaRange` and
`kReuWriteAt`/`fsxWriteRange` (both chunked) plus the 24-bit offset arithmetic,
and printing `read/write/restore/post ok|FAIL` and a verdict.

Two details make it deterministic and self-checking:

- **Where the wrap is.** The REU data base is bank 5 `$D000`, so an FS offset
  `O` is transferred at linear address `O + $5D000` and a transfer wraps a bank
  when `(O + $D000)` rolls over `$10000` — i.e. at FS offsets ending **`$3000`**,
  not `$0000`. The test's window start is therefore `$2FF8 − (base & $FFFF)`:
  its FS offset always ends `$2FF8`, so the 16 bytes always straddle the wrap,
  whatever the file's base is.
- **The oracle.** `fscross.bin` (seeded by `tools/build-reu.ps1`, 8 KB) has
  generated contents — byte `j` = `j & $FF` — so the expected window is just
  "start byte, +1, +2, …" and nothing has to be stored. The write check writes
  the **complement**: writing the same bytes would be invisible if the write
  landed elsewhere, since every offset in the file holds pattern bytes.

If the asset is gone, or a `compact` has moved it (the seeder leaves a gap of
up to 64 KB to place it, which `compact` reclaims), the first read comes up
short and the test prints `skipped` — which is the honest answer: the coverage
no longer exists on that filesystem.

### Watch item — the two clobbers that remain by design

`fontGlyph` and the loader's own cursor genuinely share `reuFsPtr`. They are
safe today because the loader runs SEI'd (no task can blit a glyph mid-load)
and because `rlAligned` re-establishes the cursor from `dlFsSave` right before
`dlLoadCore`. The only way to remove the sharing for good is the ABI-visible
split ("find result" vs "next DMA offset"), which also touches `kReuFindFile`
(`$E0BA`), `filesys.lib` and the three font-using tasks — tracked here as a
future change, not applied.

---

## Bug #79: `sysKill` Cascade Leaked Every Thread-Child's Pool Pages, Lib Refs and Dyn-Table Slot 🔴 ✅ FIXED

**Severity:** High — permanent resource loss that survives killing every task. Each
cascade-killed thread child leaked its code pages, its `KF_MALLOC_RAM` block, its
library refcounts **and its dynamic-task table entry**. The table entry is the worst
of the four: `dynTaskCount` is only decremented by `dynFree`, so after 8 cascade
kills (`MAX_DYN_TASKS`) the loader's table is permanently full and no further
dynamic task can be loaded until reboot.
**Status:** ✅ FIXED 2026-09-11 — statically verified (build green, instruction bytes
decoded from the PRG). Runtime confirmation pending.
**Location:** `src/kernel/syscall.inc` — `skKillChild`, inside the `sysKill` (2)
cascade fixpoint loop.

### Symptoms

`threads` (or any task using `sysThreadSpawn`, syscall 12) spawns children.
`kill <parent>` cascade-kills them and `ps` then shows the children gone — but
`pool` never returns the pages they held, `dynps` keeps listing their entries, and
after ~8 such kills every dynamic load fails and *stays* failed.

### Root cause

The cascade did exactly one cleanup call per victim:

```
     tya; tax                    // X = child slot
     tya; pha
     jsr freeMallocBlocks        // X = child slot — free its malloc blocks
     pla; tay
```

`dynFree` is the only routine that does the rest, and `sysKill` calls it exactly
once, at `skCascadeDone`, with `lda kArg` = the **root** target. So every victim
below the root stayed registered:

| Leaked | Would have been freed by |
|---|---|
| code pool pages (`dynTaskStartPage`/`dynTaskPageCount`) | `dynFree` → `freeFromPool` |
| `KF_MALLOC_RAM` program-RAM block | `dynFree` → `dfClear` |
| library refcounts | `dynFree` → `dlLibRelease` |
| dyn-table entry **and the `dynTaskCount` slot** | `dynFree` → `dfClear` |

`dynTaskCount` is incremented in `dlRegister` (`dynload.inc:204`) and decremented
only by `dynFree` (`:296`, `:1391`, `:2074`), and `MAX_DYN_TASKS = 8`. Because a
victim's entry kept a live-looking `dynTaskId`, its slot was never reusable.

`freeMallocBlocks` was also redundant: `dynFree` calls it as its first act, for
static and dynamic tasks alike (`dynFindByTaskId` failing on a static task returns
`C=1` *after* the malloc blocks are released).

### Second defect, same block

`transferFocusAway` takes its victim in **X** (`cpx kbFocus`, `syscall.inc:756`), but
it was called straight after `freeMallocBlocks`, whose contract is "Clobbers: A, X,
Y, kArg+3..kArg+7". The `lda kArg / pha … pla / sta kArg` guard around the call
protects `kArg` — the register that routine *writes* (`fromTask`) — not the one it
reads. So `X` was garbage and `cpx kbFocus` compared garbage: a cascade-killed child
that owned `kbFocus` left focus pointing at a dead slot, after which `kReadString`
returns immediately with `Y = 0` for every other task (copilot-instructions Rule 3)
and the shell looks frozen. The top-level target path never had this problem because
`X` = target there.

### Fix applied (2026-09-11)

In `skKillChild`, `freeMallocBlocks` is replaced by `dynFree` (A = child slot, which
is the GordonOS task ID), `inc kArg+1` is moved below that call, and `X` is reloaded
from Y immediately before `transferFocusAway`. Net **+1 byte** — no new routine and
no jump-table change. `kernelCoreEnd` is now `$1393` (→ `$139C` after the Bug #81/#82 changes), leaving **65 bytes** before
`reuDirBuf` at `$13D4`.

`dynFree` preserves `kArg..kArg+2` by contract (it pushes them around `dlRestore`)
and clobbers only `kArg+3..+7`; `freeFromPool` uses `kArg+4`/`kArg+6` and
`dlLibRelease` touches no `kArg`. The cascade root in `kArg` and the `changed` flag
in `kArg+1` therefore both survive — moving the `inc` below the call is
belt-and-braces so it does not rest on that guarantee.

### Verification

Static: build green (`Made 144 asserts, 0 failed`) and the PRG starts `$01 $08`. The
emitted bytes for the block decode as `tya / pha / tya / jsr $4450 / pla / tay /
inc $4001 / tya / pha / lda $4000 / pha / tya / tax / jsr $1293 / pla / sta $4000 /
pla / tay`, with `bne $10F8` still reaching `skCascade` (well inside ±128).

Runtime (pending): `run threads` → `ps` (note the two `threads.N` ids and the `pool`
page count) → `kill <parent>` → `ps` must show no `threads.N` rows and `pool` must
return to the pre-run count; repeat ~10× and neither number may drift.

### New guard

Nothing asserted the kernel core against `reuDirBuf` before, so core growth past
`$13D4` would silently collide with it and still build green. `syscall.inc` now ends
with a `kernelCoreEnd` label and `main.asm` asserts
`.assert "kernel core fits below reuDirBuf ($13D4)", (kernelCoreEnd <= $13D4) ? 1 : 0, 1`.

### Watch items (not fixed here)

- `dlRestore` runs in `dynFree`'s tail, so it now fires once per victim instead of
  once per kill. It cannot livelock (bounded by `MAX_DYN_TASKS`), but a restored task
  can come back alive *during* a cascade; the fixpoint loop should re-kill it if its
  parent is a sentinel victim. Confirm with the `threads` test rather than by reading.
- Unrelated: `dlRestoreMalloc` re-allocates a restored block at a new `startPage`, but
  a task holding the old `kMalloc` pointer keeps the stale address and `kFreeImpl`
  matches on `startPage`, so those frees fail silently. Exposed callers are the
  evictable `kMalloc` users (`banner`, `copy`, `sidplay`).

---

## Bug #78: Failed Eager-Lib Load Over-Decrements Lib Refcounts — Pool Pages Never Return 🔴 ✅ FIXED

**Severity:** High — a library's pool pages can be stranded for the whole session
(26 pages for `gfx.lib`, 10 `filesys.lib`, 8 `fp.lib`, 2 `string.lib`), and the
symptom looks exactly like a leak: later loads fail with `? no space in pool`
and killing the running tasks does not recover the space.
**Status:** ✅ FIXED 2026-09-11 — statically verified (kernel builds green, instruction
layout confirmed from label spacings). Runtime confirmation pending.
**Location:** `src/kernel/dynload.inc` — `dlLibEagerLoad` (the `inc libRefCount,x` /
`bcs dleFail` early exit), `dlLibRelease` (the `dec libRefCount,x`), and both
rollback callers: `dlRegUnwind` (via `dlRegister`) and `dlReLibs` (via `dlRestore`).

### Symptoms

Reported 2026-09-11 (intermittent): run `edit <file>`, save the file, run `maze`,
then `compact` → `? no space in pool`. The refusal **persisted after killing
both `edit` and `maze`**. Not reproducible every time.

`? no space in pool` is `msgDynNoSpace` (`shell.inc`) = loader error 4, i.e.
`allocFromPool` could not find a *contiguous* run of pages for the code region —
it is not the surface/gfx-slot error (`? no screens available` is a different
string).

The budget rules out a genuinely full pool. With the editor resident (36 pages +
its first 4-page chunk), the shell (19) and the shell's declared libs
(`filesys.lib` 10 + `string.lib` 2), plus `maze` (1), only **72 of 109** pool pages
are in use — **37 free**, and `compact` needs **12**. So ~26+ pages were held by
something no live task owned, which is why killing the tasks changed nothing.
(A resident `basic` and its eager libs is a *different*, already-known refusal —
see Bug #71 — and is not the case here.)

### Root Cause

`libRefCount[id]` counts **declaring tasks** — the sharing mechanism: `libLoad`
only performs the physical load once (`lib.inc`, `llAlready` returns early if the
lib is resident). The accounting therefore has to be exact per declarer, but two
rollback paths break it.

`dlLibEagerLoad` walks the task's 16-bit mask and increments **as it goes**, then
**exits early on the first failing load**:

```asm
    inc libRefCount,x           ; reached only for bits up to the failure
    lda libEager,x
    beq dleNext                 ; lazy → reference only
    ...
    jsr libLoad                 ; eager → physical load
    bcs dleFail                 ; ← bails out; the inx / loop-back is skipped
```

`dlLibRelease` walks the **whole mask** and decrements every set bit:

```asm
    dec libRefCount,x
    bne libRelNext              ; still referenced → keep
```

Both rollback callers use that full-mask release after a bailed-out loop:

- **load failure** — `dlRegister` → `dlRegUnwind` → `jsr dlLibRelease`
- **restore failure** — `dlReLibs` → `jsr dlLibEagerLoad`, and on `C=1` →
  `jsr dlLibRelease` (comment: "roll back the refcount")

So every declared bit **above** the failing lib is decremented without ever having
been incremented.

### Why it shows up as a pool leak

With a shared lib this has two outcomes, both bad:

| Case | Effect |
|---|---|
| Another task legitimately holds that lib | The count sits **below** the true number of declarers. When that other task dies, its own decrement reaches 0 and **the lib is freed while a live task still holds it** — its jump entries revert to the cold stub, the next call re-loads it, which allocates pool pages, may fail (`could not load`), adds eviction pressure and feeds the Bug #71 self-eviction chain |
| The count was 0 | `dec` wraps to `$FF`, and `bne libRelNext` reads that as "still referenced" → `libFree` never runs again → **that lib's pages are pinned for the session**, owned by no task. Killing tasks cannot release them |

The second case is the observed one: e.g. `basic`'s mask is `$000E` (bit 1
`filesys` lazy, bit 2 `gfx`, bit 3 `fp`); with a 36-page non-evictable editor
resident, `gfx.lib` (26 pages) is what cannot fit, so the loop dies at bit 2 and
**bit 3 (`fp.lib`) is decremented without ever having been incremented**.

72 used + 26 pinned = 98 used, 11 free — `compact`'s 12-page run fails by **one
page**, which is also why small layout differences flip the result.

**Why the editor's presence matters even though it declares no eager libs:**
`dlRestore` *does* call `dlReLibs` (`dynload.inc:1570`), but a restore's eager-lib
load is likeliest to fail exactly when the pool is tight — i.e. while the editor is
pinned. Each failed restore runs the over-decrementing rollback, so the editor does
not have to be the task whose mask is corrupted; its pinning makes *someone else's*
restore fail. That also explains the intermittency: it needs a load or restore of a
task whose mask has a set bit above a failing eager lib (`basic` `$000E`,
`fpdemo` `$000C`).

### Verification (before changing anything)

`pool` is a shell **built-in**, so it still works when `.com` loads are failing. It
prints `pg len type owner` rows for tasks, RAM blocks, malloc blocks **and libs** (`pg` is a zero-padded 3-char field since 2026-09-11 — libraries park at the top of the pool, so page numbers reach 100+)
(`pg` is a zero-padded 3-char field since 2026-09-11 — libraries park at the top
of the pool, so page numbers reach 100+)
(`shell.inc`, `pcPool*`). Watch whether a lib's pages appear and then never
disappear as the tasks that use it come and go — that is the pin manifesting.

### Fix applied (2026-09-11)

Sharing semantics are now exact; nothing is masked.

1. **`dlLibEagerLoad` is now two passes** (`src/kernel/dynload.inc`). Pass A
   (`dleRefLoop`) walks the mask and `inc libRefCount,x` for every *declared* bit —
   no loads, so it cannot fail. Pass B (`dleLoadInit` / `dleLoop`) performs the
   eager `libLoad` calls and may still return `C=1`. A new loader-local helper
   `dleBitSet` (`$5162`) answers "is lib X declared in slot `dlLibTmp2`'s mask?" for
   pass B; it preserves X and clobbers A/Y. Cost: 24 bytes (body 12 + helper 12),
   so `dlLibsEnd` moved **$529C → $52B4**.
2. **`dlLibRelease` clamps before decrementing** — `lda libRefCount,x` / `beq
   libRelNext` (5 bytes). With (1) in place the decrement can no longer touch a
   never-incremented bit, so this is belt-and-braces rather than a cover-up.
3. **Assert hazard closed.** `main.asm` asserted `dlLibsEnd <= scrollScreenSlot`
   (`$54AB`), which let 165..527 bytes of growth silently overwrite `font-help.inc`,
   pinned at `$5340`. It is now
   `.assert "dynload fits before font-help ($5340)", (dlLibsEnd <= $5340) ? 1 : 0, 1`.
   Loader headroom is **140 bytes**.
4. **Deliberately left undefined:** who owns a *lazy cross-lib* reference
   (`filesys.lib` → `kStrlen`, `time.lib` → `kByteToHex`). The shell declares
   `string.lib` (`libMask $0012`), so it stays resident and this is not the trigger.
   Worth a follow-up decision, not part of this fix.

### Verification

Static: kernel builds green (`Made 143 asserts, 0 failed`), and the label spacings
confirm the intended instructions — `dlLibEagerLoad→dleRefLoop = 11`,
`dleRefLoop→dleRefNext = 12` (`cpx / bcs / jsr / beq / inc abs,x` — the increment is
present), `dleLoadInit→dleLoop = 2`, `dleLoop→dleNext = 23`, `dleNext→dleDone = 4`,
`dleDone→dleFail = 8`, `dleFail→dleBitSet = 8`, and `libRelLoop→libRelNext = 46`
(34 body + 7 `libFree` + 5 clamp). Layout: `dlLibsEnd = $52B4`,
`bootFontResolve = $5340`, `scrollScreenSlot = $54AB`.

Runtime: **not yet observed.** The symptom is intermittent, so the decisive check is
the `pool` listing across the failing sequence — the `gfx` / `fp` / `filesys` /
`string` rows should now disappear once the last task using them exits.

---

## Bug #77: `compact` Listed Only the First Blocking Task 🟡 ✅ FIXED

**Severity:** Medium — the `? busy` refusal reported the correct *count* but only
one task name, so an operator with several tasks running could not clear them
from the list.
**Status:** Fixed 2026-09-11
**Location:** `src/tasks/cmd/compact.inc` — `cmpDec16` (its reuse of `CMP_T0`).

**Symptoms:** with two or more tasks alive, `compact` printed
`? busy - 2 task(s) must be killed` followed by a single `  name (id N)` line —
no second name, and no `... and N more` trailer.

**Root cause:** `cmpDec16` used `CMP_T0` as its hundreds/tens digit counter, but
`CMP_T0` is Pass 0's scan slot, and `cmpPrintBlocker` prints the task id through
`cmpPrintDec` → `cmpDec16`. After the first blocker was listed, `CMP_T0` held a
digit-counter value (~`$30`) instead of the slot index, so the next `cmpBusyLp`
iteration's `cmp #MAX_TASKS` (24) went `bcs` straight to the trailer. With fewer
than `CMP_MAXSHOW` (6) blockers the trailer takes the plain-exit branch, so the
`and N more` line is not printed either. The header count stayed correct because
`cmpGuard` counts blockers through the same loop but never prints.

**Fix:** a new instance byte `CMP_DIG` (238) holds the digit counter and
`CMP_END` moves to 239 (still ≤ `CMP_DATA_SIZE`, so no data-size change); the
seven `CMP_T0` references inside `cmpDec16` were switched to it. This is a pure
operand substitution — `codeSize` (2842) and the 114-entry fixup table are
unchanged, which matters because the v2 header has only two fixups of headroom.

**Audit:** every other `CMP_T0` site was checked — `cmpGuard` (scan only),
`cmpBlkTest` (reads it), `cmpShowLine` (sets it for the name walk *after* the
decimal emits, and the following `$DEST` emit is hex, i.e. `CMP_T1`/`CMP_T2`),
and `cmpBounds`/`cmpMove`/`cmvBank`/the Pass-1 and Pass-2 fast paths (scratch
inside non-printing phases). `cmpDec16` was the only clobber source.

---

## Bug #76: `compact` Reported the Allocator Offset as "free" 🟡 ✅ FIXED

**Severity:** Medium — misleading rather than destructive: the `free`/`was`
figures disagreed with `dir` by the whole used-space figure, and appeared to
move the wrong way after a run that had just reclaimed space.
**Status:** Fixed 2026-09-11
**Location:** `src/tasks/cmd/compact.inc` — `cmpP2End`'s report tail.

**Symptoms:** after a successful compaction the summary read
`free $0138D7 (was $0138D7)` — the same number twice — while `dir` showed
`$F4F729 free` for the same filesystem.

**Root cause:** both fields printed the raw **next-free offset** (the bytes *in
use*), not free space. A next-free offset falls by exactly the amount a
compaction reclaims, which contradicts the `reclaim $xxxxxx` line printed two
lines earlier — `old − dest` is by definition the *increase* in free space, so
the two figures could not both be right. The plan's own acceptance test
("`compact` → free rises") fails against the old output. `dir`'s footer computes
free as `capacity − nextFreeOffset` (`dirPrint24`); `compact` was not.

**Fix:** convert both fields to `capacity − field` before printing — one `sec`
plus three chained `sbc`s (`cmCapFree` macro, the same 24-bit shape as
`dirPrint24` and the existing reclaim subtract). The slot-0 commit has already
consumed `CMP_DEST` and neither field is live after the report, so in-place
conversion is safe, and `free − was` now equals `reclaim` exactly.

**Note:** the 24-bit arithmetic itself was never wrong — `cmpDestAddSize`,
`cmpBounds`, `cmpOffGeDest`/`cmpOffEqDest`, the reclaim subtract and
`cmpHex24Str`'s MSB-first print all chain carry/borrow correctly. The bug was
the quantity chosen, not how it was computed.

---

## Bug #75: `compact.com` Printed Every Generated Number as Graphics Characters 🔴 ✅ FIXED

**Severity:** High — all of `compact`'s progress and report output was
unreadable (the message text was fine; every number in it was not).
**Status:** Fixed 2026-09-11
**Location:** `src/tasks/cmd/compact.inc` — `cd16Put` and `cmpHexPut`.

**Symptoms:** `compact` printed the right words but every generated number —
file/slot counts, `(id N)`, `moved N, purged M`, and every `$xxxxxx` offset —
displayed as a graphics glyph.

**Root cause:** both putter routines ended their cursor fetch with the `ldCY`
macro, which expands to `ldy #off / lda (dataPtr),y / tay` — so **A** came back
holding the *cursor value*, not the digit/hex character the caller had just
loaded into it. The following `sta (dataPtr),y` therefore wrote the cursor
value (226 = `$E2`, then 227 = `$E3`, …) into the output buffer instead of the
character. Every character of every number became a screen code ≥ `$E0`, i.e. a
reverse-video graphics glyph. `cmpEmitCh` — which writes the status line —
already had its `pha`/`pla` guard, which is exactly why the static `.text`
messages and the line layout were correct and only the numeric fields broke.

**Second defect, same block:** `cmpDec16`/`cd16Put` used `CMP_CUR` as the
`CMP_STR` write cursor, but `CMP_CUR` is `cmpEmitCh`'s cursor into `CMP_LINE`.
Building the digits for the status line therefore clobbered the line cursor
(and vice versa) whenever a number was built mid-line.

**Fix:** `pha` before / `pla` after the `ldCY` in both putters, so A survives as
the character to store; and `cmpDec16`/`cd16Put` moved from `CMP_CUR` to
`CMP_T1`. Verified in the built binary: exactly two guarded putters and no
unguarded form, 114 fixups (unchanged, table still fits the 256-byte header),
and both `lda cmpHexChars,x` operands still carry fixups.

---

## Bug #74: `kBlitChar` Kept the Reverse-Video Bar When Re-painting a De-marked Cell 🔴 ✅ FIXED

**Severity:** Medium — un-marking an editor selection left the previously
highlighted cells still painted as reverse-video bars.
**Status:** Fixed 2026-09-11
**Location:** `src/kernel/scroll-region.inc` — `blitCharAt` (the shared
body for `kBlitChar` / `kBlitCharRev`), the matrix colour-nibble write.

### Symptoms

- In `edit`, entering mark mode (STOP/M) highlighted the selection in
  reverse video, but clearing the mark again — STOP/M off, or STOP/Y, which
  drops the highlight after a yank — did **not** restore ordinary cells. The
  affected cells stayed visibly bar-filled.
- The artifact tracked the selection exactly, so it looked like the editor
  was failing to redraw rather than like a kernel colour bug.

### Root Cause

`blitCharAt` derived the **background** nibble from the byte already in the
video matrix instead of from the surface's configured colour:

```asm
lda (kPtr),y
and #$0F                    // existing bg nibble
sta kArg+7
```

The video matrix nibble is `(foreground << 4) | background`, so `AND #$0F`
picks up the low nibble. That is the background colour for a cell written
normally — but **not** for a cell written by `kBlitCharRev`, whose nibble is
`(bg0 << 4) | textColour`. On those cells the low nibble is the *text*
colour. Re-blitting such a cell normally therefore produced
`(textColour << 4) | textColour`, i.e. the cell kept a solid text-coloured
background and still read as a bar. A "restore this cell to normal" blit
could never clear a reversed cell.

### Fix applied

Read the background from the task's surface colour instead of the cell being
replaced:

```asm
ldx curTask
lda taskGfxBg0,x            // bg0 — never leak a reversed cell's bar nibble
sta kArg+7
```

A normal `kBlitChar` now always writes `(textColour << 4) | bg0`, so
painting over a reversed cell yields a plain cell and mark/un-mark cycles are
idempotent. A one-byte `nop` was added as a size pad — the `$FBA3` pocket is
address-pinned, so the routine's length is constrained.

### Lesson

A blit that is meant to *restore* a cell must not derive its attributes from
the bytes it is replacing. Reverse-video and normal video store their nibbles
in the same two fields with swapped halves, so "preserve what was there" is
only correct when the incoming write uses the same encoding as the outgoing
one.

---

## Bug #73: Dynamic Loader v3 Fixup Chunk Advance Drops the 24-Bit Carry 🔴 ✅ FIXED

**Severity:** High — `run basic` warm-rebooted the whole system.
**Status:** Fixed 2026-09-08
**Location:** `src/kernel/dynload.inc` — the `dlcV3` chunk advance, the
`dlcV3` fixup-table offset computation, and the `dlLoadCore` code-DMA
header advance (all three 24-bit `reuFsPtr` adds).

### Symptoms

- `run basic` caused a warm reboot (every task died → the scheduler rebooted
  at the next tick) whenever the REU FS layout put `fp.lib`'s fixup table
  past FS offset `$FFFF`.
- The trigger was layout-dependent: adding `keyrpt.com` (+368 bytes) shifted
  the libs just enough to push `fp.lib`'s fixup table across the 64 KB
  boundary. The libs themselves were byte-identical; only the FS offsets
  moved.

### Root Cause

The v3 fixup path streams the fixup table in 256-byte chunks (128 fixups)
via `reuDmaRange`, then advances the 24-bit FS offset `reuFsPtr` with a
**16-bit add that never propagated the carry into the bank byte**
(`reuFsPtr+2`):

```asm
lda reuFsPtr
clc
adc kArg+6
sta reuFsPtr
lda reuFsPtr+1
adc kArg+7
sta reuFsPtr+1      ; reuFsPtr+2 never updated
```

`fp.lib` (v3, 185 fixups = 370 bytes) has its fixup table at FS offset
65438 (`$FF9E`) in the broken layout. Chunk 1 (256 bytes) ends at 65693;
the 16-bit advance wrapped `reuFsPtr+1` from `$FF` to `$00` and left
`reuFsPtr+2 = 0`, so chunk 2 was read from FS offset `$009E` (158) instead
of `$1009E` (65694). The 57 garbage fixup offsets corrupted `fp.lib`'s
relocation → `basic` jumped into garbage → memory corruption → all tasks
died → warm reboot.

### Fix applied

Propagate the carry into `reuFsPtr+2` after all three 24-bit `reuFsPtr`
advances in the loader (`dlcV3` entry, `dlcV3` chunk advance,
`dlLoadCore` header advance): `lda reuFsPtr+2 / adc #0 / sta reuFsPtr+2`.
The 24-byte growth moved `font-help.inc` from `$5284` to `$5340`
(loader-headroom pocket).

### Lesson

Any arithmetic on the 24-bit `reuFsPtr` (REU FS data-area offset) must
update all three bytes, including the bank byte `+2`. The FS data area
spans banks 5..251, so a 16-bit add that stops at `+1` silently wraps at
`$FFFF` and reads the next chunk from the wrong bank. Same boundary class
as Bug #60/#61.

---

## Bug #72: `edit` STOP Prompt Could Echo a Reversed Cursor Cell at (15,16) 🔴 ✅ FIXED

**Severity:** Medium — pressing STOP in `edit` could paint a reversed copy
of the live cursor cell onto the document body and sometimes strand a
phantom cursor block.
**Status:** Fixed 2026-09-07
**Location:** `src/tasks/dynamic/edit.inc` (STOP modal state),
`src/kernel/scroll-region.inc` (`kBlitChar` / `kBlitCharRev` shared path)

### Symptoms

- In `edit`, pressing STOP to open the command prompt could echo a reversed
  copy of the character under the cursor into a fixed body cell (`col 15`,
  `row 16`).
- On some runs the old block cursor also remained behind as a phantom.

### Root Cause

Two layers were involved.

1. **Kernel hardening gap:** the fixed-position reverse blitter
   (`kBlitCharRev`) did not share the same saved-cursor cleanup discipline
   as the other fixed-position write paths. A status-bar redraw that hit the
   cursor's saved cell could therefore leave stale blink state behind for the
   next OFF phase.
2. **Task-level modal-state bug:** `edit` treated STOP command mode as a
   status-bar modal prompt, but left the normal editing cursor active while
   the prompt was displayed. That let the IRQ blink logic keep managing a
   live body cursor even though the task was temporarily not in an editing
   state.

The reliable fix came from making the modal state explicit: once STOP puts
the editor into its command/confirm/write-as prompt, the task disables the
cursor until editing resumes. The kernel blitter fix remains important as
general hardening, but the editor also needed to stop asking the kernel to
blink a live editing cursor during a non-editing modal prompt.

### Fix applied

- `scroll-region.inc`: moved the saved-cursor cleanup into the shared
  `blitCharAtBlit` path used by both `kBlitChar` and `kBlitCharRev`, so
  fixed-position UI draws cannot resurrect stale cursor pixels.
- `edit.inc`: STOP command mode now disables the cursor on entry and
  re-enables it on every return-to-editing path (`save`, `clear`, cancel,
  write-as exit, existing-file refusal).

### Lesson

Blink-safe blitters are necessary kernel hygiene, but they do not replace
task-level modal discipline. Any task that reuses screen space for a modal
prompt should park its editing cursor for the duration of that mode.

---

## Bug #71: Shell Self-Evicts During a Failed `run basic` — "No Space" Error Lost 🔴 ✅ FIXED

**Severity:** Medium — not a showstopper. The shell becomes unusable
(swapped out) instead of reporting the failure, but it recovers once pool
space frees (e.g. the editor exits → `dlRestore`). The higher-priority
invariant — the editor is **non-evictable** so its unsaved data is never
lost — is already in place: `edit.asm` header flags `$69` include
`KF_NOT_EVICTABLE` ($40), and `dlEvict` skips any slot with that bit set
(`and #(DYN_FLAG_SWAPPED | KF_NOT_EVICTABLE); bne dlEvNext`).
**Status:** ✅ FIXED 2026-09-11 — see "Fix applied" below.
**Location:** `src/kernel/poolevict.inc` (`dlPreEvict`), `src/kernel/dynload.inc`
(`rlAligned` — `sta dlNoSelfEvict`, `dlEvict`, new `dlCallerPages` /
`dlSelfEvictOk` / `dlSelfEvictedExit`)

### Repro

1. Run `edit` (pinned — `KF_NOT_EVICTABLE`).
2. Switch to the shell and run `basic`.
3. `basic` needs ~99 pool pages (65 code + 26 `gfx.lib` + 8 `fp.lib`,
   both eager), which can never fit while the 26-page editor is pinned.
4. Expected: `? no space in pool`, shell stays alive.
   Actual: the shell freezes (it is self-evicted), no error is printed,
   and it only revives when enough pool space is freed later (e.g. the
   editor exits and `dlRestore` reloads it).

### Root Cause

Task loads set `dlNoSelfEvict = 0` (`dynload.inc`, `rlAligned`), so the
loader is allowed to evict the **calling task** (the shell) to make room.
`dlPreEvict` sums the load's whole footprint (`code + RAM + eager libs`)
and, when that doesn't fit, calls `dlEvict` (largest victim first). With
the editor pinned, the only evictable task is the shell itself, so it gets
swapped (`TA_SWAPPED`, `dlEvictedSelf = 1`). There is still not enough
room, `dlEvict` returns "no victim", and `dlPreEvict` takes the
`dlPreSelfEvicted` branch: it sets error 4 and `jmp reschedule` — but the
shell is already swapped out, so no task is left to print the error. The
error is discarded and the shell sits frozen in REU.

Note this is **not** "never evict the caller": in the normal case (no
editor), `run basic` legitimately starts *by* self-evicting the shell
(free 89 + shell 20 = 109 ≥ 99), and that must keep working.

### Fix Applied (2026-09-11)

The fix proposed above, implemented as a **two-pass pre-evict**, placed
entirely in `dynload.inc` so the ABI-frozen `poolevict.inc` block stays
byte-identical:

1. **Pass 1 — protect the caller.** `dlPreEvict` sets `dlNoSelfEvict = 1`
   before its eviction loop, so `dlEvict` skips `curTask` (the very guard
   a lib/restore load already uses) and evicts every *other* task first.
   If that makes room, the caller is never touched — better than before,
   where the largest-victim policy could sacrifice the shell while
   smaller tasks would have sufficied.
2. **Pass 2 — is the sacrifice enough?** When no other victim is left,
   `dlPreEvict` calls the new `dlSelfEvictOk`, which checks
   `free + dlCallerPages() >= dlPreNeed`. Only if that holds is the
   protection lifted (`dlNoSelfEvict = 0`) and the loop retried;
   otherwise the load fails with error 4 **with the caller still alive**,
   so the shell prints `? no space in pool` and keeps running. Called
   with the protection already lifted (pass 2) it returns "refused", so
   the retry cannot loop.
   `dlCallerPages` deliberately under-counts (code pages only — a
   caller's program-RAM/malloc pages are freed by `dlSwapMalloc` too,
   but are not counted): a short count only makes the pre-evict *more*
   conservative, while an over-count would let the caller be sacrificed
   for a load that still does not fit — i.e. the original bug.
3. **Safety net — never reboot.** The three exits that reached
   `jmp reschedule` with `dlEvictedSelf` set (`rlSelfEvicted`,
   `dlRegEagerResched`, `dlPreSelfEvicted`) now jump to the new
   `dlSelfEvictedExit`, which calls `dlRestore` first. If the caller
   *does* get swapped out and the load then fails anyway (a corrupt
   binary, error 1 — or a task with no declared libs, which skips
   `dlPreEvict` entirely and can still be self-evicted from inside
   `dlLoadCore`), the swapped tasks are reloaded and the caller
   cold-restarts, instead of the scheduler finding zero ALIVE tasks and
   warm-rebooting. That path loses the message (a cold restart has no
   memory of the `run`), but the OS survives; before, the same failure
   was a reboot. If even the restore cannot fit, the reboot remains the
   only option — there is nothing left to run.

**Space:** `poolevict.inc`'s size is ABI-frozen, so the padding it had
accumulated is now a single `.fill` at the end of its code (moved out of
the middle of `dlPreEvict`) and the new code lives in `dynload.inc`.
Net effect: `poolevict.inc` is byte-for-byte the same size (all four
exact-address `gfx.lib` asserts pass); the loader band ends at `$531C`
(bound `$533F` — font-help.inc is pinned at `$5340`), leaving ~36 bytes
of headroom. The three new helpers were placed at the very end of the
loader code after measuring that a mid-region insert pushes nearby
branches past ±128 and KickAssembler re-emits them as 5-byte
trampolines (that bloat costs more than the routines themselves).

### Verification

- Build: **144 asserts, 0 failed** — including the four exact-address
  `gfx.lib`/`ff*` scratch asserts and `dynload fits before font-help`.
- `run basic` with the shell as the only task (no editor): pass 1 finds
  no victim, pass 2 sees `free (90) + caller (19) = 109 ≥ needed (99)` →
  the shell *is* swapped out and the load proceeds — the normal path is
  unchanged.
- `edit` running (pinned, `KF_NOT_EVICTABLE`) + `run basic`: pass 1 finds
  no victim, pass 2 sees `free + caller pages < 99` → error 4, nothing
  evicted, the shell prints `? no space in pool` and stays alive.
- **Manual run (user-confirmed, 2026-09-11):** boot and dynamic task loads
  behave as before — no regressions — and a load that self-evicts the
  caller no longer warm-reboots the machine.

### Residual / Watch items

- **Co-instance self-eviction.** `dlEvict` swaps *every* instance linked
  to a shared reentrancy record — including the caller, even in pass 1,
  if another live instance of the same binary exists (e.g. two shells).
  The arithmetic cannot prevent that; `dlSelfEvictedExit` prevents the
  reboot, but that path loses the message.
- **Lib-less tasks skip `dlPreEvict`** (it returns immediately when no
  library is declared), so their only protection is the
  `dlSelfEvictedExit` safety net — no reboot, but no message either.
- `findNextAlive` still warm-reboots when it finds no ALIVE task and the
  restore could not bring one back. That case now means "nothing is left
  to run", not "a load failed".

---

## Bug #70: Editor Load Corrupts `ED_CHUNKS` on Chunk Grow (Reset on >24-line Files) 🔴 ✅ FIXED

**Severity:** High — loading any file longer than 24 lines reset the
system to the BASIC READY prompt.
**Status:** Fixed 2026-09-06
**Location:** `src/tasks/dynamic/edit.inc` (`editLoad` — `elNlGrowOk` /
`elWrapGrowOk`)

### Root Cause

`elNewline` and the hard-wrap path call `jsr elGrowChunkIfNeeded`, then
store `ED_LINES++` with `sta (dataPtr),y`. That subroutine clobbers `Y`:
on the **no-grow** path it happens to leave `Y = ED_LINES`, but on the
**grow** path (`elGrowChunk` → `elGrowOk`) it ends with `Y = ED_CHUNKS`.
So the first time a load crossed the 24-line chunk boundary, `ED_LINES+1`
was written into `ED_CHUNKS` (leaving it `25` instead of `2`) and
`ED_LINES` off by one. The garbage chunk count then fed the chunk-table
indexing / free paths (`chunkTable[25]` is past the 8-entry table) → bogus
REU DMA address → CPU reset.

### Fix applied

Reload `Y` before the store in both `elNlGrowOk` and `elWrapGrowOk`:
`ldy #ED_LINES` immediately before `sta (dataPtr),y`.

---

## Bug #69: `pool` Double-Counted Shared Reentrant Code Pages 🔴 ✅ FIXED

**Severity:** Medium — with more than one instance of a reentrant task
running (e.g. two shells), `pool` counted the shared code pages once per
instance, so the `used`/`free` totals were wrong (free read too low).
**Status:** Fixed 2026-09-04
**Location:** `src/tasks/dynamic/shell.inc` (`pcPool` reentrant branch)

### Root Cause

Reentrant tasks (the shell is `KF_REENTRANT`) share **one** loaded code
copy. The residency record holds `resStartPage`/`resPageCount`, and every
instance's `dynTaskResidency` points at the **same** record. `pcPool`'s
dyn-task loop took the reentrant branch for every instance and added
`resPageCount[resIdx]` to the accumulator each time — so N shells counted
the same pages N times. Non-reentrant tasks were unaffected (they use the
per-slot `dynTaskPageCount`).

### Fix applied

- Added `POOLRS` (offset 103) to the shell data buffer — a per-listing
  bitmap of residency records already counted — and zeroed it with the
  accumulator at the top of `pcPool`.
- In the reentrant branch, the loop computes bit `1 << resIdx`, test-and-
  sets it in `POOLRS`: the **first** instance of each residency keeps its
  page count (and adds it to the total); later instances show `0` pages,
  so the listing still sums exactly to the bottom totals.
- `SHELL_DATA_SIZE` 103 → 104 (shell.asm header + `kernel.inc` + the
  `main.asm` guard assert), so the per-instance heap buffer grew one byte.
  No new fixups — the new code is all ZP-indirect/immediate accesses.

---

## Bug #63: STOP (`$83`) Echoed as a Reversed `c` 🔴 ✅ FIXED

**Severity:** Medium — pressing RUN/STOP printed a reversed `c` in the
editor (fell through to the print path) and in the shell (`kReadString`
echoed it as a regular char).
**Status:** Fixed 2026-08-27
**Location:** `src/kernel/keyboard.inc` (`readString` rsNotDel dispatch),
`src/tasks/dynamic/edit.asm` (`edNotHome`)

### Root Cause

`$83` (STOP) is a special key (`keyTable` marks `$80+ = special`) but two
"regular char" fall-throughs treated it as printable:
- `kReadString` dispatched `$82`–`$87`, `$81`, `$80` then fell through to
the insert/echo path for **any** remaining key — including `$83` (STOP)
and `$88` (shifted DEL).
- The editor's loop dispatched the same keys but not `$83`, so it fell
through to `edPrint` → `kPrintChar($83)`.

### Fix

- `kReadString`: `rsNotDel` now checks `cmp #$80` and no-ops `$80+` keys
before the regular-char path.
- Editor: `edNotHome` now intercepts `$83` (no-op for now; Step 3 turns
it into the command prefix).

---

## Bug #64: `kMul16` Dropped the Upper Product Bits (16-bit Result) 🔴 ✅ FIXED

**Severity:** High — `kMul16` was documented (and named) as a 16×16→32
multiply, but the implementation produced correct results only when every
partial product fit in 16 bits. Any product ≥ $10000 was garbage.
**Status:** Fixed 2026-08-28
**Location:** `src/kernel/math.inc` (`mul16`)

### Root Cause

The original loop shifted the **16-bit multiplicand left** (`asl kArg;
rol kArg+1`) and discarded the carry out of `rol kArg+1` — the
multiplicand's bit 16 and above. The add chain also stopped at
`kResult+2` (`adc #0; sta kResult+2`), so `kResult+3` never accumulated
anything. The routine was effectively a 24-bit multiply. It only
survived because every existing caller (`surfSlotToReu`: `slot*$28`,
slot ≤ 27) used small products. The first caller needing a real 32-bit
product — `gxEllipse` (Step 9, `rx²·ry` etc.) — drew collapsed ellipses
(an `rx=160,ry=100` ellipse rendered ~8 px wide).

### Fix

Rewrote `mul16` with the shift-right accumulation form: shift the 32-bit
result right each iteration, test the multiplier's LSB, and add the
16-bit multiplicand into the result's **high word**; the add's carry-out
is fed straight into `ror kResult+3` (bit 31), so nothing is lost. Kept
at exactly 61 bytes (`lda #16; tax` + one documented `nop` pad) so the
frozen dyn-table ABI addresses asserted in `main.asm` do not move.

---

## Bug #50: `run` Fails With "? no free dyn slots" Despite Free Slots/Pages 🔴 ✅ FIXED

**Severity:** High — user-visible spawn failures with no actual resource
shortage; misleading error message.
**Status:** Fixed 2026-08-24
**Location:** `src/kernel/syscall.inc` `spawnCore` (`scAlloc`/`scScan`),
`src/kernel/dynload.inc` (`dlSpawnErr`), `src/tasks/dynamic/shell.inc` (`rfErr`)

**Symptoms:**
- `run shell` (or any reentrant/multi-instance task) intermittently
  failed with `? no free dyn slots` even though `ps` showed free task
  slots and the pool had free pages. Repro: mixed-size spawn churn —
  e.g. `run shell` (data_size 101) after `run basic`/`exit` cycles
  (data_size 6) fails while `run basic 5` still works.

### Root Cause

Per-instance data buffers are bump-allocated from a 704B heap
(`$1D40-$1FFF`) that **never rewinds**. On respawn, `spawnCore` reused a
dead slot's buffer **only when the respawning task had the exact same
data_size**. Mixed-size churn (shell=101, basic=6, ps=23 …) therefore
stranded dead buffers forever: each size-switch leaked one buffer, the
heap walked to its end, and `spawn` returned `kArg=1` — which the loader
reported with the misleading generic error 0 `? no free dyn slots`.

### Fix applied

1. **Size-class scavenge** (`scAlloc`/`scScan` in `syscall.inc`): when
   the slot's own buffer is missing or the wrong size, spawnCore scans
   **all dead task slots** (never live/paused/swapped) for a same-size
   buffer and adopts it — clearing the donor's size+pointer record so a
   future spawn into that slot cannot double-own the buffer. Exhaustion
   falls through to the normal fresh bump-allocation. Dead buffers are
   now recycled across slots, so the heap only grows when a size class
   has genuinely never been freed.
2. **Honest loader errors** (`dlSpawnErr` in `dynload.inc`): spawn
   failures now map to loader error **12** (`? instance heap full`) or
   **13** (`? no free task slots`) — both new codes, dispatched in the
   shell's `rfErr` with dedicated messages. Error 0 (`? no free dyn
   slots`) stays reserved for the dynamic-task table being full.
3. **Task registry moved `$132C` → `$186F`** (`main.asm`, `tasks.inc`,
   `kernel-data.inc`): the kernel core block was full to the byte at
   `$132C`. `taskLo`/`taskHi` now live in the free pocket after the REU
   FS block; the FS end assert was tightened to `≤ taskLo` so FS growth
   into the registry fails the build. `osName`/`osVersion` also moved
   (now `$AC7`/`$AD0`); aliases + asserts updated and all task binaries
   rebuilt.

### Follow-up fix (same day) — orphan free-list

The scavenge alone did NOT stop the leak: the first spawn that landed
in a dead slot with a **different** size overwrote the slot's record
before any scan could see it — orphaning one buffer per shell↔basic
alternation (101+6 B/cycle ⇒ heap full after ~6 cycles). Fixed with:

- **Orphan free-list** (`freeListSize/Lo/Hi` × 4 at `$4304`, 2026-08-24):
  on a mismatch spawn, the old buffer is pushed onto the list first;
  after the dead-slot scan fails, spawnCore pops a same-size entry from
  the list before bumping the heap. Orphans now survive their slot's
  record being overwritten, so alternation recycles instead of
  stranding. Size-0 pushes a harmless no-op entry; a full list degrades
  to the old orphan behaviour.
- **Pre-bump room check** (`scFresh`): `dataHeapEnd − dataHeap ≥ size`
  is checked BEFORE advancing the heap, so the overflow rollback (and
  its `kArg+2/3` scratch save) was deleted entirely.
- **Boot clear**: `init.inc` zeroes the free-list sizes during reset
  (`dataHeap` resets to `$1D40` on warm reboot; stale entries could
  otherwise point at ranges the fresh allocator hands out again).
- **Retired syscalls 8, 9, 10, 11, 13** (no users; wrappers deleted,
  table entries → `syscallBad` no-op) to make room in the kernel core
  block. `sleep`'s IRQ tick scan was kept (defensive).

### Notes

- The scan/free-list run under SEI (all `spawnCore` callers are syscall
  dispatcher paths), preserve X (the spawned slot — `dlRegister`
  depends on it), and only touch A/Y. Thread children (`kArg+1=0`)
  never reach the allocator.
- Per-task arrays are PRG-initialized zeros, so cold-boot slots can
  never match a size class with garbage pointers; the boot reset clears
  the free-list whenever `dataHeap` is re-initialised.
- The kernel core block (`$0810-$1359`) has only 3 bytes of headroom
  before `dlCopyArgs` — any future core growth must reclaim bytes or
  move `dlCopyArgs`.

---

## Bug #51: `pool` Totals Misprinted — `kByteToDec` Corrupted Every Multi-Digit Number 🔴 ✅ FIXED

**Severity:** High — every kernel decimal conversion printed wrong digits
when the digits differed (`pool` showed "144 pages, 22 used, 88 free").
**Status:** Fixed 2026-08-25
**Location:** `src/kernel/math.inc` `byteToDec` (kernel routine behind
`kByteToDec`, used by the shell's `pcDecA`/`pcDec3Z`/`pcDec3`)

**Symptoms:**
- `pool` printed `$5400-$bbff, 144 pages, 22 used, 88 free` — and the
  numbers appeared frozen while tasks ran.
- The real values were 104 / 22 / 82: the counting code was correct,
  but the **decimal printing** was broken.

### Root Cause

`byteToDec` emitted each digit with `txa; ora #$30` and then kept using
A for the next digit stage — but A held the **screen code**, not the
remainder. The tens stage of 104 ran on `'1'` = `$31` (49), producing
tens 4 and ones `'4'` again → "144"; 82 printed as "88"; 20 printed as
"22". Numbers with equal digits (22, 11 …) masked the bug, which is
why `22 used` looked fine while everything else was wrong.

### Fix applied

`byteToDec` rewritten (2026-08-25) with the repeated-subtraction
algorithm (digit counters initialised to -1, `sbc`/`bcs` loops,
`adc` restore), writing digits straight into the output buffer at
`(kPtr)` and pushing/popping the remainder around every digit emission
(no scratch variables — multitask-safe). `104` → `"104"`, `82` → `"82"`,
`20` → `"20"`, with the middle zero printed when hundreds ≠ 0.

The routine grew 18 bytes — the dyn-task/malloc/lib-descriptor kernel
data moved `$CA89+` → `$CA99+` (`poolBitmap` `$CAF7` → `$CB07`); all
task aliases (`kernel-data.inc`) and build asserts (`main.asm`)
updated, kernel rebuilt with 109 asserts passing.

### Related change (same day)

`pool` now prints its used/free totals at the **bottom**, accumulated
from the listed rows (task code, program-RAM, malloc, libraries) via
`pcPoolAdd` — the totals always match the listing above them.

---

## Bug #48: Char Shadow Scroll Drops Last Row (Off-by-One) 🔴 ✅ FIXED

**Severity:** High for shadow consumers (`kReadRow` editor, `edit.tsk` `kCharAt`) — shadow diverges from framebuffer after a scroll
**Status:** Fixed 2026-08-19
**Location:** `src/kernel/char-shadow.inc` `pcShadowScroll`

**Symptoms:**
- After a screen scroll (e.g. a long BASIC `LIST`), the char shadow's
  second-to-last row (row 23) reads blank while the framebuffer shows the
  scrolled-up text from the old bottom row.

### Root Cause

`scrollScreen` shifts the framebuffer and matrix **24 rows** (rows 1-24 →
0-23) and blanks row 24. `pcShadowScroll` shifted only **23 rows** (920
bytes, rows 1-23) and blanked **2 rows** (80 bytes, rows 23-24). The
character printed at row 24 — which triggers the scroll — was therefore
discarded from the shadow instead of moving to row 23, so the shadow
diverged from the framebuffer by one row.

### Fix applied

`pcShadowScroll` now copies 960 bytes (rows 1-24) and blanks 40 bytes at
`shadow+$3C0` (row 24 only), matching the framebuffer scroll. **Follow-up
fix (2026-08-20):** the row-24 fill initially wrote `taskShadowHi` as the
REU hi byte without adding the `$3C0` offset's high byte (+`$03`) — the
fill landed at `shadow+$C0` (row 4) instead of `shadow+$3C0` (row 24),
leaving the new bottom shadow row stale (`@` glyphs after a scroll). The
fill now does `clc; adc #>$3C0`. A build-time
assert (`char-shadow region fits under ZP shadows`) also pins the
`SHADOW_BASE + MAX_TASKS*$400 ≤ REU_ZP_BASE` invariant that keeps the
24-slot shadow region from overlapping the per-task ZP shadows at `$8000`.

**Second follow-up (2026-08-20):** the `@` still appeared after any
scroll. Real root cause: `REU_BOUNCE` was at `$1800`, and the scroll's
7680-byte pixel round-trip spans `$1800-$35FF` — **overlapping the live
char shadows `$2000-$35FF`** (shadow slots 0-5). Every scroll clobbered
BASIC's shadow (slot 1, `$2400-$27FF`) with framebuffer pixel bytes, so
`kReadRow` read garbage (`$00` = `@`). The bounce moved to `$A000` (the
legacy dead char-shadow region, never read). `$1800-$1FFF` is now free;
the dead-shadow boot seed loop still runs for Bug #40 timing and the first
scroll clobbers `$A000-$BDFF` — harmless.


---

## Bug #49: Editor Typing Erases the Previous Character (Stale Blink Restore) 🔴 ✅ FIXED

**Severity:** High — typing in the EhBASIC editor intermittently replaces
a just-typed character with the one before it.
**Status:** Fixed 2026-08-20
**Location:** `src/kernel/gfx.inc` `printCharAt` (new `kPrintCharAt`)

**Symptoms:**
- While editing in `run basic`, typing a character occasionally erases
  the character you just typed (it flips back to the previous glyph), or
  leaves a phantom block. Intermittent — depends on the 0.5 s blink phase.

### Root Cause

The new `kPrintCharAt` (writes one cell without cursor advance/scroll)
wrote the glyph and matrix nibble but **never erased a live blink
block**. When the block was ON (`cursorVisible=1`), the blink's saved
glyph + nibble still sat in the framebuffer matrix tail
(`$07EB`/`$07F0+`/`$07EC-D`). The next blink OFF-phase restores those
saved bytes at the saved position — overwriting the character just typed.
`kPrintChar`'s displayed path already erases the block first (Bug #42);
`kPrintCharAt` skipped that step.

### Fix applied

`printCharAt` now performs the same blink erase `kPrintChar` does, before
the glyph blit: if `cursorVisible`, restore the saved glyph (8 bytes) and
matrix nibble at the blink's **saved** position (`GFX_CURSOR_SCOL/SROW`),
then clear `cursorVisible` so the next blink is a fresh ON at the new
cell. The restore targets the saved position (never the live cursor) for
the same reason as Bug #42 — the editor renders from column 0, so the
block sits where the blink last drew it, not at the write cell. The erase
is guarded by `cursorVisible` and so costs ~3 cycles after the first cell
of a render.

**Follow-up (2026-08-20) — same defect in `scrollScreen`:** the scroll
routine *did* save/restore `cursorOn`, but its blink erase un-XOR'd the
**live** cursor cell (`jsr cursorAddr`) instead of restoring at the
blink's saved position. After a cursor move (arrow key) followed by a
wrap-scroll, the un-XOR hit the wrong cell (inverting clean text),
stranded the real block as a phantom that scrolled up a row, and restored
the nibble onto the wrong cell. The erase is now factored into a shared
`eraseCursor` routine (`gfx.inc`) that restores the saved glyph + nibble
at `GFX_CURSOR_SCOL/SROW` and is called from `printChar`, `scrollScreen`,
and `printCharAt` — fixing the cursor artifacts after scrolling.


---

## Bug #41: `ps` Output Corrupted While `clock` Runs  ✅ FIXED

**Severity:** Medium — wrong output position (shell Phase A)  
**Status:** ✅ Fixed 2026-08-28 — clock reworked to `kPrintTime(X,Y)` (no cursor dance)  
**Location:** `screen-io.inc` (`surfDisplayed`), `scheduler.inc` cursor sync, `screen-mgmt.inc` `shareScreen`, `syscall.inc` `sysExit` cursor flush, `src/tasks/dynamic/clock.inc`

**Symptoms:**
- With `clock` running (shares the shell's screen, redraws the time at
  (0,0) every tick), issuing `ps` printed the task table at the wrong
  position — clock's time redraw interfered with `ps`'s output.
- `ps` alone (no clock) worked correctly.

### Root Cause

Clock shared the shell's displayed surface and moved the shared cursor
during its time redraw (`kGetCursor` → `kSetCursor(0,0)` → `kPrintTime` →
`kSetCursor` restore). A sharer's cursor is synced to the
framebuffer tail, so clock's transient cursor positions leaked to the
shared cursor, displacing `ps`'s output.

### Resolution (2026-08-28)

`kPrintTime` was reworked to accept an explicit `(X, Y)` blit position and
draw via `kBlitChar` — no cursor save/restore, no cursor advance. The
clock now does `ldx #0; ldy #0; jsr kPrintTime` under the screen lock, so
the cursor dance is gone and there are no transient positions to leak to
the shared framebuffer tail.


---

## Bug #42: Phantom Cursor Block Stranded by Misplaced Live un-XOR 🔴 ✅ FIXED

**Severity:** Medium (cosmetic + one corrupted glyph per occurrence)
**Status:** Fixed 2026-08-16 (Step 5 Phase A testing)
**Location:** `screen-io.inc` `printChar` (`pcFocused`/`pcUnxorLive`)

**Symptoms:**
- `dir` occasionally leaves a phantom cursor block between the file
  list and the "N file(s) … free" footer, and the first character the
  shell prints afterwards is XOR-garbled. Intermittent (~3-6% of runs —
  it needs a blink ON to land in the task's exit window).

### Root Cause

`printChar`'s focused path un-XORed the blink block at the **live**
cursor, assuming the block was drawn there. The block's true location is
the blink's **saved** position (`GFX_CURSOR_SCOL/SROW`). Sequence:

1. While `dir` runs (sharer — `kShareScreen` does not transfer focus),
   every preemption syncs dir's live cursor into the shared framebuffer
   tail, and the blink's non-focused path draws the block at that tail
   position.
2. In dir's exit window (after the last `printChar`, during
   `kUnlockScreen`/exit) a blink ON fires at the then-current tail
   position P1 (end of the list / blank line). dir exits without
   printing again, so the sharer-path cleanup never runs.
3. The final `svCurSave` moves the tail to dir's post-footer position
   P2; the shell resumes with live cursor = P2.
4. The shell's first `printChar` takes the focused path and un-XORs the
   **live** position P2 — the block stays stranded at P1 (phantom
   between list and footer) and P2's clean cell gets XOR-corrupted.

### Fix applied

`printChar` now always restores the saved glyph + matrix nibble at the
blink's **saved** position (the old sharer path, unified for both
focused and sharer cases) and clears `cursorVisible`. This is correct in
every case: the saved cell snapshot is exactly the cell the block covers,
regardless of how the cursor or focus moved since the blink drew it. The
`pcUnxorLive`/`pcUnxor` live-XOR path was deleted (~15 bytes saved).


---

## Bug #43: Cursor Blinks at (0,0) on Graphics Surfaces (e.g. `gfxdemo`) 

**Severity:** Medium (cosmetic — a blinking XOR block over the bitmap)
**Status:** ✅ FIXED 2026-08-19 — verified in VICE (symptom; inheritance follow-up below)
**Location:** `screen-io.inc` `csrBoth` (tail-jmps `csrSyncTail` in
`gfx.inc`), `src/tasks/dynamic/gfxstep45.inc`

**Symptoms:**
- Running `gfxdemo` blinks a cursor block at the top-left of the bitmap.

### Root Cause

`allocScreen` treats every fresh hires-text surface (mode 0) as a text
surface: `seedSlotCursorTail` writes (0,0)+ON=1 into the slot tail and
sets the live `cursorOn=1`. gfxdemo allocates mode 0, never disables
its cursor (maze does — the opt-out pattern), and its matrix fill wrote
**1024 bytes** (`$0400`) over the slot tail, clobbering the seeded bytes
with `$10`. On `kSwitchView` the framebuffer tail inherits the slot
tail (ON≠0), the scheduler's focused restore loads it into the live
cursor, and `kbDoBlink` draws the block at (0,0) — visible because the
matrix fg nibble is white (Bug #39's matrix write makes the XOR block
render on any background). A `kDisableCursor` alone was not reliable:
the old `csrBoth` only set the live byte and relied on the scheduler's
per-switch tail sync — no preemption between the disable and
`kSwitchView` meant the seeded ON=1 tail survived.

### Failed attempt (reverted)

Loading the incoming surface's slot-tail cursor with inline REU DMAs in
`switchView` after `doGfxSwitch`. Two defects: (a) a KickAssembler
precedence bug in the REU address math (`#>BASE + OFFSET` parses as
`#$20 + $3E8` → wrong high byte), and (b) the added DMAs broke the boot
path (shell banner printed, then no cursor and no keyboard input — exact
cause under investigation, likely an REU-register interaction at first
switch). Fully reverted; boot verified.

### Original planned fix (open follow-up — inheritance)

Route the **focused** task's cursor save/restore through the per-surface
slot tails (the non-focused path already does), keeping the framebuffer
tail as a display mirror for the blink's non-focused path:
- save: displayed task writes col/row to tail (existing) AND slot (new);
  ON to tail (for the blink) AND slot (new)
- restore: displayed task reads col/row/ON from its slot (instead of
  tail)

Fresh surfaces have zeroed slot tails (`surfClearSlot`) → graphics tasks
get cursor (0,0), OFF — no blink; text tasks re-enable via
`kEnableCursor`; the shell's prompt cursor survives F-key round-trips.

### Fix applied (2026-08-19 — symptom)

1. `csrBoth` (`kEnableCursor`/`kDisableCursor`) now **syncs `cursorOn`
   to the surface's matrix tail immediately** — framebuffer tail when
the surface is displayed, else `slot+$23EA` via a 1-byte REU DMA
(SEI-guarded). The body lives in `gfx.inc` as `csrSyncTail` (the
pre-`$F063` flow has no spare bytes); screen-io tail-jmps it. Enable/
disable is now authoritative regardless of preemption timing.
2. `gfxstep45.inc`: calls `kDisableCursor` after alloc (the maze
pattern) and its matrix fill is now 1000 bytes (`$03E8`) — it no
longer overwrites the slot cursor tail.

**Open follow-up (inheritance):** the focused path still restores
col/row/ON from the framebuffer tail instead of the per-surface slot
tail (the 2026-08-16 attempt was reverted — two defects recorded in
"Failed attempt" above). No longer user-visible: fresh surfaces opt in
via `kEnableCursor` and graphics tasks opt out deterministically.

**2026-08-25 hardening:** the shell's `run` command now calls
`kDisableCursor` before `kDynLoad` and `kEnableCursor` afterwards (on
both the success and error paths, preserving `kArg`/`kPtr` around the
calls) — a spawned task that switches to its own surface can no longer
leave a phantom cursor when the user F-keys back to the shell.


---

## Bug #44: `gfxdemo` Hangs Partway When Restored After Eviction 🔴 ✅ FIXED

**Severity:** Medium — localized task hang (does not crash the whole system)
**Status:** Fixed 2026-08-20
**Location:** `dynload.inc` `dlRestore` (Phase D), bitmap-task restore path
(`kAllocBitmap` → `kSwitchView` → `kPlot`/`kLine`/`kBox`)

**Symptoms:**
- `run gfxdemo` → `run basic` (evicts shell + gfxdemo) → `exit` restores
  the shell fine; gfxdemo is then restored but **hangs halfway** — the
  bitmap is only partially drawn and the task never reaches its yield
  loop.
- Works on first run and on repeated runs — **only the eviction-restore
  path hangs**.
- Text tasks (shell/border/maze/clock/threads) restore cleanly through the
  same `dlRestore` loop; only the bitmap task hangs.

### Investigation so far
- An `allocScreen` "reuse the preserved slot" change was tried and
  **reverted** — it made no difference, ruling out a plain slot leak.
- The hang does not crash the whole system (shell stays usable), so it is
  deferred rather than blocking the eviction feature.

### Suspects to explore
- Bitmap display state after the cold restart (`dispIsBitmap`, VIC mode,
  `surfLoad` of the preserved slot).
- Fixup re-application when a task's code lands at a different pool
  address on restore (gfxdemo has 2 fixups).
- Focus hand-off: after restore both the shell and gfxdemo call
  `kSwitchView`; the resulting focus/display state may leave the bitmap
  half-drawn.

---

## Bug #45: Shell Loses Keyboard Focus Intermittently After Restore 🟡 ✅ FIXED

**Severity:** Medium — intermittent keyboard loss (recovers on F-key switch)
**Status:** Fixed 2026-08-20
**Location:** focus hand-off after restore — `transferFocusAway`
(`syscall.inc`), `switchView` (`screen-mgmt.inc`), `dlRestore` (`dynload.inc`)

**Symptoms:**
- Scenario 2 (`run border` → `run maze` → `run basic` → `exit` →
  `kill <border-id>`): maze is restored, but the shell intermittently
  stops receiving keyboard input — `kReadString` sees `curTask != kbFocus`
  and returns empty, so the terminal appears dead.
- Intermittent — does not reproduce every run.

### Suspects to explore
- `kbFocus` left pointing at a restored task (maze/border) instead of the
  shell, or at a stale slot after `transferFocusAway` blanks it to `$FF`.
- Restore order: `dlRestore` restores shell + border + maze together;
  whichever task runs first after the reschedule ends up with focus
  (`kSwitchView`), and the shell's `kReadString` then spins unfocused.
- `transferFocusAway` clobbers `kArg` (fromTask) — the cascade-kill path
  already guards this, but the exit path may not.

---

## Bug #37: Intermittent Visual Artifacts from REU Context Switching 🟡

**Severity:** Medium (cosmetic — does not affect correctness)  
**Status:** Partly fixed 2026-09-23 — the switch blanks the display; one colour mechanism remains unverified  
**Location:** REU DMA in `scheduler.inc` (context switch), `screen-io.inc` (scroll), color RAM save/restore

**Symptoms:**
- Cursor occasionally displays a stale character when not blinking (ghost character under cursor)
- Characters or the cursor occasionally turn blue
- Color RAM separation between text screens is sometimes buggy — most noticeable when one task's screen scrolls, causing another task's colors to bleed
- 'Snow' appearing on the screen, and increasing as the number of tasks using different screens increasese

Some of these are rare, intermittent, and do not necessarily affect correct text display or program execution.

### What was checked (2026-09-23), and what it rules out

**Cause 3 — "IRQ mid-scroll + context switch REU DMA could interleave" — is disproven.** Every REU
transfer in the kernel excludes interrupts around setup + trigger + poll, so no two can overlap:

| Site | Protection |
|---|---|
| `kReuToC64`/`FromC64`/`FillC64`/`FillReu` (`reu.inc`) | `php`/`sei`…`plp` — "IRQs remain disabled for the entire operation" |
| `syscall` dispatcher (`syscall.inc:44`) | `sei` at entry, `cli` out — so **every** switch (tick *and* `sysYield`/`sysSleep`) runs with I=1 |
| `doGfxSwitch` (`gfx.inc`) | `php`/`sei` — so the 20 KB surface transfer cannot be interrupted |
| `rsDelErase`, `readRow`, `csrSyncTail`, `printGlyphAt` | own `sei`; two of them say so in the comment ("...**or the IRQ's REU traffic**") |
| `pcShadowChar` callers | `screen-io.inc` marks both "(SEI held)"; `scrollScreenFocused` holds it |
| `reuDirPageDma` | "Caller must SEI (all FS kernel calls wrap in SEI/CLI)" |
| `scheduler.inc` (7 DMAs per switch) | dispatcher SEI, and the timer path is already I=1 |

Because the IRQ handler *does* use the REU (the per-switch kArg shadow / ZP shadow / cursor DMAs),
the SEI across a whole transfer is not gratuitous — it **is** the mutual exclusion. This is exactly
caveat 2 of the 1750 guide's production-library list ("whether IRQ handlers can use the REU"), and it
is why the guide's own advice ("normally allow interrupts while a long DMA transfer is in progress")
cannot be followed without making the handler's DMAs deferrable. The cost is measurable: a screen
switch runs ~20 KB with I=1 (~21 ms, two timer ticks) and a scroll bounce ~15,360 bytes (~16 ms, one
tick), during which keyboard scanning, sleep ticks and `rasterCommit` all stall (the sprite commit is
*skipped* by its deadline guard, not torn — benign).

The survivor of the whole review is a single unprotected transfer: `getTaskName`'s thread-name DMA —
see bug #133.

### Remaining symptoms, per symptom

- **Ghost character under the cursor** — *proven*, and fixed: it is bug #83 (a live block cursor baked
  into the outgoing surface by `surfSave`), whose kernel path is now closed.
- **"Snow" / a half-restored screen on a switch** — fixed by blanking. `doGfxSwitch` now clears `DEN`
  (`$D011` bit 4) before the hand-over and restores it with the incoming surface's mode bits, so the
  20 KB transfer happens with the display off. Cost: 8 B of chain code (kernel free total 184 -> 176 B)
  and one black frame where the torn screen used to be.
- **Characters or the cursor turn blue / colour RAM bleeds between surfaces** — *partly* addressed by
  the same change, and still not *proven*. The blanking removes the VIC's `c`-accesses for the duration
  of the transfer (a `c`-access is what reads colour RAM: "video matrix and Color RAM", VIC-II article
  section 3.6.2), so if the symptom is the VIC's colour-RAM fetches winning against the REU's writes,
  blanking removes the conflict. The other candidate — the mode and colour registers being programmed
  **after** the pixels, matrix and colour RAM are loaded — is also removed by the new order (blank,
  transfer, program, unblank). What is *not* closed: neither the VIC-II article nor the local 1750
  guide states what happens when the REU, a third bus master, writes colour RAM while the VIC fetches
  it. If the symptom survives, the decisive test is a task-side one: DMA a known pattern into `$D800`
  during the visible area, read it back (and watch the screen) at several start rasters.


---

## Bug #38: Boot Banner Randomly Missing — Screen Hand-Off Race 🔴 ✅ FIXED

**Severity:** High (cosmetic, but user-visible and intermittent)  
**Status:** Fixed 2026-08-01  
**Location:** `src/syscall.inc` — `spawnCore` extraction; `src/tasks/boot.inc` — spawn→exit sequence

**Symptoms:** On boot, the OS banner (name/version + `[shell] started`) sometimes appears and sometimes doesn't — occasionally you land straight on a blank shell. Non-deterministic across otherwise identical runs.

### Root Cause

`sysSpawn` unconditionally does `cli; rts`, re-enabling interrupts. Boot spawns the shell, but between spawn returning and boot reaching `sysExit` (which marks it DEAD), an IRQ can schedule the shell. `kTakeScreenOwnership(0)` fails because `taskState+0 != 0` (boot is still ALIVE), shell allocates a blank screen, banner lost.

The previous `sei`-after-spawn fix only narrowed the window to ~10 cycles but couldn't close it because `spawnDone`'s `cli` preceded boot's `sei`.

### Fix Applied

Extracted `spawnCore` — the entire spawn logic as a subroutine that does **not** touch the interrupt flag. `sysSpawn` (#1) and `sysThreadSpawn` (#12) wrap it with `cli; rts` for normal syscall use. Boot bypasses the syscall entirely:

```asm
boot:
    ; ... print banner ...
    sei                         ; atomic from here
    ; set taskNamePtr, spawnPtr, kArg+1
    jsr spawnCore               ; raw spawn — no CLI
    ; flush cursor state
    lda #0; sta taskState       ; mark slot 0 DEAD
    jmp reschedule              ; shell runs only after this
```

`spawnCore` does the same work as the old `sysSpawn` but returns with `rts` (no CLI). The entire boot spawn→flush→exit sequence runs with interrupts disabled. The shell is scheduled only after boot is DEAD, so `kTakeScreenOwnership(0)` always succeeds. No special cases, no task-level awareness, no logic duplication — `spawnCore` is the single source of truth for spawn logic, shared by all three callers.

> **2026-08-08:** Architecture simplified — boot no longer allocates a screen or prints
> the banner. Shell always uses `kAllocScreen` directly for orthogonality. The
> SEI-atomic spawn still applies, ensuring shell isn't scheduled before boot dies.

---

## Bug #39: XOR Cursor Invisible Over Empty Cells (all-bitmap) 🔴 ✅ FIXED

**Severity:** Medium (cursor unusable on blank screens)  
**Status:** Fixed 2026-08-14  
**Location:** `src/kernel/keyboard.inc` — `kbDoBlink`

**Symptoms:** In the all-bitmap display model, the cursor block only
appears when it sits over a typed glyph — on empty (never-printed) cells
it is invisible.

### Root Cause

`kbDoBlink` XORs the 8 pixel bytes of the cursor cell, but a pixel's
color comes from the video-matrix high nibble (hires mode). Empty cells
have matrix `$00` (foreground black), so the XOR'd 1-bits render
black-on-black. Printed cells carry the text color in the high nibble,
which is why the cursor only showed over text.

A second, latent variant: when the last focus-owning task dies
(`kbFocus = $FF`), the blink still ran, painting a ghost cursor at the
stale `$07E8`/`$07E9` position with `taskCursorColor[$FF]` garbage.

### Fix Applied

`kbDoBlink` now also writes the cursor cell's matrix byte as
`(cursorColor << 4) | (old & $0F)` on every blink phase (idempotent),
using `taskCursorColor[curTask]` on the live path and
`taskCursorColor[kbFocus]` on the matrix-tail path — and bails out when
`kbFocus = $FF`. The matrix write self-heals: when the cursor moves, a
stale foreground nibble is harmless (a space glyph renders as background)
and any later `kPrintChar` rewrites the byte.

> **Follow-up (2026-08-14):** `readString`'s erase block (keypress path)
> now also restores the cursor cell's matrix nibble to the task's text
> color — before, text the cursor passed over while editing permanently
> kept the cursor color. Also, `kbDoBlink` early exits now set
> `blinkTick=1` so a disabled cursor can't leave a 255-tick (~4 s)
> underflow delay before the blink resumes.
>
> **Follow-up 2 (2026-08-14):** the pure XOR-toggle model could strand
> phantom blocks or recolor characters when the cursor moved between
> blink phases. Replaced with **save/restore**: ON saves the cell's
> glyph + matrix nibble in the framebuffer matrix tail (`$07F0+`/`$07EB`),
> OFF restores them at the saved cell; `kPrintChar` erases a live block
> before moving the cursor; display switches reset the phase to ON so a
> stale saved glyph is never restored onto the incoming surface.

---

## Bug #46: Cursor Invisible at Columns 16–31 (Shell Typing) 🔴 ✅ FIXED

**Severity:** Medium — cursor unusable over a 16-column band
**Status:** Fixed 2026-08-17
**Location:** `keyboard.inc` `kbGo`/`kbRestore`, `screen-io.inc`
`printChar` saved-cell restore, `screen-io.inc` `cursorAddr`

**Symptoms:**
- In the shell, the blink cursor disappears once typing passes column 16
  and reappears at column 32 — on every row.

### Root Cause

All four cursor cell-address computations add `col*8` to a 16-bit base
the same broken way:

```asm
    clc
    adc kPtr
    sta kPtr                    ; C now = real carry from the lo add
    lda col
    lsr; lsr; lsr; lsr; lsr     ; BUG: lsr CLOBBERS C with bit4(col)
    adc kPtr+1                  ; adds bit4(col) instead of the lo carry
    sta kPtr+1
```

The `lsr` chain overwrites the carry flag from the low-byte add with bit
4 of the column, so for columns 16–31 (`bit4 = 1`) the high byte gets a
spurious `+1` even when the low add didn't carry. The resulting `kPtr`
is `$100` (32 cells) too high — the XOR block is drawn one row above the
cursor and is invisible, while the matrix nibble is written at the
correct cell (whose pixels were never XOR'd). At column 32 the `col>>5`
term is 1 and bit 4 is 0, so the sum is accidentally correct again —
exactly the observed 16→32 band. The band starts at 16 because column 16
is the first column whose bit 4 is set.

### Fix Applied

Compute `col>>5` with the carry preserved, then add the high term with a
clean carry — `bcc skip / inc kPtr+1 / skip:` before the `lsr` chain, and
`clc` before the final `adc kPtr+1`. Applied to all four sites:
`kbGo` (live blink), `kbRestore` (saved-cell restore), `printChar`'s
saved-cursor restore, and `cursorAddr` (`kCursorAddr`). `blitChar` is
unaffected (it builds the cell offset with `asl/rol` on a 16-bit pair).

---

## Bug #47: `edit` Loader Over-Reads Past End of File 🔴 ✅ FIXED

**Severity:** High — wrong file contents displayed
**Status:** Fixed 2026-08-17
**Location:** `src/tasks/dynamic/edit.asm` `editLoad`

**Symptoms:**
- `run edit boot.bat` printed `run shell` and `exit` correctly, then
  kept printing garbage until the screen was full — it did not stop at
  the end of the 17-byte file.

### Root Cause

The first loader streamed the file in 40-byte `kReuToC64` windows with
`pos`/`size`/`len`/`cur`/`end` tracked in the instance buffer. The
window/position bookkeeping over-read: after the last window the byte
cursor ran past the file's data into the next FS region. The exact faulty
interaction was never isolated — the design was judged too fragile and
replaced rather than patched.

### Fix Applied

`editLoad` now reads the whole file at once: it computes
`len = min(size, 1000)` (1000 = one 25×40 screen) and does a single
`kReuToC64` DMA into a `kMalloc`'d buffer (page-granular, so it works at
any pool page size), then prints the buffer cell by cell, stopping at the
last screen cell, and `kFree`s the buffer.

---

## Bug #40: Exiting the Last Shell Hangs the System (Fresh-Image Only) 🔴 ✅ FIXED

**Severity:** Critical (system unusable after first exit on a freshly-seeded REU)  
**Status:** Fixed 2026-08-15.  
**Location:** Dynamic task loader (`src/kernel/dynload.inc`)

> **Root cause (2026-08-15):** `kArg` is only 8 bytes (`$4000-$4007`:
> kArg(4) + kResult(4)). The loader used `kArg+8` (`$4008`) as scratch
> for `fixup_count` — but `$4008` is `savedSP[0]`, the scheduler's
> per-task saved stack pointer, rewritten on **every** context switch.
> The loader's CLI windows (after `reuFindFile` and `reuDmaRange`) let
> the 60 Hz scheduler fire and overwrite `fixup_count` (80) with the
> boot task's SP. The fixup loop then ran past the 80 entries and read
> the `$ffff` terminator → error 1 (corrupt fixup table) → red-border
> halt / freeze. This explains the timing-dependence and the
> "loader/spawn stack state" lead below.
>
> **Fix:** replaced `kArg+8` with a loader-local `dlFixCount` (in the
> loader's own `$432A-$4FFF` region, never touched by the scheduler).
> The identical `kArg+8`-aliases-`savedSP[0]` bug in `shell.inc`
> `pcPrint24bitFree` (garbled `free` hex display) was fixed at the same
> time by reusing `kArg+4/+5`.
>
> Also landed: the boot task + its (now-removed) load-failure diagnostic
> were moved to `$BC00` (the old home after the border seed had no
> growth headroom and overflowed into I/O `$D000+`).

> **Note (2026-08-15):** the boot self-heal (delete + re-save + retry in
> `src/tasks/boot.inc`) has been **removed** to simplify the boot path.
> Boot now does a single `kDynLoad("shell")`; failure → halt.
> Any evidence below of "the self-heal ran" is therefore historical.

**Symptoms:** Boot with a freshly-scratched REU → shell appears, `dir`
works → `exit` → the text disappears (normal display blanking) → the
system reboots but the shell never respawns: black screen, dark grey
border, no text, dead. With **"Use existing REU"** the exact same exit
respawns the shell cleanly (with or without a prior `dir`).

### Evidence (REU-image forensics of a hung session, 2026-08-14)

- The post-exit reboot's load **succeeds**: the boot task's saved kArg
  shadow holds entry `$7B07`, flags `$05` (reloc+reentrant), `startPage=3,
  pages=3` — i.e. `dlRegister` completed.
- The FS directory contains a **deleted** shell entry (flag `$80`,
  spaces) **plus** a re-saved shell entry at the next free slot
  (`$4BF9`) — **the self-heal ran**: the first post-reboot `kDynLoad`
  failed, boot deleted + re-saved the shell seed and retried.
- Both FS copies (seed `$36F9`, re-save `$4BF9`) are byte-identical to
  `tasks-out/shell.bin` — **no FS corruption**.
- The respawned shell **never executes one instruction** — its REU
  stack image is still the pristine 8-byte `initTCB_ptr` frame.
- Border-marker bisection (three rounds): the freeze sits after
  `spawnCore` completes (`scNoGrow`) and before the loader resumes
  (`rlSpawnOk`) — a straight-line `clc/rts/rts/bcc` span with IRQs off.
  The freeze point **drifts with code-size changes** (added markers
  shifted it; one instrumented build froze on cold boot) → timing/state
  dependent.

### Rejected Hypotheses & Reverted Fixes (all reverted 2026-08-14)

1. **`sysSpawn`'s `cli` opened an IRQ window** while the new task was
   ALIVE but unregistered. Removed the `cli` — no change. (The
   removal was technically sound but not the cause; reverted with
   everything else.)
2. **Scheduler DMA races** (unwaited `saveStackReu`/`restoreStackReu`,
   shared-EOB consumption). Added EOB waits — no change. **Rejected on
   hardware grounds:** REU DMA halts the CPU (bus takeover), transfers
   are synchronous from the CPU's perspective — the wait loops are
   redundant; the user verified removing them changes nothing.
3. **REU FS corruption** — disproven by byte comparison (above).

### Remaining Leads

- The first post-reboot load fails **after pool allocation** (its pages
  0–2 leak; the successful retry takes pages 3–5). Its error code is
  still unknown.
- The freeze with IRQs off in a straight-line `rts` span implies the
  stack content at `jsr spawnCore` is wrong by the time `rts` pops it —
  i.e. the failed first load (or the self-heal) leaves the boot task's
  stack unbalanced or corrupts state the retry depends on.
- Proposed next diagnostic: invisible REU stamping (two bytes written
  to REU at each loader stage + the first load's error code), read from
  the saved image after a hang — no border markers, no halts.

---

## Bug #35: `sysYield` + Color DMA Overlap → System Lock-Up 🔴 ✅ FIXED

**Severity:** Critical  
**Status:** Fixed 2026-07-28  
**Location:** `src/scheduler.inc` — `svAfterSave` color RAM save path

**Fix applied:** Gated the per-task color RAM save on `screenBase+1 == dispBase+1` (task owns displayed screen). The unconditional save fired a 1000-byte REU DMA ~100 CPU cycles after `saveStackReu`'s stack DMA. Overlapping DMA triggers on the REU caused the engine to hang — EOB never set, `reuWait` spun forever with I=1 from the IRQ context, deadlocking the system.

**Symptoms:** Intermittent lock-up after maze scrolls. F-keys stop responding, border diagnostic stops. Scheduler keeps running (maze still scrolls), but IRQs are dead. Removing `sysYield` from maze prevents the lock-up (different inter-DMA timing).

### Root Cause

The scheduler's context switch path does two REU DMA operations close together:
1. `saveStackReu`: stack DMA (C64 `$0100+savedSP` → REU, variable size)
2. Color RAM save: 1000-byte DMA with `reuWait` (C64 $D800 → REU)

Between them: ~100 CPU cycles of cursor-state-save code. For idle tasks the stack DMA takes ~9 cycles and poses no overlap risk; for deeply-stacked tasks it takes longer. The REU engine cannot handle overlapping DMAs.

The fix gates the color save so it only fires when the task owns the *displayed* screen — F-key switches only, not every context switch. This gives `saveStackReu` its full DMA window to complete.

### Reproduction

1. Boot, `run maze`
2. Switch to maze screen (F-key)
3. Wait for maze to start scrolling
4. Press F1 to return to shell — intermittent lock-up

---

## Bug #16: `kPrintAt` Does NOT Preserve X (Documentation Bug) 🔴 ✅ FIXED

**Severity:** Medium  
**Status:** Fixed 2026-07-25  
**Location:** `src/kernel.asm` — `printAt`; `.github/copilot-instructions.md` line 41 & 73

**Fix applied:** Updated the copilot instructions clobber table to show X=✓ (clobbered), removed `kPrintAt` from the X-preserved list in rule 3, and added `kPrintAt` to the screen output routines that clobber X and Y. `kernel.md` was already correct.

### Root Cause

The copilot instructions claim `kPrintAt` preserves X and list it among the safe routines. But tracing the actual code path:

`kPrintAt` → `kPrintString` → `kPrintChar` does `ldx cursorOn` which overwrites X with the cursor enable state. X is never saved or restored by `kPrintAt`. After the call returns, X = the old `cursorOn` value (0 or 1), not the caller's X.

**`kernel.md` is correct** — it lists `Clobbers: A, X, Y`. Only the copilot instructions are wrong.

### Impact

Any AI or developer that follows the copilot instructions and relies on X surviving `kPrintAt` will have their X register silently corrupted. Rule 3 in the instructions explicitly says X is safe through `kPrintAt`, which is false.

### Fix

Either:
- Save/restore X in `kPrintAt` (add `txa; pha` / `pla; tax`), or  
- Remove `kPrintAt` from the X-preserved list in copilot instructions and correct rule 3.

---

## Bug #17: Second `BIT abs` Trick in Cursor Blink Code — SUPERSEDED

**Severity:** Low  
**Status:** Superseded 2026-08-06 — cursor blink handler (`kbDoBlink`) has been completely rewritten with color save/restore and reverse-video cursor. The old BIT-trick code described below no longer exists.
**Location:** `src/kernel.asm` — `kbDoBlink` (old code, now removed)

### Original Report (Historical)

### Root Cause

The same `.byte $2c` skip trick from Bug #14 also exists in the cursor blink handler:

```asm
    lda cursorVisible
    bne kbBlock
    lda #$20
    .byte $2c              ; BIT abs — reads $A9A0
kbBlock:
    lda #$a0
    sta (kPtr),y
```

This reads from `$A9A0` on every blink tick. With `$01=$35`, `$A000` is BASIC ROM (LORAM=1), so the read is harmless but violates the "no tricks" rule.

### Fix

Same pattern as Bug #14 — use `bne kbStore`:

```asm
    bne kbBlock
    lda #$20
    bne kbStore
kbBlock:
    lda #$a0
kbStore:
    sta (kPtr),y
```

---

## Bug #18: Slot 0 Reserved — Not Recyclable 🟡 

**Severity:** Low  
**Status:** Open (intentionally not fixed)  
**Location:** `src/kernel/syscall.inc` — `findFreeSlot`; `src/kernel.asm` — `kbSwitchToScreen`

**Current state:** `findFreeSlot` starts at `ldx #1` and `kbSwitchToScreen` starts at `ldy #1`. Both intentionally skip slot 0. With `MAX_TASKS=24`, only 23 slots (1–23) are usable for spawning. Slot 0's screen is also invisible to F-key switching.

### Root Cause

```asm
findFreeSlot:
    ldx #1              ; slot 0 = boot, never recycled
```

Boot spawns shell then exits, leaving slot 0 DEAD forever. Recycling slot 0 would require making its screen accessible via F-keys in `kbSwitchToScreen` (currently starts at `ldy #1`). The simpler choice is to document the 7-slot effective limit.

### Fix

Either (a) change both `ldx #1`→`ldx #0` and `ldy #1`→`ldy #0`, or (b) document the `MAX_TASKS-1` effective spawn limit.

---

## Bug #19: `sysSpawn` Print Messages Not Atomic ⚫ ✅ FIXED

**Severity:** Low  
**Status:** Fixed 2026-07-25  
**Location:** `src/syscall.inc` — `sysSpawn`

**Fix applied:** Wrapped the "[name] started" print sequence in `kLockScreen`/`kUnlockScreen`. Multiple simultaneous spawns now print their banners atomically — no interleaving.

### Root Cause

The "[name] started" banner uses multiple raw `kPrintString` calls without `kLockScreen`. If two tasks call spawn around the same time, their output can interleave:

```
Task A prints: "[" → preempted
Task B prints: "[demo2] started\n"
Task A resumes: "demo] started\n"
```

Result: `[[demo2] started\ndemo] started\n`

### Fix

Wrap the spawn messages in `kLockScreen`/`kUnlockScreen`.

---

## Bug #20: Redundant `sei` in Syscall Handlers ⚫ ✅ FIXED

**Severity:** Info  
**Status:** Fixed 2026-07-25  
**Location:** `src/syscall.inc` — `sysExit`, `sysKill`, `sysPause`, `sysSetPrio`

**Fix applied:** Removed the 4 redundant `sei` instructions. The dispatcher already executes `SEI` before jumping to any handler. Saved 4 bytes.

### Observation

The dispatcher does `SEI` before jumping to handlers, but `sysExit`, `sysKill`, and `sysPause` each do their own `sei` as the first instruction. The I flag is already set — these are redundant. 3 bytes wasted.

---

## Bug #21: `future-improvements.md` Stale ⚫ ✅ FIXED

**Severity:** Info  
**Status:** Fixed (verified 2026-08-06)  
**Location:** `future-improvements.md`

**Fix applied:** `future-improvements.md` now correctly states "REU is **required** — boot halts if not detected". The software fallback description was removed.

---

## Bug #22: `kViewScreen` Undocumented ⚫ ✅ FIXED

**Severity:** Info  
**Status:** Fixed 2026-07-25  
**Location:** `src/kernel.asm` — `viewScreen`; `kernel.md`

**Fix applied:** Added `kViewScreen` entry to kernel.md (Screen Management section and clobber table). Also corrected `kShareScreen` jump table offset from `$E057` to `$E05A`.

### Observation

`kViewScreen` exists in the jump table at `$E057` and is fully implemented (display-only switch, no keyboard focus change). It is not listed in `kernel.md`. Only `kSwitchView` (which also transfers keyboard focus) is documented.

### Fix

Add `kViewScreen` entry to `kernel.md` screen management section.

---

## Bug #23: `$D018` Not Persisted on Preemption ⚫ ✅ FIXED

**Severity:** Info  
**Status:** Fixed 2026-07-25  
**Location:** `src/scheduler.inc` — `svAfterSave`; `src/kernel.asm` — `allocScreen`

**Fix applied:** Added warnings to both `kernel.md` (`kAllocScreen` entry) and `programmers-guide.md` (Virtual Screens section) that `$D018` must only be set via `kAllocScreen` — direct writes are lost on context switch because the scheduler reads `$D018` from the buffer tail, not from the hardware register.

### Observation

The scheduler saves 5 metadata bytes per context switch: `cursorCol`, `cursorRow`, `cursorOn`, `$D020`, `$D021`. `$D018` is NOT saved — it's only read from the screen buffer tail on restore (written there by `allocScreen`). If a task writes `$D018` directly (bypassing `kAllocScreen`), the value is lost on preemption and reverts to whatever `allocScreen` last wrote.

### Fix

Either add `$D018` to the saved metadata block, or document that `$D018` must only be set via `kAllocScreen`.

---

## Bug #24: `totalTasks` Overflow on Dead-Slot Reuse 🔴 ✅ FIXED

**Severity:** Critical  
**Status:** Fixed 2026-07-25  
**Location:** `src/syscall.inc` — `sysSpawn`

**Fix applied:** Replaced unconditional `inc totalTasks` with `totalTasks = max(totalTasks, slot + 1)`. The scan range now only grows when a new slot is beyond the current ceiling — recycling dead slots below `totalTasks` no longer inflates the counter. Prevents the scheduler from reading `taskState` beyond array bounds and halting.

### Root Cause

`sysSpawn` unconditionally does `inc totalTasks` after every spawn, even when `findFreeSlot` returns a dead slot **below** the current `totalTasks`. `totalTasks` represents `highest_occupied_slot + 1` — the scheduler's scan ceiling. Blindly incrementing causes it to grow past `MAX_TASKS` (8), after which `findNextAlive` reads `taskState` beyond array bounds.

The overflow value reads `totalTasks` itself (e.g., 9). Since 9 ≠ `TA_ALIVE` (1), the scheduler skips it and after `totalTasks` failed probes, the system **halts** with "system halted".

### Reproduction

1. Spawn 7 tasks → `totalTasks` = 8
2. Kill any task in a non-last slot (e.g., slot 0 or 2)
3. `shrinkTotalTasks` doesn't shrink (last slot still alive)
4. Spawn a new task → `findFreeSlot` finds the dead slot → `inc totalTasks` → **9**
5. Scheduler scans `taskState[0..8]`, reads garbage at index 8, halts

### Impact

Any spawn after killing a non-last task when `totalTasks` = 8 crashes the system. Requires ~7 spawns to set up, then trivially reproducible. Affects ALL task spawns, not just re-entrant ones — but re-entrant tasks make it much easier to reach 8 occupied slots.

### Fix

```asm
    ; After finding free slot X, instead of inc totalTasks:
    inx                         ; X = slot + 1
    cpx totalTasks
    bcc spawnNoGrow             ; slot below ceiling, no change needed
    beq spawnNoGrow
    stx totalTasks              ; grow ceiling to include new slot
spawnNoGrow:
```

---

## Bug #25: `taskNamePtr` ZP Race Between Shell Instances 🔴 ✅ FIXED

**Severity:** High  
**Status:** Fixed 2026-07-25  
**Location:** `src/tasks/dynamic/shell.inc` — `pcKill`, `pcPause`, `pcResume`

**Fix applied:** Replaced all uses of shared ZP `taskNamePtr` ($F3-$F4) with a new `SHELL_NAMEPTR` offset (47) in each shell instance's per-task `(dataPtr),y` buffer. Since `dataPtr` is saved/restored per-task by the scheduler, each shell instance now has its own private name pointer storage. `SHELL_DATA_SIZE` bumped from 48 to 49 bytes to accommodate the 2-byte pointer.

### Root Cause

`taskNamePtr` ($F3-$F4) is shared ZP that is **not** saved/restored by the scheduler. Multiple shell instances use it as a temporary for the target task's name across a syscall boundary:

```asm
; pcKill in shell.inc
    jsr kGetTaskName           ; kPtr → name
    lda kPtr
    sta taskNamePtr            ; ← save name for post-syscall print
    ...
    lda #2
    jsr syscall                ; sysKill — does CLI on return
    ; ⚠️ IRQ can fire HERE, switch to another shell instance
    lda taskNamePtr            ; ← may read CORRUPTED value
    sta kPtr
```

`sysKill` (non-self) ends with `cli; rts`. Between the `cli` and the shell reading `taskNamePtr`, an IRQ can preempt and switch to another shell instance that overwrites `taskNamePtr` with a different name. The identical pattern exists in `pcPause` and `pcResume`.

### Impact

Shell prints the wrong task name after `kill`/`pause`/`resume` commands. With 2+ shell instances running, this is reproducible. The user sees e.g. `killed demo` when they actually killed `maze`.

### Fix

Either (a) add `taskNamePtr` to the scheduler's per-task save/restore arrays alongside `kPtr`/`srcPtr`/`kArg`/`dataPtr`, or (b) have the shell store the name string in its own `(dataPtr),y` buffer (using a `.const` offset like `SHELL_NAMEBUF`) instead of shared ZP.

---

## Bug #26: Spawn Parameter Race (`spawnPtr` / `taskNamePtr`) 🔴 ✅ FIXED

**Severity:** High  
**Status:** Fixed 2026-07-25  
**Location:** `src/tasks/dynamic/shell.inc` — `pcRunSpawn`

**Fix applied:** Added `sei` at the start of `pcRunSpawn` before setting `taskNamePtr`/`spawnPtr`. The ~38-cycle unprotected window between setting spawn parameters and the dispatcher's `SEI` is now closed. `sysSpawn`'s `spawnDone` already does `CLI` before `RTS`, so interrupts are properly restored on both success and failure paths. No `CLI` needed on the error branches.

### Root Cause

Spawn parameters (`taskNamePtr`, `spawnPtr`, `kArg+1`) are set by the caller **before** the syscall's `SEI`:

```asm
; pcRun in shell.inc
    lda knownNameLo,x; sta taskNamePtr   ; (1)
    lda knownNameHi,x; sta taskNamePtr+1 ; (2)
    lda knownAddrLo,x; sta spawnPtr      ; (3)
    lda knownAddrHi,x; sta spawnPtr+1    ; (4)
    lda knownDataSize,x; sta kArg+1      ; (5) — safe: kArg is per-task
    lda #1
    jsr syscall                ; (6) → dispatcher: sta kArg; php; pla; SEI
```

The dispatcher does `SEI` only at step (6). An IRQ between steps (1)–(6) can switch to another shell instance that also calls `pcRun`. The second instance overwrites `taskNamePtr` and `spawnPtr`. When the first instance resumes and enters the dispatcher, it spawns the **wrong task** with the **wrong data size**.

`kArg+1` (dataSize) is safe because the scheduler saves/restores `kArg` per-task. But `taskNamePtr` ($F3) and `spawnPtr` ($F7) are unprotected shared ZP.

### Impact

`run demo` could silently spawn `shell` (or vice versa), or spawn a re-entrant task with `dataSize=0` causing an immediate `(dataPtr),y` crash. The unprotected window is ~20 cycles wide — narrow but real at 60 Hz with ~16,640 cycles per tick.

### Fix

Either (a) wrap spawn parameter setup in `SEI`/`CLI`:
```asm
    sei
    lda knownNameLo,x; sta taskNamePtr
    ...
    lda knownDataSize,x; sta kArg+1
    lda #1
    jsr syscall
    cli
```
Or (b) pass spawn parameters through `kArg` bytes (which ARE saved/restored per-task) rather than ZP locations.

---

## Bug #27: Bump Allocator Leaks on Task Death 🟡 ✅ FIXED

**Severity:** High  
**Status:** Fixed 2026-07-25  
**Location:** `src/syscall.inc` — `sysSpawn`

**Fix applied:** `sysSpawn` now checks whether the dead slot already has a buffer of the same size before bump-allocating. If `taskDataSize,x == kArg+1` (same size, non-zero), it reuses the old buffer in place — just zero-initializes it via `spawnZeroBuf`. No new heap allocation, no leak. Only bump-allocates fresh when the slot never had a buffer or the sizes differ (future cross-type slot reuse, rare). This eliminates the 21-lifetime-spawn limit for the current single-reentrant-task-type system.

### Root Cause

The per-instance data heap at `$1C00-$1FFF` uses a **bump allocator with no `free` operation**. Each spawn of a re-entrant task allocates a new buffer from the heap. When the task dies (`exit` or `kill`), the buffer is **never reclaimed**. Re-spawning into the same slot allocates yet another new buffer, abandoning the old one.

Neither `sysExit` nor `sysKill` attempt to free the dying task's data buffer. The bump allocator has no mechanism to mark regions as freed.

### Impact

With 48-byte shell buffers and a 1024-byte heap:
- 8 simultaneous shells consume 384 bytes (640 free)
- Each kill + re-spawn cycle leaks another 48 bytes
- After ~21 cumulative shell spawns over the system lifetime, the heap exhausts
- `run shell` fails with `? heap full` even with only 1 shell running

This is fatal for a system advertising re-entrant tasks. The heap lifetime is bounded by `MAX_TASKS` × spawn/kill churn, not by simultaneous instances.

### Fix

Options (in increasing complexity):
1. **Slot-tagging**: Store the owning slot index in each buffer. When `sysSpawn` reuses a dead slot, check if the dead task's old buffer is still at the end of the heap — if so, rewind `dataHeap` instead of allocating new. If not at end, allocate new (waste acceptable given small heap).
2. **Free list**: When a task dies, mark its buffer as free. On spawn, scan the free list before bump-allocating. Coalesce adjacent free blocks.
3. **Compact on exhaustion**: When `dataHeap` overflows, walk all alive tasks' buffers, compact them to the start of the heap, and reset `dataHeap`.

---

## Bug #28: Screen State Inheritance on Slot Reuse 🟡 ✅ FIXED

**Severity:** Medium  
**Status:** Fixed 2026-07-25  
**Location:** `src/syscall.inc` — `sysSpawn`

**Fix applied:** `spawnSetAlive` now clears `screenAlloc`, `screenOwner`, `screenBaseLo`, and `screenBaseHi` for every newly spawned task before marking it ALIVE. New tasks start with a clean screen slate — no phantom screen inherited from a previously-dead occupant of the slot.

### Root Cause

When `sysSpawn` reuses a DEAD slot, it overwrites `taskState`, `taskLo/Hi`, `taskNameLo/Hi`, and `taskPriority` — but does **NOT** clear `screenAlloc`, `screenOwner`, `screenBaseLo`, or `screenBaseHi`. The new task inherits the dead task's screen allocation.

In `svRestore` (scheduler), the inherited values take effect:
```asm
    ldx curTask
    lda screenAlloc,x
    bne svHas              ; ← inherited from DEAD task!
    ; default $2000
    ...
svHas:
    lda screenBaseHi,x
    bne svHasInit          ; ← inherited from DEAD task!
```

If the new task doesn't explicitly call `kAllocScreen` or `kTakeScreenOwnership`, it silently writes to the dead task's screen buffer — which may have been reassigned to another task.

### Impact

Non-screen tasks (demo, clock sharing a screen) that spawn into a slot previously occupied by a screen-owning task inherit a phantom screen allocation. Shell instances that fail `kAllocScreen` (all 8 screens taken) fall through to `shellHasScreen` with a stale screen pointer — output goes to a random screen buffer.

### Fix

In `sysSpawn`, after `spawnSetAlive`, zero out the screen fields for the reused slot:
```asm
    lda #0
    sta screenAlloc,x
    sta screenOwner,x
    sta screenBaseLo,x
    sta screenBaseHi,x
```

Alternatively, `sysExit` and `sysKill` should call `kFreeScreen` for the dying task (see Bug #29).

---

## Bug #29: Screen Leaked on `kill` — Kernel Doesn't Free 🟡 ✅ FIXED

**Severity:** Medium  
**Status:** Fixed 2026-07-25  
**Location:** `src/syscall.inc` — `sysKill`, `sysExit`

**Fix applied:** Both `sysExit` and `sysKill` now zero `screenAlloc`, `screenOwner`, `screenBaseLo`, and `screenBaseHi` for the dying task before marking it DEAD. Combined with Bug #30 (skipping DEAD tasks in `kAllocScreen`), screens are immediately reclaimable on task death.

### Root Cause

Neither `sysKill` nor `sysExit` frees the dying task's screen buffer. The shell's `pcExit` handler calls `kFreeScreen` *before* `syscall #0` (correct), but the `kill` command path (`pcKill` → `syscall #2`) never gives the victim a chance to clean up.

For re-entrant shell instances, each killed instance permanently consumes one of the 8 virtual screen slots. Combined with Bug #28 (screen state inheritance), this creates phantom-occupied screens that block `kAllocScreen` from reusing those addresses even after the slot is recycled.

### Impact

After killing 7 shell instances, all 8 screens are "allocated" (7 dead + 1 alive). `kAllocScreen` returns carry-set for any new task. Screens can only be recovered by spawning new tasks into the dead slots that explicitly call `kAllocScreen` (which overwrites the stale fields).

### Fix

Both `sysKill` and `sysExit` should call `kFreeScreen` for the dying task. Since these already run with `SEI` active:
```asm
sysExit:
    ldx curTask
    jsr kFreeScreen             ; release screen before dying
    lda #0
    sta taskState,x
    jsr shrinkTotalTasks
    jmp reschedule
```

---

## Bug #30: `kAllocScreen` Treats Dead Tasks' Screens as Occupied 🟡 ✅ FIXED

**Severity:** Medium  
**Status:** Fixed 2026-07-25  
**Location:** `src/kernel.asm` — `allocScreen`

**Fix applied:** `asChkSlot` now checks `taskState,y` before `screenAlloc,y`. DEAD tasks (`taskState=0`) are skipped immediately — their stale `screenAlloc`/`screenBaseHi` values no longer block that screen address from reallocation. Combined with Bug #29 (clearing screen fields on kill/exit), dead-task screen slots are never phantom-occupied.

### Root Cause

The screen address scanner iterates `cpy totalTasks`, checking `screenBaseHi` of ALL slots within `totalTasks`, including DEAD ones. If a dead task's `screenAlloc` is still 1 and `screenOwner` is still `$FF` (because the screen was never freed — see Bug #29), the scanner treats that address as occupied and skips it:

```asm
asChkSlot:
    cpy totalTasks; bcs asFound
    lda screenAlloc,y; beq asChkNext    ; DEAD with screenAlloc=1 → NOT skipped!
    lda screenOwner,y; cmp #$ff; bne asChkNext
    lda screenBaseHi,y; cmp kArg+2; beq asOccupied  ; phantom match!
```

This makes the screen address **permanently lost** — it can never be allocated again because the dead task's stale fields make it appear occupied.

### Impact

Combined with Bug #29, killing screen-owning tasks progressively exhausts the 8-screen pool. After enough kills without `kFreeScreen`, `kAllocScreen` finds all 8 addresses "occupied" and returns carry-set, even though only 1 task is actually alive.

### Fix

In `asChkSlot`, skip DEAD tasks:
```asm
asChkSlot:
    cpy totalTasks; bcs asFound
    lda taskState,y
    beq asChkNext               ; DEAD → skip, screen is free
    lda screenAlloc,y; beq asChkNext
    lda screenOwner,y; cmp #$ff; bne asChkNext
    lda screenBaseHi,y; cmp kArg+2; beq asOccupied
```

Combined with Bug #29 (free on kill/exit), this ensures dead tasks never block screen allocation.

---

## Bug #31: Shell Falls Through Without Screen Lock 🟢 ✅ FIXED

**Severity:** Low  
**Status:** Fixed 2026-07-25  
**Location:** `src/tasks/dynamic/shell.inc` — `shell:` entry

**Fix applied:** When both `kTakeScreenOwnership` and `kAllocScreen` fail (all 8 virtual screens exhausted), the shell now prints `? no screens available` to the default $2000 display and enters a yield-loop halt (`shellHalt`). No more silent fallthrough to shared-screen writing without locking.

### Root Cause

If both `kTakeScreenOwnership` (slot 0) and `kAllocScreen` fail (all 8 screens exhausted), the shell falls through to `shellHasScreen` with `screenBase` at the default `$2000`. Multiple shell instances in this state would all write to `$2000` without `kLockScreen`/`kUnlockScreen`, causing cursor corruption and interleaved output.

### Impact

Requires 9+ shell instances to trigger (8 allocate screens, 9th can't). Degenerate but possible. Output corruption manifests as garbled text and cursor jumps.

### Fix

Fall through should either halt with an error message or wrap all screen output in `kLockScreen`/`kUnlockScreen` when running without a private screen.

---

## Bug #32: `enableCursor`/`disableCursor` Writes Garbage to Screen Buffer Tail 🔴 ✅ FIXED

**Severity:** Medium  
**Status:** Fixed 2026-07-25  
**Location:** `src/kernel.asm` — `ecDo` routine

**Fix applied:** Added `pha` before the address computation and `pla` before writing to the buffer tail, so the actual cursorOn value (0 or 1) is written instead of the computed high-address byte.

### Root Cause

The `ecDo` routine computes the buffer tail address using `A`, then writes `A` to the tail — but at that point `A` is `screenBaseHi + $03` (e.g., `$23` for screen at `$2000`), not the intended 0 or 1:

```asm
ecDo:
    sta cursorOn           ; cursorOn = 0 or 1 (correct)
    lda screenBase; ...    ; A = screenBaseLo + SCR_CURSOR_ON
    sta kPtr
    lda screenBase+1; ...  ; A = screenBaseHi + $03
    sta kPtr+1
    ldy #0; sta (kPtr),y   ; ⚠️ writes screenBaseHi+3, NOT 0/1!
```

### Impact

Functionally masked because: (a) the cursor blink code only checks for zero/non-zero (`beq kbRts`), and (b) the scheduler saves the correct `cursorOn` value to the buffer tail on every context switch. The metadata is corrupted between the enable/disable call and the next preemption.

---

## Bug #33: `kResult` ($0338-$033B) Not Saved/Restored by Scheduler 🔴 ✅ FIXED

**Severity:** Medium  
**Status:** Fixed 2026-07-25  
**Location:** `src/scheduler.inc` — `switchTask`; `src/kernel.asm` — per-task arrays

**Fix applied:** Extended the scheduler's per-task save/restore from 4 bytes (`kArg+0..3`) to 8 bytes (`kArg+0..7`, covering `kResult+0..3`). Added `taskKArg4` through `taskKArg7` arrays in `kernel.asm`.

### Root Cause

The scheduler saved only `kArg+0..3` ($0334-$0337). `kMul16` and `kDiv16` write results to `kResult` ($0338-$033B = `kArg+4..7`). If a task called `kMul16`/`kDiv16` and was preempted between the `RTS` and reading `kResult`, another task calling these routines would corrupt the results.

### Impact

Latent — no current task uses `kMul16`/`kDiv16`. But any future task that does will have race-condition garbage in its multiply/divide results.

---

## Bug #34: Semaphores Held by Dead Tasks Never Released 🟡 ✅ FIXED

**Severity:** Low  
**Status:** Fixed 2026-07-25  
**Location:** `src/syscall.inc` — `sysExit`, `sysKill`; `src/kernel.asm` — `semWait`, `semSignal`

**Fix applied:** Added `semOwner` array (one byte per semaphore, stores owning task index or `$FF`). `semWait` records `curTask` in `semOwner[x]` only on the 1→0 transition (when the semaphore becomes unavailable — the mutex case). `semSignal` clears `semOwner[x]` to `$FF` when count transitions 0→1 (mutex release). `sysExit` and `sysKill` now scan `semOwner` and release semaphores where `semCount == 0` and the owner matches the dying task — precise for mutexes. `sysKill` releases semaphores for both self-kill and non-self-kill paths.

**Limitation:** A single `semOwner[x]` byte cannot track multiple concurrent holders of a counting semaphore (N>1). For counting semaphores, ownership is best-effort: only the task that exhausted the last slot (1→0) is recorded. The `semCount == 0` guard prevents corrupting multi-slot state on cleanup. Tasks using counting semaphores should manage their own unit tracking.

Also upgraded semaphores from binary (0/1) to counting (0-255), enabling producer-consumer buffering patterns. `semSignal` now caps at 255 instead of blocking at 1.

### Root Cause

Semaphores were binary counters (0=held, 1=free) with no per-task ownership tracking. If a task acquired a semaphore and then died, the counter stayed at 0 forever.

---

## Bug #35: `maze.inc` Uses `.byte $2c` BIT Absolute Skip Trick ⚫ ✅ FIXED

**Severity:** Info  
**Status:** Fixed 2026-07-25  
**Location:** `src/tasks/maze.inc`

**Fix applied:** Replaced `.byte $2c` with `bne mzPrint` — an explicit relative branch (2 bytes) instead of embedding a hidden BIT-absolute instruction.

### Root Cause

Same pattern as Bug #14 and Bug #17 (both previously fixed in kernel code). The maze's 10 PRINT used `.byte $2c` to skip `lda #$ce` when the random bit was 0. This reads from operand bytes of the following instruction on every tick.

---

## Bug #36: `future-improvements.md` Stale — REU Requirement Not Reflected ⚫ ✅ FIXED

**Severity:** Info  
**Status:** ✅ Fixed (2026-09-15) — moot: the stale text lived in the file's completed-features section, which no longer exists.  
**Location:** `future-improvements.md`

The file described REU as "auto-detected" with a software fallback copy path (`saveStackSw`/`restoreStackSw`), while the code **requires** REU and halts without it. `future-improvements.md` now lists only unbuilt work, so there is nowhere left for an implemented feature to be described wrongly.
---

## Bug #59 — `kill` after `run basic` loses the shell; exiting basic lands on an empty screen 🟡 OPEN

**Status:** 🟡 Reported (2026-08-26), not yet root-caused

**Location:** eviction/restore + focus transfer (`src/kernel/dynload.inc` `dlEvict`/`dlRestore`, `src/kernel/syscall.inc` `transferFocusAway`), `kill.com`

**Repro (user-verified):**
1. Boot → shell = id 1.
2. `run basic 4` (does **not** evict the shell — pool has room).
3. Switch back to the shell (`ps` shows basic = id 2, `pool` correct).
4. `kill 2` → the view switches **to basic** (the user suspects kill.com's load evicts the shell).
5. `exit` inside basic → **empty screen**; the shell is never restored.

**Observed facts to reconcile:** basic is still ALIVE and interactive after `kill 2` (so the kill did not reap it), yet the display left the shell — and once basic exits, `transferFocusAway` finds no alive task with a surface and blanks the display (its `cmp #TA_ALIVE` skips swapped victims). The shell therefore ended up either dead or TA_SWAPPED without a later restore. With 104 pool pages and basic ≈57+fswrite 6+fp 11+RAM 4, shell ≈21, `kill.com` (3) should fit without eviction — so the victim selection / kill cascade path needs a live trace (breadcrumbs or monitor) to explain which path killed/evicted the shell. `dlRestore`'s `dlrNone` focus hand-back (X=$FF) only acts when `kbFocus == $FF`, which is satisfied by the blank, so a restore failure would have to be in the reload itself (e.g. file re-find) or the shell being DEAD rather than SWAPPED.

**Workaround:** don't `kill` basic while the pool is tight; `exit` basic instead (its `sysExit` frees pages and `dynFree`→`dlRestore` brings evicted tasks back).

---

## Bug #107 — A file longer than 192 lines loads **silently truncated** 🟡 OPEN

**Severity:** Medium — it needs a document over `MAX_LINES`, and the loss only
becomes permanent if the user then saves.

**Symptom:** `edit <file>` on a file with more than 192 lines (`MAX_LINES`,
`edit.inc`). The editor opens and says nothing; the document simply stops at
line 192 and the remainder of the file is gone. `STOP+S` then writes the
shortened document back over the original.

**Root cause — two bare `rts` exits with no status.** The load loop ends the
document early when the line count is full, in both the hard-wrap path and the
RETURN path:

```asm
    ldy #ED_LINES                // 192 lines max
    lda (dataPtr),y
    cmp #MAX_LINES
    bcc elWrapOk
    rts                          // doc full -- stop (discard the rest)
elWrapOk:
```

The two exits (`elWrapOk` at the wrap test, `elNlOk` at the newline test) both
return with `ED_LINES == MAX_LINES` and **no indication to the caller** —
nothing distinguishes "the file ended" from "the document is full". The status
message table has no `truncated` entry; the only related string is
`edNoSpaceMsg` ("no space"), used by `edNoRoom` for the out-of-chunks path
(Bug #106).

**Fix (proposed, not applied).** Make the exits signal — set a flag, or return
with the carry set — and have the **load path** report it on the editor's own
status line, either with a new `truncated` string or by reusing `edNoRoom`'s
"no space" with `kWaitKey`, the way Bug #106's chunk failure does. The same two
exits are also reachable from a mid-session grow, where "document full" is the
correct wording, so the flag has to be interpreted by whoever called the loop
rather than printed at the exit.

---

## Bug #112 — `dlrNameSetup` overflows `dynNameBuf` for any dynamic-task name of 8+ characters ✅ FIXED (2026-09-18)

**Fix applied (2026-09-18).** `dlrNameCp`'s cap became `cpy #11` -> `cpy #7`: the `".tsk"`
suffix plus its null need 5 of the destination's 12 bytes, so 7 copied characters fill it
exactly. Byte-neutral (same instruction, same size), so the memory map needed no re-pin. The
residual limit is deliberate and visible: an 8-11 character name is now **truncated here**, so
its restore lookup fails as an ordinary not-found rather than running `".tsk\0"` past the
buffer into `reentFirst`/`reentResIdx`/`dlRegSlot`. Sizing the destination for a full 11-char
name needs a 16-byte buffer, and `dynNameBuf` sits mid-band, so that is a relocation job.

**Severity:** Critical — corrupts `dlRegSlot`, an index used throughout the rest of the
restore path to select which dyn-table slot is being operated on.

**Location:** `src/kernel/dynload.inc`, `dlrNameSetup` (the copy loop at `dlrNameCp`/
`dlrNameExt`), reached from `dlRestore` via `dlrAttNone` when swapping a dead/evicted
task's code back into the pool.

**Root cause.** `dlrNameSetup` copies a task's name into the shared 12-byte buffer
(`dynNameBuf: .fill 12, 0  // max 11 chars + null`) and then unconditionally appends the
literal `".tsk\0"` (5 bytes) starting at whatever index `Y` the copy loop stopped at:

```asm
dlrNameCp:
    lda (srcPtr),y
    sta dynNameBuf,y
    beq dlrNameExt
    iny
    cpy #11
    bcc dlrNameCp
dlrNameExt:
    lda #$2E; sta dynNameBuf,y   // '.'
    iny
    lda #$14; sta dynNameBuf,y   // 't'
    iny
    lda #$13; sta dynNameBuf,y   // 's'
    iny
    lda #$0B; sta dynNameBuf,y   // 'k'
    iny
    lda #0; sta dynNameBuf,y     // null
```

The copy loop exits with `Y` equal to either the source name's length `L` (terminator
found, `0 <= L <= 10`) or `11` (the 11-char cap reached with no terminator — the header
format and `dlCopyName` both allow names up to 11 characters). `dlrNameExt` then writes
5 more bytes at `dynNameBuf[Y .. Y+4]`, but the buffer's valid indices are only `0..11`.
The write stays in bounds only when `Y+4 <= 11`, i.e. `L <= 7`. Any legal name of 8-11
characters overruns the buffer by 1-4 bytes into the immediately following kernel-data
bytes (declared right after it, in this order):

```asm
dynNameBuf:   .fill 12, 0
reentFirst:   .byte 0
reentResIdx:  .byte 0
dlRegSlot:    .byte 0
dlFixCountLo: .byte 0
```

An 11-char name overwrites `reentFirst` (with `$14`), `reentResIdx` (with `$13`), and
`dlRegSlot` (with `$0B` = 11) — out of range, since `MAX_DYN_TASKS = 8` (valid slots
0-7). `dlRegSlot` is then re-read repeatedly for the rest of the restore
(`dlrFound`/`dlrHdrOk`/`dlrLoaded`/`dlrRestored`), so every subsequent
`dynTaskXxx,y`/`taskState,x` access in that restore indexes the wrong slot — a real
memory-corruption path, not a cosmetic one. Shorter overruns (8-10 chars) corrupt a
subset of `reentFirst`/`reentResIdx`/`dlRegSlot` the same way.

**Why it hasn't shown up yet:** every shipped `.tsk` name is currently `<= 7` characters
("sprdemo", "gfxdemo", "gortris", "threads", "sidplay", "compact", "ipctest",
"setfont", ...) — exactly at the safe boundary — so this looks like an unenforced
convention rather than a documented limit. Any dynamic task named 8-11 characters (the
header format explicitly allows it) hits this the first time it gets evicted and
restored.

**Fix (proposed, not applied).** Mirror the safe pattern already used for the identical
copy elsewhere in this file — `dlNcpy` and `dlCopyName` both cap the loop at 11 *and*
force a null at index 11 when no terminator was found early. `dlrNameCp` needs the
equivalent: cap the copy so at most 7 characters are copied before `".tsk\0"` is
appended (or write the suffix into a separate, larger scratch buffer).

---

## Bug #113 — `spawn`'s ZP-window guard ignores the addition's carry, letting a malformed header alias the kernel's reserved zero page ✅ FIXED (2026-09-18)

**Fix applied (2026-09-18).** `bcs scZpBad` immediately after the `adc kArg+6`, with the
`scZpBad` label on the existing clamp block (+2B). A carry out of that `adc` proves the true
sum exceeded `$FF`, which is beyond `$F0` regardless of where the wrapped value landed, so it
is rejected without reaching the `cmp`. This closes both shapes: a base inside `$F1-$FF`
(`$F5+$0C`) and a base below it whose length overruns the zero page (`$80+$F0`).

The 2B came from a relocation: `dlEvMaybeSelf` moved out of the kernel core's growth
headroom to `$CA8C-$CA98` (13B at the top of the free run after `sprite.inc`, leaving
`$CA88-$CA8B` for the block below), so the core band could grow into its own headroom again —
`kernelCoreEnd` is now `$13C2` and the headroom `$13C2-$13D3` (18B). Free total 165 ->
**163B across 21 pockets**, `verify-map.ps1` re-pinned.

**Severity:** High — silent kernel zero-page corruption from a single bad task header
field, reachable via ordinary `spawn`/`run` of a dynamic task, with no error reported.

**Location:** `src/kernel/syscall.inc`, `spawnCore` (the ZP-window validation block at
`scZpOk`), lines 335-350.

```asm
    // --- set ZP save region (from kArg+6/kArg+7) ---
    lda kArg+6                  // zpBase
    sta taskZpBase,x
    lda kArg+7                  // zpLen
    tay                         // save original len in Y
    beq scZpOk                  // 0 = no ZP save, always valid
    clc
    adc kArg+6                  // A = base + len
    cmp #$f1                    // must not overlap kernel ZP ($F1+)
    bcc scZpOk                  // base+len <= $F0, valid -- Y has original len
    lda #0                      // invalid -- clamp both to 0
    sta taskZpBase,x
    tay                         // Y = 0
scZpOk:
```

**Root cause.** The intended invariant (`kernel.inc`'s `MAX_ZP_LEN = 240`) is "reject
unless `zpBase + zpLen <= $F0`", so the declared save window can never reach the
kernel-reserved zero page `$F1-$FF`. The guard computes `A = zpBase + zpLen` in an
8-bit accumulator and tests only the **truncated** result against `#$f1` — it never
checks whether the `adc` itself carried (i.e. whether the true sum was `>= 256`). Any
`zpBase`/`zpLen` pair whose true sum overflows 256 and whose wrapped (mod-256) sum
happens to land below `$f1` is wrongly accepted.

Concrete failing case: `zpBase = $F5` (245), `zpLen = $0C` (12). True sum = 257, which
wraps to `A = 1` with carry set; the carry is discarded, `cmp #$f1` sees `A=1 < $f1`,
and `bcc scZpOk` accepts the window — even though `zpBase = $F5` sits squarely inside
`$F1-$FF`, the exact range this check exists to protect.

**Reachability.** `zpBase`/`zpLen` come directly from the task binary's own header
fields (`HDR_ZP_BASE`/`HDR_ZP_LEN`, `task-header.inc`), parsed verbatim by the loader
for both `spawn` and `sysThreadSpawn`. Any task binary — malformed, hand-crafted, or
just buggy — that declares such a pair bypasses the guard entirely.

**Consequence.** Once accepted, `taskZpBase`/`taskZpLen` are stored unclamped and used
by the scheduler's per-task ZP save/restore DMA on every context switch involving that
task, copying `taskZpBase..+taskZpLen` to/from a REU shadow. With the bad window, that
DMA reads and overwrites the kernel's own reserved zero page (`kPtr`, `srcPtr`,
`spawnPtr`, `tmpPtr`, `curTask`, `saveSP`, `kbTemp`, all in `$F1-$FF`) on every
preemption involving the task — corrupting kernel state used by the IRQ path, the
scheduler itself, and every other task, from a single malformed header field.

**Fix (proposed, not applied).** Check the carry out of the `adc` before the `cmp`,
e.g. `bcs zpInvalid` immediately after `adc kArg+6`, and only run the `cmp
#$f1`/`bcc` test when no carry occurred (a carry alone already proves the true sum
exceeds `$F0`, so it should be treated as invalid without even reaching the `cmp`).

---

## Bug #114 — a failed lazy-library load let the command carry on 🔴 ✅ FIXED (2026-09-18)

**Symptom class:** `? could not load <name>.lib` was printed, and then the command went
ahead anyway — running its own logic with the library absent, printing stale buffer
contents, or repeating the message once per subsequent call.

**Root cause.** A `.com` never loads a library explicitly: the first call into a lazy
library's jump-table entry runs its cold stub, and on failure the stub reaches
`libLoadFail` (`src/kernel/libstubs.inc`), which printed the message and returned **C=1**.
C is also an ordinary library *result* — `kReuDirEntry` C=1 means "end of listing",
`kReuFormat` C=1 means failure, `kStrcmp` reports in Z — so a caller cannot tell a load
failure from a normal answer. Audit of all 20 `.com` tasks:

| task | libs | on a failed load |
|---|---|---|
| `banner` | filesys | `bcs bnNext` reads C=1 as "empty slot" and keeps scanning, re-printing per retry |
| `del` | filesys + string | `kReuDirEntry` C misread as "empty"; `kReuDelete` C left untested |
| `dir` | filesys + string | sector-read C misread as "skip", and its `kArg+3..+5` outputs are then consumed |
| `format` | filesys | C ignored entirely — prints `formatted` regardless |
| `ps` | string | `kByteToHex` result ignored — the address column prints whatever the buffer held |
| `rename` | filesys | C tested, but the error decode reads `kArg+6`, which a load failure never sets |
| `copy`, `fstest` | filesys | correctly tested — not affected |
| `ipctest` | ipc | C is the protocol's own result, reinterpreted deliberately — not affected |
| the other 11 | none | no libraries at all (`blink`, `compact`, `keyrpt`, `kill`, `pause`, `prio`, `resume`, `setfont`, `sidrst` use resident `kReu*` calls; rest use nothing) |

**Fix.** `libLoadFail` does not return any more: it prints the message and `jmp sysExit`.
A command whose library is missing cannot do its job, so every caller now refuses without
being changed, and the message appears exactly once instead of once per call. Net cost is
zero: `sec`/`rts` became the `jmp`, paid for by dropping the `jsr kNewline` — the shell
prints its own prompt line once the command exits, so the visible output is unchanged. Free
total 163 -> **165B across 22 pockets**, `verify-map.ps1` re-pinned.

**Related, not fixed here:** a library *function* that fails after a successful load is
still the caller's business. `format` ignoring `kReuFormat`'s C, `del` ignoring
`kReuDelete`'s, and `dir` consuming `kReuDirPageDma`'s outputs without testing C all
produce wrong output (not a missing library), so they belong to their own entries.

---

## Bug #115 — a failed lazy-library load killed the shell 🔴 ✅ FIXED (2026-09-18)

**Symptom class:** Bug #114's fix ends the task that could not load its library. That is
right for a command task and fatal for the system shell: the shell owns the display and
is the only interpreter, so it would print `? could not load filesys.lib`, exit, hand the
display away (`transferFocusAway` blanks it and sets `kbFocus = $ff`) and leave the
machine dead until reset. `fsinfo`, `save`, `load`, the hex-address parser and the whole
`run <name>.bat` batch runner can reach that stub; **`pool` cannot** -- `pcPool` uses no
library at all (its columns are decimal, through resident `kByteToDec`).

**Root cause.** The shell declares `LIB_FILESYS | LIB_STRING` and both are **lazy**, so its
libraries are not guaranteed to be resident:

1. The shell is **evictable** (`shell.asm`: `KF_RELOCATABLE|KF_REENTRANT|KF_TAKES_ARGS`),
   and `run basic` (99 pages) has to sacrifice it.
2. Eviction **releases its libraries** -- `dlEvict` -> `dlEvLibs` -> `dlLibRelease`;
   `dynFree` skips that release for a `DYN_FLAG_SWAPPED` task, which is the proof the
   eviction already did it.
3. `dlRestore` -> `dlReLibs` re-increments the mask refcounts but `dlLibEagerLoad` loads
   only the **eager** libs (gfx, fp), so the restored shell is referenced but *unloaded*
   (`libStartPage = $FF`).
4. Its next `fsinfo`/`save`/`load`/`run *.bat` takes the cold stub. `libLoad` sets
   `dlNoSelfEvict`, so the shell can never be its own victim, and `filesys.lib` needs a
   **contiguous** 10 pages (`string.lib` 2) -- with `edit` (36, pinned), `godzi` and
   `gortris` (pinned) holding the pool, or the free pages split into smaller runs, that
   load answers C=1.

So the failure lands mid-command, long after startup, in the one task that must not die.

**Fix.** `libFailExit` (`src/kernel/kernel.asm`, 12B in the `$CD5C` pocket) decides:
`taskParent[curTask] == 0` means the boot target, so that task returns C=1 (the shell's
command refuses on it and the prompt survives); everything else leaves through `sysExit`
as Bug #114 intended. The identity needs **no stored state**: `dlRegister` writes the link
once from `curTask`, `initTCB_ptr` does not touch it (so it survives eviction and restore,
unlike a slot id which would be recycled), `scOrphan` only clears links pointing at a slot
being re-spawned, and `findFreeSlot` starts at slot 1 -- slot 0 is boot-only, so nothing
else can have a parent of 0. `libLoadFail`'s `jmp sysExit` became `jmp libFailExit`: net
zero bytes in the `$1A17` block, which ends flush at `$1C6E`.

`pcFsinfo` also tests `kReuDirEntry`'s carry now and reports no size when slot 0 is
unreadable, instead of a figure built from a failed call's leftovers. The shell stays
inside 19 pages (**4862 of 4864**, 107 fixups). Free total 165 -> **153B across 22
pockets** (the 12B routine took the bottom of the `$CD5C` run; its 5B tail stays free).

**Residual.** The shell's other callers still print their own message on top of the real
one -- `save` -> `? save failed`, `load` -> `? not found`, the batch scanner -> `? not
found`. They refuse, so nothing is corrupted or executed; only the wording is a follow-up.
Any *other* task whose lazy library fails mid-run still ends, which is the intent of Bug
#114: a resident task's loaded libraries are refcount-pinned by its own mask, so the window
is a task that was evicted and restored. `edit`, `godzi` and `gortris` are
`KF_NOT_EVICTABLE`; `basic` is not, and declares lazy `filesys.lib`, so it is the remaining
place that behaviour can be reached.

---

## Bug #116 — the shell kept running a command whose library had failed � ✅ FIXED (2026-09-18)

**Symptom class:** with `string.lib` unloadable, `pool` printed
`? could not load string.lib` **four times** and then finished its table anyway, with a header
built from whatever the last decimal print had left in `decBuf`. Repro: boot, `run gortris`,
`run sprdemo`, `run edit boot.bat`, then `pool`.

**Root cause.** `pcPool` prints its `$base-$last` header through `dyHex`, and **`dyHex` calls
`kByteToHex` -- a lazy `string.lib` call** (`shell.inc:645-650`, `:1839`). Every one of those
four calls entered the cold stub, failed the load, printed the message, and returned C=1 to
a caller that never tests it. Bug #115's soft return kept the shell alive but left the command
running -- and printing-the-message-once-per-call is structural, because every failed stub
call reaches `libLoadFail` on its own.

The library could not load because the pool was starved: `edit` (40 pages + its `kMalloc`
chunk) and `gortris` (30 + `sid.lib` 16) are both `KF_NOT_EVICTABLE`, and `run edit boot.bat`
is heavy enough to sacrifice the shell -- which then comes back **without** its libraries,
since `dlReLibs` reloads only eager ones and neither `filesys` nor `string` is eager. Nothing
loads `string.lib` earlier either: `src/batch/boot.bat` (`print / print / print / sidrst /
banner logo320 0 0 / ver / me`) never prints or parses hex. `libLoad` sets `dlNoSelfEvict`,
so the shell can never be its own victim, and a 10-page `filesys.lib` or 2-page `string.lib`
request then has nowhere to go.

**Fix.** `libFailExit` (`src/kernel/libstubs.inc`) no longer hands the C=1 back to the command.
A command task still ends (`sysExit`), exactly as Bug #114 intended. The **system shell** is
re-entered **at its own entry with a fresh stack** -- `taskLo/Hi[curTask]`, `ldx #$F7 / txs`,
`jmp (kPtr)` -- which is the path a restored instance already takes (`screenAlloc` is still
set, so the entry lands on the prompt). So the command is abandoned, the message appears once,
and the prompt returns. Nothing in the shell tests anything, and no new state is needed: the
shell is still identified by its parent link (`taskParent == 0`), and `dataPtr` is re-read from
the task's instance-buffer record because a library may have used it as scratch (`gfx.lib`
does). `$F7` is the cold-start SP, so the abandoned frames are dropped rather than leaked one
frame per abort.

**Funded, not borrowed:** the 19 cold stubs each carried their own `bcs fail / fail: jmp
libLoadFail` pair (5B). They now branch to a shared trampoline per group (2B + 2-3B,
all within 80B), which freed 39B inside the `$1A17` stub block -- enough for the new routine,
and the block still ends at `$1C65` (asserted at `$1C6E`). Free total **153 -> 171B across 22
pockets**; the `$CD5C` pocket the first version of `libFailExit` occupied is free again.

**Verified:** kernel 212 asserts / 0 failed; `verify-map.ps1` green at 171B / 22 pockets; no
task binary changed (the shell is untouched), so no REU rebuild is needed.

---

## Bug #117 — `run basic` reported `? no space in pool` after the single-header migration: `dlPeekLibPages` still read the old header offsets 🔴 ✅ FIXED (2026-09-19)

**Symptom class:** on a fresh boot, `run basic` printed `? no space in pool` (error 4) and
`basic` never started, while every other task and command loaded normally. It was a regression
from the collapse of the two binary formats — the migration that deleted the v2 fixup table,
so the header lost its version byte and every field from `flags` on moved **down by one byte**.

**Root cause.** `dlPeekLibPages` (`src/kernel/poolevict.inc`) DMAs a library's 256-byte header
page into `reuDirBuf` and reads `code_size` out of it. Under the old layout `code_size` was at
`+6/+7`; the new layout has it at `+5/+6`, because `+6` became `code_size` hi and `+7` is
`data_size`. The migration renumbered the routine's **comment** to `+5/+6` and left the **code**
at `+6/+7`, so it computed `(code_hi << 8) | data_size`. `git diff` shows the file's entire
change was those two comment lines — which is why the miss survived: it read as a comment-only
edit.

Every library has `data_size = 0`, so the computed value is the code size truncated to a
multiple of 256, and `ceil(v/256)` of *that* is always **one page short**:

| lib | code | open pages | reported |
|---|---|---|---|
| `time.lib` | 213 | 1 | 0 |
| `filesys.lib` | 2438 | 10 | 9 |
| `gfx.lib` | 6514 | 26 | 25 |
| `fp.lib` | 2039 | 8 | 7 |
| `string.lib` | 335 | 2 | 1 |
| `sid.lib` | 3152 | 13 | 12 |
| `ipc.lib` | 638 | 3 | 2 |

Its only caller is `dlEagerLibPages`, i.e. `dlPreEvict`'s pre-eviction budget, and only for a
**non-resident** library (a resident one is counted from `libPages,x`, which is exact). So for
`basic` — which declares filesys|gfx|fp, and whose eager set is gfx+fp because `libEager` is
`[0,0,1,1,0,0,0]`:

    dlPreNeed = ceil(16638/256) + 0 + (25 + 7) = 97        true: 65 + (26 + 8) = 99

Free at boot is 78 pages of 109 (shell 19 + filesys.lib 10 + string.lib 2). Evicting the shell
alone frees 19 more — **78 + 19 = 97, exactly the under-estimated `dlPreNeed`** — so `dlPreEvict`
stopped there. 97 is enough for `basic`'s 65-page code but leaves only 32 for the eager
libraries, whose true need is 26 + 8 = 34: `gfx.lib` takes 26, six pages remain, and `fp.lib`'s
8-page allocation fails. The error surfaces as error 4 *after* the shell was already evicted —
precisely the evict-then-fragment shape `poolevict.inc` exists to prevent. It only bit when the
whole pool was otherwise empty, which is why a fresh boot was the repro and why nothing else in
the tree was affected.

**Fix.** `dlPeekLibPages` now reads the field **symbolically** — `reuDirBuf+HDR_CODE_SIZE` and
`HDR_CODE_SIZE+1` — instead of by bare literal. The operands stay one byte, so the fix is
size-neutral: no kernel address moved, the PRG is the same length (63481 bytes), and no task or
library binary changed, so no REU rebuild is needed.

It also removes the side effect first noticed in `pool`, where the shell showed 20 pages
instead of 19: the shell's `dynTaskPageCount` is stored from `kArg+7`, which `dlPeekLibPages`
also uses as its DMA length hi, so the count on display was an artifact of the half-completed
evict/restore on the failing path — a symptom, not a cause.

**Guard added.** `tools/check-header-offsets.ps1`, run by `tools/build-kernel.ps1` before Ninja,
fails the build when a header field is read through a bare `+N` literal on `scratchBuf` or
`reuDirBuf` instead of an `HDR_*` constant (the REU FS directory's own `+1` and `+17..+21` are
allow-listed). This is a class of bug no `.assert` can catch: the offset is a byte index into a
DMA'd page, not a label, so the value is invisible to the assembler.

**Verified:** kernel 212 asserts / 0 failed; `verify-map.ps1` green at 330B / 22 pockets;
`run basic` loads on a fresh boot; loader headroom `$529E-$533F` and the core headroom
`$13C2-$13D3` both unchanged.

# HISTORICAL — FIXED BUGS

---

## Bug #1: `kPtr` Corruption in IRQ Path 🔴 ✅ FIXED

**Severity:** Medium-High  
**Status:** Fixed 2025-07-25  
**Location:** `src/kernel.asm` — `kbPoll`, `kbDone`, `switchView`; `src/irq.inc` — `irqHandler`

**Fix applied:** `kbPoll` now saves `kPtr` on entry (push lo, push hi) and restores it at `kbRts` before `rts` (pop hi, pop lo). This ensures the task's `kPtr` survives all internal uses by cursor blink and `switchView`.

### Root Cause

`kbPoll` (called from `irqHandler`) and its callees overwrite `kPtr` ($F5–$F6) with screen-buffer-tail addresses **without saving the task's original value**. The scheduler saves `kPtr` to per-task arrays in `switchTask` — but `switchTask` runs **after** `kbPoll`, so it saves the already-corrupted value. When the task resumes, the scheduler restores the corrupted `kPtr`.

Two code paths corrupt `kPtr`:

**Path A — Cursor blink (every ~500ms when cursor is visible):**

In `kbDone`, the cursor blink code reads from the displayed screen's buffer tail (at `SCR_CURSOR_ON`), then computes `cursorAddr` — both using `kPtr`. The "save/restore" around the cursor redraw saves and restores the blink routine's **own** `kPtr` value, not the task's original.

**Path B — F-key press (F1/F3/F5/F7 or Shift+F-key):**

`kbPoll` → `kbSwitchToScreen` → `switchView` → `svPrivate` writes to `kPtr` to read `$D018` from the new screen's buffer tail. After `switchView` returns, `kPtr` points to offset `$3EA` in the screen buffer. If the cursor blink doesn't fire on this tick, `kbRts` returns immediately with `kPtr` still corrupted.

### Impact

Any task preempted during one of these events gets its `kPtr` set to a screen buffer tail address (e.g., `$23EB` or `$23EA`). Subsequent use of `kPtr` by that task causes garbled output, wrong string operations, and potential writes to screen metadata.

### Fix

```asm
kbPoll:
    lda kPtr; pha
    lda kPtr+1; pha
    jsr scanKeyboardHw
    ; ... existing keyboard processing + cursor blink ...
    pla; sta kPtr+1
    pla; sta kPtr
    rts
```

---

## Bug #2: `kArg` ($0334) Shared-State Race 🟡 ✅ FIXED

**Severity:** Medium  
**Status:** Fixed 2025-07-25  
**Location:** `src/kernel.asm` — `printChar`, `printString`, `printTime`; `kLockScreen`/`kUnlockScreen`

---

## Bug #37: `clearScreen` Color RAM Fill Erases Output 🔴 ✅ FIXED

**Severity:** High  
**Status:** Fixed 2026-07-25  
**Location:** `src/kernel.asm` — `clearScreen`

**Fix applied:** Removed the bulk color RAM fill from `clearScreen`. `printChar` already writes the per-screen text color to color RAM for each printed character, and spaces (screen code `$20`) have no visible pixels regardless of color RAM value. The fill was unnecessary and caused text to disappear mysteriously — exact root cause not determined through code analysis but confirmed empirically.

### Root Cause

When a bulk 1000-byte fill of color RAM (`$D800-$DBE7`) was added to `clearScreen`, previously printed text (e.g., the boot banner) would vanish. The fill code itself was correct (verified address bounds and loop termination), and `clearScreen` is called BEFORE text is printed (in `kAllocScreen`), so the fill shouldn't affect subsequent output. The exact mechanism remains unknown, but the empirical test was conclusive: removing the fill fixes the issue.

### Impact

Boot screen text ("gordonOS v0.7.0") and spawn banner ("[shell] started") were invisible after adding the color RAM fill. Reverting the fill restored visibility.

### Fix

Removed the color RAM fill from `clearScreen`. The per-character approach (`printChar` writing to color RAM) is sufficient and doesn't exhibit the bug.

**Fix applied:** Added per-task `taskKArg0..taskKArg3` arrays in `kernel.asm`, and save/restore of `kArg+0..kArg+3` in `switchTask` alongside the existing `kPtr`/`srcPtr` per-task saves. This makes `kArg` behave like `kPtr` and `srcPtr` — each task has its own copy that survives context switches.

### Root Cause

`kArg` at `$0334` is a global shared memory location. Multiple kernel routines use it as a temporary, but it was **not** saved/restored per-task by the scheduler. If Task A is preempted while holding data in `kArg`, and Task B calls any routine that modifies `kArg`, Task A's data is silently corrupted.

**Affected routines:** `printChar` (~3 instruction race window), `printString` (~5 per char), `printTime` (entire function), `kMul16`/`kDiv16` (entire function), `kSetTime` (at call boundary), `syscall` dispatcher (between store and dispatch), `kLockScreen` (between semWait return and save).

**Example scenario:** Task A stores a character in `kArg`, gets preempted, Task B's `printChar` overwrites `kArg`, Task A resumes and prints Task B's character.

### Fix

Added per-task save/restore of all 4 `kArg` bytes in `switchTask`, mirroring the existing `kPtr`/`srcPtr` pattern.

---

## Bug #3: `kReadString` State Corruption Before Focus Check 🟡 ✅ FIXED

**Severity:** Low-Medium  
**Status:** Fixed 2025-07-25  
**Location:** `src/kernel.asm` — `readString`

**Fix applied:** Moved the keyboard focus check before `stx readStrMax`, using `Y` for the check so `X` (maxLen) is preserved.

### Root Cause

The first instruction of `kReadString` wrote to the global `readStrMax` variable **before** checking whether the current task owns keyboard focus. If Task A (with focus) was blocked in `rsLoop` and Task B (without focus) called `kReadString`, Task B would overwrite Task A's `readStrMax` with its own max-length value, then return `Y=0`. Task A would resume with the wrong `readStrMax`.

### Impact

Task A's input silently truncated to Task B's max-length value. The user sees input stop accepting characters with no explanation.

### Fix

```asm
readString:
    ldy curTask
    cpy kbFocus
    beq rsGo
    ldy #0; rts
rsGo:
    stx readStrMax              ; now safe — we own the keyboard
    ; ...
```

---

## Bug #4: `kPrintTime` Overlaps with `kResult` Memory 🟢 ✅ FIXED

**Severity:** Low  
**Status:** Fixed 2025-07-25  
**Location:** `src/kernel.asm` — `printTime`

**Fix applied:** Added a dedicated `timeBuf` (16 bytes in kernel data) instead of building the time string at `kArg+4` which overlaps with `kResult`.

### Root Cause

`kPrintTime` built its output string at `kArg+4` ($0338) through `kArg+15` ($0343). But `kResult` is defined at `$0338`–`$033B` — a direct overlap. Time string bytes overwrote `kResult`, and math results could corrupt the time string.

### Fix

Use a dedicated `timeBuf` in kernel data, eliminating the overlap entirely.

---

## Bug #6: Duplicated Command-Parsing Code in Shell 🟢 ✅ FIXED

**Severity:** Maintainability  
**Status:** Fixed 2025-07-25  
**Location:** `src/tasks/dynamic/shell.inc` — `pcKill`, `pcPause`, `pcResume`, `pcPrio`, `pcView`

**Fix applied:** Extracted shared `findTaskArg` subroutine that handles both decimal ID and name-based task lookup. All five command handlers now call this single routine.

### Root Cause

Five command handlers re-implemented the same two patterns: decimal number parsing (multiply-by-10 accumulator) and name-based task search. ~200 lines across 5 near-identical copies.

### Fix

Extracted `findTaskArg` — parses a task ID (decimal) or name from `lineBuf+Y`, returns slot index in `pcSlot`, carry set on failure.

---

## Bug #7: `sysGetTid` — `curTask` Read and `CLI` Order ℹ️

**Severity:** Info (not a bug)  
**Location:** `src/syscall.inc` — `sysGetTid`

### Observation

```asm
sysGetTid:
    lda curTask
    cli
    rts
```

The dispatcher does `SEI` before jumping to `sysGetTid`, so `curTask` is read atomically. The `CLI` re-enables interrupts before `RTS`. Between `CLI` and the caller consuming the return value, `curTask` could theoretically change — but this is harmless since the returned value represents identity at call time.

**Conclusion:** Not a bug. The pattern is correct and idiomatic.

---

## Bug #8: `clearScreen` Last-Page Loop is Correct but Non-Obvious ℹ️

**Severity:** Info (clarity)  
**Location:** `src/kernel.asm` — `clearScreen`

### Observation

```asm
    ldy #$e8
csLast:
    dey
    sta (kPtr),y
    bne csLast
```

The `dey`-first-then-store pattern writes offsets `$E7` down to `$00` inclusive (232 bytes). Combined with 3 full pages (768 bytes) = 1000 bytes = 40×25. The pattern is correct but easy to mistake for an off-by-one bug. A clarifying comment was added.

---

## Bug #10: `div16` — Quotient High Byte Never Incremented 🔴 ✅ FIXED

**Severity:** Medium  
**Status:** Fixed 2026-07-25  
**Location:** `src/kernel.asm` — `div16`

**Fix applied:** Added `bne d16Skip; inc kResult+1` after `inc kResult` so the high byte of the quotient is incremented when the low byte wraps from `$FF` to `$00`.

---

## Bug #11: `time set` — AM/PM Flag Inverted 🔴 ✅ FIXED

**Severity:** Medium  
**Status:** Fixed 2026-07-25  
**Location:** `src/tasks/dynamic/shell.inc` — `pcTimeConv`

**Fix applied:** Swapped the AM/PM flag sense. AM now leaves `kArg+6 = $00` (bit 7 clear = AM on CIA TOD), PM sets `kArg+6 = $80` (bit 7 set = PM).

---

## Bug #12: `semSignal` — No Overflow Guard 🔴 ✅ FIXED

**Severity:** Low  
**Status:** Fixed 2026-07-25  
**Location:** `src/kernel.asm` — `semSignal`

**Fix applied:** Added `lda semCount,x; cmp #1; bcs ssDone` before `inc semCount,x` to prevent counter wrap.

---

## Bug #13: `findNextAlive` — Infinite Loop When All Tasks PAUSED 🟡 🚫 WON'T FIX

**Severity:** Low-Medium  
**Status:** Won't Fix (by design)  
**Location:** `src/scheduler.inc` — `findNextAlive`

Pausing all tasks leaves the scheduler with no ALIVE tasks to run but a non-zero `totalTasks`, causing an infinite scan loop. Marked won't fix — pausing all tasks is user error.

---

## Bug #14: `kPrintTime` BIT Trick Removed ℹ️ ✅ FIXED

**Severity:** Info  
**Status:** Fixed 2026-07-25  
**Location:** `src/kernel.asm` — `printTime`

**Fix applied:** Replaced `.byte $2c` BIT trick with pure 6502 `bne ptStore` branch.

---

## Bug #15: `findTaskArg` — Overflow Check on Decimal Parse 🟡 ✅ FIXED

**Severity:** Low  
**Status:** Fixed 2026-07-25  
**Location:** `src/tasks/dynamic/shell.inc` — `findTaskArg`

**Fix applied:** Added `cmp #26; bcs ftaBad` to reject decimal values that would overflow past 255 during `×10 + digit` accumulation.

---

## Bug #52: Task holding the screen mutex can never be `kill`ed 🔴

**Status:** ✅ Fixed (2026-08-25) — `fptest` now unlocks before idling; issue documented as a design limitation.

**Location:** `src/kernel/semaphore.inc` (`semWait`), `src/tasks/cmd/kill.inc`, `src/tasks/dynamic/fptest.asm`

**Symptoms:** `run fptest` then `kill <id>` → system hangs: the kill task blocks in `kLockScreen` forever and the shell waits for it (`pcFgWait`). Internal commands (`pool`, `help`, …) keep working because they don't take the mutex.

**Root cause:** fptest acquired the screen mutex (semaphore 0) via `kLockScreen` and never released it. Every external command task must acquire the same mutex before printing, so `kill` deadlocked before it could call `sysKill`.

**Fix:** fptest releases the mutex (`kUnlockScreen`) before its idle loop.

**Design limitation (open):** any task that legitimately holds the screen mutex while running (e.g. BASIC) blocks `kill` the same way. A systemic fix would have `kill` print without taking the mutex, or make `sysKill` preempt the mutex owner.

---

## Bug #53 — `fp.lib` `fact_3` not contiguous with `fact_1`/`fact_2`

**Status:** ✅ Fixed (2026-08-25)

**Location:** `src/libs/fp.asm` (ZP map + `lab_2569` / `lab_26f4` users)

**Symptoms:** `fptest` T08 ((1/3)×3) and T11 (DIV10) produced wrong values while every other arithmetic test passed.

**Root cause:** The port's ZP remap placed `fact_1=$9A`, `fact_2=$9B`, but `fact_3=$9E`, leaving the evaluator-only `func_l`/`func_h` at `$9C`/`$9D` between them. EhBASIC's `FACt_1/2/3` are contiguous (`$75/$76/$77`) and the core relies on that: `lab_2569` does `ldx #fact_1-1` and accesses `plus_3,x` = `fact_1+2` = `$9C` — the dead `func_l` byte instead of `fact_3`. The divide loop's `sta fact_1,x` (X=0..2) has the same problem. Every mantissa shift ≥8 bits (large exponent differences) or quotient with a non-zero third mantissa byte read/wrote the wrong byte.

**Fix:** `.const fact_3 = fact_2+1` — contiguity restored. `func_l`/`func_h` are evaluator ZP that the fp core never uses.

---

## Bug #54 — `fp.lib` `fpCmp` missing the fac2 sign-merge

**Status:** ✅ Fixed (2026-08-25)

**Location:** `src/libs/fp.asm` (`fpCmp` handler)

**Symptoms:** `fptest` T12 failed: FCMP(1, 2.0) returned $01 ("greater") with fac1/fac2 verified correct (fac1=8180000000, fac2=8280000000).

**Root cause:** `lab_27f8` (LAB_27F8) is designed for **external** memory floats where the sign lives in b7 of the mantissa byte. `fpCmp` passed the pointer to the **internal** fac2, where the sign lives in the separate `fac2_s` byte and b7 of `fac2_1` is the 0.5 bit — so the `eor fac1_s; bmi` sign check misrouted every positive comparison. EhBASIC's own fac2 compare (LAB_LTHAN) merges the sign first: `fac2_s | $7F & fac2_1 → fac2_1`, and `lab_27f8`'s `ora #$80` restores the 0.5 bit afterwards.

**Fix:** `fpCmp` now performs the same sign-merge before jumping to `lab_27f8` (mutates `fac2_1` exactly like the original — callers must re-load fac2 before further use).

**Note:** `fptest`'s `ftFail` also prints the live `dataPtr`, the kernel `taskDataPtrLo/Hi[slot]` entry, and the fac1/fac2 bytes on any failure. The "got 05 06 07 08 09" mystery was a **diagnostic bug** (not scheduler corruption): `ftFailLp` did `lda (dataPtr),y; tya; pha; jsr ftHexSp` — the `tya` clobbered A with Y before the print, so it printed the Y indices instead of the buffer bytes. Fixed 2026-08-25 (push Y first, then load the byte).

---

## Bug #55 — `fp.lib` ATN series missing its trailing 1.0 constant

**Status:** ✅ Fixed (2026-08-25)

**Location:** `src/libs/fp.asm` (constants after `fpAtnC`)

**Symptoms:** `fptest` S10 failed: ATN(1)*4 = `83 44 87 ED` (6.1416) instead of pi. Raw captures: atn(0.5) = 0.0886 (true 0.4636), atn(2) = 1.4821 (true 1.1071), atn(1) = 1.5354 (true 0.7854).

**Root cause:** the series evaluator (`lab_2b9b`) runs `numcon` iterations of ×x², +c, but its **first add** is c1 and its **last add** reads the 4 bytes AFTER the table (c8) — EhBASIC's series rely on that trailing constant as the leading polynomial term. In EhBASIC, `LAB_259C` (1.0) directly follows `LAB_2CC9` (the ATN table). In our port `fpQuarter` (0.25) followed `fpAtnC`, so every ATN result was off by exactly 0.75·x′ (1.0 − 0.25). The observed values matched this formula exactly: atn(1) = π/4 + 0.75, atn(2) = atn(2) + 0.375, atn(0.5) = atn(0.5) − 0.375.

**Fix:** inserted `fpAtnOne: .byte $81,$00,$00,$00` between `fpAtnC` and `fpQuarter`. After the fix S10 passes (ATN(1)*4 byte-identical to FPI) and the raw captures read 0.4637 / 1.1071 / 0.7854.

**Note:** all other series tables were verified to have their trailing constant (SIN→2π via `fpTwoPi`, EXP→1.0, LOG→2.88539), matching EhBASIC's layout.

---

## Bug #56 — `fp.lib` LOG falls into `lab_power` → infinite LOG↔POW recursion

**Status:** ✅ Fixed (2026-08-26)

**Location:** `src/libs/fp.asm` (`lab_log` tail)

**Symptoms:** `run fptest` hung on S05 LOG(E): the screen froze after "S05 LOG(E)", the task stayed ALIVE and being scheduled, its stack filled with ~70+ identical `[00 E3 72]` frames (`lab_2ad9`'s `tya; pha; jsr lab_log`), `savedSP[2]` fell from page 1 into ZP ($12/$21) as the stack overflowed through $0100 into page 0, and the task's FT_TMP breadcrumb never got written (LOG never returned). Only in VICE-monitor checkpoint runs (heavily slowed) did it appear to complete.

**Root cause:** in the original EhBASIC, `LAB_LOG` ends by setting AY = LOG(2) pointer and **falling through** into `LAB_25FB` (which sits immediately after it and ends in an RTS chain back to LOG's caller). Our port placed `lab_25fb` elsewhere and wrote `jsr lab_25fb` instead — so the RTS from `lab_25fb`'s chain returned to the byte after the JSR, which in our layout is `lab_power`'s `beq lab_exp`. Every LOG completion then executed POWER: fac2 = fpLog2 (leftover from the multiply's unpack) was positive → `lab_2ad9` → `jsr lab_log` again → LOG of |fac2| completed → fell into POWER again … an unbounded LOG↔POWER loop, 2 stack bytes per cycle, until the stack wrapped into ZP and the scheduler restored garbage SPs.

**Diagnosis:** temporary breadcrumbs in the lib (EXP/LOG stage markers + LOG/POW entry counters written via `(dataPtr),y` into the fptest data buffer) showed EXP once, LOG stage `$33` (last stage, just before the final multiply) and 155 LOG / 154 POWER entries — proving LOG completed its whole algorithm and re-entered via POWER.

**Fix:** `jsr lab_25fb` → `jmp lab_25fb` (tail call, identical in effect to the original's fall-through). Also verified every other fall-through chain of the original (`LAB_24D0→LAB_24D5`, `LAB_24F1→LAB_24F3→LAB_24F5`, `LAB_2B84→LAB_2B88`) is preserved in the port; the `basic` task's port keeps `lab_25fb` adjacent to `lab_log` and is unaffected.

---

## Bug #57 — `fp.lib` POW computes fac2^fac1, API documents fac1^fac2

**Status:** ✅ Fixed (2026-08-26) — superseded by Bug #58

**Location:** `src/libs/fp.asm` (`fpPow` handler)

**Symptoms:** with the S05 hang gone, S11 was re-enabled and produced `2^10 = 86 90 00 00` (100.0) instead of 1024.

**Root cause:** EhBASIC's evaluator leaves the exponent in fac1 and the base in fac2, so its `LAB_POWER` computes **fac2^fac1** (`func_l`/`garb_l` are the same ZP buffer: the packed fac1 = exponent). The lib's documented API said `fac1 = fac1 ^ fac2`, and fptest passed base in fac1, exponent in fac2 — so the inherited algorithm returned exponent^base.

**Fix:** `fpPow` swapped the 5 contiguous fac1/fac2 fields (e,1,2,3,s) before `jmp lab_power`. With fptest's convention this yielded the right goldens (`2^10 = 8B 00 00 0F 00`, `2^-1 = 7F 7F FF FD 00`, `0^0 = 81 00 00 00 00`). **However** that swap converted LIB-native operands into `LAB_POWER`'s convention — but the BASIC wrapper (`fpPowW`) passes EhBASIC-evaluator operands (fac1=exponent, fac2=base) directly, so through the wrapper the swap double-converted: `2^2`→4 looked right, but `2^10` would have printed 100. The swap was removed in Bug #58; `FP_POW` is now `fac1 = fac2^fac1` (left^right), matching the lib's documented binary-op convention.

---

## Bug #58 — BASIC `^` with negative exponent → "syntax error" (POW convention + `prstk` ZP clobber)

**Status:** ✅ Fixed (2026-08-26)

**Location:** `src/libs/fp.asm` (`fpPow`), `src/tasks/dynamic/basic.asm` (`fpCall`)

**Symptoms:** after the Step-4 wrapper migration, `print 2^-1`, `print 2^-2` and `print 2^(-1)` all reported "syntax error", while `print 2^2`, `print 2^10`? (untested), `print -1` and `print 0-1` worked.

**Root cause (two independent bugs):**

1. **Operand-order double conversion.** Bug #57's swap converts lib-native operands into `LAB_POWER`'s EhBASIC convention (fac1=exponent, fac2=base). The BASIC wrapper passes EhBASIC-evaluator operands already in that convention, so the swap inverted them: `LAB_POWER` then computed exponent^base. `2^2`→4 hid it; `2^10` would print 100. Fixed by deleting the swap — `FP_POW` = `fac2^fac1`, which is exactly `LAB_POWER`'s semantics and the lib's documented `fac2 = LEFT, fac1 = RIGHT` convention.

2. **`prstk` ($99) clobbered across the lib call.** The lib's `lab_27f8` (used by POW's negative-base branch and FCMP) writes its pointer to `ut2_pl/h` = `$98/$99` — `$99` is EhBASIC's live `prstk`. The wrapper only saved `$BC/$BD` + `$C1-$C5`. After a negative-exponent pow, `prstk` came back positive ($C1) instead of $FF, so `lab_1b1d+3`'s `ldy prstk; bpl lab_1b3c` re-dispatched the `>` row (negate) and re-entered evaluation at `[eol]` → `lab_sner` — the observed syntax error. (Only the negative-exponent path hits `lab_27f8`, which is why positive exponents "worked" apart from the wrong-value bug.)

**Fix:** `fpCall` now also saves/restores `$98-$9E` (prstk, comp_f, func_l/h, des_2l + the lib's ut2/fact/adatal aliases) around every lib call; `fpSave` grew from 7 to 14 bytes.

**Note (latent, not fixed):** `src/tasks/dynamic/ehbasic.asm`'s LOG constant table has stray live bytes `$76,$22,$F1` (leftover `;##` alternative constants that the comment conversion didn't strip) — harmless today only because the port's `numcon` stops before them, but they sit in live memory after the table. The fp.lib's own table is clean.

---

## Bug #65 — `run basic 10` loses the fp.lib (clobbered `dlLibMask` in `drFound`) 🔴 ✅ FIXED

**Status:** ✅ Fixed (2026-08-29)

**Location:** `src/kernel/dynload.inc` (`drFound`)

**Symptoms:** `run basic 10` then any fp-using command fails — `print 1+1` → "divide by zero", `print sin(0)` → "function call" (the `libTrap` cold value is still in `libVec+38`). `run basic 9` and `run fpdemo` work fine, and killing a working fpdemo frees fp correctly — the failure is specific to the load that triggers eviction.

**Trigger:** `basic` code = 46 pages (65 today — the binary has grown); `KF_MALLOC_RAM` reserves the program-RAM region at fixed pages 46..46+pages. With 10 RAM pages the region covers 46–55, fragmenting the pool so the 46 code pages no longer fit → `dlLoadCore` **evicts the shell** (the caller). With 9 pages the region is 46–54, leaving a 46-page run at 55–100 → no eviction.

**Root cause:** `drFound` stores the parsed lib mask with `lda dlLibMask; sta dynTaskLibMaskLo,y`. But `dlLibMask` is loader-global scratch that `dlLibRelease` — reached via `dlEvict` inside `dlLoadCore` — **overwrites with the victim's mask**. Order of operations: `dlSizeOk` parses basic's mask `$0A` (fswrite+fp) → `dlLoadCore` evicts the shell, whose `dlEvLibs`→`dlLibRelease` sets `dlLibMask = $02` (fswrite only) → `drFound` then stores `$02` as basic's per-slot mask. `dlLibEagerLoad` sees no fp bit, so the eager fp.lib is never loaded (`libVec+38` stays `libTrap`) and the fp refcount incremented at `dlSizeOk` is never matched by a release — hence the leak.

**Fix:** `drFound` now copies `dlRollMask`/`dlRollMaskHi` (the mask stashed at `dlSizeOk`, written once and never clobbered) instead of `dlLibMask`/`dlLibMaskHi`. Kernel-only, one instruction pair — no task/REU rebuild needed.

**Lesson:** `dlLibMask`/`dlLibMaskHi` are shared scratch that `dlLibRelease`/`libFree` clobber; after any call that can reach `dlEvict`/`dlLibRelease`, read the parse mask only from the `dlRollMask` stash.

---

## Bug #66 — `run basic 4` after `run fpdemo` warm-reboots (contiguous code+RAM block couldn't fit) 🔴 ✅ FIXED

**Status:** ✅ Fixed (2026-08-29)

**Location:** `src/kernel/dynload.inc` (`dlLoadCore`)

**Repro:** boot → `run fpdemo` → switch to the shell (fpdemo keeps
running) → `run basic 4` → warm reboot (the shell reloads with a fresh
pool — `findNextAlive`'s "no alive tasks → `jmp start`" fallback).

**Layout at the failure:** shell 0-19, fpdemo 20-21, gfx.lib 22-48,
fp.lib 49-58, free 59-100. `basic 4` = 46 code + 4 RAM pages, which the
loader allocated as **one 50-page contiguous block** (the string.lib-hole
fix). Evicting the shell frees 0-19, evicting fpdemo + gfx frees 20-48 —
but fp.lib (10 pages, a lib **basic itself declares** and shares with
fpdemo) stays pinned at 49-58, capping the largest free run at **49**
pages. A 50-page block can therefore never be allocated even though 91
pages are free in total. The loader evicted every eligible victim, failed
with error 4, and left zero ALIVE tasks (shell + fpdemo both swapped,
basic never spawned) → warm reboot.

**Fix:** allocate the code and program-RAM as **two separate
`allocFromPool` blocks** (code first, then RAM anywhere it fits; if the
RAM block doesn't fit, the code is freed and the evict-and-retry loop
runs again). `basic 4` then loads as code 0-45 + RAM 59-62 — no 50-page
run required. This still fixes the original string.lib-hole bug (the RAM
is no longer pinned to a fixed offset) and matches the existing free
paths, which already free code and RAM as separate regions.

**Lesson:** a task's code and its `kMalloc` program RAM must not be
forced contiguous — a resident lib the task itself declares can strand
them apart, and a failed contiguous request after self-eviction can leave
no runnable task.

---

## Bug #67 — `run basic 3` (any eager-lib task that doesn't fit) warm-reboots 🔴 ✅ FIXED

**Status:** ✅ Fixed (2026-08-30)

**Location:** `src/kernel/dynload.inc` (`dlReloc`), `src/kernel/poolevict.inc` (new)

**Symptoms:** `run basic 2` started but `run basic N` (N > 2) "restarted
the shell" — the system warm-rebooted and basic never appeared.

**Root cause:** basic's footprint is `code (53) + program RAM (N) +
gfx.lib (26) + fp.lib (8)` pages. The loader loaded the code + RAM first
and `dlLibEagerLoad` loaded the eager libs afterwards. With N = 3 the
code + RAM block (56 pages) sat at the bottom of the pool, leaving only
25 free pages before the shell. Evicting the shell (20 pages) split the
free space into a 25-page run and a 20-page run — neither large enough
for `gfx.lib` (26). Every eligible victim was evicted, the eager load
failed with error 4, and no ALIVE task remained → `findNextAlive` →
`jmp start` (warm reboot). With N ≤ 2 the leftover run was ≥ 26 so
`gfx.lib` fit.

**Fix:** `dlPreEvict` runs before `dlLoadCore` (called from `dlReloc`'s
`rlAligned`). It computes `needed = code + RAM + eager libs` (peeking each
non-resident eager lib header for its exact page count, using `libPages`
for resident ones), fails cleanly with `? no space` if
`needed > POOL_PAGES`, otherwise evicts largest-first until `free ≥ needed`.
The whole footprint then fits in one pass. Mask-0 tasks return instantly;
eager-but-fitting tasks are unchanged.

**Lesson:** the *order* of pool allocations matters when a task's total
footprint (code + RAM + libs) exceeds the free space — make room for the
whole footprint before the first allocation, or the largest single piece
(`gfx.lib`) gets stranded by fragmentation. Also: the task's header name
(`"basic"`) is not its FS filename (`"basic.tsk"`) — the loader's
`reuFsPtr` (set by the find at `dynLoad` step 0) cannot be re-derived
from `dynNameBuf`, so `dlPreEvict` saves/restores it directly.

---

## Bug #68 — `run basic 4` after `run fpdemo` still warm-reboots (declaration-time eager refcount pinned `gfx.lib`) 🔴 ✅ FIXED

**Status:** ✅ Fixed (2026-08-30)

**Location:** `src/kernel/dynload.inc` (`dlLibRefcount`/`dlLibEagerLoad`/`dlLibUnref`),
`src/kernel/poolevict.inc` (`dlEagerLibPages`)

**Symptoms:** `run basic 14` worked and `run basic 15` said `no space`,
but `run fpdemo` → switch to the shell → `run basic 4` still warm-rebooted
instead of loading (or erroring).

**Root cause:** after `run fpdemo`, `gfx.lib`/`fp.lib` are resident with
refcount 1 (held by fpdemo). When basic loaded, `dlLibRefcount` (at
`dlSizeOk`) incremented basic's *declared* eager libs immediately, so gfx
rose to refcount 2. Evicting fpdemo then only dropped gfx to 1 — **basic's
pending declaration-time reference kept it resident and pinned** in the
middle of the pool, fragmenting the free space so basic's 53-page code
could not fit contiguously → error 4 → warm reboot.

**Fix:** eager libs now take their reference when they are **loaded**
(`dlLibEagerLoad` does `inc libRefCount,x`), not when declared
(`dlLibRefcount` refcounts lazy libs only; `dlLibUnref` mirrors it).
Evicting fpdemo therefore drops gfx to 0, frees it, and basic reloads it
contiguously. `dlEagerLibPages` also counts resident eager libs (via
`libPages`), so `needed` reflects the re-loadable footprint and oversized
requests fail with `no space` before evicting anything.

**Lesson:** a library is "owned" by a task only once that task actually
holds it; taking a refcount at *declare* time turns the incoming task into
a phantom holder that pins the lib against the very eviction that would
free it. Refcount eager libs at load, lazy libs at declare.

---

## Bug #60 — `reuDmaRange` split-DMA off-by-one corrupts boundary-straddling files 🔴 ✅ FIXED

**Severity:** Critical — whole-system crash (falls back to the BASIC READY
prompt, i.e. the OS effectively rebooted).
**Status:** Fixed 2026-08-26
**Location:** `src/kernel/reu-fs.inc` `reuDmaRange` → `rdrSplit`

**Symptoms:**
- `run border` (or any task) followed by `kill <id>` hard-crashed into the
  READY prompt — no shell error, no red border, just a reset.
- Surfaced only after the `wait` shell command (+81 B) and wildcard `dir`
  (+437 B) landed: their combined growth shifted `KILL.COM` across the REU
  bank-5 → bank-6 boundary, so the loader took the split-DMA path for the
  first time during the `kill` test. (`SHELL.TSK` itself never crossed —
  it was `KILL.COM`, the file the `kill` command loads.)

### Root Cause

`reuDmaRange` handles a file straddling the bank boundary by DMA'ing the
first chunk from bank 5, then a second chunk from bank 6 to `dest + chunk1`.
The destination was saved on the stack at entry (`php`, then `dest_lo`, then
`dest_hi` — so `dest_hi` ends up on top). The split code read it back with
the wrong offsets:

```asm
tsx
clc
lda kArg+3; adc $0101,x; sta $0101,x   ; "dest_lo" — actually dest_hi
lda kArg+4; adc $0100,x; sta $0100,x   ; "dest_hi" — actually free stack
lda $0101,x; sta kPtr
lda $0100,x; sta kPtr+1
```

With `X = SP`, the saved `dest_hi` is at `$0101,x` and `dest_lo` at
`$0102,x`; `$0100,x` is the caller's stack byte one below them. So the code
(a) overwrote the saved `dest_hi`, (b) built the second chunk's destination
from a garbage high byte, and (c) handed the loader a corrupted `kPtr` —
smashing memory on any file that crosses the boundary.

### Fix applied

Compute `dest2 = dest + chunk1` from the correct offsets **without writing
back**, leaving the stack intact so `rdrDone` still restores the original
destination (the loader needs `kPtr = load_addr` after the call):

```asm
tsx
clc
lda $0102,x                  // dest_lo
adc kArg+3                   // + chunk1_lo
sta kPtr                     // kPtr = dest2_lo
lda $0101,x                  // dest_hi
adc kArg+4                   // + chunk1_hi + carry
sta kPtr+1                   // kPtr+1 = dest2_hi
```

### Notes

- The bug was **pre-existing** (kernel code, untouched by the `wait`/`dir`
  changes); the shell + dir growth only moved `KILL.COM` onto the boundary
  and exposed it. Any future FS-layout change can re-trip a latent boundary
  case — a file landing with `dataOff + size > $6000` takes this path, so
  it's worth watching the `build-reu` totals after large additions.
- Kernel rebuild verified: 118 asserts, 0 failed.


---

## Bug #61 — `runbatch`/`type` misread a line whose DMA start crosses a 256-byte page boundary 🔴 ✅ FIXED

**Severity:** High — batch lines (and `type` output) read garbage when a
streamed line straddles a page boundary in the FS data area, so the command
fell through to the `.com` loader and printed `? no fs or not found`.
**Status:** Fixed 2026-08-27
**Location:** `src/tasks/dynamic/shell.inc` `rbMore` +
`src/tasks/cmd/type.inc` (`ty*` address computation)

**Symptoms:**
- `runbatch boot` executed the first few lines then failed on `me` with
  `? no fs or not found`; every command in `boot.bat` worked when typed by
  hand. The failing line depended on the REU layout: adding `scrltst.tsk`
  (522 B) shifted `boot.bat` from offset `$AFC2` to `$B1D4`, which moved the
  page boundary from past-the-EOF (offset 62) to inside the file (offset 44),
  so `me` (file offset 46) crossed it while `ver` (offset 41) did not.

### Root Cause

The 3-term 17-bit REU address `dataBase + offset + pos` was computed with a
single carry chain that re-added the low byte's carry back into the low byte:

```asm
    clc
    adc (dataPtr),y        ; offset_lo + pos_lo   → C = carry for the HIGH byte
    adc reuFsDataBaseLo    ; $00 + C               ← wrong: re-adds carry to LOW byte
    sta kArg               ; addr_lo off by one page when offset_lo+pos_lo > $FF
```

`reuFsDataBaseLo` is `$00` (data base `$A000` is page-aligned), so that `adc`
should be a no-op — instead it folded the `offset_lo + pos_lo` carry into the
low byte. Any line whose window start crosses a 256-byte boundary
(`offset_lo + pos_lo > $FF`) was DMA'd from one page too early → garbage.
It was deterministic once the FS layout put a line across a page boundary,
and "went away" when files were added/removed (which shifted the offsets) —
exactly matching the reported intermittent-but-deterministic behaviour.

### Fix applied

Drop the erroneous `adc reuFsDataBaseLo` (base_lo is `$00`, so the low byte is
simply `offset_lo + pos_lo`, and the carry now propagates to the high byte):

```asm
    ldy #BATOFF
    lda (dataPtr),y             // offset lo
    ldy #BATPOS
    clc
    adc (dataPtr),y             // + pos lo (carry → hi)
    sta kArg                    // REU addr lo
```

The 2-term sites (`banner.inc`, `edit.asm`, kernel `reuDmaRange`) were already
correct — no intermediate carry to lose.

### Notes

- Same class as Bug #60: a latent carry/offset bug exposed only when a REU
  layout change moved a file onto a boundary. Worth re-testing `runbatch` and
  `type` after any large FS addition.
- Task rebuild verified: shell 109 fixups / 5020 B, type 20 fixups / 612 B,
  relocation OK; REU rebuilt (48130 bytes).


---

## Bug #62 — Shared-surface tasks leak a character one row below the scroll region 🔴 ✅ FIXED

**Severity:** High — two tasks sharing one surface (via `kShareScreen`) could
write a character at `bottom+1`, below the scroll region, where the scroll
never clears it (a permanent stray cell).
**Status:** Fixed 2026-08-27
**Location:** `src/kernel/screen-io.inc` `newline`

**Symptoms:**
- `run scrshar` (Step 8 shared-surface test: parent + child share a surface,
  region 8..16) mostly worked, but a static `c` appeared on row 17 — one row
  below the region bottom — and never moved or cleared.

### Root Cause

`newline` ran with interrupts enabled. Its cursor advance has a window where
`cursorRow` is transiently `bottom+1`:

```asm
inc cursorRow     ; row 16 → 17 (one past the region)
jsr wrapClamp     ; clamps 17 back to 16, then scrolls
```

If the 60 Hz scheduler fired between the `inc` and the clamp, `switchTask`
snapshotted `cursorRow = 17` into the surface's cursor tail (both sharing
tasks use the same tail). The other task then restored `cursorRow = 17` and
its next `kPrintChar` wrote a char at row 17 — outside the region, so the
scroll never cleared it. `printChar` already guarded its own wrap with
`sei`/`cli`, but `newline` had no such guard.

### Fix applied

Make `newline`'s cursor advance + clamp atomic, matching `printChar`. Kept
the routine the same byte count (net-zero, so no aliases moved) by dropping
the now-redundant second `lda #0` (A is still 0 after `sta cursorOn`):

```asm
newline:
    sei
    lda cursorOn
    pha
    lda #0
    sta cursorOn
    sta cursorCol     // A still 0 — saves a byte, keeps size net-zero
    inc cursorRow
    jsr wrapClamp
nlDone:
    pla
    sta cursorOn
    cli
    rts
```

### Notes

- Kernel rebuild verified: 119 asserts, 0 failed; memory map unchanged
  (`screen-io` region `$e11a-$efa6`, `gfx.inc` still at `$f063`) — net-zero
  size avoided the usual `$E000+` alias churn.
- Exposed by the Step 8 `scrshar` test, but the race is generic: any two
  tasks sharing a surface and printing concurrently could hit it.

## Step 4 BASIC (REU-backed) string + INPUT + CONT + carry fixes 🟡 ✅ FIXED (2026-09-03)

**Symptoms:** `a$="dddd"` then `print a$` printed ~130 `@`s / blank;
`print 3+4` blank; `left$`/`right$`/`mid$` broken; "string too complex"
after three string ops; INPUT read REU bytes at `$02xx`; `cont` said
"can't continue" for offsets in `$02xx`; first `run` gave "syntax error".

**Root causes (all in the Step 4 domain conversion of `ehbasic.asm`):**

1. **Descriptor pointer ambiguity.** With program RAM in REU, ZP descriptor-
   stack entries (`$00xx`) and REU var offsets (high byte `$00`) are
   numerically indistinguishable — the stock `des vs svarl/svarh` test and
   raw `lda (zp),y` descriptor reads are unsound. Fixed with explicit
   domain flags (`desDom`/`des2Dom`/`ssDom`/`gcDom`/`fac2Dom`) + the
   `descRd*` readers.
2. **A-clobbers at the flag-insertion sites.** `lab_209c` destroyed
   A = string length before `lab_2115` (every string allocated 1 byte →
   blank prints); `lab_1c18` destroyed A before `readVar`; `lab_226d`
   passed `ssDom` instead of `ssptr_l` to `lab_22ba`. Fixed by
   preserving/reloading A.
3. **`lab_22eb` never popped.** The domain gate left A = `desDom`, so the
   `cmp last_sl` in the descriptor-stack pop never matched → `next_s` grew
   3 bytes per string op → "string too complex". Fixed by reloading
   `ut1_pl`/`des_2l` before the call; REU-var descriptors skip the stack
   pop entirely.
4. **INPUT/READ text-domain.** `lab_195b` pointed `bpntr` at the C64
   `ibuffs` while `textDom` stayed 0 → `lab_gbyt` read REU offsets `$02xx`.
   `textDom`/`strC64Src` are now derived from `imode` on the pointer switch
   and reset to REU when `bpntr` returns to program text (`lab_19c5`,
   `lab_1913`).
5. **CONT direct-mode test.** Stock `cpy #>ibuffs` is false-positive for
   REU offsets in `$02xx`. Replaced with a `contDom` byte saved alongside
   `cpntr` at `lab_15c2`/`lab_163b`.
6. **`textByteAtY` carry leak.** The DMA path saved flags at `tbaDma`
   entry (already polluted by `cmp`/`sbc`) instead of at `textByteAtY`
   entry; the line-buffer path returned C=0 unconditionally. `fioP` is now
   saved before the `clc` and C restored on both exits (`lsr` pattern).

**Verified:** user-confirmed in VICE — `print 3+4`=7, string literal,
`a$="dddd"`→`print a$`=`dddd`, `len`=4, concat, `left$/right$/mid$`,
`b$=a$`, string compare, `10 print 3` first run, counting loop, INPUT
numeric/string/multi-variable, READ/DATA, CONT, empty-input re-prompt.
All diagnostics removed; builds green (10 asserts, 0 failed).

---

## Bug #106 — `run edit <file>` vanished with no message when the pool could not give it a document chunk 🔴 ✅ FIXED

**Reported (2026-09-14):** boot → `run gortris` → switch to the shell →
`run edit boot.bat`. The editor never appears (a flicker at most), `gortris` and the
shell keep running, and **nothing is printed**. The same command works when `gortris`
is not resident, and `run edit <newfile>` works — which is what made it look like a
malloc/resource problem rather than a loader one.

**Root cause was one page of arithmetic.** `edit` needs `ceil(9209/256)` = **36** pool
pages + a **4**-page document chunk (`CHUNK_PAGES`: 24 records × 41 B) = **40**. With
`gortris` resident the pool held 71 used / 38 free (shell 19 + filesys 10 + string 2 +
gortris 23 + sid 13 + gortris's 4-page tune block), but `dlPreEvict` sizes a load as
`code + RAM + eager libs` = **36** — the code only. So 36 ≤ 38, **no eviction was
attempted**, the load succeeded, the editor took the display (the flicker), its
`kMalloc(4)` then failed against the 2 free pages, and `edInitFail` exited without
printing. The shell could not report an error because from its point of view the load
succeeded.

**Fix — both halves (user-approved "1 + 3"):**

1. **`kMalloc` evicts and retries (kernel).** `kmEvictAlloc` (`$E201`, inside the dead
   16-byte `hexChars` block, so it costs the tree no free space and moves no address)
   calls `dlEvict` with `dlNoSelfEvict = 1` and then re-enters `allocFromPool`.
   Protect-the-caller is the point: it is executing its own code. In this scenario the
   victim is the shell (gortris is pinned) — 19 code + 2 released `string.lib` pages —
   which lets the chunk fit, and `dlRestore` brings the shell back when the editor
   exits. Only the **pool-full** site is hooked: invalid input, the per-owner cap and
   the table-full cases still fail at once, because eviction cannot fix them.
2. **The editor says why (task).** `edNoRoom` blits `no space` on the editor's own
   status line (it owns the display whenever this is reachable): from the chunk
   failure — with `kWaitKey`, so the message survives the hand-back — and from
   `elGrowChunk`'s failure, the old silent "can't edit more than one page".
   `edit` grew 9209 → **9230 B = 37 pages**.

**⚠️ Regression, found and fixed the same day — the hook must be on the FAILURE branch, never
around the attempt.** The first version replaced `kmAllocNow`'s `jsr allocFromPool` with
`jsr kmEvictAlloc` (evict-then-allocate), so **every** `kMalloc` evicted a task before trying:
booting printed only the banner (the banner command's 2-page `kMalloc` evicted the *boot task*
mid-`boot.bat`, so `ver`/`me` and the shell load never ran) and `run gortris` evicted the shell
immediately. The wiring is now `jsr allocFromPool` → `bcc kmAllocOk` → `jsr kmEvictAlloc` →
`bcs kmNoSpace` (falls through to `kmAllocOk` on success, reuses the shared tail on failure),
which costs the same bytes: +2 at the site is paid by removing the `.fill 2, $ea` size keeper.
Emitted: `AE 00 40  20 10 43  90 05  20 01 E2  B0 43`. **Lesson: an allocation-recovery hook
belongs on the branch that means "it failed", not on the call that can succeed** — and a
boot-time `kMalloc` is a good canary, because the only other task in the system is the caller's
parent.

**Verified:** kernel 212 asserts / 0 failed; free total unchanged at **203 B across 18
pockets**; `edit` relocation verification OK; REU reseeded (52 files);
`tools/verify-map.ps1` green. Emitted bytes checked: the hook is `20 01 E2`
(`jsr kmEvictAlloc`) and the routine is `8A 48 A9 01 8D 49 51 20 89 4B 68 AA 4C 10 43 EA`.

**⚠️ Consequence worth knowing:** any task's `kMalloc` can now swap out — or kill —
another task. See programmers-guide → *Pool eviction*.

---

# DUPLICATE & DEAD CODE

---

## Issue D1: `initTCB` / `initTCB_ptr` Near-Duplicate ⚫

**Location:** `src/init.inc`

These two routines are ~95% identical. Both build a TCB stack frame — the only difference is the task address source:

| Routine | Address source |
|---|---|
| `initTCB` | `taskLo,x` / `taskHi,x` |
| `initTCB_ptr` | `spawnPtr` / `spawnPtr+1` |

### Suggestion

Merge by loading `spawnPtr` from the appropriate source before calling a single implementation:

```asm
initTCB:
    lda taskLo,x; sta spawnPtr
    lda taskHi,x; sta spawnPtr+1
    ; fall through to initTCB_ptr
initTCB_ptr:
    ; ... builds frame from spawnPtr ...
```

---

## Issue D2: `viewScreen` / `switchView` VIC Update Duplicated ⚫

**Location:** `src/kernel.asm`

Both routines contain identical code to update `dispBase` and `$D018` for private screens. `switchView` adds keyboard focus transfer and buffer drain; `viewScreen` is display-only. `switchView` could call `viewScreen` for the display part and handle only the keyboard logic separately.

---

## Issue D3: REU DMA Save/Restore Near-Duplicate ⚫

**Location:** `src/scheduler.inc`

`saveStackReu` and `restoreStackReu` differ only in the command byte (`$90` vs `$91`). All REU address setup ($DF02–$DF08, $DF0A) is identical. A single subroutine parameterised by command byte would save ~30 bytes.

---

## Issue D4: `enableCursor` / `disableCursor` Duplicated Tail Write ⚫

**Location:** `src/kernel.asm`

Both routines compute `screenBase + $3EB` (the `SCR_CURSOR_ON` offset) with identical pointer arithmetic, differing only in the value written (1 vs 0). Could share a helper.

---

## Issue D5: Dead Branch in `byteToDec` ⚫ ✅ RESOLVED

**Location:** `src/kernel/math.inc` — `byteToDec`

Obsolete: `byteToDec` was rewritten 2026-08-25 (Bug #51 — the digit
corruption), and the old `bdOnes`/`bdPutTens`/`cpy #0` structure no
longer exists. The replacement algorithm has no dead branches.

---

## Issue D6: Vestigial `taskCount` Variable ⚫

**Location:** `src/tasks/tasks.inc`

```asm
taskCount:
    .byte 0
```

Set to 1 at boot (`init.inc`) but never updated afterward. `totalTasks` (in `main.asm`) is the live runtime count used by the scheduler and syscalls. `taskCount` appears to be left over from before dynamic spawn was implemented. It is only read during the boot REU copy loop. Consider removing and using `totalTasks` directly.

---

# CONCISION OPPORTUNITIES

---

## Bug #121 — A status byte parked on top of the parked arguments came back as an argument 🔴 ✅ FIXED

**Severity:** Critical — a task-level sprite call corrupted the kernel's sprite tail and left
the machine with interrupts disabled.
**Status:** ✅ FIXED 2026-09-20. Introduced and fixed in the same session, while implementing
Bug #120 item 1.
**Location:** `src/libs/sprite.asm` — all four setters.

### Symptom

`run godzi` (or `sprdemo`) hung the machine a fraction of a second after the demo took the
display: the screen kept whatever the demo had drawn, the cursor block stayed where it was,
and no further tick ran. Identical whether the setters were reached at startup or per frame,
and the boat stopped before it ever moved.

### Root cause

The first version of #120's fix wrapped each setter's guard-and-store in a `php`/`plp` pair and
placed the `php` **after** the arguments had been parked. `php` therefore became the top of the
stack, and every later `pla` came off one byte high:

    pha                     ; [1] the value          (H)
    txa
    pha                     ; [2] the offset         (H-1)
    jsr sprInstall          ; clobbers A, X, Y, kPtr, kArg
    php                     ; [3] THE STATUS         (H-2)  <- top of the stack
    ldy curTask / cpy kbFocus / bne ...
    pla                     ; pops H-2 = THE STATUS   -> X = the status byte
    tax
    pla                     ; pops H-1 = the offset   -> A
    sta sprShadow,x         ; indexed by the status byte; the bound check only saw the ORIGINAL X

Two failures follow from that one mistake:

* **The store is indexed by the status byte.** `php` always leaves bits 4 and 5 set, so the
  index is `>= $30`: `sprShadow + $30` is `$CFEC` and upward — the colour array, the control
  bytes, `spriteSlotLo` (`$CFEB`), `spriteSlotHi`, `sprLiveOwner`, `sprPubMask`, the collision
  latches and `sprFrameCount`, and for a larger status the VIC mirrors. A corrupted
  slot-table entry is the fatal one: the next `sprFrameDma` builds its destination from it and
  DMAs 64 bytes to a garbage C64 address.
* **`plp` never saw the status.** It popped the parked *value* and restored the flags from it,
  **including I, which is bit 1 of that byte**. When that bit is set — godzi's enable masks and
  several of its colours have it set — the task resumes with interrupts **disabled**: no timer
  ticks, no scheduler. That is the hang, and it is why the crash looked like a dead machine
  rather than a corrupt display.

The fault is invisible to the assembler and to every map and size check: the push/pop counts
balance on every path and the branch targets are right. Only the *order* was wrong, so only the
machine could see it.

### Fix

The window parks no status byte: `sei` before the guard, `cli` at each exit that returns from
inside it, so no pop comes off one byte high. `spriteSheet` is the instructive case — its
`php`/`sei` had always been correct because it pushes the status *after* popping its `kPtr`
bytes, and its exits pop the status *first*. With no status byte at all, the sprite it reads
back off the stack moves from `$0102,x` to `$0101,x`; the offset had to change with it, or the
record stores would have been indexed by a stale byte instead.

**Rule:** never park a status byte on top of values that older pushes left for later pops to
consume. If a caller's I flag has to be preserved rather than assumed — these setters clear
with `cli`, i.e. they assume a task-level caller — push the status **before** the arguments and
pop it **after** them.

---

## Bug #120 — Two latent holes in the sprite setters: a racy guard and a bad-slot stack pop 🟡 ✅ FIXED

**Severity:** Low as shipped (neither is reachable from a correct task), high in kind —
one leaks a write across tasks, the other corrupts the stack on a bad argument.
**Status:** ✅ FIXED 2026-09-19 — found by review of the per-task sprite branch, not by a symptom.
**Location:** `src/libs/sprite.asm` (all four entries)

### 1. The focus guard was not atomic with the store

Each entry calls `sprInstall`, compares `curTask` with `kbFocus`, and then writes. The
compare-to-store span ran with `I=0`, so a timer tick landing inside it could switch tasks —
and if that tick also moved the display (an F-key switch, or another task calling
`kSwitchView`), the interrupted task would resume and write into the **new** owner's live
state. That leak survives, because the next hand-over saves that state into the new owner's
REU block. Window: ~30-50 cycles per call, so a few percent of ticks while a sprite task
runs, and only a display change inside the same window turns it into damage.

**Fix:** `sei` immediately before the guard and `cli` at every exit that returns from inside
the window, so the compare and the write are one step. `spriteSheet`'s existing window (for the
shared `reuFsPtr` during the FS find) simply widened above the guard. The exits clear with
`cli` rather than a `php`/`plp` pair because each setter parks its arguments on the caller's
stack, and a status byte on top of them comes back as an argument — that fault is Bug #121, and
it is why the first version of this fix was wrong.

### 2. `kSpritePos` unwound four pushes with three on the stack

The bound check sat after `pla`/`tax` — by then 3 of the 4 parked inputs remained, but the
reject path popped four, so a sprite index of 8+ consumed a byte of the caller's return
address. Reachable only from a task bug (the documented contract already says 8+ is
rejected), but a kernel call must fail safely.

**Fix:** the reject path got its own three-pop tail (`sprPosArgBad`) instead of sharing the
four-pop one, so a sprite index of 8+ leaves the stack exactly as it found it. The check itself
stays **below** the `rol` that consumes the input carry: `cpx` clobbers the carry, and the
carry *is* the sprite's 9th bit, so a check moved above it silently pins every sprite to
x < 256 — which is a wrong position, not a rejected call.

Cost: `sprite.lib` 209 -> 224 bytes, still one pool page. Neither fix changes any public
behaviour: the documented contracts (guard rejects with `C=1` and writes nothing, sprite 8+
is rejected) now hold in every interleaving. Both shipped **one setter per build**, each
confirmed in VICE with `run godzi` and `run sprdemo` before the next one went in — the order
matters, because the first attempt at item 1 (all four setters at once, wrapped in
`php`/`plp`) was Bug #121.

---

## Bug #119 — A task that handed back a library it kept calling froze its own sprites 🔴 ✅ FIXED

**Severity:** High — one console command (`run godzi`) left a demo visibly stuck, and
the cause was in the kernel's library system, not in the demo.
**Status:** ✅ FIXED 2026-09-19 — a regression introduced by per-task sprite state, which
moved the sprite API out of the kernel and into `sprite.lib`.
**Location:** `src/kernel/lib-hold.inc` (`kLibHold`, stub `$FFE6`) — `src/libs/sprite.asm`,
`src/tasks/dynamic/godzi.inc`

### Symptoms

`run godzi` on its own: the boat is drawn, but it never moves and the pose never flips.
Starting a second sprite task and switching back to the godzi screen made it animate again
(which is what made it look like a display-switch bug). With both demos resident, switching
between them also showed the wrong art, but that was Bug #118 — this one is the freeze.

### Root cause

`kLibHold` releases **every** library the calling task declared except the one id in A, by
rewriting the task's stored `dynTaskLibMaskLo`. `godzi` kept `sid.lib` (`lda #5`) and so
released `sprite.lib` — which, before this branch, was not a library at all: the sprite API
was kernel-resident, so no library policy could affect it. With the API as an eager library
the demo still calls it every frame (`gzPlaceBoat` -> `kSpritePos`, `gzEnable` ->
`kSpriteReg`), and a library a task has handed back stops serving it. The art is already in
the slots from `gzLoadArt`, which runs *before* the hold, so the screen showed a correctly
drawn, permanently frozen demo. `run sprdemo` re-loaded `sprite.lib` (it declares it), which
is what actually revived godzi — the screen switch was incidental.

### Fix

`kLibHold` now takes a **mask of libraries to keep** instead of one id, so `godzi` passes
`(1 << LIB_SID) | (1 << LIB_SPRITE)` and still frees filesys/gfx/string/fp. `sidplay` passes
`(1 << LIB_SID)` and behaves exactly as before. The routine is shorter (33B) and the pocket
is padded with an inert NOP sled so no pinned address or map total moves.

**Rule:** a library the task keeps *calling* has to be in the keep mask. `docs/lib-sprite.md`
and `docs/dynamic-libraries.md` now state it where a sprite task will read it, because the
trap is invisible: the calls simply have no effect.

---

## Bug #118 — `kSpriteFrame` recorded the sprite as its own frame index, so a hand-over re-DMA'd the wrong art 🔴 ✅ FIXED

**Severity:** High — the first display hand-over corrupted every task's sprite art.
**Status:** ✅ FIXED 2026-09-19 — introduced with `sprite.lib` (per-task sprite state).
**Location:** `src/libs/sprite.asm` (`spriteFrame`)

### Symptoms

`run godzi` then `run sprdemo`, then switching between the two screens: slots come back
showing the *other* task's art — the boat and the walker on the sprdemo screen, and the
walker corrupted on the godzi screen. The demos look correct until the first hand-over,
which is why it looked like a switching bug.

### Root cause

`sprFrameIdx[slot]` is what `sprRestoreFrames` re-DMAs when a task takes the display back.
`spriteFrame` pops the frame into X, the sprite into A, then stored **A** — the sprite — as
the frame, so every slot recorded `sprFrameIdx[slot] = slot`. The startup load is unaffected
(it DMAs with X), so nothing looks wrong until the first hand-over.

**The evidence that named it** (VICE monitor, `m cfa4 cfff` with the wrong screen displayed):
all eight sheet records read `lo=$76 mid=$6F hi=$01` — the 24-bit FS offset `$016F76` — and
`sprFrameIdx` read `00 01 02 03 04 05 06 07`. Resolving `$016F76` against the REU directory
(`tools/build-reu.ps1`'s layout: directory at bank 5 `$9000`, data at bank 5 `$D000`) gave
**`ball.spr`, size 64 — one frame**. So frame *N* reads `ball.spr + N*64`: slot 1 landed on
`boat.spr`, slot 3 on `godzi.spr`, slot 5 on `boot.bat`, slot 7 inside `logo320.bnr` — which
is exactly the art that appeared on the wrong screens.

### Fix

Store the frame, not the sprite: the frame is in X at that point, so the store is
`txa / sta sprFrameIdx,y / tya` (A has to stay the sprite for the `sprFrameDma` call that
follows). +2B in `sprite.lib`.

---

## Bug #122 — the paddle bounce stored the value's integer byte as the sign byte, so an extreme hit could jump the ball a whole page 🟡 ✅ FIXED (2026-09-21)

**Symptom class:** a ball that met the paddle well off-centre *while the paddle was moving
that way* could appear **256 px further along in a single frame** — a teleport to (or past)
the field's edge, then a wall bounce, with the ball otherwise unchanged. **Found by reading
the code**, not from a play report: the arithmetic is unambiguous, but no play test has been
confirmed to reach it.

**Location:** `src/tasks/dynamic/grknoid.inc`, the old `gkBallPStore`, reached only from
`gkBallPHit`'s clamp tail.

**Root cause.** `vx` is a 24-bit two's-complement value whose three bytes are the fraction,
the integer part and the **sign extension**, and `gkBallMove` adds all three into the ball's
x (`BXF` / `BXL` / `BXH`, where `BXH` is the 256-px byte):

```asm
    lda $18; clc; adc $1d; sta $18      // BXF += VXF
    lda $19;      adc $1e; sta $19      // BXL += VXL
    lda $1a;      adc $1f; sta $1a      // BXH += VXH
```

The bounce built its velocity in a 16-bit `GK_OFFL`/`GK_OFFH` pair and then stored it as

```asm
    lda GK_OFFL
    sta GK_VXF
    lda GK_OFFH
    sta GK_VXL
    sta GK_VXH          // "the 16-bit result sign-extends into the third byte"
```

which copies the **integer byte** into the sign byte. The comment's claim is accidentally
true for every value under 1.0 px/frame, where `GK_OFFH` is `$00`/`$FF` — but the angle
spans `+-27 x 8 = +-216` and the english adds `+-128`, so `|angle|` reaches **344**, i.e.
`GK_OFFH = $01` and `GK_VXH = $01`. `gkBallMove` then adds 1 to `BXH`: a whole page in one
frame. Reachable whenever a hit lands about **16 px or more off the paddle's centre with the
paddle moving into the ball**, which is the ordinary "slide the paddle under the ball"
motion.

**Fix (2026-09-21).** The bounce now adds the ball's arriving `vx` to the deflection (a
mirror plus a bend, so a centre hit is an exact mirror — see `gkBallPaddle` in
`src/tasks/dynamic/grknoid.inc`). The
sum can therefore reach 2.0 px/frame, and the sign byte is a real question on every path: it
is derived as the **sign of the sum's integer byte** — `$00` on the non-negative paths,
`$FF` on the negative one — and never copied. `GK_VXCAP` (2.0 px/frame) bounds the value the
collision test assumes, and the `$0060` magnitude floor moved onto the sum's own sign so a
hit that reversed the ball cannot be floored back through the paddle.

**Verified:** the emitted tail was read back instruction by instruction from
`tasks-out/grknoid.bin` (`$00` on the positive and `$FF` on the negative store paths), and
the model — mirror, cap, floor, sign byte and the integrator's step — was simulated against
that sequence. `grknoid` is 2108 bytes of code, 194 fixups, still 9 pool pages.

---

## Bug #123 — the idle auto-return was attached to the name entry too, so a screen whose *exit* commits data could abandon a half-typed name 🟡 ✅ FIXED (2026-09-21)

**Symptom class:** a player whose score qualified reached the name entry and was still typing
— or had stepped away — when the screen left on its own. Whatever had been typed was stored as
the name, the rest of the field was space-filled, and only then was the row written to
`grknoid.hi`; with nothing typed at all, the row landed in the file with a blank name.
**Reported from play**, as a rule rather than a symptom: "if one reaches the high score entry
when the game ends it should stay there and not move onto other screens".

**Location:** `src/tasks/dynamic/grknoid.inc` — `gkHiNameScreen`, and the `gkHnIdle` block it
branched to.

**Root cause.** `GK_RETURN_DELAY` was introduced as an idle auto-return "for shell screens" and
applied to **both** of them, which is the wrong generalisation. The game-over screen is a
MESSAGE: nothing is committed by leaving it, so a timeout there is harmless and merely hands
the attract loop back. The name entry is a DATA-ENTRY step, and its exit is what commits the
data — `gkHnDone` stores `gkHiNameBuf` and space-fills the remainder — while the save
(`gkHiSave`) sits *after* the screen, in `gkOverLeave`. A countdown there therefore had to
invent a name on the player's behalf, and it also decided whether the row was ever written to
the REU at all. The screen's own `gkKeysArmed` gate did not help: a player who pauses to think
is exactly someone whose keyboard is idle.

**Fix (2026-09-21).** The entry holds no countdown. Its `gkTickSet` on entry and its
`gkHnIdle` tick are gone, and the loop's no-key arm is `beq gkHnLoop` again, so the only ways
out are RETURN and `q`. `gkTick16` now has exactly **two** callers (the attract loop's tick and
the game-over frame) and `gkTickSet` two sites. Both `GK_RETURN_DELAY` and `gkHiNameScreen`
carry the constraint with its reason, so the countdown is not "helpfully" restored: **a screen
whose exit commits data waits for the player — only a screen that merely displays something
may time out.**

**Verified:** from `tasks-out/grknoid.bin`, `jsr gkTick16` appears at exactly two addresses —
`$59F6` (the attract tick) and `$5B07` (the game-over frame) — and the name entry's code
contains no countdown call at all.

**Consequence to know:** `q` at the name entry (the exit every task must have) leaves the new
row unsaved — it is in RAM, not in `grknoid.hi`, because the save sits after this screen. An
explicit quit abandoning work is acceptable; saving would have to be done *inside* the quit
path, which belongs to `gkLoop` and not to this screen.

---

## Bug #124 — a `lda #0` between the load and the `asl` doubled zero, so the ball launched horizontally 🔴 ✅ FIXED (2026-09-21)

**Symptom:** the served ball travelled **horizontally to the right** and never rose. **Reported
from play** — "now when the game starts the ball moves horizontally to the right" — and not
latent: the launch ramp had just replaced the fixed launch immediates with a derived velocity.

**Location:** `src/tasks/dynamic/grknoid.inc`, `gkLaunch`, the register lifetime between the
table load and the `sta GK_VYF`.

**Root cause.** `gkLaunch` derives `vy` from the level's `vx` by doubling it (the carry is
`|vy|`'s integer byte) and negating the result with `gkBallMirrorY`. The sequence as emitted
was

```asm
    lda gkVxTab-1,y     ; A = this level's vx
    sta GK_VXF
    lda #0              ; the vx bytes -- and it DESTROYS the fraction in A
    sta GK_VXL
    sta GK_VXH
    asl                 ; C:A = 2 x 0 = 0
    sta GK_VYF          ; so |vy| = 0
    lda #0
    rol
    sta GK_VYI
```

`|vy|` was therefore 0, the mirror negated zero, and the ball was left with its correct `vx`
and **no `vy` at all** — a horizontal serve. The `lda #0` is not wrong in itself; the register
lifetime between it and the `asl` is.

**Fix (2026-09-21).** Every use of A is now the instruction after its own load: the level's `vx`
is loaded a second time for the `sta GK_VXF`, with a WARNING at that line saying why it is
loaded twice, so the second load is not "optimised" away as redundant. +3 bytes, no page cost
(4004 -> 4007, still 16 pool pages).

**Why the verification missed it — the part worth keeping.** The checkpoint claimed the speeds
had been "re-simulated from those bytes through the emitted `gkLaunch`", but the simulation was
of the code's **intent** (`vx -> asl -> vy`). An intent model has no registers, so a value
destroyed between two instructions is invisible to it, and the disassembly that *would* have
shown it (`5357: lda #$00` immediately before `535d: asl`) was printed and read as the sequence
that was meant to be written. The rule: **verify a derived value by EXECUTING the emitted
instructions, not by re-deriving the author's arithmetic** — a register-lifetime slip is
exactly the class that survives an intent model. The replacement verifier decodes `gkLaunch`
plus the `gkBallMirrorY` it jumps into out of the image and runs them in a small register model
for each level (21 instructions a level), checking that every level launches **upwards** at
|vy|/|vx| = 2 — level 1 `+0.375 / -0.75` (`00 00 60` / `ff 40`) through level 5 `+0.75 / -1.5`
(`00 00 c0` / `fe 80`) — and failing loudly on a horizontal or downward launch.

---

## Bug #125 — a "drain the keyboard" loop stopped at the first key RELEASE, so stale presses left in the queue answered the next "press a key" screen 🟡 ✅ FIXED (2026-09-22)

**Symptom:** the game-over screen left for the attract loop on its **first frame**, so its tune
(`firstdt`) never played — and with the attract tune being what the attract loop requests, the
music appeared to switch to katmand instantly. **Reported from play**: "when the game ends the
game over music does not play it immediately switches to the attract screen. are you actually
clearing the keyboard buffer?" The guess in the report was the whole diagnosis.

**Location:** `src/tasks/dynamic/grknoid.inc` — `gkDrainKeys`, and the two screens that gate a
key answer on it (`gkKeysArmed`, called from `gkAtFrame` and `gkOverFrame`).

**Root cause — the kernel API cannot say "empty".** `kbReleased` (`src/kernel/keyboard.inc`)
pushes **`$ff` into the key queue for a key-UP**, and `kScanKeyboard` returns **`$ff` for both**
an empty queue *and* a popped release byte; only then does `kReadKey` fold it to 0. So
"`kReadKey` returned 0" means *empty or release*, and the drain — a bounded loop that left on
that 0 — stopped at the **first release event** and left every **newer** press behind it.

Nothing drained the queue while a game was running, because the game reads `keybHeld` (a
LEVEL, not the queue) and `gkKeysArmed` runs only on the attract and game-over screens. So by
game over the queue held a full 16 mixed events — holds, repeats and releases interleaved from
the player steering with `,`/`.` — and the drain cleared only up to the first release. The
latch (`gkKeysArmed`) then armed on the first frame with nothing held and read one of the
**stale presses** as an answer, which ran `gkOverLeave` immediately: the hi-score check, the
attract loop and `gkMusSet(ATTRACT)` → `kSidHush`, cutting the game-over tune off before it had
loaded. The latch was doing its job — it was the thing that made the leftover presses readable
rather than being discarded.

This is also why the earlier fix (#123's sibling, `docs/bugs.md` and the `KEYS ARMED` note) did
not cover it: the latch handles a key that is *still down*, and the leftover events are from a
key that has already come **up**.

**Fix (2026-09-22).** `gkDrainKeys` now reads the queue's **full capacity unconditionally** —
16 reads, no early exit: the queue holds at most 16 (the IRQ refuses a 17th) and **every read
consumes what it pops, releases included**, so 16 reads always empty whatever was in it. It
costs 2 bytes *less* than the broken form (~400 cycles, once a frame). Emitted and byte-checked
in `tasks-out/grknoid.bin`: `a9 10 / 8d 4d 60 / 20 18 e0 / ce 4d 60 / d0 f8 / 60` — no `beq`.

**Lesson (transferable).** A queue that reports "empty" with the same value it uses for a
*sentinel entry* cannot be drained by testing that value. The bound must come from the queue's
**capacity**, and every read must be unconditional. Corollary: while a task reads an input
*level* rather than the event queue, nothing drains the queue, so the first "read a key" screen
afterwards sees everything that happened during the whole session — drain it completely, or a
stale event answers the prompt.

---

## Bug #126 — a tune buffer allocated and freed per mode change made the largest tune the first the pool refused 🟡 ✅ FIXED (2026-09-22)

**Symptom (reported from play):** the attract, game and game-over tunes all played at first, but
after about **two games** the attract tune stopped loading and the game ran silent — the game
itself unaffected, and the two shorter tunes still playing. Reported as a suspected malloc/free
cycle, and that is what it was.

**Location:** `src/tasks/dynamic/grknoid.inc` — `gkMusLoad`/`gkMusFree` (now `gkMusInit` /
`gkMusBufFree`), the child's LOAD -> PLAY -> FREE loop.

**Root cause — a variable-size pool request on every mode change.** The music child took a
fresh `kMalloc` for each tune it loaded and handed it back when the player left it: `katmand`
4584 B = **18 pages**, `immig` 932 B = 4, `firstdt` 382 B = 2. `allocFromPool` needs a
**contiguous** run and the pool is shared with every other task, so a cycle whose largest
request is 4.5x the next one returns its pages in runs that the other sizes — and the other
tasks — cut up. The failure is then **in size order**: the 18-page request is the first to find
no run long enough, while the 4- and 2-page requests still fit. That is exactly the state the
pool notes warn about ("a correct-looking total beside a failing load means the free pages are
not in one run"), and `kMalloc`'s two documented failure modes (`C=1` on no contiguous run, or
on the per-task `MAX_MALLOC_PAGES` cap) both bite a *large* request first.

**Fix.** One buffer for the session. `gkMusInit` takes `GK_MUS_PAGES` (18) pages **once** — now
the task's only `kMalloc` — and each tune is `kReuLoad`ed into it, so a mode change makes **no
pool request at all** and the allocator sees one block per session. `gkMusLoad` refuses a tune
longer than the buffer instead of loading past its end (a bigger tune means raising
`GK_MUS_PAGES`, and the guard turns that mistake into silence rather than corruption).
`gkMusCur` is set only after a load *succeeds* — the old code set it *before* the load, so a
failed load claimed a tune the buffer did not hold and the child then played the previous
tune's bytes. `gkMusicBye` frees the block on quit **after** `gkMusReap`, i.e. once the child
that reads those bytes is gone; `sysKill`'s cascade does free a thread child's blocks (Bug #79),
but the free is explicit because a task that takes 18 pool pages should say where they go back.

**Not measured:** the `pool` comparison that would have shown the fragmentation directly (fresh
boot vs after game 2 — a `malloc` row appears, holding the tune's pages) was not taken; the
one-buffer design removes the churn the symptom is attributed to either way.

**Verified:** build green (`Made 32 asserts, 0 failed`), grknoid 4539 bytes of code / 534
fixups, relocation verification OK, all 42 built binaries byte-identical in the rebuilt REU
image. Emitted bytes checked in `tasks-out/grknoid.bin`: `lda #$12` (18 pages) at exactly one
site, `cmp #$13` (19 pages) at exactly one site, and the load path's three refusals all branch
to the same `rts` (`bcs` at file `$EAF`/`$EBC`/`$EC0` -> `$EDB`).

**Lesson (transferable).** A pool request whose *size* varies on a repeating trigger is a
fragmenter, and it fails in size order — the largest size goes first, while the totals still
look healthy. Take the biggest block once, keep it for the session, and size it up by raising a
constant rather than by re-requesting.

---

## Bug #127 — the wall loop's bound was EXCLUSIVE, so the side walls stopped a row short ✅ FIXED (2026-09-22)

**Symptom (reported from play):** with the field's floor moved down to the screen's last row, the
last cell row of the side walls fell out of the box — the cells at (0,23) and (39,23) were blank,
so the field had a notch above its bottom corners. Reported as "you left the sides of the row
before the last blank".

**Location:** `src/tasks/dynamic/grknoid.inc` — `gkWalls`'s side-column loop.

**Root cause — `GK_WALL_LAST` is an INDEX, and the loop's bound is EXCLUSIVE.** The loop
increments before it tests:

    ldy gkBROW
    iny
    sty gkBROW
    cpy #GK_WALL_LAST           // 23
    bcc gkWRow

so the value compared is the NEXT row to draw, and `bcc` against 23 stops once the next row would
be 23 — after row 22 has been drawn. The loop therefore drew rows 1..22 while the comment on
`GK_WALL_LAST` (`rows 1..23`) and the `gkWalls` header both claimed 23 was the last row drawn.
The same `cpx #CONST / bcc` idiom a few lines above is INCLUSIVE, because there the tested value is
the one just drawn and the constant is a COUNT (`cpx #GK_COLS`, 40, last column drawn 39): the two
loops differ only in whether the constant names an index or a count.

**Why it hid for so long.** The floor (the death line) and the paddle both sat on row 22 at the
time (`GK_DEAD_ROW = 22`, `GK_PAD_Y = 226`), so walls ending at 22 met the floor exactly and the
missing row was below the field, off the bottom of the drawn box. Moving the floor to row 24 left
the walls where they were and opened the gap.

**Fix.** `cpy #GK_WALL_LAST+1`, with the bound's meaning written into the loop's comment ("one past
the last row to draw"). No bytes and no fixups change — the constant is the only edit.

**Verified:** build green (`Made 32 asserts, 0 failed`), grknoid 4558 bytes of code / 534 fixups;
the emitted bytes carry `cpy #$18` (24) at exactly two sites — this loop and `cpy #GK_LEVEL_BYTES` —
and `cpy #$17` (23) at none; REU reseeded, all 42 built binaries byte-identical in the image.

**Lesson (transferable).** An increment-then-test loop EXCLUDES its bound, so a constant that names
the LAST index needs `+1` written at the compare — while the same idiom against a COUNT does not.
Say which one it is in the comment: a comment that claims an inclusive range the code does not
produce is a defect review cannot see, because the code matches its own shape and only the prose
is wrong.

---

## Bug #128 — a declared machine speed changed what the games DO, not just how many frames it took ✅ FIXED (2026-09-23)

**Rule (stated by the user):** `speed` exists so the games behave **identically** at 100%, 200% and
any other declared speed. The declaration may change how many frames a duration is spread over; it
may never change what happens. Four sites broke it.

**Location:** `src/tasks/dynamic/grknoid.inc` and `src/tasks/dynamic/gortris.inc`.

**1. grknoid's BALL skipped on a different rule from the paddle.** `gkBallStill` took the same fixed
2-in-3 skip for *any* positive shift, while `gkStill` (the paddle) used `2^shift - 1` phase bits, so
at 200% the ball ran ~1.33x real time against the paddle's exact rate and the two diverged further
at 400%/800%. **Fix:** the ball calls `gkStill` — literally the same frames — and `gkBallStill` is
deleted (20 bytes smaller). Sharing the rule is also what keeps a bounce in the 100% order rather
than resolving two motion steps apart.

**2. grknoid's accelerator counted DISPLAYED frames.** `jsr gkPadAccel` ran *before* the `gkStill`
gate, so at 200% a gear arrived in half the real time (0.16 s against 0.32 s) — and the same applied
to the release/reversal reset. **Fix:** the call moved after the gate, so a gear is bought with the
frames the paddle actually moves on, which run at the same 50/s at any declared speed.

**3. grknoid's two 1016-frame countdowns were never shifted.** They are the attract screen's hold
and the game-over screen's fallback, both in DISPLAYED frames, so at 200% each lasted half the real
time — and the fallback could therefore leave the game-over screen while `firstdt` (now real time
through `sid.lib`) was still playing. **Fix:** `gkTickSet` shifts the 16-bit count left by
`sysSpeedShift` (the shape of gortris's `gtPace16`), so a screen's hold is the same REAL length
everywhere. Sixteen bits cannot overflow at any speed `speed` accepts (1016 x 8 = 8128), and the
count is never shifted down to zero.

**4. gortris's `gtWaitFrames` was the one frame-counted wait not routed through `gtPaceN`.** The
line-clear flash (3 passes of 2 x 6 frames) therefore lasted half the real time at 200%. **Fix:**
`gtWaitFrames` scales its own count, so every caller states it in the game's own frames.

**Verified:** build green (33 asserts, 0 failed), relocation verification OK. In
`tasks-out/grknoid.bin`: exactly ONE `ldx sysSpeedShift` (`gkStill`) and one `ldy` (`gkTickSet`) —
the ball has no speed read of its own — the old 2-in-3 loop is absent, and the emitted `gkTickSet`
decodes as `ldy $e531 / beq / bmi / asl $60d1 / rol $60d2 / dey / bne` on the left and
`lsr $60d2 / ror $60d1 / lda / ora / bne / iny / bne` with an `inc` floor on the right. Both REU
images rebuilt (`REU.bin` at 200%, `REU-C64U.bin` at 100%) with every built binary byte-identical in
each.

**Watch (not fixed, deliberately):** the declared speed is read LIVE by grknoid (`gkStill`,
`gkTickSet`) and by gortris (`gtPaceN` at every load), but `godzi` computes its two scalars ONCE in
`gzPaceInit`, so a `speed` run while `godzi` is up leaves its walk step and pose cadence at the old
declaration until it restarts; and gortris's game-over *fill* is still one row per frame (a 0.2 s
animation at 200% against 0.4 s at 100%), because that one is a frame walk rather than a duration.

**Lesson (transferable).** A declared machine speed is a *configuration*, so anything a player can
perceive as a duration must be expressed in the machine's own frames and scaled in exactly ONE
place — a count that is shifted, a rate that is shifted, or a frame skip applied to every mover
identically. A duration counted per *displayed* frame, while the frames themselves change rate, is a
behaviour difference that reads as a bug in the game rather than a bug in the pacing.

---

## Bug #129 — a flat in a `.mus` file read the byte past the accidental table, so it played a semitone sharp at 200% 🟡 ✅ FIXED (2026-09-23)

**Symptom (latent -- no bundled tune triggers it):** a note written as a FLAT did not play a
semitone below the natural. At 100% it played as the natural, and at a declared 200% it played a
semitone SHARP.

**Location:** `src/libs/sidplay.inc` -- `DataAccidentalModifier`, and the note path's lookup in
`DecodeCommand`.

**Root cause -- a three-entry table read with a 0-3 index, where the byte past it is live state.**
The option byte's type bits (6-7) are `10` normal, `01` sharp, `11` flat
(`reference/sidplayer-mus-file-format.md`, and `tools/gen-mus.py`'s `ACC_*`), and the note path
shifts them down to 0-3 unchanged -- so the lookup index is 1 for sharp, 2 for normal and **3 for
flat**. The table held three bytes (the upstream's scratch byte plus `$01,$00`), and the very next
declaration in the file is `DataPitchOct`, which the wrapper writes with `kArg+2 + sysSpeedShift`.
A flat therefore read THAT byte as its modifier: 0 at 100% (a natural) and 1 at 200% (a semitone
sharp). The table's own comment ended `FF (-1) = Flat?` -- the entry was never there.

**Fix.** The table is now four explicit entries (`$00`, `$01` sharp, `$00` normal, `$FF` flat), so
the index the format defines is the index the table serves and the byte past its end is out of
reach. Emitted and byte-checked in `tasks-out/sidlib.bin`:
`fe fe 00 01 00 ff 00 00 ff 00 02 04` -- the `$FE,$FE` tail of `DataNoteLookup`, the four
accidental entries, then `DataPitchOct`/`DataPitchSemi` and `DataHalfStepsFromC`.

**Not tested musically:** none of the six bundled tunes uses a flat (decoded katmand, immig,
firstdt, commodo, fsonata, tetris: no flat notes, one sharp each in `commodo` and `firstdt`), so
this is verified structurally rather than by ear. Confirming it by ear needs a `.mus` with a flat.

**Lesson (transferable).** A look-up table whose index is *part of an encoded field* must be sized
for the whole field, not for the values someone happened to emit -- and a table whose last entry
abuts live state turns a missing entry into a **silent, configuration-dependent** wrong note,
because the byte it reads looks like a valid modifier. Check the field's full range against the
table's length, and check what follows the table.

---

## Bug #130 — a tune played 0.646 semitones FLAT on PAL, because the player's note table is the archive's NTSC one 🟡 ✅ FIXED (2026-09-23)

**Symptom:** on a PAL machine — the **C64 Ultimate** image (`reu/REU-C64U.bin`, `speed 100`) — every
note of every tune was **0.646 semitones low** (3.8%). Nothing else was wrong: the tempo, the
transposes and the machine-speed octave compensation were all correct, and the same tune was in
tune on an NTSC machine.

**Location:** `src/libs/sidplay.inc` — the frequency look-up in `DecodeCommand`, and the new
`sidDetectVideo`; called from `src/libs/sid.asm` before `sidSongInit`.

**Root cause — a physical constant of the machine, not a bug in the player.** A SID voice's output
frequency is the 16-bit register value scaled by the clock feeding the chip
(`f = value × clock / 16777216`), and **that clock follows the video standard**: 985248 Hz PAL
against 1022727 Hz NTSC. The archive's table is the **NTSC** one — `$861E` is C7 on a 1022727 Hz
clock to the last bit — so the same register value sounds 3.8% flat, 0.646 semitones, on PAL. The
ratio is 1.03804, and the player played the values as shipped.

**Fix.** A PAL copy of the twelve notes (`DataNoteFreqLoPal`/`DataNoteFreqHiPal`), each value
scaled by `NTSC_CLOCK / PAL_CLOCK` and stored **one octave down**, plus a selector the note path
reads per note: `DataVideoStd` (`$FF` = not measured, `0` = NTSC, `1` = PAL). `sidDetectVideo`
measures the machine from the VIC once per library load, because the KERNAL's `$02A6` is not a
video flag at all — it is that ROM's *RS232 baud-table selector*, filled in only when RS232 is
opened, so on a machine that never did it holds whatever RAM held at reset (`0`, the "NTSC"
answer, on VICE). PAL wraps at **312 raster lines** and NTSC at 263, and lines 263-311 are visible
only through `$d011`'s ninth bit — which is also set for the **256-262 lower border that both
standards reach**, so bit 7 alone is not the answer and the poll also checks `$d012`'s low byte.
The poll is bounded by a PAL frame's worth of iterations (~20 ms, once), and a machine that never
shows a line past 262 answers NTSC.

**Why the table is an octave down, and the trap it avoids.** Scaling B7 up needs `$106D0`, past
the 16-bit frequency register. The first cut clamped that one entry at `$FFFF` — and the octave
halving **divides whatever is in the table**, so a clamped entry comes out 45-52 cents flat in
*every* octave, not just the top one: one **sour pitch class**, which a listener hears as "out of
tune", where the previous uniformly-flat tuning had been in tune with itself. Stored an octave
down, every entry fits with no clamp in the data at all: the note path halves a PAL note **one
time fewer** than its octave field says and **doubles** an octave-7 note, which is exact for all
twelve notes except B at octave 7, where the doubling meets the clamp — **45.5 cents flat on that
one note, against 64.7 on all twelve with no compensation at all**.

**Verified in `tasks-out/sidlib.bin`, then by ear.** Both tables and the whole detection routine are
byte-checked: the two table pairs (PAL `$459C` is C6 — the octave-down shift), the note path's
`beq` to the doubling block and its `jmp` back to `applyDetuning`, `lda $d011` / `bmi` / the
low-byte compare, and `sta DataVideoStd` on both the NTSC and PAL exits. Three asserts hold the
invariants — each table is an entry per note with Lo under Hi, and `DataVideoStd` lies **outside
the work window `sidInitSong` clears** (so a song restart does not re-measure). `sidlib` grew
3210 → **3318 bytes / 483 fixups**, still 13 pool pages. It is the octave-down table that makes a
PAL machine play in tune: the clamp-in-the-table version was reported as *out of tune*, the
shipped one as *right* — which is what a uniform error and a single sour pitch class sound like
respectively.

⚠️ **The rest branch had to become an invert-and-jump.** `beq ProcessNoteSkipAhead` was near its
±128-byte limit, and the octave fix sits in its span: it is now `bne pnNotRest` / `jmp
ProcessNoteSkipAhead`, which is the form the instructions allow when the code cannot be
rearranged. A is zero on that path either way, so the target sees what it saw before.

⚠️ **The compare is written as `263 - 256`, and that is not decoration.** An 8-bit immediate is
silently truncated (`cmp #263` assembles as `cmp #$07`), which here happens to be *correct* — bit 7
already established the line is 256 or above, so the low byte is the discriminator — but it is
correct by accident of where the border sits. The named constant `sddPalLo = 263 - 256` states the
arithmetic deliberately, the way `gortris`'s two-byte attract delay does.

**Lesson (transferable).** When a value is *scaled by a clock*, check which clock — and "which
machine is this" is a question the hardware answers, not the ROM. A KERNAL byte that looks like a
flag is often a different subsystem's leftover, and a *border* region shared by both standards means
the ninth bit of the raster is a hint rather than a test. And **never clamp a value that is later
shifted**: the octave halving multiplies whatever error the table carries, so a clamp meant for one
unreachable note came out as a sour pitch class in all of them — uniformly flat is *tuned*, one flat
note is not.

---

## Bug #131 — a custom glyph could not be written at a fixed cell, so the last row had to be drawn with a FONT glyph 🟡 ✅ FIXED (2026-09-23)

**Symptom:** no task could draw one of its own 8-byte glyphs into the **last cell of the last row**
of a scroll region. `kPrintGlyph` writes at the cursor and then advances, and the advance wraps:
`wrapClamp` sees `cursorRow > taskScrollBottom` and **scrolls the whole surface while that cell is
being written**, so the top row is lost with no way to re-position afterwards (Rule 2). The one
routine that can reach a fixed cell, `kPrintCharAt`, takes a screen code — a FONT glyph.

**Location:** `src/kernel/gfx.inc` (`printCharAt`'s tail), `src/kernel/kernel.asm` (the new routine
and its entry), `src/kernel/jumptable.inc`, `src/main.asm` (address and fit asserts), and the callers
below.

**What the gap cost, before the fix.** It had already bent three task layouts, and every workaround
is visible in the source:

- `grknoid` stopped its side walls at row 23 and closed row 24's two corners with `kBlitChar` — which
  is why `GK_FRAME_BLOCK` existed as a screen code, and why **every font in `charsets/` had to carry
  the same solid block at `$a0`** so the corners matched the walls.
- `grknoid`'s attract frame drew its bottom edge the same way instead of with the glyph path every
  other edge uses.
- `gortris`'s attract frame ends at row 23 (`GT_FRAME_LAST`) for the same reason.
- Every caller that wanted a glyph at a given cell hand-rolled `kSetCursor` + `kPrintGlyph` and moved
  the cursor as a side effect (`gkGlyphAt`, `gtBlitNow`, `maze`).

**Fix.** `kPrintGlyphAt` (`$E0F3`, routine `printGlyphAt`): `srcPtr` = 8 glyph bytes, `X` = col,
`Y` = row; it writes ONE cell — glyph plus matrix nibble, the framebuffer when the task's surface is
displayed and its own REU slot when not — and advances, wraps and scrolls **nothing**, so `(39,24)`
is an ordinary cell. It mirrors a space (`$20`) into the char shadow, `kPrintGlyph`'s read-back
convention, and preserves the cursor.

It is **72 B in two free pockets** (`$1844-$186B`, `$CA6B-$CA8A`), not in the `$ECDC-$FC3E` chain: a
routine reached by a jump-table `jmp` may live in any free run. It does **not** copy the
matrix-nibble block — `printCharAt` gained a `paNibble` label at the head of its tail and the new
routine exits through `paNibble`/`paDone`, so that block stays a single copy; a fourth copy would
have needed ~44 B the band does not have. The entry took one of the two spare jump-table slots
(`$E0F6-$E0F8` is now the last). The kernel's free total moved **275 B -> 201 B** across the same
25 pockets, and the gate (`tools/verify-map.ps1`) pins the new figures.

**grknoid now draws every cell with it.** `gkGlyphAt` is `srcPtr` + `jmp kPrintGlyphAt` with no
cursor move; the side walls run to row 24 with the corners drawn like any other wall cell; the two
`kBlitChar` corner blits and `GK_FRAME_BLOCK` are gone; the attract frame's bottom edge is a plain
full-row call. The pixels are unchanged (the font block was byte-identical to the wall glyph) and the
game shrank 44 B.

**`gortris` too**, at both of its cell blits (`gtBlitNow` and the line-clear flash): the cursor is
no longer moved, and `GT_FRAME_LAST` moved 23 -> **24**, so the screens' frame closes on the
screen's last row instead of stopping a row short. 6 B smaller.

⚠️ **`maze` and `banner` deliberately keep the advancing path.** Both draw a *run* of glyphs whose
next cell is the cursor's business -- `maze` walks its own maze and wants the wrap-and-scroll, and
`banner` walks a banner's row. Converting them would mean carrying the walk in a counter, which is
the caller's design decision to make, not a defect.

**Verified:** 226 kernel asserts green, `tools/verify-map.ps1` green with the updated expectations,
and both REU images reseeded with all 43 built binaries byte-identical inside them.

**Lesson (transferable).** Two writers that cross on *position* but not on *source* leave a hole
exactly where a caller needs neither one's whole contract. When the only obstacle to a fixed cell is
the advance a routine performs *after* the write, the fix belongs in a sibling that does not advance
— not in every caller, and not in a workaround that changes what the program looks like.

---

## Bug #132 — the editor's status bar blanked on every cursor move 🟠 ✅ FIXED (2026-09-23)

**Symptom:** the `l:`/`c:` section of `edit`'s status bar visibly rebuilt itself — a blank solid bar
first, then the digits — on most cursor moves. Reported from the machine as "I still see the l/c
section while it is being drawn. it flickers."

**Location:** `src/tasks/dynamic/edit.inc` (`edBlitPad`, `edStatusGate`, `edDrawPos`, `edDrawSize`,
`edDrawMode`), `src/tasks/dynamic/edit.asm` (header data size).

**Root cause: a clear pass is an image.** `edDrawPos` cleared cols 21..12 before drawing, `edDrawSize`
cleared cols 31..26, `edDrawMode` cleared cols 22..24 — the first two only because `edBlitNum` emits a
variable number of digits. On a **reverse-video** bar the space `kBlitCharRev` writes is a solid
block, and cell row 24 is scanned once per frame (rasters 243-250). So the clear-then-redraw was not
an implementation detail the beam conveniently hid: it was two images, one of them an empty bar.
Sized from the source rather than estimated: a `kBlitCharRev` cell costs **~550-700 cycles**, because
`fontGlyph` performs a fresh SEI-wrapped 8-byte REU DMA plus 24-bit FS offset arithmetic for every
single character (the 8-byte framebuffer copy loop alone is 144 cycles). A cursor move drew 17-21
cells (`edMoveSync` -> `edDrawPos`, the only caller on that path) = **11-14 ms of a 19.95 ms frame**,
so the blank pass was on screen for most moves.

**Why a raster band could not fix it.** The first attempt waited for a `$D012` window: start below
raster 120 and the pass was assumed to reach row 24 too late to be caught, start at 251+ and the row
had already been scanned. Both bounds presuppose a pass that can fit a window, and a 17-21 cell pass
at ~650 cycles a cell cannot (0.6-0.7 frames) — so an "early" start still crossed row 24 and the
flicker survived. The gate carried two further defects: `$D012` is only 8 bits, so rasters 256-311
read as 0-55 and the "early" test conflated the bottom border with the top of the frame; and the
scheduler can preempt a pass, so its wall-clock span is not its CPU time. **No start time can hide a
pass that is a whole frame long.**

**Fix — never clear, then start at a frame edge.**

1. **`edBlitPad`** pads a field to a fixed end column with reverse spaces, so the fields are
   rewritten in full every time and no clear pass exists. `edDrawPos` now always writes exactly eleven
   cells (cols 11..21: `l:`, row in cols 13..15, ` c:`, col in cols 19..20, each padded, with col 21
   always a space so the field can never touch the `INS`/`OVR` indicator at col 22), `edDrawSize` six
   (cols 26..31, padded) and `edDrawMode` three (`INS`/`OVR` are exactly three cells).
2. **The `l:`/`c:` field covers the message area** — every message in this task is <= 10
   characters (`saved`, `no space`, `dir full`, `load fail`, `save fail`, `exists`), blitted at col 12
   — so overwriting all eleven cells erases a message exactly as the clear did; the bar's
   message-restore semantics are unchanged, not traded away for the padding. The prompt/message clear
   loops already write down to col 11 (their "39..12" comments were off by one), so the field's new
   leftmost cell is erased for them too.
3. **`edStatusGate` now waits for a `frameCount` change** instead of a raster band: the beam is then
   at raster 0, 243 rasters ahead of row 24, which covers the longest field the task rewrites. The
   gate is applied only to `edDrawPos`/`edDrawSize`/`edDrawMode` (the fields rewritten while editing);
   `edDrawStatus`, `edDrawName` and `edDrawHint` are un-gated on purpose — a full-bar rebuild is 77
   cells (2.5 frames) and no window contains it, so gating there would only add a frame of latency.

**Follow-up (same day).** The first layout ran the field 12..21, so a two-digit column landed on cols
20..21 — hard against `INS` at col 22, reported from the machine as *"the column number printing on
the status bar touches INS when > 9"*. The field moved one column left (11..21) so col 21 is always a
space. Lesson: a fixed-width field must reserve its **trailing** gap before a neighbouring field, or
the widest value abuts it.

**Verified:** 37 asserts green, relocation verified, `edit` **9180 B** (31 B *smaller* — the two clear
loops cost more than the pad helper), 483 fixups, both REU images reseeded with all 43 built binaries
byte-identical inside them.

**Lesson (transferable).** On a reverse-video surface, "clear then draw" is not a safe sequence to
hide from the raster — it is a second picture. Size a screen update in *cells* first (a cell is
typically ~600 cycles, and the glyph fetch dominates), then decide whether it needs timing at all;
and do not trust a duration you have not counted — the estimate that justified the first attempt was
3x low.

---

## Bug #133 — `kGetTaskName`'s thread-name DMA was the one unprotected REU transfer 🟡 ✅ FIXED (2026-09-23)

**Symptom:** none observed directly. Found by auditing every `$DF01` write in the kernel for an
enclosing SEI while investigating bug #37.

**Location:** `src/kernel/task-util.inc` (`getTaskName`).

**Root cause.** The thread branch of `getTaskName` sets up `$DF02-$DF0A` and triggers a 16-byte
REU -> C64 transfer with interrupts **enabled** — it is reachable from a task as `kGetTaskName` — and
the IRQ handler's own REU traffic (the per-switch kArg shadow / ZP shadow / cursor-state DMAs)
rewrites `$DF02-$DF0A`. Every other transfer in the kernel sits inside a held SEI; this was the only
one that did not. The window is narrow (16 bytes ~ 16 us) and only thread tasks
(`taskThreadNameReuHi != 0`) keep a name in the REU.

**Fix:** `php`/`sei` … `plp` around the setup + poll (3 B), the same shape as the `reu.inc`
primitives. The direct-pointer path is untouched — the SEI is taken after the branch that selects it.

**Verified:** 228 asserts green, `tools/verify-map.ps1` green, PRG starts `$01 08`.

---

## Issue C1: Shell Command Dispatch — Linear Chain ⚫

**Location:** `src/tasks/dynamic/shell.inc` — `processCommand`

12 sequential `strCmp` calls (~18 bytes each = ~216 bytes) could be replaced with a jump table keyed on the first character or a short hash. For a 6502 with limited code space, either approach is valid, but the current chain is the largest repetitive block in the project.

---

## Issue C2: Decimal Parsing Duplicated ⚫

**Location:** `src/tasks/dynamic/shell.inc`

The `×10 + digit` accumulator logic appears in both `findTaskArg` (decimal path) and `pcPrio` (`pcPrioValDig`). These could share a `parseDecimal` subroutine that returns the value in A and advances Y.

---

## Issue C3: Priority Display Could Use `kByteToDec` ⚫

**Location:** `src/tasks/dynamic/shell.inc` — `pcList` priority column

The priority display manually prints a 3-digit zero-padded decimal via repeated subtraction. The kernel provides `kByteToDec` which does the same conversion. The shell could call `kByteToDec` into a small buffer, then print leading zeros. The manual approach avoids null-termination overhead but duplicates ~30 bytes of conversion logic already present in the kernel.

---

## Issue C4: `kPrintTime` Could Use `kPrintAt` Internally ⚫

**Location:** `src/kernel.asm` — `printTime`; `src/tasks/clock.inc`

`kPrintTime` manually builds a `timeBuf` string and calls `kPrintString`. The clock task saves/restores cursor position around the call. If `kPrintTime` accepted an (X,Y) position and used `kPrintAt` internally, the clock task would simplify and `timeBuf` (16 bytes) could be eliminated.

> ✅ **Resolved 2026-08-28** — `kPrintTime` now takes `(X, Y)` and blits via
> `kBlitChar` (no cursor advance, no shadow). The clock task dropped its
> cursor save/restore dance and gates on `kSurfDisplayed`. `timeBuf` (now
> `timeLibBuf` in `time.lib`) is still used as the format buffer, but the
> cursor juggling is gone.

---

## Bug #145 — `kSetGfxColor` did not bound its selector, so `X >= 4` wrote the REU filesystem state and the sprite X registers 🟡 ✅ FIXED (2026-09-25)

**Status:** ✅ FIXED — found by audit, not by a failure. No caller in the tree passes `X >= 2`, so nothing reproduced it.

**Symptom:** none observed. The defect surfaced while auditing `kSetGfxColor` as a possible host for a per-cell colour write: the entry is documented as a register selector 0-3, so the allowed range had to be established before anything could be added to it.

**Root cause:** `gxSetGfxColor` tested `X` only to gate the two multicolour registers (`cmp #2` / `bcc gxGcSet`, then an MC test). Anything above 3 fell through into the same arithmetic, and **both** of the routine's writes then leave their block:

- the per-task store computes its offset as `selector*24 + curTask` from `taskGfxBorder` (`$4265`). The four colour arrays are 24 B each and end at `$42C4`, so `X = 4` stores to **`$42C5 + curTask`** — the 12-byte REU filesystem state block (`reuFsDirBase`/`DataBase`/`Capacity`/`TotalBanks`/`Enabled`) — and higher selectors walk further into it;
- the immediate write, taken when the caller is `kbFocus`, is `sta $D020,x`, so `X` 4-7 write **`$D024`-`$D027`**, the sprite 1-4 X registers.

A single mistaken selector could therefore move sprites 1-4 and corrupt the filesystem's directory base — and the second half only when the caller owns the display, which is what would have made it look intermittent.

**Fix:** the selector is bounded **before** the existing gate — `cmp #4` / `bcs gxGcDone` — so the `X < 4` path is the same logic as before (it rejects before `ldx curTask`, needing none of the routine's state). `X >= 4` is now a documented no-op in `docs/lib-gfx.md`, `docs/kernel.md` and `docs/library-call-inventory.md`.

**Cost:** 4 B. `gfx.lib` stays at **28 pages** (7092 B, 76 B free) — the rule that matters, because the pool is page-granular and `basic` is 81 pages with 106 B free of its own.

**Not changed:** the *colour* argument is still unbounded. It is not a corruption vector — the VIC uses the low nibble, and every consumer (`doGfxSwitch`, the text-blit path's `<bg0 << 4`) discards the high one — so masking it would be a behaviour change with no defect behind it.

**Same pass, stale comments:** `gfx.asm`'s header said "lib 2, 10 entries" with entry order "base 7, plot(7) ... setGfxColor(16)". The lib has **13 entries at call indices 12-24**, because `char`, `string` and `fillMatrix` were added after that comment was written. The same stale base had left three call-index comments wrong in `jumptable.inc`, `kernel.asm` and `docs/kernel.md`: `kFillMatrix` is call **24**, `kFpCall` **25**, `kStrcmp` **28** (from `libEntryCount` = 3, 9, 13, 1, 7, 2, 2, 4, confirmed by `docs/dynamic-libraries.md`). Comment-only — the kernel PRG's SHA-256 is unchanged by the sweep.

---

## Bug #144 — the montage's bags were sampled with masked random draws, which clustered: fifteen of twenty-five figures came out the same size 🟡 ✅ FIXED (2026-09-24)

**Symptom:** every figure in a run looked about the same size -- as the user put it, "a specific middle sized radius" -- even though the scale bag was weighted 4:2:1:1 toward the largest.

**Root cause:** the same stride-sampling effect as #139, now hitting the size. `kRandom` shifts left, so a masked draw on the montage's steady stride is a decimated window of the register; over twenty-five figures that produced 15 at the middle size and 4 at the largest, against a bag asking for 12 and 6.

**Fix:** the two bags are **walked with a counter** rather than sampled. The cursor is the figure count plus `SG_SPIN`, a random byte drawn once per run, so the ratios are exact (12 / 6 / 4 / 3 across the four sizes), every size appears, and the random phase keeps the order different between runs.

**Also:** zero extra halvings is now in the bag and is its commonest entry, so a third of the figures are drawn at the size the single-shape presets use -- filling the screen height -- instead of the whole range bunching around half size. Allowing 0 exposed a trap worth recording: `ldx #0` then `dex / bne` runs **256** times, so `sgMontFit` now tests for zero before the halving loop; without it every full-size figure would have collapsed to a point.

---

## Bug #143 — a figure overhanging the left edge drew horizontal lines on the far right, because `kLine` cannot clip a negative x 🟠 ✅ FIXED (2026-09-24)

**Symptom:** figures clipped at the screen edge drew long horizontal lines, in a row well away from the figure.

**Root cause:** `kLine`'s endpoints are **unsigned** 16-bit. `sgPoint` gives x a high byte, so a figure overhanging the left edge has points with a **negative** x ($FFxx). Two things then go wrong inside the library:
- the delta is computed unsigned, so a segment from $FFE9 to +5 measures about 65,000 -- the walk goes the long way round, holding SEI for the whole of it;
- `gxPix` reads the column high bit as "x >= 256". A negative x sets it, and when the low byte is below 64 it **plots at column 256+lo** instead of dropping the pixel (`pla / beq ... / cpx #64 / bcs` then `+256`); `gxPixReu` does the same. So each pass through the low bytes 0..63 paints a run of pixels along the right of that row.

Because the origins are uniform over the whole width, about one figure in seven overhangs an edge -- which is why it appeared specifically on clipped figures.

**Fix:** `sgSegment` **skips** the segment when either endpoint's x is negative, instead of handing it to a routine that cannot clip it. x >= 0 has a high byte of 0 and only a negative x has bit 7 set, so the test is exact. The point is still recorded as the previous point, so the trace resumes where the curve comes back on screen. The single-shape path has its centre at 160 with an excursion of at most 95, so its x is never negative -- presets `1`-`9` are untouched.

**Latent for every drawing task:** any task that hands `kLine` a negative x gets both the ~65,000-step walk and the right-hand streaks. `kLine` clips only at the positive end.

---

## Bug #142 — a preemption between loading `kArg` and calling `kLine` handed it garbage, drawing stray horizontal lines 🟠 ✅ FIXED (2026-09-24)

**Symptom:** the montage drew occasional long horizontal lines across the screen, every run.

**Root cause:** the live `kArg` block **is** shadowed per task — `scheduler.inc` DMAs all 8 bytes out on switch-out and back in on switch-in (`REU_KARG_BASE + slot x 8`) — but that shadow only protects it *across* a switch: it cannot protect a value held across a window an interrupt can enter. The interrupt-path writer was the **F-key display change**: `kbPoll` ran before `switchTask` in the same tick, and `switchView` -> `doGfxSwitch` stores `fromTask` in `kArg` and parameterises the surface swap through `kArg+0..+5` (**#156**, which defers the switch to the scheduler's switch path so the block is the task's own saved copy). **`kArg+4` is exactly `kLine`'s x2 low byte.** `sgSegment` loaded a segment's endpoints into `kArg`/`kResult` over about twenty cycles and then called `kLine`, so a timer tick landing in that window corrupted the segment's second x before `kLine` copied it out -- and `kLine`'s clipping trusts its inputs, so the result is a long near-horizontal stray line.

**Attribution note:** the mechanism was long recorded as "the tick's per-switch cursor sync writes `kArg+4`". It does not: that sync is a store to the framebuffer's matrix tail (`svSaveOnFocused` -> `GFX_MATRIX_BASE + GFX_CURSOR_ON`), `csrSyncTail` drives `$df02-$df0a` and the tail directly (its header used to list `kArg+4` among its clobbers, which it never wrote), and `surfDisplayed` keeps off the block by rule and says so. The per-switch REU work that *does* touch the block is `sprInstall`, which runs between the switch's save and its restore, where nothing a task can see is live.

**No sprite task is involved and `sprInstall` is not the source.** It returns at `cmp sprLiveOwner / beq sprInsDone`, and with no sprite task it does at most one DMA when the display changes hands -- not one per frame. An earlier reading of this bug wrongly blamed it.

`gortris`, `gfxdemo` and the other drawing tasks make the same unprotected call and get away with it because they draw in one short burst; the montage draws for about forty seconds over 12,800 segments, so it lands in the window on essentially every run.

**Fix:** `sgSegment` wraps the load and the call in `php`/`sei`/`plp`. `kLine` already holds SEI for its whole loop, so this only extends the IRQ-free window backwards by ~20 cycles. The documented rule is exactly this one: `kArg` may be used as scratch *within* one call, never held across a window an interrupt can enter.

**Also, as asked:** the montage's three colours now **alternate** (1, 2, 3, 1, ...) instead of being drawn per figure, so the spread is exactly even and no two figures in a row share a colour.

**Latent elsewhere:** the same unprotected `kArg`-then-call pattern is in `gfxdemo` and `gortris`. They have not shown it because their draws are short bursts, but the window is there.

---

## Bug #141 — a "rotation" that shifted theta and phi by the same angle distorted every montage figure, reading as horizontal streaks 🟠 ✅ FIXED (2026-09-24)

**Symptom:** the montage showed horizontal lines again and its figures did not look like spirographs.

**Root cause:** to stop every figure pointing the same way, each one was started with theta and phi at the same random angle. That is **not a rotation**: phi runs at P/Q times theta's rate, so advancing both by the same delta changes the phase *between* the two rotating terms -- and a hypotrochoid's shape depends entirely on that phase, being the sum of two circular motions that must start in step. The result is a different curve, one that can degenerate into a sweeping, streak-like figure. A hypotrochoid rotated by delta is `theta += delta` with `phi += delta*P/Q` -- equivalently, starting the drawing at step k, where `theta = k*Q<<m` and `phi = k*P<<m`. Advancing both by the same amount is never available.

**Fix:** the phase stays at zero, so the curve is the true hypotrochoid again (`sgTrace` starts theta and phi at zero; the montage no longer touches them). The random start angle is dropped rather than reimplemented -- it was never asked for, and doing it properly costs two 16-bit products per figure.

**Also, as asked:**
- The three palette colours are now three **independent** random draws in 1..15, re-drawn only if one repeats. They are still distinct, but no longer a fixed five apart, which is what made the palette predictable.
- The lobe count is random but **capped by the size**. The scale is drawn first (`1,1,1,1,1,1,1,2`, favouring the HIGHEST radii -- one halving is half the screen), and the shape then comes from a bag of the four lowest lobe counts (preset `1` draws 5 lobes, `8` 11, `6` 12, `5` 13) weighted toward the roundest. A halved figure skips the bag and takes the 5-lobe preset outright: eleven hoops at that radius are two or three pixels each and draw as a smear rather than a shape.

**Method note:** as with #139 and #140, the check was an offline Python model of the task's logic rendered to a PNG -- no emulator.

---

## Bug #140 — preset 10's origins came from a window narrowed by each figure's size, so every shape crowded the middle 🟡 ✅ FIXED (2026-09-24)

**Symptom:** the montage's shapes bunched toward the centre of the screen instead of being scattered over it -- and the bigger the figure, the tighter its window, so the largest figures could only land near the middle.

**Root cause:** `sgMontFit` chose each centre as `base + an offset within +/- (base - extent)`: the legal window for a figure that has to stay wholly on the bitmap. That is *uniform*, but it is uniform over a window that **shrinks as the figure grows**. With the largest figure the x centre could only reach 23..135 and the y centre 47..152. A flat draw from a small window still looks like a cluster, which is why the placement read as non-random however it was sampled.

**Fix:** both origins are now drawn uniformly over the screen:
- **x over the whole 160 columns.** A figure near an edge simply overhangs it -- x is a 16-bit coordinate in this task and `kLine` clips -- so nothing needs reserving for it.
- **y over the 200-2*extent rows the figure fits in.** y cannot overhang: `kLine` takes y as ONE byte, so a negative row would wrap to the *bottom* of the screen and draw a spurious strip there. Restricting y is the lesser evil, and since the figures are as tall as the screen the rows they cover still span its full height.
- Both reject against the bound, so the distribution over the legal range is flat.

---

## Bug #171 — the syscall jump table's last entry sat inside the sprite band, so a sprite task's art replaced the `SYS_SLEEP` handler address with two pixels 🔴 ✅ FIXED (2026-10-04)

**Symptom.** `run sprdemo`, `q`, then `run sidplay commodo` froze the whole machine — no further output, frame clock stopped. Neither half reproduced it: `sprdemo` alone, `sidplay` alone, and `sidplay` → `kill` → `sprdemo` were all fine, and `grknoid` (sprites **and** `sid.lib` **and** a tune) was fine. The original report was the mirror image: play a tune, switch to the shell, `run sprdemo` — sprdemo starts and a split second later everything stops. It looked like a sprdemo/sidplay interaction, and it is not one: it is a sprite-band/task-corruption fault that needs a specific pair of tasks to expose.

**Root cause.** `syscallTbl` is a `.word` table with `SYSCALL_COUNT = 14` entries — 28 bytes — and it stood at `$0EE7`, so it ran `$0EE7-$0F02`. **`$0F00-$0F3F` is sprite slot 5.** The band is 192 bytes of 64-byte-aligned frames, all eight slots are ordinary task sprites, and `sprdemo` is the only task in the tree that resolves sheets for **all eight** (`SPRD_BALLS = 8`), so it is the only thing that ever DMA's art into the high band. Its art bytes at `$0F01`/`$0F02` are the **low and high byte of the `SYS_SLEEP` handler address**. `sidplay`'s play loop is what calls `SYS_SLEEP`, so from the moment sprdemo's art landed, syscall 13 read `srcPtr` as `$FF`/`$00` and `jmp (srcPtr)` sent the CPU to `$00FF` — zero page.

**Evidence.** A store watchpoint on the port and an 8000-instruction window both came up empty, so the port was not written; an execute trap on `$0000-$080F` (nothing there is ever code) stopped at `PC=$00FF` and the six instructions before it are the dispatcher:

```
lda syscallTbl,y        ; A = $0D = 13 = SYS_SLEEP, Y = $1A
sta srcPtr              ; -> $FF   ... read from $0F01, inside slot 5
lda syscallTbl+1,y
sta srcPtr+1            ; -> $00   ... read from $0F02, inside slot 5
jmp (srcPtr)            ; -> $00FF
```

Everything after that is wreckage: zero page → garbage → the stack eaten one frame per pass (`SP` walked `$0B` → `$03` → `$FF`) → `BRK` → the KERNAL's handler, now reachable → its `jmp ($0314)` reading **slot 1's** art, which is `$FE $7F` → `$7FFE` → `$02` = **JAM**. That jam is what "the whole system hangs" actually was.

**Two red herrings, recorded so they are not re-run.** (1) The frozen machine reads `$01 = $37` with KERNAL vectors, which looks like the port being written; it is not — no instruction writes `$0000`/`$0001` anywhere in 22,000 instructions of run-up, and `$01 = $37` is the KERNAL having taken over *after* the jam (the reset also clears `$0200-$03FF`, which is why the art is gone from a post-mortem dump). (2) The library system was cleared by measurement, not argument: `libStartPage`/`libPages` show `filesys.lib` at `$63`+10 and `sid.lib` at `$56`+13, disjoint; `dlLoadCore`'s pool scan took its "found room" exit and never reached `dlEvict` (**no eviction**); and the binder was watched writing `kSidPlay -> $AD04`, `kSidHush -> $ADC2` — both inside `sid.lib`'s own range. A loop the CPU later wandered into inside `filesys.lib`'s pages was the *wreck*, not the route.

**Fix.** The band cannot move (it needs 192 bytes at a 64-byte boundary; `$0FC0` is `libNameMouse`'s only home and `$0FCC` is the core), and the table has no free run to move to (the largest free run above `$0801` is 15 bytes, and `$02A1-$02BF` cannot take a `.pc` block because it would become the PRG's lowest block). So 3 bytes were retired from the head band's NOP size pad in `init.inc` — between the `syscall:` label and the table, so `syscall`'s address may shift (the task ABI is `kSyscall = $E090`, a `jmp syscall` in the pinned jump table, which re-resolves) but nothing else's meaning changes. The table is now **`$0EE4-$0EFF`**, one byte below the band. `osName`/`osVersion` moved with it (`$0AD6`/`$0ADF` → `$0AD3`/`$0ADC`) and their two task-side alias asserts caught the shift, which is exactly what they exist for.

**The missing tripwire is the real lesson.** `main.asm` already asserted that *code* stays out of the band (`sysExit`, `spawnCore >= $0FC0`) and that the two slot bands do not overlap — but nothing asserted that the **table** clears it, and a table's collision is not a code-space collision: the bytes it loses are a *handler address*. The new assert is one line:

```asm
.assert "syscall: table ends below the sprite band", ((syscallTbl + 2*SYSCALL_COUNT) <= SPRITE_SLOT5) ? 1 : 0, 1
```

It fails the build the day a 15th syscall pushes the table over `$0F00`. The size pad in `init.inc` is what holds the table's address, so its length and `SYSCALL_COUNT x 2` move together.

**Verified.** 232 asserts, 0 failed; PRG header `01 08`; free total unchanged at 127 bytes across 22 pockets; `check-reu-fs`, `check-reu-current`, `verify-inventory-docs`, `verify-jumptable-docs` and `verify-map` all clean; and the exact repro on a freshly seeded image leaves the machine healthy (`$01 = $35`, frame clock advancing) instead of jammed.

---

## Bug #170 — a press on the hires band's empty chip columns took nothing and left the band up 🟡 ✅ FIXED (2026-10-03)

**Symptom.** Reported as "a left click anywhere not on the UI should always close the UI". In HIRES the four columns between the last tile and the two arrows show nothing at all, and a left press there did nothing visible and **left the band up** — so the press that was meant to dismiss the band was swallowed instead.

**Root cause.** The hit test maps the band's cells to entries by position and by nothing else: columns 34-37 are the four slot chips, and in hires the band file simply has no art there (`gpPickTakeChip` returns unless the mode is MC). The entry existed "so the hit test stays one map", which is true of the *arithmetic* and wrong about the *answer*: the press computed an entry, took it, and the taker had nothing to do — a band that will not close.

**Fix.** `gpPickSelChip` reads `GP_MODE` and, in hires, routes those columns to `gpPickSelOff`: the entry is dropped, the pointer's interior goes back to the ink, and the press is a press **off the UI** — which is what it looks like. MC is untouched: there the chips are drawn (a solid block in the slot's colour with a value-2 frame) and a press still selects both the pen's value and the assignment target. The rule is general: **an entry with nothing drawn on it is not an entry.**

---

## Bug #169 — a right press could be lost after pressing the MODE tile: the press scanned the filesystem, and the frame outlasted the pointer's sampling 🟠 ✅ FIXED (2026-10-03)

**Symptom.** Reported as "hit the MC icon to switch to multicolour then sometimes right click won't close" — the band sometimes stayed up, as if the right press had never happened.

**Root cause.** The pointer is sampled in **bursts**: `gpSample` does `GP_READS` reads at the top of each displayed frame (~5-7 ms of a 20 ms frame), and a press is only seen as an edge between one read and the next. Everything the frame does *after* the burst happens with no read at all, so a click that starts and ends inside that window is invisible — and the window is as long as the frame's work. That is why only some presses are lost, and only when they follow one particular pick: **the MODE tile was the only press that called `kReuOpen`**, a 512-slot directory scan (the band files sit on directory page 7, so ~2 KB of DMA and ~64 entry compares — measured from the REU image). It pushed that frame's work past the frame's own slack, widening the sampling gap to roughly double its normal length.

**Fix.** Both band files are resolved **at spawn**, into a record per mode (`GP_UI` / `GP_UI_MC`, selected by `GP_MODE` through `gpUiRecOfs`), and nothing re-resolves — not the MODE tile, not a step's mode re-tag. The press that switches the band's face is then the same length as any other, and the record follows the mode for free whenever it changes, a *load* included (which the single record never did). A missing band file for the mode in hand is still fatal at spawn; for the other mode it is not, and the MODE tile then refuses to switch rather than switch into a mode whose band it cannot draw.

**Transferable.** A press handler must not do a directory scan, a file write or any other long lookup: the pointer is only sampled in bursts, so the length of the work *is* the width of a hole a click can disappear into. Anything resolvable ahead of the gesture belongs at spawn — and the design already said so: this was the one path that had escaped it.

---

## Bug #168 — a step's own snapshot kept the UI band as part of the picture 🟠 ✅ FIXED (2026-10-03)

**Symptom.** With the line tool: click to place the anchor, then right-click to raise the band, then press an undo arrow. The dot came off as expected -- but a **redo** afterwards painted the palette and the tiles into the canvas's bottom two cell rows, and dismissing the band left them there. Undo on its own looked right, which is what hid it.

**Root cause.** A state is stored wrongly only when two things are true at once, and the anchor click supplies the first: `gpDot` sets `GP_U_DRAW` on **every** mark, and the line's anchor click is a mark that becomes no state (the `jsr gpUndoPush` waits for the second click) -- so the routine at the top of both step routines had something to store. The second is the band: its own two cell rows **are** the canvas's bottom two, and while it is up they hold the palette, not the picture. `gpUndoStore` reads the framebuffer, so the state that got written was the picture **with the band drawn over it** -- the dot came off because the step itself was correct, while the ring kept a poisoned state for the redo to find.

Every other action taken with the band up goes through `gpUndoPushBand`, which takes the band down for the snapshot for exactly this reason; the two step routines were the exception, because their band-safe handling (`gpUndoMove` drawing the band again) only covered what happens **after** a store.

**Fix.** A mark that is not a step is no longer stored at all: `gpDropMark` **shows `HEAD` again** instead of snapshotting the mark. The state `HEAD` names does not contain the mark -- that is what having run ahead *means* -- so the restore is exactly the canvas without it, the ring does not move, and the mark never reaches a state. It clears `GP_DRAW` with the mark (an anchor that has been taken off is not an anchor any more, so the next click starts a new one) and returns **C=1**, which both step routines stop on: one press does one thing (the mark) and the next press is the step. The band comes back at `gpUndoStepRts`, the step's shared tail, which covers the paths that end in no step as well.

The band's own opening calls it too (`gpPickOpen`, tested against `GP_TOOL_LINE`), so a line's waiting anchor does not outlive the palette: the arrows describe the **ring**, and the canvas is brought back to `HEAD` before the band reads it. That also removed the need to take the band off before the store, because the store is gone: the invariant is now *no store happens while the band is up* -- every `gpUndoPush` caller is a canvas action taken with the band down, and the three actions that run with the band up (CLEAR, an MC palette press, the MODE tile) keep going through `gpUndoPushBand`, which takes it down first. What still writes over the band's own rows is a restore, and the tail's re-show puts the band back over it.

**Transferable.** A routine that stores the *displayed* canvas must establish that the displayed rows are the canvas. But the better fix here was not to store the thing at all: a mark that no state holds is not a state, and *showing the current state again* drops it without inventing history. And a press that consumes itself should say so -- C=1 out of `gpDropMark` is what keeps the caller from also taking the step, which is the "one press, one thing" rule the ring is built on.

---

## Bug #167 — an undo removed every stroke: a flag store inserted above `gpUndoPayload` ate the state index in A 🟠 ✅ FIXED (2026-10-03)

**Symptom.** Reported as "drawing multiple freehand steps and doing an undo removes everything". One `u` after several strokes wiped the canvas rather than the last stroke.

**Root cause.** `gpUndoStore` takes the **state index in A** and hands it to `gpUndoPayload`, which walks the payload table with it. The mark-flag clear (`lda #0 / ldy #GP_U_DRAW / sta (dataPtr),y`) was added at the *top* of the routine, above that call, so it overwrote A with 0 on every push. Every state was therefore written to **state 0**, and every restore read a slot that had never been written -- so the ring handed back an empty canvas. The byte dump of the new instructions looked correct, which is what made it hard to see: the bytes matched the intent, and the intent was wrong.

**Fix.** The flag clear now runs **after** `jsr gpUndoPayload`, where A is already consumed. Verified in the built image as the call order `38 20 9f 6b a9 00 a0 a3 91 f1 20 fe 6b` -- `sec`, `jsr gpUndoPayload`, then the clear, then the extras block.

**Transferable.** Inserting into a routine is a change to its register contract. An 8-bit store of your own needs a fixed value in A, and a routine that takes its input in A cannot have one written above the call that reads it -- so re-read the input contract after any insertion, not just the output one.

---

## Bug #166 — the CLEAR tile left the video matrix at zero, so every cell was colour 0 and the live shape outline could not be seen 🟠 ✅ FIXED (2026-09-28)

**Symptom.** After using the **CLEAR** tile, the box and circle tools drew their committed
shapes normally but their **live drag outline was missing — or only partly there**, the
band appearing in some places and not others. It never happened before a clear.

**Root cause.** `kClearBitmap` zeroes the **video matrix** along with the pixels, and in
hires **a cell's colour *is* its matrix nibble**. A page whose matrix is `$00` is black,
and every pixel written into it is drawn in colour 0 — so the pixels are there and cannot
be seen. The **committed** shape still appeared because the commit's cell walk colours
every cell the shape enters (`kPlot` writes the nibble); but the **live outline** is
`kBox`/`kCircle` under the pixel op, which write **pixels and no matrix at all**. So the
band was invisible across the blank page and visible only in the cells a previous walk had
already coloured — a rectangle that looked *half drawn*.

That is why it looked like an outline bug rather than a clear bug: the clear was the only
thing that produced a zero matrix. A fresh canvas never has one — `gpCanvas` fills the
whole matrix with `(ink << 4) | bg0` at spawn, with the comment *"the cells start at the
ink, so a draw on a fresh canvas is visible"*. **The clear was missing that half**, and an
earlier attempt to work around it by resetting the ink to white could not have helped,
because the ink was never what was wrong.

**Fix.** `gpPickTakeClear` fills the matrix back to the ink's nibble after the clear —
the same three lines and the same byte `gpCanvas` uses — so a cleared page is again
indistinguishable from a fresh one.

**Transferable:** a clear has to restore the surface's **invariants**, not just blank the
pixels. Here the invariant is *"the cells are the ink"*, and the tool that broke it was a
kernel call whose documented job is to zero the pixels **and** the matrix. The tell is
which operations still work: **if the thing that commits is visible and the thing that
previews is not, look for a difference in what each writes** — the walk writes the matrix,
the preview does not.

---

## Bug #165 — the picker decided a press from the PREVIOUS frame's pointer sample, so a flick off the band took the tile again instead of dismissing it 🟠 ✅ FIXED (2026-09-28)

**Symptom.** After picking the box or the circle, the first press-and-drag on the canvas
showed **no live outline at all** — the shape only previewed from the *next* press, and
release-commit was unaffected once a drag had started.

**Root cause.** `gpPickBand` acted on a press using `GP_MSEL`, the band entry the **last**
frame had sampled, and `gpPickSel` — the only thing that writes it — was called **only on
frames with no press**. So the entry a press was judged against was always at least one
frame old, while `GP_POS` had already been moved to this frame's position by `gpSample`
earlier in the same frame.

That one frame is not small. The 1351 reports a signed delta of -32..+31 per read and
`gpSample` folds **eight** reads per frame, so the pointer can travel up to ~248 pixels
between two frames — across the band (16 pixels tall) and onto the canvas with room to
spare. The gesture that hits it is the shape tools' own: flick off the tile and press
where the box is to start, which is exactly the frame the pointer leaves the band on.

With the entry stale at the tile, `gpPickTake` took the **tile** again instead of falling
through to `gpPickAway`, so the band stayed **up** — and `gpDraw` returns on every frame
the band is up, because the band owns the left button. The drag the press was meant to
start was therefore never started: no anchor, no outline. The next press was judged
against a fresh entry, dismissed the band, and outlined normally.

The pencil hid the same defect: a re-taken pencil tile also left the canvas unpainted, but
a stroke is less obviously "wrong" than a missing rubber band, and the user's next moving
press painted as usual.

**Fix.** `gpPickBand` now calls `gpPickSel` **first** and then reads the presses, so the
entry a press acts on is the one under the pointer **now**. It is also one byte smaller
than the `jmp gpPickSel` it replaced, and it removes the same one-frame lag from the
pointer's interior preview, which is written by the same routine.

**Transferable:** a per-frame routine that consumes an **edge** must read the *other*
inputs the edge is interpreted against from the same sample as the edge itself. `GP_POS`
was current and `GP_MSEL` was one frame old, and the pair was used as if they described
one instant. Where a pointer can move a quarter of the screen in a frame, "one frame old"
is not a rounding error — it is a different place on the screen.

---

## Bug #164 — a pick that armed a one-shot tool, and a dismissing press that was also spent, so the overlay's first press after a pick never acted 🟠 ✅ FIXED (2026-09-28)

**Symptoms.** Two, reported together, both reading as "it takes two clicks": *(a)*
picking the **CLEAR** tile cleared nothing until a later press somewhere on the canvas;
*(b)* after picking a colour or a drawing tool, the **left press that dismissed the
overlay did not draw** — the stroke only began on the press *after* it.

**Root cause (a) — the clear was a TOOL, and a tool is a mode.** The picking press set
`GP_TOOL = GP_TOOL_CLEAR` and `gpShapeClear` did the blanking on a *later* press of the
left button. That read correctly only for as long as the overlay **closed on a pick**:
the next press was then certain to land on the canvas, near where the eye already was.
Once the band became a toggle that *stayed* up, the mode's second press landed on the
band instead — and while the band is up the band owns the left button — so the clear
waited for the dismiss as well. Three presses for one command, and the two that did
nothing looked like the pick had been ignored.

**Root cause (b) — the dismissing press was spent against the press that had picked.**
`gpPickAway` cleared the left bit of `GP_PREV` as the band went down, so that `gpDraw`
— which runs later in the same frame — could not read the picking press a second time as
a stroke. That is right exactly while the press that picks and the press that dismisses
are the **same** press. They stop being the same press the moment a pick leaves the band
up: the pick is spent on the entry, and the dismissal is a **fresh** press the user
means to draw with. Spending it made `gpDrawFresh`'s edge test and every shape's read it
as "already down", so the frame did nothing and the click was swallowed.

**Fix (a).** The clear is a **command**, run on the same press that picks it
(`gpPickTakeClear`): `kClearBitmap` blanks the canvas, the tool goes back to freehand so
the next press draws, and the palette is read back over the blanked rows — the band's
own pixels *are* part of what a clear blanks — so the band stays up with its palette
intact. `gpShapeClear` and `GP_TOOL_CLEAR`'s dispatch in `gpShape` went with it: a tool
number that can never be live there is dead code. The **ink is deliberately untouched**:
the clear does not own the colour, and a blank page is drawable in whatever ink was
picked.

**Fix (b).** Nothing is spent. A pick leaves the band up, and `gpDraw` returns on every
frame the band is up, so a pick still cannot mark the canvas; the press that takes the
band **down** is real and acts where it lands — the pencil's stroke, a shape's anchor or
first click, the flood fill. A left button held across a right-press dismissal stays
safe because every `GP_PREV` consumer tests the **edge**, and a button that is simply
still down is not one.

**Transferable:** decide whether a press **ends** something or **starts** something, and
never make it do both. Consuming a trigger reads correctly only while the trigger and
the action are the same press — and a modal overlay is precisely what moves them apart.
The corollary for a picker: a **one-shot action** (blank the canvas) is a command run on
the pick, not a mode armed by it. A mode needs something else to end it, and the moment
that something (the overlay closing) is gone, the mode's next press lands on the picker.

**Superseded in part (the flood fill only):** the dismissal press starts a stroke and a
shape's anchor, but it no longer fills — the fill reads its region from the canvas, so
acting on a press made while the band was up treated a click on the UI as an image click.
See **#183**.

---

## Bug #163 — three assembly traps in the circle's cell walk: an inverted loop test, a destroyed row, and a sign test that read the wrong byte 🟠 ✅ FIXED (2026-09-27)

The walk was proved exact offline first (`temp/gpcirc-model.py`, 3,328 centre/radius pairs,
MISSED 0, EXTRA 0) and **every bug was in the transcription of it**. Three, in the order
they were found:

**(1) The quadrant test was the wrong way round.** `cmp` sets the carry for `A >= mem`.
The loop loaded `A = x`, compared against `y` and branched on `bcs` -- testing `x >= y`,
where the quadrant runs while `x <= y`. At the first step `x = 0, y = R`, so it was false
for every radius and the walk exited at once, on every arc and every half. Nothing was
coloured, so the rim took the colour its cells already carried: *"the circle is not drawn
in that colour"*. `gxCircle` gets it right by comparing **`A = y` against `x`**.
**Copy the test, not just the arithmetic.**

**(2) The row was destroyed on the way to testing it.** `php / pla / and #$01` leaves the
carry in **A** -- and the code then used A as if it still held the row, so `PROW` was
stored as 0 or 1 for every point. Every cell was coloured in **cell row 0**, putting the
ink and a pixel along the top edge: dots on the top of the canvas, on every circle. Fixed
by storing the row *before* reading the flags, and letting the wrap test overwrite `PROW`
with the `$ff` sentinel only when the point is genuinely off the canvas.

**(3) A sign test read the offset it had just loaded.** `ldy #imm` sets **N from the value
it loads**, so:

```asm
    sbc (dataPtr),y        ; dx hi -- bit 7 is its sign
    ldy #GP_WK_RJ + 1      ; <-- N is now set from THIS, not from the subtraction
    sta (dataPtr),y
    bpl gpDgRadBoth        ; <-- tests offset 58, which is positive, so always branches
```

The negative path never ran, so `|dx|` stayed as the raw two's-complement high byte `$FF`,
and the very next test saw it as `|dx| >= 256` and forced the radius to **255**. Dragging
to the **south-east** has `dx >= 0` and was unaffected; dragging to the **north-west** was
always a full-radius circle. Fixed by reading the byte back before testing it.

**Transferable:** only the **carry** survives an intervening load. `sta` and `ldy` leave C
alone, but `lda` and `ldy` set **N and Z from what they load** -- so a carry test is safe
across a `ldy`/`sta` chain (which is exactly what a two-step 16-bit difference needs) and a
**sign test is not**: it must read the byte it means to test. And for a single-BYTE result,
the +1 of a two's-complement negation belongs in the same `adc` as the operand (`sec`
before the `adc`), never as a separate add of the carry into the same byte -- which is what
skipped the `v = 0` point on every circle.

---

## Bug #162 — `gpDgRadius` stored |dx| in ZERO PAGE instead of the task's own field, so a preemption could return a different radius 🟠 ✅ FIXED (2026-09-27)

**Cause:** the routine used `sta GP_AJ`, `ldy GP_AJ + 1`, `cmp GP_AJ`. `GP_AJ` is **27** -- an *offset* into the instance buffer, reached with `(dataPtr),y` everywhere else in the task -- so the bare form assembled to `$001B`/`$001C`, two bytes of zero page the task has no claim on. Inside one call the store and the read agree, which is why the routine looked correct; what it cannot survive is a preemption between them, because a switch DMAs another task's declared ZP window in over `$02-$EF`.

**Symptom:** the same as #159 -- the band's two passes computing different radii, so the erase toggles a rim that is not the one on screen and a ring is stranded. Intermittent, and it needs nothing beyond a switch landing in a ~40-cycle window.

**Fix:** `|dx|` moved to `GP_WK_RJ`, two bytes of the task's own instance data (`GP_DATA_SIZE` 57 -> 59), reached with `(dataPtr),y`. That also stops the routine borrowing `GP_AJ`, which is the line tool's major span.

**Transferable:** a `GP_*` constant is an offset, never an address. Both forms assemble and the wrong one is silent -- the assembler has no way to know which you meant.

---

## Bug #161 — `grpaint`'s circle drag erased the wrong circle: both of its passes read the live radius, so the press's `R=0` pixel was left on the canvas 🟠 ✅ FIXED (2026-09-27)

**Symptom:** drawing a circle always left one stray pixel at the centre of it, and that pixel survived the commit.

**Root cause:** the drag is a two-pass pattern, and it only works while the erase pass draws a shape described by state the refresh has **not yet** touched: the erase runs, then the geometry is saved, then the draw runs. The rectangle is safe because `gpBoxArgs` reads the **saved** edges in `GP_WK_X1/X2/Y1/Y2`, which only `gpBoxSort` writes. `gpCircleArgs` read the **live** `GP_ANCH`/`GP_POS` instead, so both passes built the same circle at the current radius — the erase toggled exactly the pixels the draw was about to toggle, the two cancelled, and no frame's circle was ever taken off.

The stray pixel is the press. `gpDgStart` draws the gesture's first frame as `kCircle` with `R = 0`, which plots the **centre pixel**, and nothing ever retoggled it: a circle of radius >= 1 does not touch its own centre, so neither pass of any later frame included it. Hence one permanent dot, at the centre, every time.

`gpDgCancel` carried the same defect — a right-button press mid-drag erased at the live radius, so cancelling a circle toggled the wrong rim.

**Fix:** the circle now keeps a saved radius, exactly as the rectangle keeps saved edges. `GP_WK_RAD` (one byte; `GP_DATA_SIZE` 56 -> 57) is written by `gpCircleSort` (`gpDgRadius`, then the store) and read by `gpCircleArgs`. `gpDgSort` refreshes both descriptions and computes the radius only when the tool **is** the circle, so a rectangle or stroke drag never runs `gpDgRadius` and never touches its scratch; `gpDgStart` and `gpDgFrame` call it where they called `gpBoxSort`. The commit calls `gpCircleSort` before marshalling — the mirror of `gpBox`'s `jsr gpBoxSort` — so the circle committed is where the pointer is **now**, not where it was on the last held frame. `grpaint` 2897 -> 2921 bytes.

**Consequence:** a press-and-release with no movement now leaves **nothing** on the canvas — the erase pass takes the `R=0` dot off, and the commit is skipped because the pointer never moved.

**Transferable:** a saved-value refresh must sit *between* the two passes of an erase/redraw pair. A pass that recomputes from live input cannot erase anything — it draws over itself.

---

## Bug #160 — `kCircle` and `kEllipse` ignored the pixel op, so `grpaint`'s circle preview drew concentric rings 🟠 ✅ FIXED (2026-09-27)

**Symptom:** dragging the circle out left a ring behind on every frame — many concentric circles instead of one following the pointer — and nothing was erased until the release.

**Root cause:** `gfxOp` is honoured by `gxPix` and `gxPixReu`, which is why the rectangle preview works: `gxBox` calls `gxPix`. `kCircle` and `kEllipse` do not. Each has its own two pixel writers — `gxCirclePixGo`/`gxCirclePixReu` and `gxEllipsePixGo`/`gxEllipsePixReu` — and all four consulted `gfxColor` alone. The XOR the preview set was silently dropped on those paths, so every pass wrote its rim **SET** in the ink, and so did the erase pass: each frame's circle simply stayed on the canvas.

**Fix:** the four writers take the same op test the shared writers take (`ldy gfxOp` / `bne <Xxx>Xor`, with a four-instruction XOR tail). ~48 bytes across `gfx.asm`, which puts `gfx.lib` at 7194 bytes — **29 pool pages where it was 28**. The duplicated writers are kept deliberately: they are there for speed, and a shape's own writer is a few cycles per pixel ahead of the shared one. `basic` still starts with the extra page.

The `gfxPixelMasks` uses in the flood fill and in `kGetPixel` are **reads**, and need no op.

**Transferable:** a duplicated fast path has to be taught every mode the shared path learns, and the assembler does not say so. The op is now consulted in six writers; the four that were missed were missed precisely because they do not call the other two.

---

## Bug #159 — a live preview interrupted by a task switch wrote its band SET instead of XORed, stranding a ring on the canvas 🟠 ✅ FIXED (2026-09-27)

**Symptom:** rings and dots left on the canvas after a circle drag -- the concentric circles that appear when the drag expands the radius, contracts it, and expands again.

**Cause:** the preview draws its band under the XOR pixel op, and the switch path **clears** `gfxOp` on every switch (Bug #158's fix, which is what stops one task inheriting another's XOR). `gpDgBand` set the op with `kSetGfxColor`, which ends with a `cli` -- so from that point until the shape routine's own `sei` there was an open window of around 100 cycles in which a switch could land and strip the op. The shape call then wrote its pixels **SET**.

**Why a ring is stranded rather than a pixel or two being wrong:** an interrupted **erase** pass is the bad case. The erase is meant to toggle the previous frame's rim off; written SET it leaves that rim exactly as it was, and the draw pass then adds the new rim. The next frame erases the *new* rim -- the one `GP_WK_RAD` holds -- and never revisits the old one, so that ready ring stays for the life of the picture. Each occurrence strands another, which is the concentric-circle pattern.

**Fix:** the op is set and cleared with the interrupts off -- `sei` / `sta gfxOp` before the shape call, `sta gfxOp` / `cli` after it. The `sei` before the store removes the window entirely, and costs nothing in interrupt latency: `gxBox` and `gxCircle` hold SEI for their own loops anyway, so the only extra disabled time is the `kArg` marshalling between. `kSetGfxColor` selector 4 cannot be used for the set -- it ends with a `cli`, which opens exactly the window being closed.

**Note:** this makes the per-task op unnecessary. The deferred fix in the original entry -- an `$9800` REU shadow, ~50 bytes of switch code and a band compaction -- is not needed and is not planned.

**Related:** #158 (the walk this replaced, and the switch-clear), #162 (the same symptom from a different cause).

---

## Bug #158 — `grpaint`'s live rectangle preview was a hand-written XOR walk over the framebuffer, and it could write where it liked 🟠 ✅ FIXED (2026-09-27)

**Symptom:** the drag never drew -- pressing a box tool and moving the pointer hung the machine.

**Root cause:** `gpDgWalk` (with `gpDgRow`/`gpDgCol`/`gpDgBase`/`gpDgDown` and three mask tables) computed the framebuffer byte for every outline pixel itself and wrote it, with **no clipping of any kind**: the span loop walked `CX - 2` whole cells from an address it derived, so any inconsistent count or edge ran it off the end of the framebuffer row and into `$4000+` -- `kArg`, `savedSP`, kernel data. This is why the fault was a hang rather than a graphic glitch, and it is the class `kBox` cannot have: the library's pixel writer drops anything off the canvas (`cmp #200`, `cpx #64`) instead of writing it.

**Fix:** the walk is gone. `gfx.lib`'s pixel writers honour a **pixel write op** (`gfxOp`, `$ED5D`; 0 = set/clear on `gfxColor` as always, non-zero = XOR), set through `kSetGfxColor` selector 4, and `gpDgBand` is now `op = 1 → kBox → op = 0` -- about 20 bytes where there were ~180, every pixel written by the kernel, bounded by the library's own clipping. The op is honoured by the framebuffer path and the REU-slot RMW path; MC mode ignores it, because a 2-bit pixel index has no XOR meaning. `grpaint.bin` 3694 → 3256 bytes.

**Cost and residual:** `gfxOp` is kernel state a drawing call reads, so the switch path **clears** it (5 bytes in the core block, which consumed the `$0EFE-$0EFF` pocket). That makes it per-task in effect -- no task can inherit another's XOR -- and leaves one residue: a band cut in half by a preemption has its tail drawn SET rather than XORed, so erasing it can flip a few pixels of the picture underneath the band. A per-slot REU shadow would remove that, but its switch-side code is ~25 bytes per site (~50 total) against 15 bytes of core headroom, so it needs a band compaction rather than a pocket. Free total 130 → **127B across 22 pockets**.

**Transferable:** a kernel primitive's clipping is the bug-prevention, not a convenience -- a task that computes framebuffer addresses itself has to re-implement it, and the first thing it loses is the bound.

---

## Bug #157 — `grpaint`'s rectangle drag never showed a live outline: `gpDgFrame` tested the held button as if it were active HIGH, so the first held frame ran the release 🟠 ✅ FIXED (2026-09-27)

**Symptom:** with the box or the filled box selected, pressing and dragging drew **nothing at all** -- no live outline while the button was held, and no rectangle when it was released. A *fast* click during a movement could still leave a thin box behind, which is what made it look like a partial failure.

**Root cause:** `gpDgFrame`, the routine that runs once per held drag frame, opened with

    and #GP_BTN_LEFT
    beq gpDgRelease

and that is the test for the button being **UP**. `mouse.asm` returns the buttons active low (`A = the buttons, PA5..PA0, 1 = released`) and `GP_BTN_LEFT` is PA4, so a HELD button masks to zero and `beq` was taken: the first frame after the press jumped straight into `gpDgRelease`. The release takes the one-point band the press drew back off the canvas, clears `GP_DRAW`, and then commits the rectangle the pointer described at that instant -- or, when the pointer had not yet moved a whole pixel, cancels it. Either way `GP_DRAW` is 0 for the rest of the gesture, so every later frame reached `gpShape` -> `gpShapePress`, which refuses because the button was already down (`and #GP_PREV` is zero for "held last frame"). The drag ended one frame after it began, after drawing and immediately erasing a single pixel.

The thin box a fast click produced came down the same path: by the frame the bogus release ran, the pointer had one or two pixels of movement, so the commit drew a real but tiny rectangle -- and with the pre-fix cell counts (**#155**) that commit was also the one that could walk off the canvas.

This is **#154**'s mistake repeated, and the two tests are easy to confuse:

| Test | Zero (after `and #mask`) means |
|---|---|
| `GP_BTN & mask` -- this frame's button byte | **HELD** (active low) |
| `GP_PREV & mask` -- last frame's button byte | **held last frame**, i.e. not a fresh press |

So in `gpShapePress`/`gpShapeClick` a `beq` is right, and in a held-frame test it is exactly wrong.

**Fix:** `bne gpDgRelease`, with a WARNING above it naming the polarity. Verified in the built binary: the four `and #$10` sites in `grpaint.bin` are `gpDraw` (`a0 0e` -> bne), `gpShapePress` (`a0 23` -> beq), `gpShapeClick` (`a0 23` -> beq) and `gpDgFrame` (`a0 0e` -> bne).

**Why the offline model missed it:** the model was built from the *flow the comments describe* -- press, then `off`/`sort`/`on` per frame -- so it could check the walk's arithmetic and the state machine's ordering, but it could never see a branch whose *condition* was inverted, because it never transcribed the condition's sense from the assembly. A model of a 6502 routine has to be a transcription of the instructions, not of the intent.

---

## Bug #156 — an F-key press ran the display switch in the middle of a running task's `kArg` block 🟠 ✅ FIXED (2026-09-27)

**Symptom:** a task marshalling a kernel call through `kArg` -- `grpaint`'s `kBox`/`kFillBox`, `gpDma`'s four REU transfers, any `kReu*`/`kGfx*` caller -- could be handed a parameter nobody asked for, and only while an F-key was being pressed. It read as an intermittent drawing defect rather than as an F-key problem (**#155** was diagnosed through it).

**Root cause:** `kbPoll` ran before `switchTask` in the same tick, and its F-key branch called `kbSwitchToScreen` -> `switchView` -> `doGfxSwitch` on the spot. `doGfxSwitch` **uses the `kArg` block**: it stores `fromTask` in `kArg`, and the surface save/load parameterises `kReu*` through `kArg+0..+5`. The tick's own switch path is careful with the block -- `switchTask` DMAs all 8 bytes to `REU_KARG_BASE + slot x 8` on the way out and `svRestore` puts them back on the way in, and the per-switch work (`surfDisplayed`, the cursor sync, `rasterCommit`) is written to keep off it -- but the key scan runs *before* that save, so its switch writes landed in a block a running task had live. An F-key press mid-marshal therefore corrupted the call, intermittently and only for as long as the key was held.

**Fix:** the switch is recorded, not applied. `kbPoll`'s F-key branch stores the requested screen in `kbScreenReq` (`$FF` = none) and returns; the scheduler's switch path opens with `jmp kbScreenGate`, which applies the request (`kbScreenReq` -> `kbSwitchToScreen`) and then joins the **restore** path (`jmp svRestore`, so `sprInstall` still publishes the new owner's sprite state and the incoming task's block is still restored), or jumps straight back when nothing was asked for. No latency is added -- the switch still happens in the same tick -- and no caller needs an IRQ-free window around a `kArg` marshal any more; `gpDma`'s and `gpBoxSize`'s `php`/`sei`/`plp` windows were removed on the strength of it.

**Transferable:** a routine that writes `kArg` must not be reachable from a context where another task's block may be live. The tick's ordering is the constraint (`kbPoll` before `switchTask`), not the routine, so the fix belongs at the call site: record the intent, apply it where the block is already shadowed.

---

## Bug #155 — the box and filled box left a row of stray dots along their bottom end: the cell-colour walk plotted outside the rectangle, and its counts came from the span instead of the edges' cells 🟠 ✅ FIXED (2026-09-27)

**Symptoms:** clicking twice with the box or the filled box drew the shape and left a row of dots eight pixels apart at its bottom end -- just inside an outline, and one cell row below a filled box. Some cells of the right-hand end also kept their old colour.

**Root cause:** `gpBox`/`gpBoxWalk` colour the shape's cells with `kPlot`, which is the only call that writes a cell's matrix nibble -- but `kPlot` (and the internal `gxPix` the shapes use) writes the **pixel** too, so every point of that walk has to be a pixel the shape itself has drawn. The walk broke that rule four ways, and two more defects made it intermittent.

- **The fill drew one cell row too many.** The row loop plotted its row and then counted down, so the pass that took the counter to zero had already drawn the last row -- and the `jmp gpBoxSpanX` after the loop drew once more, a whole cell row below the rectangle: dots below the shape, and for a rectangle low on the canvas a row past 199, which `kPlot` does not clip and `gfxRowLut` does not cover. The loop now ends with `rts`.
- **The outline's bottom row was plotted at its cell row's first raster row**, not at the bottom edge, so whenever the height was not a multiple of 8 the dots sat on the rectangle's interior. The bottom span now runs along `Y2` (its top span already ran along `Y1`), so each point lands on the pixel `kBox` drew.
- **The cell counts were `(span >> 3) + 1`.** Right only when an edge starts a cell: the cells a span covers are `cell(X2) - cell(X1) + 1`, and the two differ whenever the edges sit at different offsets inside their cells -- so the last cell row and column went uncoloured. The counts now come from the endpoints' cell numbers (the low byte over 8, with the high byte worth 32 cells).
- **The spans stepped 8 from the edge.** Also right only when the edge starts a cell: from `X1` the last point can land in the cell that holds `X2` but *beyond* `X2` itself, which is outside the shape -- a dot past the right-hand end. Each span now plots its first point on the edge and then steps to the following cells' starts (`(x + 8) & $F8`), which keeps every later point inside as well.
- **A span inherited a coordinate one cell past its predecessor's last point.** The outline ran its top row straight after a column walk, so the row used whatever row the column had stepped to -- up to one cell past the bottom, again a row `kPlot` cannot clip. Every span now sets the coordinate it does not walk: the columns set `x` (`X1`, `X2`), the rows set `y` (`Y1`, `Y2`).
- **`kArg+4` was used as task scratch** for the cell counts. `kArg` is a calling-convention scratch, not task state: any kernel call writes it, and an F-key press rewrote the live block outright through `switchView` (**#156**). The counts now use two bytes of the instance buffer.
- **The `kArg` marshal and the `jsr kBox`/`kFillBox` call could be interrupted.** `kArg+4` is the rectangle's **width** low byte, and while the marshal is open an F-key press ran `switchView` in the middle of it -- the switch stores `fromTask` in `kArg` and parameterises the surface swap through the same block -- so the library could be handed a width nobody asked for, a rectangle drawn to the wrong edge, whose cells the walk then did not match. The `php / sei ... plp` windows this got at the time are gone: the F-key switch is now applied from the scheduler's switch path, where the block is already the task's own saved copy (**#156**).

**Verification:** the walk is modelled offline (same shifts, same steps, same span order) over 6,600 rectangles -- every endpoint pair on a lattice that includes `x` values either side of 256 and rows at the canvas edges. It asserts that no point the walk plots leaves the rectangle, and that the cells it colours are exactly the rectangle's cells (fill) or its boundary cells (outline). Both held before the rebuild.

**Transferable:** `kPlot` is two operations -- colour the cell, set the pixel -- and a caller that wants only the first must plot where the shape has already drawn. And a "one point per cell" walk has to derive its count from the **cells** at each end, never from the span between them.

---

## Bug #154 — `grpaint`'s line tool never drew a line: its click test asserted the button was UP, and the release committed the anchor as if it were a pencil stroke 🟠 ✅ FIXED (2026-09-27)

**Symptoms:** the pencil (the spawn default) painted freehand normally, but picking the line tool — the leftmost tile — and clicking twice drew nothing: the first click marked its pixel and no line ever followed.

**Root cause:** two defects in the click path.

- `gpDrawLine` opened with `and #GP_BTN_LEFT / beq gpDrawLineRts` ("held: a hold is not a click"). The buttons are **active low**, so that test returns while the left button is DOWN — and `gpDrawLine` is only ever reached from `gpDraw`'s button-down path — so the routine returned on every frame it was entered. The test that can tell a click from a hold is the `GP_PREV` comparison below it (the previous frame's button), which was already there.
- `gpDrawUp` committed whatever `GP_DRAW` held, but that byte means "a pencil stroke is in progress" for the pencil and "an anchor is waiting for its second click" for the line tool. Committing on release drew the line from the anchor to wherever the pointer was when the **first** click ended, and cleared the anchor — so the second click became a new anchor instead of drawing. The routine's own header says the tool is driven by the button's *edge* and that the second click draws, so the code was violating its documented contract.

**Fix:** `gpDrawLine` keeps only the `GP_PREV` test; `gpDrawUp` commits only when the tool is the pencil; and selecting a tool clears `GP_DRAW`, because a half-made line has to be abandoned with the change — the pencil would otherwise read the stale anchor as a stroke in progress and draw a segment nobody asked for.

**Transferable:** a state byte that means two things depending on a mode must be *read* with the mode — and an active-low test's branch polarity is the opposite of the same test on a normally-high signal, which is how a condition that reads as a safety check becomes an unconditional exit.

---

## Bug #153 — the UI band was drawn as if a bitmap raster row were a contiguous 40 bytes, so the palette came out as a solid cell every fifth column and the tools smeared across the row 🟠 ✅ FIXED (2026-09-27)

**Symptoms:** with `grpaint` running and the right button held, the band came up as garbage: an 8x8 block every fifth cell column along the bottom two rows, with fragments of the tool pictures strung between them, instead of a palette and two bordered tiles.

**Root cause:** a C64 hires bitmap is **cell-block addressed**. Cell (row, col) of the 40x25 grid owns eight CONTIGUOUS bytes -- `$2000 + row*320 + col*8`, the same resolution `gxPix` uses (`row/8` selects a 320-byte cell row, `(col & $F8)` the cell, `row & 7` the byte inside it) -- and those eight bytes are that cell's eight RASTER ROWS. Consecutive bytes are therefore eight pixels apart, and a run of eight bytes is a column of eight raster rows, not eight pixels of one row. `gpBuildUi`'s palette fill wrote eight bytes and advanced 40, and `gpTile` wrote two bytes and advanced 40: both are correct only for a raster-row-linear layout. Sixteen runs of eight bytes at 40-byte strides land on cell columns 0, 5, 10, ... 35 -- sixteen solid cells spread across the band -- and each tile row landed as raster row 0 of a different cell.

The band's own 640 bytes really are contiguous (cell row 23 is `$3CC0-$3DFF`, cell row 24 is `$3E00-$3F3F`), which is why the four DMAs and the REU images were right and only the contents were wrong; and the matrix is row-major, which is why the swatch colours arrived correctly while the pixels did not.

**Fix:** nothing writes the band on the C64 any more. The band's picture is the REU-FS file `gpui.bin` -- built by `tools/gen-ui.py` from the ASCII art in `assets/ui/gpui.txt`, and holding the band's own 640 pixel bytes (cell row 23's forty cells, then cell row 24's) followed by its 80 matrix bytes -- so `grpaint` opens it once at spawn (`kReuOpen`) and reads the two halves straight into `$3CC0` and `$0798` on every right click (`kReuReadAt`), with the band it covers saved to REU bank 0 and put back on release. The file is written in cells by construction, because the generator converts each 16x16 tool picture into its four 8x8 quadrants.

**Found by:** rendering the band offline **the way the VIC reads it**. The existing model read the pixels with the same raster-row assumption the code wrote them with, so it showed the intended picture while the machine showed junk -- a model that shares the code's misconception agrees with it. Re-reading with the hardware's addressing reproduced the garbage exactly (sixteen solid cells, the tile fragments between them), which is what identified the cause.

**Transferable:** a bitmap's memory is not a picture read left to right, it is cells. Any code (or model) that walks a bitmap with a row stride of 40 bytes is wrong, and the failure is silent -- the pixels land somewhere legal, just not where they were meant. The band's *contiguity* is not evidence about its *layout*: both were true at once here.

---

## Bug #152 — `grpaint`'s tool tiles were drawn five raster rows above the band, spraying junk across the picture 🟠 ✅ FIXED (2026-09-27)

**Symptom:** after the UI band grew to two cell rows the palette came up correctly, but the tool tiles were missing from it — and stray tile-shaped pixels appeared in the canvas above the band. The UI read as garbage.

**Root cause:** `gpTile` built the tile's address from its byte offset alone — `asl` (two bytes a tile), `clc`, `adc #GP_PK_PW`, `sta kPtr`, `lda #>GP_PK_SCR0`, `sta kPtr+1` — so the offset *became* the address's low byte and the high byte was a constant. That is only true when the base has a **zero low byte**, and it silently stopped being true when the band moved from `$3E00` (low byte 0) to `$3CC0` (low byte `$C0`) to gain its second row: every tile went to `$3C08 + row*40` — about five raster rows above the band, i.e. into the picture — while the band's own tile area stayed black. The assert on the constant (`the UI band's pixels start at $3CC0`) pins the base's **value**, which stayed true throughout; nothing pinned the property the arithmetic depended on.

**Fix:** the offset is added INTO the base, low byte and carry both — `adc #<GP_PK_SCR0+GP_PK_PW`, then `lda #>GP_PK_SCR0` / `adc #0` — so the arithmetic is right for any band position instead of for one page-aligned one. The same carry is now propagated in `gpMark`, which had the base right but would have broken identically the first time its constant carried.

**Found by:** rendering the band offline from the source — the art parsed out of `grpaint.inc`, the matrix rules applied — and then modelling `gpTile`'s own address arithmetic. Both halves were needed: the *picture* was never wrong (the palette, the borders, the diagonals all rendered as intended), only the addressing, and a model that computes its own addresses cannot see an addressing bug. The first render looked perfect while the machine showed junk.

**Transferable:** "the offset IS the low byte" is a base-dependent idiom, not a general one. When a constant such arithmetic leans on moves, the arithmetic has to move with it — and an assert on that constant's value does not cover it, because the value can be exactly right while the property it was chosen for is gone.

---

## Bug #151 — the pointer's outline took the ink, so it vanished on the background, and the palette's hover highlight drew over the colour it marked 🟠 ✅ FIXED (2026-09-27)

**Symptoms:** picking colour 0 (the background) made the pointer disappear -- the one thing on screen that says where the pointer is, gone exactly when the ink is black. And sliding the pointer along the palette changed the *swatch* it was on: the hovered cell inverted, so that cell stopped showing the colour it selects.

**Root cause:** two UI defects in `grpaint`, of one shape -- an indicator that cannot be told apart from the thing it indicates.

1. The two pointer sprites were split "ring = the ink, interior = the canvas background". The ring is the pointer's **outline**, and 0 is both a legal ink and the canvas's own background, so at ink 0 the outline was drawn in the colour of the surface behind it: an outline that erases itself. The intent behind the split (the interior cuts the canvas out from under the outline, so the outline reads) holds for every ink except the background -- which is the one it fails on.
2. The hover highlight was the hovered swatch's **matrix byte inverted**. In hires a cell's colour *is* its matrix high nibble, so the one byte that says "this cell is colour 5" was the byte being changed: the swatch stopped showing the colour it selects, and the highlight was indistinguishable from a swatch that simply *is* that other colour. Putting the previous swatch back was a copy out of the palette image, which is what made the write look safe -- but that copy only proves the *last* swatch is restored, never that the one under the pointer is showing its own colour.

**Fix:** the roles go the other way round. The **interior** carries the ink (`gpPickColour`, and `gpPickSetFill` for the preview) and the ring takes a **fixed** colour (`GP_RING_COL`), so the head keeps its outline at every ink including 0. The hover feedback moved off the band and onto the pointer: `gpPickSel` now writes **no screen byte at all** -- it records the swatch under the pointer in `GP_MSEL` for the release and colours the interior with it, so the preview and the ink readout share the one channel, the sprite, and cannot displace each other. Off the palette the interior goes back to the ink. The highlight block went with it -- two matrix writes, an inversion and the image copies that undid them -- leaving the task 34 bytes smaller and `$07C0` onwards written only by `gpPickShow`/`gpPickHide`.

**Transferable:** an indicator may not be drawn *in* the thing it indicates -- neither in the bytes that carry the value (a swatch's colour) nor in the same visual channel as the background it sits on (an outline drawn in the ink). Put the feedback where the eye already is, and make it reversible by construction: `gpPickSel` cannot leave a stray mark now because it cannot make one.

---

## Bug #150 — `gpDraw` closed `grpaint`'s palette on the frame after it opened, so the band toggled at the frame rate 🟠 ✅ FIXED (2026-09-27)

**Symptom:** holding the right button to bring up the palette band made it appear and disappear rapidly and repeatedly, instead of staying up until the button was released.

**Root cause:** `gpDraw` runs on every displayed frame and opened with a guard that closes the palette -- *"a stroke that begins while the palette is up puts the band back first"* -- and that guard sat **before** the left-button test, so it ran unconditionally. `gpPicker` is level-driven: while the right button is held it opens the band, or follows the pointer once the band is up, on **every** frame. So each frame did the same three things -- `gpPicker` opens the band, `gpDraw` immediately closes it, next frame repeats -- and the band was never up for longer than a frame. The pair also copied 320 bytes of pixels and 40 bytes of matrix twice per frame for nothing.

**Fix:** the guard now follows the left-button test, so it can only fire on a frame where the left button is **down** -- which is the case it was written for, a stroke that would otherwise be drawn under the UI. With the left button up, `gpDraw` goes straight to its release path and the band stays up for as long as the right button is held.

**Transferable:** a per-frame routine's "tidy up first" guard belongs **inside** the branch it applies to. Left unconditional at the top of the routine it runs every frame, which turns a one-shot clean-up into a toggle against whatever the other half of the frame has just done.

---

## Bug #149 — one pot sample can read a count high, so the pointer shivered by a pixel 🟡 ✅ FIXED (2026-09-27)

**Symptom:** `grpaint`'s pointer tracked correctly but occasionally vibrated by a
pixel, on either axis, and it came and went as the mouse moved.

**Root cause:** VICE adds a random 0-or-1 to every pot sample it latches. In
`src/sid/sid.c`, `makepotval` does `fuzz = lib_unsigned_rand(0, 1)` for a mouse --
its own comment says paddles get a proportional error and *"This does not happen
with the 1351 mouse"*, i.e. the mouse takes the fixed small one -- and the values
are re-latched once per 512 cycles, so the fuzz is fresh on every sample.

The fuzz is aimed at the raw byte's **low bit**, which is the 1351's qualifier and
is dropped by the protocol -- **except** that adding 1 to an **odd** byte carries
into the position field. The reader decodes bits 6-1, so an odd raw byte decodes
to `d` or to `d+1` at random: one count, one canvas pixel, independently on each
axis, and only where that axis's raw byte happens to be odd.

The error is **bounded, not cumulative.** Every sample's delta is
`dec_i - dec_{i-1}`, so the accumulated position is `dec_last - dec_first` and the
intermediate fuzzes cancel; what is left is `fuzz_now - fuzz_first`, i.e. plus or
minus one. That is a shiver, not a drift, which is exactly what was reported.

**Fix:** the fuzz only ever **ADDS**, so the *smallest* reading in a frame is the
true one. `grpaint` now follows each axis's offset from the frame's first sample
(`gpTrack`: sRun and its minimum sMin) and moves the position by the change in
that minimum (`gpSettle`) instead of by the frame's last sample. The correction is
zero whenever the pointer is moving steadily and one count when the noise is
there; over the 8 samples a frame the residual is about 1 frame in 250. 117 B of
code and no extra time -- the samples were already being taken.

**Lesson:** noise that is **one-sided** can be filtered by a **minimum**, and that
is the only statistic that works. A level noise at a 50% duty cycle survives a
median and survives a debounce, because *"the reading is one high"* and *"the
pointer really moved one"* are the same observation -- no amount of looking at the
series separates them. The corollary matters as much: the same minimum would bias
a **symmetric** plus-or-minus-one device low, which is why the window is short and
the reasoning is written down next to it.

**Method note:** VICE's own source, not a guess -- `makepotval` and
`mouse_get_1351_x`'s `(last_mouse_x & 0x7f) + 0x40` are the whole of it. The
arithmetic in `mouse.lib` and in `grpaint` was walked end to end first (the decode,
the mod-64 difference, the signed fold, the sign extend, both clamp paths) and is
sound, which is what left the device as the suspect.

---

## Bug #148 — `grpaint`'s pointer wrapped at x = 255 instead of crossing to the right of the canvas 🟡 ✅ FIXED (2026-09-27)

**Symptom:** the pointer tracked correctly to the right until it reached the middle
of the canvas and then appeared at the **left** edge.

**Root cause:** `gpPlace` built the sprite's window x from the canvas x's **low
byte** alone -- `lda (dataPtr),y / clc / adc #GP_WIN_X / sta GP_WX` -- and took
the sprite's 9th bit from **that add's carry**, which is the carry out of the low
byte and says nothing about the canvas x. A canvas x of 256 has a low byte of 0,
so the window x came out as `0 + 24` and the pointer landed on the left edge; the
canvas x's **high byte was never added at all**. The clamp could not catch it --
256 is a legal canvas x.

**Fix:** the high byte is added into `GP_XH` in the same carry chain
(`lda (dataPtr),y / adc #0 / sta GP_XH`), so `GP_XH` **is** the sprite's bit 8 and
the 9th bit is no longer derived from half an add.

**Lesson:** a 16-bit quantity has to be added as 16 bits. Taking a high bit from
the carry out of a low-byte add is only correct when the add is the *first* half
of the 16-bit add -- here it was the whole of it, and the high byte sat unread in
the instance buffer next to the byte being used.

---

## Bug #147 — a display-owning task saw a frozen 1351: the SID arms its pot latch on a *change* of the port-select mask 🟠 ✅ FIXED (2026-09-27)

**Symptom:** `grpaint`'s pointer sprite was drawn correctly and then never moved,
while `mtest` -- the same library, the same port, the same 512-cycle wait --
tracked the mouse perfectly in the same session. Two programs, one device, and
only one of them worked.

**Root cause:** `mouse.lib`'s read wrote the *same* select byte (`$80`, port 2) on
every sample. The SID arms its pot latch from a **change** of the mask `$DC00`
bits 6/7 select (`store_ciapa` -> `set_joyport_pot_mask((b >> 6) & 3)`), so from
the second read onwards nothing was armed and `$D419`/`$D41A` went on returning
the value they were last given -- with a 1351 under VICE a near-centre pair,
$40/$41, which decodes to **zero movement on both axes**. The buttons stayed live
throughout, because PA5..PA0 are read from the same port directly: only the axes
froze, which is the shape the fault has, and it is why the readout looked like a
dead mouse rather than a dead port.

**Why `mtest` could not show it:** the shell's **keyboard scan writes `$DC00`** on
every tick to drive the matrix columns. A `.com` that shares the shell's screen is
carried by that traffic -- the mask changes under it constantly, so its reads are
armed and it never sees the fault. A task that **owns** the display has no
keyboard scan and no other writer, so it has to supply the change itself or watch
a still picture. The two programs were never equivalent, which is exactly what
made "but `mtest` works" a false lead for as long as it did.

**Fix:** `mouse.lib` writes `$C0` and then the select it wants, in two stores, so
the mask changes on every read whichever port is asked for. Four bytes.

**Measured:** a bank-`$FF` dump of the task's own bytes, read back out of the REU
image on the host, showed `MOVE 00` with `raw x/y = $40/$41` before and `MOVE 01`
with `raw $7F/$99` after. Reading `$D419` **directly**, bypassing the library
entirely, gave the same frozen pair -- which is what ruled the library's
delta arithmetic out and left the device as the suspect.

**Lesson:** "the other program works" is evidence only when the two programs are
equivalent in everything that touches the hardware. A per-tick writer elsewhere in
the system is precisely the kind of difference that hides -- and a task's own
isolation from the shell is usually a feature, here it was the variable.

---

## Bug #146 — a library's promised-zero result was left unwritten in shared scratch, so the first consumer to fold it jumped 🟡 ✅ FIXED (2026-09-27)

**Symptom:** `grpaint`'s pointer sprite appeared at an arbitrary y on its first
frame and then never moved again. `mtest` — the same library, the same port
select, the same 512-cycle wait inside SEI — worked perfectly, which is what
pointed at the *first* sample rather than at the read.

**Root cause:** `mouse.lib`'s per-axis loop publishes the raw byte and then, when
there is no previous sample to difference against, jumps **over** the delta
store — `lda mousePrimed / beq mouseFirst` skips `sta kArg+MOUSE_DELTA_X,x`. Its
own contract says the read that primes reports **delta 0** (`mouseEnable`'s
comment, `docs/lib-mouse.md`), so the field was not "zero", it was **unwritten**.
`kArg` is per-call scratch that the previous kernel call had filled — in
`grpaint`'s case a `kSpriteSheet` directory scan — so the consumer read
filesystem scratch as a delta and added it to its position. The y register then
held a value that matched no placement the code can produce, which is exactly
what the task's self-check reported.

**Fix:** the loop stores `0` into the delta before the primed test, so the priming
read answers with the zero it promises. 5 bytes, and the library stays one page.

**Lesson:** a result that is *promised* to be zero must be **written** as zero. In
a shared-scratch calling convention an unwritten field is not "nothing" — it is
somebody else's value. And a readout task cannot see the defect: it displays the
one stale frame and moves on, so "the other program works" is evidence about the
*moment* the bug hits, not about whether the bug exists.

**Method note:** the evidence was the task's own bytes read back out of the REU
image (a bank-`$FF` dump written by the task, then the image inspected on the
host) -- no screenshot and no emulator command in the loop.

---

## Bug #139 — preset 10's masked random draws were skewed by kRandom's bit order, so one of the three colours never appeared 🟠 ✅ FIXED (2026-09-24)

**Symptoms:** the montage drew almost everything in one or two of its three colours -- the third appeared once in a whole run -- with the same colour repeating for successive figures; and the shapes stayed cluttered even after the preset bag had been weighted toward the roundest shape.

**Root cause:** `kRandom` is a Galois LFSR that shifts **left**, so bit *i* of one value is bit *i-1* of the previous one. A masked draw (`& $03`, `& $0f`) taken on the montage's steady stride therefore reads a *sliding window* of the same register, and the decimated sequence is badly skewed. Modelling the task's own draw order -- two masked draws per figure plus two placements -- and running 60 full montages showed:
- `& $03` for the figure's colour returned **3 not once in 1500 picks**, while 1 took 875 of them, and the longest run of a single colour was **6**;
- `& $0f` for the preset index drew the 12-lobe shape 522 times against 348 for the 5-lobe one -- **inverting** the bag, which is what kept the picture busy after the bag had been re-weighted toward the round shape.

The bags were right and the sampling was wrong, and no re-weighting could fix it.

**Fix:** `sgRnd` returns `v ^ (v>>3) ^ (v>>6)` -- three overlapping fields of the same byte XORed, which decorrelates the mask. Every *masked* draw goes through it now (preset index, scale, colour, palette base). Over the same 60 runs the colours come out 464/516/520 against an even 500, and the longest same-colour run falls from 6 to 3. Full-byte draws are untouched: `sgMontPlace`'s rejection sampling reads the whole byte, which is uniform over the LFSR's 255 non-zero states, so it needed no change.

**Retuned in the same pass:** the bags. The preset bag is now `1` x10, `6` x3, `5` x3 -- only presets whose rolling radius is high and whose lobe count is low (5, 12, 13 lobes) -- and the scale bag is `2,2,2,3`, so a figure is a quarter of the screen or less. Rendered offline, a bag of half-height figures merged twenty-five of them into one mass, which rounder shapes alone did not cure.

**Method note:** what found this was an offline Python model of the task's logic -- the LFSR, the bags, `sgDerive`, the placement, the multicolour halving and the palette -- rendered to a PNG to judge by eye. No emulator involved.

---

## Bug #138 — preset 10's three palette colours could be neighbours, so two of them read as one colour 🟡 ✅ FIXED (2026-09-24)

**Symptom:** the montage's figures looked as though they were drawn in two colours rather than three.

**Root cause:** `sgMontInk` drew each colour freely in 1..15 and re-drew until it differed from the ones already chosen. That does guarantee the three are *numerically* distinct -- and they were -- but 12 and 15, or 1 and 15, or 8 and 9 are one or two palette steps apart, and at montage size those read as the same colour. Distinct values are not distinct appearance, and a free draw can land on neighbours.

**Fix:** the palette is now **built** instead of chosen: one draw picks the first colour and the other two sit a fixed **five** apart from it, wrapping inside 1..15. The three are then distinct *by construction*, so no re-draw loop can be defeated by an unlucky sequence, and the closest pair in any of the fifteen possible triples is five palette steps apart. It is also smaller and faster: three draws became one, and every triple was checked.

**Tuned in the same pass (not a defect):** the shape and scale bags. The preset is now drawn from a weighted sixteen-entry bag holding only shapes of 5-18 lobes -- the 37, 38 and 40-lobe presets are out of it, cutting the mean from about 22 lobes to 9.9 -- and the extra-halvings bag is `1,1,2,3` rather than `1,2,3` with 3 favoured, so 51% of the figures are drawn at the largest size the screen leaves room for instead of 50% at the smallest.

---

## Bug #137 — preset 10 crowded the bottom right of the screen and drew every figure stretched 🟠 ✅ FIXED (2026-09-24)

**Symptoms:** `run spirog 10` piled most of its 25 figures into the bottom right corner instead of spreading them over the screen, and each figure was visibly wider than it was tall.

**Root cause (placement):** `sgMontPlace` turned a random byte into a signed offset with `min(raw, 2*limit) - limit` -- a **clamp**. A byte lands inside the range only `(2*limit+1)/256` of the time, and every draw that misses piles onto the *same* value, `+limit`: the far edge. At the limits the montage actually uses (x 56-74, y 52-88) that put 31-59% of all figures at +x *and* +y at once, so the centre distribution had a mean of +30 rather than 0 -- the corner. Uniform placement needs **rejection**, not clamping: a byte past `2*limit` is simply drawn again. Measured over 40,000 draws per limit, the edge share falls from 31-59% to the uniform 0.5-1.0%, the mean to 0.0, and the loop costs 1.5-2.5 draws per figure (the worst case, the largest figure, still accepts 41%).

**Root cause (aspect):** a multicolour pixel is **twice as wide as it is tall** -- 160 of them across a screen 200 high -- so an x excursion of `ext` pixels beside a y excursion of `ext` pixels drew a figure `2*ext` screen units wide but only `ext` tall. Every montage figure came out stretched sideways. `sgPoint` now halves the x excursion in multicolour only (gated on `SG_MODE`, so hires is untouched), with an **arithmetic** shift: `cmp #$80` puts bit 7 in the carry and `ror` shifts it back in, because a logical `lsr` would turn a small negative offset into a large positive one. The placement window follows, so x is measured against the halved extent.

**Verified:** over 10,000 figures, x lands in 0..158 and y in 0..198 (both inside the 160x200 screen), mean x 79.3 against a centre of 79, mean y 99.3 against 99, and 49.1% / 49.5% of the pixels in the left / top half against a symmetric 50%.

**Transferable:** a clamp is not a uniform sample -- it *concentrates* every out-of-range draw on one value, and the more often it triggers the worse the pile. When a random byte has to cover a smaller range, reject and redraw.

---

## Bug #136 — `spirog` handed the bitmap mode to `allocScreen` unset, so every preset exited at once, and its montage painted the wrong rows 🔴 ✅ FIXED (2026-09-24)

**Symptoms:** after preset 10 was converted to multicolour, `run spirog 1`-`9` (and any explicit `R r d`) drew nothing at all and left the shell on screen, while `run spirog 10` took the display and showed a blank screen.

**Root cause:** three defects introduced together with the multicolour work.

1. **The mode became a caller argument, and one caller never set it.** `sgSurface` used to supply its own (`lda #0`, hires). It now took the mode in A, so the shared single-shape tail (`jsr sgDerive` / `jsr sgSurface`) passed whatever `sgDerive` happened to leave there. `allocScreen` rejects a mode of 2 or more (`cmp #2 / bcc alModeOk ... sec; cli; rts`), so `sgSurface` took its failure path and ran `SYS_EXIT`: the task died before it allocated anything, which is exactly why the shell stayed up with no picture and no error. The same edit also left `sgDraw` calling `sgSurface` a second time on that path -- not an error in itself, because `asReuse` hands a task back its preserved surface and ignores the mode, but a display switch nobody asked for.
2. **The drawing value was initialised on only one of the two drawing paths.** `SG_INK` was set in `sgDraw`; the montage never goes through `sgDraw`, so every `kLine` in preset 10 drew with 0 -- and 0 in multicolour is the **background** pixel value, so the figures were drawn invisibly on black.
3. **The cell-paint row pointer destroyed its own accumulator.** `sgPaintRowGo` builds `row*40 = (row*8)*4 + (row*8)`, but the `lda #0 / sta SG_BP+1` that prepares the high byte sits between `sta SG_R8` and the `asl` chain, so the chain shifted **0** and the row index came out as `row*8`. The matrix therefore landed in rows 0-4 whatever row was intended, while colour RAM landed in the row the matrix should have had -- so the cells the figures were drawn in kept matrix `$00` and colour RAM `0`, whose pixel values 1-3 all resolve to black. A task writing its own surface's matrix and colour RAM directly (the only way to give each figure its own palette, since `kFillColorRam` fills all 1000 cells) has no kernel call to catch this for it.

**Fix:** each of the three surface paths sets its mode explicitly (`lda #0` for hires at the single-shape tail and at the usage screen, `lda #1` for the montage), so a run allocates one surface; `SG_INK = 1` moved to the top of `sgTrace`, which every curve begins with -- single shape and montage figure alike; and the row pointer reloads `SG_R8` into A before the shift chain. Verified against an emulated pointer for all 25 rows: every row resolves to `$0400 + row*40`, colour RAM derives as `+$D400` for every row, and the last cell is `$07E7` -- the end of the 1000-byte matrix.

`docs/kernel.md`'s `kAllocBitmap` table recorded C=1 as "no free slots" only, which is what hid defect 1; it now names the invalid-mode rejection and the preserved-surface reuse.

**Transferable:** a routine that used to supply its own argument cannot simply be given one -- every caller must be updated in the same edit, and a status code that means two things (`C=1` = no slot *or* bad mode) tells the caller nothing unless it checks what it passed. State a drawing path needs must be set **on that path**, never on a sibling path that merely happens to run first.

**Superseded in part:** the per-figure cell painting that defects 2 and 3 sat in is gone. The montage now picks its three colours once and fills **every** cell with them (`kFillMatrix` for the two nibbles, `kFillColorRam` for value 3), so each figure is drawn in one of the three -- which retires `sgMontPaint`, its row pointer, and the "an overlap repaints the cells they share" caveat with it: two figures in one cell now read the same palette.

---

## Bug #135 — a signed byte added to a screen base carried, so every left-going half of a `spirog` curve was drawn 256 pixels off screen 🟠 ✅ FIXED (2026-09-24)

**Symptoms:** once `spirog` gained a movable centre, every shape drew as its right half only, with long horizontal strokes running across it.

**Root cause:** the x path built the screen x as `clc / adc centre / lda #0 / adc centre+1`, treating the **carry out of an 8-bit add** as the high byte of the sum. With a negative pen offset the operand is a two's-complement byte, so that carry means nothing: `160 + (-12)` carries -- `$A0 + $F4` passes `$FF` -- yet the true answer is 148. Every negative half therefore became `value + 256` (x = 404, 322, ...), which `kLine` clipped as off-screen, leaving only the half with positive offsets plus the long clipped-to-visible runs between them.

**Fix:** sign-extend the offset and do a real 16-bit add (`bpl` on the loaded byte gives the extension `$00`/`$FF`, then `adc centre` / `adc centre+1`). Checked against the model afterwards: a single shape is back to x 65-254, y 5-194 and closed, and a montage centre of 220 correctly reaches x = 266 with the high byte set. **The carry out of an 8-bit add is a high byte only when both operands are unsigned** -- which is exactly what the ABI's 16-bit helpers exist to avoid doing by hand.

**The same defect appeared a second time, two lines after the fix**, in `sgMontFit`'s x centre: preset 10 placed every negative offset at `160 + offset + 256`, so roughly half of its 25 figures landed past the right edge (clipped away) and the rest piled into x = 160..297 -- the montage drew on the right half of the screen only, with a cramped range. Sign-extending that offset as well fixed it; the y centre needs no high byte at all, because `100 + offset` stays inside 0..199 and the low byte is therefore the value. Coverage measured over three seeds afterwards: x 1..320, y 4..200, against x 130..447 (visible 130..319) before.

---

## Bug #134 — `spirog`'s argument parser read a whole `run` line as ONE number, so valid geometry printed the usage screen 🟠 ✅ FIXED (2026-09-24)

**Symptoms:** `run spirog 110 45 70` printed the usage line, while `run spirog 96 40 60` and `run spirog 105 63 42` appeared to work -- but drew shapes nobody asked for. `run spirog 110 45 70 7` drew in the wrong colour. Only the no-argument and single-argument forms behaved.

**Root cause:** two defects in one routine, `sgNum`. (1) A space took the "skip it" path and then re-entered the *digit* loop, so a number never ended at a separator and the entire argument string accumulated as a single value. (2) The value*10 chain (`txa / asl / sta T1 / asl / asl / clc / adc T1 / adc T2`) folded the carry out of the first `adc` into the second, so a value passing 255 came out wrong instead of saturating. Together they made the mis-parse silent: R became 7 for `110 45 70` (7 is below the default r = 35, hence the usage line) and 220 (clamped to 127) for `96 40 60`, with r, d and the colour left at their defaults. The `gfxdemo` single-value parser this was modelled on never saw either defect, because its one argument stays below 26.

**Fix:** `sgNum` is now two phases -- skip spaces *before* a number, then read digits until a non-digit **ends** it, leaving Y on that character so the next call resumes there -- plus `cpx #25 / bcs` saturating before the multiply, which makes the accumulate carry-free by construction (24*10+9 = 249). Verified by emulating the routine instruction by instruction in Python against every argument form; the first attempt at the fix still looped back into digit reading, and only that model caught it.

**Also fixed in the same release:**
- The usage screen's second line inherited the `kArg` block that the first `kGfxString` left behind, because that call clobbers the whole block -- so it drew from a stale column. The position is set again for each line. (The general rule, already recorded: a helper that uses `kArg` cannot be called from code where `kArg` is live state.)
- Taking the display did not un-draw the shell's live blink block, so quitting `spirog` occasionally left a phantom cursor on the shell's screen -- a repeat of **#83**'s rule, now applied with the documented SEI-wrapped `kBlinkReset` window before `kSwitchView`.
- `run spirog 1`-`9` selects a preset shape. That is safe because a lone `R <= 9` can never be valid geometry: r defaults to 35 and must stay smaller.

---

## Issue C5: `kPrintTime` Buffer Write Order ⚫

**Location:** `src/kernel.asm` — `printTime`

The AM/PM suffix is written in non-sequential order (`timeBuf+9` before `timeBuf+8`) due to the `BIT` trick flow. This is harmless but makes the buffer layout harder to read. A sequential write with a `bne`-based skip would be clearer.

---

# SUMMARY

### Active / Open

| ID | Severity | Status | Title |
|---|---|---|---|
| #183 | 🟠 High | ✅ Fixed | **In fill mode, the left press that dismissed the palette band also ran the flood fill.** `gpDraw`'s band guard reads `GP_MENU`, but `gpPicker` runs first in the same frame and `gpPickAway` clears `GP_MENU` as the band goes down — so the dismissal press reached `gpShapeFlood` as a canvas press and filled from a click made while the UI was up (deliberate for the pencil/shapes, #164b; wrong for a fill that reads its region from the canvas's pixels). `GP_MENU0` now records `GP_MENU` at the top of each displayed frame, before the picker can put the band away, and `gpShapeFlood` refuses when it is set: the dismissal only dismisses and the next press fills. `grpaint` 7729 -> 7743 bytes, 71 pool pages (unchanged), 64 asserts. Rule: **a guard that must tell "the UI was up when the press landed" from "the UI is up now" reads the frame's STARTING state — the flag that means "is up now" is cleared by the very dismissal it must detect.** |
| #182 | 🔴 Critical | ✅ Fixed | **`allocFromPool` promised X and clobbered it, so every `kMalloc` recorded a wrong page count — pages stranded in the bitmap, or another owner's pages freed.** `kMallocImpl`'s `kmSlotFound` took the count from X with `txa` (commented "from X, not kArg: `kmEvictAlloc`'s `dlEvict` clobbers `kArg..+7`" — right about `kArg`, wrong about X), and `allocFromPool`'s own `poolBitCheck`/`poolBitSet` candidates each end in `tax`, so the value that stuck was `startPage + pages - 1`: `mallocPages = (startPage + pages - 1) & 7`, an arbitrary 0-7. `kFreeImpl`/`freeMallocBlocks` then freed exactly that many pages — too few strands used pages with no owner, too many clears the pages *above* the block, and the next allocation hands those to a new owner, which writes code over a live task's code: a JAM, and VICE falls back to the KERNAL READY prompt. Found from two save-states of the reported repro (`run sidplay`, `run edit`, quit, let the tune finish, `run sidplay` again): `edit`'s 4-page chunk at 55-58 was freed as 2 pages from 55, stranding **57-58** and splitting the free run at 56/59 — with **no owner table row for either page**, which is why `pool` looked clean, and why `run sidplay` then died in the load path while a second `run edit` was silently refused (the shell sacrificed itself). Fix: read the count from `allocFromPool`'s `kArg+4` echo (2 bytes). Same false `Clobbers:` line corrected in `freeFromPool`, `markPagesUsed`, `dlAllocCode`, `setTextColor`, `setCursorColor` and `spriteCollide`; `shareScreen`/`takeScreenOwnership` gained their (true) X contract. Free total 127 → **125 B / 22 pockets**; the code chain's growth tail `$FC3B-$FC3E` → `$FC3D-$FC3E`. Left behind: **`tools/audit-clobbers.py`** (path-sensitive X-contract screener with a 17-routine `--selftest`) and **`tools/vsf-pool-audit.py`** (reads a VICE save-state's pool bitmap and cross-checks it against the owner tables, reporting STOLEN and STRANDED pages). Rule: **a clobber list is a contract, and the outermost one is the only one a caller reads — a routine that calls a `tax`ing primitive has already clobbered X.** |
| #181 | 🟠 High | ✅ Fixed | **The first of the four MC slot chips always showed black, however the picture had been saved.** On a load `gpPicPalMc` fills the task's four palette entries (`GP_MB+0..+3` -- what the chips draw, what a swatch press for slot 0 assigns to, what `gpCells`/`gpCram` refills the canvas from, and what value 0's marks are matched against) and it took `bg0` from `GP_HDR + GPIC_PAL_OFF` -- **+12, the border** -- instead of `+13`. `GPIC_PAL_OFF` is the palette's FIRST byte and the order is `border, bg0, bg1, bg2`, so the one MC colour the header carries (`$D021`, the value-0 source) was overwritten with the border's. Measured on the seed: `assets/pics/mc.pic` holds border 0, **bg0 6 (blue)**, bg1 0, bg2 1, with cell 0's matrix `$01` and its colour RAM `2`, so its four chip colours are blue, black, white, red -- the task drew black, black, white, red. Only the FIRST of those two blacks was wrong: the file's c1 really is 0 (613 of its 1000 cells carry a 0 high nibble), which is what the second chip is showing. The picture on screen was never affected, because `$D021` is written by `gpPicPal`'s selector loop, which does use +13 -- so the fault showed as a chip and nothing else, and the picture looking right is exactly why 4.13.5's verification passed it. Fix: `ldy #GP_HDR + GPIC_PAL_OFF + 1`. Rule: **an offset constant that names a STRUCTURE's start is not a field index** -- `GPIC_PAL_OFF + n` is a different colour per mode (hires: paper, ink; MC: bg0, c1, c2), so a read of it has to name the field it means rather than count from the block's base. `grpaint` 6762 bytes, 53 asserts. |
| #180 | 🔴 Critical | ✅ Fixed | **A second `run grpaint` did not open a second copy — it evicted the one that was running.** `grpaint` was `KF_RE_RUNNABLE`, so the second `run` skipped the single-instance dup check (`dlDupChk`) and took the full reloc path — where `dlPreEvict` sizes the load as code + `dlEagerLibPages`, and that deliberately counts a **resident** library's pages in full ("its only other holder may be evicted + re-loaded"). The second load therefore asked for **55** pages (25 code + gfx 27 + sprite 1 + mouse 2) while only **35** were free, so `dlEvict` took the largest victim — `grpaint` #1's 25 code pages — `dlEvLibs` released its libraries so the new instance could re-load them, and `dlEvSwapOrKill` left it `TA_SWAPPED` with its dyn entry kept and flagged. Every symptom followed from that one swap, and each looked like a different fault: `pool` **hides** a swapped row (`pcPoolDynLp` skips `DYN_FLAG_SWAPPED`), so there appeared to be one `grpaint`; a swapped task owns no code pages and cannot be scheduled, so only the second instance worked and the first sat still; and its surface is retained, so both screens could still be switched to. `grpaint` is now **single-instance** (`KF_RELOCATABLE \| KF_TAKES_ARGS`), so a second `run` is refused with `? already running` (error 11) **before** anything is allocated or evicted, and the copy holding the canvas keeps it. That is the right answer for the task rather than a loader change, because two copies do not fit anyway: its live footprint is **71 of the pool's 109 pages** (31 code + 30 eager lib pages + `filesys.lib` 10, which its cold start loads to open the band's file; the code was 25 pages when this was fixed), so two copies plus the shell are the whole pool with nothing to spare. `tools/check-pool-budget.ps1` now lists `grpaint` in `$StartupLazy` for those 10 pages — it had been reporting 55 — which is what puts that number in front of the build. Rule: **a re-runnable flag is a statement about the POOL, not about the code — the second load is sized against the libraries that are already resident, so a task whose footprint is a large fraction of the pool evicts its own first instance instead of running beside it, and because `pool` hides swapped rows the eviction reads as "the task never started".** |
| #176 | 🔴 Critical | ✅ Fixed | Two faults, each the mode conversion applied to **one of two callers**. **(a) The live circle outline was double the size.** `gpDgBand` erases and redraws the shape twice a frame, so the preview and the commit must enter the *same* call -- and the preview still called `kCircle` while the commit had moved to `kEllipse` (RX = r/2, RY = r): the ring the user dragged was a canvas circle of the **screen** radius, twice as wide as the ellipse it became, and it disappeared under the shape it promised. Both now enter `gpCircCall` (hires: `kCircle`; MC: `kArg+4 = GP_WK_RAD`, `lsr` into `kArg+3`, `kEllipse`), and both take the pen's colour from `gpColour` rather than `GP_INK`, which is what a mark claims in MC. The box was already shared (`gpBoxArgs` in canvas pixels + `kBox`), which is why only the circle showed it. **(b) A palette press re-coloured the whole band.** `gpPickTakeInk`'s MC path calls `gpCells` -- in MC a cell's byte *is* the palette, so a swatch assignment must re-fill the canvas matrix -- and the band's own two cell rows are canvas cells like any others: the swatches and the tiles came back in the new colour and stayed wrong until the band was closed and re-opened, the one path that re-reads the image. The path now re-applies the band exactly as a tool change does (`gpDgOurs` / `gpUiMat` / `gpChips` / `gpMark`), and every whole-canvas writer was audited: `gpPickTakeClear` and `gpPickTakeMode` already re-read afterwards, and `gpCanvas` fills before the band opens. **(c)** same pass: the text pen committed `(ink << 4)` whatever the mode, so a glyph placed with the value 2 or 3 read a colour source the byte never set -- the pen's cell byte is now the palette's in MC, `(c1 << 4) \| c2`, the same byte `gpCells` lays a canvas out with; colour RAM is deliberately not written here, because `gpCells` has already put c3 there and a per-glyph write would only fight it. **(d) The pen's whole geometry was the HIRES glyph's.** `gxChar` draws each glyph row **twice** in MC and a canvas pixel is two screen pixels across, so a glyph there is **16 x 16 screen pixels** -- while the advance, the row step, the wrap edge, the history divisor and DEL's step were all written for 8: the caret landed in the middle of the glyph it had just typed, a line stepped 8 into a 16-tall row, and the ink box coloured one cell of a two-cell footprint. One helper, `gpGlyphW` (8 or 16, screen pixels), now feeds all five, and the ink box is counted from it -- one cell across and one down in hires, two of each in MC, plus one each way when the pen is off an eight-pixel boundary -- and clamped at the matrix's right edge and last row. **(e) The committed circle wore dot noise around its circumference.** The circle's cell walk plots a *rim pixel* per cell through `kPlot`, which writes the pixel as well as the nibble -- and the rim is then rasterised twice, once by the library and once by the walk, so wherever the two disagree by a pixel the walk sets a stray dot in the pen's colour. In MC the walk has nothing to do anyway: a cell's byte there IS the palette, which `gpCells` has already laid across the whole canvas, and a loaded picture's own per-cell colours are exactly what should stay. `gpCircleCells` now returns immediately in MC, which removes the dots and the work with them; the box's walk is axis-aligned, so its pixels land exactly on the edges it has just drawn and its MC writes change nothing. **(f) A swatch assigned to slot 0 changed nothing.** In MC value 0 selects `$D021`, which is a VIC register and not a matrix nibble: the colour was stored in the palette and re-filled into every cell, while the pen on value 0 kept the black the surface started with. The swatch path now also hands slot 0's colour to `kSetGfxColor` (selector 1 = bg0) -- which writes the task's own copy and the hardware while we own the display -- and mirrors it into the header's palette byte so a save carries it. `grpaint` 5986 -> 6220 bytes, 53 asserts. **Rules: a preview and its commit must enter ONE call for the same geometry -- two callers drift, and the drift is invisible until the commit lands under the outline; a canvas-WIDE fill must be followed by the band's own restore, because the band's rows ARE canvas rows; a MODE is a change of SCALE -- every pitch, step and divisor the hires code assumed is a number the mode owns, so it must be derived in one place rather than restated as 8; and a colour that reaches no REGISTER shows nothing -- in MC value 0 is `$D021` and the matrix carries the other three, so a palette entry is only as good as the place it is written.** |
| #177 | 🔴 Critical | ✅ Fixed | **A fast flick threw the pointer, and it kept moving after the mouse stopped.** `gpSample` reads the pointer eight times a frame and strips the pot's noise by replaying the previous frame's rise (`gpSettle`, `uPrev - sRun + sMin`) -- with all three of those quantities kept in ONE BYTE. Eight reads of a fast flick are up to +/-32 each, so `sRun` passes +127, and the tracker's minimum test, which reads a *signed* difference, latches the wrapped value as the frame's smallest. The correction that comes out is the previous frame's rise replayed as movement: the pointer carries on by a frame's worth of travel after the mouse stops, further the faster the flick was -- and it is worst on the y axis, whose canvas is 200 rows against the x axis's 320, which is why it read as "huge distances towards the top and bottom" on a diagonal, where both axes are at their maximum at once and a slower speed keeps every quantity inside the byte. The correction is only ever noise, so one larger than a frame of +/- one per read can hold is not a correction at all: `gpSettle` now takes it as zero, testing the magnitude while keeping the sign (PLA restores the value it was handed and leaves the carry alone). Rule: **a wrapping accumulator may carry its wrap in a DIFFERENCE but never in a COMPARISON -- a signed test on it latches the wrap as a real extremum, and a filter built on that extremum then replays arithmetic that was never in the data.** `grpaint` 6220 bytes, 53 asserts |
| #178 | 🟠 High | ✅ Fixed | **Picking one of the four slot chips showed nothing until the band was closed and re-opened.** In MC the chips are the band's only cells coloured from RUNTIME state, and the ring on one of them is the only thing that says which slot a swatch press would assign to. `gpChips` draws it -- and a chip press (`gpPickTakeChip`) set `GP_SLOT`/`GP_VAL` and returned, so the ring moved only on the next `gpPickShow`, i.e. on a re-open. A chip press now re-draws the chips itself, display-owner guarded like every other band writer. Rule: **state an indicator reads is not state the indicator has -- a press that changes it must redraw what shows it, or the feedback arrives a gesture late and reads as "nothing happened".** `grpaint` 6309 bytes, 53 asserts |
| #179 | 🟠 High | ✅ Fixed | **Two instances of `grpaint` could not both follow the mouse** — whichever sampled consistently moved, the other sat still. `mouse.lib` kept its reader state (the last position each axis was sampled at, the priming flag, the enable flag) in the library's own `.byte` variables, and a library has ONE copy of those for every task that declares it. The header called that the correct model ("one mouse, one position"), and it is wrong for the same reason it is wrong for sprites: the shared thing is the DEVICE, not the state. A pot reports an ABSOLUTE position, so each reader must difference against its OWN last sample — then both see all of the travel instead of alternating, and no read consumes another task's movement. The reader now keeps a 4-byte record PER TASK indexed by `curTask` (flags, last x, last y, and the button byte that sample read — the buttons had to move in there too, because the axes loop runs with the interrupts on and a shared byte could be rewritten under it), and `MOUSE_ENABLE`/`MOUSE_DISABLE` are per task. The raw bytes are published straight into the caller's `kArg+0/+1` as they are sampled, which is where the API says they go, so no staging is needed. `mouse.lib` 1 -> 2 pool pages. Rule: **a library's state is shared by every declarer, so state that belongs to a CALLER must be indexed by the caller — and a hardware register that reports an ABSOLUTE value is the easy case, because differences against a private copy are lossless.** |
| #175 | 🔴 Critical | ✅ Fixed | **Every band entry past x = 255 was dead**, and the MODE tile read as "nothing happens". `gpPickSel` built the cell across as `x >> 3` from the **low byte alone** -- correct only while x < 256, and harmless until now because x >= 256 was **rejected outright**: the entries ended at exactly 256, which was #168's fix. 4.13.2 moved that edge to **304** (thirteen tiles and four chips) and opened the high half to the entry map without making the cell nine bits, so a press anywhere in the right-hand 64 pixels computed cell 0-7 -- the **palette** -- and took the *swatch* under that cell number instead of the tile or chip under the pointer. Pressing the MODE tile therefore set the ink to colour 0: no mode change, no band change, nothing to see, and the same silent wrong pick for the four chips. Fix: the x's high byte is now the edge test AND the half marker -- `cell = GP_PK_HALF + (lo >> 3)` for x >= 256, joined to the same tile/chip walk, so both halves share one map. One branch also had to become an invert-and-jump (`bcs gpPickColour` was a 131-byte backward branch once the walk grew, Rule 4). Pinned by asserts: the high half begins at cell 32, the MODE tile is entry 28, the chips are 29-32, the entries end at 33 / pixel 304. `grpaint` 5827 -> 5839 bytes, 53 asserts. **Rule: a hit test that DIVIDES a coordinate must use every bit of it -- the moment an edge moves past 255, the low byte alone silently wraps the far half onto the FIRST entries, which is worse than a miss because it takes something.** And the corollary: when a limit that used to *reject* a range is lifted, audit everything that assumed it -- the rejection was load-bearing |
| #174 | ⚫ Tooling | ✅ Fixed | The **call-count numbers in the two call inventories were stale**, and nothing could catch it: `verify-inventory-docs.ps1` checked every quoted **address** against `jumptable.inc` but never a count, while the counts drift with every task that gains a call — `kSyscall` read 158 against a real 183, `kNewline` 132 against 160. Fix: `tools/gen-call-inventory.ps1` regenerates the numbers (never the prose, never an address) and `verify-inventory-docs.ps1` now runs its `-Check`. The rule it applies, one rule for both documents: every `*.asm`/`*.inc` under `src/`; a call is `jsr`/`jmp` to the entry or one of its four `.const` aliases; full-comment lines excluded; the first `jmp` inside a `libStub*` cold stub is that stub completing its own cold call and not a caller (23 of them, asserted by the script so a new stub cannot silently change the rule); and **every** match on a line counts, so `jsr kNewline; jsr kNewline` is two call sites — which is why the totals rose, the old figures having counted lines. Building it also cost a bad afternoon worth recording: the first version's rewrite re-emitted **every** section at **each** section heading, so the kernel document ballooned to **123 MB / 3.8 M lines**, and every later run then looked like a hang (a 123 MB parse, not a loop). Both documents were restored from git and four faults fixed — the emit/skip boundary must be the next `## ` heading, not the next `### ` one; the library parse captured only a section's *heading* (`$k = $i` instead of `$i + 1`), so 425 lines of its prose were being dropped; `$_.Count` on a **hashtable** is its key count, not a key named `Count`, so every per-file row printed `2`; and the emitted paths used Windows separators where the documents used `/`. Guard: `Assert-SameShape` refuses to write when the rewritten document's line count is more than double or less than half the original's — that is what stopped the second attempt rather than a third rewrite of the tree |
| #173 | 🔴 Critical | ✅ Fixed | **`run basic` stopped starting** the moment `gfx.lib` grew by one page: the banner printed, then `could not load filesys.lib`, then exit. `basic`'s live footprint is its 65 code pages plus the eager `gfx.lib` (27) and `fp.lib` (8) plus the **lazy** `filesys.lib` (10) its cold start opens its REU working file with — 110 pages of a 109-page pool. Nothing noticed: `dlPreNeed` counts the **eager** libraries only (`dlEagerLibPages`), so the task loaded (100 <= 109) and failed later at the first lazy call, and no build step added the sum up — while the footprint had been **exactly 109/109 already**, so any byte anywhere in that chain breaks it. Fix: BASIC's code 16638 -> 16327 bytes (65 -> 64 pages) by removing five statements — `DEEK`, `DOKE`, `VARPTR`, `SADD` and the `SETFONT "name"` statement — and `tools/check-pool-budget.ps1`, called by `build-tasks.ps1`, now computes the footprint of **every** task from the tree (pool geometry from `pool.inc`; code pages and `libMask` from each binary's header; the library order and eager flags from `main.asm`; and a `$StartupLazy` table for the lazy library a task's own startup touches, which no header can state), failing on an over-budget task and warning on an exact fit. Rule: **a task's live footprint is its code plus every library it holds while alive — eager and startup-lazy — the loader checks only part of that, and an exact fit is a bug waiting for one byte.** The removal carried a side effect of its own, which the same pass had to fix: `LIST` printed one garbled line, because removing a statement touches **five positional structures** and only one of them is unchecked. The `tk_*` chain, the dispatch tables (`lab_ctbl`/`lab_gxctbl`, `lab_ftpl`/`lab_ftbl`) and the `tab_asc*` keyword chains are all covered by the header, relocation and table-order checks; `lab_keyt` is a dense four-byte record per token indexed by the **raw token value** (`lab_keyt + token*4`), and a stray duplicate `.word lbb_poke` in it shifted every later record by two bytes — so `print`, the token after it, named itself wrongly while the interpreter still ran the program correctly. Rule: **a fixed-stride table indexed by a token value is invisible to every check the build has — count its records against the token count whenever a token is added or removed.** Also stale, and regenerated: the tokenised `src/corpus/basdemo.bas`, written from the current token tables by `gen-corpus.py`, which `build-assets.ps1` re-runs in front of the REU seeding |
| #172 | 🟠 High | ✅ Fixed | The **pixel write op was hires-only**. `gxPixMcC64` and `gxPixMcReu` — the two arms that every MC pixel write in the library funnels through, since `kPlot`, `kLine`, `kBox`, `kFillBox`, `kCircle` and `kEllipse` all arrive via `gxPix` — did a plain read-modify-write (`byte & ~mask | value`) and never read `gfxOp`, so an MC write always wrote the colour and a preview, a caret or a delete had nothing to erase with. Both arms now test the op and `eor` the shifted value into the pixel's nibble instead. **XOR on a 2-bit value is still an involution** — `(v xor k) xor k = v` — so the second pass restores the pixel exactly, which is what the caret, DEL and a preview's erase half need; what differs from hires is the *colour* the first pass shows, because an MC pixel is a selector into the cell's four colour sources rather than the pixel itself: over background `0 xor k = k` is the pen's colour (exactly as in hires, and that is most of a preview's length), over drawn work it is whichever of the cell's other sources the arithmetic lands on, and where that is 0 the pixel disappears into the paper — and it is invisible wherever two palette entries hold the same colour. `A = 0` therefore toggles nothing in MC, where the hires writer toggles whatever the colour says. Rule: **the op is honoured by every pixel writer in both modes, because XOR is defined on the VALUE — it is exact for anything whose job is to put the pixels back, and whether the colours it shows mean anything is the caller's business.** `gfx.lib` 6651 -> 6680 bytes, 26 -> 27 pool pages; the plan's 4.13.1 keeps `basic` as the gate for that extra page |
| #171 | 🟠 High | ✅ Fixed | A brush stroke near the right-hand edge **wrapped to the far left**: the blob's own pixels past the canvas came back as a short run at the start of the row below, and the cells there took the ink. `kPlot` (`gxPlot`) **does not clip** — the caller's `X = col lo` / `Carry = col hi` pair is an unconditional ADD 256 to a cell row only 320 bytes wide, so `x` 320..511 addresses the *next* cell row's first bytes (a 320-byte wrap: the far left, eight raster lines down), its matrix nibble lands a cell below the intended one (`(x>>3) + 32`), and a row of 200 or more indexes the 25-entry `gfxRowLut` past its end into the neighbouring table, giving a stray pixel and nibble far from the canvas. The pointer is clamped to 0..319 by 0..199 (`gpClamp1`), and the pencil's segment and the line's both run between clamped endpoints, so **the brush is the one tool that hands `kPlot` a point outside the canvas**: it marks a blob centred on the pointer, four pixels outside it, and the six-pixel-wide glyphs reach seven. `gpBrush` now tests every blob pixel against the canvas (`gpBrClip`: row < 200, and the x hi byte 0, or 1 with the low byte < 64) and skips the mark, so a blob at an edge is **cropped**; the same test removes the stray nibble write, since `kPlot` colours the cell it addresses. Rule: **`kPlot`'s contract is an on-canvas pixel and nothing in `gfx.lib` bounds-checks it** — a caller whose art extends beyond the pointer must clip it itself (the circle's cell walk already did), and the "it clips" claim in `gpBrush`'s header was the belief the bug was built on |
| #170 | 🟠 High | ✅ Fixed | Two faults in the text pen, "I sometimes see a phantom cursor where I start or stop typing. also it does not honour the colour chosen". **(a) The phantom block**: `gpTextPlace` put the pen down wherever the canvas was clicked **without ending a live entry first**, and the caret is an XOR on the canvas — so a second click moved the pen and left the old block inverted in the pixels with no record of it, one per move. `gpTextPlace` now opens with `jsr gpTextEnd` (which returns unless `GP_TEXT` says the pen is down, so the first placement costs a byte test). **(b) The colour**: a glyph was placed with `kGfxChar`, whose pixels go through `gxPix` — a **pixel** writer that never touches the video matrix — so a glyph's ink was whatever its cells already carried, which with the XOR op set is the **inverse** of what the glyph crossed, never the chosen colour. `gpTextInk` now writes `(ink << 4) \| bg0` (the byte the pencil's `kPlot` writes for a cell it enters) into every cell the glyph's 8x8 box lands in — one, or two across and two down when the pen sits off an eight-pixel boundary — **before** the glyph, so the XOR then shows the ink; on a cell already at the ink it is a no-op. Residual: the cells keep the ink, so a DEL restores the pixels exactly but a cell that held another colour does not get it back. Rule: **with the pixel op set `gfxColor` is ignored and a glyph blit writes no matrix — an ink is committed by the matrix, which is the task's job** |
| #169 | 🟠 High | ✅ Fixed | Typing a key with the text pen **restarted the task**: the main loop reached the key handler with `jmp gpTextKey`, and that handler and its helpers are **tail-called** (`jmp gpTextPut`), so their `rts` popped the frame `jsr gpInit` had left on the stack and resumed **`gpCanvas`** — the task re-initialised on every keystroke, which reads as a hang. The loop now **calls** the handler (`jsr gpTextKey` / `jmp gpLoop`), so a tail call returns into the loop. Rule: an entry point reached by a `jmp` or a branch must not end in a tail `rts` unless its own entry was a call — before adding one, check which frame the `rts` will pop. Same pass: the caret was drawn with `@`, which reads as a character already typed at the pen, so it is now `-` |
| #168 | 🔴 Critical | ✅ Fixed | The picker stopped picking: **any** press on the band dismissed it instead of taking the entry under the pointer, so no tool or colour could be changed and `grpaint` stayed on whatever tool it had — "selecting T stays in freedraw mode, I can't type any text". `gpPickSel` bounded the pointer's column with `cmp #GP_PK_TW4`, and that constant is `GP_PK_TX0 + GP_PK_TN*GP_PK_TW`: **240 with eleven tiles, 256 with twelve**. KickAssembler emits `cmp #256` as `cmp #$00` with no error and no warning, so `bcs gpPickSelOff` was taken for every column but zero — the band read as "off the entries", `GP_MSEL` went to `$ff`, and `gpPickTake` dismissed the band without taking anything. The bound is now the pointer's **x high byte** (`ldy #GP_POS + 1 / lda (dataPtr),y / bne gpPickSelOff`), which is that same edge exactly — x >= 256 is past the last tile — and cannot truncate. Rule: **a constant that can reach 256 must never be an immediate**; the assembler will not tell you, so test a byte that already has the edge in it (a high byte, a carry) or keep the value under 256 by construction |
| #167 | 🔴 Critical | ✅ Fixed | `run grpaint <name>` **hung the whole machine** when the picture existed — and only then, because that is the one path that applies the file's palette. `gpPicPal` kept its selector in **X** across `kSetGfxColor`, and `gxSetGfxColor` opens with `ldx curTask`, so X came back as a task slot; the loop then did `inx` on *that*, and because every iteration re-parked and restored the same corrupted value, X landed on `curTask + 1` again each time and `cpx #4` never fired. A loop with no exit and no yield: the canvas stayed on screen, black, since the palette was never applied (the sample's own pair is light blue on white). It is slot-dependent — `curTask + 1 == 4` exits on the first pass — which is why `run grpaint zzz` (no file: the palette is never reached) and `run grview hires` (the viewer hands the file's palette to `kAllocScreen` and never calls `kSetGfxColor`) both worked. The selector now counts in a task-static byte (`gpPicIdx`) and is reloaded into X for each call. Rule: **a value cannot ride X, Y or A across a kernel or library call without reading that call's source for that register** — `ldx curTask` hides in the signature |
| #166 | 🟠 High | ✅ Fixed | The **CLEAR** tile zeroed the video matrix and never put it back. `kClearBitmap` clears the pixels **and** the matrix, and in hires a cell's colour *is* its matrix nibble — so a page at matrix `$00` is black and every pixel written into it is colour 0: invisible. The **committed** box/circle still appeared, because the commit's cell walk colours each cell it enters (`kPlot` writes the nibble), but the **live outline** is `kBox`/`kCircle` under the pixel op, which write **pixels and no matrix** — so the band showed only in cells a walk had coloured already, i.e. a rectangle that looked *half drawn*, and only after a clear, since a fresh canvas is filled with `(ink << 4) \\| bg0` at spawn. `gpPickTakeClear` now fills that same byte back. Rule: a clear restores the surface's **invariants**, not just the pixels — and if the committing operation is visible while the previewing one is not, compare what each **writes** |
| #165 | 🟠 High | ✅ Fixed | The picker judged a press against `GP_MSEL`, the band entry the **previous** frame sampled, because `gpPickSel` — its only writer — ran only on frames with no press, while `GP_POS` had already been moved to *this* frame's position. One frame is not small: the 1351 reports -32..+31 per read and `gpSample` folds eight reads a frame, so the pointer can cross the 16-pixel band and reach the canvas between two frames — which is exactly the frame a shape's flick-and-press lands on. With the entry stale at the tile, `gpPickTake` took the tile again instead of dismissing, the band stayed up, `gpDraw` returned (the band owns the left button) and the drag never started: **no live outline after picking box or circle**, and it previewed only from the next press. `gpPickBand` now calls `gpPickSel` first and then reads the presses, so the entry is the one under the pointer now — one byte smaller, and it removes the same one-frame lag from the pointer's interior preview |
| #164 | 🟠 High | ✅ Fixed | A pick that left the band up cost two presses. (a) The CLEAR tile was a **tool**: the pick armed `GP_TOOL_CLEAR` and a later left press ran `gpShapeClear`. That only read correctly while the band **closed on a pick**, so the mode's next press was bound to land on the canvas; once the band stayed up, that press landed on the band -- which owns the left button -- so the clear waited for the dismiss too. It is now a **command** run on the picking press (`gpPickTakeClear`), with the palette read back over the blanked rows, the tool returned to freehand and the ink left alone; `gpShapeClear` and the `GP_TOOL_CLEAR` dispatch are gone. (b) `gpPickAway` cleared `GP_PREV`'s left bit as the band went down, to stop the picking press being read twice by `gpDraw` -- correct only while the picking press and the dismissing press were the *same* press. A pick leaves the band up, so the dismissal is a **fresh** press that must act: nothing is spent now, and the dismiss press starts the stroke, the shape's anchor or the flood fill. A pick still cannot mark the canvas, because `gpDraw` returns for every frame the band is up |
| #163 | 🟠 High | ✅ Fixed | Three assembly traps in the circle's cell walk, all in the transcription of an algorithm already proven exact offline. (1) The quadrant test was inverted — `cmp` sets C for `A >= mem`, and the loop tested `x >= y` where it runs while `x <= y`, so it exited on its first step and coloured nothing: the rim took the colour its cells already carried. (2) `php / pla / and #$01` leaves the carry in A, and the code kept using A as the row, so every cell was coloured in cell row 0 — dots along the top edge on every circle. (3) `ldy #imm` sets N from what it loads, so a `bpl` meant to test dx's sign tested the offset `58` instead, always branched positive, left `\|dx\|` as `$FF` and forced the radius to 255 — **SE drags worked, NW drags did not**. Rule: only C survives an intervening load; sign tests must read the byte they test |
| #162 | 🟠 High | ✅ Fixed | `gpDgRadius` stored |dx| at `GP_AJ`/`GP_AJ + 1` as BARE addresses. `GP_AJ` is 27 — an offset into the instance buffer reached with `(dataPtr),y` everywhere else — so the routine wrote zero page `$001B`/`$001C`, two bytes the task has no claim on. Self-consistent inside one call, so it looked right; a preemption between the store and the read returns a *different* radius, because a switch DMAs another task's ZP window in over `$02-$EF`. Same symptom as #159: the band's two passes disagree and a ring is stranded. Fix: `GP_WK_RJ`, two dedicated bytes of task data. Rule: a `GP_*` constant is an offset, never an address — both forms assemble and the wrong one is silent |
| #161 | 🟠 High | ✅ Fixed | `grpaint`'s circle drag erased the wrong circle: both of its passes read the **live** radius, so the erase toggled exactly the pixels the draw was about to toggle, the two cancelled, and no frame's rim was ever taken off. The press is drawn as `kCircle(R=0)`, which plots the centre pixel, and nothing retoggled it (a circle of radius >= 1 does not touch its own centre) — hence one stray dot at the centre of every circle, surviving the commit. The rectangle is safe only because `gpBoxArgs` reads the **saved** edges. Fix: `GP_WK_RAD`, one byte of instance data, written by `gpCircleSort` and refreshed by `gpDgSort` between the erase and the draw — the mirror of the box's saved edges; `gpDgCancel`'s wrong-rim erase is fixed by the same change. A press with no movement now leaves nothing |
| #160 | 🟠 High | ✅ Fixed | `kCircle` and `kEllipse` ignored the pixel write op: each has its own two pixel writers (`gxCirclePixGo`/`gxCirclePixReu`, `gxEllipsePixGo`/`gxEllipsePixReu`) which do not call `gxPix`, and all four consulted `gfxColor` alone — so the XOR a live preview set was silently dropped and every pass wrote its rim **SET** in the ink, leaving a ring per frame (many concentric circles) and erasing nothing. Fix: the op test added to all four writers (~48B); the duplicated writers are kept for speed. `gfx.lib` 7138 → 7194 bytes, **28 → 29 pool pages**; `basic` still starts |
| #159 | � High | ✅ Fixed | A live preview interrupted by a task switch wrote its band **SET** instead of XORed, stranding a ring on the canvas: `gpDgBand` set the op via `kSetGfxColor`, which ends with a `cli`, leaving a ~100-cycle window in which a switch could clear `gfxOp` before the shape call. An interrupted **erase** pass is the bad case — written SET it leaves the previous rim as it was, and the next frame erases the *new* rim and never revisits the old one, so each occurrence strands another (the concentric-circle pattern). Fix: the op is set and cleared with interrupts off (`sei` / `sta gfxOp` / shape / `sta gfxOp` / `cli`), which costs no interrupt latency because `gxBox`/`gxCircle` hold SEI for their own loops; `kSetGfxColor` cannot be used for the set because its `cli` opens the very window. This makes the per-task op (and its band compaction) unnecessary |
| #158 | 🟠 High | ✅ Fixed | `grpaint`'s live preview was a hand-written framebuffer XOR walk with no clipping, so an inconsistent span ran off the end of a row into `$4000+` — `kArg`, `savedSP`, kernel data — which is a hang, not a glitch. Fix: the walk is deleted and the preview is `kBox` under a **pixel write op** (`gfxOp` `$ED5D`, 0 = set/clear, non-zero = XOR, set via `kSetGfxColor` selector 4), honoured by the framebuffer and REU-slot writers, ignored in MC mode (a 2-bit index has no XOR). The op is cleared in the switch path (5B) so no task can inherit another's XOR; the residual is a band interrupted mid-draw having its tail SET rather than XORed. A per-slot REU shadow would close that but needs ~50B of switch code — a band compaction, not a pocket. `grpaint.bin` 3694 → 3256 |
| #157 | 🟠 High | ✅ Fixed | `grpaint`'s rectangle drag never showed a live outline and never committed: `gpDgFrame`'s held-button test was `and #GP_BTN_LEFT / beq gpDgRelease`, which is the test for the button being **UP**. The buttons are active low and the routine is reached only from the button-DOWN path, so a held button masked to zero and took the branch — the first held frame ran the release: the press's one-point band came off, `GP_DRAW` was cleared and the rectangle was committed at that instant (or cancelled, when the pointer had not moved a whole pixel), after which `gpShape` only ever reached `gpShapePress`, which refuses because the button was already down. The drag died one frame after it started. Fix: `bne`, with a WARNING naming the polarity; the four `and #$10` sites in `grpaint.bin` are now `gpDraw` bne, `gpShapePress` beq, `gpShapeClick` beq, `gpDgFrame` bne |
| #156 | 🟠 High | ✅ Fixed | An F-key press ran the display switch **inside a running task's `kArg` block**: `kbPoll` runs before `switchTask` in the same tick, and its F-key branch called `kbSwitchToScreen` -> `switchView` -> `doGfxSwitch`, which stores `fromTask` in `kArg` and parameterises the surface swap through `kArg+0..+5` — so a task mid-marshal (a `kBox`/`kFillBox` window, `gpDma`'s four REU transfers) could be handed a parameter nobody asked for, intermittently and only while the key was held. The tick's own switch path is careful with the block (`switchTask` DMAs all 8 bytes to `REU_KARG_BASE + slot*8` and `svRestore` puts them back, and the per-switch work keeps off it), but the key scan runs *before* that save. Fix: `kbPoll` only **records** the screen (`kbScreenReq`) and the scheduler's switch path applies it (`kbScreenGate` -> `kbApplyScreenReq`, which joins `svRestore` so `sprInstall` still publishes the new owner's sprites). Same-tick latency unchanged, and `gpDma`'s and `gpBoxSize`'s `php`/`sei`/`plp` windows are gone |
| #155 | 🟠 High | ✅ Fixed | `grpaint`'s box and filled box left dots along their bottom end: the cell-colour walk plots with `kPlot`, which sets the pixel as well as the cell's nibble, and the walk plotted outside the shape -- the fill drew one cell row too many (its loop's last pass had already drawn the last row, and the trailing `jmp` drew again, which for a rectangle low on the canvas is a row past 199 that `kPlot` cannot clip), the outline's bottom span ran at its cell row's first raster row instead of `Y2`, the counts were `(span >> 3) + 1` instead of `cell(X2) - cell(X1) + 1` (so the last cell row/column went uncoloured), the spans stepped 8 from the edge (so the last point could pass `X2`), and a span inherited a coordinate one cell past its predecessor's last point. Two more made it intermittent: the cell counts were kept in `kArg` (`kArg+4`/`+5`), which is a calling-convention scratch rather than task state — a display change from the IRQ can rewrite the live block — and the `kArg` marshal plus `jsr kBox` could be interrupted by an F-key press, which ran `switchView` inside it. Note for the record: a plain task **switch** is not what clobbers `kArg` (`switchTask` saves the 8-byte block to `REU_KARG_BASE + slot*8` and `svRestore` restores it, and the tick's per-switch work keeps off it — `surfDisplayed`'s header makes that a rule); the exposure was `switchView`, which stores `fromTask` into `kArg` and swaps surfaces on an F-key press — now applied from the scheduler's switch path instead, so no caller needs an IRQ-free window around a `kArg` marshal (**#156**) |
| #154 | 🟠 High | ✅ Fixed | `grpaint`'s line tool never drew: `gpDrawLine` opened with `and #GP_BTN_LEFT / beq rts` — "held: a hold is not a click" — but the buttons are **active low** and the routine is only ever reached on the button-DOWN path, so it returned every time (the real click test, the `GP_PREV` compare, was already below it); and `gpDrawUp` committed `GP_DRAW` whatever the tool, so the first click's *release* drew the line to wherever the pointer then was and consumed the anchor, leaving the second click to anchor again. `gpDrawUp` now commits only for the pencil, and selecting a tool clears `GP_DRAW` |
| #153 | 🟠 High | ✅ Fixed | `grpaint`'s UI band came up as garbage: a hires bitmap is **cell-block** addressed (eight contiguous bytes are one 8x8 cell's eight raster rows, `$2000 + row*320 + col*8`), and `gpBuildUi`'s palette fill and `gpTile` both wrote eight/two bytes then advanced 40 as if a raster row were contiguous -- so sixteen solid cells landed on cell columns 0, 5, 10 ... 35 and every tile row became raster row 0 of a different cell. Nothing writes the band on the C64 any more: it is the REU-FS file `gpui.bin` (`tools/gen-ui.py` from `assets/ui/gpui.txt`), read straight into `$3CC0`/`$0798` on the right click |
| #152 | 🟠 High | ✅ Fixed | `grpaint`'s tool tiles were drawn five raster rows ABOVE the band, spraying tile-shaped junk across the picture while the band's own tile area stayed black: `gpTile` built the address from the offset alone (`sta kPtr` took the offset as the low byte, `#>` a constant), which only holds when the base's low byte is 0 — true at the band's old `$3E00`, false at `$3CC0` once the band grew to two rows. The offset is now added into the base low byte **and carry** (in `gpMark` too), so the arithmetic survives the band moving |
| #151 | 🟠 High | ✅ Fixed | `grpaint`'s pointer vanished when the background was picked, and its palette's hover rewrote the swatch it was marking: the ring carried the ink and 0 is both a legal ink and the canvas's own background, so at ink 0 the pointer's outline was drawn in the colour behind it -- an outline that erases itself; and the "highlight" was the hovered swatch's matrix byte **inverted**, and in hires a cell's colour IS its matrix nibble, so the swatch stopped showing the colour it selects. The **interior** now carries the ink and the ring a fixed colour, and `gpPickSel` writes no screen byte at all -- it records the swatch in `GP_MSEL` and gives the interior that colour, so the preview and the ink readout share one channel (34 bytes smaller) |
| #150 | 🟠 High | ✅ Fixed | `grpaint`'s palette band toggled at the frame rate while the right button was held: `gpDraw`'s "close the palette before drawing a stroke" guard sat at the top of the routine, **before** the left-button test, so it ran unconditionally -- every frame did `gpPicker` opens the band (right button held) -> `gpDraw` closes it -> repeat, and the band was never up for longer than a frame. The guard is now inside the button-down path, so it fires only on a frame where a stroke could actually begin under the UI |
| #149 | 🟡 Medium | ✅ Fixed | One pot sample could read a count high, so the pointer shivered by a pixel: VICE adds a random 0-or-1 to every sample it latches (`makepotval`), aimed at the raw byte's low bit -- the 1351's qualifier -- but a +1 on an **odd** byte carries into the position field, which our bits 6-1 decode reads as a count. Bounded at plus or minus one, on each axis independently, only where that axis's raw byte is odd. The noise only ever ADDS, so `grpaint` now moves by the change in the frame's **minimum** reading rather than by its last one |
| #148 | 🟡 Medium | ✅ Fixed | `grpaint`'s pointer wrapped at x = 255 instead of crossing to the right of the canvas: `gpPlace` built the sprite's window x from the canvas x's **low byte** alone and took the 9th bit from **that add's carry**, so a canvas x >= 256 (low byte 0) came out at the left edge -- the high byte was never added at all. `GP_XH` now takes the high byte through the same carry chain, so it *is* the sprite's bit 8 |
| #147 | 🟠 High | ✅ Fixed | A display-owning task saw a **frozen 1351** while `mtest` tracked it perfectly: the SID arms its pot latch on a **change** of the `$DC00` port-select mask, and `mouse.lib` wrote the same select on every read -- so nothing was armed after the first sample and `$D419`/`$D41A` held a near-centre pair (both axes -> zero movement) with the buttons still live. `mouse.lib` now writes `$C0` and then the wanted select; a `.com` sharing the shell's screen is carried by the keyboard scan's `$DC00` writes and cannot show it |
| #146 | 🟡 Medium | ✅ Fixed | `mouse.lib`'s priming read left `kArg+MOUSE_DELTA_*` **unwritten** -- `lda mousePrimed / beq` jumped over the store -- so the first consumer folded whatever the previous kernel call had left in the shared scratch (for `grpaint`, a `kSpriteSheet` directory offset) into its position. The loop now stores 0 *before* the primed test |
| #145 | 🟡 Medium | ✅ Fixed | `kSetGfxColor`'s selector was unbounded, and both of its writes then leave their block: the per-task offset is `selector*24 + curTask` from `taskGfxBorder` (`$4265`), so `X` = 4 stores to **`$42C5 + curTask`** — the REU filesystem state — and the focused-task hardware write `sta $D020,x` puts `X` 4-7 on **`$D024`-`$D027`**, the sprite 1-4 X registers. Unreachable in the tree (no caller passes `X >= 2`) and now bounded by `cmp #4 / bcs` placed *before* the multicolour gate, so the `X < 4` path is unchanged. Found auditing the call as a host for a per-cell colour write; the same pass fixed `gfx.asm`'s stale header (13 entries, base 12) and three stale call-index comments |
| #144 | 🟡 Medium | ✅ Fixed | preset 10's figures all came out about the same size (the user's "specific middle sized radius") because the bags were **sampled** with masked random draws, which at the montage's stride cluster -- 15 of 25 at the middle size against a bag asking for 12 at the largest. Both bags are now **walked with a counter** (figure count + a random phase drawn per run), giving exact ratios (12/6/4/3) and varied sizes; zero extra halvings is in the bag and is the commonest entry, so a third of the figures fill the screen height. That exposed a `ldx #0` + `dex/bne` = **256 iterations** trap in the halving loop, now guarded |
| #143 | 🟠 High | ✅ Fixed | preset 10 drew **horizontal lines on figures clipped at an edge**: `kLine`'s endpoints are unsigned 16-bit, so a point with a *negative* x ($FFxx, which `sgPoint`'s high byte allows) gives a delta of ~65,000 -- the walk goes the long way round with SEI held -- and `gxPix`/`gxPixReu` read that high bit as "x >= 256" and, for a low byte below 64, **plot at column 256+lo** instead of dropping the pixel, streaking the right of the row. `sgSegment` now **skips** segments with a negative endpoint (bit 7 of the high byte: exact, since x >= 0 is hi 0); the single-shape path is never negative, so presets `1`-`9` are untouched |
| #142 | 🟠 High | ✅ Fixed | preset 10 drew **stray horizontal lines** every run: `kArg` is neither per task nor carried across a switch, and the timer IRQ's per-switch cursor sync writes **`kArg+4`**, which is exactly `kLine`'s **x2 low byte** -- so a tick landing in the ~20 cycles between `sgSegment` loading a segment's endpoints and calling `kLine` corrupted the second x before `kLine` copied it, and `kLine`'s clipping trusts its input. `sgSegment` now wraps the load and the call in `sei` (kLine holds SEI anyway, so it costs ~20 cycles). No sprite task is involved -- `sprInstall` bails when the owner is unchanged. The other drawing tasks have the same unprotected window but draw in short bursts, which is why only the ~40-second montage showed it. Also: the three colours now **alternate** per figure instead of being drawn at random |
| #141 | 🟠 High | ✅ Fixed | preset 10 "rotated" each figure by starting **theta and phi at the same random angle** -- but phi runs at P/Q times theta's rate, so a shared delta changes the phase *between* the two rotating terms and draws a different, streak-like curve (horizontal lines). The phase is back to zero. Also as asked: the three colours are now three **independent** random distinct draws instead of a fixed five apart, and the lobe count is random but capped by the size -- scale first (`1` seven parts in eight, highest radii), then a bag of the four lowest lobe counts, with a halved figure taking the 5-lobe preset outright |
| #140 | 🟡 Medium | ✅ Fixed | preset 10 placed each figure with `base +/- (base - extent)`, a window that **shrinks as the figure grows**, so the biggest shapes could only land in the middle and the layout read as non-random; x is now uniform over the whole 160 columns (an overhang clips, x being 16-bit) and y uniform over the `200-2*extent` rows that fit (y cannot overhang -- `kLine` takes it as one byte, so a negative row would wrap to the bottom of the screen). Same pass: the scale bag favours the highest radii (`1` seven parts in eight) and the preset bag the fewest lobes (`1` 13 of 16, `8` 3), each figure starts at a random angle for a random rotation |
| #139 | 🟠 High | ✅ Fixed | `kRandom` shifts **left**, so bit *i* of one value is bit *i-1* of the previous one and a masked draw on a steady stride reads a sliding window of the same register: `& $03` for preset 10's figure colour returned 3 **not once in 1500 picks** (1 took 875, longest run 6), and `& $0f` drew the 12-lobe preset 522 times against 348 for the 5-lobe one, inverting the shape bag. New `sgRnd` returns `v ^ (v>>3) ^ (v>>6)`, so masked draws decorrelate (colours 464/516/520, longest run 3); full-byte draws like the placement's rejection sampling were already uniform and are unchanged. Bags retuned to high-radius, low-lobe presets at a quarter-screen size |
| #138 | 🟡 Medium | ✅ Fixed | preset 10's palette drew each colour freely and re-drew on a duplicate, which guarantees distinct VALUES but not distinct appearance -- 12 and 15, or 1 and 15, are one palette step apart and read as one colour; the palette is now built (one draw picks a base, the other two sit five apart, wrapping in 1..15), so the three are distinct by construction and the closest pair is five steps apart. Same pass: weighted preset and scale bags, so figures are rounder (mean 9.9 lobes, was about 22) and larger (51% at the biggest size, was 50% at the smallest) |
| #137 | 🟠 High | ✅ Fixed | preset 10's placement turned a random byte into an offset by **clamping**, so every rejected draw piled onto the far edge -- 31-59% of all figures at +x and +y together (the bottom right) with a mean of +30 instead of 0; it now rejects and redraws (edge share 0.5-1.0%, mean 0.0, 1.5-2.5 draws per figure). It also drew every figure stretched, because a multicolour pixel is twice as wide as it is tall and the x excursion equalled the y one; `sgPoint` now halves the x excursion in multicolour alone, with an arithmetic shift so negative offsets survive |
| #136 | 🔴 High | ✅ Fixed | `spirog`'s multicolour release passed an unset register as the bitmap mode (`sgSurface` used to supply its own), so `allocScreen` rejected it and every preset died via `SYS_EXIT` leaving the shell up; `SG_INK` was initialised on the single-shape path only, so the montage drew with pixel value 0 = background; and the cell-paint row pointer's `lda #0` wiped its own accumulator, so matrix rows and colour RAM rows disagreed and the figures' cells stayed black. Each path now sets its mode, `sgTrace` starts the ink, and the row pointer reloads the shifted value |
| #135 | 🟠 High | ✅ Fixed | `spirog`'s x path treated the carry out of an 8-bit add as a high byte, so a negative pen offset (a two's-complement operand) came out as `value + 256` and `kLine` clipped it -- every shape drew as its right half with long horizontal strokes; the offset is now sign-extended into a real 16-bit add. The identical expression sat two lines away in `sgMontFit`'s x centre, where it confined preset 10 to the right half of the screen |
| #134 | 🟠 High | ✅ Fixed | `spirog`'s argument parser skipped spaces instead of ending a number at them, and its x10 accumulate folded the first `adc`'s carry into the second -- so `run spirog 110 45 70` parsed as one value (R = 7 -> usage line) and the triples that "worked" drew wrong shapes with r/d/colour at their defaults. Now two-phase (spaces only *before* a number, any non-digit *ends* it) with a saturate before the multiply; the same release fixes the usage screen's second line (kGfxString clobbers kArg) and adds the #83 blink-reset window before `kSwitchView` |
| #116 | 🔴 High | ✅ Fixed | With `string.lib` unloadable, `pool` printed `? could not load string.lib` four times (one per `dyHex` call in its header) and then finished the table with stale hex digits — Bug #115's soft return kept the shell alive but let the command carry on. `libFailExit` now re-enters the shell **at its own entry with a fresh stack** (`taskLo/Hi[curTask]`, SP `$F7`), so the command is abandoned with the message printed once and the prompt returns; funded by collapsing the stubs' duplicated fail labels into shared trampolines (153 -> 171B free) |
| #115 | 🔴 Critical | ✅ Fixed | A failed lazy-library load ending the task (#114) was fatal for the shell itself — it owns the display and is the only interpreter, so `fsinfo`/`save`/`load`/`run *.bat` after an eviction-and-restore (which drops loaded-but-unloaded libs) could exit it and blank the machine; `libFailExit` now identifies the shell by its parent link (`taskParent == 0`, the boot slot, which cannot be recycled) and returns C=1 to it instead of exiting it |
| #114 | 🔴 High | ✅ Fixed | A failed lazy-library load printed `? could not load <name>.lib` and let the command carry on — C=1 from the cold stub is indistinguishable from an ordinary library result, so callers (`banner`, `del`, `dir`, `format`, `ps`, `rename`) misread it as "empty"/"skip" and used stale or garbage data; `libLoadFail` now ends the command task via `libFailExit` instead of returning |
| #113 | 🔴 High | ✅ Fixed | `spawn`'s ZP-window guard computed `zpBase + zpLen` in 8 bits and tested only the truncated sum against `$f1`, ignoring the carry — a header like `zpBase=$F5, zpLen=$0C` (true sum 257) wrapped below the threshold and was wrongly accepted, letting the per-task ZP save DMA alias and corrupt the kernel's own reserved zero page every context switch |
| #112 | 🔴 Critical | ✅ Fixed | `dlrNameSetup`'s name-copy loop capped at 11 chars before appending `".tsk\0"` (5 bytes) into the 12-byte `dynNameBuf`, overflowing into `reentFirst`/`reentResIdx`/`dlRegSlot` for any restored dynamic-task name of 8+ characters — `dlRegSlot` going out of range then misdirected every subsequent dyn-table access in that restore; cap lowered to 7 chars so the suffix always fits exactly |
| #111 | 🔴 High | ✅ Fixed | `run basic` never started after `edit` had run — `dlPreEvict`'s fit test checked a free-page *count* but a load needs a contiguous *run*, so eviction stopped early on fragmented space, placement then failed, and the caller had already been sacrificed with nowhere to report the error; the loop now also probes for a contiguous run and `dlSelfEvictOk` refuses a sacrifice that can't help |
| #107 | 🟡 Medium | 🟡 Open | `edit` on a file over 192 lines (`MAX_LINES`) loads silently truncated — the load loop's two full-document exits return with no status, so nothing distinguishes "file ended" from "document full," and `STOP+S` then writes the shortened document back over the original |
| #59 | 🟡 Medium | 🟡 Open | `kill` after `run basic` loses the shell — the display switches to `basic` on `kill 2` and exiting `basic` lands on a blank screen with the shell never restored; not yet root-caused (victim selection / kill cascade needs a live trace) |
| #108 | 🔴 High | ✅ Fixed | The shell parked its foreground child slot in `kArg+4` — the global DMA length byte — so `pcFgWait` polled the wrong `taskState` slot after `compact`: an ALIVE leftover (0 or 1) meant the console never woke again, a dead one meant the shell resumed while `compact` was still running (garbage echo). The slot now lives in the shell's instance buffer, and `surfDisplayed` keeps its value in its own byte instead of the kArg block |
| #93 | 🟡 Medium | ✅ Fixed | A voluntary `yield` was swallowed at priority > 1 — `switchTask`'s per-task slice counter is right for a preemption and wrong for a hand-over, so `semWait`'s "yield and retry" polled with a full context save/restore per retry; `sysYield` now forces `taskSliceLeft = 1` (and page-aligning the kArg shadow paid for it, plus a relocation that gave the core band its headroom back) |
| #92 | 🟡 Medium | ✅ Fixed | A warm reboot did not reclaim the libraries — `start:` reset everything *except* the library tables, so a loaded library survived (stale `pool` listing) and its patched `libJumpLo/Hi` entries still pointed into pool pages the fresh bitmap calls free → `warmBootReset` is now the warm-reboot entry point (and it also drops the sprite intent, which no per-frame gate may destroy) |
| #91 | 🟡 Medium | ✅ Fixed | `kLibHold` stored the kept id as the mask *before* releasing, so `dlLibRelease` freed the library it was told to keep and left it pinned at refcount 0 (the second attempt complemented the dyn slot instead of the bit) |
| #90 | 🟡 Medium | ✅ Retired | Two concurrent sprite tasks shared the register set and the art slots — **retired 2026-09-19** by per-task sprite state: the kernel keeps one task's state live (shadow, sheet records, frame indices) and swaps it with the display, so `godzi` and `sprdemo` run together (`docs/lib-sprite.md`) |
| #89 | 🟡 Medium | ✅ Fixed (superseded) | A recycled task id made a dead task's sprites reappear — `spriteOwnerGate` tested the owner's liveness **before** the focus compare; the gate is **gone** since 2026-09-19 (state is per slot, zeroed on reuse) |
| #80 | 🔴 High | ✅ Fixed | Loader lib-scan: the partial 2-of-3-byte `reuFsPtr` stash in `dlPreEvict` left a stale REU bank byte once a lib moved above FS `$10000`, so a task's fixup table was read one bank high → error 1 → warm reboot on `run basic` |
| #71 | 🟡 Medium | ✅ Fixed | Shell self-evicted during a failed `run basic` — the caller is now protected in pass 1 of `dlPreEvict` and only sacrificed when that alone makes the load fit (else error 4 with the shell alive), plus a `dlRestore` safety net so a self-evicted failure never warm-reboots |
| #79 | 🔴 High | ✅ Fixed | `sysKill`'s cascade killed thread children without `dynFree` — leaked their pool pages, lib refcounts and (permanently) their dyn-table slot, so `dynTaskCount` never came back |
| #78 | 🔴 High | ✅ Fixed | Failed eager-lib load over-decrements lib refcounts → a shared lib is freed early and/or pinned at `$FF`, stranding its pool pages (looks like a leak) |
| #77 | 🟡 Medium | ✅ Fixed | `compact` `? busy` listed only the first blocker — `cmpDec16` clobbered `CMP_T0`, Pass 0's scan slot |
| #76 | 🟡 Medium | ✅ Fixed | `compact` printed the allocator offset as "free" — now reports `capacity − nextFreeOffset`, matching `dir` |
| #75 | 🔴 High | ✅ Fixed | `compact` numbers emitted as screen code `$E2`+ — `ldCY` clobbered A in `cd16Put`/`cmpHexPut` (plus `CMP_CUR` cursor aliasing) |
| #24 | 🔴 Critical | ✅ Fixed | `totalTasks` overflow on dead-slot reuse → system halt |
| #25 | 🔴 High | ✅ Fixed | `taskNamePtr` ZP race between shell instances |
| #26 | 🔴 High | ✅ Fixed | Spawn parameter race (`spawnPtr` / `taskNamePtr`) |
| #27 | 🟡 High | ✅ Fixed | Bump allocator leaks on every task death |
| #28 | 🟡 Medium | ✅ Fixed | Screen state inheritance on slot reuse |
| #29 | 🟡 Medium | ✅ Fixed | Screen leaked on `kill` — kernel doesn't free |
| #30 | 🟡 Medium | ✅ Fixed | `kAllocScreen` treats dead tasks' screens as occupied |
| #31 | 🟢 Low | ✅ Fixed | Shell falls through without screen lock |
| #51 | 🔴 High | ✅ Fixed | `kByteToDec` digit corruption — `pool` printed "144 pages, 22 used, 88 free" |
| #52 | 🔴 High | ✅ Fixed | Task holding the screen mutex deadlocks `kill` (fptest never unlocked) |
| #53 | 🔴 High | ✅ Fixed | `fp.lib` — `fact_3` not contiguous with `fact_1/2` broke the ≥8-shift path (fptest T08/T11) |
| #54 | 🔴 High | ✅ Fixed | `fp.lib` — `fpCmp` passed internal fac2 to `lab_27f8` without the sign-merge (fptest T12) |
| #65 | 🔴 High | ✅ Fixed | `drFound` stored the clobbered `dlLibMask` — eviction during `run basic 10` wiped basic's fp bit, so the eager fp.lib never loaded |
| #66 | 🔴 High | ✅ Fixed | Contiguous code+RAM block couldn't fit when a self-declared lib straddled the pool (`run basic 4` after `run fpdemo` → warm reboot) |
| #67 | 🔴 High | ✅ Fixed | Eager libs loaded after code+RAM fragmented the pool (`run basic 3` → warm reboot) — `dlPreEvict` pre-evicts to the full footprint |
| #68 | 🔴 High | ✅ Fixed | Declaration-time eager refcount pinned `gfx.lib` (`run basic 4` after `run fpdemo` → warm reboot) — eager libs refcounted at load, not declare |
| #40 | 🔴 Critical | ✅ Fixed | Dynamic loader `kArg+8` aliased `savedSP[0]` — scheduler clobbered `fixup_count` |
| #16 | 🔴 Medium | ✅ Fixed | `kPrintAt` — does NOT preserve X (copilot instructions fixed) |
| #17 | 🔴 Low | ✅ Fixed | Second BIT trick in cursor blink (`kbDone`) |
| #18 | 🟡 Low | ✅ Fixed | Slot 0 permanently wasted (now recycled) |
| #19 | ⚫ Low | ✅ Fixed | `sysSpawn` print messages now atomic (lockScreen) |
| #20 | ⚫ Info | ✅ Fixed | Redundant `sei` in syscall handlers (4 bytes saved) |
| #21 | ⚫ Info | ✅ Fixed | `future-improvements.md` stale (REU fallback removed) — the completed-features section that carried the claim has since been deleted outright (#36) |
| #22 | ⚫ Info | ✅ Fixed | `kViewScreen` documented in kernel.md |
| #23 | ⚫ Info | ✅ Fixed | `$D018` must use kAllocScreen (documented) |
| D1-D6 | ⚫ | Open | Duplicate / dead code |
| C1-C5 | ⚫ | Open | Concision opportunities |

### Fixed / Won't Fix

| ID | Severity | Status | Title |
|---|---|---|---|
| #10 | 🔴 Medium | ✅ Fixed | `div16` — quotient high byte never incremented |
| #11 | 🔴 Medium | ✅ Fixed | `time set` — AM/PM flag inverted |
| #12 | 🔴 Low | ✅ Fixed | `semSignal` — no overflow guard |
| #13 | 🟡 Low-Med | 🚫 Won't Fix | `findNextAlive` — infinite loop if all tasks PAUSED (by design) |
| #14 | ℹ️ Info | ✅ Fixed | `kPrintTime` — BIT trick removed (pure 6502 branch) |
| #15 | 🟡 Low | ✅ Fixed | `findTaskArg` — overflow check on decimal parse |
| #1 | 🔴 Med-High | ✅ Fixed | `kPtr` corruption in IRQ path |
| #2 | 🟡 Medium | ✅ Fixed | `kArg` shared-state race |
| #3 | 🟡 Low-Med | ✅ Fixed | `kReadString` state corruption before focus check |
| #4 | 🟢 Low | ✅ Fixed | `kPrintTime` overlaps with `kResult` |
| #6 | 🟢 Maint. | ✅ Fixed | Duplicated command-parsing code |
| #7 | ℹ️ Info | ✅ Noted | `sysGetTid` CLI order (not a bug) |
| #8 | ℹ️ Info | ✅ Noted | `clearScreen` dey-first loop (not a bug) |

---

