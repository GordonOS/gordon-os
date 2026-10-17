// ==========================================
//  GordonOS -- Dynamic Task Loader
//  Pool bitmap allocator
// ==========================================
//  Placed at $4310: loader + dlCopyArgs + lib-lifecycle
//  helpers form one contiguous block $4310-$526F (libstubs follows).

.pc = $4310

// --- allocFromPool -- allocate `pages` consecutive pages ---
//  Input:  X = pages needed. dlAllocHi = 1 -> HIGHEST fit, 0 -> lowest fit.
//  Output: C=0, A = startPage; C=1 if no space
//  Clobbers: A, X, Y, kArg+4..kArg+6
//  WARNING: X IS CLOBBERED. The poolBit* helpers end in `tax`, and the search
//  runs one per candidate page plus one per page marked, so X cannot be
//  promised to any caller. The requested count is echoed in kArg+4 at entry
//  and re-installed by every call (so it is still valid on the kmEvictAlloc
//  retry): read the count back from kArg+4, never from a register.
//  One exhaustive search, two placements. The direction only changes WHICH
//  END the candidate walk starts from -- never whether a run is found -- so
//  the allocation succeeds exactly when it did before; no page is reserved
//  for either kind and nothing is taken from the task side.
//    lowest  (dlAllocHi = 0) -- task code, program RAM, kMalloc. Tasks grow
//            up from the bottom, so an exiting task's pages always rejoin
//            the free space below them.
//    highest (dlAllocHi = 1) -- libraries, via dlAllocCode (pool-top.inc).
//            Libs come down from the top, so one can never sit BETWEEN two
//            task blocks and strand the space -- leaving the free pages in runs
//            too small for the next big task while `pool`, whose rows come from
//            the tables rather than the bitmap, still reports them as free.
allocFromPool:
    stx kArg+4
    //  0 pages is not a request: without this it would "succeed" with startPage
    //  0 and mark pool page 0 used. Every caller guards it (kMallocImpl,
    //  dlLoadCore's RAM path, the library loader), so this is the allocator
    //  defending its own contract.
    txa
    beq afpFull
    // Early check: if pages > totalPages, fail immediately.
    lda #POOL_PAGES
    cmp kArg+4
    bcc afpFull                 // pages > total -> fail
afpInit:
    lda dlAllocHi
    beq afpInitLo
    lda #POOL_PAGES
    sec
    sbc kArg+4                  // highest fit: top-most candidate start
    sta kArg+6
    jmp afpScan
afpInitLo:
    lda #0
    sta kArg+6                  // lowest fit: startPage candidate

afpScan:
    // For each candidate start, check `pages` consecutive bits.
    lda #0
    sta kArg+5                  // page counter (preserved across poolBitCheck)

afpChk:
    lda kArg+5
    clc
    adc kArg+6                  // A = startPage + offset
    jsr poolBitCheck            // C=1 if already set
    bcs afpNext
    inc kArg+5
    lda kArg+5
    cmp kArg+4
    bcc afpChk

    // All clear -- set the bits and return.
    lda #0
    sta kArg+5                  // reset counter for set loop
afpSet:
    lda kArg+5
    clc
    adc kArg+6
    jsr poolBitSet
    inc kArg+5
    lda kArg+5
    cmp kArg+4
    bcc afpSet
    lda kArg+6                  // return startPage
    clc
    rts

afpNext:
    lda dlAllocHi
    bne afpNextHi
    inc kArg+6
    // Check if startPage + pages > totalPages
    lda kArg+6
    clc
    adc kArg+4
    cmp #POOL_PAGES+1           // if start+pages > total, C=1
    bcs afpFull
    jmp afpScan

afpNextHi:
    lda kArg+6                  // highest fit: walk candidates down to 0
    beq afpFull                 // exhausted -> no run of `pages` exists
    dec kArg+6
    jmp afpScan

afpFull:
    ldx kArg+4                  // restore X
    sec
    rts

// --- poolBitCheck -- test bit N in poolBitmap ---
//  Input:  A = bit index (0..15)
//  Output: C=1 if set, C=0 if clear
//  Clobbers: A, X, Y
poolBitCheck:
    pha
    lsr
    lsr
    lsr                         // bitIdx / 8 -> byte offset
    tay
    pla
    and #7                      // bit position
    tax
    lda bitMasks,x
    and poolBitmap,y
    beq pbcClr
    sec
    rts
pbcClr:
    clc
    rts

// --- poolBitSet -- set bit N in poolBitmap ---
//  Input:  A = bit index (0..15)
//  Clobbers: A, X, Y
poolBitSet:
    pha
    lsr
    lsr
    lsr
    tay
    pla
    and #7
    tax
    lda bitMasks,x
    ora poolBitmap,y
    sta poolBitmap,y
    rts

// --- poolBitClear -- clear bit N in poolBitmap ---
//  Input:  A = bit index (0..15)
//  Clobbers: A, X, Y
poolBitClear:
    pha
    lsr
    lsr
    lsr
    tay
    pla
    and #7
    tax
    lda bitMasks,x
    eor #$FF                    // invert mask
    and poolBitmap,y
    sta poolBitmap,y
    rts

// --- freeFromPool -- free `pages` starting at `startPage` ---
//  Input:  X = startPage, Y = pages
//  Clobbers: A, X, Y, kArg+4, kArg+6 (poolBitClear ends in `tax`)
freeFromPool:
    stx kArg+6                  // startPage
    sty kArg+4                  // page count

ffpLoop:
    lda kArg+4
    beq ffpDone
    lda kArg+6
    clc
    adc kArg+4
    sec
    sbc #1                      // A = startPage + count - 1 (clear from end)
    jsr poolBitClear
    dec kArg+4
    jmp ffpLoop
ffpDone:
    rts

// --- markPagesUsed -- set bits for data-region reservation pages (section 3.3) ---
//  Input:  X = startPage, Y = pages
//  Clobbers: A, X, Y, kArg+4, kArg+6 (poolBitSet ends in `tax`)
markPagesUsed:
    stx kArg+6
    sty kArg+4

mpuLoop:
    lda kArg+4
    beq mpuDone
    lda kArg+6
    clc
    adc kArg+4
    sec
    sbc #1
    jsr poolBitSet
    dec kArg+4
    jmp mpuLoop
mpuDone:
    rts

// ==========================================
//  Dynamic Task Table Operations
// ==========================================

// --- dynRegister -- add entry to dynamic task table ---
//  Input:  kArg/kArg+1 = origin, kArg+2/+3 = size,
//          kArg+4 = flags, kArg+5 = pool, kArg+6 = startPage
//  Output: C=0, A = slot; C=1 if full
//  Clobbers: A, Y
dynRegister:
    ldy #0
drLoop:
    lda dynTaskId,y
    cmp #$FF
    beq drFound
    iny
    cpy #MAX_DYN_TASKS
    bcc drLoop
    sec
    rts
drFound:
    lda kArg; sta dynTaskOriginLo,y
    lda kArg+1; sta dynTaskOriginHi,y
    lda kArg+2; sta dynTaskSizeLo,y
    lda kArg+3; sta dynTaskSizeHi,y
    lda kArg+4; sta dynTaskFlags,y
    lda kArg+6; sta dynTaskStartPage,y
    // Use the stashed parse mask, NOT dlLibMask -- dlLibRelease (via
    // dlEvict inside dlLoadCore) clobbers dlLibMask with the VICTIM's
    // mask, which would corrupt this slot's libMask (bug: basic + fp).
    lda dlRollMask; sta dynTaskLibMaskLo,y   // per-slot lib mask
    lda dlRollMaskHi; sta dynTaskLibMaskHi,y
    tya; sta dynTaskId,y       // placeholder = slot index (replaced by real ID after spawn)
    lda #0; sta dynTaskPageCount,y
    lda #$FF; sta dynTaskResidency,y
    inc dynTaskCount
    tya
    clc
    rts

// --- dynFindByTaskId -- find slot by GordonOS task ID ---
//  Input:  A = task ID
//  Output: C=0, A = slot; C=1 if not found
//  Clobbers: A, Y
dynFindByTaskId:
    ldy #0
dfLoop:
    cmp dynTaskId,y
    beq dfFound
    iny
    cpy #MAX_DYN_TASKS
    bcc dfLoop
    sec
    rts
dfFound:
    tya
    clc
    rts

// --- dynFree -- free a dynamic task's entries and bitmap pages ---
//  Input:  A = GordonOS task ID
//  Output: C=0 freed (or not found but statically compiled), C=1 not found
//  Clobbers: A, X, Y, kArg+3..kArg+7
//  NOTE: caller must hold SEI (called from sysExit/sysKill, already SEI'd).
//        Residency/refcount path: shared-code instances decrement the
//        refcount; the LAST instance frees the shared pages + record.
dynFree:
    sta dlFreeTaskId            // stash task id (dynFindByTaskId clobbers A)
    tax                          // X = task id for freeMallocBlocks
    jsr freeMallocBlocks         // free malloc blocks (static + dynamic)
    lda dlFreeTaskId             // reload (freeMallocBlocks clobbers A/X/Y)
    jsr dynFindByTaskId
    bcc dfGotSlot
    jmp dfNotFound              // (dfNotFound too far to branch)
dfGotSlot:
    sty kArg+7                  // dyn slot (all paths)
    // Release this task's declared libraries (free at refcount 0).
    // Swapped tasks already released them at eviction -- skip (no double-free).
    lda dynTaskFlags,y
    and #DYN_FLAG_SWAPPED
    bne dfNoLibs
    lda dynTaskLibMaskLo,y
    ora dynTaskLibMaskHi,y
    beq dfNoLibs
    jsr dlLibRelease            // Y = slot (clobbers A/X/Y)
    ldy kArg+7                  // restore slot
dfNoLibs:
    lda dynTaskResidency,y
    cmp #$FF
    beq dfSingle
    // shared-code instance -- decrement refcount, free code when last dies
    tax
    txa
    pha                         // resIdx on stack
    dec resRefCount,x
    bne dfSharedAlive
    lda resStartPage,x; sta kArg+6
    lda resPageCount,x; tay
    ldx kArg+6
    jsr freeFromPool            // X=startPage, Y=pages (single pool)
    pla
    tax
    lda #0; sta resNameLo,x     // tombstone: name ptr = 0 = free record
    sta resNameHi,x
    jmp dfClear
dfSharedAlive:
    pla
    jmp dfClear
dfSingle:
    lda dynTaskFlags,y
    and #DYN_FLAG_SWAPPED
    bne dfClear                 // swapped: code pages already freed -- skip
    lda dynTaskPageCount,y
    beq dfClear                 // no pages reserved -> just clear slot
    ldx dynTaskStartPage,y
    lda dynTaskPageCount,y
    tay
    jsr freeFromPool            // X=startPage, Y=pages (single pool)
    ldy kArg+7                  // restore slot

dfClear:
    ldy kArg+7
    lda #$FF; sta dynTaskId,y
    lda #0; sta dynTaskOriginLo,y
    sta dynTaskOriginHi,y
    sta dynTaskPageCount,y
    lda #$FF; sta dynTaskResidency,y
    dec dynTaskCount
    // free the program-RAM block if this slot had one (KF_MALLOC_RAM)
    lda dynTaskRamPages,y
    beq dfClearDone
    lda dynTaskRamStartPage,y
    cmp #$ff
    beq dfClearRamSwapped       // swapped: pages already freed
    tax
    lda dynTaskRamPages,y
    tay
    jsr freeFromPool            // X=startPage, Y=pages
dfClearRamSwapped:
    ldy kArg+7
    lda #0
    sta dynTaskRamPages,y
    sta dynTaskRamStartPage,y
dfClearDone:
    lda kArg; pha
    lda kArg+1; pha
    lda kArg+2; pha             // dlRestore clobbers kArg..+7 -- the dynFree
                                // contract keeps kArg..+2 (sysKill reads
                                // kArg afterwards for kill-self)
    jsr dlRestore               // Phase D: try to swap a task back in
    pla; sta kArg+2
    pla; sta kArg+1
    pla; sta kArg
    clc
    rts

