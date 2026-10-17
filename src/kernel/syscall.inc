// ==========================================
//  GordonOS -- Syscall API
// ==========================================
//
//  Tasks call kernel services via:
//    lda #syscall_number
//    jsr syscall
//
//  Syscall table:
//    0  exit        -- terminate current task
//    1  spawn       -- create task at spawnPtr ($F7-$F8)
//    2  kill        -- kill task X (X = target index)
//    3  yield       -- voluntarily give up timeslice
//    4  pause       -- suspend task X (X = target index)
//    5  resume      -- resume task X (X = target index)
//    6  setprio     -- set task priority (X=target, kArg+1=0-5)
//    7  gettid      -- return current task index in A
//    8  allocscreen -- inert no-op (use kAllocScreen)
//    9  freescreen  -- inert no-op (use kFreeScreen)
//    10 semWait     -- acquire semaphore X (index in X; blocks by yielding)
//    11 semSignal   -- release semaphore X (index in X; count caps at 255)
//    12 threadSpawn -- spawn child task from spawnPtr, auto-named parent.N
//    13 sleep       -- timed block for kArg+1 ticks (1-255; 0->1)
//
//  Some syscalls (exit, kill-self, pause-self) do not
//  return -- they jump directly into the next task via
//  reschedule().

// ==========================================
//  syscall -- dispatcher
//  Input: A = syscall number (0..SYSCALL_COUNT-1)
//  Invalid numbers fall through to cli/rts (no-op).
//  Constant-time via jump table (syscallTbl). Runs under
//  SEI (set immediately after PHP), so borrowing srcPtr
//  for the indirect jump is safe.
//  X is preserved for handlers that take a target index in X
//  (kill/pause/resume/setprio/semWait/semSignal) -- the table is
//  indexed via Y, which is not part of the syscall ABI.
// ==========================================
.const SYSCALL_COUNT = 14      // syscalls 0..13
syscall:
    pha
    php
    sei
    pla
    sta saveSP
    pla
    cmp #SYSCALL_COUNT
    bcs syscallBad             // out of range -> no-op
    asl                        // x2 -> word index
    tay                        // Y = index (NOT X -- X carries the target for kill/pause/etc.)
    lda syscallTbl,y
    sta srcPtr
    lda syscallTbl+1,y
    sta srcPtr+1
    jmp (srcPtr)               // dispatch (srcPtr on $F9 -> no page-cross bug)
syscallBad:
    cli
    rts

// Jump table -- indexed by syscall number x 2. Order MUST match numbers.
//  WARNING: THE TABLE MUST END BELOW $0F00, the sprite band's first slot. A slot's
//  art is DMA'd over its whole 64 bytes, and the SYS_SLEEP entry's lo/hi bytes land
//  at $0F01/$0F02 -- inside that slot -- so art there replaces the handler address
//  with sprite pixels and the dispatcher jumps to whatever they leave. main.asm
//  asserts the clearance; the size pad in init.inc is what holds the table's
//  address, so its length and SYSCALL_COUNT x 2 have to be kept in step.
syscallTbl:
    .word sysExit              // 0
    .word sysSpawn             // 1
    .word sysKill              // 2
    .word sysYield             // 3
    .word sysPause             // 4
    .word sysResume            // 5
    .word sysSetPrio           // 6
    .word sysGetTid            // 7
    .word syscallBad           // 8  (inert -- use kAllocScreen)
    .word syscallBad           // 9  (inert -- use kFreeScreen)
    .word semWait              // 10 (X = semaphore index)
    .word semSignal            // 11 (X = semaphore index)
    .word sysThreadSpawn       // 12
    .word sysSleep             // 13

// ==========================================
//  WARNING: PINNED -- this block does NOT sit in the kernel core's sequential layout.
//  $0F00-$0FBF must stay FREE for three sprite slots: it is the only
//  64-byte-aligned space left in VIC bank 0 ($1000-$1FFF is char ROM to the
//  VIC, $2000-$3FFF is the framebuffer).
//  Relocating it needs no glue: the routine is entered from syscallTbl by an
//  assemble-time .word, and it ends in `jmp reschedule`, so nothing falls
//  through into it. 78B into an 82B run ($1CBF is the last byte; $1CC0 is
//  spriteFrame). $0EFA-$0FCB is free.
.pc = $1C6E
// ==========================================
//  sysExit (0) -- terminate current task
//  Marks curTask DEAD, shrinks totalTasks
//  if possible, immediately switches away.
//  Never returns.
// ==========================================
sysExit:
    ldx curTask
    // NOTE: do NOT clear screen fields here. Boot relies on them
    // surviving exit so shell can take ownership via kTakeScreenOwnership.
    // Tasks that exit voluntarily should call kFreeScreen themselves.
    // sysKill handles forcible cleanup.
    lda #0
    sta taskState,x             // mark DEAD
    jsr transferFocusAway       // auto-switch focus+display if needed
    jsr shrinkTotalTasks
    // Release semaphores owned by this task (mutex ownership only)
    // LIMITATION: only the mutex case (count exhausted to 0) is reclaimed.
    // A task that consumed one slot of a multi-count semaphore and then
    // dies does NOT return that slot -- semOwner only tracks the last 1->0
    // owner, so partial counting-semaphore holds leak. Acceptable for now.
    ldx #0
seClean:
    lda semOwner,x
    cmp #$ff
    beq seNext
    cmp curTask                 // owned by us?
    bne seNext
    lda semCount,x
    bne seNext                  // count > 0 -- counting semaphore, skip
    lda #$ff; sta semOwner,x    // clear owner
    lda #1; sta semCount,x      // release (0->1)
