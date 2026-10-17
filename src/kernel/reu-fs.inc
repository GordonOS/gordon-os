// ==========================================
//  GordonOS -- REU Filesystem
//  Flat root-directory namespace
// ==========================================
//  Implements kReuSave, kReuLoad, kReuDelete, kReuRename,
//  kReuDirEntry, kReuFormat, and internal helpers.

// --- reuDetectSize -- probe REU bank count ---
//  Writes/reads $A5 at $FFF0 in each bank.
//  Stores count in reuTotalBanks (0-255).
//  Clobbers: A, X
//  Non-destructive: each bank's $FFF0 byte is saved and restored, so the
//  probe never scribbles $A5 over FS data -- the data region reaches $FFF0 of
//  bank 5+, which is exactly where the probe looks.
reuDetectSize:
    ldx #0
rdsProbe:
    stx $df06                   // set bank
    // DMA registers: C64 = reuDirBuf, REU addr = $FFF0, length = 1
    lda #<REU_DIR_BUF; sta $df02
    lda #>REU_DIR_BUF; sta $df03
    lda #$F0; sta $df04
    lda #$FF; sta $df05
    lda #1; sta $df07
    lda #0; sta $df08; sta $df0a
    // Save the original byte at $FFF0
    lda #$91; sta $df01         // REU -> C64
rdsWait0:
    lda $df00; and #$40; beq rdsWait0
    lda reuDirBuf
    sta reuDirBuf+1             // stash original byte
    // Write $A5
    lda #$A5; sta reuDirBuf
    lda #$90; sta $df01         // C64 -> REU
rdsWait1:
    lda $df00; and #$40; beq rdsWait1
    // Read back
    lda #$91; sta $df01
rdsWait2:
    lda $df00; and #$40; beq rdsWait2
    lda reuDirBuf
    cmp #$A5
    bne rdsDone                 // bank absent -- nothing to restore
    // Restore the original byte
    lda reuDirBuf+1
    sta reuDirBuf
    lda #$90; sta $df01
rdsWait3:
    lda $df00; and #$40; beq rdsWait3
    inx
    cpx #0                      // wrapped? max 256 banks
    bne rdsProbe
rdsDone:
    cpx #0
    beq rdsRts                  // wrapped: 256 banks, keep default
    stx reuTotalBanks
    lda #0; sta reuTotalBanks+1 // clear hi byte for <256 banks
rdsRts:
    rts

// --- reuFsInitBoot -- compute FS directory base, data base, capacity ---
//  Sets reuFsDirBase*, reuFsDataBase*, reuFsCapacity*, reuFsEnabled.
//  Must be called after reuDetectSize.
//  Clobbers: A
reuFsInitBoot:
    // Compute FS directory base from constants
    lda #<REU_FS_OFFSET
    sta reuFsDirBaseLo
    lda #>REU_FS_OFFSET
    sta reuFsDirBaseHi
    lda #REU_FS_BANK
    sta reuFsDirBaseBank

    // Data base = dir base + directory size (both in the same bank)
    lda reuFsDirBaseLo
    clc; adc #<REU_DIR_TOTAL_SIZE
    sta reuFsDataBaseLo
    lda reuFsDirBaseHi
    adc #>REU_DIR_TOTAL_SIZE
    sta reuFsDataBaseHi
    lda #REU_FS_BANK
    sta reuFsDataBaseBank

    // Capacity check: need at least 6 banks (384KB) for FS (16-bit)
    lda reuTotalBanks+1
    bne rfibInit                // >255 banks -> enough
    lda reuTotalBanks
    cmp #6
    bcs rfibInit                // >=6 banks -> enough
    lda #0; sta reuFsEnabled    // disabled
    rts
rfibInit:
    lda #REU_FS_SIGNATURE; sta reuFsEnabled

    // --- Compute capacity (24-bit, no clamp) ---
    // Bank 5 contributes REU_FS_BANK5_CAP ($3000); each bank 6..250 adds
    // $10000. Capacity = $3000 + (totalBanks - 11) * $10000:
    //   byte0 = $00, byte1 = $30, byte2 = max(0, totalBanks - 11)
    // The TOP full bank (251) is reserved -- $FB0000-$FBFFFF is grpaint's
    // undo ring -- so the data region ends at $FAFFFF and capacity is one
    // bank short of the REU's own top. tools/build-reu.ps1's $cap computes
    // the same figure for the host image, because the two MUST agree.
    // See docs/memory-map.md.
    lda #<REU_FS_BANK5_CAP; sta reuFsCapacityLo
    lda #>REU_FS_BANK5_CAP; sta reuFsCapacityHi
    lda reuTotalBanks+1
    bne rifCapFull              // >255 banks -> byte2 = 245 (banks 6..250)
    lda reuTotalBanks
    sec; sbc #11                // extra full banks = total - 11
    bcc rifCapDef               // <11 -> byte2 = 0
    sta reuFsCapacity2
    jmp rifCapDone
