// ==========================================
//  copy.inc -- REU-FS file copy body
// ==========================================
//  "copy <src> <dst>" copies a file on the shell's shared screen, then
//  exits. Parses two names from cmdArgsBuf, opens src (kReuOpen),
//  mallocs 4 pages of scratch, creates dst at the src size (kReuCreate
//  code 0 = duplicate -> "? exists"), opens dst for its record, then
//  streams src -> C64 scratch -> dst in 1024-byte chunks (REU->REU DMA is
//  impossible, so each chunk is REU->C64 then C64->REU). Handles any file
//  size the FS can store (<= $FFFF) with 16-bit position/remaining math.
//  taskParent/reuFsEnabled/cmdArgsBuf come from src/tasks/kernel-data.inc
//  (imported globally by tools/assemble-tasks.asm).

.const CPY_SRC     = 0     // 5 bytes: src file record (size lo/hi, base lo/mid/hi)
.const CPY_DST     = 5     // 5 bytes: dst file record
.const CPY_POS     = 10    // 2 bytes: intra-file write position
.const CPY_REM     = 12    // 2 bytes: bytes still to copy
.const CPY_LEN     = 14    // 2 bytes: current chunk length
.const CPY_SCR     = 16    // 2 bytes: scratch block pointer
.const CPY_SRCNAME = 18    // 2 bytes: src name pointer
.const CPY_DSTNAME = 20    // 2 bytes: dst name pointer
.const CPY_CHUNK   = 1024  // scratch block size (4 pages)

copy:
    // owner = taskParent[own slot] (the shell)
    lda #SYS_GETTID
    jsr syscall                 // gettid -> A
    tax
    lda taskParent,x            // shell slot
    tax
    jsr kShareScreen            // X = shell (preserved); C=0 ok
    bcc cpScr
    lda #SYS_EXIT
    jsr syscall                 // no screen -- exit
cpScr:
    jsr kLockScreen
    lda reuFsEnabled
    cmp #$A5
    beq cpFs
    lda #<cpNoFs
    sta kPtr
    lda #>cpNoFs
    sta kPtr+1
    jsr kPrintString
    jmp cpNewlines
cpFs:
    // --- parse src name from cmdArgsBuf ---
    ldy #0
cpSk1:
    lda cmdArgsBuf,y
    bne cpSk1Chk
    jmp cpBad                   // no args
cpSk1Chk:
    cmp #$20
    bne cpN1
    iny
    bne cpSk1
cpN1:
    tya
    clc
    adc #<cmdArgsBuf
    sta kArg                    // kArg/kArg+1 = src name ptr
    lda #>cmdArgsBuf
    adc #0
    sta kArg+1
cpN1Scan:
    lda cmdArgsBuf,y
    bne cpN1Chk
    jmp cpBad                   // line ended -- no dst name
cpN1Chk:
    cmp #$20
    beq cpN1Got
    iny
    cpy #39
    bcc cpN1Scan
cpN1Got:
    lda #0
    sta cmdArgsBuf,y            // null-terminate src name
    iny
    // --- parse dst name ---
cpSk2:
    cpy #40
    bcc cpSk2Chk
    jmp cpBad                   // buffer exhausted -- no room for a name
cpSk2Chk:
    lda cmdArgsBuf,y
    bne cpSk2Sp
    jmp cpBad                   // missing dst name
cpSk2Sp:
    cmp #$20
    bne cpN2
    iny
    bne cpSk2
cpN2:
    tya
    clc
    adc #<cmdArgsBuf
    sta kArg+2                  // kArg+2/+3 = dst name ptr
    lda #>cmdArgsBuf
    adc #0
    sta kArg+3
cpN2Scan:
    lda cmdArgsBuf,y
    beq cpN2Got
    cmp #$20
    beq cpN2Got
    iny
    cpy #39
    bcc cpN2Scan
cpN2Got:
    lda #0
    sta cmdArgsBuf,y            // null-terminate dst name
    // stash name pointers (every FS call clobbers kArg)
    ldy #CPY_SRCNAME
    lda kArg
    sta (dataPtr),y
    iny
    lda kArg+1
    sta (dataPtr),y
    ldy #CPY_DSTNAME
    lda kArg+2
    sta (dataPtr),y
    iny
    lda kArg+3
    sta (dataPtr),y
    // validate src name length (1..11)
    ldy #CPY_SRCNAME
    lda (dataPtr),y
    sta kPtr
    iny
    lda (dataPtr),y
    sta kPtr+1
    jsr cpChkLen
    bcc cpValDst
    jmp cpBad
