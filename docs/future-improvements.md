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
| **§9 `edit` undo/redo** | ~400 B of `edit` code and ~12 B of instance fields; **0 kernel bytes** | yes — `edit` has margin 26 pool pages | the last editor without history gets it |
| **§1 IPC wakeup** | (a) a semaphore handshake: syscalls 10/11 exist and release on death — **~40 B of library code, 0 kernel**; (b) or a real `SYS_RECV` plus a wait list — 4 B of table and ~130 B of handler | yes, both | a consumer that blocks instead of polling (up to a jiffy of latency today) |
| **§6 crash protection** | `irqHandler` already sees every `BRK`; add the status-flag test and a route into the teardown — **~40 B** | yes, in the 215 B run | `BRK` becomes a defined task abort instead of being swallowed (a bad `JMP` or wild store is still undetectable — there is no MMU) |
| **§8 char cache** | the 1 KB mirror already exists in bank 0, so nothing new is allocated; the cost is the integration — `printChar`/`wrapClamp` sit in the `$E11A` band pinned by 17 ABI asserts — plus ~200 B for the rasteriser, dirty mask and rotation | bytes yes | stage 1: a hidden surface keeps scrollback and read-back stops being opt-in; stage 2: bulk output stops paying a pixel blit per character |
| **§2 signals** | a pending byte per task, a syscall on slot 8 or 9, and a test in the yield/sleep path | yes | only pays off once something can block for a long time (§4) |
| **§4 blocking I/O** | a `BLOCKED` state in `switchTask`/`taskState` plus a device table — and `findNextAlive` warm-reboots when no task is eligible, so it needs a guaranteed-runnable fallback | the state machine does; the idle task and drivers do not | the enabler for §3, and the end of `kWaitKey` spinning on a whole quantum |
| **§3 streams** | a per-task stream table, `read`/`write` syscalls, a pipe buffer in REU, a device vtable | no — a project | pipelines, and a child's output becoming capturable at all |
| **§10 FS directories** | a path parser behind filesys.lib (dispatching — `LIB_CALLS` is full) and a change to **every** consumer, host tool included; one global traversal handle | bytes yes, blast radius large | a traversal cursor, then nesting — not more files (the table is fixed at 512 slots) |
| **§5 IEC disk**, **§7 sector cache** | cycle-exact bit-banging in a library; **the 60 Hz tick has to be masked across a transfer**, so multitasking stops for tens of ms per sector | bytes yes; untestable in this harness | only matters on real hardware with a 1541. §7 is blocked on §5 — the shipping filesystem is already RAM |
| **§11 C compiler** | a cc65 retarget: KERNAL-free runtime, relinkable image, libc subset | no | a project |

---

## Roadmap

### 1. SEND/RECEIVE IPC — the missing half

**Effort:** Low for the wiring (task-side), Medium for a real wakeup (the design is the
blocker, not the bytes)

**Want.** Three things the primitive does not have: a mutex inside the library, a way for a
pair to *own* a channel, and a consumer that blocks instead of polling.

**What already ships** ([lib-ipc.md](lib-ipc.md), calls 35/36):

- **Byte channels**, four of them (`X` = 0-3): 128-byte rings in the library's own data,
  127 usable slots (one stays empty so full and empty differ), single-producer /
  single-consumer. `kIpcPost` blocks while the ring is full — by `SYS_SLEEP`, one tick at a
  time — and `kIpcPick` never blocks (carry set on empty).
- **Block channels**, four of them (`X` = `$80`-`$83`): 64 frames of 256 bytes in REU bank
  250 (`REU_IPC_BASE`, `$FA0000`), one DMA per message. The length lives in the library's
  `ipcBlkLen` array, not in a frame header, and a zero-length post is rejected.
- **Kernel semaphores**: four (`MAX_SEMAPHORES`), `semCount` at `$E120` and `semOwner` at
  `$E124`, with the death release in `sysExit`'s `seClean` and `sysKill`'s `skDeadSem` scan.
- `ipc.lib` owns `LIB_CALL` entries 35 and 36; `LIB_CALLS` is 42 and **full**.

**What is missing.**

1. **A semaphore in the library.** The kernel's four cover a mutex between two tasks, but a
   semaphore living in `ipc.lib` would remove the last kernel dependency — and it needs a
   death story the kernel's scan cannot provide, because that scan only knows its own tables.
2. **Allocation and ownership.** Four channels of each kind are fixed and anonymous: a pair
   cannot claim one, and the kernel cannot tell when an owner dies.
3. **A wakeup.** `kIpcPick` never blocks on purpose (a dead producer must not strand a
   consumer), so a consumer polls — up to a tick of latency per message.

**What it buys.** Removes the tick of latency per message, and — more useful — turns "is
there a message" from a poll into a wait, which is what §3 needs to build on.

**Caveats.**

- The **death release is narrower than it looks**: it reclaims an *exhausted mutex* held by a
  dying task, not a partially consumed count. A library semaphore that copies that rule
  inherits the same hole, and a pair cannot repair it without the kernel.
- A `SYS_RECV` needs a **death sweep of its wait list** — the same "a missing parent is not
  proof of safety" problem `childGate` already solves for thread children.
- `LIB_CALLS` is full, so a library semaphore is a *dispatching* entry (the `kFpCall` shape),
  not a new table slot; that is the one structural cost in the item.
- Only `ipctest` uses block channels. Two consumers worth wiring before any design change —
  `grview` taking a picture straight from the task that made it, and `musedit`'s player
  taking its tune over a channel instead of paging its `.wrk` file — are task-side changes
  and would prove the frame API before §3 leans on it.

**Room today.** ~40 B inside `ipc.lib`'s own pool pages for the handshake (zero kernel
bytes); a real `SYS_RECV` is 2 B of table on slot 8 or 9 plus ~130 B of handler, which the
215 B run holds.

---

### 2. Signals

**Effort:** Medium, and it depends on §4 rather than the other way round

