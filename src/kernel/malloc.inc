// ==========================================
//  GordonOS -- Generic page-granular malloc (kMalloc/kFree)
// ==========================================
//  Lives in the high region right after reuCmpName.
//
//  malloc table lives in main.asm (mallocOwner/StartPage/Pages/Swapped +
//  mallocCount). Blocks are page-aligned runs in the single pool, owned by
//  a GordonOS task slot, and freed on exit/kill (freeMallocBlocks, called
//  from dynFree) or preserved across eviction via the per-dyn-slot REU
//  shadow (dlEvict/dlRestore).
//
//  kMallocImpl: A = pages (1..MAX_MALLOC_PAGES)
//               -> C=0, kPtr = block addr; C=1 no space
//  kFreeImpl:   kPtr = block addr -> C=0 freed; C=1 not found
//  freeMallocBlocks: X = task slot -> free all blocks owned by that slot.
//  kMallocImpl/kFreeImpl are SEI-atomic; freeMallocBlocks requires SEI'd
//  caller (dynFree's contract).

.pc = *

kMallocImpl:
    php
    sei
    tax                          // X = pages
    bne kmChkPages               // pages != 0 -> check cap
    jmp kmNoSpace                // 0 pages -> fail
kmChkPages:
    cpx #MAX_MALLOC_PAGES+1
    bcc kmChkTable               // pages <= cap -> check table
    jmp kmNoSpace
kmChkTable:
    stx kArg                     // stash pages (allocFromPool keeps kArg..+3)
    lda mallocCount
    cmp #MAX_MALLOC_BLOCKS
    bcc kmCapGo                  // table not full -> cap check
    jmp kmNoSpace
kmCapGo:
    // per-owner cap: owned + requested <= MAX_MALLOC_PAGES (bounds REU shadow)
    lda #0
    sta kArg+1                   // running total
    ldy #0
kmCapLoop:
    cpy #MAX_MALLOC_BLOCKS
    bcs kmCapDone
    lda mallocOwner,y
    cmp curTask
    bne kmCapNext
    lda mallocPages,y
    clc
    adc kArg+1
    sta kArg+1
kmCapNext:
    iny
    bne kmCapLoop
kmCapDone:
    lda kArg+1
    clc
    adc kArg                     // total = owned + requested
    cmp #MAX_MALLOC_PAGES+1
    bcc kmAllocNow               // total <= cap -> allocate
    jmp kmNoSpace
kmAllocNow:
    ldx kArg
    jsr allocFromPool            // C=1 full; else A = startPage
    bcc kmAllocOk
    jsr kmEvictAlloc             // pool full -- evict one resident task and
                                 //   retry once (kernel-vars.inc).  WARNING: Hooked
                                 //   on the FAILURE path on purpose: wrapping
                                 //   the attempt above would evict on every
                                 //   kMalloc, including ones that were never
                                 //   going to need the room.
    bcs kmNoSpace                // still full -> the shared tail (falls
                                 //   through to kmAllocOk when it fits)
kmAllocOk:
    sta kArg+2                   // startPage
    // find a free table slot
    ldy #0
kmFindSlot:
    lda mallocOwner,y
    cmp #$ff
    beq kmSlotFound
    iny
    cpy #MAX_MALLOC_BLOCKS
    bcc kmFindSlot
    // defensive: table full despite the count check -- free the pages
    //  (unreachable in any case: the kmChkTable check above scans the same table)
    ldx kArg+2
    ldy kArg+4                   // the count, echoed by allocFromPool (not kArg,
                                 //   which kmEvictAlloc's dlEvict clobbers)
    jsr freeFromPool
    jmp kmNoSpace