seNext:
    inx
    cpx #MAX_SEMAPHORES
    bcc seClean
    lda curTask                 // dying task's GordonOS ID
    jsr dynFree                 // free dynamic pages (no-op if static)
    // Flush cursor to framebuffer tail if this task shares the displayed
    // surface -- reschedule skips svCurSave, so without this the tail
    // stays at the last IRQ-preemption position (stale mid-line).
    ldx curTask
    lda screenAlloc,x
    beq seNoCurFlush
    jsr surfDisplayed           // C=1 if this task's surface is displayed
    bcc seNoCurFlush
    lda cursorCol; sta GFX_MATRIX_BASE + GFX_CURSOR_COL
    lda cursorRow; sta GFX_MATRIX_BASE + GFX_CURSOR_ROW
seNoCurFlush:
    jmp reschedule              // never returns
.label sysExitMovedEnd = *

// ==========================================
//  WARNING: PARTLY PINNED -- spawnCore is split. $0F00-$0FBF must stay free for sprite
//  slots 5-7, so the opening 132B lives at $1796 and is cut at `scFree`, rejoined
//  with a 3-byte `jmp` (see the SEAM note at the cut).
//  The cut point is chosen so the remainder ($0FCC-$10D8) keeps its addresses --
//  every branch in it targets a label inside it, so nothing downstream shifts and
//  no +/-128 branch needs converting (Rule 4).
// ==========================================
//  spawnCore -- spawn a task (internal, no CLI)
//  Caller must set taskNamePtr, spawnPtr, kArg+1.
//  Caller is responsible for interrupt state.
//  Returns C=0 on success, C=1 on failure.
//  kArg = 0 (no slots) or 1 (heap full) on failure.
// ==========================================
.pc = $1796
spawnCore:
    jsr findFreeSlot            // X = free slot
    bcc scGo
    lda #0; sta kArg            // error code: no free slots
    sec
    rts
scGo:
    jsr initTCB_ptr             // build TCB from spawnPtr
    lda taskNamePtr
    sta taskNameLo,x            // set name pointer
    lda taskNamePtr+1
    sta taskNameHi,x
    lda spawnPtr
    sta taskLo,x                // set start address
    lda spawnPtr+1
    sta taskHi,x
    // --- allocate per-instance data buffer (re-entrant task support) ---
    lda kArg+1                  // dataSize from caller (0 = no buffer)
    bne scAlloc
    jmp scNoBuf
scAlloc:
    // Buffer decision for slot X:
    //   old buffer, same size  -> reuse it, zeroed (no bump-alloc churn)
    //   old buffer, wrong size -> push the old buffer onto the orphan
    //   free-list so it stays recyclable, then scavenge a same-size
    //   buffer from any dead slot / the free-list, else allocate fresh.
    //   The heap never rewinds, so without the free-list mixed-size
    //   churn strands dead buffers forever ("? instance heap full"
    //   even with free task slots).
    lda taskDataSize,x
    cmp kArg+1
    bne scPush                  // no/mismatched buffer -> push + scavenge
    jmp scZeroBuf               // same size -> reuse (too far to branch)
scPush:
    // Push slot X's old buffer onto the free-list. Size 0 pushes a
    // harmless (0,0,0) no-op entry. Full list -> old buffer is orphaned.
    ldy #FREE_LIST_ENTRIES-1
scPushLp:
    lda freeListSize,y
    bne scPushNext
    lda taskDataSize,x
    sta freeListSize,y
    lda taskDataPtrLo,x
    sta freeListLo,y
    lda taskDataPtrHi,x
    sta freeListHi,y
    jmp scScan                  // pushed once -- go scavenge
scPushNext:
    dey
    bpl scPushLp
    // list full -> fall into scScan (old buffer orphaned, pre-fix behaviour)
scScan:
    // Scavenge: adopt a same-size buffer from ANY dead slot (never
    // touch live/paused/swapped slots). Exhaustion tries the free-list.
    ldy #MAX_TASKS-1
scScanLp:
    lda taskState,y
    bne scScanNext
    lda taskDataSize,y
    cmp kArg+1
    bne scScanNext
    sta taskDataSize,x          // A == kArg+1 (cmp matched)
    // donor found -- adopt its buffer, clear its ownership record so a
    // future spawn into slot Y can't double-own this buffer
    lda taskDataPtrLo,y
    sta taskDataPtrLo,x
    lda taskDataPtrHi,y
    sta taskDataPtrHi,x
    lda #0
    sta taskDataSize,y
    sta taskDataPtrLo,y
    sta taskDataPtrHi,y
    jmp scZeroBuf               // zero the adopted buffer
scScanNext:
    dey
    bpl scScanLp
    //  ---- SEAM (see the spawnCore header) ----
    //  The moved piece ends here and the remainder resumes at scFree's ORIGINAL
    //  address. scFree is the latest label that both clears the slot band and
    //  keeps each side's branches internal: scFree..scNoGrow never reference a
    //  label back in the piece, and the piece leaves only via the absolute
    //  `jmp scZeroBuf` / `jmp scNoBuf` above. Placing the seam any earlier (e.g.
    //  at scScanNext) would put the backward `bpl scScanLp` in the remainder
    //  with its target in the piece -- a +/-128 branch across the cut.
    jmp scFree
.label spawnPieceEnd = *
.pc = $0FCC
scFree:
    // No dead-slot donor -- take an orphaned buffer of this size from
    // the free-list, else fall into the fresh bump-allocation.
    ldy #FREE_LIST_ENTRIES-1
scFreeLp:
    lda freeListSize,y
    cmp kArg+1
    beq scFreeHit               // forward ~80B -- within branch range
    dey
    bpl scFreeLp
    // fall into scFresh -- nothing recyclable
