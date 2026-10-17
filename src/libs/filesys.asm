// ==========================================
//  filesys.asm -- dynamic library "filesys.lib" (lib 1, 9 entries)
// ==========================================
//  format/save/delete/rename/dirEntry + the in-place I/O primitives
//  create/open/readAt/writeAt -- the REU filesystem path, moved out
//  of reu-fs.inc. kReuLoad stays resident.
//  Entry order = call-index order: format(0), save(1), delete(2), rename(3),
//  dirEntry(4), create(5), open(6), readAt(7), writeAt(8).
//  The lib calls the resident FS helpers through fixed jump-table entries
//  (kReuDirPageDma/kReuCmpName/kReuFindFile/kReuFillReu/kReuFromC64/kStrlen)
//  and reads/writes kernel FS state (reuFs* / reuDirBuf) at fixed addresses.
//  All labels are prefixed fsx* to avoid the global namespace shared with
//  the task sources (kernel-data.inc aliases reuFs* are NOT re-declared).

.encoding "screencode_mixed"

// REU_FS_SIGNATURE now comes from src/kernel/kernel-shared.inc, imported
// globally via src/tasks/kernel-data.inc.
.const REU_FS_BANK      = 5

filesysBinStart:
    .word HDR_MAGIC              // +0:  magic 'G','O' (LE)
    .byte KF_RELOCATABLE        // +2:  flags
    .word $5000                  // +3:  origin (page-aligned, nominal)
    .word filesysEnd - filesysCode // +5: code size
    .byte 0, 0, 0                // +7:  data size, ZP base, ZP len
    .word 0                      // +10: reserved (libs consume no libMask)
    .byte filesysNameEnd - filesysName  // +12: name len
filesysName:
    .text "filesys"
filesysNameEnd:
    .word 0                      // fixup_count (16-bit -- written by the build)
    .word 0                      // fixup_offset (written by the build)
    .fill 256 - (* - filesysBinStart), 0

filesysCode:
    .word fsxFormat, fsxSave, fsxDelete, fsxRename, fsxDirEntry
    .word fsxCreate, fsxOpen, fsxReadAt, fsxWriteAt   // entry table (9 entries)

// ==========================================
//  fsxFormat -- kReuFormat (zero dir, write signature/capacity/offset)
// ==========================================
fsxFormat:
    sei
    lda reuFsDirBaseLo; sta kArg
    lda reuFsDirBaseHi; sta kArg+1
    lda reuFsDirBaseBank; sta kArg+2
    lda #$00; sta kArg+3       // length lo
    lda #$40; sta kArg+4       // length hi = $4000 (16KB)
    lda #0
    jsr kReuFillReu
    lda #0
    clc
    jsr kReuDirPageDma
    lda #REU_FS_SIGNATURE; sta reuDirBuf+1
    lda #0; sta reuDirBuf+17
    sta reuDirBuf+18
    sta reuDirBuf+19
    lda reuFsCapacityLo; sta reuDirBuf+20
    lda reuFsCapacityHi; sta reuDirBuf+21
    lda reuFsCapacity2; sta reuDirBuf+22
    lda #0
    sec
    jsr kReuDirPageDma
    lda #$A5; sta reuFsEnabled
    clc
    cli
    rts

// ==========================================
//  fsxSave -- kReuSave (save C64 memory block to REU)
// ==========================================
fsxSave:
    lda kArg; sta reuFsName
    lda kArg+1; sta reuFsName+1

    // Validate name length: 1-11 chars
    lda kArg; sta srcPtr
    lda kArg+1; sta srcPtr+1
    ldy #0
fsxNmLen:
    lda (srcPtr),y
    beq fsxNmDone
    iny
    cpy #12                     // >11 chars?
    bcc fsxNmLen
    lda #3; sta kArg+6          // error: name too long
    sec; rts
fsxNmDone:
    cpy #0
    bne fsxNmOk
    lda #4; sta kArg+6          // error: empty name
    sec; rts
fsxNmOk:

    // Compute size = end - start
    sec
    lda kArg+2; sbc kPtr
    sta kArg+4
    lda kArg+3; sbc kPtr+1
    sta kArg+5
    bcs fsxSz1
    jmp fsxBadSize
fsxSz1:
    ora kArg+4
    bne fsxvGo
    jmp fsxBadSize

fsxvGo:
    sei
    lda reuFsEnabled; cmp #REU_FS_SIGNATURE
    beq fsxFsOk
    jmp fsxFailCli
fsxFsOk:
    lda #$ff; sta reuFsFile
    sta reuFsLen
    lda #0; sta reuFsFlags      // page counter

