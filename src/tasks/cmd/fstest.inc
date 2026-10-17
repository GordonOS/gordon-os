// ==========================================
//  fstest.inc -- REU FS bank-boundary self-test body
// ==========================================
//  A file may straddle an FS bank boundary ($10000) - the REU address space
//  is linear in the FS offset, but the DMA length register is 16-bit, so
//  every transfer is clamped to its bank. Both of the failure modes that live
//  exactly here -- a dropped 24-bit carry and a dropped bank byte -- were once
//  caught only by manual runs. This task makes the paths
//  checkable in seconds: `fstest`.
//
//  Window position: pos = $2FF8 - (record base & $FFFF). The REU data base
//  is bank 5 $D000, so an FS offset O is transferred at linear address
//  O + $5D000 and wraps a bank when (O + $D000) mod $10000 rolls over -
//  i.e. at offsets ending $3000, not $0000. A window whose FS offset ends
//  $2FF8 therefore starts 8 bytes before the wrap, so its 16 bytes ALWAYS
//  span a bank boundary, whatever the file's base is. The file only has to
//  be long enough to contain it: the seeder (tools/build-reu.ps1) places
//  the asset fscross.bin so that this window lands in its middle. If that
//  does not hold (a compacted FS, or the asset was deleted) the first
//  read comes up short and the test prints "skipped".
//
//  Oracle: the asset's bytes are generated (byte j = j & $FF), so the
//  expected window is just "start byte, +1, +2, ..." and nothing has to be
//  stored. The write check writes the COMPLEMENT of the window back over
//  it - writing the same bytes would be invisible if the write landed in
//  the wrong place, since every offset in the file holds pattern bytes.
//  The original bytes are then restored, so the asset is unchanged and the
//  test is repeatable.
//
//  Scratch is in this task's own code pages (absolute, relocated by the
//  loader's fixups). Non-reentrant, so a fresh load always starts clean.

.const FT_WIN = 16

fstest:
    // --- attach to the shell's screen (ps/copy protocol) ---
    lda #SYS_GETTID
    jsr syscall
    tax
    lda taskParent,x
    tax
    jsr kShareScreen
    bcc ftScr
    lda #SYS_EXIT
    jsr syscall
ftScr:
    jsr kLockScreen

    // --- open the straddling asset ---
    lda #<ftFsName
    sta kArg
    lda #>ftFsName
    sta kArg+1
    lda #<ftRec
    sta kPtr
    lda #>ftRec
    sta kPtr+1
    jsr kReuOpen
    bcc ftOpened
    lda #<ftSNoFile
    sta kPtr
    lda #>ftSNoFile
    sta kPtr+1
    jsr kPrintString
    jsr kNewline
    jmp ftDone

ftOpened:
    jsr ftBase                  // ftPos = $2FF8 - (base & $FFFF)

    // ================= 1: read across the boundary =================
    lda #<ftSRead
    sta kPtr
    lda #>ftSRead
    sta kPtr+1
    jsr kPrintString
    lda #<ftBuf0
    sta kPtr
    lda #>ftBuf0
    sta kPtr+1
    jsr ftRead                  // -> ftBuf0 (16 bytes) or a short read
    bcs ftReadShort
    lda #<ftBuf0               // (kReuDmaRange restores kPtr -- do not rely on it)
    sta kPtr
    lda #>ftBuf0
    sta kPtr+1
    lda ftPos
    jsr ftVerify                // ftBuf0 must be pattern(start = ftPos lo)
    jmp ftReadOut
ftReadShort:
    sec
ftReadOut:
    jsr ftStatus                // prints ok/FAIL + newline, C = result
    bcc ftDoWrite
    jmp ftSkip                  // no window -> the rest is meaningless

ftDoWrite:
    // ================= 2: write the complement across ==============
    lda #<ftSWrite
    sta kPtr
    lda #>ftSWrite
    sta kPtr+1
    jsr kPrintString
    ldy #0
