// ==========================================
//  SCREEN LOCK
// ==========================================

// --- lockScreen -- acquire screen semaphore (#0) ---
//  Saves kPtr/kArg to per-task arrays (safe across
//  yields). Only the caller blocks -- other tasks run.
lockScreen:
    ldx #0                      // semaphore 0 = screen
    jsr semWait
    ldx curTask
    lda kPtr; sta screenLockKPtrLo,x
    lda kPtr+1; sta screenLockKPtrHi,x
    lda kArg; sta screenLockKArg,x
    rts

// --- unlockScreen -- release screen semaphore (#0) ---
//  Restores kPtr/kArg from per-task arrays, frees lock.
unlockScreen:
    sei
    ldx curTask
    lda screenLockKPtrLo,x; sta kPtr
    lda screenLockKPtrHi,x; sta kPtr+1
    lda screenLockKArg,x; sta kArg
    ldx #0                      // semaphore 0 = screen
    jsr semSignal
    rts