kmSlotFound:
    lda curTask
    sta mallocOwner,y
    lda kArg+2
    sta mallocStartPage,y
    lda kArg+4                   // pages -- allocFromPool echoes the request in
                                 //   kArg+4 and re-installs it on the kmEvictAlloc
                                 //   retry, so it is the one valid home. NEVER read
                                 //   the count from X: allocFromPool clobbers X (its
                                 //   poolBit* helpers end in `tax`), and a wrong count
                                 //   either strands pages in the bitmap or frees the
                                 //   pages of whoever owns the range above.
    sta mallocPages,y
    lda #0
    sta mallocSwapped,y
    inc mallocCount
    // kPtr = POOL_BASE + startPage << (8+POOL_PAGE_SHIFT)
    lda kArg+2
    :poolAsl()
    clc
    adc #>POOL_BASE
    sta kPtr+1
    lda #0
    sta kPtr
    plp
    clc
    rts
kmNoSpace:
    plp
    sec
    rts

kFreeImpl:
    php
    sei
    // startPage = (kPtr - POOL_BASE) >> (8+POOL_PAGE_SHIFT)
    lda kPtr+1
    sec
    sbc #>POOL_BASE
    :poolLsr()
    sta kArg+4                   // target startPage
    lda #0
    sta kArg+5                   // table index
kfLoop:
    lda kArg+5
    cmp #MAX_MALLOC_BLOCKS
    bcs kfNotFound
    tay
    lda mallocOwner,y
    cmp #$ff
    beq kfNext
    lda mallocStartPage,y
    cmp kArg+4
    bne kfNext
    lda mallocSwapped,y
    bne kfClear                  // pages already freed (swapped) -- just clear
    ldx mallocStartPage,y
    lda mallocPages,y
    tay
    jsr freeFromPool             // X=startPage, Y=pages
kfClear:
    jsr clearMallocEntry
    plp
    clc
    rts
kfNext:
    inc kArg+5
    bne kfLoop
kfNotFound:
    plp
    sec
    rts

// --- clearMallocEntry -- clear the malloc entry at index kArg+5 ---
//  Clobbers A, Y. Called SEI'd (kFreeImpl + freeMallocBlocks).
clearMallocEntry:
    ldy kArg+5
    lda #$ff
    sta mallocOwner,y
    lda #0
    sta mallocPages,y
    sta mallocSwapped,y
    dec mallocCount
    rts

// --- freeMallocBlocks -- free every block owned by a task slot ---
//  Input: X = task slot. Requires SEI'd caller.
//  Clobbers: A, X, Y, kArg+3..kArg+7.
freeMallocBlocks:
    stx kArg+3                   // owner (task slot)
    lda #0
    sta kArg+5                   // table index
fmbLoop:
    lda kArg+5
    cmp #MAX_MALLOC_BLOCKS
    bcs fmbDone
    tay
    lda mallocOwner,y
    cmp kArg+3
    bne fmbNext
    lda mallocSwapped,y
    bne fmbClear                 // pages already freed (swapped) -- just clear
    ldx mallocStartPage,y
    lda mallocPages,y
    tay
    jsr freeFromPool             // X=startPage, Y=pages
fmbClear:
    jsr clearMallocEntry
fmbNext:
    inc kArg+5
    bne fmbLoop
fmbDone:
    rts

// ==========================================
//  Malloc preserve/restore (eviction)
// ==========================================
//  On eviction a victim's program-RAM block (KF_MALLOC_RAM) and any
//  task-runtime kMalloc blocks are DMA'd to the per-dyn-slot REU shadow
//  (banks 252-255, 32 KB/slot: program RAM first, then malloc blocks in
//  table order; <=8 RAM + <=8 malloc pages = 32 KB max). dlLoadCore
//  re-marks the program-RAM region on restore; task-runtime blocks are
//  re-allocated via allocFromPool. All SEI'd.