dfNotFound:
    sec
    rts

bitMasks:
    .byte $01, $02, $04, $08, $10, $20, $40, $80

// ==========================================
//  Header Parser
// ==========================================

//  dphBadMagic sits BEFORE the entry label so the two magic compares can
//  branch back to it. Placed after the field block it would be out of range.
dphBadMagic:
    lda #1; sta kArg            // error 1: bad magic
    sec
    rts

// --- dynParseHeader -- parse task binary header ---
//  Input:  kPtr/kPtr+1 = header address (in scratchBuf)
//  Output: C=0: kArg..kArg+7 + dlFixCountLo/Hi filled, name at (dataPtr)+0 null-term'd
//          C=1: kArg = 1 (bad magic, or a fixup table not where the header says)
//  Clobbers: A, X, Y, srcPtr
dynParseHeader:
    ldy #0
    // Check magic "GO" -- the two halves of HDR_MAGIC, little-endian
    lda (kPtr),y; cmp #<HDR_MAGIC; bne dphBadMagic
    iny
    lda (kPtr),y; cmp #>HDR_MAGIC; bne dphBadMagic
    // flags at +2
    iny
    lda (kPtr),y; sta kArg+4
    sta dlFlags
    // origin at +3/+4 (LE)
    iny; lda (kPtr),y; sta kArg
    iny; lda (kPtr),y; sta kArg+1
    // code_size at +5/+6 (LE)
    iny; lda (kPtr),y; sta kArg+2
    iny; lda (kPtr),y; sta kArg+3
    // data_size at +7
    iny; lda (kPtr),y; sta kArg+5
    // ZP base at +8
    iny; lda (kPtr),y; sta kArg+6
    // ZP len at +9
    iny; lda (kPtr),y; sta kArg+7
    // +10/+11 -- libMask (16-bit LE)
    iny; lda (kPtr),y; sta dlLibMask
    iny; lda (kPtr),y; sta dlLibMaskHi
    iny                          // Y = 12
    // name_len at +12
    lda (kPtr),y
    sta dlFixCountLo            // temp store name_len
    tax                         // X = loop counter
    // srcPtr = kPtr + 13 (name start)
    lda kPtr; clc; adc #13; sta srcPtr
    lda kPtr+1; adc #0; sta srcPtr+1
    // Copy name to (dataPtr)+0, null-terminated.
    // Copies up to 12 chars + null (name_len capped at 12); the caller's
    // (dataPtr) buffer is >= 13 bytes (shell line buffer, ibuffs).
    ldy #0
    cpx #0; beq dphNameEnd
    cpx #12
    bcc dphNameCp               // name_len < 12 -> copy all chars
    ldx #12                     // cap long names
dphNameCp:
    lda (srcPtr),y
    sta (dataPtr),y
    iny
    dex
    bne dphNameCp
dphNameEnd:
    lda #0; sta (dataPtr),y     // null-terminate
    // the fixup trailer sits at header+13+name_len -- use the REAL name_len
    // (Y was capped for the copy)
    lda dlFixCountLo            // name_len
    tay
    lda (srcPtr),y              // fixup_count LO
    sta dlFixCountLo
    iny
    lda (srcPtr),y              // fixup_count HI
    sta dlFixCountHi
    iny
    lda (srcPtr),y              // fixup_offset LO -- must == code_size LO
    cmp kArg+2
    bne dphBadFixup
    iny
    lda (srcPtr),y              // fixup_offset HI -- must == code_size HI + 1
    sec
    sbc kArg+3
    cmp #1
    bne dphBadFixup
    clc
    rts
dphBadFixup:
    lda #1; sta kArg            // error 1: fixup table not at 256 + code_size
    sec
    rts

// ==========================================
//  Dynamic Task Loader -- kDynLoad
// ==========================================

// --- dynLoad -- load + spawn a task from REU FS ---
//  Input:  kPtr/kPtr+1 = filename (null-term screen codes)
//  Output: C=0 success (task spawned + dynTaskTable entry)
//          C=1 failure, kArg = error code (0=no slots, 1=bad magic,
//          2=unsupported binary (fixed-origin), 3=data region
//          already resident, 4=pool full, 5=not found,
//          8=origin not aligned)
//  Clobbers: A, X, Y, kArg block, srcPtr, kPtr, spawnPtr, taskNamePtr
//  Format: 256-byte paged header; the code body follows at file offset 256
//  and is loaded to a pool address (every binary is relocatable).
dynLoad:
    sei
    lda #0
    sta reentFirst              // H1: one-shot per load -- a stale flag from
                                // a failed load must not make the NEXT
                                // successful (possibly non-reentrant) load
                                // create a bogus residency record
    sta dlEvictedSelf           // Phase C: reset per load
    lda kArg+2; sta dlRamPages  // requested RAM pages (KF_MALLOC_RAM)
    // --- args snapshot: convention "name.tsk\0args\0" -- the args
    //  string lives after the filename's NUL in the caller's buffer.
    //  kPtr is clobbered by every later stage, so capture the pointer
    //  now (copy-out happens in dlCopyArgs after spawn).
    ldy #0
dlArgsScan:
    lda (kPtr),y
    beq dlArgsGot
    iny
    cpy #12
    bcc dlArgsScan
    lda #0                      // no NUL within 12 bytes -- treat as no args
    sta dlArgsLo
    sta dlArgsHi
    jmp dlArgsSnapDone
dlArgsGot:
    tya                         // dlArgs = kPtr + name_len + 1
    clc
    adc kPtr
    sta dlArgsLo
    lda kPtr+1
    adc #0
    sta dlArgsHi
    inc dlArgsLo
    bne dlArgsSnapDone
    inc dlArgsHi
dlArgsSnapDone:
    // Step 0: dyn slot capacity -- fail fast before any FS work.
    lda dynTaskCount
    cmp #MAX_DYN_TASKS
    bcc dlSlotOk
    jmp dlErr0
dlSlotOk:
    // Step 0: Find the file (FS offset + size) -- don't load it all.
    lda kPtr; sta kArg          // kArg = filename ptr
    lda kPtr+1; sta kArg+1
    jsr reuFindFile
    bcc dlFoundFile
    jmp dlErr5
dlFoundFile:
    sei                         // reuFindFile returns CLI'd -- keep load atomic
    // Step 0.5: DMA the 256-byte header page to scratchBuf
    lda #0; sta kArg+6
    lda #1; sta kArg+7
    lda #<$C400; sta kPtr
    lda #>$C400; sta kPtr+1
    jsr reuDmaRange
    sei                         // reuDmaRange returns CLI'd -- keep load atomic

    // Step 1: Parse header from scratchBuf
    lda #<$C400; sta kPtr
    lda #>$C400; sta kPtr+1
    jsr dynParseHeader
    bcc dlHdrOk
    jmp dlRts                    // kArg already set (1=bad magic, 2=bad ver)
dlHdrOk:
    // Reject zero-size binaries -- they'd spawn a task with no code
    // (and the pool allocator would mark a phantom page).
    lda kArg+2
    ora kArg+3
    bne dlSizeOk
    lda #1; sta kArg            // error 1: empty/corrupt binary
    jmp dlRts
dlSizeOk:
    lda dlLibMask
    sta dlRollMask              // stash the parsed mask -- dlLibRelease (via
    lda dlLibMaskHi             //   dlEvict inside dlLoadCore) clobbers
    sta dlRollMaskHi            //   dlLibMask/dlLibMaskHi, so rollback uses this
    // Assign the loader's cursor (dlFsSave) from the file we just found,
    // while the value is still intact. It is the master copy for the whole
    // load: dlLoadCore advances it and installs it into reuFsPtr before
    // each DMA (see dlInstallCursor), so nothing that happens in between --
    // including the lib-header peek in dlPreEvict -- can disturb the load. That
    // peek can re-find a lib and leave a stale bank byte behind, which would be
    // read as the task's fixup table.
    :FSCopy(reuFsPtr, dlFsSave)
    jsr dlLibRefcount           // count this task's declared libs
    // kArg = origin_lo, +1=origin_hi, +2=size_lo, +3=size_hi,
    // +4=flags, +5=data_size, +6=zp_base, +7=zp_len, dlFixCountLo/Hi=fixup_count
    lda kArg+5
    sta dlcDataSize             // data_size -- the reentrant path skips
                                // dlLoadCore (which also sets it on reloc loads)

    // zp_len -> scratch var (feeds the spawn syscall later)
    lda scratchBuf+HDR_ZP_LEN
    sta dlcZpLen

    // Copy name from (dataPtr) to dynNameBuf -- dataPtr is shell's
    // volatile line buffer; dynNameBuf provides persistent storage
    ldy #0
dlNcpy:
    lda (dataPtr),y; sta dynNameBuf,y
    beq dlNcdone                 // null terminator copied
    iny; cpy #11; bcc dlNcpy    // copy up to 11 chars
    lda #0; sta dynNameBuf+11   // truncate longer names with null
dlNcdone:

    // Branch: reentrant (name-keyed residency) -> relocatable -> fixed.
    // Reentrant comes FIRST so relocatable+reentrant binaries (the
    // shell) share one loaded copy by name instead of reloading.
    lda kArg+4
    and #KF_REENTRANT
    beq dlChkReloc
    lda kArg+5                  // section 3.3 guard: reentrant requires a data buffer --
    beq dlChkReloc              // reentrant + data_size=0 is a mis-flag (basic)
    jmp dlReent
dlChkReloc:
    // Non-reentrant (or mis-flagged reentrant) binary.
    // Re-runnable binaries (KF_RE_RUNNABLE) skip the single-instance dup
    // check -- each run loads a fresh pool copy instead of sharing.
    lda kArg+4
    and #KF_RE_RUNNABLE
    bne dlChkRelocGo
    // Otherwise refuse if a task with this name is already running;
    // reentrant binaries take dlReent above and share their copy instead.
    ldy #0
dlDupChk:
    cpy #MAX_DYN_TASKS
    bcs dlChkRelocGo
    lda dynTaskId,y
    cmp #$ff
    beq dlDupNext
    lda dynTaskNameLo,y; sta kPtr
    lda dynTaskNameHi,y; sta kPtr+1
    lda #<dynNameBuf; sta srcPtr
    lda #>dynNameBuf; sta srcPtr+1
    tya; pha                    // rnStrCmp clobbers Y
    jsr rnStrCmp
    tax
    pla; tay
    txa
    beq dlDupFound
dlDupNext:
    iny
    jmp dlDupChk
dlDupFound:
    jmp dlErr11                 // error 11: already running
dlChkRelocGo:
    lda kArg+4
    and #KF_RELOCATABLE
    bne dlGoReloc
    // fixed-origin (flags=0) binaries are not supported
    jmp dlErr2                  // error 2: unsupported (fixed-origin)
dlGoReloc:
    jmp dlReloc

// ==========================================
//  Reentrant path (name-keyed residency, section 4) -- shared code copy
// ==========================================
dlReent:
    // Identity first: an existing LIVE instance with this name must share its
    // code even when the table is full (error 10 only applies to a NEW record
    // -- rnNotFound). Identity is derived from the instance, never read from a
    // stored name pointer: see findLiveByName.
    lda #<dynNameBuf; sta kPtr
    lda #>dynNameBuf; sta kPtr+1
    jsr findLiveByName
    bcs rnNotFound
    sta reentResIdx             // A = the record index of that instance
    //  WARNING: AND IT MUST ALSO GO IN **Y**. `dlReentFound` indexes the record with
    //  Y (`lda resAddrLo,y` ...), but findLiveByName leaves Y = the INSTANCE's
    //  dyn slot, which equals the record index only by luck: it does for the
    //  boot shell (record 0 IS its dyn slot 0). Once they diverge -- kill the
    //  boot instance, load anything (it takes the freed dyn slot), then
    //  `run shell` -- the spawn reads a different, tombstoned record (entry
    //  $0000, size 0) and starts a task at address 0.
    tay
    jmp dlReentFound