fsxPageLoop:
    lda reuFsFlags; cmp #64
    bcs fsxScanDoneJmp
    lda reuFsFlags; clc; jsr kReuDirPageDma

    lda reuFsFlags; beq fsxReadSlot0
    jmp fsxNotPage0
fsxReadSlot0:
    lda reuDirBuf+17; sta reuFsPtr
    lda reuDirBuf+18; sta reuFsPtr+1
    lda reuDirBuf+19; sta reuFsPtr+2
fsxNotPage0:

    lda #0; sta kArg+6
fsxEntryLoop:
    lda kArg+6; cmp #8
    bcs fsxPageNextJmp
    lda kArg+6; asl; asl; asl; asl; asl; tay

    lda reuDirBuf+20,y; bne fsxSlotUsed
    lda reuDirBuf+21,y; beq fsxSlotEmpty
fsxSlotUsed:
    lda reuDirBuf,y; and #$80; bne fsxEntryNext
    tya; clc; adc #1; tay
    jsr kReuCmpName
    beq fsxDupFoundJmp
    jmp fsxEntryNext

fsxSlotEmpty:
    lda reuFsFile; cmp #$ff
    bne fsxEntryNext
    lda reuFsFlags; sta reuFsFile
    lda kArg+6; sta reuFsLen

fsxEntryNext:
    inc kArg+6
    jmp fsxEntryLoop

fsxPageNextJmp:
    jmp fsxPageNext

fsxScanDoneJmp:
    jmp fsxScanDone

fsxDupFoundJmp:
    jmp fsxDupFound

fsxPageNext:
    inc reuFsFlags
    jmp fsxPageLoop

fsxDupFound:
    lda #0; sta kArg+6          // error: duplicate
    sec; cli; rts

fsxScanDone:
    lda reuFsFile; cmp #$ff
    bne fsxHaveSlot
    jmp fsxDirFullCli
fsxHaveSlot:

    // Check space: nextFreeOffset(24) + size(16) <= capacity(24)
    clc
    lda reuFsPtr; adc kArg+4; sta kArg+6      // end lo
    lda reuFsPtr+1; adc kArg+5; sta kArg+7    // end mid
    lda reuFsPtr+2; adc #0; sta kArg+2        // end hi
    lda kArg+2
    cmp reuFsCapacity2
    bcc fsxSpaceOk
    bne fsxNoSpaceJmp
    lda kArg+7
    cmp reuFsCapacityHi
    bcc fsxSpaceOk
    bne fsxNoSpaceJmp
    lda kArg+6
    cmp reuFsCapacityLo
    bcc fsxSpaceOk
    beq fsxSpaceOk               // exact fit is OK
fsxNoSpaceJmp:
    jmp fsxNoSpaceCli

fsxSpaceOk:
    // Stash free-slot page + entry (the DMA loop clobbers reuFsFile/
    // reuFsLen/reuFsFlags), then size + C64 source.
    lda reuFsFile; pha          // free-slot page
    lda reuFsLen; pha           // free-slot entry index
    lda kArg+4; sta kArg+6      // size lo (preserved for the entry write)
    lda kArg+5; sta kArg+7      // size hi
    jsr fsxWriteRange           // write at reuFsPtr, len kArg+6/7, src kPtr
    pla; sta reuFsLen           // free-slot entry index
    pla; sta reuFsFile          // free-slot page

    // DMA target directory page into reuDirBuf
    lda reuFsFile; clc; jsr kReuDirPageDma

    lda kArg+6; sta reuFsRemain  // size lo (fsxPutEntry reads size from here)
    lda kArg+7; sta reuFsRemain+1
    jsr fsxPutEntry              // write the entry + DMA the page back

    // Update slot 0: nextFreeOffset += size (24-bit)
    lda #0; clc; jsr kReuDirPageDma
    clc
    lda reuFsPtr; adc kArg+6; sta reuDirBuf+17
    lda reuFsPtr+1; adc kArg+7; sta reuDirBuf+18
    lda reuFsPtr+2; adc #0; sta reuDirBuf+19
    lda #0; sec; jsr kReuDirPageDma

    clc; cli; rts

fsxFailCli:
    sec; cli; rts

fsxBadSize:
    sec; rts

fsxNoSpaceCli:
    lda #1; sta kArg+6          // error: no space
    sec; cli; rts
fsxDirFullCli:
    lda #2; sta kArg+6          // error: dir full
    sec; cli; rts

