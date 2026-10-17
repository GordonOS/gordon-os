// ==========================================
//  mousetest.inc -- the 1351 protocol readout
// ==========================================
//  Settles, on the machine, the three items the protocol leaves
//  open:
//    1. the hold sequence -- DDRA per bit, the PRA $DC00 select, the wait for
//       the SID's conversion, all inside SEI;
//    2. whether one conversion latches BOTH axes;
//    3. which line each of the two buttons appears on.
//
//  The readout, refreshed every MT_REFRESH displayed frames:
//    raw    the POT byte the SID returned -- $D419 (POT X) and $D41A (POT Y),
//           whichever control port the PRA select has routed to them
//    dec    bits 6-1 of it: the 1351's position mod 64
//    delta  (dec - the previous dec) & $3F: how far it moved since the sample
//           before
//    acc    the running total of delta
//    btn    PA5..PA0 of $DC00, 1 = released
//
//  The 1351 reports an ABSOLUTE position, not a delta, so the reader keeps its
//  own total: a dropped sample is a lost delta, and a flick past 64 counts
//  between samples aliases. Sampling every few displayed frames sits well inside
//  that.
//
//  Keys: 'p' switches the port being read -- the totals restart, because they
//  are the SELECTED port's -- and any other key exits.
//
//  The screen is the SHELL's, shared, like every other .com command: the readout
//  is written where the shell's cursor already is and updated IN PLACE, and the
//  command exits with the shell's prompt below it. The one thing a sharer cannot
//  do is read the keyboard -- kScanKeyboard answers $FF to a task that does not
//  hold kbFocus -- so the readout takes the FOCUS with kSwitchView, exactly as
//  `format` does: the task's surface IS the shell's, so nothing on screen moves.

// --- the readout's layout: absolute columns, and rows as OFFSETS from its own
//     first line (mtBase), so the block lands wherever the cursor was ---
.const MT_ROWS     = 8           // the readout's height, in rows
.const MT_BASE_MAX = 24 - MT_ROWS + 1   // 17: the lowest first row that fits
.const MT_R_TITLE = 0
.const MT_R_PORT  = 1
.const MT_R_HDR   = 2
.const MT_R_X     = 3
.const MT_R_Y     = 4
.const MT_R_ACC   = 5
.const MT_R_BTN   = 6
.const MT_R_HINT  = 7
.const MT_C_PORT = 5
.const MT_C_HOLD = 14            // the hold's two cells, on the port row
.const MT_C_RAW  = 12            // raw, dec and delta share one header row
.const MT_C_DEC  = 17
.const MT_C_DEL  = 21
.const MT_C_ACCX = 6
.const MT_C_ACCY = 26
.const MT_C_BTN  = 19
.const MT_C_SRC  = 29            // the source word, at the end of the title row

// --- refresh period in displayed frames. A power of two, so the frame
//     counter's LOW BITS are the period and nothing here does frame
//     arithmetic (it wraps). The readout is some thirty glyphs and each one is
//     a kernel print, so this also caps what the readout costs. ---
.const MT_REFRESH = 4

// --- the hold after the multiplexer select changes, as OUTER-loop counts: one
//     outer pass is 256 inner `dey/bne` iterations at 5 cycles = 1280 cycles, so
//     the four settings are about 1 / 5 / 20 / 79 ms of emulated time. The SID's
//     conversion is free-running and asynchronous and a full one must elapse, but
//     how long the SELECT needs before the ADC value is settled is one of the
//     things this task exists to measure -- 'h' cycles these live. ---
.const MT_HOLDN = 4

// --- the PRA $DC00 multiplexer select: 7=0,6=1 reads port 1, 7=1,6=0 port 2 ---
.const MT_SEL1 = $40
.const MT_SEL2 = $80

// --- the field table's stride, its total size, and the decimal divisors ---
.const MT_FTABROW = 5
.const MT_FTABN   = 50           // 10 rows x MT_FTABROW
.const MT_DIVN    = 5            // 1, 10, 100, 1000, 10000

// --- print a string at (column, row offset) ---
.macro mtSay(col, rowoff, str) {
    lda #rowoff
    ldx #col
    jsr mtAt
    lda #<str
    sta kPtr
    lda #>str
    sta kPtr+1
    jsr kPrintString
}