rnNotFound:
    lda resCount
    cmp #MAX_RESIDENTS
    bcc rnFirst
    lda #10; sta kArg            // error 10: residency table full
    jmp dlRts
rnFirst:
    lda #1
    sta reentFirst              // first load -> create record after register
    lda kArg+4
    and #KF_RELOCATABLE
    beq rnFixed
    jmp dlReloc
rnFixed:
    jmp dlErr2                  // error 2: fixed-origin not supported

// --- rnStrCmp -- compare null-terminated strings at (kPtr)/(srcPtr) ---
//  Max 12 chars (11 + null -- dynNameBuf and per-slot name buffers).
//  Output: A = 0 equal, A = 1 different. Clobbers A, Y.
rnStrCmp:
    ldy #0
rnCmpLp:
    lda (kPtr),y
    cmp (srcPtr),y
    bne rnCmpDiff
    cmp #0
    beq rnCmpSame
    iny
    cpy #12
    bcc rnCmpLp
rnCmpSame:
    lda #0
    rts
rnCmpDiff:
    lda #1
    rts

// --- findLiveByName -- the reentrancy records' identity ---
//  Input:  kPtr = the name to look for (dynNameBuf for a load; a slot's own
//          name buffer for a restore).
//  Output: C=0, A = the residency index of a LIVE instance with that name;
//          C=1 = no live instance carries it (so the caller does a full load).
//  WARNING: On the C=0 path Y = that INSTANCE's dyn slot -- which is NOT the record
//  index any more (they coincide only while the record's origin still owns its
//  original slot). A caller that needs the record index must take it from A and
//  put it where its callee expects it: `dlReent` does `sta reentResIdx / tay`
//  because `dlReentFound` indexes the record with Y.
//  Clobbers: A, X, Y, srcPtr.
//
//  WHY IDENTITY IS DERIVED, NOT STORED: a record must not cache one instance's
//  name POINTER -- `resAddRecord` would be storing the very `kPtr` that
//  `dlCopyName` had just written into that instance's dyn slot. That buffer is
//  rewritten when the slot is re-used, which happens as soon as the FIRST
//  instance dies while a co-instance keeps the record alive, so the record's key
//  would start reading the new occupant's name: `run shell` matches nothing and
//  loads a second full copy (and with a second reentrant binary it could share
//  the WRONG entry).
//  The authoritative name is already per-instance: `dlRegister`'s `dlCopyName`
//  gives every instance its own buffer, and a slot's buffer can only be
//  rewritten once its dyn entry is free (`dynRegister` picks `dynTaskId == $ff`)
//  -- so "a slot holding a live, residency-tagged instance" means "its buffer is
//  that instance's current name", and no pointer can go stale.
//  The `dynTaskId == $ff` gate is also what makes a half-torn-down slot (a
//  failed registration that left its residency byte behind) invisible
//  here rather than a phantom instance.
findLiveByName:
    ldy #0
flbLoop:
    cpy #MAX_DYN_TASKS
    bcs flbNone
    lda dynTaskId,y
    cmp #$ff
    beq flbNext                 // free dyn slot
    lda dynTaskResidency,y
    cmp #$ff
    beq flbNext                 // detached (single instance) -- not a record
    lda dynTaskNameLo,y; sta srcPtr
    lda dynTaskNameHi,y; sta srcPtr+1
    tya                         // save the slot -- rnStrCmp clobbers Y
    pha
    jsr rnStrCmp                // (kPtr) vs this instance's own name
    tax
    pla; tay
    txa
    bne flbNext
    lda dynTaskResidency,y      // the record index comes from the INSTANCE
    clc
    rts
flbNext:
    iny
    bne flbLoop
flbNone:
    sec
    rts

// --- dlReentFound -- spawn another instance of an already-resident task ---
//  Entry + size from the record; fresh per-instance data buffer.
//  No overlap check, no DMA, no bitmap marking.
dlReentFound:                   // Y = resIdx (reentResIdx holds it)
    lda resAddrLo,y; sta spawnPtr
    lda resAddrHi,y; sta spawnPtr+1
    lda #<dynNameBuf; sta taskNamePtr
    lda #>dynNameBuf; sta taskNamePtr+1
    lda resAddrLo,y; pha        // entry_lo
    lda resAddrHi,y; pha        // entry_hi
    lda resSizeLo,y; pha        // size_lo
    lda resSizeHi,y; pha        // size_hi
    lda kArg+5; sta kArg+1      // data_size
    lda dlcZpLen; sta kArg+7    // zp_len (kArg+6 = zp_base from parse)
    lda #SYS_SPAWN; jsr syscall
    bcc rnSpawnOk
    lda kArg                    // spawn's failure code (0/1)
    pla; pla; pla; pla          // discard entry/size (balanced stack)
    jsr dlSpawnErr              // map to loader error 12/13
    jmp dlRts
rnSpawnOk:
    pla; sta kArg+3
    pla; sta kArg+2
    pla; sta kArg+1
    pla; sta kArg
    stx dlNewTask               // stash spawned slot for the ALIVE restore
    lda #TA_SLEEPING            // hold it SLEEPING -- see dlRegNoEager
    sta taskState,x
    lda scratchBuf+HDR_FLAGS; sta kArg+4  // flags (spawn clobbers kArg+4)
    lda #0; sta kArg+6
    sta kArg+7
    sta reentFirst              // defensive
    jsr dlRegister              // returns C=0, CLI'd
    bcc rnRegOk
    sec                         // register failed -- defensive path already
    rts                         // cleaned up and reported error 0
rnRegOk:
    sei                         // re-lock: residency tagging is atomic
    lda dlRegSlot
    tay
    lda reentResIdx
    sta dynTaskResidency,y
    tay
    lda resRefCount,y
    clc
    adc #1
    sta resRefCount,y           // this instance holds a reference too
    clc
    cli
    rts

// ==========================================
//  Relocatable path (13.2) -- Phase B: dlLoadCore + spawn tail
// ==========================================
dlReloc:
    // Origin must be page-aligned (delta_lo = 0 requirement)
    lda kArg
    beq rlAligned
    lda #8; sta kArg            // error 8: origin not aligned
    jmp dlRts
rlAligned:
    lda #0; sta dlNoSelfEvict   // task load -- self-evict of the caller is OK
    // KF_MALLOC_RAM with 0 requested pages -> error 9. Check BEFORE
    // dlPreEvict: making room for a load that can never succeed would
    // evict (and possibly self-evict) the caller -- a bare "run basic"
    // rebooted the system this way.
    lda dlFlags
    and #KF_MALLOC_RAM
    beq rlRamOk
    lda dlRamPages
    bne rlRamOk
    lda #9; sta kArg            // error 9: pages required
    jmp dlRts
rlRamOk:
    jsr dlPreEvict              // evict BEFORE the load so code + RAM +
                                //   eager libs fit in one pass (C=0 ok,
                                //   error 4 routes to dlRts internally).
                                //   The lib scan inside clobbers reuFsPtr --
                                //   harmless: dlLoadCore installs the cursor
                                //   from dlFsSave before each DMA it issues.
    jsr dlLoadCore              // load code -> kPtr=load_addr, dlStartPage/dlPages
    bcc rlLoaded
    jsr dlLibUnref              // roll back the refcount (dlLibRefcount ran at dlSizeOk)
    lda dlEvictedSelf           // self-evicted during the load? then the
    bne rlSelfEvicted           //   caller's code is freed -- never return to it
    jmp dlRts                   // C=1 -> kArg = error (1/3/4)
rlLoaded:
    sei                         // dlLoadCore returns SEI'd -- re-assert (harmless)
    // --- spawn via syscall 1 ---
    lda kPtr; sta spawnPtr       // entry = load_addr
    lda kPtr+1; sta spawnPtr+1
    lda #<dynNameBuf; sta taskNamePtr
    lda #>dynNameBuf; sta taskNamePtr+1
    lda dlcDataSize; sta kArg+1  // data_size
    lda dlcZpBase; sta kArg+6    // zp_base
    lda dlcZpLen; sta kArg+7     // zp_len
    lda #SYS_SPAWN; jsr syscall          // spawn -- X = spawned slot (preserved; SEI'd)
    bcc rlSpawnOk
    lda kArg; pha               // save spawn's failure code (freeFromPool
                                // may clobber kArg)
    // spawn failed -- free code + region pages (no leak)
    lda dlPages
    beq rlSpFailReg
    ldx dlStartPage
    tay
    jsr freeFromPool
rlSpFailReg:
    lda dlRegPages
    beq rlSpFailErr
    ldx dlRegStartPage
    tay
    jsr freeFromPool
rlSpFailErr:
    jsr dlLibUnref              // roll back the refcount (dlLibRefcount ran at dlSizeOk)
    pla
    jsr dlSpawnErr              // kArg = 12 (heap full) / 13 (no task slot)
    lda dlEvictedSelf
    bne rlSelfEvicted
    jmp dlRts
rlSelfEvicted:
    jmp dlSelfEvictedExit       // caller's code freed -- restore + switch away
rlSpawnOk:                      // spawn returned SEI'd -- the new task is
                                //   unschedulable until dlRegister completes
    stx dlNewTask               // stash spawned slot for the ALIVE restore
    lda #TA_SLEEPING            // hold it SLEEPING so dlEvict (during the
    sta taskState,x             //   eager lib load) can never evict THIS task
    // dlRegister inputs: kArg=entry, +2/3=size, +4=flags, +6=startPage, +7=pages
    lda dlcLoadLo; sta kArg      // entry lo (spawn clobbered kPtr)
    lda dlcLoadHi; sta kArg+1    // entry hi
    lda dlcSizeLo; sta kArg+2
    lda dlcSizeHi; sta kArg+3
    lda dlFlags; sta kArg+4      // flags (allocFromPool clobbered kArg+4;
                                 // the fixup chunk DMAs overwrote scratchBuf)
    lda dlStartPage; sta kArg+6
    lda dlPages; sta kArg+7
    jmp dlRegister

// ==========================================
//  Load-cursor helpers -- they live in cursor-helpers.inc (loader band, $5457)
//  so they share the font-help/pool-top .pc chain. The rules they implement:
//  the load cursor lives in dlFsSave, reuFsPtr is only the argument reuDmaRange
//  reads, and it is SHARED (filesys.lib/gfx.lib use it as working scratch) -- so
//  the loader installs its cursor before every DMA, a nested find inside
//  dlPreEvict cannot disturb a load, and no caller saves/restores reuFsPtr.
//  See cursor-helpers.inc for the full note.

// ==========================================
//  dlLoadCore -- load a parsed binary's code into the pool (no spawn)
// ==========================================
//  Input:  kArg = origin_lo, +1=origin_hi, +2=size_lo, +3=size_hi,
//          +5=data_size, +6=zp_base; dlcZpLen, dlFixCountLo/Hi,
//          dlFlags set; dlFsSave = this file's FS offset (its start).
//  Output: C=0 -> kPtr = load_addr, dlStartPage, dlPages.
//          C=1 -> kArg = error (1=corrupt fixup, 4=full).
//  Returns SEI'd on all paths (caller CLI's before returning to the task).
//  Clobbers: A, X, Y, kArg..+7, srcPtr.
//  CONTRACT: the load cursor lives in dlFsSave -- set by the caller
//  (dlFindMaster on the restore/lib paths, dlSizeOk on a task load). This
//  routine advances it and installs it into reuFsPtr right before each DMA
//  (dlInstallCursor), because reuFsPtr is SHARED with filesys.lib/gfx.lib
//  and anything nested inside a load may clobber it -- including the
//  lib-header peek in dlPreEvict.
//  Never rely on reuFsPtr surviving between two DMAs.
dlLoadCore:
    // Preserve params that clobbering calls destroy
    lda kArg+2; sta dlcSizeLo
    lda kArg+3; sta dlcSizeHi
    lda kArg+5; sta dlcDataSize
    lda kArg+6; sta dlcZpBase
    lda kArg+1; sta dlcOriginHi
    lda #0; sta dlRegPages      // reset (non-KF_MALLOC_RAM -> no region)

    // code page count = ceil(size / pageSize) -- also the RAM region base
    lda dlcSizeLo
    clc
    adc #<POOL_PAGE_MASK
    sta kArg+4
    lda dlcSizeHi
    adc #>POOL_PAGE_MASK
    :poolLsr()
    sta dlPages

    // --- program-RAM reservation (KF_MALLOC_RAM) ---
    lda dlFlags
    and #KF_MALLOC_RAM
    beq dlcNoReg
    lda dlRamPages
    bne dlcRegHave
    lda #9; sta kArg            // error 9: pages required
    sec
    rts