// ==========================================
//  fsxWriteRange -- multi-bank C64->REU write at a 24-bit FS offset
// ==========================================
//  Input:  reuFsPtr/reuFsPtr+1/reuFsPtr+2 = FS offset (24-bit, 0-based)
//          kArg+6/kArg+7 = length (16-bit)
//          kPtr/kPtr+1   = C64 source
//  Output: kPtr restored; reuFsPtr (3B) + kArg+6/+7 unchanged.
//  Clobbers: A, Y, kArg..kArg+5, reuFsFile, reuFsLen, reuFsFlags
//  Shared by fsxSave and fsxWriteAt (the C64->REU half of the
//  loop; kReuDmaRange is the resident REU->C64 half).
fsxWriteRange:
    lda kPtr; pha
    lda kPtr+1; pha
    :FSToReuAddr(reuFsPtr, reuFsFile, reuFsFile+1, reuFsFlags)
    lda kArg+6
    sta reuFsLen                // remaining lo
    lda kArg+7
    sta reuFsLen+1              // remaining hi
fsxwrLoop:
    lda reuFsLen
    ora reuFsLen+1
    bne fsxwrGo
    jmp fsxwrDone
fsxwrGo:
    lda reuFsFile
    ora reuFsFile+1
    bne fsxwrSplit
    lda #0; sta kArg; sta kArg+1
    lda reuFsFlags; sta kArg+2
    lda reuFsLen; sta kArg+3
    lda reuFsLen+1; sta kArg+4
    lda #0; sta kArg+5
    jsr kReuFromC64
    jmp fsxwrDone
fsxwrSplit:
    sec
    lda #0
    sbc reuFsFile               // chunk lo
    sta kArg+3
    lda #0
    sbc reuFsFile+1             // chunk hi
    sta kArg+4
    lda reuFsLen+1
    cmp kArg+4
    bcc fsxwrLast
    bne fsxwrFull
    lda reuFsLen
    cmp kArg+3
    bcc fsxwrLast
fsxwrFull:
    lda reuFsFile; sta kArg
    lda reuFsFile+1; sta kArg+1
    lda reuFsFlags; sta kArg+2
    lda #0; sta kArg+5
    jsr kReuFromC64
    lda kPtr
    clc
    adc kArg+3
    sta kPtr
    lda kPtr+1
    adc kArg+4
    sta kPtr+1
    sec
    lda reuFsLen
    sbc kArg+3
    sta reuFsLen
    lda reuFsLen+1
    sbc kArg+4
    sta reuFsLen+1
    lda #0; sta reuFsFile; sta reuFsFile+1
    inc reuFsFlags
    jmp fsxwrLoop
fsxwrLast:
    lda reuFsFile; sta kArg
    lda reuFsFile+1; sta kArg+1
    lda reuFsFlags; sta kArg+2
    lda reuFsLen; sta kArg+3
    lda reuFsLen+1; sta kArg+4
    lda #0; sta kArg+5
    jsr kReuFromC64
fsxwrDone:
    pla; sta kPtr+1
    pla; sta kPtr
    rts

// ==========================================
//  fsxPutEntry -- write the 32-byte entry for the free slot
// ==========================================
//  Precondition: reuFsFile = page (already DMA'd into reuDirBuf),
//  reuFsLen = entry index, reuFsPtr = offset (3B), reuFsRemain = size (2B),
//  reuFsName = name ptr. DMAs the page back (write).
//  Clobbers: A, X, Y, srcPtr. Preserves reuFsFile/reuFsLen/reuFsPtr/reuFsRemain.
fsxPutEntry:
    lda reuFsLen; asl; asl; asl; asl; asl; tay
    lda #0; sta reuDirBuf,y      // flags = 0
    tya; clc; adc #1; tax        // X = name start (+1)
    lda reuFsName; sta srcPtr
    lda reuFsName+1; sta srcPtr+1
    ldy #0
fsxpeNameCopy:
    cpy #16; bcs fsxpeNameDone
    lda (srcPtr),y
    beq fsxpeNamePad
    sta reuDirBuf,x
    inx; iny
    bne fsxpeNameCopy
fsxpeNamePad:
    lda #$20
fsxpePadLoop:
    cpy #16; bcs fsxpeNameDone
    sta reuDirBuf,x
    inx; iny
    bne fsxpePadLoop
fsxpeNameDone:
    lda reuFsLen; asl; asl; asl; asl; asl; tay
    lda reuFsPtr; sta reuDirBuf+17,y
    lda reuFsPtr+1; sta reuDirBuf+18,y
    lda reuFsPtr+2; sta reuDirBuf+19,y
    lda reuFsRemain; sta reuDirBuf+20,y
    lda reuFsRemain+1; sta reuDirBuf+21,y
    lda reuFsFile; sec; jsr kReuDirPageDma   // write page back
    rts