cpValDst:
    ldy #CPY_DSTNAME
    lda (dataPtr),y
    sta kPtr
    iny
    lda (dataPtr),y
    sta kPtr+1
    jsr cpChkLen
    bcc cpOpenSrc
    jmp cpBad

    // --- open src (existence + record) ---
cpOpenSrc:
    lda dataPtr
    clc
    adc #CPY_SRC
    sta kPtr
    lda dataPtr+1
    adc #0
    sta kPtr+1
    ldy #CPY_SRCNAME
    lda (dataPtr),y
    sta kArg
    iny
    lda (dataPtr),y
    sta kArg+1
    jsr kReuOpen
    bcc cpHaveSrc
    jmp cpNotFound

cpHaveSrc:
    // size = record[0..1] -> CPY_REM
    ldy #CPY_SRC
    lda (dataPtr),y
    ldy #CPY_REM
    sta (dataPtr),y
    ldy #CPY_SRC+1
    lda (dataPtr),y
    ldy #CPY_REM+1
    sta (dataPtr),y

    // --- malloc 4 pages of scratch ---
    lda #4
    jsr kMalloc
    bcc cpHaveScr
    jmp cpNoMem
cpHaveScr:
    ldy #CPY_SCR
    lda kPtr
    sta (dataPtr),y
    iny
    lda kPtr+1
    sta (dataPtr),y

    // --- create dst at src size (code 0 = duplicate -> "exists") ---
    ldy #CPY_DSTNAME
    lda (dataPtr),y
    sta kArg
    iny
    lda (dataPtr),y
    sta kArg+1
    ldy #CPY_REM
    lda (dataPtr),y
    sta kArg+2
    ldy #CPY_REM+1
    lda (dataPtr),y
    sta kArg+3
    jsr kReuCreate
    bcc cpCreated
    lda kArg+6                  // 0 dup, 1 no space, 2 dir full
    beq cpDupJ
    cmp #1
    beq cpSpaceJ
    cmp #2
    beq cpFullJ
    jmp cpBad
cpDupJ:
    jmp cpExists
cpSpaceJ:
    jmp cpNoSpace
cpFullJ:
    jmp cpDirFull

cpCreated:
    // --- open dst for its record (base offset unknown until now) ---
    lda dataPtr
    clc
    adc #CPY_DST
    sta kPtr
    lda dataPtr+1
    adc #0
    sta kPtr+1
    ldy #CPY_DSTNAME
    lda (dataPtr),y
    sta kArg
    iny
    lda (dataPtr),y
    sta kArg+1
    jsr kReuOpen
    bcc cpOpenOk
    jmp cpFail
cpOpenOk:
    // pos = 0
    ldy #CPY_POS
    lda #0
    sta (dataPtr),y
    iny
    sta (dataPtr),y

// --- copy loop: stream size bytes in <=1024-byte chunks ---
cpLoop:
    ldy #CPY_REM
    lda (dataPtr),y
    ldy #CPY_REM+1
    ora (dataPtr),y
    bne cpMore
    jmp cpSuccess               // all bytes copied
cpMore:
    // chunk = min(CPY_CHUNK, rem)
    ldy #CPY_REM+1
    lda (dataPtr),y
    cmp #>CPY_CHUNK
    bcc cpChunkSmall            // rem < 1024 -> chunk = rem
    lda #<CPY_CHUNK
    ldy #CPY_LEN
    sta (dataPtr),y
    lda #>CPY_CHUNK
    iny
    sta (dataPtr),y
    jmp cpRead
cpChunkSmall:
    ldy #CPY_REM
    lda (dataPtr),y
    ldy #CPY_LEN
    sta (dataPtr),y
    ldy #CPY_REM+1
    lda (dataPtr),y
    ldy #CPY_LEN+1
    sta (dataPtr),y

cpRead:
    // kReuReadAt(src record, pos, len) -> scratch
    lda dataPtr
    clc
    adc #CPY_SRC
    sta kArg
    lda dataPtr+1
    adc #0
    sta kArg+1
    ldy #CPY_POS
    lda (dataPtr),y
    sta kArg+2
    ldy #CPY_POS+1
    lda (dataPtr),y
    sta kArg+3
    ldy #CPY_LEN
    lda (dataPtr),y
    sta kArg+6
    ldy #CPY_LEN+1
    lda (dataPtr),y
    sta kArg+7
    ldy #CPY_SCR
    lda (dataPtr),y
    sta kPtr
    iny
    lda (dataPtr),y
    sta kPtr+1
    jsr kReuReadAt
    bcc cpWrite
    jmp cpFail

