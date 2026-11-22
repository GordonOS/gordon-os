# GordonOS — Future Improvements

> Work that is **not built yet.** Anything already implemented is documented
> where it lives: [`kernel.md`](kernel.md) for the kernel API,
> [`programmers-guide.md`](programmers-guide.md) for task authoring,
> [`writing-relocatable-tasks.md`](writing-relocatable-tasks.md) §15 for the
> per-task REU workspace, and
> [`dynamic-libraries.md`](dynamic-libraries.md) for libraries.
>
> What the system cannot do today — including the limits that are staying, not
> just the work in progress — is [limitations.md](limitations.md).
>
> Section numbers are document order, not priority — see
> [Recommended order](#recommended-order).

## The room a change has

Every item below is bounded by the same numbers, and they are what separates
"possible" from "a project".

| Budget | Today | What it means |
|---|---|---|
| Kernel free runs | **443 B across 28 pockets** ([kernel-fragmentation.md](kernel-fragmentation.md)) | only one of them can hold code |
| `$0E0C-$0EE2` | **215 B, contiguous** | the room `irq.inc`, `scheduler.inc` and a new syscall handler grow into — it sits immediately after the syscall dispatcher |
| Core headroom | **10 B** at `$13CA` | treat as zero: a core change larger than it needs a relocation |
| Syscall table | 14 slots, **8 and 9 inert** | a new call costs 2 B of table plus ~80 B of handler (`sysExit` is the scale) |
| `LIB_CALLS` | **42, full** | a new library call must be a *dispatching* entry (`kMouseCall`, `kFpCall`); otherwise the four scan tables grow and every pinned address behind them moves |
| Pool | 109 pages, `basic` on **margin 1** | a few KB of task or library *data* is affordable; a KB-scale ring is not |
| REU | banks 248-255 all allocated | the per-slot **4 KB workspace** is the free home for a big per-task buffer, so "where does it live" is no longer a design question |

**Cost at a glance.** "Fits" means the code lands in the 215 B run or a named
pocket without moving a pinned address.

| Item | Needs | Fits today? | Buys |
|---|---|---|---|
| **§9 `edit` undo/redo** | ~250 B of `edit` code and ~16 B to seat its workspace address; **0 kernel bytes** | yes — `edit` has margin 31 pool pages | the last editor without history gets it |
| **§1 IPC wakeup** | (a) a semaphore handshake: syscalls 10/11 exist and release on death — **~40 B of library code, 0 kernel**; (b) or a real `SYS_RECV` plus a wait list — 4 B of table and ~130 B of handler | yes, both | a consumer that blocks instead of polling (up to a jiffy of latency today) |
| **§6 crash protection** | a 6510 takes `BRK` through the **IRQ vector with B set**, and `irqHandler` already sees every one: test the bit, reuse the exit path — **~40 B** | yes, in the 215 B run | a stray `BRK` kills one task, not the machine (a bad `JMP` still takes the system down — there is no MMU) |
| **§8 char cache** | the cache itself goes in the workspace for free; the cost is the *integration* — `printChar` lives in the `$E11A` band pinned by 17 ABI asserts, so the change is size-neutral or an assert-and-alias sweep | bytes yes | bulk output and scrollback stop paying a pixel blit per glyph |
| **§2 signals** | a pending byte per task, a syscall on slot 8 or 9, and a test in the yield/sleep path | yes | only pays off once something can block for a long time (§4) |
| **§4 blocking I/O** | a `BLOCKED` state in `switchTask`/`taskState` plus a device table | the state machine does; the drivers do not | the enabler for §3 |
| **§3 streams** | a per-task stream table, `read`/`write` syscalls, a device vtable | no — a project | pipelines, stdin/stdout |
| **§10 FS directories** | a path parser behind filesys.lib (dispatching — `LIB_CALLS` is full) and a change to **every** consumer, host tool included | bytes yes, blast radius large | subdirectories in a flat 512-entry design |
| **§5 IEC disk**, **§7 sector cache** | cycle-exact bit-banging, in a library | yes, but untestable here | only matters on real hardware with a 1541 |
| **§11 C compiler** | a cc65 retarget: KERNAL-free runtime, relinkable image, libc subset | no | a project |

---

## Roadmap

### 1. SEND/RECEIVE IPC — the missing half

**Effort:** Medium-Hard

[`ipc.lib`](lib-ipc.md) is the primitive today, in two halves behind the same two
calls (35/36). **Byte channels** (`X` = `0`-`3`): four fixed 128-byte rings in the
library's own data, single-producer / single-consumer, shared by every declaring
task with no kernel buffer, no new syscall and no caller zero page; `kIpcPost`
blocks while a ring is full, `kIpcPick` never blocks. **Block channels**
(`X` = `$80`-`$83`): whole 256-byte frames of REU bank 250, one DMA per message
with a length per frame, so a payload that used to need a temporary file now
moves in one call.

What is missing, in order of usefulness:

1. **A library-side semaphore.** The kernel's counting semaphores (syscalls 10/11,
   indices 1-3, owner-tracked, released on task death) cover a mutex between two
   tasks, and `semCount`/`semOwner` are published in `kernel-data.inc` so a task
   sets its own initial count with one store. A semaphore living in
   [`ipc.lib`](lib-ipc.md)'s own data would remove the last kernel dependency, but
   it needs a death story: a task that dies holding a library semaphore leaves it
   locked, because the kernel's death-release scan only knows its own
   `semCount`/`semOwner` pool. The library would have to be told, or the pair
   would have to elect a survivor. The design is the blocker, not the space: the
   `LIB_CALLS` **growth reservoir is spent**, so a library semaphore needs its own
   call index (a dispatching entry, or a relocation of the two `LIB_CALLS`
   tables).