// ==========================================
//  fsxCreate -- kReuCreate (create + zero a fixed-size file)
// ==========================================
//  Input:  kArg/kArg+1 = name ptr, kArg+2/kArg+3 = size (16-bit)
//  Output: C=0 created (zeroed); C=1 error, code in kArg+6
//          (0 dup, 1 no space, 2 dir full, 3 name>11, 4 empty, 5 zero size)
fsxCreate:
    lda kArg; sta srcPtr
    lda kArg+1; sta srcPtr+1
    ldy #0
fsxcNmLen:
    lda (srcPtr),y
    beq fsxcNmDone
    iny
    cpy #12
    bcc fsxcNmLen
    lda #3; sta kArg+6          // name too long
    sec; rts
fsxcNmDone:
    cpy #0
    bne fsxcNmOk
    lda #4; sta kArg+6          // empty name
    sec; rts
fsxcNmOk:
    lda kArg+2; ora kArg+3
    bne fsxcSzOk
    lda #5; sta kArg+6          // zero size
    sec; rts
fsxcSzOk:
    sei
    lda reuFsEnabled; cmp #REU_FS_SIGNATURE
    beq fsxcFsOk
    jmp fsxcFail
fsxcFsOk:
    lda kArg; sta reuFsName
    lda kArg+1; sta reuFsName+1
    lda #$ff; sta reuFsFile
    sta reuFsLen
    lda #0; sta reuFsFlags      // page counter
fsxcPageLoop:
    lda reuFsFlags; cmp #64
    bcs fsxcScanDoneJmp
    lda reuFsFlags; clc; jsr kReuDirPageDma
    lda reuFsFlags; beq fsxcReadSlot0
    jmp fsxcNotPage0
fsxcReadSlot0:
    lda reuDirBuf+17; sta reuFsPtr
    lda reuDirBuf+18; sta reuFsPtr+1
    lda reuDirBuf+19; sta reuFsPtr+2
fsxcNotPage0:
    lda #0; sta kArg+6
fsxcEntryLoop:
    lda kArg+6; cmp #8
    bcs fsxcPageNextJmp
    lda kArg+6; asl; asl; asl; asl; asl; tay
    lda reuDirBuf+20,y; bne fsxcSlotUsed
    lda reuDirBuf+21,y; beq fsxcSlotEmpty
fsxcSlotUsed:
    lda reuDirBuf,y; and #$80; bne fsxcEntryNext
    tya; clc; adc #1; tay
    jsr kReuCmpName
    beq fsxcDupFoundJmp
    jmp fsxcEntryNext
fsxcSlotEmpty:
    lda reuFsFile; cmp #$ff
    bne fsxcEntryNext
    lda reuFsFlags; sta reuFsFile
    lda kArg+6; sta reuFsLen
fsxcEntryNext:
    inc kArg+6
    jmp fsxcEntryLoop
fsxcPageNextJmp:
    jmp fsxcPageNext
fsxcScanDoneJmp:
    jmp fsxcScanDone
fsxcDupFoundJmp:
    jmp fsxcDupFound
fsxcPageNext:
    inc reuFsFlags
    jmp fsxcPageLoop
fsxcDupFound:
    lda #0; sta kArg+6          // duplicate
    sec; cli; rts
fsxcScanDone:
    lda reuFsFile; cmp #$ff
    bne fsxcHaveSlot
    jmp fsxcDirFull
fsxcHaveSlot:
    // space check: nextFreeOffset(24) + size(16) <= capacity(24)
    clc
    lda reuFsPtr; adc kArg+2; sta kArg+6      // end lo
    lda reuFsPtr+1; adc kArg+3; sta kArg+7    // end mid
    lda reuFsPtr+2; adc #0; sta kArg+4        // end hi
    lda kArg+4; cmp reuFsCapacity2
    bcc fsxcSpaceOk
    bne fsxcNoSpaceJmp
    lda kArg+7; cmp reuFsCapacityHi
    bcc fsxcSpaceOk
    bne fsxcNoSpaceJmp
    lda kArg+6; cmp reuFsCapacityLo
    bcc fsxcSpaceOk
    beq fsxcSpaceOk
fsxcNoSpaceJmp:
    jmp fsxcNoSpace
