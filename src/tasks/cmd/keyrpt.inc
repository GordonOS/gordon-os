// ==========================================
//  keyrpt.inc -- set keyboard repeat timing at runtime
// ==========================================
//  Usage: keyrpt <delay> <repeat>
//    delay  = ticks before the first repeat (~60 Hz; default 30)
//    repeat = ticks between repeats (default 6)
//  Patches the operand bytes of the kernel's lda #KEYB_INITIAL /
//  lda #KEYB_REPEAT immediates (keyboard.inc). Each value is clamped to
//  a minimum of 1 (0 leaves keybTimer stuck at 0 -- the repeat countdown
//  wraps to $FF and never fires again). A missing argument leaves that
//  value unchanged.

keyrpt:
    ldx #0
    jsr kpNum                   // parse delay -> kpVal, kpFound
    lda kpFound
    beq kpExit                  // no delay arg -> leave both unchanged
    lda kpVal
    bne kpSetDelay
    lda #1                      // clamp explicit 0 -> 1
kpSetDelay:
    sta keybInitialPatch        // patch lda #KEYB_INITIAL operand

    jsr kpNum                   // parse repeat -> kpVal, kpFound
    lda kpFound
    beq kpExit                  // no repeat arg -> leave repeat unchanged
    lda kpVal
    bne kpSetRepeat
    lda #1
kpSetRepeat:
    sta keybRepeatPatch         // patch lda #KEYB_REPEAT operand
kpExit:
    lda #SYS_EXIT
    jsr syscall                 // exit

// parse one decimal number from cmdArgsBuf starting at X; skips leading
// spaces; kpVal = value (0 if none); X advanced past the number.
kpNum:
    lda #0
    sta kpVal
    sta kpFound                 // 0 = no digits yet
kpSkip:
    lda cmdArgsBuf,x
    beq kpRet                   // end of args -> 0
    cmp #$20
    bne kpDigit                 // non-space -> digit expected
    inx
    bne kpSkip                  // loop (args buffer < 256 bytes)
kpDigit:
    lda cmdArgsBuf,x
    sec
    sbc #$30                    // '0'
    bcc kpRet                   // < '0' -> done
    cmp #10
    bcs kpRet                   // > '9' -> done
    pha                         // save digit
    lda #1
    sta kpFound                 // saw at least one digit
    lda kpVal
    asl
    sta kpVal
    asl
    asl
    clc
    adc kpVal
    sta kpVal                   // kpVal = kpVal * 10
    pla
    clc
    adc kpVal
    sta kpVal                   // kpVal = kpVal * 10 + digit
    inx
    jmp kpDigit
kpRet:
    rts

kpVal:
    .byte 0                     // parse scratch (not kernel state)
kpFound:
    .byte 0                     // 1 = a number was parsed (vs no arg)