2. **Channel allocation.** The four channels of each kind are fixed and anonymous,
   so a pair cannot claim one for itself and the kernel cannot tell when an owner
   dies. Numbering is not the gap — allocation and ownership are.
3. **A wakeup.** A block frame carries its length, but `kIpcPick` still never
   blocks on purpose (a dead producer must not strand a consumer), so a consumer
   polls — up to a jiffy of latency per message.

**Two costed routes to the wakeup**, both short of the design below:

- **Semaphore handshake (library only, ~40 B, no kernel change).** A pair agrees
  on a block channel and one of semaphores 1-3: the consumer `semWait`s, the
  producer `semSignal`s after posting. The kernel already owns the hard half —
  blocking without spinning, and releasing the semaphore when the owner dies
  (`seClean`). What is missing is the convention and a documented rule for who
  signals on the empty path.
- **A real `SYS_RECV` (kernel, ~130 B).** Slots 8 and 9 are inert: 4 B of table
  plus a handler that parks the task and a wake list the producer walks. That
  removes polling entirely, and is the primitive §3 would build on.

**Before either route, at no cost.** The block channels have no consumer outside
`ipctest`. Two are worth wiring up, and they also settle whether the frame API is
the right shape before §3 leans on it: `grview` taking a picture straight from the
task that generated it instead of handing it over as a `.pic` file, and
`musedit`'s player thread taking its tune over a channel instead of paging the
`.wrk` file. Both are task-side changes — no kernel bytes, no library change.

**Unbuilt design — a blocking, zero-copy handoff.** Each task gets a message
queue; `send(target, msg_ptr)` blocks until the receiver calls
`receive(buf_ptr)`, and both block until the handoff completes — no buffering, no
copying. The kernel mediates by swapping message *pointers*.

- Per-task `waitingSender` / `waitingReceiver` fields (2 bytes each)
- `send(tid, msgPtr)` — if the receiver is waiting in `receive`, hand off
  immediately; otherwise block
- `receive(bufPtr)` — if a sender is waiting, hand off immediately; otherwise block
- Zero-copy: the message pointer is passed, not the data

It fits because each task already has a 256-byte stack saved and restored by REU
DMA: the per-task fields slot into existing kernel data arrays, and no buffer has
to be allocated.

---

### 2. Signals

**Effort:** Medium

A task registers a signal handler address and mask. Another task (or an IRQ handler) calls `sendsig(tid, signal)` which sets a bit in the target's pending-signal bitmap. When the target is next scheduled, the kernel patches in a call to the signal handler before resuming normal execution.