fsxcSpaceOk:
    // stash free slot, save size in reuFsRemain, zero-fill, then commit
    lda reuFsFile; pha           // free-slot page
    lda reuFsLen; pha            // free-slot entry
    lda kArg+2; sta reuFsRemain  // size lo
    lda kArg+3; sta reuFsRemain+1
    jsr fsxCreateZero
    pla; sta reuFsLen
    pla; sta reuFsFile
    lda reuFsFile; clc; jsr kReuDirPageDma
    jsr fsxPutEntry
    // slot 0: nextFreeOffset += size
    lda #0; clc; jsr kReuDirPageDma
    clc
    lda reuFsPtr; adc reuFsRemain; sta reuDirBuf+17
    lda reuFsPtr+1; adc reuFsRemain+1; sta reuDirBuf+18
    lda reuFsPtr+2; adc #0; sta reuDirBuf+19
    lda #0; sec; jsr kReuDirPageDma
    clc; cli; rts
fsxcNoSpace:
    lda #1; sta kArg+6
    sec; cli; rts
fsxcDirFull:
    lda #2; sta kArg+6
    sec; cli; rts
fsxcFail:
    lda #6; sta kArg+6
    sec; cli; rts

// ==========================================
//  fsxCreateZero -- zero-fill the new file's region (multi-bank)
// ==========================================
//  Input:  reuFsPtr (3B) = FS offset, reuFsRemain (2B) = size
//  Preserves reuFsPtr + reuFsRemain. Clobbers A, Y, kArg..kArg+4,
//  reuFsFile, reuFsLen, reuFsFlags.
fsxCreateZero:
    :FSToReuAddr(reuFsPtr, reuFsFile, reuFsFile+1, reuFsFlags)
    lda reuFsRemain
    sta reuFsLen                // remaining lo
    lda reuFsRemain+1
    sta reuFsLen+1
fsxczLoop:
    lda reuFsLen
    ora reuFsLen+1
    bne fsxczGo
    rts
fsxczGo:
    lda reuFsFile
    ora reuFsFile+1
    bne fsxczSplit
    lda #0; sta kArg; sta kArg+1
    lda reuFsFlags; sta kArg+2
    lda reuFsLen; sta kArg+3
    lda reuFsLen+1; sta kArg+4
    lda #0
    jsr kReuFillReu
    rts
fsxczSplit:
    sec
    lda #0
    sbc reuFsFile               // chunk lo
    sta kArg+3
    lda #0
    sbc reuFsFile+1             // chunk hi
    sta kArg+4
    lda reuFsLen+1
    cmp kArg+4
    bcc fsxczLast
    bne fsxczFull
    lda reuFsLen
    cmp kArg+3
    bcc fsxczLast
fsxczFull:
    lda reuFsFile; sta kArg
    lda reuFsFile+1; sta kArg+1
    lda reuFsFlags; sta kArg+2
    lda #0
    jsr kReuFillReu
    sec
    lda reuFsLen
    sbc kArg+3
    sta reuFsLen
    lda reuFsLen+1
    sbc kArg+4
    sta reuFsLen+1
    lda #0; sta reuFsFile; sta reuFsFile+1
    inc reuFsFlags
    jmp fsxczLoop
fsxczLast:
    lda reuFsFile; sta kArg
    lda reuFsFile+1; sta kArg+1
    lda reuFsFlags; sta kArg+2
    lda reuFsLen; sta kArg+3
    lda reuFsLen+1; sta kArg+4
    lda #0
    jsr kReuFillReu
    rts

// ==========================================
//  fsxOpen -- kReuOpen (resolve name -> 5-byte file record)
// ==========================================
//  Input:  kArg/kArg+1 = name ptr, kPtr/kPtr+1 = 5-byte record buffer
//  Output: C=0 -- record [size lo,size hi,base lo,base mid,base hi];
//          C=1 -- code 6 not found / FS disabled
fsxOpen:
    sei
    jsr kReuFindFile            // C=0: reuFsPtr(3B)=base, reuFsRemain(2B)=size
    bcc fsxoFound
    lda #6; sta kArg+6
    sec; cli; rts
fsxoFound:
    ldy #0
    lda reuFsRemain; sta (kPtr),y     // size lo
    iny
    lda reuFsRemain+1; sta (kPtr),y   // size hi
    iny
    lda reuFsPtr; sta (kPtr),y        // base lo
    iny
    lda reuFsPtr+1; sta (kPtr),y      // base mid
    iny
    lda reuFsPtr+2; sta (kPtr),y      // base hi
    clc; cli; rts