mousetest:                       // ENTRY -- the first label in the body
    // --- attach to the shell's screen, as every .com that prints does ---
    lda #SYS_GETTID
    jsr syscall
    tax
    lda taskParent,x             // shell slot
    tax
    jsr kShareScreen             // X = shell (preserved); C=0 ok
    bcc mtScr
    lda #SYS_EXIT
    jsr syscall                  // no screen -- exit
mtScr:
    //  TAKE THE KEYBOARD FOCUS, and nothing else. kScanKeyboard answers $FF to
    //  a task that does not hold kbFocus, so a sharer could not read the key
    //  that stops the readout; our surface IS the shell's, so the display does
    //  not change. kDisableCursor is for US -- a readout wants no blink block
    //  over it -- and kBlinkReset un-draws the SHELL's block before the
    //  hand-over: it is drawn into framebuffer pixels, and surfSave would bake
    //  it into the surface. The SEI keeps an IRQ tick from
    //  drawing it straight back; no syscall may run inside the window.
    jsr kDisableCursor
    lda #SYS_GETTID
    jsr syscall
    tax
    php
    sei
    txa
    pha
    jsr kBlinkReset              // clobbers A, X, Y, kPtr and kArg+7
    pla
    tax
    jsr kSwitchView              // kbFocus = us; the display does not change
    plp
    jsr kLockScreen

    // --- the readout is updated IN PLACE, so it has to start high enough to fit
    //     above the bottom row: from the cursor when it does, otherwise at the
    //     lowest row that holds all MT_ROWS. Nothing here scrolls the shell's
    //     screen, so the text above the readout stays put ---
    jsr kGetCursor               // X = col, Y = row
    cpy #MT_BASE_MAX + 1
    bcc mtFit
    ldy #MT_BASE_MAX
mtFit:
    sty mtBase
    ldx #0
    jsr kSetCursor               // column 0 of the readout's first row

    // --- the labels, written once: the values are what refresh ---
    mtSay(0, MT_R_TITLE, mtLTitle)
    mtSay(MT_C_SRC, MT_R_TITLE, mtLDir)   // where the samples come from
    mtSay(0, MT_R_PORT, mtLPort)
    mtSay(7, MT_R_PORT, mtLHold)
    mtSay(17, MT_R_PORT, mtLKeys)
    mtSay(12, MT_R_HDR, mtLCols)
    mtSay(0, MT_R_X, mtLXReg)
    mtSay(0, MT_R_Y, mtLYReg)
    mtSay(0, MT_R_ACC, mtLAccX)
    mtSay(20, MT_R_ACC, mtLAccY)
    mtSay(0, MT_R_BTN, mtLBtn)
    mtSay(26, MT_R_BTN, mtLRel)
    mtSay(0, MT_R_HINT, mtLHint)

    // --- start on port 2: a 1351 belongs in control port 2, at the shortest
    //     hold (the 'h' key cycles them) ---
    lda #1
    sta mtPort                   // 0 = port 1, 1 = port 2
    lda #2
    sta mtVPort                  // the port's number, which is what is printed
    lda #0
    sta mtHoldIdx
    jsr mtHoldPick               // load the hold the index names
    jsr mtSeed
    jsr mtDraw

mtLoop:
    lda #0
    jsr kReadKey                 // 0 = nothing waiting, or a key release
    beq mtTick
    cmp #'p'
    beq mtPortSwap
    cmp #'h'
    beq mtHoldNext
    cmp #'l'
    beq mtSrcNext
    jmp mtExit                   // any other key exits
mtTick:
    lda frameCount
    and #MT_REFRESH - 1
    bne mtIdle
    jsr mtRead
    jsr mtDraw
mtIdle:
    lda #SYS_YIELD               // the frame counter is the clock, not licence
    jsr syscall                  // to spin
    jmp mtLoop

// --- mtPortSwap -- show the other port ---
mtPortSwap:
    lda mtPort
    eor #1
    sta mtPort
    clc
    adc #1
    sta mtVPort
    jsr mtSeed
    jmp mtIdle