rifCapFull:
    lda #245
    sta reuFsCapacity2
    jmp rifCapDone
rifCapDef:
    lda #0
    sta reuFsCapacity2
rifCapDone:

    // --- DMA directory page 0 -> reuDirBuf ---
    lda #0
    clc                         // C=0 -> REU->C64 (read)
    jsr reuDirPageDma

    // Check signature at slot 0, byte 1
    lda reuDirBuf+1
    cmp #REU_FS_SIGNATURE
    beq rifAlreadyInit

    // --- First boot: no signature -> leave FS disabled ---
    // Boot does NOT auto-format. User must type 'format' at shell.
    lda #0; sta reuFsEnabled
    jmp rifDone

rifAlreadyInit:
    // Boot-computed capacity is authoritative; directory copy is stale.
    // (Prevents vicious cycle where old buggy capacity persists forever.)
    // FS stays enabled ($A5)

rifDone:
    rts

// ==========================================
//  kReuLoad -- load named block from REU to C64
// ==========================================
//  Input:  kPtr/kPtr+1 = C64 destination
//          kArg/kArg+1 = name pointer (null-term screen codes)
//  Output: C=0 success (kArg+2/kArg+3 = size)
//          C=1 not found or FS disabled
// ==========================================
//  reuFindFile -- locate a file, return FS offset + size (no DMA)
// ==========================================
//  Input:  kArg/kArg+1 = name pointer (null-term screen codes)
//  Output: C=0 found -- reuFsPtr/reuFsPtr+1 = FS offset,
//                      reuFsRemain/reuFsRemain+1 = size,
//                      A = page, X = entry index
//                      (entry location -- kReuRename captures these)
//          C=1 not found or FS disabled
//  Clobbers: A, X, Y, reuFsFlags, reuFsName (via reuCmpName)
//  NOTE: preserves the caller's interrupt flag (php/sei ... plp). The
//  loader calls this SEI'd with no alive task to run the scheduler;
//  a hard CLI here would let a pending 60 Hz IRQ warm-reboot mid-restore.
reuFindFile:
    php
    sei
    lda reuFsEnabled; cmp #REU_FS_SIGNATURE
    beq rffFsOk
    plp; sec; rts
rffFsOk:
    // Save name pointer to reuFsName (ZP-safe via reuCmpName which uses srcPtr)
    lda kArg; sta reuFsName
    lda kArg+1; sta reuFsName+1
    // Scan all 64 pages (512 entries)
    lda #0; sta reuFsFlags
rffPageLoop:
    lda reuFsFlags; cmp #64
    bcc rffDoPage
    plp; sec; rts               // not found
rffDoPage:
    lda reuFsFlags; clc; jsr reuDirPageDma
    lda #0; sta kArg+6          // entry index (0..7)
rffEntryLoop:
    lda kArg+6; cmp #8; bcs rffPageNext
    lda kArg+6; asl; asl; asl; asl; asl; tay   // entry * 32
    // Check not empty and not deleted (size field +20/+21)
    lda reuDirBuf+20,y; bne rffChkDel
    lda reuDirBuf+21,y; beq rffEntryNext
rffChkDel:
    lda reuDirBuf,y; and #$80; bne rffEntryNext
    // Compare name (name field at +1..+16, 11-char compare)
    tya; clc; adc #1; tay
    jsr reuCmpName
    bne rffEntryNext
    // found -- save offset (3 bytes) and size (2 bytes)
    lda kArg+6; asl; asl; asl; asl; asl; tay
    lda reuDirBuf+17,y; sta reuFsPtr
    lda reuDirBuf+18,y; sta reuFsPtr+1
    lda reuDirBuf+19,y; sta reuFsPtr+2
    lda reuDirBuf+20,y; sta reuFsRemain
    lda reuDirBuf+21,y; sta reuFsRemain+1
    lda reuFsFlags               // page (A) -- kReuRename output
    ldx kArg+6                   // entry index (X) -- kReuRename output
    plp; clc; rts
rffEntryNext:
    inc kArg+6; jmp rffEntryLoop
rffPageNext:
    inc reuFsFlags; jmp rffPageLoop