// ==========================================
//  fsxReadAt -- kReuReadAt (read len bytes at pos, EOF-clamped)
// ==========================================
//  Input:  kArg/kArg+1 = record ptr, kArg+2/+3 = pos (16-bit),
//          kArg+6/+7 = len (16-bit), kPtr/kPtr+1 = C64 destination
//  Output: C=0 -- kArg+6/+7 = actual bytes read (0 = EOF, < len = clamped)
//          C=1 -- code 6 (FS disabled)
fsxReadAt:
    sei
    lda reuFsEnabled; cmp #REU_FS_SIGNATURE
    beq fsxraFsOk
    jmp fsxraFail
fsxraFsOk:
    lda kArg; sta srcPtr
    lda kArg+1; sta srcPtr+1
    ldy #0
    lda (srcPtr),y; sta reuFsRemain      // size lo
    iny
    lda (srcPtr),y; sta reuFsRemain+1    // size hi
    iny
    lda (srcPtr),y; sta reuFsPtr         // base lo
    iny
    lda (srcPtr),y; sta reuFsPtr+1       // base mid
    iny
    lda (srcPtr),y; sta reuFsPtr+2       // base hi

    // remaining = size - pos (16-bit)
    sec
    lda reuFsRemain; sbc kArg+2; sta reuFsFile     // rem lo
    lda reuFsRemain+1; sbc kArg+3; sta reuFsFile+1 // rem hi
    bcc fsxraEof                 // pos > size -> EOF (count 0)

    // count = min(len, remaining)
    lda kArg+7; cmp reuFsFile+1
    bcc fsxraGo                  // len hi < rem hi -> use len
    bne fsxraUseRem              // len hi > rem hi -> use rem
    lda kArg+6; cmp reuFsFile
    bcc fsxraGo                  // len lo < rem lo -> use len
fsxraUseRem:
    lda reuFsFile; sta kArg+6
    lda reuFsFile+1; sta kArg+7
fsxraGo:
    lda kArg+6; ora kArg+7
    bne fsxraDma
    clc; cli; rts                // count 0 (len was 0)
fsxraDma:
    clc
    lda reuFsPtr; adc kArg+2; sta reuFsPtr
    lda reuFsPtr+1; adc kArg+3; sta reuFsPtr+1
    lda reuFsPtr+2; adc #0; sta reuFsPtr+2
    jsr kReuDmaRange             // preserves reuFsPtr/reuFsRemain/kArg+6/7
    clc; cli; rts
fsxraEof:
    lda #0; sta kArg+6; sta kArg+7
    clc; cli; rts
fsxraFail:
    lda #6; sta kArg+6
    sec; cli; rts

// ==========================================
//  fsxWriteAt -- kReuWriteAt (write len bytes at pos, reject past-EOF)
// ==========================================
//  Input:  kArg/kArg+1 = record ptr, kArg+2/+3 = pos, kArg+6/+7 = len,
//          kPtr/kPtr+1 = C64 source
//  Output: C=0 written; C=1 -- code 6 (FS disabled) or 7 (past EOF)
fsxWriteAt:
    sei
    lda reuFsEnabled; cmp #REU_FS_SIGNATURE
    beq fsxwaFsOk
    jmp fsxwaFail
fsxwaFsOk:
    lda kArg; sta srcPtr
    lda kArg+1; sta srcPtr+1
    ldy #0
    lda (srcPtr),y; sta reuFsRemain      // size lo
    iny
    lda (srcPtr),y; sta reuFsRemain+1    // size hi
    iny
    lda (srcPtr),y; sta reuFsPtr         // base lo
    iny
    lda (srcPtr),y; sta reuFsPtr+1       // base mid
    iny
    lda (srcPtr),y; sta reuFsPtr+2       // base hi

    // end = pos + len (16-bit + carry)
    clc
    lda kArg+2; adc kArg+6; sta reuFsFile      // end lo
    lda kArg+3; adc kArg+7; sta reuFsFile+1    // end hi
    bcs fsxwaPast                  // > $FFFF -> past (size <= $FFFF)
    lda reuFsFile+1; cmp reuFsRemain+1
    bcc fsxwaOk
    bne fsxwaPast
    lda reuFsFile; cmp reuFsRemain
    beq fsxwaOk                    // end == size -> OK
    bcc fsxwaOk
fsxwaPast:
    lda #7; sta kArg+6
    sec; cli; rts
fsxwaOk:
    clc
    lda reuFsPtr; adc kArg+2; sta reuFsPtr
    lda reuFsPtr+1; adc kArg+3; sta reuFsPtr+1
    lda reuFsPtr+2; adc #0; sta reuFsPtr+2
    jsr fsxWriteRange
    clc; cli; rts
