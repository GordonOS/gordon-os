// ==========================================
//  warmBootReset -- warm-reboot entry point (jumped to from scheduler.inc)
// ==========================================
//  Resets the state a warm reboot would otherwise carry over, then performs the
//  `jmp start`. `start:` re-initialises the pool bitmap, the dyn tables, the
//  residency records and the surfaces, but kernel DATA survives the jump -- and
//  two pieces of it must not:
//
//  (1) THE LIBRARY SYSTEM. libStartPage/libPages/libRefCount and the patched
//      libJumpLo/Hi entries would survive, with two consequences:
//        * the reboot would not reclaim libraries the way a power-on does -- a
//          library stays listed in `pool` with nothing able to free it;
//        * the patched jump entries still point into pool pages the fresh
//          bitmap calls free. The next task loaded into those pages is entered
//          through a library entry -- straight into its own code from the wrong
//          entry point -- with the cold stubs gone, so nothing can re-trap it.
//      Fix: call the loader's own `libFree` for EVERY id. libFree returns the
//      pages, re-points every call entry at its cold stub and sets
//      libStartPage = $FF; for a library that was never loaded it returns at
//      once. Walking every id rather than the refcounted ones is deliberate:
//      the case to repair is precisely a LOADED library sitting at refcount 0.
//
//  (2) THE SPRITE STATE, in three pieces. The live state in kernel RAM, the
//      byte that names the installed slot, and every task's block in the REU
//      all survive the jump -- so the fresh boot task (slot 0) would come back
//      wearing whatever sprites the last session's slot 0 was showing.
//      `warmSpriteReset` (sprite.inc) clears all three: it unpublishes the
//      stale state, forgets which slot is installed, and zeroes the blocks, so
//      no task can inherit a pre-reboot sprite. See there for why the boot
//      task needs this when no spawned task does.
//
//  Reached from scheduler.inc's "no alive tasks" reboot, which is IRQ context:
//  interrupts are already off -- what libFree wants -- and a task switch cannot
//  land mid-walk. Registers are dead there (everything is about to be
//  re-initialised), so clobbering A/X/Y/srcPtr is fine, except that libFree
//  clobbers X, hence the stack around the call.
//
//  Placed in the run at $181D-$1843, between spawnCore's relocated first half
//  and taskLo ($186F) -- the reset's 28B plus `sprite.lib`'s name string, which
//  fills the run exactly ($1844-$186E stays free). It is reached by a single
//  re-pointed `jmp`, so it can live anywhere -- which is exactly why it lives
//  here: the kernel core's own band is left to the changes that have nowhere
//  else to go.
// ==========================================

.pc = $181D

warmBootReset:
    jsr warmClearBufRecs        // instance-buffer records go with the heap too
    jsr warmSpriteReset         // no task may inherit a pre-reboot sprite
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

.label warmResetEnd = *

// ==========================================
//  warmClearBufRecs -- drop every slot's instance-buffer record
// ==========================================
//  `start:` rewinds dataHeap to $1D40, but a (size, pointer) pair survives the
//  jump as kernel data. A spawn that finds a matching size would then take the
//  reuse path and hand out a buffer the fresh bump allocator is about to issue
//  again -- two tasks on one buffer, and the newcomer's zeroing wipes the
//  other's instance state (the shell's line buffer lives at offset 0 of its
//  own buffer, so its console goes deaf: keys echo, every line reads empty).
//  The free list is cleared by `start:` for the same reason.
//
//  Reached by a jsr from warmBootReset, so the routine can live anywhere: the
//  $CA1B run is otherwise unused, and keeping it out of the core band leaves
//  that band's headroom to the changes that have nowhere else to go.
.pc = $CA1B

warmClearBufRecs:
    ldx #MAX_TASKS-1
    lda #0
wcbrLoop:
    sta taskDataSize,x
    sta taskDataPtrLo,x
    sta taskDataPtrHi,x
    dex
    bpl wcbrLoop
    rts

.label warmBufRecsEnd = *
