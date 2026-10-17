# GordonOS — Kernel Calls → Dynamic Libraries Survey

> Working survey of which resident kernel routines could move into
> dynamic libraries, and which must stay resident. Update this doc as
> routines are moved.

**How to read this doc:** a routine may only move to a lib if it has **no
resident kernel caller** and it talks to the kernel **only through
exported jump-table entries** (`kXxx`) and/or aliased kernel data. Libs
are assembled separately — they cannot call resident labels that are not
in the jump table.

---

## 1. Already in dynamic libraries (42 calls)

Resident cost is a single 3-byte jump stub per call.

| Lib | Mode | Calls |
|---|---|---|
| `string.lib` | lazy | strlen, strcpy, strcmp, skipSpaces, byteToHex, hexDigit, nibbleToHex (7) |
| `time.lib` | lazy | getTime, setTime, printTime (3) |
| `filesys.lib` | lazy | format, save, delete, rename, dirEntry, create, open, readAt, writeAt (9) |
| `gfx.lib` | eager | plot, line, box, fillBox, circle, ellipse, floodFill, getPixel, clearBitmap, setGfxColor, gfxChar, gfxString, fillMatrix (13) |
| `fp.lib` | eager | fpCall (1) |
| `sid.lib` | lazy | sidPlay, sidHush (2) |
| `ipc.lib` | lazy | ipcPost, ipcPick (2) — `X` = one of 4 channels |
| `sprite.lib` | eager | spriteSheet, spriteFrame, spritePos, spriteReg (4) — `kSpriteCollide` stays resident (it reads the kernel's collision latch) |

`LIB_CALLS = 42`. `libEntryCount` = time 3, filesys 9, gfx 13, fp 1, string 7,
sid 2, ipc 2, sprite 4, mouse 1
(42 in use, including 33/34 for `sid.lib`, 35/36 for `ipc.lib`, 37-40 for
`sprite.lib` and 41 for `mouse.lib`) — the tables are full and end flush against
`gfxRowLutLo`, so a further entry needs its own dispatching call (`fpExec`/`kFpCall` is the
pattern) or a relocation.

---

## 2. Must stay resident (has a resident kernel caller)

| Routine | Resident caller(s) |
|---|---|
| `mul16` | `screen-mgmt.inc` (`slot*$28`) |
| `div16` | `char-shadow.inc`, `screen-mgmt.inc` (slot math) |
| `byteToDec` | `syscall.inc` `sysThreadSpawn` (thread name `parent.N`) |
| `getTaskName` | `syscall.inc` `sysThreadSpawn` |
| `reuFindFile`, `reuDmaRange`, `reuDirPageDma`, `reuCmpName` | loader (`dynload.inc`), `lib.inc`, `poolevict.inc`, `reu-fs.inc` |
| `shadowHiOf`, `eraseCursor`, `pcShadowChar`, `surfSlotToReu`, `surfFreeSlot`, `surfClearSlot`, `seedSlotCursorTail`, `rowAddr`, `semWait`, `semSignal` | resident internals, called by resident code — `semWait`/`semSignal` are also reachable from tasks as syscalls 10/11 |
| screen-io / keyboard / scroll-region / blitter | hot path (every char print, scroll, view switch) |

---

## 3. Cold-but-resident candidates (only task/lib-reachable)

| Routine | Size | Callers | Verdict |
|---|---|---|---|
| `reuDirEntry` (`kReuDirEntry`) | ~213B | dir, type, banner, edit, shell `ls`, basic `DIR` | ✅ **Moved → `filesys.lib`** |
| `readRow`, `printCharAt`, `charAt`, `setCharShadow` | ~275B | editor/basic only | ⚠️ coupled to non-exported internals |
| `printGlyph` | ~139B | banner, maze, basic | ⚠️ coupled to print path |
| `setGfxMode` + `fillColorRam` | ~168B | basic only | ⚠️ coupled to screen-mgmt |
| `lockScreen`/`unlockScreen` | 49B | every command task | ✗ screen mutex — keep |
| `surfDisplayed` | ~37B | gfx.lib + clock | ✗ cross-cutting — keep |
| `random` | ~9B | maze only | ✗ too small |
| `getCursorOn` | ~4B | basicedit | ✗ trivial |

---

## 4. Decision log

| Routine | Decision | Status |
|---|---|---|
| `reuDirEntry` → `filesys.lib` | Approved | ✅ done |

*(Append rows as each move is planned/done.)*

---

## 5. Move checklist (per routine)

For each routine moved into a lib, update **all** of:

1. Jump-table entry in `src/kernel/kernel.asm` — `jmp routine` → `jmp libStubXxx`.
2. New stub in `src/kernel/libstubs.inc` (cold path: load lib, patch, tail-call).
3. `libEntryCount` in `src/main.asm` (the lib's count +1).
4. `LIB_CALLS`, `libJumpLo/Hi`, `libInitLo/Hi` in `src/main.asm` (+1 entry each, in call order) — ⚠️ `LIB_CALLS` is at its ceiling and the growth reservoir is spent, so this step needs the two `LIB_CALLS` tables relocated (or a dispatching entry, see §1).
5. The lib's source (`src/libs/*.asm`) — receive the routine, wired to its entry index.
6. `.assert` guards in `src/main.asm` — any alias/address that shifted.
7. Rebuild libs (`tools/build-tasks.ps1`) + REU (`tools/build-reu.ps1`) + kernel (`ninja -C build`, touch `main.asm`).
8. Update `docs/memory-map.md` and `docs/kernel-fragmentation.md` addresses.

---

## Notes on the coupled cluster (blocked, not worth it yet)

The editor text helpers (`readRow`, `printCharAt`, `charAt`, `setCharShadow`)
and `printGlyph` live inside the resident gfx/char-shadow cluster and call
**non-exported** resident internals — `shadowHiOf`, `eraseCursor`,
`pcShadowChar`, `reuWait`. Moving them would require exporting those
internals as new jump entries (plus the usual alias/assert churn) for
~275B of savings. Not a good trade until/unless more of the cluster can
move at once.