fsxwaFail:
    lda #6; sta kArg+6
    sec; cli; rts

// ==========================================
//  fsxDelete -- kReuDelete (mark named entry deleted)
// ==========================================
fsxDelete:
    sei
    lda reuFsEnabled; cmp #REU_FS_SIGNATURE
    beq fsxdFsOk
    jmp fsxdFail
fsxdFsOk:
    lda kArg; sta reuFsName
    lda kArg+1; sta reuFsName+1
    lda #0; sta reuFsFlags
fsxdPageLoop:
    lda reuFsFlags; cmp #64
    bcc fsxdDoPage
    jmp fsxdNotFound
fsxdDoPage:
    lda reuFsFlags; clc; jsr kReuDirPageDma
    lda #0; sta kArg+6
fsxdEntryLoop:
    lda kArg+6; cmp #8; bcs fsxdPageNext
    lda kArg+6; asl; asl; asl; asl; asl; tay
    lda reuDirBuf+20,y; bne fsxdChkDel
    lda reuDirBuf+21,y; beq fsxdEntryNext
fsxdChkDel:
    lda reuDirBuf,y; and #$80; bne fsxdEntryNext
    tya; clc; adc #1; tay
    jsr kReuCmpName
    bne fsxdEntryNext
    jmp fsxdFound
fsxdEntryNext:
    inc kArg+6; jmp fsxdEntryLoop
fsxdPageNext:
    inc reuFsFlags; jmp fsxdPageLoop
fsxdFound:
    // Set deleted flag, clear name to spaces
    lda kArg+6; asl; asl; asl; asl; asl; tay
    lda reuDirBuf,y; ora #$80; sta reuDirBuf,y  // set b7
    tya; clc; adc #1; tax        // X = name offset
    ldy #0; lda #$20
fsxdClrName:
    cpy #16; bcs fsxdClrDone
    sta reuDirBuf,x
    inx; iny; bne fsxdClrName
fsxdClrDone:
    // Write page back
    lda reuFsFlags; sec; jsr kReuDirPageDma
    clc; cli; rts
fsxdNotFound:
fsxdFail:
    sec; cli; rts

// ==========================================
//  fsxRename -- kReuRename (rename a directory entry)
// ==========================================
fsxRename:
    // Validate old name: 1-11 chars
    lda kArg; sta kPtr
    lda kArg+1; sta kPtr+1
    jsr kStrlen
    bne fsxrV1
    jmp fsxrBadName               // empty
fsxrV1:
    cmp #12
    bcc fsxrV2
    jmp fsxrBadName               // too long
fsxrV2:
    // Validate new name
    lda kArg+2; sta kPtr
    lda kArg+3; sta kPtr+1
    jsr kStrlen
    bne fsxrV3
    jmp fsxrBadName
fsxrV3:
    cmp #12
    bcc fsxrDiff
    jmp fsxrBadName
fsxrDiff:
    // Same pointer for both names -> nothing to do
    lda kArg; cmp kArg+2
    bne fsxrAtomic
    lda kArg+1; cmp kArg+3
    beq fsxrNoop
fsxrAtomic:
    php
    sei
fsxrScanOld:
    // reuFindFile takes its input from kArg/kArg+1, returns A=page X=entry
    jsr kReuFindFile
    bcc fsxrOldFound
    lda #0; sta kArg+6          // old not found
    plp
    sec; rts
fsxrOldFound:
    stx reuFsLen                // stash entry index (X)
    sta reuFsFile               // stash page (A)
    // New-name scan: swap the new name into kArg/kArg+1
    lda kArg+2; sta kArg
    lda kArg+3; sta kArg+1
    jsr kReuFindFile
    bcs fsxrNewFree
    lda #1; sta kArg+6          // new name exists
    plp
    sec; rts
fsxrNewFree:
    // (still SEI'd from fsxrAtomic)
    lda reuFsFile; clc; jsr kReuDirPageDma
    // X = name field offset within the page
    lda reuFsLen
    asl; asl; asl; asl; asl
    clc; adc #1
    tax
    lda kArg+2; sta srcPtr
    lda kArg+3; sta srcPtr+1
    ldy #0
fsxrCopy:
    cpy #16; bcs fsxrCopyDone
    lda (srcPtr),y
    beq fsxrPad
    sta reuDirBuf,x
    inx; iny
    bne fsxrCopy
fsxrPad:
    lda #$20
fsxrPadLoop:
    cpy #16; bcs fsxrCopyDone
    sta reuDirBuf,x
    inx; iny
    bne fsxrPadLoop