dlcRegHave:
    sta dlRegPages
    cmp #MAX_MALLOC_PAGES+1
    bcc dlcRegFit
    lda #4; sta kArg            // error 4: exceeds REU-shadow cap
    sec
    rts
dlcRegFit:
    clc
    adc dlPages                 // code + RAM total pages
    cmp #POOL_PAGES+1
    bcc dlcNoReg
    lda #4; sta kArg            // error 4: pool full
    sec
    rts
dlcNoReg:
    ldx dlPages
    jsr dlAllocCode             // code block -- A = startPage, C=1 if full
    bcc dlcCodeOk               //   (lib -> highest fit, task -> lowest)
    // pool full -- try evicting resident tasks to make room (Option 5)
dlcEvictLoop:
    jsr dlEvict                 // C=0 = evicted a victim
    bcc dlcEvictRetry
    jmp dlcEr4                  // no eligible victim -> error 4
dlcEvictRetry:
    ldx dlPages
    jsr dlAllocCode             // code block -- same placement as the first try
    bcc dlcCodeOk
    jmp dlcEvictLoop            // still full -- evict another
dlcEr4:
    lda #4; sta kArg            // nothing allocated here -- the RAM-fail path
    sec                         //   frees the code before re-entering the loop
    rts
dlcCodeOk:
    sta dlStartPage             // code start page
    // program-RAM is a SEPARATE block -- a resident lib (e.g. fp.lib) in
    // the middle of the pool must not strand it from the code. One
    // contiguous 50-page block could never fit with fp.lib capping the
    // largest free run at 49 -> every task got evicted -> warm reboot.
    lda dlRegPages
    beq dlcRamDone              // no RAM region -> code only
    ldx dlRegPages
    jsr allocFromPool           // RAM block -- A = startPage, C=1 if full
    bcc dlcRamOk
    // RAM didn't fit -- free the code, evict another victim, retry both
    ldx dlStartPage
    ldy dlPages
    jsr freeFromPool
    jmp dlcEvictLoop
dlcRamOk:
    sta dlRegStartPage
dlcRamDone:
    lda dlStartPage
    // load_addr = POOL_BASE + startPage*pageSize
    :poolAsl()
    clc
    adc #>POOL_BASE
    sta kPtr+1
    lda #0
    sta kPtr
    sta dlcLoadLo
    lda kPtr+1
    sta dlcLoadHi
    // delta_hi = load_hi - (origin_hi + 1)
    sec
    lda kPtr+1
    sbc dlcOriginHi
    sec
    sbc #1
    sta dlDelta

    // Advance the load cursor past the header page, install it, DMA the
    // code -> load_addr.
    :FSAddPage(dlFsSave)
    lda dlcSizeLo; sta kArg+6
    lda dlcSizeHi; sta kArg+7
    jsr dlInstallCursor
    jsr reuDmaRange             // kPtr = load_addr (dest preserved)
    sei                         // reuDmaRange returns CLI'd -- keep load atomic

    // Fixup application -- the table follows the code body, so it is DMA'd in
    // chunks of 128 fixups (256 bytes) and the count is not bounded by the
    // header page.
    :FSAdd16(dlFsSave, dlFsSave, dlcSizeLo, dlcSizeHi)   // += code size
                                // dlFsSave = fixup table offset (256+code_size)
dlcFixLoop:
    lda dlFixCountLo
    ora dlFixCountHi
    bne dlcFixGo
    jmp dlcFxDone               // all fixups applied
dlcFixGo:
    // chunk = min(remaining, 128) fixups
    lda dlFixCountHi
    bne dlcFixFull              // > 255 remaining -> 128
    lda dlFixCountLo
    cmp #128
    bcc dlcFixPartial
dlcFixFull:
    lda #128
    sta dlChunkCnt
    lda #0; sta kArg+6
    lda #1; sta kArg+7
    jmp dlcFixDma
dlcFixPartial:
    sta dlChunkCnt              // chunk = remaining (1..127)
    asl                         // x2 = byte length (no carry: < 128)
    sta kArg+6
    lda #0; sta kArg+7
dlcFixDma:
    lda #<$C400; sta kPtr
    lda #>$C400; sta kPtr+1
    jsr dlInstallCursor         // reuFsPtr = dlFsSave (see the helpers)
    jsr reuDmaRange             // fixup chunk -> $C400
    sei                         // reuDmaRange returns CLI'd -- stay atomic
    // advance the load cursor by the chunk byte length (kArg+6/+7 survive)
    :FSAdd16(dlFsSave, dlFsSave, kArg+6, kArg+7)
    // reload kPtr = load_addr from scratch (chunk DMA clobbered it)
    lda dlcLoadLo; sta kPtr
    lda dlcLoadHi; sta kPtr+1
    lda #<$C400; sta srcPtr
    lda #>$C400; sta srcPtr+1   // srcPtr = current chunk
dlcFixApply:
    lda dlChunkCnt
    beq dlcFixLoop              // chunk exhausted -> next chunk
    dec dlChunkCnt
    ldy #0
    lda (srcPtr),y; sta kArg+2  // off_lo
    iny
    lda (srcPtr),y; sta kArg+3  // off_hi
    // bounds: offset < code size (dlcSizeHi/Lo)
    lda kArg+3
    cmp dlcSizeHi
    bcc dlcFixOk
    bne dlcFixBad
    lda kArg+2
    cmp dlcSizeLo
    bcc dlcFixOk
dlcFixBad:
    jmp dlcFxBad
dlcFixOk:
    lda srcPtr
    clc
    adc #2
    sta srcPtr
    bcc dlcFixAdd
    inc srcPtr+1
dlcFixAdd:
    lda kPtr+1                  // code addr = kPtr + offset
    clc
    adc kArg+3
    sta kPtr+1
    ldy kArg+2
    lda (kPtr),y
    clc
    adc dlDelta                 // + delta_hi
    sta (kPtr),y
    lda kPtr+1                  // restore kPtr+1
    sec
    sbc kArg+3
    sta kPtr+1
    // decrement the 16-bit remaining count
    lda dlFixCountLo
    bne dlcFixDecLo
    dec dlFixCountHi
dlcFixDecLo:
    dec dlFixCountLo
    jmp dlcFixApply

dlcFxDone:
    lda dlcLoadLo; sta kPtr     // kPtr = load_addr (return value)
    lda dlcLoadHi; sta kPtr+1
    clc
    rts

// --- corrupt fixup: free code + region pages, error 1 ---
dlcFxBad:
    lda dlPages
    beq dlcFxBadReg
    ldx dlStartPage
    tay
    jsr freeFromPool
dlcFxBadReg:
    lda dlRegPages
    beq dlcFxBadErr
    ldx dlRegStartPage
    tay
    jsr freeFromPool
dlcFxBadErr:
    lda #1; sta kArg
    sec
    rts

// ==========================================
//  dlEvict -- swap out one resident dynamic task to free pool pages
// ==========================================
//  Policy: largest pageCount first (most pages freed per eviction).
//  Skips free slots, already-swapped, DEAD/SLEEPING tasks. A shared
//  reentrant record (any refCount) is evictable: EVERY instance linked
//  to it is swapped together, so no instance is left running freed code.
//  Frees the victim's code pages (and data region if it owns one),
//  tombstones the residency record + detaches all its instances. Each
//  instance is individually marked DYN_FLAG_SWAPPED + TA_SWAPPED, or
//  killed (dlEvSwapOrKill) if IT has a live thread child of its own --
//  a shared record's co-instances can each have spawned their own
//  threads, so this is checked per instance, not just for the one the
//  scan happened to pick. dlEvictedSelf is set if the calling task ends
//  up one of the swapped or killed instances (so the caller reschedules
//  instead of returning to freed code).
//  Output: C=0 = evicted; C=1 = no eligible victim.
//  Clobbers: A, X, Y, kArg..+7. Runs SEI'd (from dlLoadCore's alloc-fail).
dlEvict:
    lda #0
    sta dlEvictBest
    lda #$FF
    sta dlEvictSlot
    ldy #0
dlEvScan:
    cpy #MAX_DYN_TASKS
    bcs dlEvPick
    lda dynTaskId,y
    cmp #$FF
    beq dlEvNext                 // free slot
    lda dynTaskFlags,y
    and #(DYN_FLAG_SWAPPED | KF_NOT_EVICTABLE)
    bne dlEvNext                 // swapped or pinned -- skip
    // eligibility: ALIVE or PAUSED only (not DEAD/SLEEPING/SWAPPED)
    lda dynTaskId,y
    tax
    lda taskState,x
    cmp #TA_ALIVE
    beq dlEvElig
    cmp #TA_PAUSED
    beq dlEvElig
    jmp dlEvNext
dlEvElig:
    // a lib/restore load must never evict the calling task -- its code is
    // executing and would be freed mid-call (dir-from-basic crash).
    lda dlNoSelfEvict
    beq dlEvEligGo
    lda dynTaskId,y
    cmp curTask
    beq dlEvNext                // skip the caller
    // pageCount: shared reentrant -> resPageCount. Multi-instance shared
    // code is evictable -- dlEvDo swaps EVERY instance linked to it.
dlEvEligGo:
    lda dynTaskResidency,y
    cmp #$FF
    beq dlEvSingle
    tax
    lda resPageCount,x
    jmp dlEvCmp
dlEvSingle:
    lda dynTaskPageCount,y
dlEvCmp:
    beq dlEvNext                 // zero pages -> nothing to free, skip
    cmp dlEvictBest
    bcc dlEvNext
    beq dlEvNext                 // tie -> keep first (lower slot)
    sta dlEvictBest
    sty dlEvictSlot
dlEvNext:
    iny
    jmp dlEvScan
dlEvPick:
    lda dlEvictSlot
    cmp #$FF
    bne dlEvDo
    sec
    rts                          // no victim
dlEvDo:
    lda dlEvictSlot
    tay                          // Y = victim dyn slot
    lda dynTaskResidency,y
    cmp #$FF
    beq dlEvFreeSingle
    // shared reentrant -- free the shared pages once, then swap or kill
    // EVERY instance linked to this record (a co-instance left running
    // freed code, or with an unwatched thread child, is not an option)
    tax                          // X = resIdx
    lda resStartPage,x
    sta kArg+6
    lda resPageCount,x
    tay
    ldx kArg+6
    jsr freeFromPool            // free the shared code pages
    ldy dlEvictSlot
    lda dynTaskResidency,y
    sta dlEvictBest             // stash resIdx -- kArg+6 is clobbered by
    tax                         //   dlSwapMalloc/dlEvLibs (freeFromPool)
    lda #0
    sta resNameLo,x             // tombstone the record
    sta resNameHi,x
    ldy #0
