// ==========================================
//  EhBASIC Full-Screen Line Editor -- PRESERVED REFERENCE
// ==========================================
//  Extracted verbatim from src/tasks/dynamic/ehbasic.asm (2026-08-16).
//
//  This is the pre-bitmap kEditLine shadow-buffer editor that used to be
//  called by vecInln. It was WRITTEN FOR THE OLD TEXT-MODE SCREEN MODEL:
//
//    - elRender     writes glyphs via `sta (kPtr),y` + color RAM `$D800`
//                   (framebuffer cells are now 8 bytes, not 1 -- broken)
//    - elScanRow    reads screen codes back via `lda (kPtr),y`
//                   (now reads glyph BYTES -- broken)
//    - elRestChar   clears bit 7 of the screen code + color-RAM restore
//                   (old reverse-video blink -- broken on bitmap)
//    - elDone/elRetCleanup  write the old per-screen tail
//                   (`screenBase+SCR_CURSOR_COL` -- stale)
//
//  PORTED (2026-08-19 -> 2026-08-20): the editor now
//  lives in src/tasks/dynamic/basicedit.inc and vecInln calls `kEditLine`.
//  The port keeps this file's INSERT/DELETE/multi-row-wrap/history logic
//  and swaps only the screen primitives:
//
//    - elRender    -> kPrintCharAt loop + kCsrBoth cursor save/restore
//                    (kPrintCharAt writes a cell with NO cursor advance/
//                    scroll, so a line may sit on row 24)
//    - elScanRow   -> kReadRow (1-DMA char-shadow row read)
//    - elReturn    -> multi-row submit: merges a wrapped logical line on
//                    RETURN and lands the cursor on the next program line
//    - elRestChar  -> dropped; the shared eraseCursor un-draws the blink
//    - elDone/elRetCleanup -> dropped; cursor/blink are kernel-managed
//
//  This file is historical reference only -- do not edit the live editor
//  here.
//
//  Kernel-call discipline: only kScanKeyboard,
//  kEnableCursor/kDisableCursor, kCursorAddr, kSetCursor, kGetCursor,
//  kPrintChar/kPrintString, kNewline. Read-only access to
//  cursorRow/cursorCol/cursorVisible/cursorOn/blinkTick/screenBase.

// ==========================================
//  kEditLine -- Full-Screen Line Editor
// ==========================================

elBuf:
    .word 0                     // saved output buffer pointer (kPtr from caller)
elMax:
    .byte 0                     // saved max length
elChar:
    .byte 0                     // temp for character during dispatch
elTemp:
    .byte 0                     // temp: cursorOn save across elRender
elStartRow:
    .byte 0                     // first screen row for rendered content
elShadowPos:
    .byte 0                     // cursor position in shadow buffer (0..elShadowLen)
elShadowLen:
    .byte 0                     // current text length (0..126)

// --- Entry point ---
editLine:
    lda #7; jsr syscall         // gettid (safe: SEI inside syscall)
    tay
    cpy kbFocus
    beq elGo                    // we own the keyboard
    ldy #0; rts                 // not focused, return empty

// --- elGo: initialise editor, load current screen row ---
elGo:
    stx elMax                   // save maxLen from caller
    lda kPtr; sta elBuf         // save output buffer pointer
    lda kPtr+1; sta elBuf+1
    jsr kEnableCursor           // cursor on for this screen (clobbers A,Y)
    lda cursorRow; sta elStartRow // remember input row
    lda #0
    sta cursorVisible           // fresh blink cycle
    lda #BLINK_RATE; sta blinkTick
    jsr elScanRow               // load current screen row into shadow buffer
    // fall into elLoop

// --- elLoop: poll keyboard, dispatch ---
elLoop:
    lda #7; jsr syscall         // gettid (safe: SEI inside syscall)
    tay
    cpy kbFocus
    beq elFocused
    ldy #0; rts                 // lost focus
elFocused:
    jsr kScanKeyboard           // clobbers A,X,Y
    cmp #$ff; beq elLoop        // no key
    // Dispatch chain (all far targets via jmp)
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
    jmp elPrintable             // far target -- jmp always

// =====================================================
//  Shadow-Buffer Handlers
//  All handlers: modify shadow buffer, call elRender,
//  then elSyncCursor, then jmp elDone.
// =====================================================