fsxrCopyDone:
    lda reuFsFile; sec; jsr kReuDirPageDma   // write page back
    plp
    clc; rts
fsxrNoop:
    clc; rts
fsxrBadName:
    lda #2; sta kArg+6          // bad name
    sec; rts

// ==========================================
//  fsxDirEntry -- kReuDirEntry (read a single directory entry)
// ==========================================
//  Input:  A = entry index lo, X = entry index hi (0/1)
//          kPtr/kPtr+1 = C64 buffer for name (12 bytes: 11 chars + null)
//  Output: C=0 success (buffer filled, kArg/kArg+1 = size, kArg+2 = flags,
//                      kArg+3/kArg+4/kArg+5 = data offset lo/mid/hi --
//                      file entries; slot 0 returns nextFreeOffset there)
//          C=1 empty/deleted slot, entry >= 512, or FS disabled
//  Clobbers: A, X, Y, kArg+2..kArg+7
fsxDirEntry:
    sei
    stx kArg+7                  // entry hi (0/1)
    sta kArg+6                  // entry lo

    lda reuFsEnabled; cmp #REU_FS_SIGNATURE
    beq fsxdeOk
    jmp fsxdeFail
fsxdeOk:
    // out of range (entry >= 512)?
    lda kArg+7
    cmp #2
    bcc fsxdeInRange            // hi < 2 -> 0..511
    jmp fsxdeSkip
fsxdeInRange:
    lda kArg+7
    bne fsxdeNormal             // hi == 1 -> entries 256..511
    lda kArg+6
    bne fsxdeNormal             // lo != 0

    // --- Slot 0: return the allocator's nextFreeOffset (24-bit) ---
    lda #0; clc; jsr kReuDirPageDma
    lda reuDirBuf+17; sta kArg+3    // nextFreeOffset lo
    lda reuDirBuf+18; sta kArg+4    // mid
    lda reuDirBuf+19; sta kArg+5    // hi
    ldy #0
fsxdeFreeLp:
    lda fsxdeFreeStr,y; sta (kPtr),y
    beq fsxdeFreeDone
    iny
    bne fsxdeFreeLp
fsxdeFreeDone:
    lda #0; sta kArg+2          // flags = 0
    clc; cli; rts

fsxdeNormal:
    // page = (entryHi << 5) | (entryLo >> 3)
    lda kArg+7
    asl; asl; asl; asl; asl     // hi << 5 (0 or 32)
    sta kArg+7
    lda kArg+6
    lsr; lsr; lsr               // lo >> 3 (0..31)
    ora kArg+7                  // page (0..63)
    clc; jsr kReuDirPageDma
    // in-page offset = (entryLo & 7) << 5
    lda kArg+6
    and #$07
    asl; asl; asl; asl; asl
    tay                         // Y = offset in reuDirBuf

    // Check size != 0
    lda reuDirBuf+20,y; bne fsxdeNotFree
    lda reuDirBuf+21,y; bne fsxdeNotFree
    // Empty slot
fsxdeSkip:
    sec; cli; rts
fsxdeNotFree:
    // Check deleted flag (b7)
    lda reuDirBuf,y; and #$80
    bne fsxdeSkip               // deleted -> treat as empty

fsxdeHasEntry:
    // Copy name (11 bytes from offset+1, space-trimmed -> null-term)
    tya; clc; adc #1            // offset + 1 = name start
    tax                         // X = source offset in reuDirBuf
    ldy #0
fsxdeNameLp:
    cpy #11; bcs fsxdeNameDone
    lda reuDirBuf,x
    cmp #$20; beq fsxdeNameDone // stop at first space
    sta (kPtr),y
    inx; iny
    bne fsxdeNameLp
fsxdeNameDone:
    lda #0; sta (kPtr),y        // null-terminate

    // Read flags, size, and 24-bit data offset
    lda kArg+6
    and #$07
    asl; asl; asl; asl; asl
    tay
    lda reuDirBuf,y; sta kArg+2 // flags
    lda reuDirBuf+20,y; sta kArg   // size lo
    lda reuDirBuf+21,y; sta kArg+1 // size hi
    lda reuDirBuf+17,y; sta kArg+3 // data offset lo
    lda reuDirBuf+18,y; sta kArg+4 // data offset mid
    lda reuDirBuf+19,y; sta kArg+5 // data offset hi
    clc; cli; rts

fsxdeFail:
    sec; cli; rts

fsxdeFreeStr:
    .text "(free)"
    .byte 0

.label filesysEnd = *