// --- dlSmOne -- DMA one block C64->REU, free its pages, advance shadow hi ---
//  Input: dlMallocPage = startPage, dlMallocCnt = pages, kArg+2 = shadow
//         bank, dlMallocHi = shadow hi (lo = 0).
//  Clobbers: A, X, Y, kPtr.
dlSmOne:
    lda dlMallocPage
    :poolAsl()
    clc
    adc #>POOL_BASE
    sta kPtr+1
    lda #0
    sta kPtr                    // kPtr = block C64 addr
    lda dlMallocHi
    sta kArg+1                  // REU addr hi
    lda #0
    sta kArg                    // REU addr lo
    sta kArg+3                  // len lo = 0
    sta kArg+5
    lda dlMallocCnt
    :poolAsl()
    sta kArg+4                  // len hi = pages*8
    jsr reuFromC64              // C64->REU (clobbers A, Y; preserves X)
    ldx dlMallocPage
    ldy dlMallocCnt
    jsr freeFromPool            // X=startPage, Y=pages
    lda dlMallocCnt
    :poolAsl()
    clc
    adc dlMallocHi
    sta dlMallocHi
    rts

// --- dlRmOne -- DMA one block REU->C64, advance shadow hi ---
//  Input: dlMallocPage = startPage, dlMallocCnt = pages, kArg+2 = shadow
//         bank, dlMallocHi = shadow hi (lo = 0).
//  Clobbers: A, X, Y, kPtr.
dlRmOne:
    lda dlMallocPage
    :poolAsl()
    clc
    adc #>POOL_BASE
    sta kPtr+1
    lda #0
    sta kPtr                    // kPtr = block C64 addr
    lda dlMallocHi
    sta kArg+1
    lda #0
    sta kArg
    sta kArg+3
    sta kArg+5
    lda dlMallocCnt
    :poolAsl()
    sta kArg+4
    jsr reuToC64                // REU->C64 (clobbers A, Y; preserves X)
    lda dlMallocCnt
    :poolAsl()
    clc
    adc dlMallocHi
    sta dlMallocHi
    rts

// --- dlSwapMalloc -- DMA a task's blocks to its REU shadow (evict) ---
//  Input: Y = dyn slot. Clobbers A, X, Y, kPtr, kArg..kArg+5.
dlSwapMalloc:
    sty dlMallocSlot            // save slot (dlSmOne clobbers Y)
    tya
    lsr                         // slot >> 1 -- 2 slots/bank (32 KB each)
    clc
    adc #REU_MALLOC_BANK
    sta kArg+2                  // shadow bank
    tya
    and #1
    beq dlSmHi0                 // slot even -> hi = 0
    lda #$80
dlSmHi0:
    sta dlMallocHi              // shadow hi (lo = 0)
    lda dynTaskId,y
    sta dlMallocOwner
    // program-RAM block (KF_MALLOC_RAM) first
    lda dynTaskRamPages,y
    beq dlSmNoRam
    sta dlMallocCnt             // stash pages (harmless on the $ff path)
    lda dynTaskRamStartPage,y
    cmp #$ff
    beq dlSmNoRam
    sta dlMallocPage
    jsr dlSmOne
    ldy dlMallocSlot
    lda #$ff
    sta dynTaskRamStartPage,y   // mark program RAM swapped
dlSmNoRam:
    // task-runtime malloc blocks
    lda #0
    sta dlMallocIdx
dlSmLoop:
    lda dlMallocIdx
    cmp #MAX_MALLOC_BLOCKS
    bcs dlSmDone
    tay
    lda mallocOwner,y
    cmp dlMallocOwner
    bne dlSmNext
    lda mallocSwapped,y
    bne dlSmNext
    lda mallocStartPage,y
    sta dlMallocPage
    lda mallocPages,y
    sta dlMallocCnt
    jsr dlSmOne
    ldy dlMallocIdx
    lda #1
    sta mallocSwapped,y
    lda #0
    sta mallocStartPage,y
dlSmNext:
    inc dlMallocIdx
    bne dlSmLoop
dlSmDone:
    rts

// --- dlRestoreMalloc -- DMA swapped blocks back (restore) ---
//  Input: Y = dyn slot, X = task slot (= task id). Clobbers A, X, Y,
//  kPtr, kArg..kArg+5.
dlRestoreMalloc:
    stx dlMallocOwner            // task slot -- also the malloc scan owner
    tya
    lsr                         // slot >> 1 -- 2 slots/bank (32 KB each)
    clc
    adc #REU_MALLOC_BANK
    sta kArg+2
    tya
    and #1
    beq dlRmHi0                 // slot even -> hi = 0
    lda #$80
