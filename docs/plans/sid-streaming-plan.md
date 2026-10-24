# Streaming a tune through `sid.lib`

> **Status.** Implemented in the player (`src/libs/sidplay.inc`, `src/libs/sid.asm`) and in one
> caller (`sidplay.tsk`); awaiting VICE verification, then the other three callers. Work happens on
> the `sid-stream` branch, cut from `main` so it carries none of the `musedit` work. `musedit`'s own
> half — the editor reading and editing a tune through windows — is **step 12 of
> `musedit-plan.md`** and will adopt the entry point defined here.
>
> **Goal.** Play a `.mus` of *any* size. A buffer costs `ceil(size/256)` pool pages, so the tune's
> size used to be a memory question: the largest bundled tune is 18 pages and a bigger one could not
> be played at all. With streaming the RAM cost is **three 256-byte windows (3 pages, fixed)** and
> the only ceiling is the filesystem's 16-bit size field — **64 KB**, fourteen times the biggest
> tune.
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
- **Each voice's stream is walked linearly.** Counting every fetch with `tools/mus-sim.py`:
  a 256-byte window per voice is reloaded 6/5/6 times across all of `katmand` (142 s, 2231 fetches)
  — **17 window loads for an 18-page tune**, ~400 cycles each inside a ~19,600-cycle jiffy.
- **One shared window would thrash.** The same tune would load **1724** times (~12/s), because the
  three streams start pages apart and each is fetched every jiffy. Per-voice windows are a
  requirement, not an optimisation.