ftCplLp:
    lda ftBuf0,y
    eor #$FF                    // complement -- distinguishable from the file
    sta ftBuf,y
    iny
    cpy #FT_WIN
    bcc ftCplLp
    lda #<ftBuf
    sta kPtr
    lda #>ftBuf
    sta kPtr+1
    jsr ftWrite
    bcs ftWriteBad
    lda #<ftBuf1
    sta kPtr
    lda #>ftBuf1
    sta kPtr+1
    jsr ftRead                  // read back
    bcs ftWriteBad
    jsr ftCompare               // ftBuf1 must equal what we wrote (ftBuf)
    jmp ftWriteOut
ftWriteBad:
    sec
ftWriteOut:
    jsr ftStatus
    bcc ftWrOk
    jmp ftVerdict               // a failed write leaves the asset dirty
ftWrOk:

    // ================= 3: restore the original bytes ================
    lda #<ftSRest
    sta kPtr
    lda #>ftSRest
    sta kPtr+1
    jsr kPrintString
    lda #<ftBuf0
    sta kPtr
    lda #>ftBuf0
    sta kPtr+1
    jsr ftWrite                 // original bytes back
    bcs ftRestBad
    lda #<ftBuf1
    sta kPtr
    lda #>ftBuf1
    sta kPtr+1
    jsr ftRead
    bcs ftRestBad
    lda #<ftBuf1               // verify the read-back, not a saved copy
    sta kPtr
    lda #>ftBuf1
    sta kPtr+1
    lda ftPos
    jsr ftVerify                // pattern again
    jmp ftRestOut
ftRestBad:
    sec
ftRestOut:
    jsr ftStatus

    // ================= 4: the window after the write ================
    // (catches a forward spill from either write)
    lda #<ftSPost
    sta kPtr
    lda #>ftSPost
    sta kPtr+1
    jsr kPrintString
    clc
    lda ftPos
    adc #FT_WIN
    sta ftPos
    lda ftPos+1
    adc #0
    sta ftPos+1
    bcs ftPostBad               // index wrapped -- nothing to compare
    lda #<ftBuf1
    sta kPtr
    lda #>ftBuf1
    sta kPtr+1
    jsr ftRead
    bcs ftPostBad
    lda #<ftBuf1               // verify the read-back, not a saved copy
    sta kPtr
    lda #>ftBuf1
    sta kPtr+1
    lda ftPos
    jsr ftVerify
    jmp ftPostOut
ftPostBad:
    sec
ftPostOut:
    jsr ftStatus

    // ================= verdict =====================================
ftVerdict:
    lda #<ftSVer
    sta kPtr
    lda #>ftSVer
    sta kPtr+1
    jsr kPrintString
    lda ftBad
    beq ftVerdictOk
    lda #<ftSFail
    sta kPtr
    lda #>ftSFail
    sta kPtr+1
    jsr kPrintString
    jsr kNewline
    jmp ftDone
ftVerdictOk:
    lda #<ftSOk
    sta kPtr
    lda #>ftSOk
    sta kPtr+1
    jsr kPrintString
    jsr kNewline
    jmp ftDone

ftSkip:
    lda #<ftSSkip
    sta kPtr
    lda #>ftSSkip
    sta kPtr+1
    jsr kPrintString
    jsr kNewline
    jmp ftDone
ftDone:
    jsr kUnlockScreen
    lda #SYS_EXIT
    jsr syscall                 // never returns

// --- ftBase -- ftPos = $2FF8 - (record base & $FFFF) ---
//  $2FF8 + $D000 = $FFF8, so the 16-byte window starting there ends 8 bytes
//  past the bank wrap: the transfer spans two banks and must be chunked.
//  Two-step 16-bit subtract: no `sec` between the two sbc's.
//  Clobbers A, and C on exit.
ftBase:
    sec
    lda #$F8
    sbc ftRec+2                 // - base lo
    sta ftPos
    lda #$2F
    sbc ftRec+3                 // - base mid - borrow
    sta ftPos+1
    rts