dlEvMarkShared:
    cpy #MAX_DYN_TASKS
    bcs dlEvSharedEnd
    lda dynTaskResidency,y
    cmp #$FF
    beq dlEvSharedNext
    cmp dlEvictBest             // == resIdx
    bne dlEvSharedNext
    tya; pha                    // save Y (dlSwapMalloc clobbers it)
    jsr dlSwapMalloc            // preserve THIS instance's RAM + malloc
    pla; tay                    // restore Y
    tya; pha                    // save slot (dlEvLibs clobbers Y)
    jsr dlEvLibs                // release this instance's libraries
    pla; tay                    // restore slot
    sty dlEvictSlot              // this instance's slot -- dlEvSwapOrKill's input
    lda dlEvictBest
    pha                          // save resIdx (dlEvictBest reused below)
    lda dynTaskId,y
    sta dlEvictBest              // = this instance's task id
    tya                          // save the loop's slot cursor: dlEvSwapOrKill
    pha                          //   clobbers Y (dlEvKill's cascade, kill path)
    jsr dlEvSwapOrKill
    pla; tay                     // pla/tay leave C alone, so the bcs sees the call
    bcs dlEvShNoSelf             // killed -- dlEvMaybeSelf already ran for it
    jsr dlEvMaybeSelf
dlEvShNoSelf:
    pla
    sta dlEvictBest              // restore resIdx for the loop's own comparison
dlEvSharedNext:
    iny
    jmp dlEvMarkShared
dlEvSharedEnd:
    clc
    rts
dlEvFreeSingle:
    lda dynTaskPageCount,y
    beq dlEvMark
    ldx dynTaskStartPage,y
    lda dynTaskPageCount,y
    tay
    jsr freeFromPool
dlEvMark:
    ldy dlEvictSlot
    jsr dlEvLibs                // release the victim's libraries (single path)
    ldy dlEvictSlot             // reload slot (dlEvLibs clobbered Y)
    jsr dlSwapMalloc            // preserve program RAM + malloc blocks to REU
    ldy dlEvictSlot
    lda dynTaskId,y
    tax                          // X = victim GordonOS task id
    stx dlEvictBest              // victim id -- dlEvSwapOrKill's input
    jsr dlEvSwapOrKill           // swap it, or kill it + its live thread kids
    bcs dlEvDone                 // killed -- dlEvMaybeSelf already ran for it
    jsr dlEvMaybeSelf
dlEvDone:
    clc
    rts

// --- dlEvMaybeSelf -- kernel.asm, core growth headroom ($13C0) ---
//  Its 10B left this band so the shared-instance loop could save the Y slot
//  cursor across dlEvSwapOrKill (4B), whose kill path clobbers it via
//  dlEvKill's cascade. Called by dlEvMark (single) and dlEvMarkShared.

// --- dlEvSwapOrKill -- mark a task SWAPPED, or kill it if it has a live
//  thread child (its code page can't be freed out from under one) ---
//  A task with live thread children can't be swapped -- the children
//  share its code page, so swap-out would leave them running freed
//  code.
//  Input: dlEvictBest = task id, dlEvictSlot = dyn slot. Output: C=0 =
//  swapped; C=1 = killed (dlEvKill's cascade already ran). Clobbers A, X, Y.
dlEvSwapOrKill:
    ldy #0
dlEvSokScan:
    cpy totalTasks
    bcs dlEvSokSwap              // no live thread kid found -- swap it
    lda taskParent,y
    cmp dlEvictBest
    bne dlEvSokNext
    lda taskState,y
    beq dlEvSokNext              // stale parent link on a dead slot
    tya                          // A = child task id (== slot index)
    pha                          // save it (dlEvIsDynTask clobbers Y)
    jsr dlEvIsDynTask            // C=1 if child has its own dyn entry
    pla; tay                     // restore Y
    bcs dlEvSokNext              // dynamic-task child (own pages) -- skip
    jsr dlEvKill                 // live thread kid -- kill instead of swap
    jsr dlEvMaybeSelf            // the caller's code is gone either way: the
    sec                          //   loader must not rts into it (dlEvictedSelf)
    rts
dlEvSokNext:
    iny
    bne dlEvSokScan
dlEvSokSwap:
    ldy dlEvictSlot
    lda dynTaskFlags,y
    ora #DYN_FLAG_SWAPPED
    sta dynTaskFlags,y
    lda #$FF
    sta dynTaskResidency,y      // detach -- dynFree treats as single (no-op if already)
    ldx dlEvictBest
    lda #TA_SWAPPED
    sta taskState,x
    clc
    rts

// --- dlEvIsDynTask -- C=1 if task A has a dyn-table entry ---
//  Thread children (threadSpawn) share the parent's code page and have
//  NO entry; dynamic tasks (kDynLoad) do. Clobbers A, X, Y, dlFreeTaskId.
dlEvIsDynTask:
    sta dlFreeTaskId             // stash task id (scratch -- free here)
    ldy #0
dlEvIsDynScan:
    cpy #MAX_DYN_TASKS
    bcs dlEvIsDynNo
    lda dynTaskId,y
    cmp #$ff
    beq dlEvIsDynNext
    cmp dlFreeTaskId
    beq dlEvIsDynYes
dlEvIsDynNext:
    iny
    bne dlEvIsDynScan
dlEvIsDynNo:
    clc
    rts
dlEvIsDynYes:
    sec
    rts

// --- dlEvKill -- kill a dynamic task that has live children ---
//  Pages were already freed by the caller. Frees the victim's surface,
//  marks it DEAD, cascades to its descendants, then removes its
//  dyn-table entry. Returns C=0 (space was made).
//  Clobbers: A, X, Y, kArg..kArg+3. Uses dlEvictBest (victim id) and
//  dlFreeTaskId (cascade changed flag).
dlEvKill:
    ldx dlEvictBest
    jsr dlEvKillTask             // kill victim
dlEvKillPass:
    lda #0
    sta dlFreeTaskId             // changed = 0
    ldy #0
dlEvKillScan:
    cpy totalTasks
    bcs dlEvKillChk
    lda taskState,y
    beq dlEvKillNext             // already dead
    lda taskParent,y
    cmp #$ff
    beq dlEvKillNext             // orphan -- leave running
    cmp dlEvictBest
    beq dlEvKillDoChild          // direct child of victim
    tax                          // X = parent slot
    lda taskParent,x
    cmp #$fe
    bne dlEvKillNext             // parent not a cascade victim
dlEvKillDoChild:
    //  WARNING: THREAD CHILDREN ONLY. A `kDynLoad` child owns its own code pages AND
    //  its own dyn-table entry, so killing it here would leak both:
    //  `dlEvKillTask` frees no pages and removes no entry, leaving marked pages
    //  and a phantom `dynTaskId` -- which `pool` shows, which can make a later
    //  `kill <id>` land on a recycled slot, and which consumes the 8 dyn slots
    //  until `run` fails with "no free dyn slot". Nothing is lost by leaving it
    //  running: only a THREAD child shares the victim's pages (that is the whole
    //  reason this path exists), and surviving a parent is exactly what `exit`
    //  already does -- and what `kill`'s own cascade documents (thread
    //  descendants only). Same test `kGetTaskName` uses to spot a thread child
    //  (its stored REU hi byte is `$C0` -- REU_THREAD_NAME = $C000 -- so "$00"
    //  really does mean "not a thread child"). The survivor keeps its link to
    //  the dead victim's slot until that slot is reused, at which point
    //  `scOrphan` clears it -- the same stale link an `exit` orphan carries.
    lda taskThreadNameReuHi,y
    beq dlEvKillSkipChild       // $00 -> kDynLoad child -- leave it running
    tya; pha                     // save Y (dlEvKillTask clobbers it)
    tya; tax                     // X = child slot
    jsr dlEvKillTask
    pla; tay
    lda #$fe
    sta taskParent,y             // sentinel: cascade victim
    inc dlFreeTaskId
dlEvKillSkipChild:
dlEvKillNext:
    iny
    bne dlEvKillScan
dlEvKillChk:
    lda dlFreeTaskId
    bne dlEvKillPass
    // restore $FE sentinels -> $FF (orphans)
    ldy #0
dlEvKillClean:
    cpy totalTasks
    bcs dlEvKillRemove
    lda taskParent,y
    cmp #$fe
    bne dlEvKillCleanNext
    lda #$ff
    sta taskParent,y
dlEvKillCleanNext:
    iny
    bne dlEvKillClean
dlEvKillRemove:
    ldy dlEvictSlot
    lda #$ff
    sta dynTaskId,y
    lda #0
    sta dynTaskOriginLo,y
    sta dynTaskOriginHi,y
    sta dynTaskPageCount,y
    lda #$ff
    sta dynTaskResidency,y
    dec dynTaskCount
    clc
    rts

// --- dlEvKillTask -- kill task X (free owned surface, mark DEAD) ---
//  Input: X = task id. Clobbers A, X, Y, kArg..kArg+3.
dlEvKillTask:
    lda screenAlloc,x
    beq dktDead
    lda screenOwner,x
    cmp #$ff
    bne dktDead                  // sharer -- owner keeps the slot
    txa; pha                     // save X (surfFreeSlot clobbers it)
    jsr surfFreeSlot
    pla; tax
dktDead:
    lda #TA_DEAD
    sta taskState,x
    sta screenAlloc,x
    sta screenOwner,x
    sta taskThreadIdx,x
    sta taskThreadNameReuHi,x
    rts

// ==========================================
//  dlRestore -- swap-in swapped tasks (Phase D)
// ==========================================
//  Called from dynFree after a task's pages are freed. Loops over all
//  DYN_FLAG_SWAPPED tasks and re-loads every one whose code now fits,
//  cold-restarting its existing TCB (no re-spawn: task id/parent/screen/
//  name survive; scrollback is lost -- accepted v1 cost). Returns when no
//  further swapped task fits.
//  Runs SEI'd (dynFree's caller holds SEI). Clobbers A, X, Y, kArg..+7,
//  kPtr, srcPtr, dataPtr.
dlRestore:
    ldy #0
dlrScan:
    cpy #MAX_DYN_TASKS
    bcc dlrScanChk
    jmp dlrNone
dlrScanChk:
    lda dynTaskId,y
    cmp #$FF
    bne dlrChkSwap
    jmp dlrNext
dlrChkSwap:
    lda dynTaskFlags,y
    and #DYN_FLAG_SWAPPED
    bne dlrCandidate
    jmp dlrNext
dlrCandidate:
    sty dlRegSlot               // dyn slot (resAddRecord + table update)
    lda #0
    sta dlrAttached             // reset attach marker (set by dlrAttach)
    // Reentrant fast path: if a LIVE instance with this name exists -- a
    // co-instance restored first, a fresh instance spawned while this one sat
    // swapped, or this slot's own tag still pointing at a live record -- attach
    // to its record instead of reloading the code. Identity comes from the
    // instance, not from a stored name pointer:
    // findLiveByName.
    lda dynTaskFlags,y
    and #KF_REENTRANT
    beq dlrAttNone
    lda dynTaskNameLo,y; sta kPtr
    lda dynTaskNameHi,y; sta kPtr+1
    jsr findLiveByName
    bcs dlrAttNone              // no live instance -> full reload
    tax                         // X = resIdx -- dlrAttach's input
    jmp dlrAttach
dlrAttNone:
    jmp dlrNameSetup            // continue with the full reload path
dlrAttach:
    // Attach this swapped instance to the existing record -- the shared
    // code is already resident. Copy the record's entry/pages into the
    // reload scratch vars, mark attached (so the record is not
    // recreated) and run dlrLoaded's shared restart tail.
    ldy dlRegSlot
    txa
    sta dynTaskResidency,y
    lda resRefCount,x
    clc
    adc #1
    sta resRefCount,x
    lda resAddrLo,x; sta dlcLoadLo
    lda resAddrHi,x; sta dlcLoadHi
    lda resStartPage,x; sta dlStartPage
    lda resPageCount,x; sta dlPages
    lda #1
    sta dlrAttached
    jmp dlrLoaded
    // The dyn-table name is the binary's header name ("shell" -- no
    // extension), but reuFindFile needs the FS name ("shell.tsk").
    // Copy the name into dynNameBuf and append ".tsk".
    // NOTE: dlrAttNone jumps HERE -- srcPtr must be set from the slot's
    // name buffer (Y = dlRegSlot) before the copy loop.