cpWrite:
    // kReuWriteAt(dst record, pos, len) <- scratch
    lda dataPtr
    clc
    adc #CPY_DST
    sta kArg
    lda dataPtr+1
    adc #0
    sta kArg+1
    ldy #CPY_POS
    lda (dataPtr),y
    sta kArg+2
    ldy #CPY_POS+1
    lda (dataPtr),y
    sta kArg+3
    ldy #CPY_LEN
    lda (dataPtr),y
    sta kArg+6
    ldy #CPY_LEN+1
    lda (dataPtr),y
    sta kArg+7
    ldy #CPY_SCR
    lda (dataPtr),y
    sta kPtr
    iny
    lda (dataPtr),y
    sta kPtr+1
    jsr kReuWriteAt
    bcc cpAdvance
    jmp cpFail

cpAdvance:
    // pos += len ; rem -= len (16-bit)
    ldy #CPY_POS
    lda (dataPtr),y
    ldy #CPY_LEN
    clc
    adc (dataPtr),y
    ldy #CPY_POS
    sta (dataPtr),y
    ldy #CPY_POS+1
    lda (dataPtr),y
    ldy #CPY_LEN+1
    adc (dataPtr),y
    ldy #CPY_POS+1
    sta (dataPtr),y
    ldy #CPY_REM
    lda (dataPtr),y
    ldy #CPY_LEN
    sec
    sbc (dataPtr),y
    ldy #CPY_REM
    sta (dataPtr),y
    ldy #CPY_REM+1
    lda (dataPtr),y
    ldy #CPY_LEN+1
    sbc (dataPtr),y
    ldy #CPY_REM+1
    sta (dataPtr),y
    jmp cpLoop

// --- name length check (kPtr = name): C=0 ok (1..11), C=1 bad ---
cpChkLen:
    ldy #0
cpChkLp:
    lda (kPtr),y
    beq cpChkEnd
    iny
    cpy #12
    bcc cpChkLp
    sec                         // >= 12 chars
    rts
cpChkEnd:
    tya
    bne cpChkOk
    sec                         // empty
    rts
cpChkOk:
    clc
    rts

// --- exits ---
cpSuccess:
    lda #<cpOk
    sta kPtr
    lda #>cpOk
    sta kPtr+1
    jmp cpExit
cpFail:
    lda #<cpFailStr
    sta kPtr
    lda #>cpFailStr
    sta kPtr+1
    jmp cpExit
cpNotFound:
    lda #<cpNf
    sta kPtr
    lda #>cpNf
    sta kPtr+1
    jmp cpExit
cpExists:
    lda #<cpEx
    sta kPtr
    lda #>cpEx
    sta kPtr+1
    jmp cpExit
cpNoMem:
    lda #<cpMem
    sta kPtr
    lda #>cpMem
    sta kPtr+1
    jmp cpExit
cpNoSpace:
    lda #<cpNoSpaceStr
    sta kPtr
    lda #>cpNoSpaceStr
    sta kPtr+1
    jmp cpExit
cpDirFull:
    lda #<cpDirFullStr
    sta kPtr
    lda #>cpDirFullStr
    sta kPtr+1
    jmp cpExit
cpBad:
    lda #<cpBadStr
    sta kPtr
    lda #>cpBadStr
    sta kPtr+1
    jmp cpExit
cpExit:
    jsr kPrintString
cpNewlines:
    jsr kNewline
    jsr kNewline
cpUnlock:
    jsr kUnlockScreen
    lda #SYS_EXIT
    jsr syscall                 // exit -- never returns

.encoding "screencode_mixed"
cpOk:      .text "copied";          .byte 0
cpNf:      .text "? not found";     .byte 0
cpEx:      .text "? exists";        .byte 0
cpBadStr:  .text "? bad name";      .byte 0
cpNoFs:    .text "no fs";           .byte 0
cpMem:     .text "? out of memory"; .byte 0
cpNoSpaceStr: .text "? no space";   .byte 0
cpDirFullStr: .text "? dir full";   .byte 0
cpFailStr: .text "? copy failed";   .byte 0