scFresh:
    lda kArg+1
    sta taskDataSize,x          // store requested size, not old slot's size
    lda dataHeap
    sta taskDataPtrLo,x         // new buffer starts at current heap ptr
    lda dataHeap+1
    sta taskDataPtrHi,x
    // room check BEFORE bumping: dataHeapEnd - dataHeap >= kArg+1
    lda dataHeapEnd
    sec
    sbc dataHeap
    sta kArg+2                  // rem_lo (scratch -- no rollback needed)
    lda dataHeapEnd+1
    sbc dataHeap+1
    bne scDoBump                // rem >= 256 -> room
    lda kArg+2
    cmp kArg+1
    bcc scOverflow              // rem_lo < size -> heap exhausted
scDoBump:
    lda dataHeap
    clc
    adc kArg+1
    sta dataHeap
    bcc scZeroBuf               // hi unchanged -> ok
    inc dataHeap+1
    jmp scZeroBuf
scOverflow:
    // heap exhausted -- clean partial state (dataHeap never advanced)
    lda #0
    sta taskDataSize,x
    sta taskDataPtrLo,x
    sta taskDataPtrHi,x
    lda #1
    sta kArg                    // error code: heap exhausted
    sec
    rts
scFreeHit:
    // Free-list hit -- adopt the orphaned buffer (A already = size from
    // the cmp that matched), clear its entry so it can't be handed out
    // twice.
    sta taskDataSize,x
    lda freeListLo,y
    sta taskDataPtrLo,x
    lda freeListHi,y
    sta taskDataPtrHi,x
    lda #0
    sta freeListSize,y
    // fall into scZeroBuf (zero the recycled buffer)
scZeroBuf:
    // Zero-initialize the buffer at taskDataPtrLo/Hi,x using dataPtr
    // as temp (reuses a dead slot's same-size buffer).
    lda dataPtr; pha
    lda dataPtr+1; pha          // save caller's dataPtr
    lda taskDataPtrLo,x; sta dataPtr
    lda taskDataPtrHi,x; sta dataPtr+1
    txa; pha                    // save X
    ldy #0
    lda #0
scZeroLp:
    sta (dataPtr),y
    iny
    cpy kArg+1
    bcc scZeroLp
    pla; tax                    // restore X
    pla; sta dataPtr+1
    pla; sta dataPtr            // restore caller's dataPtr
    jmp scSetAlive              // skip scNoBuf clearing
scNoBuf:
    lda #0
    sta taskDataPtrLo,x
    sta taskDataPtrHi,x
    sta taskDataSize,x          // no buffer for this task
scSetAlive:
    // Clear inherited screen state so the new task starts clean
    lda #0
    sta screenAlloc,x
    sta screenOwner,x
    sta taskShadowFlag,x
    lda #1
    sta taskState,x             // mark ALIVE
    sta taskPriority,x          // initialise priority (A still = 1)
    sta taskSliceLeft,x
    // --- set ZP save region (from kArg+6/kArg+7) ---
    lda kArg+6                  // zpBase
    sta taskZpBase,x
    lda kArg+7                  // zpLen
    tay                         // save original len in Y
    beq scZpOk                  // 0 = no ZP save, always valid
    clc
    adc kArg+6                  // A = base + len
    bcs scZpBad                 // the carry proves base+len > $FF: the wrapped sum
                                //   would slip past the $F1 test below
    cmp #$f1                    // must not overlap kernel ZP ($F1+)
    bcc scZpOk                  // base+len <= $F0, valid -- Y has original len
scZpBad:
    lda #0                      // invalid -- clamp both to 0
    sta taskZpBase,x
    tay                         // Y = 0
scZpOk:
    tya
    sta taskZpLen,x             // store original len (or 0 if invalid)
    //  WARNING: AND ZERO THIS SLOT'S ZP SHADOW. The shadow is per slot and its
    //  content is only ever written by switchTask's save DMA, so without this
    //  a task spawned into a slot a previous occupant used has that occupant's
    //  bytes DMA'd into its own ZP window on its first switch-in (the restore
    //  gate is unconditional). Both of this path's inputs -- kArg+6/+7 -- are
    //  already stored above, and the call clobbers only kArg..kArg+5, which is
    //  free here (the name build in sysThreadSpawn finished before scGo). See
    //  slot-shadow.inc; the routine does no DMA at all when len = 0.
    jsr clearSlotShadows
    //  Zero this slot's SPRITE state. The block is per SLOT and holds the task's
    //  shadow, sheet records and frame indices, so a successor spawned into a
    //  recycled slot must start from nothing rather than inherit the previous
    //  occupant's sprites. Zeros are the one correct starting state: every
    //  sprite disabled, and no sheet resolved (bank 0 is never an FS data bank,
    //  so a zero record cannot name a file). X survives both this and
    //  clearSlotShadows -- Y does not (sprBlkAddr writes it, and kReuFillReu's
    //  caller reloads it below) -- and the spawn path's inputs are in kArg+6/+7.
    jsr clearSlotSprite

    stx kArg+2                  // save slot for comparison
    ldy #0
scOrphan:
    cpy totalTasks
    bcs scOrphanDone
    lda taskParent,y
    cmp kArg+2
    bne scOrphanNext
    lda #$ff; sta taskParent,y  // clear stale parent link
scOrphanNext:
    iny
    bne scOrphan
