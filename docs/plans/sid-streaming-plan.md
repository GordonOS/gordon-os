# Streaming a tune through `sid.lib` — implementation plan

> **Status: designed, not started.** Work happens on the `sid-stream` branch, which is cut from
> `main` (it carries none of the `musedit` work). `musedit`'s own half — the editor reading and
> editing a tune through windows — is **step 12 of `musedit-plan.md`** on the `musedit` branch and
> is not part of this plan; it will adopt the entry point defined here.
>
> **Goal.** Play a `.mus` of *any* size. Today every player task (`sidplay`, `godzi`, `gortris`,
> `grknoid`) `kMalloc`s `ceil(size/256)` pages and `kReuLoad`s the whole file, so the tune's size
> is a pool question: the largest bundled tune is 18 pages, and a larger one cannot be played at
> all. With this change the RAM cost is **three 256-byte windows (3 pages, fixed)** and the only
> ceiling is the filesystem's 16-bit size field — **64 KB**, fourteen times the biggest tune.
>
> **Non-goals.** Seeking (the format has none), repeat-forever (a separate parked item), and any
> change to what the player *does* with a tune — the sound must be bit-identical.

## 1. What was measured

- **The player never writes the tune image.** Its only stores through pointers are `($04)` (its own
  per-voice workspace) and `($06)` (SID registers), so a window is a plain copy and needs no
  write-back.
- **The file holds no absolute addresses.** Streams are located from the header's three size words
  (bytes 2-7), and jumps go through runtime phrase slots (`DEF`/`CALL`) and `WorkCmdAfterHED`
  (`HED`/`TAL`). Paging therefore needs **no relocation and no fixups** — only offset arithmetic.
- **Each voice's stream is walked linearly.** Counting every fetch with `tools/mus-sim.py`'s
  pointers: a 256-byte window per voice is reloaded 6/5/6 times across all of `katmand`
  (142 s, 2231 fetches) — **17 window loads for an 18-page tune**, ~400 cycles each.
- **One shared window would thrash.** The same tune would load **1724** times (~12/s), because the
  three streams start pages apart and each is fetched every jiffy. Per-voice windows are a
  requirement, not an optimisation.
- **Eight sites touch `WorkNxtCmdLo/Hi`** — INIT (times two), `SetCurrentCmdAddress` (the single
  per-command fetch), and the advance. The jump paths already copy 16-bit values verbatim, so they
  need no change once the values are offsets.
- **`sid.lib` is 3324 bytes of 3328 (4 spare)**, so any paging support takes it to **14 pages**.
  Accepted.

## 2. Design

### 2.1 The player works in file offsets

On the streaming path `WorkNxtCmdLo/Hi` holds a **file offset** instead of a RAM address (16-bit:
the FS size field is 16 bits, so an offset always fits). `$08/$09` keeps its present meaning — the
address of the current command, used for option bytes — but it is derived from the offset and the
window:

    $08/$09 = window[voice] + (offset - winOfs[voice])

The fetch (`SetCurrentCmdAddress`) seats the window first; the advance adds the command length to
the offset and recomputes the address through the same helper. `INIT_SONG` loses its "add the image
base" step (an offset needs none), and the phrase and HED slots simply hold offsets.

A **sliding** window, not a page-aligned one: 256 bytes covering `[winOfs, winOfs+256)`. The guard
is a 16-bit "is the offset inside the window with room for a command" test; when it fails the
window slides to the offset. That is what makes a 256-byte window safe — a command that would run
off the end simply slides the window, which costs the same one load a page change would have.

### 2.2 Two paths, chosen by the caller

`kSidPlay` keeps working exactly as it does now (image in RAM, absolute addresses) with its code
paths untouched, so nothing that works today can regress. The streaming path is a **new entry**,
`kSidPlayStream` (sid.lib `call 35`), used when the tune does not fit the RAM budget the caller
chose. One flag byte in the player selects which addressing mode a run uses; the cost on the
classic path is one test per command (~10 cycles out of ~17,000 per jiffy).

The threshold is the caller's: the window set costs 3 pages, so a tune of **3 pages or less** is
cheaper to play the classic way. Start at 3 and tune with measurements.