dlrNameSetup:
    ldy dlRegSlot               // reload slot -- rnStrCmp may have clobbered Y
    lda dynTaskNameLo,y; sta srcPtr
    lda dynTaskNameHi,y; sta srcPtr+1
    ldy #0
dlrNameCp:
    lda (srcPtr),y
    sta dynNameBuf,y
    beq dlrNameExt
    iny
    cpy #7                      // the ".tsk" suffix below is 5 bytes and dynNameBuf
    bcc dlrNameCp               //   is 12: 7 copied + 4 + null = 12 exactly. A longer
dlrNameExt:                     //   name is truncated here, which makes the restore
                                //   lookup fail visibly instead of running the suffix
                                //   past the buffer into reentFirst/reentResIdx/
                                //   dlRegSlot. Sizing the destination for
                                //   a full 11-char name needs 16 bytes and a relocation.
    lda #$2E; sta dynNameBuf,y   // '.'
    iny
    lda #$14; sta dynNameBuf,y   // 't'
    iny
    lda #$13; sta dynNameBuf,y   // 's'
    iny
    lda #$0B; sta dynNameBuf,y   // 'k'
    iny
    lda #0; sta dynNameBuf,y     // null
    // kArg = "name.tsk" for reuFindFile; dataPtr = slot's own name
    // buffer (the header parser writes the name to (dataPtr) -- the
    // dying task's dataPtr may be 0, so point it at the per-slot buffer)
    lda #<dynNameBuf; sta kArg
    lda #>dynNameBuf; sta kArg+1
    ldy dlRegSlot
    lda dynTaskNameLo,y; sta dataPtr
    lda dynTaskNameHi,y; sta dataPtr+1
    jsr dlFindMaster            // find + make it the loader's cursor
    bcc dlrFound
    sei                         // not-found path returns CLI'd -- stay atomic
    ldy dlRegSlot               // reuFindFile clobbered Y -- reload scan slot
    jmp dlrNext
dlrFound:
    sei                         // reuFindFile returns CLI'd
    lda #0; sta kArg+6
    lda #1; sta kArg+7
    lda #<$C400; sta kPtr
    lda #>$C400; sta kPtr+1
    jsr reuDmaRange
    sei
    lda #<$C400; sta kPtr
    lda #>scratchBuf; sta kPtr+1
    jsr dynParseHeader
    bcc dlrHdrOk
    ldy dlRegSlot               // dynParseHeader clobbered Y -- reload scan slot
    jmp dlrNext
dlrHdrOk:
    lda scratchBuf+HDR_ZP_LEN; sta dlcZpLen  // spawn-independent; set for safety
    lda #0; sta dlEvictedSelf   // restore is not self-eviction
    lda #1; sta dlNoSelfEvict   // restore must not evict the calling task
    ldy dlRegSlot
    lda dynTaskRamPages,y
    sta dlRamPages              // restore the swapped program-RAM size
    jsr dlLoadCore              // kPtr=load_addr, dlStartPage/dlPages set
    bcc dlrLoaded
    ldy dlRegSlot               // reload the scan slot (dlLoadCore clobbered Y)
    jmp dlrNext                 // still doesn't fit -- leave swapped
dlrLoaded:
    // dlReLibs -> dlLibEagerLoad -> libLoad -> dlLoadCore clobbers the
    // reload scratch (dlcLoadLo/Hi, dlStartPage, dlPages). Stash them on
    // the stack so the task's entry/pages survive the lib reload on BOTH
    // the success and failure paths (lda/pha/pla/sta preserve C, so the
    // branch below still sees dlReLibs's carry). dlcSizeLo/Hi are NOT
    // stashed -- resAddRecord reads the size from the dyn table instead.
    lda dlcLoadLo; pha
    lda dlcLoadHi; pha
    lda dlStartPage; pha
    lda dlPages; pha
    ldy dlRegSlot
    jsr dlReLibs                // re-acquire libraries; C=1 = eager load failed
    pla; sta dlPages
    pla; sta dlStartPage
    pla; sta dlcLoadHi
    pla; sta dlcLoadLo
    bcc dlrLibOk
    jmp dlrLibFail
dlrLibOk:
    ldy dlRegSlot
    lda dynTaskId,y
    tax                         // X = task id (initTCB_ptr preserves X)
    // update code address + restart the stack frame (cold swap)
    lda dlcLoadLo; sta taskLo,x
    lda dlcLoadHi; sta taskHi,x
    lda dlcLoadLo; sta spawnPtr
    lda dlcLoadHi; sta spawnPtr+1
    jsr initTCB_ptr             // resets savedSP/frame/priority (X preserved)
    // cold restart: reset instance data (stale line buffer / startup
    // args must not re-fire -- the shell's "? unknown" after restore)
    lda taskDataSize,x
    beq dlrNoZero
    sta dlArgsCap               // loop limit (cpy has no abs,X mode)
    lda taskDataPtrLo,x; sta dataPtr
    lda taskDataPtrHi,x; sta dataPtr+1
    ldy #0
    lda #0
dlrZeroLp:
    sta (dataPtr),y
    iny
    cpy dlArgsCap
    bcc dlrZeroLp
dlrNoZero:
    // update dyn table: pages + flags + state
    ldy dlRegSlot
    lda dlStartPage; sta dynTaskStartPage,y
    lda dlPages; sta dynTaskPageCount,y
    lda dynTaskFlags,y
    and #$7F                    // clear DYN_FLAG_SWAPPED ($80)
    sta dynTaskFlags,y
    lda dynTaskId,y
    tax
    lda #TA_ALIVE
    sta taskState,x
    jsr dlRestoreMalloc         // DMA program RAM + malloc blocks back
    ldy dlRegSlot
    // recreate the residency record for reentrant binaries (tombstoned
    // on swap-out); dlcSizeLo/Hi hold code size, dlcLoadLo/Hi the entry
    lda dynTaskFlags,y
    and #KF_REENTRANT
    beq dlrRestored
    lda dlrAttached
    bne dlrRestored             // attached instance -- record already exists
    lda dynTaskNameLo,y; sta kPtr
    lda dynTaskNameHi,y; sta kPtr+1
    lda dlcLoadLo; sta kArg
    lda dlcLoadHi; sta kArg+1
    lda dynTaskSizeLo,y; sta kArg+2   // code size survives in the dyn table
    lda dynTaskSizeHi,y; sta kArg+3   // (dlcSizeLo/Hi are clobbered by dlReLibs)
    lda dlStartPage; sta kArg+6
    lda dlPages; sta kArg+7
    jsr resAddRecord
dlrRestored:
    ldy dlRegSlot
    iny
    jmp dlrScan                 // keep going -- restore every fitting task
dlrLibFail:
    // Eager lib load failed on restore -- free the code, leave the task
    // swapped (retried on a later dynFree). The attach path can't reach
    // here (its libs are already resident via the co-instance).
    lda dlrAttached
    bne dlrLibNext              // attached -- shared code, don't free
    lda dlPages
    beq dlrLibNext
    ldx dlStartPage
    tay
    jsr freeFromPool            // X=startPage, Y=pages
dlrLibNext:
    ldy dlRegSlot               // reload the scan slot (dlReLibs clobbered Y)
    jmp dlrNext
dlrNext:
    iny
    jmp dlrScan
dlrNone:
    // A focus-owner exit blanks the display (transferFocusAway runs
    // BEFORE dynFree, while the swapped victims look DEAD-ish). Now
    // that they're restored, hand the display back to the first
    // owner-surface task. X=$FF -> transferFocusAway acts only when
    // kbFocus is $FF -- a live focus holder is never stolen.
    ldx #$ff
    jsr transferFocusAway
    clc
    rts

// ==========================================
//  Shared registration (both paths)
// ==========================================
//  kArg = entry (code address), +2/3 = size, +4 = flags,
//  +6 = startPage, +7 = pages.
//  X = spawned task slot (spawnCore/initTCB_ptr preserve X through
//  the spawn syscall -- the slot is NOT found by scanning names;
//  multi-instance tasks share a name and that scan matched the
//  first instance).
dlRegister:
    jsr dynRegister              // A = dynTask slot; X (task slot) preserved
    bcc dlRegGot
    // Defensive (unreachable -- dynLoad pre-checks dynTaskCount):
    // table full. Free this task's pages, kill the spawned slot,
    // report error 0. dlReentFound checks C on return.
    stx kArg+2                   // save task slot
    lda kArg+7
    beq dlRegKill                // no pages to free
    ldy kArg+7                   // pages
    ldx kArg+6                   // startPage
    jsr freeFromPool             // X is clobbered -- the slot is reloaded below
dlRegKill:
    ldx kArg+2                   // task slot
    lda #0
    sta taskState,x              // mark spawned task DEAD
    sta kArg                     // error 0 (no free dyn slots)
    sec
    cli
    rts
dlRegGot:
    tay                          // Y = dynTask slot
    sty dlRegSlot
    txa; sta dynTaskId,y          // store real GordonOS task ID
    lda kArg+7; sta dynTaskPageCount,y
    lda kArg+4
    and #KF_MALLOC_RAM
    beq dlRegNoRam
    lda dlRegStartPage
    sta dynTaskRamStartPage,y
    lda dlRegPages
    sta dynTaskRamPages,y
    // write B/N into the instance buffer (dataPtr+0..3): B_lo, B_hi, N_lo, N_hi
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
    lda dlRegPages
    :poolAsl()
    sta (kPtr),y
dlRegNoRam:
    ldy dlRegSlot
    // Copy the name into this slot's OWN buffer and point the task at
    // it -- otherwise all dynamic tasks share dynNameBuf and `ps` shows
    // the most-recently-loaded name for every one of them.
    lda dynTaskNameLo,y; sta kPtr
    lda dynTaskNameHi,y; sta kPtr+1   // kPtr = &dynTaskNameBuf[y*12]
    ldy #0
dlCopyName:
    lda dynNameBuf,y
    sta (kPtr),y
    beq dlCopyNameDone
    iny
    cpy #11
    bcc dlCopyName
    lda #0
    sta (kPtr),y                // truncate 12+ char names with null
dlCopyNameDone:
    lda kPtr; sta taskNameLo,x   // the task's name lives in its own buffer
    lda kPtr+1; sta taskNameHi,x
    lda reentFirst
    beq dlRegNoRes
    lda #0; sta reentFirst
    txa; pha                     // resAddRecord clobbers X -- save task slot
    jsr resAddRecord             // first reentrant load -> residency record
    pla; tax
dlRegNoRes:
    lda #0
    sta taskThreadNameReuHi,x   // the slot may be recycled: a thread child that
                                //   exited itself leaves its tag and its REU name
                                //   record behind, and getTaskName tests that tag
                                //   to decide whether to DMA the old thread's name
    lda curTask
    sta taskParent,x            // parent = caller (SEI'd -- child can't read it early)
    jsr dlCopyArgs              // deliver args (no-op unless KF_TAKES_ARGS)
    // --- load EAGER libs (transactional; after dlLoadCore -- scratchBuf free) ---
    ldy dlRegSlot
    lda dynTaskLibMaskLo,y
    ora dynTaskLibMaskHi,y
    beq dlRegNoEager
    jsr dlLibEagerLoad          // C=0 all eager libs resident / C=1 (kArg=code)
    bcc dlRegNoEager
    lda kArg; pha               // save error code (unwind clobbers kArg+4/+6)
    jsr dlRegUnwind             // free pages, release libs, kill task slot
    pla; sta kArg               // restore error code
    lda dlEvictedSelf
    bne dlRegEagerResched
    cli
    sec
    rts
dlRegEagerResched:
    jmp dlSelfEvictedExit       // self-evicted caller is gone -- restore it
dlRegNoEager:
    ldx dlNewTask
    lda #TA_ALIVE
    sta taskState,x             // un-hold -- the task is now schedulable
    lda dlEvictedSelf
    beq dlRegNormal
    sei                         // atomic: the caller's code was just freed
    jmp reschedule              // switch to the newly-loaded task (never returns)
dlRegNormal:
    clc; cli; rts