// ==========================================
//  reuDmaRange -- DMA a byte range of the FS data area to C64
// ==========================================
//  Input:  reuFsPtr/reuFsPtr+1/reuFsPtr+2 = FS offset (24-bit, 0-based)
//          kArg+6/kArg+7       = length
//          kPtr/kPtr+1         = C64 destination
//  Output: kPtr restored; reuFsPtr (3B) / reuFsRemain / kArg+6/+7
//          unchanged. Multi-bank (banks 5..251) handled by a chunked loop.
//  Clobbers: A, Y, kArg..kArg+5, reuFsFile, reuFsLen, reuFsFlags
reuDmaRange:
    php                         // save caller's I flag (loader runs SEI'd)
    sei
    lda kPtr; pha                  // save dest lo
    lda kPtr+1; pha                // save dest hi
    // Absolute position = data base (bank 5 $D000) + offset:
    //   addr = (O + $5D000) & $FFFF;  bank = (O + $5D000) >> 16
    :FSToReuAddr(reuFsPtr, reuFsFile, reuFsFile+1, reuFsFlags)
    lda kArg+6
    sta reuFsLen                // remaining lo
    lda kArg+7
    sta reuFsLen+1              // remaining hi

rdrLoop:
    lda reuFsLen
    ora reuFsLen+1
    bne rdrGo
    jmp rdrDone                 // remaining == 0
rdrGo:
    // addr == 0 -> single DMA of the whole remainder, done
    lda reuFsFile
    ora reuFsFile+1
    bne rdrSplit
    lda #0; sta kArg; sta kArg+1
    lda reuFsFlags; sta kArg+2
    lda reuFsLen; sta kArg+3
    lda reuFsLen+1; sta kArg+4
    lda #0; sta kArg+5
    jsr kReuToC64
    jmp rdrDone
rdrSplit:
    // chunk = $10000 - addr (fills to the end of this bank)
    sec
    lda #0
    sbc reuFsFile               // chunk lo
    sta kArg+3
    lda #0
    sbc reuFsFile+1             // chunk hi
    sta kArg+4
    // if remaining < chunk -> DMA the remainder and stop
    lda reuFsLen+1
    cmp kArg+4
    bcc rdrLast
    bne rdrFull
    lda reuFsLen
    cmp kArg+3
    bcc rdrLast
rdrFull:
    // remaining >= chunk: DMA chunk bytes at addr/bank
    lda reuFsFile; sta kArg
    lda reuFsFile+1; sta kArg+1
    lda reuFsFlags; sta kArg+2
    lda #0; sta kArg+5
    jsr kReuToC64
    // advance the C64 destination by the chunk length (kArg+3/+4)
    lda kPtr
    clc
    adc kArg+3
    sta kPtr
    lda kPtr+1
    adc kArg+4
    sta kPtr+1
    // remaining -= chunk
    sec
    lda reuFsLen
    sbc kArg+3
    sta reuFsLen
    lda reuFsLen+1
    sbc kArg+4
    sta reuFsLen+1
    // advance to next bank: addr = 0, bank++
    lda #0; sta reuFsFile; sta reuFsFile+1
    inc reuFsFlags
    jmp rdrLoop
rdrLast:
    // remaining < chunk: DMA the remainder at addr/bank, done
    lda reuFsFile; sta kArg
    lda reuFsFile+1; sta kArg+1
    lda reuFsFlags; sta kArg+2
    lda reuFsLen; sta kArg+3
    lda reuFsLen+1; sta kArg+4
    lda #0; sta kArg+5
    jsr kReuToC64
rdrDone:
    // Restore C64 dest from stack
    pla; sta kPtr+1
    pla; sta kPtr
    plp                         // restore caller's I flag
    rts

// ==========================================
//  reuLoad -- load a whole file to C64 (used by `load` command)
// ==========================================
//  Input:  kPtr/kPtr+1 = C64 destination
//          kArg/kArg+1 = name pointer (null-term screen codes)
//  Output: C=0 success (kArg+2/kArg+3 = size)
//          C=1 not found or FS disabled
reuLoad:
    jsr reuFindFile
    bcc rlGo
    sec
    rts
rlGo:
    lda reuFsRemain; sta kArg+6
    lda reuFsRemain+1; sta kArg+7
    jsr reuDmaRange
    lda kArg+6; sta kArg+2      // return size
    lda kArg+7; sta kArg+3
    clc
    rts

// ==========================================
//  reuLoadAtomic -- atomic kReuLoad wrapper
// ==========================================
//  kReuLoad jumps here; runs the find->dma sequence with interrupts
//  disabled so a concurrent FS user cannot clobber reuFsPtr/reuFsRemain
//  between the two helpers (concurrency section 10.2). Preserves the caller's I
//  flag and the C result: callerSR on stack, SEI, jsr reuLoad, capture
//  result C in A bit0, restore callerSR via plp, move C back with lsr.
reuLoadAtomic:
    php                         // caller SR (I flag) on stack
    sei
    jsr reuLoad                 // returns C=0/1 with I still set
    php                         // result SR (C in bit0) on stack
    pla                         // A = result SR
    plp                         // restore caller's SR (I; C discarded)
    lsr                         // C = A bit0 = result carry
    rts
.label reuFsCodeEnd = *