// --- mtHoldNext -- the next hold length, shown at once ---
mtHoldNext:
    lda mtHoldIdx
    clc
    adc #1
    and #MT_HOLDN - 1
    sta mtHoldIdx
    jsr mtHoldPick
    jmp mtIdle

// --- mtHoldPick -- load the hold and its printed value from the index ---
mtHoldPick:
    ldx mtHoldIdx
    lda mtHoldOuterTab,x
    sta mtHoldOuter
    lda mtHoldMsTab,x
    sta mtVHold                  // the field the readout prints, in ms
    rts

// --- mtHold -- burn the hold: mtHoldOuter x 256 inner iterations x 5 cycles ---
//  WARNING: this runs inside the read's SEI window, so the longest setting stalls
//  the kernel's tick for ~80 ms. That is why the hold is a TEST knob and not part
//  of the library.
mtHold:
    ldx mtHoldOuter
mtHoldOut:
    ldy #0
mtHoldIn:
    dey
    bne mtHoldIn
    dex
    bne mtHoldOut
    rts

// --- mtSrcNext -- take the samples from mouse.lib instead, or back again ---
//  The library path is the point of this key: the hardware path is the
//  reference, and the two fill the same fields, so a disagreement is visible
//  field by field on the screen.
mtSrcNext:
    lda mtSrc
    eor #1
    sta mtSrc
    beq mtSrcDir                 // 0 = the direct path
    lda #MOUSE_ENABLE
    jsr kMouseCall               // start tracking: the next read reports delta 0
    mtSay(MT_C_SRC, MT_R_TITLE, mtLLib)
    jmp mtSrcSeed
mtSrcDir:
    mtSay(MT_C_SRC, MT_R_TITLE, mtLDir)
mtSrcSeed:
    jsr mtSeed                   // the totals restart with the new source
    jmp mtIdle

// --- mtAt -- position the cursor: X = column, A = row offset in the readout ---
//  Every row the readout uses is an offset from mtBase, so the block lands
//  wherever the shell's cursor already was. Below the entry label: the code-size
//  field is `end - entry`, so a helper placed before it is outside the body.
mtAt:
    clc
    adc mtBase
    tay
    jsr kSetCursor               // the one call that preserves X and Y
    rts

// --- mtSeed -- sample once, then start the deltas and totals at zero ---
//  A wrapping position has no previous sample to be differenced against, so the
//  difference the first read would produce is discarded.
mtSeed:
    jsr mtRead
    lda #0
    sta mtVDelX
    sta mtVDelX+1
    sta mtVDelY
    sta mtVDelY+1
    sta mtVAccX
    sta mtVAccX+1
    sta mtVAccY
    sta mtVAccY+1
    rts

// --- mtRead -- take one sample from the selected source into the readout state ---
//  Either the direct hardware read below, or `mouse.lib` ('l' selects): both fill
//  the SAME fields, so the two can be compared field by field.
//  Clobbers A, X, Y, kPtr (kPrintString, not this routine).
mtRead:
    lda mtSrc
    beq mtReadDir
    //  --- through mouse.lib: the deltas are the library's, because it is the
    //      library that remembers the previous position (one mouse, one reader)
    //      ---
    lda #MOUSE_READ
    jsr kMouseCall
    bcc mtReadLib
    jmp mtReadDone               // reader disabled: leave the last values up
mtReadLib:
    sta mtBtnRaw                 // the buttons come back in A: take them first
    lda kArg+MOUSE_RAW_X
    sta mtVRawX
    lda kArg+MOUSE_RAW_Y
    sta mtVRawY
    //  WARNING: the library's delta is SIGNED and this readout prints it
    //  unsigned, so mask it back to the mod-64 distance the direct path
    //  reports -- a signed -1 is $ff, and $ff & $3f is 63, the same number.
    lda kArg+MOUSE_DELTA_X
    and #$3f
    sta mtVDelX
    lda kArg+MOUSE_DELTA_Y
    and #$3f
    sta mtVDelY
    jmp mtReadDec

    //  --- straight from the hardware, which is what the library path is
    //      checked against ---
    //  The buttons share CIA #1 port A with the PRA bits that select the port,
    //  and the kernel's keyboard scan WRITES $DC00 to drive the matrix columns --
    //  so a plain read returns the driven column, not the device. The read
    //  therefore makes DDRA per bit (6/7 out for the select, 5..0 in for the
    //  buttons, which is what gkJoyBits' whole-port flip cannot do) and puts BOTH
    //  port registers back as it found them. All of it inside SEI: a scan that ran
    //  in the window would drive nothing and see a phantom "no keys".