scOrphanDone:
    lda #$ff
    sta taskParent,x            // default: no parent (caller may override)
    lda #0
    sta taskThreadIdx,x         // reset thread counter for new task
    // initTCB_ptr already DMA'd the initial frame to REU -- no separate copy needed.
    // totalTasks = max(totalTasks, slot + 1) -- grow scan range only if
    // new slot is beyond current ceiling. Avoids overflow when recycling
    // dead slots below totalTasks.
    txa
    clc
    adc #1                      // A = slot + 1
    cmp totalTasks
    bcc scNoGrow                // slot+1 < totalTasks -> no growth needed
    beq scNoGrow                // slot+1 = totalTasks -> no growth needed
    sta totalTasks              // slot+1 > totalTasks -> expand scan range
scNoGrow:
    clc                         // success
    rts

// ==========================================
//  sysSpawn (1) -- syscall wrapper for spawnCore
// ==========================================
sysSpawn:
    jsr spawnCore
    rts                         // stay SEI'd -- the caller (loader) re-enables
                                // IRQs only after dlRegister completes, so
                                // the spawned task cannot be scheduled with
                                // its registration half-done (warm-reboot
                                // hang: IRQ switched to the new task mid-return)

// ==========================================
//  sysKill (2) -- kill task X
//  Input: X = target task index
//  Marks target DEAD, shrinks totalTasks.
//  Recursively kills the target's THREAD children (spawned via
//  sysThreadSpawn -- taskThreadNameReuHi != 0) and their descendants.
//  Dynamic children (kDynLoad) are ORPHANED, not killed.
//  If killing self, reschedules immediately.
//
//  NOTE: sysExit does NOT cascade -- a task that exits normally
//  leaves its children running as orphans (by design). Only a
//  forcible kill reaps the thread subtree. See syscall.md.
// ==========================================
sysKill:
    // Proceed even if target is already DEAD so cascading kill can clean up any remaining children
    // Release the victim's surface: an owner hands the slot to a surviving
    // sharer (a shared screen outlives its owner); with no sharer the slot
    // is freed (kills must not leak REU slots).
    lda screenAlloc,x
    beq skNoSurf
    lda screenOwner,x
    cmp #$ff
    bne skNoSurf                // sharer -- the owner keeps the slot
    jsr skOwnSurf               // owner: hand slot to a sharer, else free (X = victim)
skNoSurf:
    lda #0
    sta screenAlloc,x
    sta screenOwner,x
    sta taskShadowFlag,x
    sta taskState,x             // mark target DEAD
    sta taskThreadIdx,x         // reset thread counter
    sta taskThreadNameReuHi,x   // clear stale thread name (slot reuse in ps)
    txa
    pha                         // save target X
    jsr transferFocusAway       // auto-switch focus+display if target had it
    jsr shrinkTotalTasks
    pla
    tax                         // restore target X (after shrinkTotalTasks clobbers it)
    stx dlCascadeRoot           // cascade root -- deliberately NOT kArg:
                                //   surfFreeSlot clobbers kArg..+3 and
                                //   dynFree kArg+3..+7
    // --- cascade: recursively kill target's entire descendant tree ---
    // Multi-pass fixpoint. A task dies if its parent is the target OR its
    // parent was itself killed in this cascade (marked with sentinel $FE).
    // Repeating until a pass kills nothing catches grandchildren and deeper.
    // Exit-orphans (a live task whose parent slot died via exit, not this
    // cascade) are NOT $FE-marked, so they are correctly left untouched.
skCascadePass:
    lda #0
    sta dlCascadeFlag           // changed = 0 for this pass
    ldy #0
skCascade:
    cpy totalTasks
    bcs skCascadeChk
    lda taskState,y
    beq skNextChild             // already DEAD -> skip
    lda taskThreadNameReuHi,y
    beq skNextChild             // not a thread child -> orphan (dynamic tasks survive)
    lda taskParent,y
    cmp #$ff
    beq skNextChild             // no parent -> skip
    cmp dlCascadeRoot
    beq skKillChild             // parent == target -> kill
    tax                         // X = parent slot
    lda taskParent,x
    cmp #$fe
    bne skNextChild             // parent not a cascade victim -> skip
skKillChild:
    // free the child's surface slot before clearing state (no leak)
    lda screenAlloc,y
    beq skcNoSurf
    lda screenOwner,y
    cmp #$ff
    bne skcNoSurf               // sharer -- the owner keeps the slot
    tya; pha                    // save child index (surfFreeSlot clobbers Y)
    //  surfFreeSlot also clobbers kArg..kArg+3 -- harmless here: the cascade's
    //  root and flag live in `dlCascadeRoot`/`dlCascadeFlag` (kernel-vars.inc),
    //  out of reach of every clobberer in this path. Do not move them into kArg:
    //  `dynFree` clobbers kArg+3..+7 and `surfFreeSlot` clobbers kArg..+3, and
    //  this cascade calls both.
    tya; tax
    jsr surfFreeSlot
    pla; tay
skcNoSurf:
    lda #0
    sta taskState,y             // mark child DEAD
    sta screenAlloc,y
    sta screenOwner,y
    sta taskThreadIdx,y
    sta taskThreadNameReuHi,y   // clear stale thread name (slot reuse in ps)
    lda #$fe
    sta taskParent,y            // sentinel: cascade victim (so its children die too)
    tya; pha                    // save Y (dynFree clobbers it)
    tya                         // A = child slot (GordonOS task ID)
    jsr dynFree                 // free its malloc blocks, code + program-RAM
                                // pages, lib refcounts and dyn-table entry
                                // (dynFree also decrements dynTaskCount)
    pla; tay                    // restore Y
    inc dlCascadeFlag           // changed = 1 -- a dedicated byte, so this no
                                //   longer depends on what dynFree preserves
    tya; pha                    // save Y (transferFocusAway clobbers it)
    tya; tax                    // X = child slot -- transferFocusAway reads the
                                // victim from X, and dynFree left X clobbered
    jsr transferFocusAway
    pla; tay                    // restore Y (the root is not in kArg, so
                                //   transferFocusAway writing kArg as fromTask
                                //   cannot touch it)