// --- elPrintable: overwrite char at cursor (C64 screen editor) ---
elPrintable:
    ldx elShadowLen
    cpx elMax; bcc elPrOk       // buffer full? no -> continue
    jmp elDone                  // buffer full -> no-op
elPrOk:
    sta elChar                  // save char
    jsr elRestChar              // undo blink on
    ldy elShadowPos
    lda elChar
    sta ibuffs,y                // overwrite at cursor position
    cpy elShadowLen             // at end of line?
    bcc elPrAdvance             // within text -> don't extend length
    inc elShadowLen             // at end -> extend buffer
elPrAdvance:
    inc elShadowPos
    jsr elRender
    jsr elSyncCursor
    jmp elDone

// --- elDelete: delete char before cursor ---
elDelete:
    lda elShadowPos; bne elDelOk // at start? no -> continue
    jmp elDone                  // at start -> no-op
elDelOk:
    jsr elRestChar
    dec elShadowPos
    // Shift elShadowPos+1..elShadowLen-1 left by 1
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

// --- elInsert: SHIFT+DEL -- insert space at cursor (cursor doesn't move) ---
elInsert:
    ldx elShadowLen
    cpx elMax; bcc elInsOk      // buffer full? no -> continue
    jmp elDone
elInsOk:
    jsr elRestChar
    // Shift right (same as printable, but skip inc elShadowPos)
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
    // elShadowPos unchanged -- cursor stays
    jsr elRender
    jsr elSyncCursor
    jmp elDone

// --- elRight: cursor right, wrap to next row at end of line ---
elRight:
    lda cursorCol
    cmp elShadowLen; bcs elRWrap   // at/past end of line text -> wrap
    jsr elRestChar
    inc cursorCol
    inc elShadowPos
    ldx cursorCol
    ldy cursorRow
    jsr kSetCursor
    jmp elDone
elRWrap:
    // At or past end of text -> wrap to next row
    lda cursorRow; cmp #24; bcc elRWrapOk  // bottom row, can't wrap
    jmp elDone
elRWrapOk:
    jsr elRestChar
    inc cursorRow
    lda #0; sta cursorCol
    lda cursorRow; sta elStartRow
    jsr elScanRow               // load new row -> ibuffs (elShadowPos set)
    ldx cursorCol
    ldy cursorRow
    jsr kSetCursor
    jmp elDone

// --- elLeft: cursor left, wrap to previous row at column 0 ---
elLeft:
    lda cursorCol; bne elLDec
    // At column 0 -> wrap to end of previous row
    lda cursorRow; bne elLDecOk   // at top, can't wrap
    jmp elDone
elLDecOk:
    jsr elRestChar
    dec cursorRow
    lda cursorRow; sta elStartRow
    jsr elScanRow               // load row -> ibuffs
    // Position at end of new row's text
    lda elShadowLen; sta elShadowPos
    sta cursorCol
    ldx cursorCol
    ldy cursorRow
    jsr kSetCursor
    jmp elDone
elLDec:
    jsr elRestChar
    dec cursorCol
    lda cursorCol
    sta elShadowPos
    ldx cursorCol
    ldy cursorRow
    jsr kSetCursor
    jmp elDone

// --- elHome: cursor to start ---
elHome:
    jsr elRestChar              // undo blink before moving
    lda #0; sta elShadowPos
    jsr elSyncCursor
    jmp elDone

// --- elDown: cursor down one row, load row into shadow buffer ---
elDown:
    lda cursorRow; cmp #24; bcc elDownGo
    jmp elDone
elDownGo:
    jsr elRestChar              // undo blink before moving
    inc cursorRow
    lda cursorRow; sta elStartRow
    jsr elScanRow               // load screen row -> ibuffs
    ldx cursorCol
    ldy cursorRow
    jsr kSetCursor
    jmp elDone

// --- elUp: cursor up one row, load row into shadow buffer ---
elUp:
    lda cursorRow; bne elUpGo
    jmp elDone
elUpGo:
    jsr elRestChar              // undo blink before moving
    dec cursorRow
    lda cursorRow; sta elStartRow
    jsr elScanRow               // load screen row -> ibuffs
    ldx cursorCol
    ldy cursorRow
    jsr kSetCursor
    jmp elDone