mtReadDir:
    ldy mtPort
    lda mtSelTab,y
    sta mtSelect
    php
    sei
    lda $dc02
    pha
    lda $dc00
    pha
    lda #%11000000               // PA7/PA6 out (the select), PA5..PA0 in
    sta $dc02
    lda mtSelect
    sta $dc00
    jsr mtHold                   // let the SID complete a conversion
    lda $d419                    // POTX -- the selected port's x axis
    sta mtVRawX
    lda $d41a                    // POTY -- the selected port's y axis
    sta mtVRawY
    lda $dc00                    // the buttons, PA5..PA0, active low
    and #%00111111
    sta mtBtnRaw
    pla
    sta $dc00                    // both registers back as they were found
    pla
    sta $dc02
    plp
    //  --- the distance moved since the previous sample, per axis ---
    //  WARNING: bit 7 is unused and bit 0 is a qualifier the reader shifts out,
    //  so the position is bits 6-1 -- one `lsr` and one mask, not a plain read.
    lda mtVRawX
    lsr
    and #$3f
    sec
    sbc mtLastX                  // (new - old) & $3F: the modulo-64 distance
    and #$3f
    sta mtVDelX
    lda mtVRawY
    lsr
    and #$3f
    sec
    sbc mtLastY
    and #$3f
    sta mtVDelY

    //  --- the tail both sources share: the decoded position, which the NEXT
    //      sample is differenced against, then the running total ---
mtReadDec:
    lda mtVRawX
    lsr
    and #$3f
    sta mtVDecX
    sta mtLastX
    lda mtVRawY
    lsr
    and #$3f
    sta mtVDecY
    sta mtLastY
    lda mtVDelX
    clc
    adc mtVAccX                  // ...added to the running total
    sta mtVAccX
    bcc mtRxHi
    inc mtVAccX+1
mtRxHi:
    lda mtVDelY
    clc
    adc mtVAccY
    sta mtVAccY
    bcc mtRyHi
    inc mtVAccY+1
mtRyHi:
mtReadDone:
    rts

// --- mtDraw -- repaint every field in mtFTab, then the buttons ---
//  Each field is rewritten to its own width, so a shorter number leaves no
//  stale digit behind and no clear pass is needed.
mtDraw:
    lda #0
    sta mtIdx
mtDrawLp:
    ldx mtIdx
    lda mtFTab,x
    sta kPtr
    lda mtFTab+1,x
    sta kPtr+1
    lda mtFTab+3,x               // row offset -- the drawer adds the base
    clc
    adc mtBase
    tay
    lda mtFTab+2,x
    tax                          // column
    jsr kSetCursor
    ldx mtIdx
    lda mtFTab+4,x
    pha                          // the field's width
    ldy #0
    lda (kPtr),y                 // the field is a {lo, hi} pair: the printer
    sta mtVal                    // always reads two bytes, and a byte-sized
    iny                          // field simply keeps hi = 0
    lda (kPtr),y
    sta mtVal+1
    pla
    tax
    jsr mtDec16
    lda mtIdx
    clc
    adc #MT_FTABROW
    sta mtIdx
    cmp #MT_FTABN
    bcc mtDrawLp
    // --- the button lines have no table row: six cells, printed bit by bit ---
    //  WARNING: the bit index lives in a byte of our own, never in Y --
    //  kPrintChar clobbers X and Y, so a `cpy #6` after it compares whatever it
    //  left behind and the loop never ends (it printed "1"s forever).
    lda #MT_R_BTN
    ldx #MT_C_BTN
    jsr mtAt
    lda #0
    sta mtIdx
mtBtnLp:
    ldx mtIdx
    lda mtBtnRaw
    and mtMaskTab,x
    beq mtBtnLow
    lda #'1'
    bne mtBtnPut                 // always taken: A is $31, never zero
mtBtnLow:
    lda #'0'
mtBtnPut:
    jsr kPrintChar
    inc mtIdx
    lda mtIdx
    cmp #6
    bcc mtBtnLp
    rts

