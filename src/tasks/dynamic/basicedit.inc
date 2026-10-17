// ==========================================
//  basicedit.inc -- EhBASIC full-screen line editor (bitmap port)
// ==========================================
//  Port of reference/ehbasic-editline.asm.
//  Keeps the reference's shadow-buffer logic (dispatch chain,
//  insert/delete/wrap rules); replaces the text-mode screen primitives
//  with bitmap kernel calls:
//
//    - elRender    -> kPrintCharAt loop + kCsrBoth cursor save/restore
//                    (kPrintCharAt writes glyph + matrix nibble + char
//                    shadow with NO cursor advance/scroll, so a line may
//                    sit on row 24; the old $D800/screenBase math is gone)
//    - elScanRow   -> kReadRow (1-DMA char-shadow row read)
//    - elRestChar  -> deleted (kPrintChar already un-XORs the blink at
//                    the saved position)
//    - elDone/elRetCleanup -> deleted (cursor/blink are kernel-managed)
//
//  ABI (matches kReadString): kPtr = buffer, X = maxLen, Y = length on
//  return, buffer null-terminated in screen codes.
//
//  Cursor movement uses kGetCursor/kSetCursor -- no direct cursorCol/
//  cursorRow reads (those kernel aliases were deleted from basic.asm).
//
//  Wrapped in a namespace because KickAssembler labels are global
//  across segments -- the `el*` names here would collide with edit.asm.

