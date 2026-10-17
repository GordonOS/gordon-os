// ==========================================
//  SEMAPHORES
// ==========================================
//  X = semaphore index (0..MAX_SEMAPHORES-1). Index 0 is the SCREEN lock
//  (kLockScreen/kUnlockScreen); 1-3 are free for task synchronisation,
//  reachable from tasks as syscalls 10/11.
//
//  WARNING: CALL WITH INTERRUPTS ENABLED -- the normal task state. Both routines
//  bracket their critical section with sei/cli and leave with IRQs enabled, so
//  they are task-level calls (like kLockScreen/kUnlockScreen), not something to
//  call from inside an SEI'd region.
//
//  WARNING: SIZE IS PINNED. The gfx.lib scratch band ends flush against the
//  library-entry block at $FF7E, so any growth here collides with it and the
//  build's overlap error is what catches that. A php/plp-safe variant needs two
//  bytes this band does not have.
// ==========================================

// --- semWait -- acquire semaphore X (counting) ---
//  Decrements count if >0. If 0, yields and retries.
//  Records ownership only when count transitions 1->0 (mutex).
//  Used by kLockScreen and syscall 10. X preserved.
semWait:
    sei                         // atomic check-and-set
    lda semCount,x
    bne swGot                   // available, take it
    cli
    txa; pha                    // save X (clobbered by syscall)
    lda #SYS_YIELD
    jsr syscall                 // yield -- retry on next schedule
    pla; tax
    jmp semWait
swGot:
    cmp #1                      // about to become unavailable?
    bne swDec                   // no -- just decrement, owner unchanged
    lda curTask
    sta semOwner,x              // record owner: 1->0 transition (mutex acquire)
swDec:
    dec semCount,x              // consume one slot
    cli
    rts

// --- semSignal -- release semaphore X (counting) ---
//  Increments count (caps at 255). Clears ownership
//  when count transitions 0->1 (mutex release).
//  Used by kUnlockScreen and syscall 11. X preserved.
semSignal:
    sei
    lda semCount,x
    beq ssWasZero               // was 0 -> becoming 1, clear owner
    cmp #255
    bcs ssDone                  // already maxed, don't overflow
    inc semCount,x              // >0 and <255 -> just increment
    bne ssDone                  // always (can't wrap -- capped at 254->255)
ssWasZero:
    inc semCount,x              // 0 -> 1
    lda #$ff
    sta semOwner,x              // clear ownership (fully released)
ssDone:
    cli
    rts