**Want.** A task registers a handler and a mask; `sendsig(tid, sig)` sets a bit in the
target's pending byte; when the target is next scheduled the kernel enters the handler.
SIG_INT would make a blocking call interruptible (`E_INT`), SIG_CHLD would tell a parent its
child exited.

**What already ships.** Nothing — there is no pending bitmap, no handler field and no `SIG_`
constant anywhere in `src`.

**What the waits look like today** — this is what a signal would have to interrupt:

| Call | How it waits | Cancellable |
|---|---|---|
| `semWait` (syscall 10) | `SYS_YIELD`, then retry the test | no |
| `kWaitKey` | **spins** on `scanKeyboard` — no yield at all | no |
| `kIpcPost` on a full channel | `SYS_SLEEP` one tick, then retry | no |
| `SYS_SLEEP` (13) | timed: the IRQ counts down, wakes at zero | n/a (timed) |

**What it buys.** The first three are unbounded waits with no way out, and the shell's own
child-wait is a fourth (it polls `taskState` and yields until the child is `TA_DEAD`).
SIG_CHLD would replace that poll; SIG_INT would let a wedged wait be abandoned.

**Caveats.**

- **A spinning task cannot see a signal.** `kWaitKey` never yields, so it never reaches a
  scheduler decision — the delivery point only exists for tasks that already yield or sleep.
  That makes §4 (a scheduler-visible wait) a *prerequisite*, not a follow-on.
- The delivery point is `switchTask`'s resume path, in core part 1 — the 215 B run is the
  only place it can grow, and §4 and §6 want the same bytes.
- A handler runs on the task's own 256-byte stack, and there is no second stack to give it.
  A handler that needs much (a register save, a longjmp) is out of reach; a handler that sets
  a flag and returns is not.
- One byte per task is eight signals — enough, but there is no queue: two signals of the same
  bit collapse into one.

**Room today.** 24 B of state (one byte per task, and a free run holds it) + 2 B of table and
~60 B of handler on slot 8 or 9.

---

### 3. Stream Abstraction

**Effort:** Hard — a project, and it is the item with the least underneath it

**Want.** `GETC`/`PUTC` on named streams — pipes between tasks, stdin/stdout in the shell, so
`dir | grep foo` means something.

**What already ships.** No stream table, no file-descriptor table — nothing in `src/kernel`,
`src/libs` or `src/tasks`. The nearest things are the REU filesystem's open record
(`kReuOpen`/`kReuReadAt`/`kReuWriteAt` in `filesys.lib`) and the kernel's *global* transfer
state (`reuFsFile`, `reuFsLen`, `reuFsFlags`, `reuDirBuf`) — global by design, which is why
a second concurrent reader has to be excluded by convention.

There is also **nothing to redirect**: a child task draws on its parent's screen through the
parent link, and the shell's `run` loop polls `taskState` until the child dies.

**What is missing.** All of it: a per-task descriptor table, a device/producer abstraction
behind it, `read`/`write` syscalls (or one dispatching library call), a pipe buffer, and a
shell syntax to wire two ends together.

**What it buys.** Pipelines; capture of a child's output (today a child can only draw on
whatever surface its parent owns); and a single place where "where does this task's output
go" is decided — which is the precondition for any redirection, including to a file.

**Caveats.**

- **A blocking `getc` needs §4's `BLOCKED` state.** Without it, `getc` is a poll — which is
  what the shell already does, at a yield per retry — so the abstraction would buy syntax and
  lose nothing else.
- **The pipe buffer has to live in REU.** A 256-byte pipe per end is pool *data*, and `basic`
  is on margin 1 page. The slot's 4 KB workspace (or a bank like the IPC frames) is the only
  affordable home, which means a pipe is a DMA per refill, not a ring in RAM.
- **The shell cannot hold it.** Its instance buffer is 124 B and fully used (line buffer plus
  the task header fields), so stream handles are kernel state or nothing.
- **A global handle is a trap the FS already demonstrates**: one `kReadString` buffer, one
  directory buffer, and two tasks that want them at once have to take turns. A stream layer
  that repeats that pattern is not worth building.
- `LIB_CALLS` is full: a stream API is a dispatching entry (`kStreamCall`) or a syscall, and
  syscall slots 8/9 are also contested by §1 and §2.

**Room today.** None of it exists; the table is kernel data, the code is a dispatcher plus a
couple of hundred bytes, and the work is in deciding what a stream *is* here.

---

### 4. Blocking I/O & Device Drivers

**Effort:** Hard, and it carries a constraint that is not in the original sketch

**Want.** A task asks for I/O, the scheduler skips it until the device answers, an IRQ wakes
it. No polling anywhere.

**What already ships.** Five task states — `TA_DEAD` (0), `TA_ALIVE` (1), `TA_PAUSED` (2),
`TA_SLEEPING` (3), `TA_SWAPPED` (4) — and `TA_SLEEPING` with `taskSleepTicks` is already the
shape a blocked task needs: the IRQ decrements the countdown and marks the task `TA_ALIVE` at
zero. A blocked task is the same machine with an event instead of a countdown. The keyboard
IRQ ring (16 entries) is the device side that already exists.

**What is missing.** A `BLOCKED` value, a wait-reason field, a wake path per device, and the
drivers themselves (a keyboard driver needs nothing new; a disk driver needs §5).

**⚠️ The constraint that decides the design.** `findNextAlive` accepts only
`taskState == TA_ALIVE` and **warm-reboots when no slot is eligible**. So a state that can
swallow every task turns "everything is waiting" into a reboot. A blocking design therefore
needs one of: a guaranteed-runnable idle task, or a rule that the last blocked task is forced
runnable. Neither exists today.

**What it buys.** Removes the spin: `kWaitKey` burns a whole time slice per scan today, and
`semWait` costs a context save and restore per retry. A blocked task costs the scheduler one
skipped slot, and — with an idle task — the machine stops burning cycles when there is
nothing to do.

**Caveats.**

