// ==========================================
//  poolevict.inc -- eager-lib pre-eviction
// ==========================================
//  A task whose code + program-RAM + eager libs exceed the free pool would
//  fragment it: the code loads first (its own eviction may swap the shell),
//  then gfx.lib can never fit the leftover holes, so the load fails AFTER the
//  shell was already evicted -- leaving no ALIVE task and forcing a warm reboot.
//  dlPreEvict runs BEFORE dlLoadCore (from dlReloc's rlAligned) and evicts until
//  `needed = code + RAM + eager libs` pages are free, so the whole footprint
//  fits in one pass.
//
//  Cheap where it does not apply: mask-0 tasks return instantly (nothing to
//  do); eager-but-fitting tasks are unchanged (free >= needed -> no eviction);
//  and `needed > POOL_PAGES` fails cleanly (error 4) BEFORE evicting anything.
//
//  Lives in the high region right after malloc.inc.
//
//  All four run SEI'd (the whole dynLoad path is atomic).

.pc = *

// --- dlPreEvict -- orchestrate the pre-eviction --------------------------
//  Input:  kArg+2/+3 = code size, dlFlags, dlRamPages,
//          dlRollMask/dlRollMaskHi (stashed parsed mask).
//  Output: C=0 ok (kArg+1..+7 preserved) / C=1 error 4 (kArg).
//  reuFsPtr is NOT preserved: it is the shared DMA-offset register, and the
//  loader re-installs its own cursor before each DMA (dlInstallCursor), so
//  clobbering it here is expected and harmless.
//  Manages dlNoSelfEvict: the caller is protected for the first eviction
//  pass and only sacrificed if that alone makes the load fit (see
//  dlSelfEvictOk). Running out of victims otherwise returns error 4 with
//  the caller still alive, so it can print the message.
//  Clobbers A, X, Y, kPtr, dlLibTmp, dlPreNeed, dlPreTmp. kArg (origin
//  lo, unused by the reloc path) and reuFsRemain are clobbered.
dlPreEvict:
    lda dlRollMask
    ora dlRollMaskHi
    bne dlPreDo                 // no declared libs -> nothing to pre-evict
    clc
    rts
dlPreDo:
    lda #1
    sta dlNoSelfEvict           // pass 1: evict every other task first -- the
                                //   caller is only sacrificed when that alone
                                //   can make the load fit (see dlSelfEvictOk)
    lda kArg+2                  // needed = ceil(code_size/256)
    clc
    adc #<POOL_PAGE_MASK
    lda kArg+3
    adc #>POOL_PAGE_MASK
    :poolLsr()
    sta dlPreNeed
    lda dlFlags
    and #KF_MALLOC_RAM
    beq dlPreNoRam              // no program-RAM reservation
    lda dlPreNeed
    clc
    adc dlRamPages
    sta dlPreNeed
dlPreNoRam:
    ldx #1                      // stash kArg+1..+7 -- dlEagerLibPages (via
dlPreSave:                      //   reuFindFile) + dlEvict clobber them
    lda kArg,x
    pha
    inx
    cpx #8
    bne dlPreSave
    // The task's FS offset is NOT stashed here. This block has zero byte slack,
    // and a partial (2-of-3 byte) stash would be worse than none: a missing bank
    // byte silently makes the task's fixup table read one bank high (garbage
    // offsets, error 1, warm reboot). dlEagerLibPages clobbers reuFsPtr, so the
    // COMPLETE 24-bit offset is saved at dlSizeOk and re-established in rlAligned
    // before the load -- this function neither needs nor does any stashing, and
    // the invariant has exactly one home.
    jsr dlEagerLibPages         // A = eager, non-resident lib pages
    sta dlPreTmp
    lda dlPreNeed
    clc
    adc dlPreTmp
    sta dlPreNeed               // needed = code + RAM + eager libs
    cmp #POOL_PAGES+1
    bcc dlPreLoop
    lda #4                      // bigger than the whole pool -- fail before
    jmp dlPreFail               //   evicting anything
dlPreLoop:
    jsr dlFreePages             // A = free pages (total, as always)
    cmp dlPreNeed
    bcc dlPreEv                 // short of pages -> evict one
    ldx dlPreNeed
    jsr dlProbeFit              // pages enough: a RUN of that size must exist
    bcs dlPreEv                 //   too, or the allocation fails later
    jmp dlPreDone               // both hold -- the load can proceed
dlPreEv:
    jsr dlEvict                 // evict one (largest first); C=0 evicted
    bcc dlPreLoop
    lda dlEvictedSelf           // no victim left -- did we free OUR OWN code?
    bne dlPreSelfEvicted
    jsr dlSelfEvictOk           // caller still protected: would taking it
    bcs dlPreLoop               //   close the gap? yes -> allow it + retry
    lda #4                      // no -> error 4 with the caller ALIVE, so it
    jmp dlPreFail               //   survives to print the message
