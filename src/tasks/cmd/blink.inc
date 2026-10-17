// ==========================================
//  blink.inc -- set cursor blink rate at runtime
// ==========================================
//  Parses a decimal number from cmdArgsBuf and writes it to the kernel's
//  blinkRate byte (ticks per toggle; 1..255, 0 clamped to 1). The IRQ
//  reads blinkRate when re-arming blinkTick, so the new rate takes
//  effect on the next toggle. blinkVal is a code-body scratch so the
//  kernel byte is only written once with the final value.

blink:
    ldx #0
    lda #0
    sta blinkVal                // scratch accumulator = 0
blinkSkip:
    lda cmdArgsBuf,x
    beq blinkClamp              // end of args
    cmp #$20
    bne blinkDigit              // space -> skip
    inx
    jmp blinkSkip
blinkDigit:
    lda cmdArgsBuf,x
    sec
    sbc #$30                    // '0'
    bcc blinkClamp              // < '0' -> done
    cmp #10
    bcs blinkClamp              // > '9' -> done
    pha                         // save digit
    lda blinkVal
    asl                         // *2
    sta blinkVal
    asl                         // *4
    asl                         // *8
    clc
    adc blinkVal                // *8 + *2 = *10
    sta blinkVal
    pla                         // digit
    clc
    adc blinkVal                // *10 + digit
    sta blinkVal
    inx
    jmp blinkDigit
blinkClamp:
    lda blinkVal
    bne blinkSet
    lda #1                      // 0 -> minimum 1
blinkSet:
    sta blinkRate               // write final value to the kernel byte
    lda #SYS_EXIT
    jsr syscall                 // exit

blinkVal:
    .byte 0                     // parse scratch (not kernel state)
