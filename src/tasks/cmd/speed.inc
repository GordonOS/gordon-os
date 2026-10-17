// ==========================================
//  speed.inc -- declare the machine's speed
// ==========================================
//  "speed <percent>" -- <percent> is the speed the whole machine (CPU, VIC
//  and SID together) runs at relative to real time: 100 = real time, 200 =
//  the 200% `tools/launch-vice.ps1` starts VICE at. Only powers of two are
//  accepted (25 50 100 200 400 800), because every use of the value is a
//  shift: sid.lib moves the tune's pitch by a whole octave per step and its
//  ticks-per-jiffy delay by the same exponent, and a frame-paced task
//  shifts its own durations by it. A speed between two of those values
//  cannot be compensated at all, and music half an octave out is worse than
//  a refused command.
//
//  Writes sysSpeedShift, then re-derives the kernel's blink and key-repeat
//  tick counts from BLINK_RATE/KEYB_INITIAL/KEYB_REPEAT -- never from the
//  live values, so running this twice is a no-op. A machine at 100% leaves
//  all three exactly as the kernel set them at boot.
//
//  Silent on success, like blink/keyrpt. Run it BEFORE blink/keyrpt: those
//  override the timings this sets. It survives a reboot only as far as the
//  image does -- the value is a line in boot.bat, which is what makes the
//  image the machine's declaration: reu/REU.bin carries `speed 200` for
//  VICE and reu/REU-C64U.bin carries `speed 100` for an Ultimate.
//
//  Anything else -- no argument, 0, or a speed between two compensable ones --
//  prints the accepted values, then a blank line.

speed:
    //  A .com prints on the SHELL's screen: the shell owns the display, so
    //  without the share this command's output lands on its own surface and is
    //  never seen. Same prologue as dir/time/ps -- the owner is taskParent[own].
    lda #SYS_GETTID
    jsr syscall                 // gettid -> A
    tax
    lda taskParent,x            // shell slot
    tax
    jsr kShareScreen            // X = shell (preserved); C=0 ok
    bcc sdScr
    lda #SYS_EXIT
    jsr syscall                 // no screen -- exit
sdScr:
    jsr kLockScreen             // serialise screen access against other tasks
    ldx #0
    jsr sdNum                   // sdVal = the argument, 0 when there is none
    ldy #0
sdLook:
    lda sdTable,y
    cmp sdVal
    beq sdHit
    iny
    cpy #sdTableEnd - sdTable
    bne sdLook
    //  Nothing usable was given: print the values that are, then leave.
    lda #<sdMsgUse
    sta kPtr
    lda #>sdMsgUse
    sta kPtr+1
    jsr kPrintString
    jsr kNewline
    jsr kNewline
    jmp sdExit
sdHit:
    lda sdShiftTab,y
    sta sysSpeedShift
    sta sdShift                 // ...and scale the kernel's timings by it
    lda #BLINK_RATE
    jsr sdScale
    sta blinkRate
    lda #KEYB_INITIAL
    jsr sdScale
    sta keybInitialPatch
    lda #KEYB_REPEAT
    jsr sdScale
    sta keybRepeatPatch
sdExit:
    jsr kUnlockScreen
    lda #SYS_EXIT
    jsr syscall                 // exit

//  sdScale -- A = a tick count, sdShift = the signed speed exponent
//  -> A scaled by 2^sdShift, clamped to 1..255.
//  A faster machine needs MORE ticks for the same real time (its ticks are
//  shorter), so a positive shift is a left shift. The low clamp matters: a
//  repeat count of 0 stops the repeat firing at all (see keyrpt.inc).
sdScale:
    ldy sdShift
    beq sdscRet                 // 100%: unchanged
    bmi sdscDown
sdscUp:
    asl
    bcs sdscMax                 // beyond 255: saturate
    dey
    bne sdscUp
    rts
sdscMax:
    lda #255
    rts
sdscDown:
    lsr
    iny                         // Y counts up from the negative shift
    bne sdscDown
    cmp #1
    bcs sdscRet
    lda #1
sdscRet:
    rts

//  sdNum -- parse the first decimal number from cmdArgsBuf starting at X,
//  skipping leading spaces. sdVal = the value, 0 if there are no digits; X is
//  left past the number.
sdNum:
    lda #0
    sta sdVal
sdnSkip:
    lda cmdArgsBuf,x
    beq sdnRet                  // end of args
    cmp #$20
    bne sdnDigit                // non-space -> digit expected
    inx
    bne sdnSkip                 // loop (args buffer < 256 bytes)
sdnDigit:
    lda cmdArgsBuf,x
    sec
    sbc #$30                    // '0'
    bcc sdnRet                  // < '0' -> done
    cmp #10
    bcs sdnRet                  // > '9' -> done
    pha                         // save digit
    lda sdVal
    asl
    sta sdVal
    asl
    asl
    clc
    adc sdVal
    sta sdVal                   // sdVal = sdVal * 10
    pla
    clc
    adc sdVal
    sta sdVal                   // sdVal = sdVal * 10 + digit
    inx
    jmp sdnDigit
sdnRet:
    rts

sdTable:
    .byte 25, 50, 100, 200, 400, 800
sdTableEnd:
sdShiftTab:
    .byte $fe, $ff, 0, 1, 2, 3  // -2, -1, 0, +1, +2, +3
sdMsgUse:
    .text "? speed 25 50 100 200 400 800"
    .byte 0

sdVal:
    .byte 0                     // parse scratch (not kernel state)
sdShift:
    .byte 0
