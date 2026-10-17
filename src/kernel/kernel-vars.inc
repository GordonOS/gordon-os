// ==========================================
//  Kernel data
// ==========================================
cursorState:                    // NOTE: aliases cursorCol/cursorRow ($E11A/$E11B --
    .byte 0, 0                  //   pinned by main.asm's .asserts).
                                // Scheduler DMAs use this label as source/dest;
                                // writing cursorCol/Row IS writing cursorState.

screenBase:
    .byte $00, $20              // framebuffer base ($2000) -- read by EhBASIC text color tracking
dispBase:
    .byte $00, $20              // unused in all-bitmap model -- kept so the
                                // $E1xx layout stays stable (EhBASIC aliases)

semCount:
    .fill MAX_SEMAPHORES, 0    // pool of counting semaphores (0=unavailable)
semOwner:
    .fill MAX_SEMAPHORES, $ff  // last task to exhaust semaphore 1->0 ($ff=none)
screenLockKPtrLo:
    .fill MAX_TASKS, 0          // per-task saved kPtr lo
screenLockKPtrHi:
    .fill MAX_TASKS, 0          // per-task saved kPtr hi
screenLockKArg:
    .fill MAX_TASKS, 0          // per-task saved kArg

// per-task ZP save for context switch (kPtr, srcPtr, kArg)
taskKPtrLo:
    .fill MAX_TASKS, 0
taskKPtrHi:
    .fill MAX_TASKS, 0
taskSrcPtrLo:
    .fill MAX_TASKS, 0
taskSrcPtrHi:
    .fill MAX_TASKS, 0
taskSleepTicks:
    .fill MAX_TASKS, 0          // per-task sleep countdown (0=awake)

readStrChar:
    .byte 0                     // temp for character during kReadString
readStrBuf:
    .word 0                     // saved buffer pointer for kReadString
readStrLen:
    .byte 0                     // current length (survives task switches)
readStrMax:
    .byte 0                     // max length (survives task switches)
readStrPos:
    .byte 0                     // cursor position within buffer (0..len)
readStrStartRow:
    .byte 0                     // saved cursor row on entry (for HOME)

timeBuf:
    .fill 16, 0                 // time string buffer (16 chars)

kbFocus:
    .byte $ff                   // task index that owns keyboard ($ff=none)
tfaActive:
    .byte 0                     // 1 = transferFocusAway set kArg as fromTask

// --- kmEvictAlloc -- kMalloc's pool-full recovery -----------------------
//  Entered from kMallocImpl's allocation path with SEI already asserted
//  (the impl opens `php`/`sei`) and X = pages requested.  Evicts one
//  resident task to make room, then RETRIES the allocation, so a task
//  can ask for memory the pool cannot spare while other tasks are
//  resident.  `edit` is the case that needs it: its 4-page document
//  chunk asks for more than its own load left free.
//  The caller is never the victim (dlNoSelfEvict): it is executing its
//  own code, and freeing that mid-call is a crash, not a graceful fail.
//  C/A are allocFromPool's own result, so C=1 still means "out of
//  room" -- the eviction found no victim, or freed too little.
//  WARNING: dlEvict clobbers kArg..kArg+7, so the page count does NOT survive
//  in kArg -- but allocFromPool echoes the request in kArg+4 on entry, and the
//  retry below re-installs it, so kArg+4 is valid again when this returns.
//  kMallocImpl reads the count from there.
//  WARNING: THIS LIVES IN A RESERVED 16-BYTE BLOCK ON PURPOSE.  Nothing references the
//  block (the kernel's hexChars table lives in string.lib) and its SIZE is ABI --
//  the labels after it must not shift -- so these are free bytes that only code
//  may spend.  kernel-vars is plain RAM, so any address in it is executable, and
//  the routine needs no relocation and moves no address.
kmEvictAlloc:
    txa
    pha                          // save the request -- dlEvict clobbers X
    lda #1
    sta dlNoSelfEvict            // never evict the caller of a kMalloc
    jsr dlEvict                  // C=0 evicted one victim
    pla
    tax                          // X = pages again (allocFromPool's input)
    jmp allocFromPool            // retry -- its rts returns to kMallocImpl
    .fill 16 - (* - kmEvictAlloc), $ea

randomSeed:
    .byte $42                   // LFSR seed
surfResolved:
    .byte 0                     // surfDisplayed's scratch: resolve(kbFocus).
                                // Kernel-private ON PURPOSE -- surfDisplayed runs
                                // from the timer IRQ (per-switch cursor sync) and
                                // from sysExit, where a parked task's kArg block
                                // is live. Address and size are pinned by the
                                // kernel-vars layout.

// --- Keyboard ring buffer ---
keybLast:
    .byte $ff                   // last scanned key ($ff=none)
keybHeld:
    .byte $ff                   // key currently held ($ff=none)
keybTimer:
    .byte 0                     // repeat countdown (0=idle)
keybCount:
    .byte 0                     // entries in buffer (0-16)
keybHead:
    .byte 0                     // write index
keybTail:
    .byte 0                     // read index
keybBuffer:
    .fill 16, 0                 // 16-byte ring buffer
keybShift:
    .byte 0                     // 1 = shift held

// --- Cursor blink ---
blinkTick:
    .byte BLINK_RATE            // countdown to next toggle
cursorVisible:
    .byte 1                     // 1=visible, 0=hidden
cursorOn:
    .byte 1                     // 1=cursor enabled, 0=disabled
//  The kill cascade's ROOT slot. It must live where neither `surfFreeSlot`
//  (kArg..kArg+3) nor `dynFree` (kArg+3..kArg+7) nor `transferFocusAway`
//  (kArg) can reach, and its offset is fixed so nothing after it moves.
dlCascadeRoot:
    .byte 0
//  The cascade's fixpoint "changed" flag, out of reach for the same reason
//  (`kArg+1` would be clobbered by `surfFreeSlot`).
dlCascadeFlag:
    .byte 0

// --- TCB init scratch (SEI-protected, used only by initTCB_ptr) ---
initTaskX:
    .byte 0                     // temp X (task index) save during initTCB_ptr
frameBuf:
    .fill 8, 0                  // 8-byte temp buffer for building stack frame
nameBuf:
    .fill 16, 0                 // temp buffer for REU->C64 thread name DMA

