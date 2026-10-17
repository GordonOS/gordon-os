// ==========================================
//  pool-top.inc -- library pool placement
// ==========================================
//  Lives in the free pocket AFTER font-help ($53F2-$54AA, i.e. before
//  scrollScreenSlot at $54AB) -- deliberately NOT in the pool: the pool keeps
//  all 109 pages ($5700-$C3FF) available to tasks and libraries alike.
//
//  Why: `allocFromPool` is first-fit from the bottom, so a library that is
//  RE-loaded (libFree -> libLoad -- what happens when a big load self-evicts the
//  shell and releases its lazy libs) would land in the lowest hole, which can
//  be BETWEEN the shell and the tail. That splits the pool permanently: the
//  free pages end up in runs too small for the next big task, `pool` still
//  reports them as free (its rows come from the tables, not the bitmap), and
//  killing tasks cannot heal it -- only a reboot can.
//
//  Fix: libraries allocate from the TOP of the pool. Task code / program RAM /
//  kMalloc keep growing up from the bottom, so an exiting task's pages always
//  rejoin one contiguous task region, and a library can never split it. Both
//  directions run the SAME exhaustive candidate walk, so the fit test is
//  byte-for-byte the same decision -- nothing is reserved for libs and no page
//  becomes unusable for tasks.

.pc = *

// --- dlLibCore -- dlLoadCore for a LIBRARY (highest-fit pool placement) ---
//  Input / output / contract: identical to dlLoadCore (C = result). Extra
//  guarantee: dlAllocHi is set before the load and ALWAYS cleared after, so
//  the direction can never leak into a later task load even when this one
//  fails or bails out early.
//  Clobbers: whatever dlLoadCore clobbers; dlAllocHi always returns 0.
dlLibCore:
    lda #1
    sta dlAllocHi
    jsr dlLoadCore
    php                         // preserve the result carry
    lda #0
    sta dlAllocHi
    plp
    rts

// --- allocFromPoolTop -- highest-fit entry into allocFromPool ---
//  Input:  X = pages. Output: C=0, A = startPage; C=1 if no run of `pages`
//  exists anywhere (the same decision the lowest-fit search would make).
//  Sets the direction and re-enters the shared search, so the
//  `pages > POOL_PAGES` bound check and the bit-set loop are the same code.
allocFromPoolTop:
    lda #1
    sta dlAllocHi
    jmp allocFromPool

// --- dlAllocCode -- allocate a binary's code block in the right direction ---
//  Input:  X = pages -> Output: C=0, A = startPage; C=1 = pool full.
//  dlAllocHi = 1 (a lib load) -> highest fit, and the flag is CONSUMED on
//  success so the task's own program-RAM block -- allocated later inside
//  dlLoadCore with a plain `jsr allocFromPool` -- still grows up from the
//  bottom. On failure the flag is deliberately LEFT SET: dlLoadCore's
//  evict-and-retry loop must retry with the same placement.
//  Clobbers: A, X, Y, kArg+4..+6 (allocFromPool's own clobbers -- X included).
dlAllocCode:
    lda dlAllocHi
    beq dlAcLow
    jsr allocFromPoolTop
    bcs dlAcRts                 // no room -- keep the flag for the retry
    pha                         // save startPage (pha/pla do not touch C)
    lda #0
    sta dlAllocHi               // consumed -- the next allocation is a task again
    pla
dlAcRts:
    rts
dlAcLow:
    jmp allocFromPool

// ==========================================
//  Caller-eviction accounting
// ==========================================
//  Evicting a task runs dlEvLibs -> dlLibRelease, which frees every declared
//  library that nothing else holds (libRefCount == 1). Those pages are part
//  of what sacrificing the CALLER makes available, and dlSelfEvictOk has to
//  count them: with libraries parked at the TOP of the pool they are not
//  part of the free space, so a code-pages-only count refuses loads that
//  would fit: a code-pages-only count refuses loads that in fact fit, because
//  the eviction also frees the libraries the caller is the last holder of.

// --- dlCallerAddLibs -- caller's code pages + its releasable lib pages ---
//  Input:  A = caller's code pages, Y = caller's dyn slot.
//  Output: A = total pages the caller's eviction makes available.
//  Clobbers: A, X, Y, kArg+7, dlLibTmp, dlLibTmp2 (all dead at that point --
//  dlPreEvict stashes kArg+1..+7 on its stack and restores them).
dlCallerAddLibs:
    pha                         // code pages
    jsr dlCallerLibPages        // A = liberatable lib pages
    sta kArg+7
    pla
    clc
    adc kArg+7
    rts

// --- dlCallerLibPages -- pages the caller's libs give back at eviction ---
//  Sums libPages[x] over the libs the CALLER declares whose refcount is 1
//  (only those are freed by dlLibRelease). Input: Y = caller's dyn slot.
//  Output: A = pages. Clobbers A, X, Y, kArg+7, dlLibTmp, dlLibTmp2.
dlCallerLibPages:
    sty dlLibTmp2               // dleBitSet reads the dyn slot from here
    lda #0
    sta kArg+7                  // page total
    ldx #0
dlclpLoop:
    cpx #MAX_LIBS
    bcs dlclpDone
    jsr dleBitSet               // Z=0 -> lib X is declared by this slot
    beq dlclpNext
    lda libRefCount,x
    cmp #1
    bne dlclpNext               // shared -- dlLibRelease keeps it resident
    lda kArg+7
    clc
    adc libPages,x
    sta kArg+7
dlclpNext:
    inx
    bne dlclpLoop
dlclpDone:
    lda kArg+7
    rts
.label poolTopEnd = *