### 2.3 The ABI

    kSidPlayStream (call 35)
      kPtr   = pointer to a 9-byte config block in the CALLER's RAM:
               +0 REU bank of the file's data area
               +1 REU address lo
               +2 REU address hi
               +3/+4 window 0 pointer (voice 1, 256 bytes)
               +5/+6 window 1 pointer
               +7/+8 window 2 pointer
      kArg   = delay (scheduler ticks per jiffy at 100%, 0 -> 1)  -- scaled by the library
      kArg+1 = repeats: 0 = forever, 1 = once, 2-255 = that many
      kArg+2/+3 = transpose (0/0 -- kSidPlay adds the machine's pitch shift itself)

The player **copies the nine bytes into its own data at entry**, because `kPtr` is volatile across
anything that could run (the same lesson the callers already record for `kReuLoad`). Every entry
and exit rule of `kSidPlay` is unchanged: blocking, exclusive ownership of the SID, `kSidHush` to
stop, the ZP `$02-$0A` declaration, and the machine-speed scaling of `kArg`.

The caller fills the config from the filesystem and keeps no library afterwards:

    kReuFindFile(name)            -> reuFsPtr (24-bit FS offset), reuFsRemain (16-bit size)
    absolute REU address          = reuFsDataBase (bank/lo/hi) + reuFsPtr   (24-bit add)
    windows                       = kMalloc 3 pages (or the task's own data)

`kReuFindFile` is kernel-resident and `kReuToC64` is a kernel call, so the pager needs **no
library**: `sidplay` can go on dropping filesys with `kLibHold` the moment the tune is found.

### 2.4 The fetch

The player does the DMA itself, re-entrantly, with `kReuToC64` (16-bit REU address plus bank)
rather than `kReuDmaRange`, which takes its offset from the *kernel-global* `reuFsPtr` — a
concurrent filesystem call could overwrite that between the store and the call. `kReuToC64` needs
only task-local state; a 256-byte window that crosses the `$FFFF` bank edge is split into two DMAs.

### 2.5 What a reader of `$54A2` sees

On the streaming path the published position block holds **file offsets**; on the classic path it
holds RAM addresses (unchanged). A reader must know which path its child is on. `musedit`'s step 12
will use offsets only, so this is documented and then frozen.

## 3. Work items

Each is a checkpoint that builds and passes the gates on its own.

- [ ] **1. The window helper.** `sidWinSeat` (seat voice X's window at an offset) plus the
      bank-aware 256-byte fetch through `kReuToC64`, in `sidplay.inc`. Data: three window
      pointers, three `winOfs` word pairs, the file base (3 B), and the mode flag.
- [ ] **2. Offsets in the player.** `WorkNxtCmd` becomes an offset on the stream path;
      `SetCurrentCmdAddress`, the advance and `INIT_SONG` route through the helper; the mode flag
      short-circuits the classic path. Verify the existing tunes on the **classic** path first:
      no behaviour change at all.
- [ ] **3. The entry.** `kSidPlayStream` (call 35), its dispatcher case and a cold stub for the
      lazy path. `$E119` is the last free vector byte and `$E11A` upward are pinned aliases, so the
      entry goes next to `kSidPlay`/`kSidHush` in the loader's stub band (`$FF7E`), whose block is
      sized for two entries and must be extended into the `$FFE6`-up run. `main.asm`'s band asserts
      and `verify-map.ps1` are updated with it.
- [ ] **4. One caller.** `sidplay.tsk` grows the config and the size test: 3 pages or less goes
      the classic way, else stream. It is the smallest caller and it has a status line, so a
      failure is visible without a debugger.
- [ ] **5. The rest of the callers.** `gortris`, `godzi`, `grknoid` — the same shape. Their loops
      must behave identically: `gortris` calls with `repeats = 0`, the others with `1`.
- [ ] **6. Gates and docs.** `check-pool-budget.ps1` (the player tasks' malloc rows drop to 3,
      `sid.lib` goes to 14 pages), `verify-map.ps1`, the call inventories, `docs/lib-sid.md`
      (the new entry and the offset semantics), `tools/tasks.json` notes where a mask changes.

## 4. How it is tested

**The sound is the specification**, so every check is "the same music, or it is a bug":

1. **Classic path unchanged:** every bundled tune through `sidplay` before and after — the
   published pointers, the SID register writes and the audible result must be identical (that code
   is not touched; the flag test is the only difference).
2. **Stream path, small tune forced on** (threshold temporarily 0): each tune plays to its end,
   `kSidPlay` returns at the right moment, `repeats = 0` loops and `kSidHush` stops.
3. **Stream path, a big tune:** `katmand` (18 pages) in `sidplay` and `grknoid` — the full song,
   the loop point, no wedging. This is the tune that cannot be played at all today, so it is the
   headline test.
4. **Window crossings:** the three streams cross a window edge many times in a long song
   (`katmand`: 17 loads). Confirmed by counting loads from the host model *and* by listening for a
   glitch at a crossing.
5. **A synthetic tune at the ceiling:** 64 KB of notes (generated), which no RAM budget could hold.
   It must play, loop and stop.
6. **Pool behaviour:** `pool` before and after shows a player task holding 3 pages instead of 18,
   and no eviction churn during playback (the reason `sidplay` drops filesys today).
7. **Interference:** play from one task while another uses the filesystem (a `dir`, a task load) —
   the window fetch must not be disturbed by the FS's own kernel globals.
8. **Speed:** `-speed 100` and 200; the music's tempo must not change (the DELAY scaling is
   untouched, but a page load inside a jiffy costs time — measure that it is invisible).

## 5. Risks

- **The per-command guard is on the hot path** — one 16-bit test per command per voice, ~30 cycles
  per jiffy. Verify by comparing the tick count over a fixed number of jiffies.
- **A fetch inside the jiffy** costs ~400 cycles (2.3 % of a jiffy) and happens ~17 times per song
  — invisible, but it *is* inside the timing loop, so item 8 checks it.
- **`kReuToC64` clobbers `$DF00-$DF0A` and A** — safe inside the player (its scratch is `$02-$0A`,
  and X and Y are preserved), but the helper must not assume A survives.
- **The config copy at entry** must happen before anything can clobber `kPtr` — the first thing
  after the mode check.
- **A wrong window is silent**: reading the wrong 256 bytes plays wrong notes, not nothing. The
  host model (`tools/mus-sim.py`) is the reference for what should sound, and the offsets it
  reports are the same numbers the player now uses, so a mismatch is directly comparable.
- **`main` differs from the `musedit` branch** in `sidplay.inc` (the branch adds the 6-byte
  position publish). Whichever lands first, the other rebases; the publish stores are orthogonal to
  the paging, so that merge is mechanical.
