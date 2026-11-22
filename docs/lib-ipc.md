# `ipc.lib` — Library Reference

Four byte channels between tasks, implemented **entirely in the library's own
data**. One task posts bytes into a channel, another picks them up, and the
kernel pays nothing for it: no syscall, no kernel buffer, no new resident code —
just two 3-byte entries and four 128-byte rings that live in the pool with the
rest of the library.

The same two entries also carry **whole 256-byte frames** through REU, for
payloads a byte ring is the wrong shape for — see [The block half](#the-block-half--whole-frames-in-reu).

| | |
|---|---|
| Lib ID | 6 |
| Entries | 2 (`kIpcPost`, `kIpcPick`) — `X` selects the channel |
| Byte channels | 4 (`0`–`3`), independent, each with its own ring and indices |
| Block channels | 4 (`$80`–`$83`), each 64 frames of 256 bytes in REU bank 250 |
| Load policy | **lazy** — the first call cold-loads it; declare `libMask` bit 6 (`$0040`) |
| Call indices | 35–36 (jump entries patched via `libJumpLo/Hi` + `libInitLo/Hi`) |
| Kernel entry points | `kIpcPost` (`$FF84`), `kIpcPick` (`$FF87`) |
| Source | `src/libs/ipc.asm` |
| Pool footprint | **5 pages** (1088 bytes of code, 29 fixups) |
| REU footprint | **64KB** — bank 250 (`REU_IPC_BASE`), held out of the FS data region |
| Caller ZP | **none** — no zero page is needed or claimed |
| Clobbers | A, X, Y and `kPtr`/`kPtr+1` (see below) |

> The two entry points are **not** in the `$E000` jump table — that table is
> full (its only free byte is `$E119`). They are ordinary 3-byte stubs in the
> free block at `$FF7E`, next to `sid.lib`'s, in the block that must stay the
> **last thing assembled** in the kernel.

## Before you call

- **Declare `libMask` bit 6** (`$0040`) so the library is refcounted to your
  task: loaded on your first call, freed when the last declared task exits.
- **One producer and one consumer *per channel*. That is the contract, not a
  performance note.** `ipcPost` writes that channel's `ipcHead` and `ipcPick`
  writes its `ipcTail`; nothing arbitrates, because there is nothing to
  arbitrate *while each index has exactly one owner*. Two tasks posting to the
  *same* channel would both write `ipcHead`, and the full test is a
  read-modify-write, so the ring corrupts. **A second task pair takes another
  channel** — that is what the four are for. There is no protection against two
  pairs choosing the same one; agreeing on the index is the caller's job.
- **The channels are empty at load.** `ipcHead`, `ipcTail` and the rings are
  ordinary library data, initialised by the binary; bytes left in a ring do not
  survive a `libFree`/`libLoad` cycle. That is a purge on last close, which is
  the right default.
- **127 bytes of capacity, per channel.** Each ring is 128 slots with one left
  unused, so "full" and "empty" are different states — a full ring reports
  `head+1 == tail`, an empty one `head == tail`.
- **No ZP, but `kPtr` is clobbered.** Unlike [`sid.lib`](lib-sid.md), this
  library keeps all its state in the pool and needs no caller window. It *does*
  use `kPtr`/`kPtr+1` as the ring pointer for the duration of a call (a
  per-channel pointer table lives beside the rings). That is race-free
  precisely because `switchTask` saves and restores `kPtr` per task; a
  self-modified `sta abs,y` operand would not have been safe, because two tasks
  preempting inside `ipcPost` would clobber each other's channel.
- **The data is shared, and that is the point.** A library's `.byte` variables
  are one copy in the pool, so the ring is a mailbox between two tasks rather
  than a per-task buffer. (The same property is what makes `sidOwner` work in
  `sid.lib`.)
- ⚠️ **A library can declare neither ZP nor a filesys reference** (a library's
  `libMask` is structurally `0`). If a participant needs to read or write the
  REU FS, that task must declare `filesys.lib` itself.

## `kIpcPost` — push a byte into a channel

- **Input:** X = channel (`0`–`IPC_CHANNELS-1`, i.e. `0`–`3`), A = the byte.
- **Output:** `C=0` — the byte is in that channel's ring. `C=1` — `X` was out of
  range and **nothing** was stored.
- **Clobbers:** A, X, Y, `kPtr`/`kPtr+1`, `kArg+1` (the sleep count).
- **Blocks** *politely* while that channel's ring is full: it sets `kArg+1 = 1`,
  calls `SYS_SLEEP`, and retries. A slow consumer therefore applies backpressure
  to a fast producer, which yields the CPU instead of dropping data or spinning.
  (The other three channels are unaffected by a full one — a blocked producer
  holds its own channel and nothing else.)
- ⚠️ **There is no timeout.** If the consumer dies with the ring full, the
  producer stays inside `kIpcPost` for good — yielding, so the rest of the
  system is unaffected, but that task never finishes. Kill it, or have whoever
  owns the pair tear the other end down.
- ⚠️ **A bad `X` is refused, not wrapped.** The range test runs *before* the
  `pha` that parks the byte, so the `C=1` path leaves the stack untouched (a
  stray `pla` there would eat the caller's return address). This is the same
  rule the sprite API uses for an out-of-range slot.
- ⚠️ **The wait parks the channel as well as the byte, because `SYS_SLEEP`
  returns with `X = curTask`.** `sysSleep` opens with `ldx curTask`, so `X` is
  *not* the channel any more once the backpressure path has been taken: the
  retry would index `ipcHead,x`/`ipcTail,x` with a task slot and, on a channel
  whose number differs from the task's slot, freeze the pair *and* scribble a
  neighbouring channel's head. Both values therefore go on the task's own stack
  (`pha`, then `txa; pha`), which is the one place a preemptive switch cannot
  reach — the scheduler saves and restores SP per task, so the pair is part of
  the suspended image — and the channel is re-read from it after the wait. It
  only costs the slow path, so a push that never waits pays nothing for it. The
  byte is parked *on top of* the channel so the store's `pla` recovers it
  without disturbing `X`.
- The byte is parked with `pha` and recovered with `pla` across the yield. That
  is safe because it is the task's **own** stack: the scheduler saves and
  restores SP per task, so the parked byte is part of the suspended image.
- **Publish order:** the data goes into the slot *before* `ipcHead` is
  advanced, so the reader can never see an index pointing at a slot that has
  not been written yet.

## `kIpcPick` — take the oldest byte out

- **Input:** X = channel (`0`–`3`).
- **Output:** `C=0`, A = the byte. `C=1` = that channel is empty (or `X` was out
  of range) and **nothing** was consumed.
- **Clobbers:** A, X, Y, `kPtr`/`kPtr+1`.
- **Never blocks**, on purpose: a producer that dies must not strand a consumer
  forever. A consumer that wants to wait calls it in a loop with `SYS_SLEEP`
  (which also guarantees the producer gets scheduled).
- **Release order:** the slot is released (`ipcTail` advanced) only after the
  byte is in A, so the producer cannot overwrite a byte mid-read.

## Why this needs no lock

Each index is a single byte with exactly one writer: `ipcPost` is the only code
that stores to a channel's `ipcHead`, `ipcPick` the only code that stores to its
`ipcTail`, and a 6502 `sta`/`lda` of one byte is atomic with respect to a
preemptive switch — the scheduler can only take over between instructions, and
there is no point in either routine where *both* indices have been left in an
inconsistent state. The reads the two sides do of *each other's* index are
single-byte loads, so the worst a switch can do is make a side see a slightly
stale value and take the other branch — never a torn value.

Note that the argument is strictly *per channel*: it says nothing about two
producers on one channel, which is exactly why that case is forbidden above. It
also says nothing about a task pair that both call `ipcPost` on *different*
channels — that is fine, because `kPtr` (the only non-index state either routine
touches) is saved and restored per task by the scheduler.

The two orderings above are what make it correct rather than merely
almost-correct: **data before index on the way in, index after data on the way
out.** Reverse either and the peer can observe a published index for a slot
that is not ready.

## The ring

There are four of these, one per channel, each with its own `ipcHead`/`ipcTail`
pair and its own 128-byte `ipcRingN`:

```
        ipcTail                                   ipcHead
           |                                         |
           v                                         v
        +-----+-----+-----+-----+-----+-----+-----+-----+
        |  7  |  8  |  9  |     |     |     |     |  ?  |
        +-----+-----+-----+-----+-----+-----+-----+-----+
              \_____________/               \_________/
               readable bytes                one slot kept
                (oldest first)               free on purpose
```

`ipcHead` is the slot the **producer will write next**, `ipcTail` the slot the
**consumer will read next**. Both wrap with `and #127`. Empty is
`head == tail`; full is `(head+1) & 127 == tail`, which keeps that one slot
between the two indices so the two states can never be confused.

The channel selects the ring through two 4-entry tables (`ipcRingLo`/`ipcRingHi`)
rather than arithmetic, so `IPC_SIZE` stays free to change to a power of two
other than 128.

## The block half — whole frames in REU

A byte ring is the wrong shape for bulk. Every push is a `jsr`, a range test, a
full test, a store and an index update — about **86 cycles a byte**, so an
8000-byte screen costs roughly 0.7 s of CPU and a chunking loop in every
producer. A frame is **one DMA**: 255 bytes in a fraction of a millisecond, with
no loop at all.

There is no third entry point. `LIB_CALLS` is at its ceiling (see
[`dynamic-libraries.md`](dynamic-libraries.md) §4 — the four scan tables end
flush against the pinned `gfxRowLut`), so the **channel code is the sub-op**, the
way `kMouseCall` and `kFpCall` select theirs. The byte channels keep their
`0`–`3` indices and their behaviour; `$80` + channel selects a block channel, and
anything else is refused exactly as `X = 4` always was.

### The geometry

```
REU_IPC_BASE ($FA0000, bank 250)
  frame 0    frame 1    frame 2   ...            frame 255
  +---------+---------+---------+                +---------+
  | 256 B   | 256 B   | 256 B   |      ...       | 256 B   |
  +---------+---------+---------+                +---------+
  \_____________________/ \____________________/ \_________/
    block channel $80          $81                  $83
    (frames 0-63)          (64-127)             (192-255)
```

A frame is one **page** of the bank, so a frame index *is* its address high byte:
`lo = 0`, `hi = frame`, `bank = $FA`. Seating a transfer is therefore two loads,
and the four channel bases are the arithmetic `channel × 64`.

Each block channel is a ring of **64 frames, 63 of them usable** — the same
one-slot-kept-free convention as the byte rings, so "full" and "empty" stay
distinct — with its own `ipcBlkHead`/`ipcBlkTail`. `IPC_BLK_FRAMES` and
`IPC_BLK_MASK` are powers of two, so both the base and the wrap are arithmetic.

### `kIpcPost` — send a frame

- **Input:** X = `$80` + channel, A = the length (**1–255**), `kPtr` = the source.
- **Output:** `C=0` — the frame is in the ring. `C=1` — `X` was out of range and
  **nothing** was stored. `A = 0` is refused the same way.
- **Clobbers:** A, X, Y, `kArg..kArg+4` (`kPtr` is only read).
- **Blocks** while that channel's ring is full, exactly as the byte form does:
  it parks the channel and the length on the task's own stack, `SYS_SLEEP`s and
  retries — so the same
  [stale-`X` trap](#kipcpost--push-a-byte-into-a-channel) applies and is handled
  the same way.
- **Publish order:** the length goes into `ipcBlkLen` first, then the payload,
  then `ipcBlkHead` — so a consumer can never pair a length with another
  message's bytes.

### `kIpcPick` — receive a frame

- **Input:** X = `$80` + channel, `kPtr` = the destination.
- **Output:** `C=0`, A = the length (1–255); `C=1` when that channel is empty (or
  `X` was out of range), with nothing transferred.
- **Clobbers:** A, X, Y, `kArg..kArg+4` (`kPtr` is only read).
- **Release order:** `ipcBlkTail` is advanced only *after* the payload has landed
  in the caller's buffer, so the producer cannot reuse the frame mid-transfer.
- **Never blocks**, as above — poll it, or loop with `SYS_SLEEP`.

### Why the length is a separate array

`ipcBlkLen` holds one byte per **frame** (256 bytes, indexed by frame index), not
per channel and not inside the frame. Inside the frame it would cost a second
DMA to read, or a shared scratch byte that two producers could collide on; per
channel it could not survive two frames in flight. An array indexed by frame has
exactly **one writer** (that frame's producer) and **one reader** (its consumer),
which is the same single-owner argument that makes `ipcHead`/`ipcTail` lock-free.

### What it costs

- **1 extra REU bank, already paid for.** Bank 250 sits outside the FS data
  region (`reuFsCapacity` tops out at `$F7FFFF`), so the frames cost the
  filesystem nothing.
- **2 extra pool pages** (3 → 5): the frame indices, the length array, and the
  code. That is why the byte rings stay in the pool and the *bulk* path is in
  REU — a KB-scale byte ring could not be afforded at all (the pool is 109
  pages).

### Using it

```asm
//  Send 8000 bytes of a bitmap: 32 frames of 250, and one DMA each.
        lda #250
        sta ipcLen                  // this task's own byte
        ldx #$80+1                  // block channel 1
        lda #<bitmap
        sta kPtr
        lda #>bitmap
        sta kPtr+1
        lda ipcLen
        jsr kIpcPost                // C=1 only if X was bad; a full ring waits

//  ...and the consumer, one frame at a time:
        ldx #$80+1
        lda #<page
        sta kPtr
        lda #>page
        sta kPtr+1
        jsr kIpcPick
        bcs waitForIt               // C=1: nothing yet -- sleep and retry
                                    // A = the length, copied into (kPtr)
```

`ipctest` runs exactly this shape across two tasks (see below).

## Example

A producer — a task whose own counter is per-instance code image data, so
nothing of it escapes into the library. It posts on **channel 1** (say):

```asm
        lda #0
        sta ipNext
ipLoop:
        ldx #1                  // the pair's agreed channel
        lda ipNext
        jsr kIpcPost            // returns only when the byte is buffered
        inc ipNext
        bne ipLoop              // 256 bytes: the counter wrapping to 0 ends it
        lda #SYS_EXIT
        jsr syscall

ipNext: .byte 0
```

A consumer, polling with a yield so the producer gets the CPU:

```asm
ipLoop:
        ldx #1
        jsr kIpcPick
        bcs ipIdle              // C=1: empty, nothing consumed
        sta ipByte
        jmp ipLoop
ipIdle:
        lda #1                  // one tick: still switches tasks, never spins
        sta kArg+1
        lda #SYS_SLEEP
        jsr syscall
        jmp ipLoop
```

Note what is *not* here: no `kArg`, no ZP, no library mask on the call — just
`libMask` bit 6 in the two task headers and two `jsr`s. And note what **is**
there: `X` is set at every call, not once, because `kIpcPost` and `kIpcPick`
both clobber `X` — and so does everything else in between.

## How to prove it

### Four channels — `ipctest`

`ipctest` is how to re-check the library after any change to
`src/libs/ipc.asm`: it is the ipc.lib counterpart of `fstest`. Run it and expect
`ipc PASS`.

| What it prints | What it proves |
|---|---|
| `guard ok` | `kIpcPost` with `X = 4` is refused with `C=1` and stores nothing (channel 0 is still empty afterwards) |
| `4 empty ok` | all four rings start empty and independent |
| `ch1 ok` | a thread producer posting `0,1,2…` onto channel 1 is received in order, all 256 bytes |
| `ch3 ok` | a second thread posting `255,254,…` onto channel 3 is received in order, **interleaved** with channel 1 |
| `blk guard ok` | `kIpcPost` with `X = $84` is refused with `C=1`, and block channel `$80` is still empty |
| `blk ch2 ok` | a third thread sends the **whole 64-frame ring** on block channel `$82` — length `1+n` on frame *n*, every payload byte `n` — and the parent drains it in order |

The block test runs after the two byte pairs have finished, on a *different*
channel number from either, and the payload is XORed with the displayed-frame
counter so a second run cannot pass on the frames the first one left in bank 250:
a frame the library failed to write still holds the old seed. Sending 64 frames
into a 63-frame ring also guarantees the wrap and the producer's backpressure
wait are reached.

The interleave is the substance of the test. Both producers are faster than the
single consumer, so both rings (127 bytes) fill and both producers block inside
`kIpcPost`'s backpressure wait — the path where `SYS_SLEEP` returns with
`X = curTask`, so the call re-derives the channel from the producer's own stack.
The two channels are deliberately *1* and *3*, i.e. neither is
guaranteed to match the task slot that happens to be free: a stale `X` therefore
cannot pass by coincidence, and because the two patterns differ, a channel
mix-up shows as a mismatch rather than as plausible data. A failure prints
`FAIL` on the named check; a stall prints `TIMEOUT` after ~68 s of empty passes,
so a broken library fails the test instead of hanging the machine.

The parent outlives both children on every path (a thread reaches the library
only through the parent's `libMask`, and `SYS_EXIT` does not reap thread
children), and the test is **single-instance** — flags `$01`, so a second
`ipctest` is refused with `? already running`: two copies would be two producers
on the same channels, which is the one thing the contract forbids.

### The index convention

`ipcHead` and `ipcTail` must be defined as the same kind of thing — both name
the slot that will be used **next**: the writer stores to `ipcRing[ipcHead]` and
then advances `ipcHead`, and the reader consumes `ipcRing[ipcTail]` and then
advances `ipcTail`. If one of them instead means "the slot I used last", the pair
is off by one: the reader sits one slot behind the writer and its first pick
returns slot 0, which was never written and still holds the `.fill` zero. The
capacity is 127 bytes either way.

⚠️ **A byte-identical wrong result from two different boots is a code path, not a
race.** A genuine two-producer interleaving differs between runs; identical
output points at the index arithmetic.

## What it is not (yet)

- **No semaphores of its own.** A channel carries its own throttle — `kIpcPost`
  blocks while the ring is full — so a producer/consumer pair needs no counting
  semaphore at all. For *mutual exclusion* a task uses the kernel's own counting
  semaphores instead: syscalls 10/11 (`semWait`/`semSignal`, indices 1-3,
  owner-tracked, mutex released on task death), so a pair that needs a lock around
  shared state reaches for those, not for this library. A *library-side* semaphore would be a natural
  second entry pair here, with one honest caveat — a task that dies holding a
  library semaphore leaves it locked, because the kernel's death-release scan
  only knows its own `semCount`/`semOwner` pool. See
  [`future-improvements.md`](future-improvements.md).
- **Four channels of each kind, and no more.** Both sets are fixed and anonymous:
  a pair agrees out of band (a convention, or a byte in the task header) which
  index is theirs, and nothing detects two pairs picking the same one. A fifth
  byte channel means growing `IPC_CHANNELS`, the two pointer tables and the ring
  block — a code change plus two more pool pages, not a runtime allocation; a
  fifth block channel would mean finding another bank.
- **No message boundaries on the byte channels.** They are a byte stream — no
  framing, no sizes, no priority. The block channels *do* carry a length per
  frame; what they do not carry is a protocol, a sequence number or a reply.
- **No wakeup.** `kIpcPick` never blocks, so a consumer polls; with a 1-tick
  sleep that is up to a jiffy of latency per byte.
- **No protocol on death.** Nothing here notices that the other side is gone.

## See also

- [`dynamic-libraries.md`](dynamic-libraries.md) — the library system itself
  (refcounts, lazy cold stubs, the `LIB_CALLS` ceiling)
- [`lib-sid.md`](lib-sid.md) — the sibling library, and the opposite extreme:
  13 pool pages and a mandatory ZP contract
- [`lib-sprite.md`](lib-sprite.md) — the sprite API, whose per-task state lives
  in the *kernel* rather than the library: the shadow belongs to whichever task
  owns the display, and the kernel hands it over when the display changes hands
- [`programmers-guide.md`](programmers-guide.md) — task headers, `libMask`,
  `SYS_SLEEP`
- [`future-improvements.md`](future-improvements.md) — the library-semaphore
  extension