dlRmHi0:
    sta dlMallocHi
    // program-RAM block (re-marked by dlLoadCore at dlRegStartPage)
    lda dynTaskRamPages,y
    beq dlRmNoRam
    sta dlMallocCnt             // stash pages (harmless on the bne path)
    lda dynTaskRamStartPage,y
    cmp #$ff
    bne dlRmNoRam               // not swapped -- nothing to restore
    lda dlRegStartPage
    sta dlMallocPage
    jsr dlRmOne
    ldy dlRegSlot
    lda dlRegStartPage
    sta dynTaskRamStartPage,y
dlRmNoRam:
    // task-runtime malloc blocks
    lda #0
    sta dlMallocIdx
dlRmLoop:
    lda dlMallocIdx
    cmp #MAX_MALLOC_BLOCKS
    bcs dlRmBn
    tay
    lda mallocOwner,y
    cmp dlMallocOwner
    bne dlRmNext
    lda mallocSwapped,y
    beq dlRmNext
    ldx mallocPages,y
    stx dlMallocCnt
    jsr allocFromPool           // A = startPage, C=1 full
    bcc dlRmAllocOk
    //  Pool full: STOP the walk instead of skipping this block. Skipping would
    //  leave the shadow cursor where it was, so every block after this one
    //  would be DMA'd from the wrong offset -- and their contents do not matter
    //  anyway, because a restored task is cold-restarted at its entry and
    //  re-kMallocs. Bailing out leaves the rest of the data in the shadow with
    //  `swapped` still set, and those entries are cleared when the task dies
    //  (freeMallocBlocks), so nothing leaks.
    jmp dlRmBn
dlRmAllocOk:
    sta dlMallocPage
    jsr dlRmOne
    ldy dlMallocIdx
    lda dlMallocPage
    sta mallocStartPage,y
    lda #0
    sta mallocSwapped,y
dlRmNext:
    inc dlMallocIdx
    bne dlRmLoop
dlRmBn:
    ldy dlRegSlot
    lda dynTaskRamPages,y
    beq dlRmDone                // no program RAM -- nothing to re-write
    sta dlMallocCnt             // stash page count (the write loop resets Y;
                                //   dlRegPages is clobbered by dlReLibs' lib
                                //   re-load -> read the dyn table instead)
    ldx dlMallocOwner
    lda taskDataPtrLo,x
    sta kPtr
    lda taskDataPtrHi,x
    sta kPtr+1
    ldy #0
    lda #0
    sta (kPtr),y
    iny
    lda dlRegStartPage
    :poolAsl()
    clc
    adc #>POOL_BASE
    sta (kPtr),y
    iny
    lda #0
    sta (kPtr),y
    iny
    lda dlMallocCnt
    :poolAsl()
    sta (kPtr),y
dlRmDone:
    rts

// ==========================================
//  dynTableReset -- warm-reboot reset helper (called from init.inc)
// ==========================================
//  Resets task slots 1.. to DEAD (slot 0 = boot stays ALIVE), the
//  dynamic-task table, and the malloc table. Consolidated right after
//  the malloc code. Clobbers A, X.
dynTableReset:
    ldx #MAX_TASKS-1
    lda #0
dtrTask:
    sta taskState,x             // slots 23..1 -- slot 0 (boot) untouched
    dex
    bne dtrTask
    ldx #MAX_DYN_TASKS-1
dtrLoop:
    lda #$FF
    sta dynTaskId,x
    sta dynTaskResidency,x
    sta mallocOwner,x
    lda #0
    sta dynTaskFlags,x
    sta dynTaskRamStartPage,x
    sta dynTaskRamPages,x
    sta mallocStartPage,x
    sta mallocPages,x
    sta mallocSwapped,x
    dex
    bpl dtrLoop
    sta mallocCount
    rts
.label dynTableResetEnd = *