// --- ftRead -- read FT_WIN bytes at ftPos into (kPtr) ---
//  Input: kPtr = destination. Output: C=0 ok (exactly FT_WIN bytes),
//  C=1 error or short read. Clobbers A, Y, kArg..kArg+7.
ftRead:
    lda #<ftRec
    sta kArg
    lda #>ftRec
    sta kArg+1
    lda ftPos
    sta kArg+2
    lda ftPos+1
    sta kArg+3
    lda #FT_WIN
    sta kArg+6
    lda #0
    sta kArg+7
    jsr kReuReadAt
    bcs ftReadErr
    lda kArg+6                  // actual length -- must be the full window
    cmp #FT_WIN
    bne ftReadErr
    clc
    rts
ftReadErr:
    sec
    rts

// --- ftWrite -- write FT_WIN bytes at ftPos from (kPtr) ---
//  Input: kPtr = source. Output: C=0 ok / C=1 error. Clobbers A, Y,
//  kArg..kArg+7.
ftWrite:
    lda #<ftRec
    sta kArg
    lda #>ftRec
    sta kArg+1
    lda ftPos
    sta kArg+2
    lda ftPos+1
    sta kArg+3
    lda #FT_WIN
    sta kArg+6
    lda #0
    sta kArg+7
    jsr kReuWriteAt
    rts

// --- ftVerify -- (kPtr) must be pattern(start) ---
//  Input: kPtr = buffer to check, A = first expected byte (window file
//  index & $FF); the pattern is byte i = index + i. Output: C=0 ok /
//  C=1 mismatch. Clobbers A, Y.
//  NOTE: the buffer comes from kPtr, never a hardcoded name -- an earlier
//  version verified ftBuf0 while the callers read into ftBuf1, which made
//  the restore check vacuous and the post check a false failure.
ftVerify:
    sta ftExp
    ldy #0
ftVerLp:
    lda (kPtr),y
    cmp ftExp
    bne ftVerBad
    inc ftExp
    iny
    cpy #FT_WIN
    bcc ftVerLp
    clc
    rts
ftVerBad:
    sec
    rts

// --- ftCompare -- ftBuf1 must equal ftBuf ---
//  Output: C=0 equal / C=1 differ. Clobbers A, Y.
ftCompare:
    ldy #0
ftCmpLp:
    lda ftBuf1,y
    cmp ftBuf,y
    bne ftCmpBad
    iny
    cpy #FT_WIN
    bcc ftCmpLp
    clc
    rts
ftCmpBad:
    sec
    rts

// --- ftStatus -- print "ok" / "FAIL" + newline for the result in C ---
//  Output: C = the same result (so the caller can branch on it), and ftBad
//  is set on failure. Clobbers A, X, Y, kPtr.
ftStatus:
    lda #0
    adc #0                      // A = C (0 = ok, 1 = fail)
    sta ftRes
    bne ftStFail
    lda #<ftSOk
    sta kPtr
    lda #>ftSOk
    sta kPtr+1
    jsr kPrintString
    jsr kNewline
    clc
    rts
ftStFail:
    lda #1
    sta ftBad
    lda #<ftSFail
    sta kPtr
    lda #>ftSFail
    sta kPtr+1
    jsr kPrintString
    jsr kNewline
    sec
    rts

// --- strings ---
ftFsName:
    .text "fscross.bin"
    .byte 0
ftSNoFile:
    .text "fstest: no fscross.bin"
    .byte 0
ftSRead:
    .text "read   "
    .byte 0
ftSWrite:
    .text "write  "
    .byte 0
ftSRest:
    .text "restore"
    .byte 0
ftSPost:
    .text "post   "
    .byte 0
ftSVer:
    .text "fstest: "
    .byte 0
ftSSkip:
    .text "fstest: skipped (no straddling window)"
    .byte 0
ftSOk:
    .text "ok"
    .byte 0
ftSFail:
    .text "FAIL"
    .byte 0

// --- scratch (this task's code pages) ---
ftRec:   .fill 5, 0             // kReuOpen record: size lo/hi, base lo/mid/hi
ftPos:   .word 0                // window start (file index)
ftExp:   .byte 0                // expected byte (ftVerify)
ftRes:   .byte 0                // last check result (ftStatus)
ftBad:   .byte 0                // any check failed
ftBuf0:  .fill FT_WIN, 0        // original window bytes
ftBuf:   .fill FT_WIN, 0        // complement (write source)
ftBuf1:  .fill FT_WIN, 0        // read-back buffer
