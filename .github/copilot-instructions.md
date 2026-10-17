# GordonOS Copilot Instructions

> These rules prevent the most common bugs in GordonOS. Copilot must follow them strictly.

## Rule 1: ZP IRQ Safety

IRQ fires at ~60 Hz asynchronously. Never use IRQ-owned ZP in task-level kernel code.

| ZP | Label | Owner | Safe? |
|---|---|---|---|
| $F0 | blinkRate | Kernel (blink interval, set via `blink.com`) | ⛔ NEVER |
| $F1-$F2 | dataPtr | Task (scheduler saves/restores) | ⚠️ Scheduler saves/restores per-task |
| $F3-$F4 | taskNamePtr | Spawn (SEI) | ✓ |
| $F5-$F6 | kPtr | Kernel | ⚠️ Scheduler saves/restores per-task |
| $F7-$F8 | spawnPtr | Spawn (SEI) | ✓ |
| $F9-$FA | srcPtr | Kernel (scroll) | ⚠️ Scheduler saves/restores per-task |
| $FB-$FC | tmpPtr | Scheduler (IRQ) | ⛔ NEVER |
| $FD | curTask | Scheduler (IRQ) | ⛔ NEVER |
| $FE | saveSP | Scheduler (IRQ) | ⛔ NEVER |
| $FF | kbTemp | IRQ (keyboard) | ⛔ NEVER |