- **Only the fetch touches the address.** `$08/$09` (where the current command's bytes are) is read
  in exactly two places, both at the top of `ProcessCurrentCmd`; every jump path moves the *offset*
  (`WorkNxtCmd*`) instead.
- **`sid.lib`'s code was 3318 bytes of 3328 on `main` (10 spare).** Streaming took it to **3538
  bytes = 14 pages**, 296 bytes spare — one page more than before, which the release accepts.

## 2. Design

### 2.1 The player works in file offsets

The three voice pointers (`WorkNxtCmdLo/Hi`, and the phrase/HED/call-stack slots that copy them) hold
a **file offset** on both paths. `$08/$09` keeps its meaning — the address of the current command —
and is **derived from the offset at the top of every command**:

    mode 0   $08/$09 = SidBase + offset
    mode 1   $08/$09 = winHi : offsetLo

Because the derive happens on entry to `ProcessCurrentCmd`, a handler that loads a new pointer
(`CALL`, `END`, `TAL`) simply `rts`es: the handler returns to the loop head, which derives the new
address. That is why the advance no longer has to keep an address in step with the offset, and why
nothing has to park Y across a window fetch.

### 2.2 Page-aligned windows, not a sliding one

A voice's window is **page-aligned and exactly 256 bytes**, so `address = winHi : offsetLo` with no
arithmetic. That is safe because every command is a 2-byte pair at an **even** offset: a command and
its option byte can never straddle a window's end, so crossing a page simply refills the window
before the next command is read. A sliding window would need a per-voice `winOfs` word pair and a
16-bit "is the offset inside, with room for a command" test on the hot path — for the same number of
reloads, since a stream crossing a page boundary reloads exactly once either way.

The window is refilled **lazily**: `sidWinSeat` compares the offset's page (`WorkNxtCmdHi`) with the
page the window holds (`StreamPage,x`) and DMA's only on a miss.

### 2.3 The fetch

The player does the DMA itself, through **`kReuDmaRange` ($E116)**, a *kernel* entry: it takes a
24-bit filesystem offset from the kernel-global `reuFsPtr`, a destination in `kPtr`, a length in
`kArg+6/+7`, and chunks a range that crosses an REU bank edge. Two consequences:

- The player needs **no library** to read the file, so a caller can hand `filesys.lib` back with
  `kLibHold` before the music starts.
- Because `reuFsPtr` is a kernel **global**, another task's filesystem call could land between the
  store and the call, so the fetch stores + DMA's with interrupts off (`php`/`sei`/`plp`). That is a
  ~400-cycle block of the jiffy interrupt, ~17 times per song.

### 2.4 One entry, two modes

The call-index space has no room for a third `sid.lib` entry: `LIB_CALLS` is 42 and the four
positional tables end flush against `gfxRowLutLo` ($1977), which `gfx.asm` pins. So `kSidPlay` is a
**dispatching entry** (the `fp.lib` pattern): `A` selects the mode.

    kSidPlay   A = 0   kPtr = the .mus image in RAM (classic)
               A = 1   kPtr = the 9-byte config block (streamed)
               kArg, kArg+1, kArg+2, kArg+3 = delay, repeats, transpose -- unchanged

Every entry and exit rule is unchanged: blocking, exclusive ownership of the SID, `kSidHush` to
stop, the ZP `$02-$0A` declaration, and the machine-speed scaling of `kArg`.

### 2.5 The config block

    +0 +1 +2   the file's FS offset (lo, mid, hi) -- from kReuFindFile
    +3 +4      voice 1's window pointer (lo, hi)
    +5 +6      voice 2's
    +7 +8      voice 3's

The player **copies the nine bytes into its own data at entry** (`sidplay.StreamPtrLo` is the first
of nine contiguous bytes), because `kPtr` is volatile across anything that could run.

The caller fills it from the filesystem and keeps no library afterwards:

    kReuFindFile(name)   -> reuFsPtr (24-bit FS offset), reuFsRemain (size)
    windows              = kMalloc 3 pages -- page-aligned by construction,
                           so all three low bytes are the block's own

### 2.6 What a reader of the published position sees

The `musedit` branch's position block publishes RAM addresses on the classic path and would publish
**file offsets** on the streamed one. `musedit`'s step 12 reads a file through windows itself, so it
will use offsets only; the distinction is documented there rather than here.

## 3. Work items

- [x] **1. The stream state and the window helper.** `StreamMode`, `SidBase`, the 24-bit file
      offset, three interleaved window pointers, three page tags, and `sidWinFetch` / `sidWinSeat` /
      `sidStreamBegin` in `sidplay.inc`. They live after `DataVideoStd`, outside the two windows
      `sidInitSong` clears, so a song restart keeps the configuration.
- [x] **2. Offsets in the player.** `SetCurrentCmdAddress` derives the address through the mode
      branch (now `sidCmdAddress`); the advance moves the *offset*; `INIT_SONG` drops its "add the
      image base" step and starts voice 1 at offset 8; `HED`, `DEF`, `CALL`, `TAL` and `END` copy
      offsets. The two emptiness sentinels became `(hi | lo) == 0` — `PhraseDefHi` in `CALL` and
      `WorkCallStkNxtCmdAdrHi` in `END` — because an offset is never 0 (they start at 8) while an
      address's high byte could never be 0, and `DEF`'s tombstone now clears both bytes.
- [x] **3. The entry.** `sidPlay` reads `A`, latches the mode and the base, copies the config for
      mode 1 and falls into the shared claim/loop; `sidSongInit` branches to `sidStreamBegin` in
      stream mode. No new call index; the kernel change is documentation only.
- [x] **4. One caller.** `sidplay.tsk`: four pages or more streams, three or less is buffered, and a
      fifth argument forces either way (1 = stream, 2 = RAM). Instance data grew 18 → 27 bytes for
      the config.
- [x] **5. The rest of the callers.** `gortris`, `godzi` and `grknoid` all stream, each with one
      three-page window block taken at startup and freed after the child is reaped: `gortris` reuses
      its existing tune-buffer fields, `godzi` shares one window set between its two tunes and
      rewrites the config's three offset bytes on a switch, and `grknoid` drops its 18-page buffer
      for one config that a load only rewrites. Their loops are unchanged apart from the mode byte:
      `gortris` calls with `repeats = 0`, the others with `1`.
- [x] **6. Gates and docs.** `check-pool-budget.ps1` models every player at three window pages
      (`$LiveMallocMusic` is empty now), the player guides carry the new footprints, and
      `docs/lib-sid.md` has the mode, the config block and the window rules. The call inventories
      are current.

What the pool now reports for the four players (109-page pool):

| Player | before | after |
|---|---|---|
| `sidplay` | 45 | **30** |
| `grknoid` | 62 | **47** |
| `godzi` | 43 | **33** |
| `gortris` | 52 | **51** |

## 4. How it is tested

**The sound is the specification**, so every check is "the same music, or it is a bug":

1. **Classic path unchanged:** every bundled tune through `sidplay` — the audible result must be
   identical. The addressing is the only difference (offsets plus one add per command).
2. **Stream path, small tune forced on** (`run sidplay immig 1 1 0 1`): it plays to its end,
   `kSidPlay` returns at the right moment, `repeats = 0` loops and `kSidHush` stops.
3. **Stream path, a big tune:** `katmand` (18 pages) — the full song, the loop point, no wedging.
   This is the tune that cannot be played from a buffer on a loaded system, so it is the headline
   test.
4. **Window crossings:** the three streams cross an edge 17 times in `katmand`; listen for a glitch
   at each, and compare against the buffered rendering of the same tune.
5. **A synthetic tune at the ceiling:** 64 KB of notes (generated), which no buffer budget could
   hold. It must play, loop and stop.
6. **Pool behaviour:** `pool` before and after shows a streaming player holding 3 pages instead of
   18, and no eviction churn during playback.
7. **Interference:** play from one task while another uses the filesystem (`dir`, a task load) — the
   `php`/`sei` window around the fetch is what protects it.
8. **Speed:** `-speed 100` and 200; the tempo must not change (the DELAY scaling is untouched, but a
   page load inside a jiffy costs time — measure that it is invisible).

## 5. Risks

- **The derive is on the hot path** — one mode test plus the add (or a page compare) per command per
  voice, ~20-35 cycles per jiffy out of ~17,000.
- **A fetch inside the jiffy** costs ~400 cycles with interrupts off, ~17 times per song.
- **A wrong window is silent** in the wrongness: reading the wrong 256 bytes plays wrong notes, not
  nothing. `tools/mus-sim.py`'s voice pointers are the same offsets the player now uses, so a
  mismatch is directly comparable.
- **`kReuDmaRange` clobbers `kArg..kArg+5`** and the FS scratch. The player sets `kArg+6/+7` (the
  length) and reads nothing back, and no `kSidPlay` caller can observe `kArg`+4/+5 anyway.
- **`INIT_SONG` still returns a "song text" pointer** computed by adding the three size words to
  `$04/$05`. On the streamed path that arithmetic walks off window 0 and the pointer is meaningless;
  nothing consumes it (`sidSongInit` ignores the return).
- **`main` differs from the `musedit` branch** in `sidplay.inc` (the branch adds a 6-byte
  position-publish store). Whichever lands first, the other rebases; the publish is orthogonal to
  the paging, so that merge is mechanical.
