// ==========================================
//  ws.inc -- check this task's per-task REU workspace
// ==========================================
//  Prints the slot it ran in, then three verdicts about the window
//  REU_WS_BASE owns for it:
//
//    FRESH  the window reads as zeros -- what clearSlotWorkspace writes at
//           spawn. RUN IT TWICE: the second run must say FRESH again, which is
//           what makes this a test of the SPAWN clear rather than of a
//           boot-time coincidence.
//    BACK   a pattern written through kReuFromC64 comes back through
//           kReuToC64, with the buffer wiped in between so a match cannot be
//           luck.
//    END    the window's LAST byte takes a write while its FIRST byte still
//           holds what the BACK check left there, so the two ends are distinct
//           addresses: the slot arithmetic -- including the bank carry past
//           slot 15 -- lands inside the window rather than wrapping onto its
//           start.
//
//  Scratch: 15B instance buffer -- WS_BUF (8B round-trip), WS_DEC (5B digits),
//  WS_SLOT (1B), WS_TMP (1B).

.const WS_BUF  = 0
.const WS_DEC  = 8
.const WS_SLOT = 13
.const WS_TMP  = 14

ws:
    lda #SYS_GETTID
    jsr syscall                 // A = this task's slot
    ldy #WS_SLOT
    sta (dataPtr),y
    tax
    lda taskParent,x            // the shell owns the screen we print on
    tax
    jsr kShareScreen
    bcc wsScr
    lda #SYS_EXIT
    jsr syscall                 // no screen to print on -- leave quietly
wsScr:
    jsr kLockScreen

    // --- "WS SLOT n" ---
    lda #0
    jsr wsMsg
    lda #WS_DEC
    jsr wsPtr
    ldy #WS_SLOT
    lda (dataPtr),y
    jsr kByteToDec              // digits at (kPtr), Y = their length
    lda #0
    sta (kPtr),y                // terminate
    lda #WS_DEC
    jsr wsPtr
    jsr kPrintString
    jsr kNewline

    // --- FRESH: every byte of the window reads zero ---
    jsr wsIn8
    ldy #WS_BUF
    ldx #8
    lda #0
wsFshLp:
    ora (dataPtr),y
    iny
    dex
    bne wsFshLp
    beq wsFshOk                 // A = 0: nothing but zeros came back
    lda #2
    jmp wsFshSay
wsFshOk:
    lda #1
wsFshSay:
    jsr wsSay

    // --- BACK: a written pattern comes back unchanged ---
    ldy #WS_BUF
    ldx #8
    lda #$a5
wsPatLp:
    sta (dataPtr),y
    eor #$ff
    iny
    dex
    bne wsPatLp
    jsr wsOut8
    ldy #WS_BUF                 // wipe the buffer: a match must come from the REU
    ldx #8
    lda #0
wsWipeLp:
    sta (dataPtr),y
    iny
    dex
    bne wsWipeLp
    jsr wsIn8
    ldy #WS_BUF
    ldx #8
    lda #$a5
wsCmpLp:
    cmp (dataPtr),y
    bne wsBckBad
    eor #$ff
    iny
    dex
    bne wsCmpLp
    lda #3
    jmp wsBckSay
wsBckBad:
    lda #4
wsBckSay:
    jsr wsSay

    // --- END: the window's last byte is its own address ---
    jsr wsLast
    lda #1
    sta kArg+3
    lda #0
    sta kArg+4
    lda #WS_TMP
    jsr wsPtr
    ldy #0
    lda #$3c
    sta (kPtr),y                // the byte to send -- not a value the pattern holds
    jsr kReuFromC64             // ($FFF into the window) <- $3c
    lda #WS_TMP
    jsr wsPtr
    jsr kReuToC64               // ...and back again
    ldy #0
    lda (kPtr),y
    cmp #$3c
    bne wsEndBad
    jsr wsIn1                   // the FIRST byte must still be the pattern's $a5:
    ldy #WS_BUF                 //   if $FFF aliased onto the start, the write would
    lda (dataPtr),y             //   have replaced it
    cmp #$a5
    bne wsEndBad
    lda #5
    jmp wsEndSay
wsEndBad:
    lda #6
wsEndSay:
    jsr wsSay

    jsr kUnlockScreen
    lda #SYS_EXIT
    jsr syscall

// --- wsBase -- kArg/kArg+1/kArg+2 = this slot's window start ---
//  The same arithmetic clearSlotWorkspace uses in the kernel: the low byte is
//  0 because REU_WS_BASE is page-aligned, the hi byte is slot x 16 (which IS
//  slot x REU_WS_SIZE / 256), and the shift's carry is the bank.
wsBase:
    lda #0
    sta kArg
    ldy #WS_SLOT
    lda (dataPtr),y
    asl
    asl
    asl
    asl
    sta kArg+1
    lda #>REU_WS_BASE
    adc #0
    sta kArg+2
    rts

// --- wsLast -- the same, plus the $FFF offset: the window's last byte ---
wsLast:
    jsr wsBase
    lda #$ff
    sta kArg                    // the $00FF half of $0FFF
    lda kArg+1
    clc
    adc #$0f                    // the $0F00 half
    sta kArg+1
    lda kArg+2
    adc #0
    sta kArg+2
    rts

// --- wsIn8 / wsOut8 / wsIn1 -- 8 bytes in, 8 bytes out, 1 byte in ---
wsIn8:
    jsr wsBase
    lda #8
    sta kArg+3
    lda #0
    sta kArg+4
    lda #WS_BUF
    jsr wsPtr
    jmp kReuToC64
wsOut8:
    jsr wsBase
    lda #8
    sta kArg+3
    lda #0
    sta kArg+4
    lda #WS_BUF
    jsr wsPtr
    jmp kReuFromC64
wsIn1:
    jsr wsBase
    lda #1
    sta kArg+3
    lda #0
    sta kArg+4
    lda #WS_BUF
    jsr wsPtr
    jmp kReuToC64

// --- wsPtr -- kPtr = (dataPtr) + A ---
wsPtr:
    clc
    adc dataPtr
    sta kPtr
    lda dataPtr+1
    adc #0
    sta kPtr+1
    rts

// --- wsSay -- print message A, then end the line ---
wsSay:
    jsr wsMsg
    jsr kNewline
    rts

// --- wsMsg -- print message A of wsMsgs at the cursor ---
wsMsg:
    asl
    tay
    lda wsMsgs,y
    sta kPtr
    lda wsMsgs+1,y
    sta kPtr+1
    jmp kPrintString

wsMsgs:
    .word wsM0, wsM1, wsM2, wsM3, wsM4, wsM5, wsM6
wsM0: .text "WS SLOT ";  .byte 0
wsM1: .text "FRESH OK";  .byte 0
wsM2: .text "FRESH FAIL"; .byte 0
wsM3: .text "BACK OK";   .byte 0
wsM4: .text "BACK FAIL"; .byte 0
wsM5: .text "END OK";    .byte 0
wsM6: .text "END FAIL";  .byte 0