- A wake path runs **in IRQ context**, where a syscall is illegal (the dispatcher re-enables
  interrupts); the wake must be a flag the scheduler reads, not a teardown.
- Every blocked state needs a timeout story, or one dead device wedges a task forever — and
  the only existing timeout mechanism is the sleep countdown, which a device wait cannot use
  without losing the wake.
- A per-task device table has nowhere to live: the shell's instance is 124 B and full, and
  pool data is on margin 1 for `basic`.
- §2 (interruptible waits) and §6 (the crash hook) both want the same 215 B run as this item's
  `switchTask` change, so they have to be sequenced, not batched.

**Room today.** The state machine fits the 215 B run: a state value, a reason byte per task,
and a test in `findNextAlive`. Everything else — drivers, a device table, an idle task — does
not exist.

---

### 5. File System (IEC / Disk)

**Effort:** Very Hard, and it is the only item here that must stop multitasking

**Want.** Read and write real floppy disks — `LOAD`/`SAVE` and the 1541's file protocol over
the serial bus.

**What already ships.** A complete REU filesystem: 512 directory slots of 32 bytes at bank 5
`$9000-$CFFF` (slot 0 is the header, so 511 files), data from `$05D000`, capacity 15 872 000
bytes, with open/read/write/delete/rename/compact and a host-side editor (`reufs.py`, which
`reufs-gui.py` imports) that keeps the image and the C64 in step.

**What is not there.** Any IEC code at all — no references to the CIA2 port, ATN, CLOCK or
DATA anywhere in the tree. The KERNAL is not available either: startup writes `$35` to `$01`
(HIRAM=0), so `$E000-$FFFF` is RAM holding this kernel and its vectors. Every `$FFxx` vector
the ROM would have provided is gone, so the driver has to be written from scratch.

**What is missing.** Bit-banged ATN/CLOCK/DATA with cycle-exact timing, the 1541's command
protocol (channel 15, secondary addresses, error-channel reads), a sector or file-level layer,
and error handling for "no drive", "device not present", and a drive that is mid-command.

**What it buys.** Compatibility with real hardware, and movement of files between the two
filesystems. From inside an image, nothing: `reufs.py` already moves files in and out.

**Caveats.**

- **It cannot be preempted.** An IEC byte is a timed sequence of line states; the 59.2 Hz
  scheduler tick has to be masked across it or the transfer corrupts. That means the
  scheduler, the sprite commit and screen output all stop for the duration — tens of
  milliseconds per sector — which is the one place in this roadmap where multitasking has to
  give way.
- **Untestable in the current flow.** VICE needs a true-drive emulation with cycle-accurate
  CIA line timing; the `vice-drive.py` harness drives through the binary monitor and has no
  disk model.
- Two filesystems then coexist, and nothing in the tree can copy between them — that bridge
  is a second project, not a detail.
- `filesys.lib` (2 438 B, 10 pool pages) is the model for where a driver *would* live: a lazy
  library. But a driver that runs with the scheduler stopped is nothing like the existing
  libraries, none of which touch interrupt state.

**Room today.** A library, so the pool pays 10-20 pages and the kernel pays nothing except
the SEI window's consequences. The bytes are not the problem; the timing is.

---

### 6. Crash Protection

**Effort:** Low, once the premise is corrected

**Want.** A fault in one task should kill that task, not the machine.

**What actually happens today.** `irqHandler` opens with `cld` and saves the registers; it
tests neither the pushed status register nor the B flag. The vector a `BRK` takes is the IRQ
vector, so a `BRK` runs the ordinary tick path and the task is resumed through `RTI` with no
abort at all — there is no crash, and no diagnosis either. So the item is not "stop the
machine dying": it is "give a task a defined way to die".

**What it buys.** `BRK` becomes a task abort — an assert/panic instruction an assembly task
can execute deliberately — routed into `sysExit`/`sysKill`'s existing teardown so the slot,
its surfaces, its pages and its child threads are reclaimed. It is also the only fault a 6510
lets the kernel see at all. That is worth having: "load a new task, watch it wedge, want to
kill it" is daily work in this tree.

**Caveats.**

- **A wild `JMP`, a wild store, or a stack overflow remains undetectable** — there is no MMU
  and no guard page. Reading a rogue pointer never faults here; it silently corrupts whatever
  it lands on, and the crash surfaces somewhere else entirely.
- A `BRK` cannot be told apart from *executed data that happens to be `$00`* — the same
  instruction. The distinction is diagnostic, not structural: the register dump at the moment
  of the BRK is the only evidence of which happened.
- The handler must **not** run the ordinary tick when it decides the interrupt was a `BRK`,
  or a task that panics in a loop doubles the tick rate and starves everything.