.namespace basicedit {

// --- editor state (task-private; BASIC is single-instance) ---
elBuf:        .word 0          // saved output buffer pointer (kPtr)
elMax:        .byte 0          // saved max length
elStartRow:   .byte 0          // first screen row for rendered content
elShadowPos:  .byte 0          // cursor position in buffer (0..elShadowLen)
elShadowLen:  .byte 0          // current text length (0..126)
elTemp:       .byte 0          // cursorOn save across elRender
elScanCol:    .byte 0          // saved cursor col during elScanRow
elPromptLen:  .byte 0          // prompt prefix length (stripped on submit)
elRCol:       .byte 0          // elRender: current column (0-39)
elRRow:       .byte 0          // elRender: current row
elLS:         .byte 0          // elReturn: logical line start row
elLE:         .byte 0          // elReturn: logical line end row
elBEnd:       .byte 0          // elReturn: buffer's last row
elNup:        .byte 0          // elReturn: upper screen rows to prepend
elNlow:       .byte 0          // elReturn: lower screen rows to append
elShift:      .byte 0          // elReturn: shift = elNup*40 (8-bit)
elR:          .byte 0          // elReturn: merge loop row
elOff:        .byte 0          // elReturn: merge dest offset

// --- Entry point ---
kEditLine:
    lda #SYS_GETTID; jsr syscall         // gettid -> A
    tay
    cpy kbFocus
    beq elGo                    // we own the keyboard
    ldy #0; rts                 // not focused -- return empty

elGo:
    stx elMax                   // save maxLen
    lda kPtr; sta elBuf         // save output buffer pointer
    lda kPtr+1; sta elBuf+1
    jsr kEnableCursor           // clobbers A,X,Y
    jsr kGetCursor              // X=col, Y=row
    sty elStartRow
    jsr elScanRow               // load current row into buffer
    lda ibuffs                  // only a real INPUT prompt ("? ") is stripped
    cmp #$3f                    // '?' screen code
    bne elGoNoPrompt            // hover/READY -> submit the row as-is
    lda elShadowLen
    sta elPromptLen             // strip the "? " prefix
    jmp elLoop
elGoNoPrompt:
    lda #0
    sta elPromptLen             // no prompt -> strip nothing
    // fall into elLoop

// --- elLoop: poll keyboard, dispatch ---
elLoop:
    lda #SYS_GETTID; jsr syscall         // gettid
    tay
    cpy kbFocus
    beq elFocused
    ldy #0; rts                 // lost focus -> return empty
elFocused:
    jsr kScanKeyboard
    cmp #$ff; beq elLoop
    cmp #$81; bne elNotRet; jmp elReturn
elNotRet:
    cmp #$80; bne elNotDel; jmp elDelete
elNotDel:
    cmp #$82; bne elNotHome; jmp elHome
elNotHome:
    cmp #$84; bne elNotDown; jmp elDown
elNotDown:
    cmp #$85; bne elNotUp; jmp elUp
elNotUp:
    cmp #$86; bne elNotRight; jmp elRight
elNotRight:
    cmp #$87; bne elNotLeft; jmp elLeft
elNotLeft:
    cmp #$88; bne elNotIns; jmp elInsert
elNotIns:
    cmp #$01; bcc elLoop        // skip NUL
    jmp elPrintable

// =====================================================
//  Shadow-buffer handlers
// =====================================================

// --- elPrintable: overwrite char at cursor (C64 screen editor) ---
elPrintable:
    ldx elShadowLen
    cpx elMax; bcc elPrOk
    jmp elDone                  // buffer full -- no-op
elPrOk:
    ldy elShadowPos
    sta ibuffs,y                // overwrite at cursor (A = char)
    cpy elShadowLen
    bcc elPrAdvance             // within text -> don't extend length
    inc elShadowLen
elPrAdvance:
    inc elShadowPos
    jsr elRender
    jsr elSyncCursor
    jmp elDone

// --- elDelete: delete char before cursor ---
elDelete:
    lda elShadowPos; bne elDelOk
    jmp elDone                  // at start -- no-op
elDelOk:
    dec elShadowPos
    ldy elShadowPos
elDelShift:
    iny
    cpy elShadowLen; bcs elDelDone
    lda ibuffs,y
    dey
    sta ibuffs,y
    iny
    jmp elDelShift
elDelDone:
    dec elShadowLen
    jsr elRender
    jsr elSyncCursor
    jmp elDone

// --- elInsert: insert space at cursor (cursor doesn't move) ---
elInsert:
    ldx elShadowLen
    cpx elMax; bcc elInsOk
    jmp elDone                  // buffer full -- no-op
elInsOk:
    ldy elShadowLen
    beq elInsHere
elInsShift:
    cpy elShadowPos
    beq elInsHere
    bcc elInsHere
    dey
    lda ibuffs,y
    sta ibuffs+1,y
    jmp elInsShift
elInsHere:
    ldy elShadowPos
    lda #$20; sta ibuffs,y      // insert space
    inc elShadowLen
    jsr elRender
    jsr elSyncCursor
    jmp elDone

// --- elRight: cursor right, wrap to next row at end of text ---
elRight:
    jsr kGetCursor              // X=col, Y=row
    txa
    cmp elShadowLen; bcs elRWrap
    inx                         // col+1
    inc elShadowPos
    jsr kSetCursor              // (col+1, row)
    jmp elDone
elRWrap:
    cpy #24; bcc elRWrapOk
    jmp elDone                  // bottom row -- can't wrap
elRWrapOk:
    iny                         // row+1
    ldx #0
    jsr kSetCursor              // (0, row+1)
    sty elStartRow
    jsr elScanRow               // load new row
    jmp elDone

// --- elLeft: cursor left, wrap to previous row at col 0 ---
elLeft:
    jsr kGetCursor              // X=col, Y=row
    txa
    bne elLDec
    tya
    bne elLDecOk
    jmp elDone                  // (0,0) -- can't move
elLDecOk:
    dey                         // row-1
    ldx #0
    jsr kSetCursor              // (0, row-1)
    sty elStartRow
    jsr elScanRow               // load row (elShadowPos=0)
    lda elShadowLen
    sta elShadowPos             // position at end of row's text
    tax
    ldy elStartRow
    jsr kSetCursor              // (len, row-1)
    jmp elDone
elLDec:
    dex                         // col-1
    jsr kSetCursor              // (col-1, row)
    stx elShadowPos
    jmp elDone

// --- elHome: cursor to start ---
elHome:
    lda #0; sta elShadowPos
    jsr elSyncCursor
    jmp elDone

// --- elDown: cursor down one row, load row into buffer ---
elDown:
    jsr kGetCursor
    cpy #24; bcc elDownGo
    jmp elDone
elDownGo:
    iny
    jsr kSetCursor              // (col, row+1)
    sty elStartRow
    jsr elScanRow
    jmp elDone

// --- elUp: cursor up one row, load row into buffer ---
elUp:
    jsr kGetCursor
    tya
    bne elUpGo
    jmp elDone
elUpGo:
    dey
    jsr kSetCursor              // (col, row-1)
    sty elStartRow
    jsr elScanRow
    jmp elDone

// --- elScanRow: read row at cursor into ibuffs ---
//  Sets elShadowLen, elShadowPos (clamped); sets cursor col = clamped.
//  Precondition: elStartRow == cursorRow (all callers maintain this).
//  Clobbers: A, X, Y
elScanRow:
    jsr kGetCursor              // X=col, Y=row
    stx elScanCol
    tya
    pha                         // row -> stack (kReadRow clobbers A,X,Y)
    lda #<ibuffs; sta kPtr
    lda #>ibuffs; sta kPtr+1
    pla                         // A = row
    jsr kReadRow                // row -> ibuffs (C=1 -> spaces)
    ldy #39
elScanTrim:
    lda ibuffs,y
    cmp #$20; bne elScanGotLen
    dey
    bpl elScanTrim
    ldy #$ff                    // all spaces -> empty
elScanGotLen:
    iny                         // Y = length (0-40)
    sty elShadowLen
    lda elScanCol
    cmp elShadowLen
    bcc elScanClampDone
    lda elShadowLen
elScanClampDone:
    sta elShadowPos
    tax                         // X = clamped col
    ldy elStartRow              // row (== cursor row)
    jsr kSetCursor
    rts

// =====================================================
//  elRender: render buffer to screen
//  Content is rendered with kPrintCharAt (writes a cell with NO cursor
//  advance / scroll), so a line may sit on row 24 without triggering
//  kPrintChar's wrap-scroll. The only scroll is the explicit overflow
//  path below, which fires when the content runs PAST row 24 (typing
//  past col 39 of the last line). Clobbers A, X, Y, kPtr, kArg.
// =====================================================
elRender:
    jsr kGetCursorOn            // A = cursorOn (X/Y preserved)
    sta elTemp
    lda #0
    jsr csrBoth                 // disable cursor + tail sync

    lda elShadowLen
    bne elRNotEmpty
    jmp elRClear                // empty -> clear one row
elRNotEmpty:
    clc; adc #39                // rows = ceil(len/40)
    ldy #0
elRDiv:
    cmp #40; bcc elRDivDone
    sbc #40
    iny
    bne elRDiv
elRDivDone:
    tya
    clc; adc elStartRow         // A = elStartRow + rows
    cmp #25; bcc elRGo          // fits above the bottom row
    sec; sbc #25
    beq elRGo                   // ends exactly on row 24
    tax                         // X = overflow rows
elRScroll:
    txa; pha
    jsr kScrollScreen
    pla; tax
    dec elStartRow
    dex
    bne elRScroll

elRGo:
    lda #0
    sta elRCol
    lda elStartRow
    sta elRRow
    ldx #0                      // buffer index
elRLoop:
    cpx elShadowLen; bcs elRPad
    txa; pha                    // save index (kPrintCharAt clobbers X)
    lda ibuffs,x
    ldx elRCol
    ldy elRRow
    jsr kPrintCharAt            // glyph at (elRCol, elRRow), no advance
    pla; tax                    // restore index
    inx
    inc elRCol
    lda elRCol
    cmp #40
    bcc elRLoop
    lda #0; sta elRCol          // wrap to next row
    inc elRRow
    jmp elRLoop

elRPad:
    ldx elRCol
    beq elRDone                 // col 0 -> nothing to pad
elRPadLp:
    cpx #40; bcs elRDone
    txa; pha                    // save col (kPrintCharAt clobbers X)
    lda #$20
    ldy elRRow
    jsr kPrintCharAt            // space at (col, elRRow), no advance
    pla; tax
    inx
    jmp elRPadLp
elRDone:
    lda elTemp
    jsr csrBoth                 // restore cursorOn
    rts

elRClear:
    lda #0
    sta elRCol
    lda elStartRow
    sta elRRow
    ldx #0                      // col counter
elRClrLp:
    cpx #40; bcs elRClrDone
    txa; pha
    lda #$20
    ldy elRRow
    jsr kPrintCharAt            // clear cell, no advance/scroll
    pla; tax
    inx
    jmp elRClrLp
elRClrDone:
    lda elTemp
    jsr csrBoth                 // restore cursorOn
    rts

// =====================================================
//  elSyncCursor: map elShadowPos -> cursor (col, row)
//  Clobbers: A, Y
// =====================================================
elSyncCursor:
    lda elShadowPos
    ldy #0
elSDiv:
    cmp #40; bcc elSDivDone
    sbc #40
    iny
    bne elSDiv
elSDivDone:
    tax                         // X = col (remainder)
    tya
    clc; adc elStartRow
    cmp #25
    bcc elSCur                  // valid row
    ldx #39                     // insertion past (24,39) -> clamp to last cell
    lda #24
elSCur:
    tay                         // Y = row
    jsr kSetCursor
    rts

// =====================================================
//  elReturn: submit the whole logical (possibly wrapped) line
// =====================================================
//  A program line longer than 40 chars spills across screen rows; the
//  row above a continuation is "full" (col 39 non-space). On RETURN,
//  assemble every row of the logical line that contains the cursor:
//  the edited buffer supplies the rows it currently holds, the other
//  rows are read back via kReadRow. The cursor is then placed on the
//  line's LAST row so EhBASIC's post-input CR/LF lands on the next
//  program line. Clobbers A, X, Y, kPtr, kArg.
// =====================================================

// --- elRowFull: C=1 if row A has a non-space at col 39 (line wraps) ---
//  Clobbers A, X (Y preserved by kCharAt).
elRowFull:
    tay                         // Y = row
    lda #39                     // A = col 39
    jsr kCharAt                 // A = screen code at (39,row)
    cmp #$20
    beq elRFNo
    sec
    rts
elRFNo:
    clc
    rts

// --- elSetDest: kPtr = ibuffs + A (8-bit offset, page-crossing safe) ---
//  Clobbers A; Y preserved.
elSetDest:
    clc
    adc #<ibuffs
    sta kPtr
    lda #>ibuffs
    adc #0
    sta kPtr+1
    rts

elReturn:
    // line start: scan up from elStartRow while the row above is full
    lda elStartRow
    sta elLS
elRetUp:
    lda elLS
    beq elRetUpDone
    sec
    sbc #1                      // A = row above
    jsr elRowFull               // C=1 if row above is full
    bcc elRetUpDone
    dec elLS
    jmp elRetUp
elRetUpDone:
    lda elShadowLen
    bne elRetNotEmpty
    jmp elRetSubmit             // empty -> submit as-is (no cursor move)
elRetNotEmpty:
    // brows = ceil(len/40); bEnd = elStartRow + brows - 1
    clc
    adc #39
    ldy #0
elRetBrows:
    cmp #40
    bcc elRetBrowsDone
    sbc #40
    iny
    bne elRetBrows
elRetBrowsDone:
    tya
    clc
    adc elStartRow
    sec
    sbc #1
    sta elBEnd
    sta elLE
    // line end: scan down while row elLE is full
elRetDown:
    lda elLE
    cmp #24
    bcs elRetDownDone
    jsr elRowFull               // A = elLE; C=1 if full
    bcc elRetDownDone
    inc elLE
    jmp elRetDown
elRetDownDone:
    // nup = elStartRow - elLS ; nlow = elLE - elBEnd
    lda elStartRow
    sec
    sbc elLS
    sta elNup
    lda elLE
    sec
    sbc elBEnd
    sta elNlow
    // shift = nup*40
    lda #0
    sta elShift
    ldx elNup
    beq elRetGuard
elRetShift:
    clc
    lda elShift
    adc #40
    sta elShift
    dex
    bne elRetShift
    // guard: total = shift + elShadowLen + nlow*40 must fit in elMax
elRetGuard:
    lda elShift
    clc
    adc elShadowLen
    bcc elRetGuardOk
    jmp elRetSubmit             // >255 -> fall back to buffer-only
elRetGuardOk:
    ldx elNlow
    beq elRetGuardFit
elRetAddLow:
    clc
    adc #40
    bcc elRetAddLowOk
    jmp elRetSubmit
elRetAddLowOk:
    dex
    bne elRetAddLow
elRetGuardFit:
    cmp elMax
    bcc elRetMerge
    beq elRetMerge
    jmp elRetSubmit             // too long -> fall back to buffer-only

    // --- merge: lower rows -> shift buffer right -> upper rows ---
elRetMerge:
    lda elShift
    clc
    adc elShadowLen
    sta elOff                   // dest offset for lower rows
    lda elBEnd
    clc
    adc #1
    sta elR                     // first lower row
elRetLowLp:
    lda elR
    cmp elLE
    bcc elRetLowGo
    beq elRetLowGo
    jmp elRetLowDone
elRetLowGo:
    lda elOff
    jsr elSetDest               // kPtr = ibuffs + elOff
    lda elR
    jsr kReadRow                // row -> (kPtr)
    lda elOff
    clc
    adc #40
    sta elOff
    inc elR
    jmp elRetLowLp
elRetLowDone:
    // shift the edited buffer right by elShift
    ldy elShadowLen
    dey                         // Y = len-1
    lda elShift
    jsr elSetDest               // kPtr = ibuffs + elShift (Y preserved)
elRetShiftLp:
    lda ibuffs,y
    sta (kPtr),y
    dey
    bpl elRetShiftLp
    // upper rows -> ibuffs[0 .. shift)
    lda elLS
    sta elR
    lda #0
    sta elOff
elRetUpLp:
    lda elR
    cmp elStartRow
    bcc elRetUpGo
    beq elRetUpGo
    jmp elRetUpLpDone
elRetUpGo:
    lda elOff
    jsr elSetDest
    lda elR
    jsr kReadRow
    lda elOff
    clc
    adc #40
    sta elOff
    inc elR
    jmp elRetUpLp
elRetUpLpDone:
    // total = shift + elShadowLen + nlow*40 ; trim trailing spaces
    lda elShift
    clc
    adc elShadowLen
    ldx elNlow
    beq elRetTrimGo
elRetTotalLow:
    clc
    adc #40
    dex
    bne elRetTotalLow
elRetTrimGo:
    sta elOff                   // total
    ldy elOff
elRetTrimLp:
    dey
    bmi elRetTrimZero           // all spaces
    lda ibuffs,y
    cmp #$20
    beq elRetTrimLp
    iny                         // Y = length
    sty elShadowLen
    jmp elRetCursor
elRetTrimZero:
    ldy #0
    sty elShadowLen
    // cursor -> (0, elLE); EhBASIC's CR/LF then lands on elLE+1
elRetCursor:
    ldx #0
    ldy elLE
    jsr kSetCursor
elRetSubmit:
    ldx elPromptLen             // prompt prefix length (chars before cursor)
    beq elSubTerm               // none -> terminate as-is
    lda elShadowLen
    sec
    sbc elPromptLen             // A = length after strip
    bcc elSubEmpty              // safety: prompt >= len -> empty
    beq elSubEmpty
    sta elShadowLen             // new length (>= 1)
    clc
    lda #<ibuffs
    adc elPromptLen
    sta kPtr                    // source = ibuffs + promptLen
    lda #>ibuffs
    adc #0
    sta kPtr+1
    ldy #0
elSubLp:
    lda (kPtr),y
    sta ibuffs,y
    iny
    cpy elShadowLen
    bcc elSubLp                 // loop while Y < len
elSubTerm:
    ldy elShadowLen
    lda #0
    sta ibuffs,y                // null-terminate
    lda elBuf; sta kPtr
    lda elBuf+1; sta kPtr+1
    rts                         // Y = elShadowLen
elSubEmpty:
    lda #0
    sta elShadowLen
    beq elSubTerm               // always (Z=1) -> terminate empty

// =====================================================
//  elDone: all handlers converge here
// =====================================================
elDone:
    jsr kBlinkReset             // move the blink block to the new cursor
    jmp elLoop

}