skNextChild:
    iny
    bne skCascade
skCascadeChk:
    lda dlCascadeFlag
    bne skCascadePass           // a task was killed -> rescan for its children
    // convert cascade-victim sentinels ($FE) back to orphan state ($FF)
    ldy #0
skCleanParent:
    cpy totalTasks
    bcs skCascadeDone
    lda taskParent,y
    cmp #$fe
    bne skCleanNext
    lda #$ff
    sta taskParent,y
skCleanNext:
    iny
    bne skCleanParent
skCascadeDone:
    // release semaphores owned by any now-dead task (target + children)
    // LIMITATION: same as sysExit -- only fully-exhausted (mutex) semaphores
    // are reclaimed; partial counting-semaphore holds by a dead task leak.
    ldy #0
skDeadSem:
    lda semOwner,y
    cmp #$ff
    beq skDeadSemNext
    tax
    lda taskState,x
    bne skDeadSemNext           // owner still alive
    lda semCount,y
    bne skDeadSemNext           // counting semaphore, skip
    lda #$ff; sta semOwner,y    // clear owner
    lda #1; sta semCount,y      // release (0->1)
skDeadSemNext:
    iny
    cpy #MAX_SEMAPHORES
    bcc skDeadSem
    ldx dlCascadeRoot           // save target index (shrinkTotalTasks clobbers X)
    jsr shrinkTotalTasks        // after killing children
    lda dlCascadeRoot           // target task ID (killed task)
    jsr dynFree                 // free dynamic pages (no-op if static); clobbers X
    ldx dlCascadeRoot           // restore target index
    cpx curTask                 // killing self?
    bne killDone
    jmp reschedule              // self -> switch away now
killDone:
    cli
    rts

// ==========================================
//  skOwnSurf -- release an owner's surface on kill (X = victim, owner)
// ==========================================
//  A surviving sharer (screenAlloc=1, screenOwner==victim, ALIVE) inherits
//  the slot: screenOwner = $ff -- the shared screen outlives its owner.
//  With no surviving sharer the slot is freed. X preserved.
//  Clobbers: A, Y, kArg+6.
skOwnSurf:
    stx kArg+6                  // victim slot (survives surfFreeSlot's clobber)
    ldy #0
skOwnFind:
    cpy totalTasks
    bcs skOwnFree               // no sharer -> free the slot
    cpy kArg+6
    beq skOwnNext               // skip the victim itself
    lda screenAlloc,y
    beq skOwnNext
    lda screenOwner,y
    cmp kArg+6
    bne skOwnNext               // not a sharer of this surface
    lda taskState,y
    cmp #TA_ALIVE
    bne skOwnNext
    lda #$ff
    sta screenOwner,y           // sharer inherits ownership
    rts
skOwnNext:
    iny
    bne skOwnFind
skOwnFree:
    txa; pha                    // X = victim (surfFreeSlot clobbers X)
    jsr surfFreeSlot
    pla; tax
    rts

// ==========================================
//  sysYield (3) -- voluntarily give up slice
// ==========================================
//  Builds a fake IRQ frame on the stack that
//  matches what the hardware pushes during a
//  real interrupt. Calls switchTask to save
//  this frame into the TCB and switch away.
//
//  When the scheduler later restores this
//  task, switchTask's RTS returns here (to
//  the jmp irqRet below), which pops the
//  saved registers and RTIs back into the
//  task at the instruction after JSR syscall.
//
//  Stack frame built (bottom -> top, $01FA->$01FF):
//    Y, X, A, SR, PCL, PCH
//  irqRet consumes: PLA->Y, PLA->X, PLA, RTI
//
//  WARNING:  6502 JSR pushes PC+2 (addr of last byte
//  of the 3-byte JSR instruction). RTS adds 1
//  to land on PC+3. RTI does NOT add 1 -- it
//  jumps to the exact address popped. So we
//  must add 1 here so RTI lands on the
//  instruction after JSR syscall (PC+3).
// ==========================================
sysYield:
    // saveSP already holds original SR (set by dispatcher)
    stx srcPtr                  // save task's X
    sty srcPtr+1                // save task's Y

    // Pop the JSR return address and add 1.
    // JSR pushed PC+2 (last byte of JSR syscall).
    // RTI needs PC+3 (first byte of next instruction).
    // Without this +1, RTI would jump into the middle
    // of the JSR instruction and execute garbage.
    pla                         // PCL = low byte of JSR return addr
    clc
    adc #1                      // PC+2 + 1 = PC+3 (low byte)
    tax                         // corrected PCL -> X
    pla                         // PCH = high byte of JSR return addr
    adc #0                      // propagate carry to high byte
    tay                         // corrected PCH -> Y

    // Build fake IRQ frame (pushed in reverse pop order)
    tya
    pha                         // [6] PCH -- popped by RTI -> jump target
    txa
    pha                         // [5] PCL
    lda saveSP                  // original SR from dispatcher's PHP
    pha                         // [4] SR  -- popped by RTI
    lda #$00
    pha                         // [3] A   -- popped by PLA
    lda srcPtr                  // original X
    pha                         // [2] X   -- popped by PLA->TAX
    lda srcPtr+1                // original Y
    pha                         // [1] Y   -- popped by first PLA->TAY

    // WARNING: A YIELD MUST FORCE THE SWITCH. `switchTask` decides
    //  whether to advance or to re-enter the same task from
    //  `taskSliceLeft[curTask]`: it decrements and, while slices remain, restores
    //  the SAME task. That is what a priority > 1 means for a timer preemption,
    //  but a voluntary yield is asking to leave NOW, so the counter is set to 1
    //  to make the decrement land on 0. Without this, a task at priority 2-5 had
    //  its yields swallowed until its quantum ran out -- and since `semWait`'s
    //  busy path is literally `lda #SYS_YIELD / jsr syscall / retry`, a blocked
    //  task polled with a full save/restore per retry instead of giving way.
    //  `sysSleep` sets the same byte for the same reason (it marks the task
    //  SLEEPING and would otherwise be restored into itself).
    ldx curTask                 // the caller's X/Y are already on the frame
    lda #1
    sta taskSliceLeft,x         // force switch-away

    jsr switchTask              // save frame -> TCB, switch away
    jmp irqRet                  // <- we resume HERE on next slice;
                                //   irqRet pops regs and RTIs to PC+3