dlPreSelfEvicted:
    lda #4                      // caller's code was freed -- never return
    sta kArg
    sei
    jmp dlSelfEvictedExit       //   (swaps it back in instead of rebooting)
dlPreDone:
    clc
    jmp dlPreRestore
dlPreFail:
    sta dlPreTmp                // stash error code (restore loop clobbers A)
    sec
dlPreRestore:
    ldx #7
dlPreRestoreLoop:
    pla
    sta kArg,x
    dex
    bne dlPreRestoreLoop
    bcc dlPreRts                // success -- kArg+1..+7 restored, C=0
    lda dlPreTmp                // fail -- error code
    sta kArg
    pla                         // discard our own jsr return address so
    pla                         //   dlRts's rts returns to the SHELL, not
    jmp dlRts                   //   back into rlAligned (sec; cli; rts)
dlPreRts:
    rts
    // 6 bytes of deliberate padding -- the SIZE of this block is ABI-frozen:
    // the scratch addresses that follow it (ff*/kEll*/kCircle*, $FC3F-$FF7D)
    // are hardcoded in gfx.lib and pinned by exact-address .asserts in
    // main.asm, so every edit here must keep the block's total size
    // unchanged.
    .fill 6, $EA
.label dlPreEnd = *

// --- dlEagerLibPages -- sum eager, non-resident lib pages ----------------
//  Input: dlRollMask/dlRollMaskHi. Output: A = total pages (C=0).
//  Clobbers A, X, Y, kArg, kPtr, dlLibTmp, reuFsPtr/reuFsRemain.
dlEagerLibPages:
    lda #0
    sta dlPreTmp
    ldx #0
dlElpLoop:
    cpx #MAX_LIBS
    bcs dlElpDone
    lda dlRollMask
    cpx #8
    bcc dlElpLo
    lda dlRollMaskHi
dlElpLo:
    sta dlLibTmp                // current mask byte
    txa
    and #7
    tay
    lda bitMasks,y
    and dlLibTmp
    beq dlElpNext               // not declared
    lda libEager,x
    beq dlElpNext               // lazy -- 0 pages at load
    lda libStartPage,x
    cmp #$FF
    beq dlElpPeek               // not resident -> exact pages from header
    lda libPages,x              // resident -> count it (its only other
    jmp dlElpAdd                //   holder may be evicted + re-loaded)
dlElpPeek:
    jsr dlPeekLibPages          // A = pages (X preserved, C=0)
dlElpAdd:
    adc dlPreTmp
    sta dlPreTmp
dlElpNext:
    inx
    jmp dlElpLoop
dlElpDone:
    lda dlPreTmp
    rts
.label dlEagerEnd = *

// --- dlPeekLibPages -- one lib's pool page count -------------------------
//  Input: X = lib id. Output: A = pages (0 if not found), C=0. X preserved.
//  Reads the lib's header code_size (bytes +5/+6) -- the file size includes
//  the fixup table, so the header is required.
//  Clobbers Y, kArg, kPtr, reuFsPtr/reuFsRemain.
dlPeekLibPages:
    lda libNameLo,x
    sta kArg
    lda libNameHi,x
    sta kArg+1
    txa
    pha
    jsr reuFindFile             // C=0 found; preserves I
    bcc dlPpFound
    pla
    tax
    lda #0                      // not found -- 0 pages (defensive)
    clc
    rts
dlPpFound:
    lda #<reuDirBuf             // DMA the 256-byte header page
    sta kPtr
    lda #>reuDirBuf
    sta kPtr+1
    lda #0
    sta kArg+6
    lda #1
    sta kArg+7
    jsr reuDmaRange             // preserves I
    lda reuDirBuf+HDR_CODE_SIZE    // code_size lo
    clc
    adc #<POOL_PAGE_MASK
    lda reuDirBuf+HDR_CODE_SIZE+1  // code_size hi
    adc #>POOL_PAGE_MASK
    :poolLsr()                  // A = ceil(code_size/256)
    sta kArg                    // stash (survives the pla/tax below)
    pla
    tax
    lda kArg
    clc
    rts
.label dlPeekEnd = *

// --- dlFreePages -- count free pool pages -------------------------------
//  Output: A = free pages. Clobbers X, Y, dlPreTmp.
dlFreePages:
    lda #POOL_PAGES
    sta dlPreTmp
    ldy #POOL_BITMAP_BYTES-1
dlFpByte:
    lda poolBitmap,y
    ldx #8
dlFpBit:
    lsr
    bcc dlFpClr
    dec dlPreTmp
dlFpClr:
    dex
    bne dlFpBit
    dey
    bpl dlFpByte
    lda dlPreTmp
    rts

// scratch shared by the helpers
dlPreNeed: .byte 0             // pages needed (code + RAM + eager libs)
dlPreTmp:  .byte 0             // accumulator / free-count / error-code stash
.label dlFreeEnd = *

// bounds -- the consolidated block lives in the high region before lib.inc
.assert "poolevict fits before the vectors", (dlFreeEnd <= $FFFA) ? 1 : 0, 1