// --- mtDec16 -- print mtVal as decimal at the cursor, exactly X cells wide ---
//  Leading zeros are skipped, then the field is space-padded to X cells, so the
//  field is fully rewritten every time. The digits come from repeated
//  subtraction of 10000/1000/100/10/1 rather than a divide, and kPrintChar
//  clobbers X and Y, so the divisor index is a byte of our own (mtDiv) and the
//  loop keeps nothing in a register.
mtDec16:
    stx mtWide
    lda #0
    sta mtDigits
    lda #MT_DIVN - 1
    sta mtDiv
mtD16Div:
    lda #0
    sta mtCount
mtD16Sub:
    ldy mtDiv
    lda mtVal
    sec
    sbc mtDivLo,y
    sta mtLo
    lda mtVal+1
    sbc mtDivHi,y
    bcc mtD16Digit               // it does not fit -- this digit is settled
    sta mtVal+1
    lda mtLo
    sta mtVal
    inc mtCount
    bne mtD16Sub
mtD16Digit:
    lda mtCount
    bne mtD16Put
    lda mtDigits
    beq mtD16Next                // still skipping leading zeros
mtD16Put:
    lda mtCount
    clc
    adc #'0'
    jsr kPrintChar
    inc mtDigits
mtD16Next:
    dec mtDiv
    bpl mtD16Div
    lda mtDigits
    bne mtD16Pad
    lda #'0'                     // zero prints as one digit, not as nothing
    jsr kPrintChar
    inc mtDigits
mtD16Pad:
    lda mtWide
    sec
    sbc mtDigits
    beq mtD16Rts
    sta mtCount
mtD16PadLp:
    lda #' '
    jsr kPrintChar
    dec mtCount
    bne mtD16PadLp
mtD16Rts:
    rts

// --- mtExit -- leave the readout behind, hand the keyboard back, exit ---
//  The shell's prompt has to land BELOW the readout, and in-place output does not
//  move the cursor on its own, so the cursor is advanced past the block first.
//  The queue is then drained by CAPACITY -- a press leaves its RELEASE behind,
//  and an empty queue and a release read the same, so "empty" cannot be tested
//  for -- while we still hold the focus, and kSwitchView hands the focus back
//  (it empties the queue again for free, so the shell starts clean).
mtExit:
    ldx #0
    ldy mtBase
    jsr kSetCursor
    ldy #MT_ROWS
mtExitLp:
    tya
    pha
    jsr kNewline                 // kNewline clobbers X and Y
    pla
    tay
    dey
    bne mtExitLp
    //  Give the surface's cursor back. It is the SHELL's surface, and cursorOn
    //  is per-surface, so the disable at entry turned the shell's block off --
    //  the same pair shell.inc uses around a spawned task (kDisableCursor before
    //  it, kEnableCursor after). The cursor sits below the readout, so the
    //  block comes back at the prompt rather than over the readout.
    jsr kEnableCursor            // clobbers A, X, Y, kPtr, kArg..+3
    ldx #16
mtDrainLp:
    txa
    pha
    jsr kReadKey                 // kReadKey clobbers X
    pla
    tax
    dex
    bne mtDrainLp
    lda #SYS_GETTID
    jsr syscall
    tax
    lda taskParent,x
    cmp #$ff
    beq mtExitUnlock             // no parent to hand it back to
    tax
    lda taskState,x
    beq mtExitUnlock             // a dead parent cannot take it
    jsr kSwitchView              // X = the shell
mtExitUnlock:
    jsr kUnlockScreen
    lda #SYS_EXIT
    jsr syscall

// --- the printed fields: {lo, hi} address of the value, then COLUMN, ROW
//     OFFSET and WIDTH -- in that order, and the drawer reads them by offset ---
mtFTab:
    .word mtVPort
    .byte MT_C_PORT, MT_R_PORT, 1
    .word mtVHold
    .byte MT_C_HOLD, MT_R_PORT, 2
    .word mtVRawX
    .byte MT_C_RAW, MT_R_X, 3
    .word mtVDecX
    .byte MT_C_DEC, MT_R_X, 2
    .word mtVDelX
    .byte MT_C_DEL, MT_R_X, 2
    .word mtVRawY
    .byte MT_C_RAW, MT_R_Y, 3
    .word mtVDecY
    .byte MT_C_DEC, MT_R_Y, 2
    .word mtVDelY
    .byte MT_C_DEL, MT_R_Y, 2
    .word mtVAccX
    .byte MT_C_ACCX, MT_R_ACC, 5
    .word mtVAccY
    .byte MT_C_ACCY, MT_R_ACC, 5