// --- resAddRecord -- create a residency record after a reentrant first load ---
//  Called from dlRegister when reentFirst was set.
//  Input: kArg = entry (code address), kArg+2/+3 = size, kArg+6 = startPage,
//         kArg+7 = pages, kPtr = this slot's name buffer (dlCopyName)
//  Output: record at resCount, refCount=1, dynTaskResidency[dlRegSlot] = idx
//  Clobbers: A, X, Y
resAddRecord:
    // reuse a tombstoned record (resName=0) before appending -- swap-out
    // tombstones a record without decrementing resCount (Phase D)
    ldx #0
resScan:
    cpx resCount
    bcs resAppend
    lda resNameLo,x
    ora resNameHi,x
    beq resFill                  // tombstone -> reuse slot X
    inx
    cpx #MAX_RESIDENTS
    bcc resScan
resAppend:
    ldx resCount
    cpx #MAX_RESIDENTS
    bcc resAppendOk              // defensive (H1): never write out of
    rts                          // bounds -- record is skipped, the instance
                                 // stays unshared (dynFree treats it as single)
resAppendOk:
    inc resCount
resFill:
    lda kPtr; sta resNameLo,x
    lda kPtr+1; sta resNameHi,x
    lda kArg; sta resAddrLo,x
    lda kArg+1; sta resAddrHi,x
    lda kArg+2; sta resSizeLo,x
    lda kArg+3; sta resSizeHi,x
    lda kArg+6; sta resStartPage,x
    lda kArg+7; sta resPageCount,x
    lda #1; sta resRefCount,x
    ldy dlRegSlot
    txa; sta dynTaskResidency,y
    rts

// --- dlSpawnErr -- map the spawn syscall's failure code to a loader error ---
//  Input: A = spawn failure code (0 = no free task slot, 1 = heap full)
//  Output: kArg = 13 (no free task slot) or 12 (instance heap full)
dlSpawnErr:
    bne dlSpHeap
    lda #13; sta kArg            // error 13: no free task slot
    rts
dlSpHeap:
    lda #12; sta kArg            // error 12: instance heap full
    rts

dlErr0: lda #0; sta kArg; jmp dlRts
dlErr2: lda #2; sta kArg; jmp dlRts
dlErr5: lda #5; sta kArg; jmp dlRts
dlErr11: lda #11; sta kArg; jmp dlRts
//  Every failure return goes through here, because a failure that happens
//  after the caller was evicted has nowhere to return TO: its code pages were
//  freed to make room, so an rts lands in whatever now occupies them. Hand
//  those to dlSelfEvictedExit, which restores the parked tasks (the caller
//  cold-restarts from its entry) and reschedules.
dlRts:  lda dlEvictedSelf
        beq dlRtsReturn
        jmp dlSelfEvictedExit
dlRtsReturn:
        sec
        cli
        rts

// --- Persistent name buffer (copied here from (dataPtr) before spawn) ---
dynNameBuf:  .fill 12, 0       // max 11 chars + null
reentFirst:  .byte 0           // 1 = this load is a reentrant FIRST load
reentResIdx: .byte 0           // residency index for shared-instance spawns
dlRegSlot:   .byte 0           // dyn slot set by dlRegister (residency tagging)
dlFixCountLo: .byte 0          // fixup count LO (the count is a 16-bit pair)
dlFixCountHi: .byte 0          // fixup count HI
dlFlags:      .byte 0          // flags copy -- fixup chunks overwrite scratchBuf
dlChunkCnt:   .byte 0          // fixups remaining in the current chunk
dlNewTask:    .byte 0          // spawned task slot -- held SLEEPING during the
dlRegSizeHi:  .byte 0
// This load's FS offset (24-bit), saved at dlSizeOk and re-established in
// rlAligned. Kept as a variable rather than the stack so the loader's error
// paths (which return without unwinding) cannot leak it.
dlFsSave:     .fill 3, 0          // the loader's FS-offset cursor -- 3 bytes so
                                //   the bank byte survives; see dlSizeOk/rlAligned
dlRamPages:    .byte 0         // requested program-RAM pages (kDynLoad input)
dlRegStartPage: .byte 0        // region start page (computed at reserve time)
dlRegPages:   .byte 0          // region page count
// --- malloc-block scan scratch (dlSwapMalloc/dlRestoreMalloc) ---
dlMallocIdx:  .byte 0          // malloc-table scan index
dlMallocOwner:.byte 0          // malloc-table scan owner (task slot)
dlMallocHi:   .byte 0          // REU shadow hi accumulator
dlMallocPage: .byte 0          // current block startPage
dlMallocCnt:  .byte 0          // current block pages
dlMallocSlot: .byte 0          // dyn slot saved across dlSmOne (dlSwapMalloc)
dlFreeTaskId: .byte 0          // dynFree stash: the task id being freed
dlAllocHi:  .byte 0            // pool placement: 1 = highest fit (libraries),
                               //   0 = lowest (task code/RAM/malloc). Set by
                               //   dlLibCore, consumed by dlAllocCode (see
                               //   pool-top.inc) -- always returns to 0.
// --- Phase B: dlLoadCore scratch/results ---
dlStartPage: .byte 0           // core result: allocated start page
dlPages:     .byte 0           // core result: pages allocated
dlDelta:     .byte 0           // core: fixup delta_hi
dlcSizeLo:   .byte 0           // core: code size LO (survives clobbers)
dlcSizeHi:   .byte 0           // core: code size HI
dlcOriginHi: .byte 0           // core: origin HI (delta calc)
dlcDataSize: .byte 0           // core: data_size (survives clobbers)
dlcZpBase:   .byte 0           // core: zp_base (survives clobbers)
dlcZpLen:    .byte 0           // zp_len (moved off the stack -- both paths)
dlcLoadLo:   .byte 0           // core: load_addr LO (reloaded after a chunk DMA)
dlcLoadHi:   .byte 0           // core: load_addr HI
// --- Phase C: eviction ---
dlEvictedSelf: .byte 0         // 1 = the CALLER's code was freed (swapped or killed):
                               //   never return to it -- dlSelfEvictedExit reschedules
dlNoSelfEvict: .byte 0         // 1 = lib/restore load: never evict curTask
dlEvictBest:  .byte 0          // best victim pageCount during the scan
dlEvictSlot:  .byte 0          // best victim dyn slot ($FF = none)
dlrAttached:  .byte 0          // restore: 1 = attach (skip resAddRecord)
// --- args delivery (KF_TAKES_ARGS) ---
dlArgsLo:     .byte 0          // args string pointer (caller's buffer; 0 = none)
dlArgsHi:     .byte 0
dlArgsCap:    .byte 0          // copy cap: min(MAX_TASK_ARGS, data_size - 1)
dlLibTmp2:    .byte 0          // lib refcount: mask hi scratch (libFree clobbers dlLibMaskHi)

// ==========================================
//  dlLibRefcount lives in the loader tail ($FA6F block) -- see the copy right
//  before dlReSlot. It skips eager libs, whose reference is taken at
//  dlLibEagerLoad instead, so a resident eager lib
//  can be freed during the load when its only other holder is evicted
//  (rather than pinning the pool with a pending reference).
// ==========================================

// ==========================================
//  dlLibEagerLoad -- take this task's lib references + load EAGER libs
//  (after the task's dlLoadCore). ALL declared libs get their refcount
//  here (lazy AND eager) -- never before the code load, so a resident lib
//  whose only other holder is evicted during the load is freed and
//  re-loaded contiguously instead of pinning pages that fragment the
//  pool (the `run basic 3` warm-reboot bug).
// ==========================================
//  Input:  Y = dyn slot (dynTaskLibMaskLo/Hi[y] = the task's 16-bit mask)
//  Output: C=0 all eager libs resident; C=1 a libLoad failed (kArg = code).
//  Clobbers: A, X, Y, kArg block, kPtr, srcPtr, dataPtr, dlLibTmp, dlLibTmp2,
//            dlLibMask/dlLibMaskHi (via libLoad's dynParseHeader).
//  NOTE: SEI'd. Called from dlRegister AFTER the task is fully registered, so
//        libLoad's dlLoadCore clobbering the loader's dlc*/dlStartPage/dlPages
//        is harmless (all consumed). The mask is re-read from the per-slot
//        table every iteration -- libLoad overwrites dlLibMask/dlLibMaskHi.
dlLibEagerLoad:
    sty dlLibTmp2               // dyn slot (libLoad clobbers X/Y)
    lda dataPtr; pha            // preserve the caller's dataPtr (the shell's
    lda dataPtr+1; pha          //   instance buffer) -- libLoad points it at
                                //   dynNameBuf for dynParseHeader
    // --- pass A: one reference for EVERY declared lib (lazy + eager) ---
    //  No loads happen here, so this pass cannot fail and it visits every
    //  declared bit. That is what makes the callers' rollback exact:
    //  dlRegUnwind/dlReLibs undo a failed load with dlLibRelease, which walks
    //  the WHOLE mask. Interleaving the increments with the loads left the
    //  bits ABOVE a failing load un-incremented but still decremented -- so a
    //  lib another task still held was freed early, or (count 0) wrapped to
    //  $FF and be pinned with its pool pages stranded forever.
    ldx #0
dleRefLoop:
    cpx #MAX_LIBS
    bcs dleLoadInit
    jsr dleBitSet               // Z=0 -> lib X is declared in this slot's mask
    beq dleRefNext
    inc libRefCount,x           // reference for ALL declared libs (lazy + eager)
dleRefNext:
    inx
    jmp dleRefLoop
    // --- pass B: load the EAGER libs (lazy ones load on first use) ---
dleLoadInit:
    ldx #0
dleLoop:
    cpx #MAX_LIBS
    bcs dleDone
    jsr dleBitSet               // Z=0 -> lib X is declared in this slot's mask
    beq dleNext
    lda libEager,x
    beq dleNext                 // lazy -> reference only, loaded on first use
    txa                         // A = lib id
    pha                         // save loop counter (libLoad clobbers X)
    jsr libLoad                 // C=0 loaded / C=1 error (kArg = code)
    pla
    tax                         // C preserved by pla/tax
    bcs dleFail
dleNext:
    inx
    jmp dleLoop
dleDone:
    pla; sta dataPtr+1
    pla; sta dataPtr
    clc
    rts
dleFail:
    pla; sta dataPtr+1
    pla; sta dataPtr
    sec
    rts

// --- dleBitSet -- Z=0 iff lib X is declared in slot dlLibTmp2's mask ---
//  Input: X = lib id, dlLibTmp2 = dyn slot. Clobbers A, Y -- X is preserved.
//  Loader-local helper (no jump-table entry, no kernel routine): both passes
//  need the same 16-bit-mask test, so it lives here once.
dleBitSet:
    ldy dlLibTmp2
    lda dynTaskLibMaskLo,y
    cpx #8
    bcc dleBsMask
    lda dynTaskLibMaskHi,y
dleBsMask:
    sta dlLibTmp                // mask byte
    txa
    and #7
    tay
    lda bitMasks,y
    and dlLibTmp
    rts

// ==========================================
//  dlLibRelease -- decrement lib refcounts for a task slot; free at 0
// ==========================================
//  Input:  Y = dyn slot (dynTaskLibMaskLo/Hi[y] = the task's 16-bit mask)
//  Clobbers: A, X, Y, dlLibMask, dlLibTmp, dlLibTmp2
//  NOTE: runs SEI'd (called from dynFree). libFree clobbers dlLibMaskHi,
//  so the mask hi byte is stashed in dlLibTmp2.
dlLibRelease:
    lda dynTaskLibMaskLo,y
    sta dlLibMask
    lda dynTaskLibMaskHi,y
    sta dlLibTmp2               // mask hi (safe from libFree)
    ldx #0
libRelLoop:
    cpx #MAX_LIBS
    bcs libRelDone
    lda dlLibMask
    cpx #8
    bcc libRelTest
    lda dlLibTmp2