// ==========================================
//  sysPause (4) -- suspend task X
//  Input: X = target task index
//  Marks target PAUSED, but only from a pausable state (ALIVE/PAUSED/
//  SLEEPING) -- DEAD and SWAPPED are refused.
//  If pausing SELF the frame is saved and the task switches away (the
//  sysYield path), so it resumes at the instruction after `jsr syscall`
//  once something resumes it -- the same A=0 / Y=syscall#*2 return
//  contract a yield has.
// ==========================================
sysPause:
    //  WARNING: REFUSE a target whose code is not resident, BEFORE storing PAUSED.
    //  A SWAPPED task's pages are already back in the pool bitmap (dlEvict
    //  freed them) and its saved frame lives in the REU -- overwriting the
    //  SWAPPED marker defeats sysResume's guard below, so the task is marked
    //  ALIVE and scheduled into whatever now lives at that address. DEAD is
    //  refused for the mirror reason: `resume` would resurrect a dead slot.
    //  Only 1-3 (ALIVE/PAUSED/SLEEPING) may be paused. Reachable from the
    //  shell: `ps` prints swapped tasks with their ids, and `prio <id> 0`
    //  routes here (see sysSetPrio).
    lda taskState,x
    beq pauseDone               // DEAD -- leave it dead
    cmp #TA_SWAPPED
    beq pauseDone               // SWAPPED -- code not resident
    lda #TA_PAUSED
    sta taskState,x             // mark target PAUSED
    cpx curTask                 // pausing self?
    bne pauseDone
    //  WARNING: A SELF-PAUSE MUST SAVE THE FRAME. `reschedule` only
    //  finds the next task and restores it -- it saves nothing, so the paused
    //  task came back at its LAST PREEMPTION point and silently replayed the
    //  work since then. `sysYield` builds the same fake interrupt frame the
    //  timer path uses, so the task resumes at the instruction after
    //  `jsr syscall` -- and because its state is PAUSED, `findNextAlive` will
    //  not pick it up again until something resumes it. Same branch, same
    //  size: only the jump TARGET changed.
    jmp sysYield                // self -> save frame, switch away
pauseDone:
    cli
    rts

// ==========================================
//  sysResume (5) -- resume task X
//  Input: X = target task index
//  Marks target ALIVE. Safe to call on
//  already-alive tasks (no-op).
// ==========================================
sysResume:
    lda taskState,x
    cmp #TA_SWAPPED
    beq srDone                  // swapped-out task: code not resident -- refuse
    lda #TA_ALIVE
    sta taskState,x             // mark target ALIVE
srDone:
    cli
    rts

// ==========================================
//  sysSetPrio (6) -- set task priority
//  Input: X = target index
//         kArg+1 = priority (0-255)
//  0 = pause task, 1-5 = valid priorities,
//  >5 clamped to 5. Resets sliceLeft.
// ==========================================
sysSetPrio:
    lda kArg+1                  // priority from caller
    beq sysPause                // 0 -> pause: sysPause holds the state guards
                                //   (one copy -- a SWAPPED target must not be
                                //   re-marked; see sysPause)
    cmp #6
    bcc spSet                   // 1-5 -> valid
    lda #5                      // >5 -> clamp to 5
spSet:
    sta taskPriority,x
    sta taskSliceLeft,x
    cli
    rts

// ==========================================
//  sysGetTid (7) -- return current task index
//  Returns: A = current task index (0..MAX_TASKS-1)
//  Allows tasks to self-identify for IPC, logging,
//  or per-task data indexing.
//  Read is atomic: dispatcher already did SEI, so curTask
//  can't change mid-read. Value may be stale after CLI, but
//  that's harmless -- it represents identity at call time.
// ==========================================
sysGetTid:
    lda curTask
    cli
    rts

// ==========================================
//  sysSleep (13) -- timed block for kArg+1 ticks
//  Input: kArg+1 = tick count (1-255; 0 treated as 1)
//         ~16.7ms per tick at the 60 Hz IRQ
//  Blocks the current task, saves full state via
//  switchTask. Resumes at the instruction after
//  JSR syscall once the ticks expire. Never
//  returns immediately -- always blocks.
// ==========================================
sysSleep:
    ldx curTask
    lda kArg+1                  // tick count from caller
    bne ssStore
    lda #1                      // 0 ticks -> minimum 1
ssStore:
    sta taskSleepTicks,x
    lda #TA_SLEEPING
    sta taskState,x
    lda #1
    sta taskSliceLeft,x         // force switch-away
    jsr switchTask              // save state, switch away
    // Resumes HERE when the task wakes: restoreStack's RTS
    // lands on the cli below. CLI re-enables interrupts
    // (the dispatcher ran SEI), then RTS returns to the
    // caller at the instruction after JSR syscall.
    cli
    rts