// --- elScanRow: read screen row at cursorRow into ibuffs ---
//  Sets elShadowLen, elShadowPos (clamped to row length).
//  Clobbers: A,X,Y,kPtr
elScanRow:
    lda #0; sta cursorCol
    jsr kCursorAddr             // kPtr = screen addr at (row, 0)
    ldy #0
elScanRd:
    cpy #40; bcs elScanEnd
    lda (kPtr),y                // read screen code from row
    sta ibuffs,y                // store in buffer
    iny
    bne elScanRd
elScanEnd:
    // Y = 40; trim trailing spaces
    dey                         // Y = 39
elScanTrim:
    lda ibuffs,y
    cmp #$20; bne elScanGotLen
    dey
    bpl elScanTrim
    ldy #$ff                    // all spaces -> empty
elScanGotLen:
    iny                         // Y = length (0-40)
    sty elShadowLen
    lda cursorCol
    cmp elShadowLen
    bcc elScanClampDone
    lda elShadowLen             // clamp cursorCol to line length
elScanClampDone:
    sta elShadowPos
    sta cursorCol
    rts

// --- Shortcut for elDone ---
elToDone:
    jmp elDone

// =====================================================
//  elRender: render shadow buffer to screen
//  Handles bottom-of-screen overflow via kScrollScreen.
//  Clobbers: A,X,Y,kPtr,srcPtr (via kernel calls)
//  Saves/restores cursorOn via elTemp.
// =====================================================
elRender:
    ldx cursorOn
    stx elTemp                  // save cursorOn
    lda #0
    jsr csrBoth                 // disable cursor + tail sync (Bug #34)

    // Read text color from screen tail -> elChar
    lda screenBase; clc; adc #SCR_TEXT_COLOR; sta srcPtr
    lda screenBase+1; adc #$03; sta srcPtr+1
    ldy #0; lda (srcPtr),y
    sta elChar                  // text color for color RAM writes

    // Compute rows needed = (elShadowLen + 39) / 40 (ceiling)
    lda elShadowLen
    bne elRNotEmpty             // has content -> render it
    jmp elRClear                // empty -> clear one row
elRNotEmpty:
    clc; adc #39
    ldy #0
elRDiv:
    cmp #40; bcc elRDivDone
    sbc #40
    iny
    bne elRDiv
elRDivDone:
    // Y = rows needed
    tya
    clc; adc elStartRow
    cmp #25; bcc elRGo          // fits on screen

    // Overflow: scroll screen up to make room
    sec; sbc #25
    beq elRGo
    tax
elRScroll:
    txa; pha
    jsr kScrollScreen
    pla; tax
    dec elStartRow
    dex
    bne elRScroll

elRGo:
    ldx #0
    stx cursorCol
    lda elStartRow; sta cursorRow
    jsr kCursorAddr             // kPtr = screen RAM addr

elRLoop:
    cpx elShadowLen; bcs elRPad
    ldy cursorCol
    lda ibuffs,x
    sta (kPtr),y                // screen RAM
    // Color RAM = $D800 + (kPtr + col - screenBase)
    txa; pha                    // save buffer index X
    tya; clc; adc kPtr
    tax
    lda kPtr+1; adc #0
    sec; sbc screenBase+1
    clc; adc #>$d800
    sta srcPtr+1
    txa; sec; sbc screenBase
    adc #<$d800
    sta srcPtr
    lda elChar
    ldy #0; sta (srcPtr),y      // color RAM
    ldy cursorCol               // restore col from memory
    pla; tax                    // restore buffer index X
    inx
    inc cursorCol
    lda cursorCol; cmp #40; bcc elRLoop
    lda #0; sta cursorCol
    inc cursorRow
    jsr kCursorAddr
    jmp elRLoop

elRPad:
    ldy cursorCol
    beq elRDone                 // wrapped to next row -> nothing to pad
    cpy #40; bcs elRDone
elRPadLp:
    lda #$20; sta (kPtr),y      // screen RAM
    // Color RAM for pad space
    tya; clc; adc kPtr; tax
    lda kPtr+1; adc #0
    sec; sbc screenBase+1; clc; adc #>$d800; sta srcPtr+1
    txa; sec; sbc screenBase; adc #<$d800; sta srcPtr
    lda elChar
    pha; ldy #0; pla             // restore A=elChar, Y=0
    sta (srcPtr),y               // color RAM
    ldy cursorCol                // restore col
    iny
    sty cursorCol
    cpy #40; bcc elRPadLp