libRelTest:
    sta dlLibTmp                // current mask byte
    txa
    and #7
    tay
    lda bitMasks,y
    and dlLibTmp
    beq libRelNext
    lda libRefCount,x
    beq libRelNext              // already 0 -> nothing to release. Without
                                //   this the decrement wrapped to $FF, which
                                //   reads as "still referenced" forever, so
                                //   the lib is pinned and its pool pages
                                //   stranded
    dec libRefCount,x
    bne libRelNext              // still referenced -> keep
    txa
    pha                         // save lib id (libFree clobbers X/Y)
    jsr libFree                 // X = lib id
    pla
    tax
libRelNext:
    inx
    jmp libRelLoop
libRelDone:
    rts

// ==========================================
//  dlRegUnwind -- undo a registered-but-failed spawn (eager lib load failed)
// ==========================================
//  Frees the task's code + RAM pages, releases its lib refcounts, kills the
//  spawned task slot and removes the dyn-table entry. Runs SEI'd; called
//  from dlRegister after dlLibEagerLoad returns C=1. The dyn slot is taken
//  from dlRegSlot; the GordonOS task slot from dynTaskId[slot] (read before
//  it is cleared). Clobbers A, X, Y, kArg+4/+6 (via freeFromPool).
dlRegUnwind:
    ldy dlRegSlot
    lda dynTaskLibMaskLo,y
    ora dynTaskLibMaskHi,y
    beq duNoLibs
    jsr dlLibRelease            // decrement refcounts, free at 0 (clobbers A/X/Y)
duNoLibs:
    ldy dlRegSlot
    lda dynTaskPageCount,y
    beq duNoCode
    ldx dynTaskStartPage,y
    tay
    jsr freeFromPool            // X=startPage, Y=pages
duNoCode:
    ldy dlRegSlot
    lda dynTaskRamPages,y
    beq duNoRam
    lda dynTaskRamStartPage,y
    cmp #$ff
    beq duNoRam                 // swapped -- pages already freed
    tax
    lda dynTaskRamPages,y
    tay
    jsr freeFromPool
duNoRam:
    ldy dlRegSlot
    ldx dynTaskId,y             // GordonOS task slot (read before clearing)
    lda #0
    sta taskState,x             // kill the spawned (never-scheduled) task
    lda #$FF; sta dynTaskId,y
    sta dynTaskResidency,y      // detach as well: a tag left
                                //   behind describes a record whose pages this
                                //   path just freed, and `findLiveByName` gates
                                //   on `dynTaskId != $ff` so it would be skipped
                                //   -- harmless, but a half-torn-down slot must
                                //   not look live to anything
    lda #0
    sta dynTaskPageCount,y
    sta dynTaskRamPages,y
    sta dynTaskRamStartPage,y
    dec dynTaskCount
    rts

// ==========================================
//  dlCopyArgs -- consolidated right after the loader
// ==========================================
//  Deliver args to a freshly spawned task (X = slot).
//  KF_TAKES_ARGS binaries: copies the args string into the new task's
//  instance buffer at offset 0 (null-terminated). Leading spaces are
//  skipped; the copy is capped at MAX_TASK_ARGS chars and data_size - 1
//  so the terminator always fits. No-op when the flag is clear, the task
//  has no data buffer, or no args were passed.
//  Clobbers: A, Y, kPtr, srcPtr, dlArgsCap. X preserved.
dlCopyArgs:
    lda dlFlags
    and #KF_TAKES_ARGS
    beq dlCaRts
    lda dlcDataSize
    beq dlCaRts                 // no instance buffer -- args undeliverable
    lda dlArgsLo
    ora dlArgsHi
    beq dlCaRts                 // no args string passed
    lda dlcDataSize
    cmp #MAX_TASK_ARGS+1
    bcc dlCaSmall               // data_size <= 64 -> cap = data_size - 1
    lda #MAX_TASK_ARGS
    bne dlCaCap                 // (always taken)
dlCaSmall:
    sec
    sbc #1
dlCaCap:
    sta dlArgsCap
    lda taskDataPtrLo,x
    sta kPtr                    // dest = child instance buffer
    lda taskDataPtrHi,x
    sta kPtr+1
    lda dlArgsLo
    sta srcPtr
    lda dlArgsHi
    sta srcPtr+1
    ldy #0
dlCaSkipSp:
    lda (srcPtr),y
    beq dlCaTerm                // empty args -- just terminate
    cmp #$20
    bne dlCaCopy
    inc srcPtr
    bne dlCaSkipSp
    inc srcPtr+1
    jmp dlCaSkipSp
dlCaCopy:
    lda dlArgsCap
    beq dlCaTerm                // cap reached -- terminate
    lda (srcPtr),y
    beq dlCaTerm
    sta (kPtr),y
    inc srcPtr
    bne dlCaNext
    inc srcPtr+1
dlCaNext:
    inc kPtr
    bne dlCaLoop
    inc kPtr+1
dlCaLoop:
    dec dlArgsCap
    jmp dlCaCopy
dlCaTerm:
    lda #0
    sta (kPtr),y
dlCaRts:
    rts

// ==========================================
//  Library lifecycle helpers -- eviction / restore / rollback
// ==========================================
//  Right after dlCopyArgs. All run SEI'd
//  (dlEvict/dlRestore/dlReloc callers hold SEI), so the helper-local
//  dlReSlot is IRQ-safe.

// --- dlEvLibs -- release slot Y's declared libraries (eviction) ---
//  Input: Y = dyn slot. No-op when the mask is 0. Clobbers A, X, Y.
dlEvLibs:
    lda dynTaskLibMaskLo,y
    ora dynTaskLibMaskHi,y
    beq dlvRts
    jsr dlLibRelease            // Y = slot (decrement + free at 0)
dlvRts:
    rts

// --- dlReLibs -- re-acquire slot Y's libraries after a restore ---
//  Input: Y = dyn slot. Output: C=0 ok, C=1 eager lib load failed
//  (refcounts rolled back). Clobbers A, X, Y, kArg block, kPtr, srcPtr,
//  dataPtr, dlLibMask/Hi, dlLibTmp, dlLibTmp2.
dlReLibs:
    sty dlReSlot                // stash slot (dlLibEagerLoad clobbers Y)
    lda dynTaskLibMaskLo,y
    sta dlLibMask
    lda dynTaskLibMaskHi,y
    sta dlLibMaskHi
    jsr dlLibRefcount           // re-increment refcounts (all declared libs)
    ldy dlReSlot
    jsr dlLibEagerLoad          // load eager libs; C=0 ok / C=1 fail
    bcc dlrOk
    ldy dlReSlot
    jsr dlLibRelease            // roll back the refcount (decrement + free at 0)
    sec
    rts
dlrOk:
    clc
    rts

// --- dlLibUnref -- NO-OP. Refcounts are taken only in
//  dlLibEagerLoad (after the code load), so a dlLoadCore/spawn failure
//  has nothing to roll back here. The eager-load failure path rolls back
//  via dlLibRelease in dlRegUnwind. Callers (rlAligned/rlSpFailErr) stay
//  balanced through this stub.
dlLibUnref:
    rts

// --- dlLibRefcount -- NO-OP. ALL lib refcounts are taken
//  in dlLibEagerLoad, AFTER the code load. Taking the lazy reference here
//  (at dlSizeOk, BEFORE the load) pinned a resident lazy lib (filesys.lib)
//  while its only other holder (the shell) was evicted during the load --
//  the pinned pages fragmented the pool so a large code block (basic)
//  could not fit contiguously -> warm reboot. Kept as a stub so the
//  dlSizeOk/dlReLibs call sites stay balanced.
dlLibRefcount:
    rts

dlReSlot:   .byte 0
dlRollMask: .byte 0
dlRollMaskHi: .byte 0

// ==========================================
//  Caller self-eviction policy
// ==========================================
//  Kept at the very end of the loader code so this addition shifts as
//  little existing code as possible (a mid-region insert pushes nearby
//  branches past +/-128 and KickAssembler re-emits them as 5-byte
//  trampolines -- that bloat ate the region's slack).

// --- dlCallerPages -- pool pages that evicting the CALLER would free ----
//  Output: A = pages (0 when the caller has no dyn-table entry).
//  Counts the caller's code pages (or its shared reentrant block) PLUS the
//  pages its declared libraries give back: eviction runs dlEvLibs, which
//  frees every lib the caller is the last holder of. That second part is
//  not optional book-keeping -- with libraries allocated from the TOP of the
//  pool those pages are outside the free space, so a code-only count refuses
//  loads that would actually fit once the caller is sacrificed (`run basic`
//  needs 99 pages: 78 free + 19 caller = 97, so a code-only count refuses it).
//  The lib part is exact: dlLibRelease only frees refcount-1 libs. Clobbers
//  A, X, Y, kArg+7, dlLibTmp, dlLibTmp2 (see dlCallerAddLibs).
dlCallerPages:
    lda curTask
    jsr dynFindByTaskId         // C=0 -> A = dyn slot
    bcs dlCpNone
    tay
    lda dynTaskResidency,y
    cmp #$FF
    beq dlCpTcb
    tax
    lda resPageCount,x          // shared reentrant code -- all instances swap
    jmp dlCallerAddLibs
dlCpTcb:
    lda dynTaskPageCount,y
    jmp dlCallerAddLibs         // + the pages its libs release
dlCpNone:
    lda #0
    rts

// --- dlSelfEvictOk -- may the pre-evict loop sacrifice the caller? ------
//  Called by dlPreEvict when it runs out of other victims. Pass 1 keeps
//  the caller protected (dlNoSelfEvict = 1); this decides whether lifting
//  that protection can still make the load fit. If it cannot, the caller
//  must be left alone so it survives to report the error -- otherwise its
//  own code is freed, nothing is left to print the error, and the
//  scheduler warm-reboots.
//  Output: C=1 = allowed (protection lifted -- the caller is now evictable)
//          C=0 = refused (already pass 2, or the caller's pages cannot help)
//  The test is PLACEMENT, not a page count: the sacrifice only helps when the
//  space left can host a run of `dlPreNeed` once the caller's pages cover the
//  shortfall -- free pages split into runs smaller than the request are free
//  space the request cannot use. A sacrifice that cannot help must be refused,
//  because it is irreversible: the caller's code pages are freed, so the load
//  cannot return to it and the shell's own "? no space in pool" is lost.
//  Clobbers A, X, Y, kArg, kArg+4, kArg+5, kArg+6.
dlSelfEvictOk:
    lda dlNoSelfEvict
    beq dlSeNo                  // pass 2 -> genuinely impossible, don't loop
    jsr dlCallerPages
    sta kArg                    // stash (dlProbeFit clobbers kArg+4..+6)
    lda dlPreNeed
    sec
    sbc kArg                    // shortfall the free space must cover
    bcc dlSeAllow               // caller's pages cover the whole request
    tax
    jsr dlProbeFit              // C=1 -> no run would appear: refuse
    bcs dlSeNo
dlSeAllow:
    lda #0
    sta dlNoSelfEvict           // caller evictable now -- retry the loop
    sec
    rts
dlSeNo:
    clc
    rts

// --- dlSelfEvictedExit -- the caller's own code was swapped out --------
//  The caller can never return to itself, but abandoning the load with
//  zero ALIVE tasks makes findNextAlive warm-reboot the machine (Bug
//  #71). Swap the evicted tasks back in -- the caller cold-restarts from
//  its entry -- and reschedule to them, so a failed load costs a restart
//  of the affected task instead of the whole OS.
//  The error code cannot travel with that restart (dlRestore destroys
//  kArg), so it is drawn into the CALLER's own surface slot first: that
//  slot is what dlRestore swaps back in, so the restored screen carries
//  the code where the user can read it. Without it a failed load is
//  completely silent -- no caller is left to report it.
dlSelfEvictedExit:
    sei                         // atomic: the caller's code is already freed
    jsr dlRestore               // restores every swapped task that fits
    sei                         // local find/dma paths exit CLI'd
    jmp reschedule
.label dlLibsEnd = *