// ==========================================
//  findFreeSlot -- scan taskState for a DEAD slot
//  Returns: X = free slot index, carry clear
//           carry set = no free slots
// ==========================================
findFreeSlot:
    ldx #1                     // slots 1..MAX_TASKS-1 for spawn; slot 0 reserved for boot
fsLoop:
    lda taskState,x
    beq fsFound                 // DEAD = free
    inx
    cpx #MAX_TASKS
    bcc fsLoop
    sec                         // no slot found (all spawn slots occupied)
    rts
fsFound:
    clc
    rts

// ==========================================
//  shrinkTotalTasks -- decrement totalTasks
//  while the highest-index slot(s) are DEAD.
//  Keeps the scheduler scan range tight.
// ==========================================
shrinkTotalTasks:
    lda totalTasks
    beq stsDone                 // already 0
    sec
    sbc #1                      // index of last slot
    tax
    lda taskState,x
    bne stsDone                 // alive or paused? stop (TA_DEAD = 0)
    dec totalTasks              // dead? shrink
    jmp shrinkTotalTasks        // check next
stsDone:
    rts

// ==========================================
//  transferFocusAway -- hand the display to a live task
//  Input:  X = dying task slot (or $FF when called just to reclaim a
//          blanked display, e.g. from dlRestore)
//  Output: kbFocus transferred to first ALIVE task with an owner surface, or
//          $ff + blanked display if none.
//  Also reclaims a display that is already unclaimed (kbFocus = $FF): without
//  that, an exit which blanks the display when nothing looks ALIVE-owned leaves
//  kbFocus = $FF forever -- the shell then renders into its REU slot
//  (surfDisplayed compares resolve(owner) with resolve(kbFocus)) and the
//  machine looks frozen. A live focus holder is never stolen.
//  Clobbers: A, X, Y, kPtr, kArg  (callers must save X)
// ==========================================
transferFocusAway:
    cpx kbFocus
    beq tfaGo                   // the dying task holds the focus
    lda kbFocus
    cmp #$ff
    bne tfaRts                  // a live focus holder exists -- nothing to do
    ldx #$ff                    // blank display: no fromTask surface to save
tfaGo:
    lda kPtr; pha
    lda kPtr+1; pha             // save caller's kPtr
    stx kArg+1                  // save dying task (fromTask; $FF = none)
    ldy #0
tfaLoop:
    cpy totalTasks
    bcs tfaNone                 // no suitable task found
    lda taskState,y
    cmp #TA_ALIVE
    bne tfaNext
    lda screenAlloc,y
    beq tfaNext
    lda screenOwner,y
    cmp #$ff
    bne tfaNext
    // found -- fromTask = dying task, switch display to Y
    lda kArg+1; sta kArg        // fromTask = dying
    sty kbFocus                 // set kbFocus = new task
    tya; tax                    // X = toTask
    jsr doGfxSwitch             // unified surface switch
    jmp tfaRestore
tfaNext:
    iny
    bne tfaLoop
tfaNone:
    // no alive task with a surface -- blank the display
    lda kArg+1; sta kArg        // fromTask = dying (save its surface first)
    lda #$ff; sta kbFocus
    lda #0; tax                 // slot 0 has no surface -> blank
    jsr doGfxSwitch
tfaRestore:
    pla; sta kPtr+1
    pla; sta kPtr               // restore caller's kPtr
tfaRts:
    rts

// ==========================================
//  sysThreadSpawn (12) -- spawn task from spawnPtr
//  Generates name as "parent.N" using kByteToDec
//  (handles 0-255), sets kArg+1=0, then jumps
//  to shared spawn logic.
//  WARNING: kArg+6/kArg+7 (zpBase/zpLen) are the CALLER's to set: a thread may
//  declare its own ZP window (0/0 = none, the same as `spawn`).
//  Returns C=0 on success, C=1 on failure
//  (on failure kArg=0 = no free slots, as sysSpawn).
// ==========================================
sysThreadSpawn:
    jsr findFreeSlot            // X = child slot
    bcc stsGo
    jmp stsFail
stsGo:
    // build name into nameBuf, then DMA to REU
    stx kArg+2                  // save child slot
    ldx curTask
    lda taskThreadIdx,x         // read counter (starts at 0)
    sta kArg+3                  // save for name
    inc taskThreadIdx,x         // bump for next spawn
    jsr kGetTaskName            // kPtr = parent name
    // capped copy: max 11 chars (room for ".255\0" in 16-byte buffer)
    ldy #0
stNameCopy:
    cpy #11
    bcs stNameCopyDone
    lda (kPtr),y
    beq stNameCopyDone
    sta nameBuf,y
    iny
    bne stNameCopy