// --- the decimal divisors, low bytes and high bytes (index 0 = 1 ... 4 = 10000) ---
mtDivLo:
    .byte <1, <10, <100, <1000, <10000
mtDivHi:
    .byte >1, >10, >100, >1000, >10000

// --- PA5..PA0 of $DC00, most significant first, matching the label ---
mtMaskTab:
    .byte %00100000, %00010000, %00001000, %00000100, %00000010, %00000001

// --- the state -----------------------------------------------------------------
//  Every printed field is a {lo, hi} PAIR, so the printer reads the same two
//  bytes whatever the field holds; a byte-sized field keeps its hi at 0.
mtVRawX:    .byte 0, 0           // the raw POT byte, $D419
mtVRawY:    .byte 0, 0           // the raw POT byte, $D41A
mtVDecX:    .byte 0, 0           // bits 6-1: the position mod 64
mtVDecY:    .byte 0, 0
mtVDelX:    .byte 0, 0           // (dec - the previous dec) & $3F
mtVDelY:    .byte 0, 0
mtVAccX:    .word 0              // the running total of delta
mtVAccY:    .word 0
mtVPort:    .byte 2, 0           // the port on show, 1 or 2
mtVHold:    .byte 1, 0           // the hold, in ms, as the readout prints it
mtHoldIdx:  .byte 0              // which hold setting is in force
mtHoldOuter:.byte 1              // its outer-loop count (see mtHold)
mtBase:     .byte 0              // the readout's first row, from kGetCursor
mtLastX:    .byte 0              // the previous dec per axis (not printed)
mtLastY:    .byte 0
mtBtnRaw:   .byte 0              // PA5..PA0 of $DC00, 1 = released
mtPort:     .byte 1              // the port being read: 0 = port 1, 1 = port 2
mtSelect:   .byte 0              // its $DC00 select value
mtSrc:      .byte 0              // 0 = the hardware directly, 1 = mouse.lib
mtVal:      .word 0              // the printer's working copy of a field
mtWide:     .byte 0
mtDiv:      .byte 0
mtCount:    .byte 0
mtDigits:   .byte 0
mtLo:       .byte 0
mtIdx:      .byte 0

mtSelTab:
    .byte MT_SEL1, MT_SEL2

// --- the four hold settings (indexed by mtHoldIdx): one outer pass is 1280
//     cycles, and the printed value is that in milliseconds ---
mtHoldOuterTab:
    .byte 1, 4, 16, 62
mtHoldMsTab:
    .byte 1, 5, 20, 79

// --- the labels. The SID has ONE pair of paddle registers -- $D419 (POT X) and
//     $D41A (POT Y) -- and the PRA select decides which control port they carry,
//     so the x/y addresses do not move with `p`; the buttons are CIA #1 port A's
//     low six lines. ---
mtLTitle: .text "mousetest - 1351 POT readout";            .byte 0
mtLPort:  .text "port";                                    .byte 0
mtLHold:  .text "  hold";                                  .byte 0
mtLKeys:  .text "ms  p=port h=hold l=src";                 .byte 0
mtLCols:  .text "raw  dec delta";                          .byte 0
mtLXReg:  .text "POT x $D419";                             .byte 0
mtLYReg:  .text "POT y $D41A";                             .byte 0
mtLAccX:  .text "acc x";                                   .byte 0
mtLAccY:  .text "acc y";                                   .byte 0
mtLBtn:   .text "btn $DC00 PA5..PA0";                      .byte 0
mtLRel:   .text "1 = released";                            .byte 0
mtLHint:  .text "move the mouse; any other key exits";     .byte 0
//  Both 7 cells wide, so neither leaves a stale character when they alternate.
mtLDir:   .text "direct ";                                 .byte 0
mtLLib:   .text "library";                                 .byte 0