elRDone:
    lda elTemp
    jsr csrBoth                 // restore cursorOn + tail sync (Bug #34)
    rts

elRClear:
    // Read text color too
    lda screenBase; clc; adc #SCR_TEXT_COLOR; sta srcPtr
    lda screenBase+1; adc #$03; sta srcPtr+1
    ldy #0; lda (srcPtr),y
    sta elChar
    lda #0; sta cursorCol
    lda elStartRow; sta cursorRow
    jsr kCursorAddr
    ldy #0
elRClrLp:
    lda #$20; sta (kPtr),y      // screen RAM
    // Color RAM for this space
    tya; clc; adc kPtr; tax
    lda kPtr+1; adc #0
    sec; sbc screenBase+1; clc; adc #>$d800; sta srcPtr+1
    txa; sec; sbc screenBase; adc #<$d800; sta srcPtr
    lda elChar
    pha; ldy #0; pla
    sta (srcPtr),y               // color RAM
    ldy cursorCol
    iny
    sty cursorCol
    cpy #40; bcc elRClrLp
    lda elTemp
    jsr csrBoth                 // restore cursorOn + tail sync (Bug #34)
    rts

// =====================================================
//  elSyncCursor: map elShadowPos -> cursorRow/cursorCol
//  Clobbers: A,Y (kSetCursor preserves X & Y)
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
    sta cursorCol               // col = remainder
    tya
    clc; adc elStartRow
    sta cursorRow               // row = startRow + quotient
    ldx cursorCol
    ldy cursorRow
    jsr kSetCursor              // preserves X & Y
    rts

// =====================================================
//  elReturn: submit buffer directly (no screen scanning)
// =====================================================
elReturn:
    jsr elRestChar
    jsr kDisableCursor          // clobbers A,Y
    // Null-terminate shadow buffer
    ldy elShadowLen
    lda #0
    sta ibuffs,y                // buffer is ibuffs (EhBASIC input buffer)
    // Restore kPtr = elBuf (output destination)
    lda elBuf; sta kPtr
    lda elBuf+1; sta kPtr+1
    // Y already = elShadowLen
    jmp elRetCleanup

// =====================================================
//  elRestChar -- undo blink ON (unchanged from Step 1-4)
// =====================================================
elRestChar:
    lda cursorVisible
    beq elRestRts
    php; sei
    ldx cursorOn
    lda #0
    jsr csrBoth                 // disable cursor + tail sync (Bug #34)
    jsr kCursorAddr
    ldy #0; lda (kPtr),y
    and #$7f; sta (kPtr),y
    sec
    lda kPtr; sbc screenBase; sta srcPtr
    lda kPtr+1; sbc screenBase+1; clc; adc #$d8; sta srcPtr+1
    lda screenBase; clc; adc #SCR_CURSOR_SAVED_COLOR; sta kPtr
    lda screenBase+1; adc #$03; sta kPtr+1
    ldy #0; lda (kPtr),y
    sta (srcPtr),y
    txa
    jsr csrBoth                 // restore cursorOn + tail sync (Bug #34)
    plp
elRestRts:
    rts

// =====================================================
//  elDone -- all handlers converge here
// =====================================================
elDone:
    lda #0; sta cursorVisible    // force next blink ON
    lda #1; sta blinkTick
    lda screenBase; clc; adc #SCR_CURSOR_COL; sta kPtr
    lda screenBase+1; adc #$03; sta kPtr+1
    ldy #0
    lda cursorCol; sta (kPtr),y
    iny
    lda cursorRow; sta (kPtr),y
    jmp elLoop

// =====================================================
//  elRetCleanup -- disable blink, save cursor tail, rts
//  Entry: Y = length
// =====================================================
elRetCleanup:
    tya; pha
    lda #0; sta cursorVisible; sta blinkTick
    sty cursorCol
    lda screenBase; clc; adc #SCR_CURSOR_COL; sta kPtr
    lda screenBase+1; adc #$03; sta kPtr+1
    ldy #0
    lda cursorCol; sta (kPtr),y
    iny
    lda cursorRow; sta (kPtr),y
    pla; tay
    rts