**Per-task ZP DMA preservation.** ZP `$02-$EF` can be preserved across context switches via REU DMA (`$F0` is a kernel global — `blinkRate`, never use in tasks). A task declares its region as `zp_base`/`zp_len` in its binary header (`+8`/`+9`). The scheduler DMAs the declared region to/from a per-task REU shadow on every switch. Tasks without a declared region pay zero overhead. See `programmers-guide.md` for details. ZP `$00-$01` are 6510 port registers — never use them.
  ⚠️ **The shadow is per SLOT, and it is zeroed when a task is spawned** (`clearSlotShadows`, `src/kernel/slot-shadow.inc`, 32B at `$5482`, called from `scSetAlive` in `syscall.inc`). Its content is only ever written by the switch-away DMA, so it outlives the task that owned it: without the zeroing fill, a task landing in a recycled slot had the previous occupant's ZP DMA'd into its own declared window on its first switch-in — and ids are recycled lowest-first, so "the slot the task you just killed was using" is the normal case (see `docs/bugs.md` #97). ⚠️ The **kArg shadow** (`REU_KARG_BASE + slot × 8`) has the same per-slot-stale shape and is **deliberately not cleared** — `kArg` is a calling-convention scratch every kernel call documents as clobbered, and its 8B fill does not fit the 41B pocket. Marked **BENIGN / WON'T FIX**: do not "fix" it opportunistically; `docs/bugs.md` #99 carries the recipe for the day a relocation makes it free, or a symptom appears.

## Rule 2: Disable Cursor During Screen Output

All kernel output routines (`printChar`, `printString`, `newline`, `scrollScreen`, `clearScreen`) save cursorOn, set it to 0, do work, restore. When writing new screen output code, call existing kernel routines or manually save/restore `cursorOn`.

**⚠️ Never print into the LAST CELL of the LAST ROW of a scroll region with a cursor-carrying writer.** `printChar`/`printGlyph`/`newline` wrap immediately (`inc cursorRow` → `wrapClamp`, `src/kernel/scroll-region.inc`), and `wrapClamp` scrolls the region when `cursorRow > bottom` — it clamps at `== bottom` and does nothing. So a routine that fills the bottom row to column 39 **scrolls the whole surface up one row as it writes that cell**, and no re-positioning afterwards undoes it: the top row of whatever was on screen is gone. Writing row 24 itself is fine (the wrap lands on 24 = bottom); it is the 40th cell of it that is out of bounds. `kPrintGlyphAt`/`kPrintCharAt` are the **fixed-cell** writers that sidestep the hazard completely -- they write one cell and advance, wrap and scroll **nothing** -- and `gortris` draws its entire frame, row 24 included, through `kPrintGlyphAt` for exactly this reason (see the `GT_FRAME_LAST` note in `gortris.inc`).

## Rule 3: Preserve Registers Across Subroutine Calls

**Definitive clobber table — verified from source code (kernel.asm). Always consult this before writing code.**

### Kernel routines

> **✓** = clobbered  |  **—** = preserved  |  **⚠️** = conditionally clobbered

**X is the column that bites. A routine that does `ldx curTask`, `tsx`, or tail-jumps to one that does, clobbers X — and the flag it does it for is invisible in the signature.** `kSetTextColor`, `kSetCursorColor`, `kEnableCursor` and `kDisableCursor` are the four that do this, so a value in X must be reloaded after them, never carried across them. Before holding anything in X across a kernel call, check that call's own source for an X load.

| Routine | A | X | Y | kPtr | srcPtr | Notes |
|---|---|---|---|---|---|---|
| `kClearScreen` | ✓ | ✓ | ✓ | ✓ | — | |
| `kPrintChar` | ✓ | ✓ | ✓ | ✓ | ⚠️ | X=old cursorOn. Scrolls region if row > bottom |
| `kPrintString` | ✓ | ✓ | ✓ | ✓ | ⚠️ | X via printChar. Scrolls region if row > bottom |
| `kSetCursor` | — | — | — | — | — | **Only routine that preserves X & Y** |
| `kGetCursor` | — | ✓ | ✓ | — | — | X=col, Y=row (outputs) |
| `kCursorAddr` | ✓ | — | — | ✓ | — | kPtr = screen addr at cursor; X preserved |
| `kNewline` | ✓ | ✓ | ✓ | ⚠️ | ⚠️ | Scrolls region if at bottom row |
| `kScrollScreen` | ✓ | ✓ | ✓ | ✓ | ✓ | |
| `kEnableCursor` | ✓ | ✓ | ✓ | ✓ | — | **X clobbered** — tail-jmps `csrSyncTail`, which does `ldx curTask` |
| `kDisableCursor` | ✓ | ✓ | ✓ | ✓ | — | **X clobbered** — tail-jmps `csrSyncTail`, which does `ldx curTask` |
| `kPrintAt` | ✓ | ✓ | ✓ | ✓ | — | X=old cursorOn (via printChar). Saves/restores cursor position |
| `kReadKey` | ✓ | ✓ | ✓ | — | — | |
| `kWaitKey` | ✓ | ✓ | ✓ | — | — | |
| `kScanKeyboard` | ✓ | ✓ | ✓ | — | — | |
| `kReadString` | ✓ | ✓ | ✓ | ✓ | — | kPtr advanced past terminator; Y=length |
| `kStrlen` | ✓ | — | ✓ | — | — | A=length, X preserved. string.lib (lazy) |
| `kStrcpy` | ✓ | — | ✓ | — | — | A=0, X preserved. string.lib (lazy) |
| `kStrcmp` | ✓ | — | ✓ | — | — | Z=1 equal, Z=0 differ. string.lib (lazy) |
| `kByteToHex` | ✓ | ✓ | ✓ | — | — | Y=1. string.lib (lazy) |
| `kByteToDec` | ✓ | ✓ | ✓ | — | — | Y=length (1-3). resident (kernel caller) |
| `kSkipSpaces` | ✓ | — | ✓ | ✓ | — | kPtr advanced; A=char (0=EOL), C=1 EOL. string.lib (lazy) |
| `kHexDigit` | ✓ | — | ✓ | — | — | screen code → nibble, C=1 fail. string.lib (lazy) |
| `kNibbleToHex` | ✓ | — | ✓ | — | — | nibble → screen code. string.lib (lazy) |
| `kMul16` | ✓ | ✓ | — | — | — | |
| `kDiv16` | ✓ | ✓ | ✓ | — | — | |
| `kRandom` | ✓ | — | — | — | — | |
| `kGetTime` | ✓ | — | — | — | — | Writes kArg..kArg+3 |
| `kSetTime` | ✓ | — | — | — | — | |
| `kPrintTime` | ✓ | ✓ | ✓ | ✓ | ✓ | X=col,Y=row → blits at (X,Y) via kBlitChar (displayed surface only, no cursor/shadow) |
| `kLockScreen` | ✓ | ✓ | — | — | — | Saves kPtr/kArg to per-task arrays |
| `kUnlockScreen` | ✓ | ✓ | — | ✓ | — | Restores kPtr/kArg from arrays |
| `kGetTaskName` | ✓ | — | — | ✓ | — | kPtr=name ptr, X preserved |
| `kAllocScreen` | ✓ | ✓ | ✓ | ✓ | — | |
| `kFreeScreen` | ✓ | ✓ | ✓ | — | — | |
| `kSetScrollRegion` | ✓ | ✓ | ✓ | ✓ | — | A=top, X=bottom; C=1 invalid (region unchanged); SEI/CLI; clobbers kArg..kArg+6 |
| `kGetScrollRegion` | ✓ | ✓ | — | — | — | A=top, X=bottom (outputs); C=0; Y preserved |
| `kSwitchView` | ✓ | — | ✓ | ⚠️ | — | X preserved (input). kPtr only if private screen |
| `kShareScreen` | ✓ | — | ✓ | — | — | X preserved (input) |
| `kTakeScreenOwnership` | ✓ | — | ✓ | — | — | X preserved (input) |
| `kSetTextColor` | ✓ | ✓ | ✓ | ✓ | — | **X clobbered** (`ldx curTask`); writes buffer tail |
| `kSetCursorColor` | ✓ | ✓ | ✓ | ✓ | — | **X clobbered** (`ldx curTask`); writes buffer tail |
| `kReuToC64` | ✓ | — | — | — | — | php/plp; X & Y preserved; kPtr read-only |
| `kReuFromC64` | ✓ | — | — | — | — | php/plp; X & Y preserved; kPtr read-only |
| `kReuFillC64` | ✓ | — | — | — | — | php/plp; kPtr read-only |
| `kReuFillReu` | ✓ | — | — | — | — | php/plp; kPtr & X preserved |
| `kCsrBoth` | ✓ | ✓ | ✓ | — | — | A=1 enable/0 disable for curTask; clobbers kArg..kArg+3; C=0 always |
| `kSetCharShadow` | ✓ | ✓ | ✓ | — | — | A=1 enable/0 disable for curTask; clobbers kArg..kArg+3; C=0 always |
| `kCharAt` | ✓ | ✓ | — | — | — | Input A=col, Y=row → A=screen code; C=1 + A=$20 if no surface/shadow; clobbers kArg+6/+7; Y preserved |
| `kPrintGlyph` | ✓ | ✓ | ✓ | ✓ | ✓ | Input: kPtr=8-byte glyph → blit at cursor, advance + colour; shadow = space; like kPrintChar |
| `kBlitChar` | ✓ | ✓ | ✓ | ✓ | ✓ | A=char, X=col, Y=row → font glyph + nibble `(textColour<<4)|bg`; no shadow, no cursor advance (displayed surface only) |
| `kBlitCharRev` | ✓ | ✓ | ✓ | ✓ | ✓ | A=char, X=col, Y=row → font glyph + nibble `(bg0<<4)|textColour` (reverse video); no shadow, no cursor advance |
| `kSpriteSheet` | ✓ | ✓ | ✓ | — | ✓ | A=slot 0-7 (8+ rejected — no reserved slot), `kPtr`=FS filename → C=1 rejected (not found / no whole frame); SEI'd; ≈1,000-3,000 cycles — resolve ONCE per sprite, never per frame |
| `kSpriteFrame` | ✓ | ✓ | ✓ | ✓ | — | A=slot 0-7 (8+ rejected), X=frame 0-255 → C=1 rejected; SEI'd; `sheet[slot] + frame*64` → 64B DMA into the slot. Neither call touches `kArg+8`+ and both avoid `kArg` on the API surface (X carries the frame) |
| `kSpritePos` | ✓ | ✓ | ✓ | — | — | X=sprite 0-7, A=x lo, Y=y, **C=x high bit** → C=1 rejected (8+, or not the display owner). Writes the shadow + the per-sprite X-MSB flag; never touches `$D000-$D02E` (the raster commit publishes it) |
| `kSpriteCollide` | ✓ | ✓ | ✓ | — | — | → C=1 (not the display owner), else A=`$D01E` mask, X=`$D01F` mask, both then **cleared** (frame-latched, OR-accumulated, service is the only `$D01E`/`$D01F` reader) |
| `kSpriteReg` | ✓ | ✓ | ✓ | — | — | A=value, X=shadow offset (bound-checked against `SPR_SHD_SIZE`) → C=1 rejected; the one setter for enable/colour/priority/MC-mode/expand |

### Syscall-layer routines

| Routine | A | X | Y | Notes |
|---|---|---|---|---|
| `shrinkTotalTasks` | — | ✓ | — | **Save/restore X around calls** |
| `syscall` (dispatcher), non-blocking handlers | ✓ | — | ✓ | X preserved (kill/pause/resume/setprio/semWait/semSignal take a target index in X); Y is clobbered — the table is indexed via Y; **`srcPtr` is clobbered** — the dispatcher puts the handler address in it for `jmp (srcPtr)`, so nothing survives a syscall there |
| `SYS_SLEEP` (13) | ✓ | ✓ | ✓ | ⛔ **`sysSleep` does `ldx curTask` — X is NOT preserved** (the dispatcher's X rule is about the dispatcher, not the handler). Park register inputs on your own stack across a blocking call: `kIpcPost` does exactly this, one byte under the parked data byte, and re-derives X with `tsx / lda $0102,x / tax` after the wait (see `docs/bugs.md` #94) |
| `SYS_YIELD` (3) | ✓ (=0) | — | ✓ | X preserved; **A returns as 0 and Y as `syscall#*2`** — never keep a live value in A or Y across a yield |

### Key rules

- **`kPrintChar`/`kPrintString`/`kNewline`/`kScrollScreen`/`kClearScreen` ALSO clobber `kArg`** (kPrintChar stores the char in `kArg`; scrollScreen uses `kArg..kArg+5`; clearScreen uses it for fill params; kSetScrollRegion uses `kArg..kArg+6`). Save/restore `kArg` around any screen output if its value must survive.
- **If you pass a value in X to anything except `kSetCursor`, `kGetTaskName`, `kSwitchView`, `kShareScreen`, `kTakeScreenOwnership`, `kCursorAddr`, `kReuToC64`, `kReuFromC64`, `kReuFillC64`, or `kReuFillReu`, it WILL be clobbered.** Of those ten, `kSwitchView`/`kShareScreen`/`kTakeScreenOwnership` preserve X around the work by saving and restoring it, so they are safe *as callees* — never as a place to park a value.
- **After any screen output routine** (`kPrintChar`, `kPrintString`, `kNewline`, `kScrollScreen`, `kClearScreen`, `kPrintAt`), X and Y are garbage.
- **`kReadString` ** — when curTask ≠ kbFocus, returns immediately with Y=0. Callers that loop on kReadString will spin without yielding. Always check/set kbFocus before blocking reads.
- **A routine that blocks must re-derive its register inputs afterwards.** `SYS_SLEEP` returns with **X = curTask** (`sysSleep` opens `ldx curTask`), so a loop that sleeps and comes back to an `abs,x` read is indexing with a task slot, not whatever you passed. Park what you need on the task's own stack — `switchTask` saves and restores SP per task, so it survives the switch — and recover it after the wait (`kIpcPost` parks the byte *and* the channel, then does `tsx / lda $0102,x / tax`). Doing this consumed caller `kArg+2` (6 bytes smaller) was rejected because it silently eats a caller's argument slot; patching a store operand is worse — the operand would be *shared* between tasks. See `docs/bugs.md` #94.
- **`SYS_KILL` is NOT a plain `rts` — it runs a task restore inside the syscall and that restore clobbers `dataPtr`.** `sysKill` does `jsr dynFree` (`syscall.inc`), `dynFree` ends with `jsr dlRestore` (`dynload.inc`), and `dlRestore`'s own header lists **`dataPtr`** among its clobbers (`dlrNameSetup` stores `dynTaskNameLo/Hi,y` into it) alongside `kPtr`, `srcPtr` and `kArg..+7`. So any task code that keeps state across a `kill` must save it — the task's own **stack** survives (only the victim is torn down) — or re-derive it afterwards: `kill.com` pushes `dataPtr`/`dataPtr+1` around the syscall for exactly this reason (see `docs/bugs.md` #98). It only bites when a *swapped-out* task fits the pages the kill just freed, which is what makes it intermittent.
- **A THREAD child (`SYS_THREADSPAWN`) has no dyn entry and runs its PARENT's code pages — it must not outlive them.** Three layers cover it: `kill` cascades over the whole thread subtree; **pool eviction refuses to *swap* a task with live thread children** (`dlEvDo` scans the victim's children via `dlEvIsDynTask`, which keeps `kDynLoad` children out of the test, and takes `dlEvKill` — victim + `$FE`-sentinel fixpoint over its **thread** descendants only (`taskThreadNameReuHi`, the same test `kGetTaskName` uses) — instead), so do NOT "fix" eviction for this; and **`childGate`** (the last thing in the core, `$13AD`, called from `findNextAlive`) **holds any thread child whose parent is `TA_DEAD` or `TA_SWAPPED`** — that is what covers `exit`, which deliberately does not cascade, and the shared-reentrant co-instance the eviction guard cannot see (see `docs/bugs.md` #101). ⚠️ **A `$FF`/`$FE` in `taskParent` must ALSO hold, not run** (see `docs/bugs.md` #101 H3): `scOrphan` clears a stale link to `$FF` at slot reuse and `$FE` is the cascade's sentinel, so for a *thread* child — the only kind that reaches this branch — a missing link is proof the pages are no longer its parent's, not proof that it is safe. A held child is never *reclaimed*, so a task that spawns children must still reap them before exiting (`godzi`/`gortris`/`ipctest` do). ⚠️ The core's growth headroom is the 15B run `$13C5-$13D3` (`kernelCoreEnd` is `$13C5`, and `childGate` is the block immediately before it). Any core change needing more than that needs a **relocation** — and the roomy pockets below `$0801` (`$02A1-$02BF`, `$020D-$021E`) can never hold kernel code (PRG block order / VICE autostart).
- **Hardware sprite collision is PER-PIXEL, not a bounding box — do not describe it as a rectangle test.** `$D01E` is set when two sprite data sequencers output a non-transparent pixel at the same time (VIC-II article §3.8.2), so a shaped sprite collides where it is *drawn*: a ball's empty corners do not collide, and the report agrees with the eye. It is also **not confined to the display window** — the sequencers output in the border too, so sprite-sprite collisions are reported there. `$D01F` differs: it needs the graphics sequencer to emit a foreground pixel, and that output is switched off in the upper/lower border, so it cannot fire there. What the register cannot give is **the pair** (bit *i* = "sprite *i* touched someone", so `maskA & maskB` is a heuristic, exact only with two sprites enabled) or any geometry. Priority (`$D01B`) does not affect detection; a disabled sprite cannot collide.
  Rule 9 is about exactly this: read `docs/kernel.md`'s `kSpriteCollide` section, or the VIC-II article — never recall hardware behaviour.
- **`rasterCommit` has a hard deadline and a guard — do not remove either.** It opens with `lda $d012 / cmp #SPR_COMMIT_DEADLINE (30) / bcc rcGo / rts`: a commit that would land once the beam has reached the sprite commits **nothing**, because publishing a new X that late splits a visible sprite into two positions at once. With the guard the artifact is a one-frame **hold**, not a split. What ships as `sprdemo` is only the bouncing-balls demo -- the nine test-vector modes are UNBUILT and kept in `removed/sprdemo-modes.inc`, including mode 7, the tear procedure that re-measures this. Skipping also skips the collision latch — harmless, `$D01E`/`$D01F` are cleared *by reading* and accumulate until the next commit. A sprite in the top border (Y < 31) is still exposed: the deadline is a **fixed line**, not a per-sprite scan.
- **`sprShXHi` is state, not a latch.** The commit must **read** the per-sprite X-MSB flags (`ldy sprShXHi,x / beq rcXhiNext`), never `lsr sprShXHi,x` — that is a read-modify-write on *memory*, so it consumes the flag it just published and a sprite at x ≥ 256 is drawn at x−256 until the task calls `kSpritePos` again. It hides while every x is < 256, where all flags are 0 and destroying them is a no-op (the unbuilt mode 5 in `removed/sprdemo-modes.inc` demonstrates it). General rule: **a publish must not consume its source.**
- **Animate on `frameCount`, never on the scheduler tick.** The tick is 59.2 Hz and the display is 50.1 Hz, so a per-tick sprite animation is sampled **1.18 times per frame** — some frames show two steps, the next none. That is a stutter no amount of speed arithmetic removes. `frameCount` (`$CFFF`, published in `src/tasks/kernel-data.inc`) is incremented by `rasterCommit` at raster line 0 *before* the deadline guard: compare it for **change** (8-bit, it wraps — never for elapsed frames) and integrate only when it differs. Keep the `SYS_YIELD` — gating on the counter is not licence to spin. Smooth needs **both**: the frame clock fixes *sampling*, a fractional accumulator fixes *quantisation*.
- **All eight slots are task sprites — there is no reserved slot.** `SPRITE_TASK_SLOTS = SPRITE_SLOT_COUNT = 8` (the VIC's hardware maximum) and calls reject only slot 8+. Nothing between the two frame bands can hold frames: `$1000-$1FFF` is char ROM to the VIC (sprite fetches included) and `$2000-$3FFF` is the framebuffer. `$0FC0` is deliberately *not* a slot (relocation slack) — do not "reclaim" it.
- **Sprite state is PER TASK and swapped with the display — there is no one-sprite-task limitation.** The register set is still one global resource, but it is not shared: the kernel keeps exactly one task's sprite state live — the display owner's sheet records + 39-byte shadow + frame indices, `SPR_BLK_LEN` = 71B, as ONE contiguous run at `$CFA4` (`sprLiveState`) — and every task's copy waits in its own 128-byte REU block at `REU_SPR_BASE ($C400) + slot*128`, in the same order, so a hand-over is a save DMA, a load DMA and one 64B DMA per resolved frame. `sprInstall` (`$52DC`) does it and is called from **two** places, both needed: the timer tick (before `switchTask`, covering an F-key screen switch and a dead owner) and the top of every `sprite.lib` setter (so a write cannot land in the *previous* owner's live state before the next tick). A recycled slot's block is zeroed by `clearSlotSprite` (spawn path), which also forgets the install if that slot was the live one; `warmSpriteReset` (`$529E`, called by `warmBootReset`) clears the whole region at a warm reboot, because the **boot task** never goes through the spawn path. The publish is gated by ONE byte, `sprPubMask` ($FF = an owner is installed, $00 = none) — `rasterCommit` ANDs it into `$D015` and does no id compare at all. The four setters are `sprite.lib` (lib 7, eager, `libMask` bit 7) call entries — `$CDDE/$CDE1/$CDE4/$CDEA` — and `kSpriteCollide` stays resident because it reads the kernel's collision latch. `sprSlotOwner`, `sprShOwner`, `spriteOwnerGate`, `sprShadowInit` and the `? sprites busy` guard are **gone** — do not reintroduce an ownership mask or a task-side claim. `docs/lib-sprite.md`, `docs/kernel.md` → *Sprites*, `docs/bugs.md` #90.
- **A voluntary hand-over must force the switch — reset `taskSliceLeft` (see `docs/bugs.md` #93).** `switchTask` is shared by the timer preemption *and* by `sysYield`, and it decides advance-vs-stay from the per-task counter: `dec taskSliceLeft,x / bne svSameTask` → with slices left it restores the SAME task. That is correct for a preemption and wrong for a `yield`, which is a request to leave now. At priority 2-5 a yield is *swallowed* until the quantum expires, and since `semWait`'s busy path is `lda #SYS_YIELD / jsr syscall / retry`, a blocked task polled with a full context save/restore per retry. `sysYield` and `sysSleep` both do `ldx curTask / lda #1 / sta taskSliceLeft,x` before switching. **Any new routine that switches away on purpose must do the same** — and check whether it shares `switchTask` with the timer path before assuming a hand-over is unconditional.

- **An 8-bit immediate is SILENTLY TRUNCATED — `lda #1016` assembles as `lda #$F8`.** KickAssembler v5.25 takes the value modulo 256 with no error and no warning, so a constant that grows past 255 turns into a *different, usually smaller* number and the symptom looks like a logic bug. Anything that can pass 255 must be split (`lda #<x` / `lda #>x`) and counted in two bytes — `gortris`'s attract delay is. Give such a constant a name and a comment; the assembler will not tell you.

- **Decimal mode is only DEFINED for BCD operands — never hand a `sed`/`adc` pair a value you computed in binary.** `adc` under `sed` corrects a nibble sum over 9 by adding 6 **once**, so an invalid operand whose nibble sum passes `$0F` leaves an invalid nibble in the result; a `$30 + digit` printer then emits `:` `;` `<` `=` `>` `?` (nibbles `$A-$F`). So keep every addend valid BCD — add 2 a row in a loop instead of doubling the count — or keep the value binary and print it with a `gtPrint2Dig`-style repeated subtraction (see `docs/bugs.md` #95). Audit **every** caller when you touch a BCD add: the soft drop's `1`, the `gtPts` table and `gtLinesMade` are all valid BCD, so a hard-drop `asl` on a binary row count is the only trap.

- **`kPrintChar` ALWAYS advances one cell, so a path that repositions has to account for the advance it just caused — and an echo positioned by the CURSOR can drift from an index kept in a COUNTER.** `gortris`'s hi-score name editor keeps the cursor on `NAME_COL + gtHiLen`; the DEL path prints the `.` placeholder (which advances) and loops, leaving the cursor one cell past the next character's cell — and the next DEL derives its cell from `gtHiLen`, so it clears the placeholder instead of the letter. A DEL path therefore needs a second `gtHnCursor` **after** the print — do not "simplify" it away, or every key typed after a DEL echoes one cell right of its buffer slot. Note the opposite hazard in `gtHnAdd`: positioning *before* the echo prints the row, because `gtHnCursor` ends in `tay` and leaves A = the row (row 19 = screen code `$13` = `s`) (see `docs/bugs.md` #96).

- **A kernel call clobbers C — capture a carry IMMEDIATELY.** `lda #imm` leaves C alone; a `jsr` almost never does. Anything derived from "the last call's carry" must be tested with `bcc`/`bcs` (into a code-body byte, or `php`) *before* any other call — the routine that produced the carry and the routine you print it with are two different calls, and a test that prints `C=0` whatever the library returned is exactly this bug.

- **The table above is the `$E000` jump-table API — library calls are NOT in it.** Each library (`gfx.lib`, `string.lib`, `fp.lib`, `sid.lib`, `ipc.lib`, `filesys.lib`, `mouse.lib`, `sprite.lib`) documents its own clobbers, and the lazy ones are entered through a **cold stub that preserves A, X, Y, `kArg[0..7]`, `kPtr`, `dataPtr` and `srcPtr`** (`libColdSave`/`libColdRestore`, `libstubs.inc`), so a *cold* call may take its inputs in X. Never assume that of a *warm* call — the library's own header is the contract. `taskNamePtr`/`spawnPtr` are NOT preserved on the cold path. And **a library's variables are shared by every task that declares it** (one copy, in the library's pool pages), so state that belongs to a *caller* must be indexed by the caller (`curTask`) or kept in the caller — a library-private "last sample"/"position" byte is a cross-task race even at 60 Hz. The shared thing is the DEVICE, never the per-reader state.

- **An entry point reached by a `jmp` or a branch must not end in a tail `rts`** unless its own entry was a `jsr` — the `rts` pops the frame of whatever last *called* into that chain and resumes the wrong routine (a key handler that did it re-ran the task's whole init on every keystroke). And **a helper that returns an index must say which register holds it**: a caller that assumed the register its callee "happened to leave behind" indexed a table with the wrong value and locked the machine up.

- **The loader/pool internals are a third clobber domain, and X is the register that bites there too.** `allocFromPool`, `freeFromPool`, `markPagesUsed` and `dlAllocCode` all clobber X — the `poolBitCheck`/`poolBitSet`/`poolBitClear` helpers end in `tax`, and the search runs one per candidate page plus one per page marked. Their headers used to promise X preserved, and a caller that believed it stored a garbage `mallocPages`: a count too small strands pages in the bitmap with no owner, a count too large clears the pages *above* the block, i.e. another owner's, which the next allocation then hands out as code (see `docs/bugs.md` #182). So **a `kMalloc` block's page count is read back from the `kArg+4` echo** (re-installed on the `kmEvictAlloc` retry, where `kArg` itself is garbage), never from a register. And the general rule the sweep confirmed: **a routine that calls a `tax`ing primitive has already clobbered X** — its own header must say so, or it must hand X back the way `surfSlotToReu`, `shareScreen`, `takeScreenOwnership`, `wrapClamp` and `blitChar`/`blitGlyphFb` do (`stx addr … ldx addr`, or `txa/pha … pla/tax` around the call).

- **`kPlot` does not clip — the caller owns the canvas edge.** Its contract is an on-canvas pixel (`X` = col lo, C = col hi, 0-319 by 0-199) and nothing in `gfx.lib` bounds-checks it: x >= 320 wraps one *cell row* (a 320-byte wrap = eight raster lines down, at the far left) and a row >= 200 walks off the 25-entry row LUT. A caller whose art extends past the pointer — a brush blob, a glyph box — must clip every pixel it marks. In hires `kPlot` **keeps the cell's low nibble** (`(ink<<4) | paper`), so a task that composes a cell byte itself must do the same; in MC a cell's byte *is* the palette, so a mark has to **claim** the cell (give the nibble its colour source) instead of letting the cell's existing colour show through.

- **`kRandom` is a Galois LFSR whose state 0 is the lock-up, and the state is not task-visible.** It is seeded once at boot; to vary a run, **advance** the sequence (N x `kRandom`) — do not write the seed.

- **Page-align a per-task REU shadow base, and never un-align it.** `REU_KARG_BASE` is `$9700` so the scheduler can form the address as `task × 8` with **no add at all**: the base's low byte is 0 and `MAX_TASKS × 8 ≤ 256`, so `txa/asl/asl/asl` *is* the low byte and the high byte is a constant. A non-aligned base needs `clc / adc #<base` before the store plus a carry-propagating `adc #>base` after it — 5 bytes in each of the two sites, for arithmetic alignment does for free. Both facts are asserted in `main.asm` (`kArg shadow base is page-aligned`, `kArg shadow table fits one page`).

- **A growth pocket is not storage (see `docs/bugs.md` #93).** The kernel core's growth headroom is the run immediately after `kernelCoreEnd` — `$13C5-$13D3` (**15B**), pinned by `tools/verify-map.ps1`. Parking a routine at the bottom of that pocket leaves the whole core band without room, and the next core change fails the build with a block overlap. A routine reached by a re-pointed `jmp` is an ideal tenant for a free *run* elsewhere (`warmBootReset` sits at `$181D`, 26B of a 35B run) — put it there, not in the headroom the next change needs. `main.asm`'s `kernelCoreEnd <= $13D4` assert is the guard that fires when it runs out.

- **The binaries repo gets the FIVE APPLICATION GUIDES and nothing else from `docs/`, and every link in the published tree is verified.** `publish-binaries.ps1` copies the kernel PRG, the two REU images, `tools/README.binaries.md` (as `README.md`), `tools/quickstart.binaries.md`, `NOTICE`, `docs/LICENSE.md` (as `LICENSE.md`), `docs/img/`, and `$guides` — `edit.md`, `gordonpaint.md`, `gortris.md`, `grknoid.md`, `gordonbasic.md`. **A new guide MUST be added to `$guides` or it will not ship** (the rest of `docs/` is developer material and stays private; `docs/` is replaced on every publish, never merged). Each copied guide has its `*.md` links filtered: a target outside the shipped set becomes a bare `` `name.md` `` in code font, which is why **a guide must never rely on a link to `kernel.md`, `memory-map.md`, `lib-*.md`, `quick-start.md` or any other developer doc — state the fact inline instead** (the filter keeps the sentence readable, it does not make the document available). The publish then resolves every link in every published `.md` and **throws before committing** if one is left, so a dead link cannot reach the binaries repo; the neutralised names are printed as it goes. The screenshots are published under `docs/img/` because that is where BOTH spellings resolve — root-level files reach them as `docs/img/<name>` and a guide under `docs/` as `img/<name>` (the spelling its source already uses). The deployed set is listed in `docs/development.md` → *What the binaries repo receives*.

- **A task that takes the display with `kSwitchView` must un-draw the live blink block first, SEI-wrapped (see `docs/bugs.md` #83).** The block cursor is drawn into the framebuffer pixels, and `surfSave` copies the framebuffer verbatim into the outgoing surface's REU slot, so a block drawn at that instant is baked in and `dgsDone` then clears `cursorVisible` — the cell can never be restored and returns as a phantom cursor where the previous owner's cursor was. Only a pixel write removes it. Recipe: `php / sei / txa; pha / jsr kBlinkReset / pla; tax / jsr kSwitchView / plp` — no syscall inside the window (the dispatcher re-enables IRQs), and `kBlinkReset` clobbers A, X, Y, `kPtr`, `kArg+7`. The kernel's own switch paths (`doGfxSwitch`'s callers) still have this exposure.

## Rule 4: Branch Distance

6502 branches ±128 bytes. When target is too far:
```asm
; WRONG
    beq farLabel

; CORRECT
    bne skipJmp
    jmp farLabel
skipJmp:
```
But do this only if code can not be rearranged to make the branch short without changing the logic. If you are not sure, ask a human for help.

## Rule 5: String Comparison — Use srcPtr, Not Offset Tracking

When comparing against `lineBuf` at an offset, pre-compute a pointer and use the same Y for both:

```asm
    lda #<lineBuf
    clc
    adc offset
    sta srcPtr
    lda #>lineBuf
    adc #0
    sta srcPtr+1       ; srcPtr = &lineBuf[offset]
    ldy #0
cmpLoop:
    lda (kPtr),y       ; name char
    beq end
    cmp (srcPtr),y     ; lineBuf char — same Y!
    bne next
    iny
    bne cmpLoop
```

**Never** track a running offset with push/pop/inc — it corrupts across loop iterations.

## Rule 6: 6502 Instructions

- 65C02-only instructions (`phy`, `ply`, `stz`, `bra`) are **not available**
- Use `tya; pha` / `pla; tay` instead
- ⚠️ **There is no `(zp),X` addressing mode, and KickAssembler does NOT reject it — it silently assembles `(zp),x` as plain `zp,X`.** So `sta (srcPtr),x`, written to mean "index that pointer with X", emits `STA srcPtr,X` — a **zero page** store. `srcPtr` is `$F9`, so a 12-byte name copy through it ran over `srcPtr`, the IRQ-owned `tmpPtr`, `curTask`, `saveSP` and into the **stack page at `$100`**: the scheduler died and the machine wedged, with the task's surface left blank (allocated and displayed, but never filled — hence a whole screen of the paper colour). Only `(zp,X)` (indexed-indirect) and `(zp),Y` (indirect-indexed) exist, and only `LDA/STA/ADC/SBC/AND/ORA/EOR/CMP` can take them — there is **no `ldx (zp),y`**. To walk a buffer by a computed index, use **ONE `Y` for source and destination** (Rule 5's own pattern: `lda (src),y` / `sta (dst),y`); to index by `X`, either `txa`/`tay` first, or fold X into the pointer (`txa / clc / adc kPtr / sta kPtr / lda kPtr+1 / adc #0 / sta kPtr+1`) and keep `Y` at 0. This mis-assembles as silently as the 8-bit immediate truncation below — **check the emitted bytes** whenever a `(zp)` form looks unusual.

## Rule 7: .encoding

Strings with `.text` use `.encoding "screencode_mixed"`. Character literals like `#'$'` and `#'0'` give screen codes (identical to PETSCII only for `$20-$3F` — the letters are `$01-$1A`, not PETSCII's `$41-$5A`). Keyboard input produces screen codes — comparisons between typed input and `.text` strings always match.
- **A typed key is a SCREEN CODE, so "printable" is NOT `>= $20`.** The letters are `$01-$1A` (and `$41-$5A` with SHIFT held), so a range test written from PETSCII habit silently discards **every unshifted letter**. Test the letters explicitly; reject `$00` (the '@' key, shared with the F-keys), `$1B-$1F` (`S↑` `[` `]` `£` `←`) and `$80+` (`$80` DEL, `$81` RETURN, `$82` HOME, `$83` STOP, `$84`/`$86` CRSR).
- ⚠️ **`kReadKey` CANNOT report "empty", so a queue drain must read the full 16 unconditionally.** `kbReleased` pushes **`$ff` into the key queue for a key-UP**, and `kScanKeyboard` returns `$ff` for BOTH an empty queue and a popped release byte; `kReadKey` then folds it to 0. So `loop: jsr kReadKey / beq done` stops at the first **release** and leaves every newer **press** behind it — and a task that reads `keybHeld` (a level, i.e. any paddle/game loop) never drains the queue at all, so the first "press a key" screen afterwards sees the whole session's leftovers and answers itself (`docs/bugs.md` #125). Drain by **capacity**: 16 unconditional reads — the IRQ refuses a 17th entry, and every read consumes what it pops, releases included.

## File Change Rules

- All `.inc` files imported via `.import source` must be listed in `project-config.json` `sources` array for auto-rebuild
- Jump table entries go in `kernel.asm` at label `$E000` — **and the table's 91 slots are FULL** (`kMouseCall` at `$E0F6` is the last slack entry, before the filesys stubs at `$E0F9`). A new entry goes into the vector-block headroom (the `kStrcmp`/`kFillMatrix`/`kFpCall` way, `$E107`+) or into a `$CDDE`-class pocket, never by pushing the band. `src/kernel/jumptable.inc` is the authority for every address, and `tools/verify-jumptable-docs.ps1` checks the docs against it (and `tools/verify-inventory-docs.ps1` the call counts).
- A new dynamic task must be added to `tools/tasks.json` (`name`, `source`, `origin`, `fsName`, `kind`); the generated assembler files pick it up on the next build
- **NEVER place kernel code or data below `$0801`.** KickAssembler writes PRG blocks in ascending address order, and VICE's autostart ("direct RAM injection") treats the FIRST block's address as the start of the whole file — a block below `$0801` becomes the first block, the whole PRG gets injected linearly from that address (kernel core, data, jump table all misplaced; VIC registers clobbered), and the system shows a garbled blue screen instead of booting. The `$0801-$080C` BASIC stub must always be the lowest block (`reuRename` lives at `$0801+` for this reason).
- KickAssembler writes PRG blocks in ascending address order (see above). Check `build/gordon-os.prg`'s first two bytes after a build: they must be `$01 $08`.
- **A state transition may only move a task between the states legal for its CURRENT state — never overwrite `TA_SWAPPED` or `TA_DEAD` elsewhere (see `docs/bugs.md` #102).** `taskState` is not just a flag: for a swapped task it is the **only** record that its code pages went back to the pool (`sysPause`'s write is not paired with a reload), and for a dead task it is what keeps a recycled slot out of the run queue. `sysPause` opens with `beq pauseDone` (DEAD) / `cmp #TA_SWAPPED / beq pauseDone`, and `sysSetPrio`'s `priority 0` path is `beq sysPause` (never a duplicate store). Without those guards, `pause` *or* `prio <id> 0` on a swapped or dead task makes it look pausable/resumable — reachable from the shell (`ps` → `prio <id> 0` → `resume <id>`) and it runs freed pages. Any routine that writes `taskState` must carry the same guard.

- **A cascade's root belongs in its own byte, never in a shared scratch block** (`dlCascadeRoot`/`dlCascadeFlag`, two repurposed dead kernel-data bytes — see `docs/bugs.md` #103). `surfFreeSlot` clobbers `kArg..kArg+3` and `dynFree` clobbers `kArg+3..+7`, so a kill cascade may not keep its ROOT in `kArg`: the save/restore needed around `jsr surfFreeSlot` is 8B, sits between `skCascadePass` and `bne skCascadePass`, and pushes that branch past −128 (needing the invert+jmp form as well) — 11B against 9B of headroom. Do NOT delete the `surfFreeSlot` call — that leaks the victim's surface. Transferable rule: **a helper that uses `kArg` cannot be called from code where `kArg` is live state.**

- **A size change in the `$E000` band must be paid back IN PLACE — its labels are ABI (see `docs/bugs.md` #104 L8).** `$E11A-$EA70` holds `kernel-vars.inc` + `screen-io.inc` + `keyboard.inc`, and **17 asserts in `main.asm` pin labels inside it**: `keyrpt.com` pokes `keybRepeatLda`/`keybInitialLda`, and gfx.lib's `ffStk*`/`kCircle*`/`kEll*` scratch addresses (`$ECxx`/`$FFxx`) resolve into the same band. Any size change there shifts all of them and fails the build. The answer is to give the bytes back as an inert sled — `.fill 8, $ea` (NOPs, never `$00`: that is a BRK) — the same "keep the size, spend nothing" trick `kernel-vars.inc`'s 16B `hexChars` block uses. So: changing size here means updating the asserts AND every external consumer (task-side aliases, `keyrpt`, docs) in the same commit, or padding.

- **A reentrancy record's identity must be DERIVED, never stored as a pointer (see `docs/bugs.md` #104 M4).** A record must not cache the `kPtr` that `dlCopyName` wrote — that is one instance's **dyn-slot name buffer**, and it is rewritten when the slot is re-used, which happens as soon as the FIRST instance dies while a co-instance keeps the record alive (`resRefCount` stays ≥ 1, so nothing tombstones it). The key would then read the new occupant's name: `run shell` matches nothing and loads a second full copy, and a second reentrant binary could share the **wrong entry**. `findLiveByName` asks the live instances instead: scan dyn slots for `dynTaskId != $FF` **and** `dynTaskResidency != $FF`, then compare that slot's own name buffer. It is sound because `dlCopyName` gives every instance its own buffer and `dynRegister` only reuses a slot whose `dynTaskId == $FF` — so a tagged live slot *is* the name. Two rules generalise: when you cache a pointer to mutable per-slot storage, cache nothing at all and re-derive from the owner; and a tombstone/detach pair must clear the tags too, or a half-torn-down slot looks live (that gate is also what makes a stale tag harmless).

- **A shift that feeds an address must keep its carry — `clc` before the base add throws the overflow away (see `docs/bugs.md` #105).** `sysThreadSpawn` builds the thread-name REU address as `slot × 16 + REU_THREAD_NAME`, and that base's low byte is `$00` (now asserted): the shifted value *is* the low byte, so nothing may add to it and the ×16 overflow — only slots 16-23, the ones past `$C0FF` — has to ride **C** into the high byte via `lda #>base / adc #0`. The old `clc / adc #<base` was a no-op add that cleared C, so slots 16+ wrote their name 256 bytes low, over the child 16 slots earlier (`ps` showed them each other's name). `STA` leaves C alone, which is what carries it. The sibling idiom a few lines up (`clc / adc #<nameBuf / lda #>nameBuf / adc #0`) is the case that NEEDS its `clc`, because `nameBuf` is a C64 address with a real low byte — so check *whose* carry you are propagating before removing one.

- **A task-side `kMalloc` may evict.** When the pool cannot satisfy a `kMalloc`, `kmEvictAlloc` (`$E201`, the dead 16-byte `hexChars` block in `kernel-vars.inc`) calls `dlEvict` with `dlNoSelfEvict = 1` and retries `allocFromPool` — the **caller is never the victim** (it is executing its own code; freeing that mid-call is a crash, not a graceful fail). C=1 means "no victim either". The victim may be swapped out **or killed** (its thread children), and `dlEvict` clobbers `kArg..+7`: hold no kernel state across a `kMalloc`. Only the pool-full site is hooked — invalid-input, per-owner-cap and table-full failures must NOT retry. The routine lives in that dead block on purpose: its 16 bytes are reserved for address stability (the gfx.lib scratch asserts pin everything after it), so it costs the tree **zero free space and zero relocation** — when you need a home for small kernel code, look for reserved-but-unused bytes like this before taking a growth pocket.
- **A warm reboot must reclaim the LIBRARY system and drop the SPRITE intent (see `docs/bugs.md` #92).** `start:` resets the pool bitmap, dyn tables, residency records and surfaces, but kernel *data* otherwise survives `jmp start` — and the library tables are kernel data. Left alone, a loaded library survives the reboot (stale `pool` listing) and, far worse, its patched `libJumpLo/Hi` entries still point into pool pages the fresh bitmap calls **free** ⇒ the next task loaded there is entered through a library entry, into the wrong code, with no cold stub left to trap it. `warmBootReset` (`src/kernel/warm-boot-reset.inc`, `$181D`, 26B) **is** the warm-reboot entry point (the scheduler's `jmp start` points at it); it calls the loader's own `libFree` for **every** id (count-agnostic on purpose — the case to repair is a *loaded* library at refcount 0, see `docs/bugs.md` #91; `libStartPage = $FF` returns immediately), clears the refcount, calls `warmSpriteReset` (which unpublishes, forgets `sprLiveOwner` and zeroes every task's REU block), then `jmp`s to `start`. **The same `warmSpriteReset` runs at COLD boot** (`init.inc`): VICE writes the REU image back (`-reuimagerw`), so the per-task sprite blocks in it survive to the next session, and slot 0 is the one slot that never goes through the spawn path's `clearSlotSprite`. Reached by re-pointing an existing jump, so the *call* costs the core band nothing. ⚠️ Do NOT put a routine at the BOTTOM of a growth pocket: that leaves the whole core band without room and makes the next core change fail the build with a block overlap. Free total is **127B across 22 pockets**; `tools/verify-map.ps1` pins the totals, the reset pocket (`$529E-$52C0`), the install pocket (`$52DC-$5336`) and the core headroom (`$13C5-$13D3`, **15B**).

- **A warm reboot must clear every per-slot pointer into the instance heap (see `docs/bugs.md` #108).** `start:` rewinds `dataHeap` to `$1D40`, so anything that *points into* the old heap and survives `jmp start` aliases memory the fresh bump allocator is about to hand out. The free list is cleared for this reason; the per-slot instance-buffer records (`taskDataSize`/`taskDataPtrLo/Hi`) need the same treatment. Without it, a spawn whose `dataSize` matches a stale record takes `scAlloc`'s reuse path (handed the old buffer, heap NOT advanced), the next size-mismatched spawn takes `scFresh` and is issued `dataHeap` *inside that same buffer*, and both slots share it -- so `scZeroBuf` zeroes the shell's instance buffer, whose offset 0 is `LINEBUF`. Symptom: keys echo but every line reads empty and no command ever runs again (warm-reboot sessions only, and it takes a task with a new size to trigger). `warmClearBufRecs` (`warm-boot-reset.inc`, 17B at `$CA1B`, `jsr`-ed from `warmBootReset`) clears them. General rule: **when a reboot rewinds an allocator, every record that points into it must be cleared in the same pass -- and a routine that has to grow must take bytes from a free run, not from a pinned band.**

- **Failing `.assert`s must fail the build.** KickAssembler prints `ERROR IN ASSERTION` / `Made N asserts, M FAILED!` on a failed `.assert` but still exits 0, so every KickAssembler invocation is wrapped by `tools/kick.ps1`. Dynamic-task builds call it from `tools/build-tasks.ps1`; kernel builds must use `tools/build-kernel.ps1`, which patches VS64's generated `build/build.ninja` asm rule through `tools\kick.ps1` before running Ninja.

## Rule 8: Kernel Calls

- If there is a kernel call that does the job asked use it instead of duplicating code 

## Rule 9: Guessing

- Never make assumptions on how c64 hardware or VICE emulator works. If you are not sure, check documentation on the web or do a google search.
- To settle what a C64 binary actually does, disassemble it rather than infer it:
  `tools/dis6502.py --entry XXXX` (recursive, with a hardware-touch report),
  `tools/dis6502-linear.py` (py65, the independent cross-check) and `tools/scan-prg.py`
  (address byte pairs + strings, for code the recursive walk cannot reach).

## Rule 10: Documentation
- After every update, where applicable, update the documentation files in the `docs` folder. There are also ones in the tools folder that must also be kept in sync where appropriate. If you are not sure what to update, ask a human for help.
- Documentation describes the system as it is NOW — see Rule 14.

## Rule 11: Concise Code
- You are writing code for a 6502 CPU, which has very limited resources. Always try to write the most concise code possible. If you are not sure if your code is concise enough, ask a human for help.

## Rule 12: Use Existing Code
- If there is existing code that does what you need, use it instead of writing new code. Any new kernel routine should use existing more granular kernel routines where possible. If you are not sure if there is existing code that does what you need, ask a human for help.

## Rule 13: This is a multi-tasking OS
- GordonOS is a multi-tasking OS. This means that any code you write must not produce any unexpected side effects that could affect other tasks. If you are not sure if your code has any side effects, ask a human for help.

## Rule 14: Comments and Docs Carry No History

**A comment states what is true NOW. Bug and fix history is recorded in `docs/bugs.md`, and nowhere else.**

- **No bug numbers, dates, plan phases or release steps in a comment.** `Bug #83`, `2026-09-13`, `Scope 0 step A`, `defrag Phase 2`, `Phase 0.3b`, `Step 4.5` belong in `docs/bugs.md`, not in the source.
- **Never narrate a change.** No "used to", "was moved", "no longer", "retired", "legacy", "previously", "originally", "the first version did X", "tried and reverted", "restored until ... is fixed". Write the rule the code obeys, in the present tense.
- **Keep the reason, drop the story.** A constraint earned by a bug stays: `// ⚠️ add 2 a row, never \`asl\` the count — \`adc\` under \`sed\` needs a valid BCD addend`. The bug number, the date and "fixed on ..." do not.
- **`⚠️` marks a constraint that is still alive**, not a war story. If the only content left is history, delete the comment.
- **A bug pointer is not wanted.** Bug numbers and pointers into `docs/bugs.md` belong in `docs/bugs.md` alone: no bug comment in `src/` or `tools/`, and no bug reference in any `.md` other than `docs/bugs.md`. A local plan document, kept outside this repo, may carry bug entries, and so may `.github/copilot-instructions.md` -- its `see docs/bugs.md #NN` pointers send the reader to the mechanism instead of copying it, and they are re-checked in the same pass that changes the code. A pointer to the *implementation* -- `docs/kernel.md`, a source routine -- stays: it sends the reader to the code instead of copying it into the tree.
- **Provenance stays.** A vendored port (`ehbasic.asm`, `sidplay.inc`, `basicedit.inc`, `filesys.asm`, …) keeps its origin, licence and "what the port changed" notes. Upstream markers such as EhBASIC's `//*** fix p2` are part of the source, not history about our work.
- **A diagnostic string is not a comment.** Text a routine prints or a build script echoes at runtime keeps its wording — only the comments around it are cleaned.
- **The same applies to every `.md` file** in `docs/` and `tools/`: describe how the system works today. No dated entries, no "was X, now Y", no running changelog, no "recently added". `docs/bugs.md` is the one place a bug, its symptom and its fix are written down.

## Rule 15: Check kernel documentation before making a kernel call
- Before making a kernel call, check the kernel documentation to understand its side effects. If you are not sure, ask a human for help.

## Rule 16: Build Sections Must Not Overflow
- When you do a build you need to make sure the section of the code you build does not overflow into another section, code or data. If you are not sure, ask a human for help.

## Rule 17: Do NOT write sloppy code
- Always code concisely and clearly. If there is a more concise way of writing code that does not change behaviour compared to a longer version, do it. If you are not sure if your code is clear enough, ask a human for help.

## Rule 18: When the user reports a problem
- Always try to get to the root cause of the problem. Do not apply any fixes until the user confirms.

## Rule 19: Code Comments Are Pure ASCII

**Every comment in code is plain 7-bit ASCII. Typographic characters live in `.md` files only.**

- Scope: `src/**` and `removed/**` (`.asm`, `.inc`), `tools/**` (`.py`, `.ps1`, `.json`), reference code (`.asm`), `.vscode/settings.json` -- line comments, block headers and Python docstrings alike. `.md` files (docs, README, this file) keep their own typography.
- Why: the toolchain is CP1252-hostile. KickAssembler runs under a Java default charset, PowerShell 5.1 round-trips text through the console codepage, and this machine's console cannot even encode `U+2192` (`UnicodeEncodeError: 'charmap' codec can't encode character`). A comment that survives one host tool and not the next reads as `a-hat-euro` garbage in an editor, a diff or a terminal -- and if the character is inside a `.text` string it becomes a WRONG SCREEN CODE on the C64, which is a behaviour bug rather than a cosmetic one.
- Write the plain equivalent: `--` for em/en dash and the minus sign; `->` `<-` `^` `v` `<->` `=>` for arrows; `<=` `>=` `!=` `+/-`; `x` for times, `/` for divide; `section N` for the section mark; `...` for the ellipsis; `WARNING:` and `NEVER-` for the warning and no-entry marks; `POUND` for the C64 pound key cap; `^2` for a superscript two; `PHI2` for the clock phase; `-` for a middle dot used as a separator.
- Never put a non-ASCII character in a `.text`/string literal: that is emitted data, not a comment, and it changes what the task prints.
- Check before you finish -- `python3 tools/check-ascii.py` must exit 0 and print `ASCII check: OK` (the script form of this scan, and a step of the CI `gates` workflow):
- Data files are exempt and stay byte-exact: `src/corpus/*.bas` (tokenized BASIC), `charsets/*`, `src/banners/*`, `music/*`, `reference/**/*.md`, and the REU image.
- A comment edit must never change emitted bytes: after such a sweep, rebuild the tasks and the kernel and confirm the `.bin`/`.prg` hashes are unchanged.

# GordonOS Dynamic Loader and Task-Build Rules

## Task binary ABI

- **Write `.word HDR_MAGIC`, never the literal `$474F`.** `.word $474F` emits bytes
  `$4F $47`, which is a bad magic. `src/kernel/task-header.inc` is the one home for that
  value and for every header field offset.
- **The code body is at file offset 256 and runs at `origin + 256`.** Task code is
  assembled from `.pc = origin` at the header start, so the loader must place the code at
  `origin + 256` (fixed) or at the pool page (relocatable) — anything else leaves every
  absolute reference wrong by the header size.
- **A `.tsk` entry label must be the FIRST label in the body.** The code-size field is
  `end - entry`, so a helper placed before the entry falls outside the declared size, the
  fixup offsets exceed it, and the loader rejects the file with error 1.
- **`kArg` is only 8 bytes.** `$4000-$4007` is `kArg(4)` + `kResult(4)`, and `$4008` is
  `savedSP[0]`, rewritten on EVERY context switch. **Never use `kArg+8` or higher** — it is
  clobbered during any CLI window. `kResult` must not be used while the `kArg` block is live.
- **`kArg+4` is the DMA length hi.** Every `kReuToC64`/`kReuFromC64`/`kReuFill*` caller
  writes it (`0` under 256B, `1` for a page), and `dynFree` -> `dlRestore` -> `dynParseHeader`
  leaves a flags byte there. The kArg block is legitimately clobbered by ANY task's kernel
  call: a task may use it only as a scratch *within one call* (set it, call it, read it),
  never as state that must survive a `yield`/`sleep` — which is why the shell keeps its
  foreground child slot in its own instance byte rather than in `kArg`.
- **The spawn syscall clobbers `kArg..kArg+3`** — park origin/size on the stack around
  `jsr syscall`.
- **`transferFocusAway` writes `kArg` as `fromTask`** when it hands the focus over, so state
  kept in the kArg block across a focus transfer is lost.
- **Register the spawned slot directly.** A name scan over `taskState` matches the FIRST
  instance of a name, so a second `run` of the same task registers the wrong slot. Keep the
  slot the spawn returned (in X) and register that.
- **A task field at an offset below 65 holds the loader's ARGUMENT bytes at spawn.** With
  `KF_TAKES_ARGS` the loader copies the command line into the instance buffer's first
  `min(MAX_TASK_ARGS=64, data_size-1)` bytes, so **every field the startup path does not
  explicitly set is STALE** — a leftover argument byte read back as a colour, a mode or a count.
  Set every such field on EVERY path, and say so in the comment.
- **`KF_RE_RUNNABLE` (`$20`) is a statement about the POOL, not about the code.** A second `run`
  skips the single-instance check and takes the full reloc path, where the pre-need sum is the
  code plus its **eager** library pages and counts a *resident* library in full — so the second
  copy can **evict the first** rather than share it, and `pool` hides a swapped row, so there
  appears to be only one. Flag a task re-runnable only when the pool can hold two copies.
- **`kLibHold` is one-way.** A = the MASK of lib ids to KEEP. A lazy library released this way
  reloads through its cold stub, but the reload is credited to nobody (refcount 0), and a
  refcount-0 library can never be freed again — its pages leak until a reboot. An EAGER library
  has no cold stub at all: its entries become `jmp libTrap`, so the calls go silent until a task
  that still declares it loads it again. Call it LAST, and release only what the task is done
  with for good.

## Loader internals

- **`kDynLoad` does SEI — CLI before EVERY `rts`, error paths included**, or the shell hangs
  with interrupts disabled (only saved when spawn -> scheduler -> RTI restores I).
- **`dphBadMagic` sits BEFORE the `dynParseHeader` entry label.** The two magic compares
  branch back to it, so placing it after the field block puts it out of branch range. See
  the `dynParseHeader` section in `docs/kernel.md` for the field-by-field layout.
- **`reuDmaRange` clobbers `kArg..kArg+5`** — push the size (`kArg+2/3`) on the stack before
  the code DMA and restore it after, or `dynTaskTable` gets garbage sizes (breaking both
  `ps` and the overlap checks).
- **`allocFromPool` clobbers `kArg+3..6`, flags included** — reload the flags from
  `scratchBuf+2` after the call. `markPagesUsed` clobbers `kArg+3/4` too.
- **`freeFromPool` takes the pool id in A**, not the page count — passing the count frees the
  wrong bitmap.
- **A helper that uses `kArg` cannot be called from code where `kArg` is live state.**
  `surfFreeSlot` and `dynFree` both use the block.
- **Seeding runs only when the REU image has no FS signature** (`$A5`); `format` and `del` do
  not re-seed on the next boot.
- **The REU filesystem persists binaries, and a RUNNING VICE holds `reu/REU.bin` open**
  (`-reuimagerw` maps it). So after rebuilding a `.tsk`/`.com`/`.lib`, re-seed the REU (or
  `del <name>` plus a reset) or boot loads the stale copy — and close VICE first, or
  `tools/build-reu.ps1` fails on that file with a user-mapped-section error. A correct rebuild of
  identical bytes shows **no diff at all**, so never judge whether it ran from timestamps or
  `git status` — run the task and read its output.
- **One pool.** `POOL_BASE = $5700`, `POOL_END = $C400`, page-granular. Tasks, program RAM
  and `kMalloc` allocate upward from the bottom; libraries come down from the top, so a
  library can never split the task region.

## Task build pipeline

- **The VS64 build task, not raw Ninja, is the real kernel builder.** `build/build.ninja`
  only lists `main.asm`; `project-config.json`'s `sources` array regenerates it, so an `.inc`
  pulled in with `.import source` must be listed there for auto-rebuild.
- **`tools/build-kernel.ps1` runs three tripwires before Ninja.** They exist for couplings no
  `.assert` can see: `check-fs-offsets.ps1` (24-bit REU FS offset arithmetic),
  `check-lib-ids.ps1` (`LIB_*` ids vs the positional descriptor tables in `main.asm`), and
  `check-header-offsets.ps1` (a task/lib header field read as a bare `+N` on `scratchBuf` or
  `reuDirBuf` instead of an `HDR_*` constant — the offset is a byte index into a DMA'd page,
  not a label, so the assembler cannot catch it). All three exit 1 on violation.
- **`tools/assemble-tasks.asm` and `tools/gen-fixups.asm` are generated.**
  `tools/gen-task-asm.ps1` rewrites them from `tools/tasks.json`; hand edits are silently
  reverted.
- **Imported task files share ONE KickAssembler label namespace.** Use a unique prefix per
  file — a label such as `binStart` collides across tasks.
- **`>` binds LOOSER than `+` and `*`.** `cmp #>BASE + PAGES` assembles as
  `cmp #>(BASE + PAGES)`, which is a different number. Write
  `.const X_END = BASE + PAGES*SIZE` then `cmp #>X_END`, and check the PRG bytes when a
  range test looks wrong.
- **`tsx` clobbers X** — park values elsewhere (for example in `kArg+4`) before a `tsx`.
- **KickAssembler conditionals are brace-form:** `.if (cond) { ... } else { ... }`.
  `.else`/`.endif` are not directives.
- **`.assert` with a boolean expression fails** — use `(cond) ? 1 : 0`.
- **A size change in the `$E000` band is an ABI change.** The gfx scratch addresses and
  `keyrpt`'s poke addresses resolve into it, and `main.asm` pins them with asserts. Either
  give the bytes back as an inert `.fill n, $ea` sled (`$ea` is NOP; never `$00`, which is
  BRK) or update every assert and external consumer in the same commit.
- **A routine that grows must take bytes from a free run, not from a pinned band.** The
  roomy pockets below `$0801` (`$02A1-$02BF`, `$020D-$021E`) can never hold kernel code —
  KickAssembler writes PRG blocks in ascending address order and VICE autostart treats the
  first block as the file start.
- **`$D020` debug traces are repainted by the scheduler every tick** — use a freeze-style
  trace (`sei` / `jmp *`), guarded so it skips the first boot instance.
- ⚠️ **Never pipe a build or gate script through `Select-Object -First N`.** Stopping the
  pipeline early kills the build half-way (three `Made N asserts` lines, exit 1, no fixup or
  relocation stage), and it replaces `$LASTEXITCODE` with the pipeline's, so a failing script can
  read as a pass. Use `Select-String` (no `-First`) or `-Last N`, and run a gate unpiped when you
  are checking its exit code.
- **PowerShell 5.1 traps around these scripts.** Variables are CASE-INSENSITIVE, so a local
  `$c64uOut` IS the parameter `$C64UOut` and the assignment silently disables the block. A native
  command's **stderr becomes a terminating error** under `$ErrorActionPreference = 'Stop'`, so a
  `git`/`ninja` warning kills a script with no message (run it under `Continue` and let
  `$LASTEXITCODE` decide). And `-creplace 'x'` with no replacement argument DELETES every match
  instead of doing nothing. And `>` writes **UTF-16LE**, so a redirected report comes back
  with a NUL between every byte and a text tool reads it as binary -- use
  `| Out-File -Encoding ascii` (or `Set-Content -Encoding ascii`).
- **Do not hard-fail a build on an aesthetic or convention check of art.** Gate on STRUCTURE
  (how many pictures, their size, where they land), never on what they depict — an artist's
  180-degree icon pair is a mirror in use, not a broken mirror, and failing a REU build over it
  blocks everyone.

> Screen-switch artifacts are a known cosmetic REU/VIC race — see `docs/bugs.md` #37.





## Pool and display placement

- **A task's live footprint is its code plus the libraries it holds while alive** — the
  eager ones its `libMask` names (loaded at spawn) *and* any lazy one its own startup
  touches early. The loader's pre-need sum counts the eager ones only (`dlEagerLibPages`),
  so an over-budget task **loads successfully** and then dies when its code first reaches
  the lazy library. `basic` is the tight case: 64 code + 27 `gfx.lib` + 8 `fp.lib` + 10
  `filesys.lib` (its cold start opens a REU working file) = **109 of 109 pages, margin
  zero**, so one extra page anywhere in that chain kills it (`docs/bugs.md` #173).
  `tools/check-pool-budget.ps1` runs in `build-tasks.ps1`, fails on an over-budget task and
  warns on an exact fit; `$LiveLazy` in it names the lazy libraries each task's own code
  loads while it is alive (and the gate fails if a row is not in that task's `libMask`),
  while `$LiveMallocMusic` / `$LiveMallocFixed` name the `kMalloc` blocks it holds (a tune
  block is recomputed from the file on disk), so the row is the task's real footprint.
- **`pool` cannot show a PLACEMENT problem.** Its rows (and therefore its `used`/`free`
  totals) come from the **tables** (`pcPool`: dyn tasks, program RAM, malloc, libs), never
  from `poolBitmap` — so a correct-looking total beside a failing load means the free pages
  are **not in one run**, not that space is missing. Compare the **`pg` column against a
  fresh boot** to judge a lib's home; `docs/bugs.md` #81/#82 carries the worked cases.
- **Libraries allocate downward from the top of the pool** (`dlAllocHi`: `dlLibCore` ->
  `dlAllocCode` -> `allocFromPoolTop`), while tasks, program RAM and `kMalloc` grow up from
  the bottom. Both directions run the same exhaustive candidate walk, so the fit decision is
  unchanged — nothing is reserved and no page is unusable — but a library can never land in
  a hole above a resident task and split the pool.
- **The caller-self-evict test must count the caller's last-holder LIBRARIES.**
  `dlSelfEvictOk` compares `free + dlCallerPages() >= dlPreNeed`, but eviction also runs
  `dlEvLibs` -> `dlLibRelease`, freeing every declared lib with `libRefCount == 1` — and with
  libraries parked at the top of the pool those pages are *outside* the free space, so a
  code-only count refuses loads that fit. `dlCallerAddLibs`/`dlCallerLibPages`
  (`pool-top.inc`) add exactly those (shared libs skipped).
- **A task that owns the display (holds `kbFocus`) must hand it over BEFORE `kFreeScreen`.**
  `sysExit`'s `transferFocusAway` runs *after* the task marked itself DEAD, so a dying task
  that is already surfaceless blanks the display and sets `kbFocus = $FF` — a state only the
  `kbFocus = $FF` arm of `transferFocusAway` reclaims, for the first ALIVE owner-surface
  task. So `edit` calls `edGiveDisplay` (`kSwitchView` to `taskParent`) before both of its
  `kFreeScreen` sites, and a live focus holder is never stolen.
- **A task's pool footprint changes when its code crosses a page boundary.** Keep a task's
  code clear of the next boundary unless the extra page is affordable.
- ⚠️ **Never park a temporary value in another routine's state field.** The editor's shift
  routines `edRunDn`/`edRunUp` must not use an `ED_*` field for their length: `edPaste`
  holds its clipboard read pointer in `ED_PTR` across `edInsLine`, so borrowing that field
  corrupts every paste made where records follow the insertion point. Park it on the task
  stack.
- ⚠️ **The `$ECDC-$FC3E` code chain (`gfx.inc` → `text-blit.inc` → `screen-mgmt.inc` → … → `lib.inc`) has 2 B of growth room (`$FC3D-$FC3E`) — the chain's whole growth budget, which only holds because two routines sit OUTSIDE the chain.** `gfx.inc` is `.pc`-anchored at `$ECDC` because gfx.lib hardcodes its data addresses (`gfxPixelMasks`, `gfxCol`, `dispIsBitmap`, `kLineX1Lo`, `kBoxColor`), and the chain ends against the `$FC3F` gfx-scratch anchor, so a `.pc` makes an overflow a build error rather than a silent drift. Growing the chain means **funding** it: `blitGlyphFb` (9 B) sits in the `$CDF5` pocket for exactly this reason and `paRoute` (31 B, `kPrintCharAt`'s surface router) fills the pocket under `$ECDC` exactly. Nothing between `$ED5D` and `$FC3E` is pinned by an exact-address assert, so shifting the chain's *contents* is safe; moving a pin in `gfx.inc`'s early data ($ECE9-$ED56) is not. `kPrintGlyphAt` (`printGlyphAt`, 39 B at `$1844` + 32 B at `$CA6B`) is outside the band for the same reason -- and it **reuses the chain's tail instead of copying it**: `printCharAt` carries a `paNibble` label where the matrix-nibble write starts and uses `paDone` for the cursor restore, and the glyph writer `jmp`s into them (its stack contract is the shared prologue's: col pushed, then row). Sharing a tail by label costs the band nothing; a fourth copy of that block would have cost ~44 B the chain does not have.