**Signal types:**

| Bit | Name | Purpose |
|---|---|---|
| 7 | SIG_INT | Allow blocking syscalls to be interrupted (returns E_INT) |
| 6 | SIG_CHLD | Thread-spawned child task has terminated |
| 3-0 | SIG_USR1-4 | User-defined signals |

**Why it matters:** `kReadString` and `semWait` block forever — no timeout and no cancellation. SIG_INT makes them interruptible; SIG_CHLD lets a parent learn a child exited without polling `ps`.

**Room today:** the pending bitmap is one byte per task (24 B, and a free run can
hold it), and the send is a syscall on slot 8 or 9 (2 B of table, ~60 B of
handler). The cost that decides this item is the *test*: it lands in
`switchTask`'s resume path, which is core part 1 and has the 215 B run to grow
into. Worth doing once something can block for a long time (§4) — before that it
interrupts calls that return in a tick anyway.

---

### 3. Stream Abstraction

**Effort:** Medium (~100 lines)

`GETC`/`PUTC` on named streams. Decouples I/O from hardware and enables shell pipelines.

**Design:**
- Stream table: 8 streams, each with a read-end task and write-end task
- `openStream(readTid, writeTid)` — creates a pipe between two tasks
- `putc(streamNum, char)` / `getc(streamNum)` — blocking read/write on a stream
- Shell keyboard → stream 0 (stdin), shell screen ← stream 1 (stdout)
- Enables: `ls | grep foo | wc`

---

### 4. Blocking I/O & Device Drivers

**Effort:** Hard

Allow tasks to block waiting for I/O (keyboard, serial, disk) instead of polling. The scheduler skips BLOCKED tasks. When I/O completes (via IRQ), the waiting task is unblocked.

**Design:**
- `BLOCKED` state + `blockReason` (keyboard, timer, message, etc.)
- IRQ handlers unblock waiting tasks
- Abstraction: `read`, `write` syscalls
- All blocking calls should be interruptible via SIG_INT (see §2)

**Room today:** the state byte fits — `taskState` gains a value, `switchTask` and
`findNextAlive` gain a test, and both live in core part 1 (the 215 B run). That is
the whole of what fits. The rest of this item is *drivers*, and a driver is a
library: a keyboard one needs nothing new (the IRQ ring already exists), a disk
one needs §5, and both need a per-task stream table that does not fit anywhere
today. This is the next multi-step project rather than an item — and §3 sits
behind it.

---

### 5. File System (IEC / Disk)

**Effort:** Very Hard

Read/write files on 1541/1581 disk drives via the IEC serial bus. Would need a from-scratch bit-banging implementation since KERNAL ROM is banked out.

---

### 6. Crash Protection

**Effort:** Hard

If a task crashes (BRK instruction, invalid memory access), the kernel catches it instead of the whole system dying. IRQ handler checks B flag in saved SR; if set, task caused BRK → kill it.

**Room today, and what it actually buys.** The hook already exists: a 6510 takes
`BRK` through the **IRQ vector** with the B flag set in the pushed status, and
`irqHandler` sees every one, so the change is a test at its entry plus a route
into `sysExit`'s existing teardown — around **40 B**, which fits the 215 B run
with room to spare. The half it cannot buy is the other half of the sentence
above: an invalid *jump* or a wild store is not detectable without an MMU, so
"crash protection" here means a stray `BRK` kills one task instead of the whole
machine — which is still the class of failure that actually happens while poking
at the machine from the shell.

---

### 7. RAM Disk / Sector Cache

**Effort:** High

Use REU as a disk sector cache for the planned IEC filesystem (§5).

```
REU $020000-$02FFFF: 64KB sector cache (256 × 256-byte sectors)
```

**Design:**
- DMA entire 1541 disk tracks into REU for instant access
- LRU or simple ring-buffer eviction policy
- Swap DMA (`$92`) could implement copy-on-write buffering
- The range above sits inside the bitmap-slot band, so it comes out of the FS
  band

---

### 8. Char Cache for Text Surfaces

**Effort:** Medium (designed, not built)