stNameCopyDone:
    // append ".N"
    lda #$2e                    // '.'
    sta nameBuf,y
    iny
    tya
    clc; adc #<nameBuf
    sta kPtr
    lda #>nameBuf
    adc #0
    sta kPtr+1                  // kPtr = &nameBuf[y] (after '.')
    lda kArg+3                  // saved counter
    jsr kByteToDec              // writes digits at (kPtr), Y=count
    lda #0; sta (kPtr),y        // null-terminate
    // compute + store REU address: REU_THREAD_NAME + childSlot * 16
    //  WARNING: The x16 OVERFLOW rides the CARRY, and `REU_THREAD_NAME`'s low byte
    //  is $00 (asserted in main.asm) -- so there is nothing to add to the low
    //  byte, and nothing may clear C on the way to the high one. The old
    //  `clc / adc #<REU_THREAD_NAME` threw that carry away, so slots 16-23 --
    //  exactly the ones that overflow, since 16 x 16 = $100, and the table is
    //  sized for MAX_TASKS (24) -- addressed $C0xx instead of $C1xx and wrote
    //  their name over the child 16 slots earlier, so two live thread children
    //  showed each other's name in `ps`. STA leaves
    //  C alone, which is what carries it. (The `clc`-then-`adc #0` idiom just
    //  above is the other case: `nameBuf` is a C64 address whose low byte is
    //  real, so there its low add DOES need the clc and its own carry.)
    ldx kArg+2                  // child slot
    txa
    asl; asl; asl; asl          // x16: A = lo byte, C = bit 8 (slot 23 -> $170)
    sta taskThreadNameReuLo,x
    sta kArg                    // REU addr lo (DMA arg)
    lda #>REU_THREAD_NAME
    adc #0                      // + the x16 carry
    sta taskThreadNameReuHi,x
    sta kArg+1                  // REU addr hi (DMA arg)
    // DMA nameBuf -> REU at taskThreadNameReu[childSlot] via kReuFromC64
    lda #0; sta kArg+2          // bank 0
    lda #16; sta kArg+3         // length lo
    lda #0; sta kArg+4          // length hi
    lda #0; sta kArg+5
    lda #<nameBuf; sta kPtr
    lda #>nameBuf; sta kPtr+1
    jsr kReuFromC64             // X preserved (= child slot)
    // Set spawn params and jump to the shared spawn path.
    //  WARNING: kArg+6/kArg+7 (zpBase/zpLen) are NOT cleared here: a thread declares
    //  its own ZP window exactly as `spawn` does. It must be the thread's OWN
    //  registration, because the scheduler's save and restore are gated on the
    //  SWITCHING task's taskZpLen and use ITS shadow -- a window declared by the
    //  parent cannot cover a child: the parent's restore DMAs the parent's shadow
    //  straight over the child's live ZP, and the child's own restore is skipped
    //  at length 0, so its state is gone. A caller that wants no window sets both
    //  to 0. Nothing in this path touches kArg+6/+7 -- the name build and the name
    //  DMA take kArg..kArg+5.
    lda #<nameBuf; sta taskNamePtr
    lda #>nameBuf; sta taskNamePtr+1
    lda #0; sta kArg+1          // threads still allocate no private data
    txa; pha                    // save child slot on stack (scGo clobbers it)
    jsr scGo                    // shared spawn path
    pla; tax                    // restore child slot
    lda curTask
    sta taskParent,x            // thread: set parent for cascading kill
    cli
    rts
stsFail:
    lda #0; sta kArg            // error code: no free slots (matches sysSpawn)
    sec
    cli
    rts

// ==========================================
//  childGate -- may this candidate task run?
// ==========================================
//  In:  X = candidate slot (from findNextAlive)
//  Out: C=0 run it; C=1 hold it -- a thread child whose parent is not running
//  Clobbers A and X. Y is findNextAlive's round-robin counter and is NOT touched.
//
//  A thread child (sysThreadSpawn, `taskThreadNameReuHi != 0`) has no dyn entry
//  and executes its PARENT's code pages -- its entry point is an address inside
//  the parent's image. So once the parent is evicted (`TA_SWAPPED`) or has died
//  (`TA_DEAD`), `dlEvict`/`dynFree` have handed those pages back to the pool
//  bitmap and a scheduled child would run whatever lands there next. Holding the
//  child is the whole fix, and it needs exactly one site: every scheduling
//  decision in the kernel goes through findNextAlive.
//
//  Two of the three death paths already handle the family themselves -- `kill`
//  cascades over the thread subtree, and pool eviction refuses to *swap* a task
//  with live thread children (`dlEvDo`'s child scan -> `dlEvKill`) -- but both
//  miss a case this gate covers:
//    * parent DEAD  -- `sysExit` deliberately does not cascade, so an exit-orphan
//      was left scheduled in freed pages. It now simply never runs. The contract
//      is still "a task reaps its children before exiting" (this is the safety
//      net, and such a child keeps its TCB slot until something kills it).
//    * parent SWAPPED -- the eviction guard tests only the SELECTED victim, so a
//      shared reentrant co-instance swapped by `dlEvMarkShared` would have left
//      its own thread children orphaned. They are now held and resume when
//      `dlRestore` cold-restarts that instance. (A pinned parent never gets here:
//      it is never evicted, so its children are never held.)
//    * parent ALIVE / PAUSED / SLEEPING -- run it. `pause` is per-task by design,
//      and the thread/ipc demos park the PARENT in SYS_SLEEP while their
//      producers work, so a sleeping parent must not freeze its children.
//  A `kDynLoad`ed child (`taskThreadNameReuHi == 0`) has its own pages and is
//  unaffected: run-loaded tasks keep running when their spawner dies.
childGate:
    lda taskThreadNameReuHi,x
    beq cgYes                   // not a thread child -- schedule it
    lda taskParent,x
    bmi cgNoSet                 // $FF/$FE on a THREAD child: its parent's slot was
                                //   recycled (scOrphan clears the link when a new
                                //   task takes the slot) -- the pages it runs are
                                //   not its parent's any more, so HOLD it. $FE is
                                //   the kill cascade's own sentinel: already dying.
    tax                         // X = parent slot (the caller reloads its own)
    lda taskState,x
    beq cgNoSet                 // parent DEAD -> hold (C must be set for the caller)
    cmp #TA_SWAPPED
    bcs cgNo                    // parent SWAPPED (or later) -> hold -- C is already set
cgYes:
    clc
    rts
cgNoSet:
    sec
cgNo:
    rts

//  End of the kernel core. syscall.inc is the last core file (init -> scheduler
//  -> irq -> syscall); main.asm asserts this stays below reuDirBuf ($13D4).
kernelCoreEnd:

.encoding "screencode_mixed"