- It is not a supervisor: a task that corrupts kernel data (or another task's slot) cannot be
  caught by anything the 6502 offers.

**Room today.** ~40 B at `irqHandler`'s entry plus a route into the teardown — inside the
215 B run, with room to spare.

---

### 7. RAM Disk / Sector Cache

**Effort:** High, and it is blocked on §5 — with one correction to its premise

**Want.** A disk sector cache in REU: whole 1541 tracks DMA'd into REU so a repeated read
costs nothing.

**The premise needs correcting.** There is no disk filesystem to cache. The filesystem that
ships *is* RAM — the REU FS — and its read path is already one DMA per transfer
(`kReuReadAt`), so "a sector cache over the REU FS" would be caching RAM in RAM. The idea only
means something for a real disk, i.e. after §5.

**What is missing.** §5 first; then a track/sector model, an eviction policy, and a
correctness rule for a cache that the drive can invalidate (a disk swap, a write to a cached
track).

**What it buys.** For a real 1541: the difference between ~2 s and ~0.2 s to re-read a
directory, because the bottleneck is the serial bus, not the REU. Nothing at all until §5
exists.

**Caveats.**

- **The proposed range is inside the picture band.** `$020000-$02FFFF` is bitmap-slot space
  (slots are 10 KB each from `$010000`), so a 64 KB cache comes out of the graphics band, not
  out of the FS data region. The FS capacity formula (`$3000 + (banks − 14) × $10000`) is what
  a reserve has to change, and every `memory-map.md` figure with it.
- **Allocation in the FS is monotonic between compactions** — deletes leave tombstones and
  `compact` repacks — so a reserved cache region must be re-derived whenever the FS grows, or
  a `compact` will hand it to a file.
- The **swap-DMA (`$92`) copy-on-write** idea is unverified. Do not build on it without
  reading the REU's documented behaviour first (Rule 9).
- A cache in the kernel is the wrong home: it belongs beside the driver, as a library, so a
  machine without a drive pays nothing for it.

**Room today.** Not applicable — nothing is allocated, and the sizes depend on §5's driver.

---

### 8. Char Cache for Text Surfaces

**Effort:** Medium for the correctness half (stage 1), Medium-Hard for the speed half (stage 2)
— designed, not built

**Want.** Two separate things, which need two different mechanisms:

1. **Scrollback for a hidden text surface.** A non-displayed surface does not scroll —
   `wrapClamp` clamps the cursor at the region bottom and skips `scrollScreen` when
   `surfDisplayed` is false — so a background `dir` or `type` keeps overwriting row 24 and
   loses everything above it.
2. **Bulk output whose cost follows the screen, not the volume printed.** Every printed
   character costs a font-glyph REU DMA (`fontGlyph`, 8 B) plus an 8-byte blit — to the
   framebuffer when displayed, to the REU slot when hidden (`blitCharSlot`) — plus the video
   matrix nibble. A scroll moves ~8.3 KB through `REU_BOUNCE` (23 × 320 pixels + 23 × 40
   matrix). 100 lines of `dir` is ~4 000 characters + 100 scrolls ≈ 2 M cycles ≈ 2 s at 1 MHz.

**What already ships.** The 1 KB per-surface char mirror exists, and it is the half that is
hard to get right: `kSetCharShadow` (opt-in), 1 KB per surface at
`SHADOW_BASE + gfxSlot × $400` in bank 0, `taskShadowHi` cached per task, mirrored by
`pcShadowChar` from `printChar`/`clearScreen`/`scrollScreen`, read back through `kCharAt` and
`kReadRow`, blanked for a fresh slot by `allocScreen` and surviving pool eviction. So the job
is not "add a cache"; it is "promote the mirror to the text model, and stop doing pixel work
for text that is about to scroll away".

**What is missing.**

- The mirror is **opt-in and a side effect**: `printChar` still blits pixels on every
  character, so a hidden surface pays full pixel cost and still cannot scroll, and a task
  restored from eviction must call `kSetCharShadow(1)` again or read-back silently degrades to
  `$20`.
- **No dirty tracking**: nothing can say "this row needs rasterising", and `pcShadowScroll`
  moves R × 40 bytes through scratch RAM on every scroll.
- **No rasteriser**: nothing draws a row *from* the cache into a slot.

**Shape — stage 1 (correctness).**

- The mirror becomes **unconditional** for text surfaces: the flag and its re-arm discipline go
  away, and `kCharAt`/`kReadRow` become exact on every text surface.
- **Dirty mask**: 25 bits plus a row offset per surface, bits set by `printChar`, all set by
  `clearScreen` and by any scroll. Marking every row dirty on a scroll costs nothing extra,
  because a rasterise is at most 25 rows.
- **Scroll is a rotation** for a hidden surface: the row offset advances instead of moving
  bytes, so `wrapClamp` loses its `surfDisplayed` test.
- **Rasterise on switch**: save the outgoing slot, rasterise the incoming surface's dirty rows
  into its slot with `blitCharSlot`, then load the slot (~80-120 B of kernel code).
- Result: hidden scrollback works and read-back is universal. The *displayed* path is
  untouched, so `dir` speed is unchanged — this stage buys correctness, not speed.

**Shape — stage 2 (speed).** Speed appears only if rasterisation is **deferred**: cache the
characters, mark rows, and rasterise when the screen is next looked at. That needs a flush
rule, and the rule — not the code — is the decision:

- an explicit bulk mode a task opts into (`kPrintBulk` on/off, flush at the end), which leaves
  interactive output byte-for-byte as it is today; or
- a flush on block-for-input and on switch-in, which is invisible for a background task but
  makes a *displayed* burst of 100 lines show only its last screen — today the user watches
  that text scroll past, and the animation is something a user can lose.
- a per-frame budget in the idle path is the third option and the worst: it splits ~250 k
  cycles of rasterise over 12+ frames and delays everything else.

With stage 2 the arithmetic is ~60 cycles per character (the existing `pcShadowChar` already
does one 1-byte DMA plus the cell maths), a scroll is a row-offset bump, and the visible 1 000
cells rasterise once at ~250 k cycles: **~4×** on the 100-line case, and flat as the volume
grows.

**Caveats.**

- **A rasterise erases what it cannot reproduce.** `kBlitChar`/`kBlitCharRev`,
  `kPrintCharAt`, `kPrintGlyphAt` and all of gfx.lib draw pixels and deliberately bypass the
  mirror — the status bar, `gortris`'s frame, every graphics surface, and any text surface
  with graphics drawn on it. Those writers must either write the cache too (with their own
  nibble) or their cells must be excluded. This is the part that reaches into tasks and
  libraries, and it is why the cache cannot be authoritative for a surface that also draws
  pixels.
- **Colour is not in the cache.** A cell's colour is the video-matrix nibble `(fg<<4) | bg`,
  and reverse-video bars invert it. Faithful rasterisation needs a second ~500 B-1 KB per
  surface, or a documented rule that a rasterised surface uses the task's uniform colour.
- **Switch-in gets slower.** Today a hidden surface's slot is already correct, so a switch is
  one DMA (~10 ms). Inverted, a task that printed a lot while hidden costs up to 1 000 cell
  blits on the way in — nearer 250 ms than the 150 ms this item used to quote.
- **Cursor blink.** The block is XOR'd into pixels at a saved position; a rasterise must
  re-apply it and must not capture it. `printGlyph` already dodges the same class of problem by
  mirroring a space.
- **A 1-byte cache write is not free** — ~35-45 cycles of DMA setup. The win is 4-5×, not "no
  cost"; an 8-cell staging buffer is what would make the per-character cost small enough to
  stop paying the font DMA as well.
- **The integration is the budget, not the bytes.** `printChar` and `wrapClamp` live in the
  `$E11A` band that 17 asserts in `main.asm` pin (`keyrpt` pokes `keybRepeatLda`, and gfx.lib
  resolves scratch addresses inside it), so a size change there is an ABI change: give the
  bytes back as an inert `.fill $ea` sled, or update every assert and every alias in the same
  commit. Outside the band the tree holds 443 B across 28 pockets and 95 B of gfx-chain room.

**Room today.** The 1 KB is already allocated — `$2000-$7FFF` in bank 0 is reserved for 24
surfaces of 1 KB ([memory-map.md](memory-map.md)), page-aligned per slot, and it needs nothing
from the pool or from the per-task workspace. Stage 1 is ~150-200 B of kernel code (dirty mask,
rotation, rasteriser, switch hook); stage 2 adds the flush rule and a colour decision.

**Done when:** a background task that prints more than a screenful keeps its history, `kCharAt`
is exact on every text surface with no opt-in, a display switch costs no more than the dirty
rows, and — stage 2 — bulk output on a displayed surface no longer costs a pixel blit per
character.

---

### 9. Editor Undo / Redo

**Effort:** Hard (designed, not built — a first engine is parked on the `edit-undo` branch, see *Status*)

**Want.** Undo and redo in `edit.tsk` ([edit.md](edit.md)) covering every mutation — typing,
overtype, backspace, RETURN split/join, the line cascade, insert/delete line, paste, cut and
clear — with "a new edit after an undo discards the redo tail" and a "nothing to undo" report
at the oldest slot. Three requirements decide the model:

1. **The cursor comes back to the change.** An undo of an insert leaves the cursor where the
   inserted text began, an undo of a delete leaves it after the reinserted text, and a redo
   mirrors it.
2. **A run of typing is one step.** Typing `hello` and undoing removes the whole word, not one
   character. The run ends when the next edit does not continue the previous one — a cursor
   move, a mode change, RETURN, a block operation, a backspace.
3. **A block operation is one step**, however big: paste of a 200-character clipboard, cut of a
   marked range, delete of many lines. Nothing may cap an undo step at a fixed record size.

**How the established editors do it.** *(sources at the end)*

- Runs are coalesced by **position adjacency**, not by a timer. Wine's Win32 EDIT control — the
  model behind Notepad — extends an insert run when the new insert sits at `undo_position` or at
  `undo_position + undo_insert_count`, and extends a delete backwards (backspace) or forwards
  (delete) from the end of the saved buffer. Scintilla coalesces an insert at `prevPos + prevLen`
  and a one- or two-character remove adjacent to the previous one. VS Code closes a "Typing"
  stack element as soon as the next operation is not typing; Emacs closes a change group at each
  key sequence but amalgamates consecutive `self-insert-command`; Vim makes one typed command one
  block.
- **Cursor after undo** has two schools. *Derived*: Scintilla computes the caret from the action
  it just reversed (insert undo → start of the removed range, remove undo → end of the
  reinserted text) and stores nothing. *Stored*: VS Code keeps before/after cursor state per
  stack element, Vim stores `uh_cursor` in its undo header. The derived rule costs no bytes and
  already satisfies requirement 1, so it is the one to take.
- **Record format**: delta records are the norm — Scintilla `{type, position, length,
  mayCoalesce}` over a separate scrap stack, VS Code `TextChange(oldPos, oldText, newPos,
  newText)`, Emacs `(beg . end)` for inserted and `(text . position)` for deleted text. Vim is
  the outlier that saves whole-line images. A block is simply a larger delta inside one group,
  and a compound action (a replace, an auto-indent with its newline) is several deltas in one
  group.
- **Growth**: Emacs and Vim evict whole groups; Scintilla and VS Code showed no depth cap in the
  code read.

**Chosen model: a delta log in the slot's 4 KB REU workspace, coalesced per action.** Two models
were weighed:

| Model | Code | Storage per action | Depth in 4 KB | Where it fails |
|---|---|---|---|---|
| Swap snapshot of the used records | no inverse logic at all | `41 × ED_LINES + 6`: 416 B for a ten-line file, 7 878 B for a full one | 9 steps small, **0 for a full document** | a 192-line document does not fit, so undo would be missing exactly when it is wanted |
| **Delta log** (chosen) | one inverse per record kind | `6 + n`; a typing run grows one record in place | ~500 single-character steps, a 200-char paste in 206 B | every inverse is a case to get right |

The snapshot is the simpler machine and would win if the workspace were 32 KB. It is not: 4 KB
holds less than one full document, and a *per-slot* budget is what keeps two `edit` instances
from sharing a history. The delta log is what every serious editor uses for the same reason —
storage proportional to the *change*, not to the document.

**Shape**

- **Record.** `kind` (bit 7 = "this record starts a new action", bits 0-2 = the kind), `prev`
  (2 B, the previous record's offset — the back link that makes a reverse walk one read), `line`,
  `col`, `len`, then `len` payload bytes. Six header bytes; `len` ≤ 255, so a longer span chains
  records inside the same action.
- **Kinds.** `INS_TEXT` (payload = the characters that appeared), `DEL_TEXT` (payload = the
  characters that went), `INS_LINE`, `DEL_LINE` (payload = the 41-byte record that went), `SPLIT`
  and `JOIN` (line structure only). Each inverts exactly.
- **Where the hooks go.** Every mutation funnels through `edInsChar`, `edDelChar`, `edReturn`,
  `edInsLine`/`edInsNoGrow`, `edDelLine` and the cascade helpers `edCascadeStep`/`edInsCascLp`;
  paste, cut and clear are built from those. A hook runs *before* the primitive writes and
  records the bytes about to be lost — which is why the lossy sites (`edCascDelDone`'s roll-off,
  `edInsOverflow`'s overwritten tail character) need no special case: their character is in a
  `DEL_TEXT` payload like any other.
- **Coalescing.** A record is appended unless the previous one is the same kind, its action is
  still open, no non-typing key has arrived since, and the new edit continues it — for `INS_TEXT`
  the same line with `col == prev col + prev len`, for `DEL_TEXT` the backspace (same `col`,
  payload grows leftwards) or delete (same `col`, payload grows rightwards) adjacency. Continuing
  a run *appends to the payload in place* (one byte at `record + 6 + len`, then bump `len`), so a
  typed word costs one record plus one byte per character.
- **Cursor policy.** Derived, stored nowhere: undo of `INS_TEXT` → `(line, col)`; undo of
  `DEL_TEXT` → `(line, col + len)`; redo mirrors. The line kinds leave the cursor on the affected
  line at the recorded column.
- **Undo / redo.** `UPOS` walks the action boundaries; the *first* record of an action carries
  bit 7, and an action's records are applied in **reverse** so a `SPLIT`'s halves are rejoined
  before the text that moved is written back. A new edit winds `UPOS` back to the action start,
  which is the redo tail falling away.
- **Instance fields.** ~12 B: `ULAST` (2, the most recent record's offset), `UPOS` (2, the undo
  position), `UTOP` (2, the append offset), `UREC` (2), `UHDR` (2), and the `UGRP`/`UREST`/`UDIR`
  guards. `data_size` is already 223 for today's engine and has room.
- **Overflow.** When a record will not fit, the whole history is dropped at an action boundary and
  the next undo reports "no history". Evicting the oldest action instead needs a compaction pass
  through the bounce buffer, and is a later refinement.
- **Clear.** `edClear` frees every chunk, so it is a new document rather than an edit: it resets
  the history and is documented as not undoable. Making it undoable needs ~9 KB of `DEL_LINE`
  records.
- **Not in history.** The clipboard stays outside it — "undo a cut, then paste" duplicates text,
  as it does on Windows.

**Traps.** A restore must not log itself (one guard byte checked by every hook). `ED_MOD` is set
by an undo, not restored. A restore can need *more* chunks than exist (undoing a big delete), so
it grows chunks first and reports "no room" rather than leaving half a document. `ED_SIZE` cannot
be maintained incrementally across a restore; it is recounted from the line lengths, plus the
`2 × ED_LINES` the status bar adds for the CR+NUL pairs.

**Room today:** 0 kernel bytes. The engine is `edit`'s own code — `edit` sits at 41 code pages of
10 496 B with 10 300 B used, against **margin 26 pool pages**, so roughly 6 KB of code is
affordable; the log is the per-slot 4 KB workspace `grpaint` and `musedit` already use, and the
bindings are two free screencodes.

**Prerequisites — both were live bugs the first engine exposed, and both are fixed.**

- **The workspace bank byte.** `REU_WS_BASE` is `$F80000`, and `<`/`>` take bytes of the *low 16
  bits*, so `#>REU_WS_BASE` assembles to `$00`, not `$F8`. Every workspace DMA — this feature,
  `musedit`'s undo stack, `grpaint`'s band and the kernel's `clearSlotWorkspace` — was aimed at
  **bank 0**, over the stack images, char shadows and ZP shadows. `REU_WS_BANK =
  REU_WS_BASE >> 16` in `kernel-shared.inc`, with two asserts, and the five sites now load it;
  verified in VICE (`ED_UBANK` reads `$F8`).
- **`edit`'s `data_size`.** The header declared 208 B while the engine's fields ran to 222 — an
  instance-buffer overrun. The constant now lives in `edit.asm` beside the header
  (`ED_DATA_SIZE = 223`) and the body asserts its fields fit inside it.

**Status — parked.** What the branch carries, and what it does.

- **Written** (`src/tasks/dynamic/edit.inc`): an engine of ~272 B — 15 instance fields (208-222),
  seven mutation hooks, STOP+U (`$15`) / STOP+R (`$12`) bindings, `edUInit`/`edUClose`/
  `edUApply`/`edUUndo`/`edURedo`/`edUSize`. Its storage is the model this plan rejects: **one
  88-byte record per line**, holding that line before and after the edit, 46 records deep, and one
  record per keystroke — no runs, no block spans, no cursor restore.
- **Works, measured in VICE** (`edit` in slot 2): the log seats at bank 248 offset `$2000`
  (`ED_UBANK = $F8`, `ED_UHI = $20`); a keystroke appends exactly one record (`UTOP` → `$0058`);
  undo restores the edited line's text and winds `UPOS` to 0; redo brings it back; a new edit
  drops the redo tail; ordinary typing, overtype, RETURN, DEL and paste behave as before.
- **Broken, measured:** undoing an insert leaves an **extra line holding the pre-edit text**
  (`ED_LINES` 9 → 11, the stray line = `aspeed 200` after undoing a typed `a`), and the size
  read-out disagrees with the document (93 where the file is 81). The record's header reads back
  as `$80` (image, action start) and exactly one record is applied, so the stray line is produced
  by the restore path or by the `edUSize` recount — **not yet isolated**. The keys are still
  bound, so **do not merge this branch**; the engine is a starting point for the plumbing
  (workspace seat, arming, guard, keys), not for the storage model.
- **Next step if resumed:** re-run the undo test after the `data_size` fix (it has not been
  re-tested since), and if the stray line persists, breakpoint the restore path in VICE rather
  than reasoning from the source.

**Test plan** (VICE, per Rule 20 — one consent per launch, keys through the binary monitor):

1. Load `boot.bat`, type `hello`, STOP+U → the word goes and the cursor is where `h` was;
   STOP+R → it returns with the cursor after it.
2. Overtype a character, backspace mid-line, backspace at column 0 (join), DEL, RETURN (split),
   STOP+V paste of a marked block, STOP+K cut, STOP+C clear (history resets).
3. Fill to the document limit and undo across the chunk grow/shrink, then undo until "no history"
   is reported.
4. Verify by *reading memory*, not by the screen: the editor's instance buffer is found in the
   data heap by its filename bytes, `ED_LINES` (12), `ED_SIZE` (22), `ED_CHKTAB` (54) and the log
   fields (208-222) are read from it, and the chunk records give every line's length and text.

**Milestones.** (1) record layer + `INS_TEXT`/`DEL_TEXT` + coalescing + the two keys, tested for
typing runs and backspace; (2) `SPLIT`/`JOIN`/`INS_LINE`/`DEL_LINE`, tested across the cascade
and chunk boundaries; (3) paste/cut as single steps with a payload chain; (4) overflow and the
"nothing to undo" report; (5) docs — [edit.md](edit.md) keys and depth, [limitations.md](limitations.md)
row removed, `README.md`, and this section deleted.

**Open questions.**

- How deep should history go? ~500 single-character steps and ~40 block operations is what 4 KB
  buys; deeper means a second workspace page or evicting the oldest action.
- Does undo reach back past a save, and does saving clear the modified flag that an undo set?
- Is "clear" allowed to stay outside history?
- Should a whole-word run be capped (Emacs amalgamates 20 commands) or run until the edit stops
  being adjacent?

**Done when:** undo/redo work for every mutation, one step per run and per block operation, with
the cursor restored, across chunk grow/shrink, and with no pool or workspace leak.

**Sources.** Win32 EDIT undo — <https://learn.microsoft.com/en-us/windows/win32/controls/em-undo>
and Wine's `EDITSTATE`/`EDIT_EM_ReplaceSel` (<https://github.com/wine-mirror/wine/blob/master/dlls/user32/edit.c>);
Scintilla `UndoHistory` (<https://github.com/mirror/scintilla/blob/master/src/UndoHistory.cxx>)
and `SCI_SETUNDOSELECTIONHISTORY` (<https://www.scintilla.org/ScintillaDoc.html#UndoRedo>);
VS Code `editStack.ts` and `cursorTypeEditOperations.ts`
(<https://github.com/microsoft/vscode/blob/main/src/vs/editor/common/model/editStack.ts>);
Emacs undo (<https://www.gnu.org/software/emacs/manual/html_node/elisp/Undo.html>) and amalgamation
(<https://www.gnu.org/software/emacs/manual/html_node/elisp/Maintaining-Undo.html>); Vim undo
(<https://vimhelp.org/undo.txt.html>).

---

### 10. REU FS Directories

**Effort:** Medium (traversal API) / Hard (subdirectories) — designed, not built

**What already ships.** A flat namespace: 512 slots of 32 bytes at bank 5 `$9000-$CFFF`
(slot 0 is the FS header and allocator offset, so **511 file entries**), data from `$05D000`,
15 872 000 bytes of capacity. `kReuDirEntry` reads entry *n* by index. The consumers are
`del` (loops), BASIC's `DIR` (loops from index 1 until an empty slot), `banner`, `type` and
`dir` (entry calls), `musedit` and the shell (slot 0 only, for the allocator). The host tool
`reufs.py` decodes the same table, and `reufs-gui.py` imports it rather than re-implementing
it. `/` is already reserved as a path separator *in names* by the host tool.

**What is missing.**

1. **A traversal API** — `kReuOpenDir`/`kReuReadDir`/`kReuCloseDir` do **not** exist; there is
   no handle, no cursor and no `closedir`. The scan itself exists inside `kReuDirEntry`, so
   this is saved cursor state plus three entry points.
2. **Subdirectories** — a directory is an entry with a directory bit whose payload is a
   pointer list into the data area rather than a slot in the fixed table; a path walker
   (`/`-separated) sits in front of it, ported from the same Lee Davison ROM FS model that
   supplied the entry format and `kReuCmpName`. Names then identify paths, so `run`,
   `del`, `dir`, `rename`, `copy`, BASIC's `DIR`, both loaders and both host tools all
   become path consumers.

**What it buys.** A traversal cursor instead of every consumer walking the table (which is
also how a caller currently gets out of step with a concurrent delete), and then nesting.

**Caveats.**

- **One handle, by design** — the same single-threaded model as `kReadString` and
  `reuDirBuf`. Two traversals at once (a background `dir` and BASIC's `DIR`) cannot coexist,
  so the API must either refuse a second open or the second caller works.
- **Nesting is not capacity.** The table is fixed at 512 entries; subdirectories give
  structure, not room, unless the table itself becomes dynamic — a much bigger change.
- **The blast radius is the cost**: every consumer plus both host tools, in one commit, and
  `del`/`compact` must learn about directory entries or they leak them.
- **No spare jump-table slot**: the `$E000` table's 91 slots are full (`kMouseCall` at `$E0F6`
  is the last slack one), and `LIB_CALLS` is full too, so three new calls are pocket stubs or
  one dispatching entry. A single `kReuDirCall`-style dispatcher is the cheaper shape.
- Worth it only alongside a reason to nest — `reufs-gui.py` already gives a host-side tree,
  and a flat namespace is what the image format documents.

**Room today.** The path walker (~120 B) fits the 215 B run; the three calls need the pocket
route; the host tools are part of the change.

---

### 11. A C compiler

**Effort:** Hard — a toolchain retarget, not a kernel change

**Want.** Write a task in C, build it to a relocatable `.tsk`, run it.

**What already ships.** Everything a retarget needs on this side: EhBASIC as the only
high-level language in the image; `fp.lib` as the float engine (five-byte values, 26 opcodes
behind one `kFpCall` dispatcher — [lib-fp.md](lib-fp.md)); a task ABI with a 256-byte header
and a **derived** fixup table (the pipeline assembles each task at three shifted bases and
diffs which bytes moved — `tools/assemble-tasks.asm`, `tools/gen-fixups.asm`, and
[writing-relocatable-tasks.md](writing-relocatable-tasks.md)); a pool budget checker
(`tools/check-pool-budget.ps1`, 109 pages, `basic` at 108); and lazy library loading with a
cold stub, which is how a libc would have to enter. There is no C toolchain in the tree today.

**What is missing.** A compiler plus four pieces of runtime:

1. **A KERNAL-free runtime.** Every C64 C runtime calls the ROM: `$FFxx` vectors, the
   zero-page pointer block, the `$0100` stack. None of that exists here — `$FFxx` is RAM and
   the kernel's own API, zero page is per task and DMA-preserved, and each task has a 256-byte
   stack. Startup, console I/O and allocation have to be re-expressed on the syscalls
   ([syscall.md](syscall.md)), the screen routines ([kernel.md](kernel.md)), `kMalloc` and
   [filesys.lib](lib-filesys.md).
2. **A relocatable task, not a PRG.** The output must carry the header and be re-linkable at
   the pool's base — which is exactly what the derived-fixup pipeline already requires of
   assembly tasks. A `ld65` configuration plus a `crt0` that ends in the task entry contract
   is where that lands.
3. **Float on `fp.lib`.** cc65 documents no `float`/`double` at all, and the others bring
   their own arithmetic; this OS already has a better answer — map C `float` onto the
   library's five-byte value and make the operators opcode calls.
4. **A small libc.** A header of wrappers over the jump table plus a libc subset small enough
   that a *hello world* does not cost thirty resident pages.

**What it buys.** A second language for the tasks that are painful in assembly (parsers,
number formatting, anything with data structures) — and, with `fp.lib`, floating point that
agrees with BASIC instead of a second soft-float implementation.

**Caveats.**

- **The 256-byte task stack is the hard limit.** C code uses the stack for locals, spills and
  call frames; a recursive function or a modest local array will not fit. Any C runtime needs
  either a bigger per-task stack (a kernel change: the stack image is 256 B per slot in the
  REU) or a documented "no large locals, no deep recursion" rule — a language subset, in
  effect.
- **cc65's own C64 runtime is the wrong runtime** (KERNAL + `$0100` stack + zero page). The
  port is the work, not the compiler install, and the toolchain would then be two assemblers
  in one build (KickAssembler for the kernel, `ca65`/`ld65` for C tasks).
- **Licences**: cc65 is zlib (fine); Oscar64 is GPL-3.0, and this tree is GPL-2.0-*or-later*,
  so linking its runtime into a task is permitted but makes that task GPL-3.0 — a decision,
  not a detail. llvm-mos is the most capable and by far the largest. SubC has no 6502 back
  end, so choosing it means writing one.
- A libc in the pool competes with `basic`'s margin-1 budget: it must be **lazy**, and the
  first thing to prove is that a compiled hello-world is smaller than the assembly
  equivalent — otherwise the toolchain is a hobby, not a capability.

**Room today.** Zero kernel bytes. The work is a host toolchain plus a runtime that lives in
a library and in task code; the pool is the budget that decides whether it is usable.

---

## Recommended order

Ranked by what it costs today against what it buys — not by ambition. The first
three are all inside the 215 B run or task code alone.

| Priority | Idea | Cost today | Why |
|---|---|---|---|
| **1** | Editor undo / redo (§9) | **0 kernel bytes**, ~400 B of `edit` code, margin 26 pages | The last editor without history gets it, and the storage question is answered by the slot's 4 KB workspace — a delta log, not snapshots |
| **2** | IPC wakeup (§1) | ~40 B of library code (semaphore handshake) or ~130 B kernel (`SYS_RECV` on slot 8/9) | Removes up to a jiffy of poll latency per message, and is the primitive §3 builds on |
| **3** | Crash protection, the reachable half (§6) | ~40 B in the 215 B run | `BRK` becomes a defined task abort instead of being swallowed by the tick. A bad `JMP` or wild store still cannot be caught — no MMU |
| **4** | Char cache (§8) | the mirror already exists; stage 1 is ~200 B of kernel code, stage 2 needs a flush-rule decision | Stage 1 fixes hidden-task scrollback and opt-in read-back; stage 2 is the bulk-output speedup |
| **5** | Signals (§2) | 24 B of state + ~60 B handler, plus a test in `switchTask` | Only pays off once something can block for a long time (§4) |
| **6** | Blocking I/O (§4) | the state machine fits the 215 B run; the idle task and the drivers do not | The enabler for §3 and for §2's interruptible waits, and the end of `kWaitKey` spinning a whole quantum |
| **7** | Stream abstraction (§3) | needs §4, a per-task table and a pipe buffer in REU | Pipelines, and a child's output becoming capturable |
| **8** | REU FS directories (§10) | path walker fits; three new calls need a pocket or dispatcher, one global handle, and every consumer plus both host tools change | A traversal cursor, then nesting — the entry table stays at 512 slots |
| **9** | RAM disk (§7) | needs §5 | A sector cache for a filesystem that does not exist yet |
| **10** | File system, IEC/Disk (§5) | a library, no kernel space — but the tick must be masked across every transfer | Only matters on real hardware with a 1541; the REU filesystem is what ships, and it is already RAM |
| **11** | A C compiler (§11) | a toolchain retarget | cc65 is the candidate; `float` lands on `fp.lib`; the work is the runtime, the relinkable image and the libc subset |

Two sequencing facts worth keeping in mind. **A single relocation of the four
`LIB_CALLS` scan tables buys several later items at once** — the library semaphore
(§1), the FS traversal calls (§10) — and costs one commit that moves the pinned
addresses behind them together with every alias. And **anything that grows the
core band in two places** (`irq.inc` for §6 and §2, `switchTask` for §4) is
competing for the same 215 B run, so those changes are best done one at a time,
each verified with `tools/verify-map.ps1` before the next.