**Motivation.** A non-displayed surface does not scroll. `printChar`'s wrap path
clamps the cursor at the region bottom instead of scrolling when the target
surface is not the displayed one, so a task that prints more than a screenful
while it is in the background keeps overwriting its bottom row instead of keeping
scrollback. Bulk output on the *displayed* surface is slow for the same reason —
every glyph is a pixel blit, so 100 lines of `dir` cost ~1.5-2 s.

**Mechanics:**
- 1KB char-code cache + 25-bit dirty mask per text surface (kernel data)
- Not displayed: print updates the cache + the dirty bit; scroll rotates the
  cache pointer (free); no pixel traffic
- On display-switch: rasterize the dirty rows into the slot (~150 ms one-time),
  then DMA the slot into the framebuffer (~10 ms)
- Displayed: unchanged fast path
- Cost: 1KB + 4B per screen-owning task — additive, no change to the displayed path

**Room today.** The 1KB cache per surface is now free: it fits in the slot's own
**4 KB REU workspace**, so the pool pays nothing and the "per task" cost is a
region every task already has. The expensive part moved: `printChar` lives in the
`$E11A` band, which 17 asserts in `main.asm` pin (external consumers poke
`keybRepeatLda` and gfx.lib resolves scratch addresses inside it), so any size
change there is an ABI change — give the bytes back as an inert `.fill` sled, or
update every assert and every alias in the same commit. Worth doing for the felt
speedup (`dir` output and scrollback), but budget the integration, not the bytes.

---

### 9. Editor Undo / Redo

**Effort:** Medium-Hard (designed, not built)

**Want.** Undo and redo in `edit.tsk` ([edit.md](edit.md)) covering every mutation — typing, overtype,
backspace, RETURN split/join, the line cascade, insert/delete line, paste, cut and
clear — with "a new edit after an undo discards the redo tail" and a "nothing to
undo" report at the oldest slot.

**Chosen model: full-state snapshots in a reserved REU arena, not an inverse log.**
The document is *lossy* at several mutation sites: `edCascDelDone` discards the
character rolled off a full line, `edInsOverflow`'s full-line branch overwrites the
tail character, and its pool-full path drops the overflow character outright. An
inverse log would have to capture every discarded byte and invert splits, joins,
chunk grow/shrink and clipboard spans. A snapshot never inverts anything — one
commit hook per mutation entry point instead of a bespoke inverse per operation —
and with ample REU the storage cost is irrelevant, so it is also the smaller design.

**Shape**

- **Arena.** Reserve a REU region by shortening the FS data region (e.g. banks
  240-251, 768 KB; the FS keeps banks 5-239). The FS allocator is monotonic and
  never reclaims, an FS-backed scratch file caps at 65535 B (≈ 7 snapshots) and
  would show up in `dir`; a reserved arena has no ceiling, no allocator
  interaction and no directory footprint. Costs the capacity formula, the
  reserved-bank asserts and `memory-map.md`; REU images must be re-seeded.
  Cheaper fallback: a reuse-if-present scratch file per instance (the `basicwrk`
  pattern).
  **Answered instead by the per-task workspace: `edit`'s slot already owns 4 KB
  of REU at `REU_WS_BASE`, zeroed at spawn, at no cost to the FS or the pool**
  (`musedit`'s undo stack lives there). No arena to reserve, no re-seed, no
  directory footprint. What the workspace does *not* give is depth: 4 KB is about
  **three full-document snapshots** of a 192-line document, against ~170 records
  of an inverse log. So the storage question is settled and a *new* trade-off
  opens — snapshot depth versus record count — with the workspace as the budget
  either way. A hybrid is defensible: snapshots for the structural edits (insert,
  delete, paste, clear) and a byte-diff log for typing.
- **Slot.** 32-byte header (version, `chunkCount`, `ED_LINES`, `ED_SIZE`, `ED_WIN`,
  `ED_CURL`, `ED_CURC`, `ED_PREFC`, `ED_MOD`, 8 chunk pointers, slot length) plus
  `chunkCount × 1024` bytes of chunk payload. 1 KB per undo level for a one-chunk
  document, ~8 KB for a full 192-line one — depth adapts inversely to document
  size. Chunks are dumped whole, so each transfer is one aligned 1 KB DMA.
- **Ring.** `ED_UPOS`/`ED_UCOUNT` plus a restore-in-progress guard in the instance
  buffer (~5 B; `data_size` 207 → ~212 of a 704 B heap). Snapshot *before* each
  mutation; undo restores `POS−1`, redo `POS+1`; load and new-file push the
  initial state, so undo reaches the file exactly as it was opened.
- **Routines.** `edUndoEnable` (arena + initial state), `edUndoCommit` (write the
  current state), `edUndoRestore`, `edUndoRun`, `edRedoRun`. `edUndoCommit` is
  called before the mutation from `edInsChar`, `edOvChar`, `edDelDispatch`,
  `edReturn`, `edCascadeStep`, `edInsOverflow`, `edInsLine`/`edDelLine`
  (interactive only, never from a restore), `edPaste`, `edCutDelete` and
  `edClear`.
- **Bindings.** STOP+U (`$15`) = undo, STOP+R (`$12`) = redo; both screencodes are
  free.
- **Cost.** A full 8 KB snapshot is ~8 ms at 1 byte/cycle — cheaper than the screen
  redraw the editor already does per keystroke, so per-keystroke snapshots are
  affordable.

**Traps.** Undo can need *more* chunks than are currently allocated (undoing a
large delete that shrank the document), so the restore either grows chunk by chunk
or all 8 chunks are pre-allocated when undo is enabled (32 pages). Undo must cancel
an active selection. The clipboard is deliberately outside history, so "undo a cut,
then paste" duplicates text. `ED_MOD` needs either an unconditional set or a saved
clean position that clears it.

**No new kernel calls** — `kReuToC64`/`kReuFromC64` (or `kReuWriteAt`/`kReuReadAt`
if the arena is exposed as a file) are enough.

**Room today: none of it is kernel code.** Every routine above is `edit`'s own
(~250 B against margin 31 pool pages), the workspace seat is `SYS_GETTID` plus the
same four shifts `grpaint` and `musedit` use, and the bindings are two free
screencodes. This is the cheapest item on the page, and the only design decision
left is the snapshot-versus-log split above.

**Done when:** undo/redo work for every mutation, including the lossy cascade paths
and across chunk grow/shrink boundaries, with no pool or arena leaks.

---

### 10. REU FS Directories

**Effort:** Medium (traversal API) / Hard (subdirectories) — designed, not built

**Today the filesystem is a flat namespace of 512 fixed 32-byte entries** in bank 5
(`$9000-$CFFF`) plus a 24-bit data region from `$D000`. Every consumer walks the table
itself: `kReuDirEntry` reads entry *n* by index, and the callers (`dir`, `del`, `type`,
`copy`, BASIC's `DIR`) loop `n = 1, 2, ...` until it returns C=1. That works, but it
makes every consumer a directory scanner and leaves no room for a path.

**1. Traversal API — `kReuOpenDir`/`kReuReadDir`/`kReuCloseDir`.** Open/read/close,
mirroring POSIX `opendir`/`readdir`/`closedir`, so a caller iterates without knowing
the table layout:

```
// kReuOpenDir  — begin traversal
// Input:  kPtr/kPtr+1 = path (a directory, e.g. "/")
// Output: C=0 opened (handle kept in the kernel), C=1 not found / not a directory
//
// kReuReadDir  — next non-deleted entry
// Input:  kPtr/kPtr+1 = C64 buffer, 13 bytes or more (12 chars + null)
// Output: C=0 filled (kArg/kArg+1 = size, kArg+2 = flags),
//         C=1 end of directory, or no open handle
//
// kReuCloseDir — end traversal, free the handle
```

One **global** handle — single-threaded traversal, matching the rest of GordonOS (one
`kReadString` buffer, one `reuDirBuf`). The read step skips deleted entries (flags b7).
The scan itself already exists inside `kReuDirEntry`, so this is mostly saved cursor
state plus three jump-table entries — and the band has **no** spare slot left (`$E0F3`
holds `kPrintGlyphAt` and `$E0F6` `kMouseCall`), so each of these entries needs a home
outside the `$E000` table.

**2. Subdirectories.** The design is Lee Davison's ROM FS model (`reference/filesys.asm`)
— already the source of the entry format and `kReuCmpName`. A directory is an entry with
the directory bit set whose payload is a *pointer list* to its children in the REU data
area rather than in the fixed table: files keep their 32-byte entry, directories get one
indirection. `LAB_find`/`LAB_directory` port to `reuPathFind` (a '/' -separated path
walker, ~120B) plus `reuDirStep`/`reuNextEntry`, and two new calls: `kReuMkDir` (a header
plus an empty pointer list) and `kReuRmDir` (mark deleted, refusing a non-empty
directory). A name then identifies a path rather than a leaf, so every existing caller —
`run`, `save`, `load`, `del`, `dir`, BASIC's `DIR` — becomes a path consumer.

**Sequencing:** do (1) first — it is useful on its own and it is the traversal primitive
(2) needs.

**Room today.** Three new calls cannot go in the `$E000` table (no slot left), and
`LIB_CALLS` is full, so they are dispatching entries or a relocation — the same
constraint §1's library semaphore runs into, which is why both would be cheaper
after one deliberate relocation of the four scan tables than before it. The path
walker itself (~120 B) fits the 215 B run. The real cost is the blast radius: a
name becomes a path, and every caller changes — `run`, `save`, `load`, `del`,
`dir`, `rename`, `copy`, BASIC's `DIR`, both loaders, and the host tools
(`reufs.py`/`reufs-gui.py` decode the same table). Worth it only alongside a
reason to nest.

### 11. A C compiler

**Effort:** Hard — a toolchain retarget, not a kernel change

EhBASIC is the only high-level language in the image (`basic.tsk`), and floating point has
exactly two consumers: the interpreter and `fpdemo`. Every other task is 6502 assembly.
There is no reason to *write* a compiler — 6502 C compilers exist and are mature, and a
compiler is a host tool: it costs the machine nothing, it is what it *produces* that has to
fit the pool.

- **cc65** (zlib licence) is a complete cross-development package — compiler, the `ca65`
  macro assembler, the `ld65` linker — with the C64 among its many 65(C)02 targets, and it
  documents the path this needs: *how to use the toolset for a custom hardware platform*,
  plus a generic target configuration to copy. It is itself a descendant of Ron Cain's
  **Small-C**, as extended by James E. Hendrix — the original "small C compiler", whose
  porting story is exactly this one: rewrite the code generator and the OS interface and
  nothing else.
- **Oscar64** (GPL-3.0) — "optimizing small memory C compiler, assembler and runtime for
  the C64": C99, selected C23 and a good part of C++ (variadic templates, lambdas), with
  the author's own games as the demonstration. ⚠️ The licence is the catch, not the code:
  this tree is GPL-2.0-*or-later*, so a GPL-3.0 runtime linked into a task makes that task
  GPL-3.0 — permitted by "or later", but a licence decision rather than a technical one.
- **llvm-mos** — an LLVM fork with a 6502 back end and its own C64 SDK: the most capable
  and by far the largest option, and the obvious one if the goal were maximum code quality
  rather than a toolchain that fits in your head.
- **SubC** (public domain, ~100 KB, internals documented in *Practical Compiler
  Construction*) is the smallest clean C subset around, but it has **no 6502 back end** —
  x86, ARM and DOS only — so choosing it means writing the back end.

**The work is the port, in four parts, in this order:**

1. **A KERNAL-free runtime.** Every existing C64 C runtime calls the KERNAL: the `$FFxx`
   vectors for I/O, the zero-page pointer block at `$FB-$FE`, the `$0100` stack. None of
   that is available here — the kernel owns `$FFFA-$FFFF`, zero page is per task and
   DMA-preserved, and each task has its own 256-byte stack. Startup, console I/O and memory
   allocation have to be re-expressed on this OS: the syscalls in [syscall.md](syscall.md),
   the screen routines in [kernel.md](kernel.md), the `kMalloc` heap, and
   [filesys.lib](lib-filesys.md) for files.
2. **A relocatable *task*, not a PRG.** The output has to carry a task header and a fixup
   table ([writing-relocatable-tasks.md](writing-relocatable-tasks.md)) so the loader can
   place it anywhere in the pool. The fixup table is **derived, not written** — the pipeline
   assembles the image at shifted bases and records which bytes moved — so the port's one
   hard requirement is that the whole image can be **re-linked at a different base** on
   demand. A `ld65` configuration plus a `crt0` that ends in the task entry contract is
   where that lands; the C build would then carry its own assembler and linker (`ca65`,
   `ld65`) beside the KickAssembler the kernel keeps building with.
3. **Floating point lands on `fp.lib`.** cc65 documents plainly that `float` and `double`
   "are not available ... floating point arithmetic expressions are not supported", and the
   other compilers bring their own arithmetic. This OS already has one: `fp.lib` is the
   EhBASIC core — **40-bit floats**, 26 opcodes behind a single dispatcher
   ([lib-fp.md](lib-fp.md)). A C `float` therefore maps onto the library's value, and `+`
   `-` `*` `/` `^` and the transcendentals become opcode calls: far less work than porting
   a soft-float library, and one shared implementation for BASIC, `fpdemo` and any C task.
4. **The library surface is thin.** A header of wrappers over the jump table
   ([kernel.md](kernel.md)) plus [lib-gfx.md](lib-gfx.md), and a libc subset small enough
   to fit the pool budget (`tools/check-pool-budget.ps1` — a *hello world* that costs thirty
   resident pages is not a C compiler worth having).

**Recommendation:** cc65 — the licence is permissive, the custom-target path is documented,
and the compiler is small enough to read. The first milestone is a `hello.c` that builds into
a task, prints through the kernel, and carries a derived fixup table.

## Recommended order

Ranked by what it costs today against what it buys — not by ambition. The first
three are all inside the 215 B run or task code alone.

| Priority | Idea | Cost today | Why |
|---|---|---|---|
| **1** | Editor undo / redo (§9) | **0 kernel bytes**, ~250 B of `edit` code, margin 31 pages | The last editor without history gets it, and the storage question is already answered by the slot's 4 KB workspace |
| **2** | IPC wakeup (§1) | ~40 B of library code (semaphore handshake) or ~130 B kernel (`SYS_RECV` on slot 8/9) | Removes up to a jiffy of poll latency per message, and is the primitive §3 builds on |
| **3** | Crash protection, the reachable half (§6) | ~40 B in the 215 B run | A stray `BRK` kills one task instead of the machine. A bad `JMP` still takes the system down — no MMU |
| **4** | Char cache (§8) | cache is free in the workspace; the integration is an ABI-change budget | Bulk output and scrollback stop paying a pixel blit per glyph |
| **5** | Signals (§2) | 24 B of state + ~60 B handler, plus a test in `switchTask` | Only pays off once something can block for a long time (§4) |
| **6** | Blocking I/O (§4) | the state machine fits the 215 B run; the drivers do not | The enabler for §3, and the next multi-step project |
| **7** | Stream abstraction (§3) | needs §4 plus a per-task stream table | Pipelines, stdin/stdout |
| **8** | REU FS directories (§10) | path walker fits; three new calls need a relocation, and every consumer plus both host tools change | Subdirectories, in a namespace that is flat by design |
| **9** | RAM disk (§7) | needs §5 | A sector cache for a filesystem that does not exist yet |
| **10** | File system, IEC/Disk (§5) | a library, no kernel space | Only matters on real hardware with a 1541 — the REU filesystem is what ships |
| **11** | A C compiler (§11) | a toolchain retarget | cc65 is the candidate; `float` lands on `fp.lib`; the work is the runtime, the relinkable image and the libc subset |

Two sequencing facts worth keeping in mind. **A single relocation of the four
`LIB_CALLS` scan tables buys several later items at once** — the library semaphore
(§1), the FS traversal calls (§10) — and costs one commit that moves the pinned
addresses behind them together with every alias. And **anything that grows the
core band in two places** (`irq.inc` for §6 and §2, `switchTask` for §4) is
competing for the same 215 B run, so those changes are best done one at a time,
each verified with `tools/verify-map.ps1` before the next.
