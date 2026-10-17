// ==========================================
//  GordonOS -- Per-surface scroll region
// ==========================================
//  kSetScrollRegion(A=top, X=bottom) narrows the scroll region; the
//  cursor clamp and auto-scroll respect it. The region is per-surface:
//  it persists in the slot matrix tail (+$23EE = top, +$23EF = bottom)
//  and travels with the surface via surfSave/surfLoad (the framebuffer
//  copies live at $07EE/$07EF).
//
//  These routines now live in the high region right after keyboard.inc
//  Jump-table entries: kSetScrollRegion ($E0E7),
//  kGetScrollRegion ($E0EA).

.pc = *

// --- slotRegionAddr -- kArg..kArg+2 = slot matrix tail REU address ---
//  Input: X = task (preserved). Clobbers A. Caller must hold SEI.
slotRegionAddr:
    lda taskGfxReuLo,x
    clc
    adc #<(GFX_SLOT_MATRIX + GFX_SCROLL_TOP)
    sta kArg
    lda taskGfxReuHi,x
    adc #>(GFX_SLOT_MATRIX + GFX_SCROLL_TOP)
    sta kArg+1
    lda taskGfxReuBank,x
    adc #0
    sta kArg+2
    rts

// --- seedSlotRegion -- write taskScrollTop/Bottom[X] to the slot tail ---
//  Input: X = task (preserved). Caller must hold SEI (scrollSeedBuf is
//  a shared 2-byte scratch). Clobbers A, Y, kPtr, kArg..kArg+5.
seedSlotRegion:
    lda taskScrollTop,x
    sta scrollSeedBuf
    lda taskScrollBottom,x
    sta scrollSeedBuf+1
    jsr slotRegionAddr
    lda #<scrollSeedBuf
    sta kPtr
    lda #>scrollSeedBuf
    sta kPtr+1
    lda #2
    sta kArg+3
    lda #0
    sta kArg+4
    sta kArg+5
    jsr kReuFromC64
    rts

// --- loadSlotRegion -- read slot tail -> scrollSeedBuf -> ZP arrays ---
//  Input: X = task (preserved). Caller must hold SEI. Restores the
//  evicted / switched-in surface's region. Clobbers A, Y, kPtr,
//  kArg..kArg+5.
loadSlotRegion:
    jsr slotRegionAddr
    lda #<scrollSeedBuf
    sta kPtr
    lda #>scrollSeedBuf
    sta kPtr+1
    lda #2
    sta kArg+3
    lda #0
    sta kArg+4
    sta kArg+5
    jsr kReuToC64
    lda scrollSeedBuf
    sta taskScrollTop,x
    lda scrollSeedBuf+1
    sta taskScrollBottom,x
    rts

// --- setScrollRegion -- kSetScrollRegion entry ---
//  Input: A = top (inclusive), X = bottom (inclusive).
//  Invalid (C=1, state unchanged): X >= 25 or A > X. A == X valid.
//  Valid (C=0): stores the ZP arrays; if the surface exists, seeds the
//  slot tail; if the surface is displayed, also writes the framebuffer
//  tail so surfSave cannot clobber the slot copy with stale bytes.
//  Clobbers A, X, Y, kPtr, kArg..kArg+6.
setScrollRegion:
    cpx #SCREEN_ROWS
    bcs srrBad                  // bottom >= SCREEN_ROWS -> invalid
    stx kArg+6                  // bottom (A=top survives the cmp)
    cmp kArg+6
    beq srrOk                   // top == bottom -> valid
    bcs srrBad                  // top > bottom -> invalid
srrOk:
    sei
    ldx curTask
    sta taskScrollTop,x
    lda kArg+6
    sta taskScrollBottom,x
    lda screenAlloc,x
    beq srrNoSlot               // no surface -- ZP only
    jsr seedSlotRegion          // slot tail (X preserved)
    jsr surfDisplayed           // C=1 if displayed (clobbers A,X,Y)
    ldx curTask
    bcc srrNoSlot               // background -- framebuffer tail untouched
    lda taskScrollTop,x
    sta GFX_MATRIX_BASE + GFX_SCROLL_TOP
    lda taskScrollBottom,x
    sta GFX_MATRIX_BASE + GFX_SCROLL_BOTTOM
srrNoSlot:
    clc
    cli
    rts
srrBad:
    sec
    rts

// --- getScrollRegion -- kGetScrollRegion entry ---
//  Output: A = top, X = bottom, C=0. Y preserved.
getScrollRegion:
    ldx curTask
    lda taskScrollTop,x
    pha
    lda taskScrollBottom,x
    tax
    pla
    clc
    rts

// --- wrapClamp -- cursorCol already reset; cursorRow just incremented ---
//  Clamps cursorRow to taskScrollBottom[curTask]; if it passed the bottom
//  on a displayed surface, scrolls. Shared by printChar/printGlyph/newline
//  (the $E000+ region has no slack for three inline copies). Preserves X
//  (callers may keep a loop counter there across kNewline).
wrapClamp:
    txa
    pha                         // preserve X (Rule 3)
    ldx curTask
    lda cursorRow
    cmp taskScrollBottom,x      // C=1 if row >= bottom, Z=1 if row == bottom
    bcc wcDone                  // row < bottom -> nothing
    beq wcDone                  // row == bottom -> cursor stays
    dec cursorRow               // clamp cursorRow to region bottom
    jsr surfDisplayed
    bcc wcDone                  // background -> no scroll (section 9 char-cache)
    jsr scrollScreen
wcDone:
    pla
    tax                         // restore X
    rts

// --- scrollScreenFocused -- scroll the DISPLAYED surface's region ---
//  Called from scrollScreen under SEI with cursorOn = 0 (dispatcher owns
//  both). Scrolls rows T..B up one (T/B = taskScrollTop/Bottom[curTask]),
//  blanks row B. R == 0 (single-row region) skips the move and just
//  blanks row B. Clobbers A, X, Y, kPtr, kArg block.
scrollScreenFocused:
    jsr eraseCursor             // un-XOR the block cursor before the move

    ldx curTask
    lda taskScrollTop,x
    sta kArg+6                  // T
    lda taskScrollBottom,x
    sec
    sbc kArg+6                  // A = B - T = R
    sta kArg+7                  // R
    beq ssfClear                // R == 0 -> blank row B only

    // --- pixels: rows (T+1)..B -> rows T..(B-1), R*320 bytes ---
    ldy kArg+7                  // R
    lda gfxRowLutLo,y           // R*320 lo ($2000 lo byte = 0)
    sta kArg+3
    lda gfxRowLutHi,y           // R*320 hi + $20
    sec
    sbc #>$2000
    sta kArg+4                  // R*320 hi
    ldy kArg+6                  // T
    iny                         // T+1
    lda gfxRowLutLo,y
    sta kPtr
    lda gfxRowLutHi,y
    sta kPtr+1                  // source = $2000 + (T+1)*320
    lda #<REU_BOUNCE; sta kArg
    lda #>REU_BOUNCE; sta kArg+1
    lda #0; sta kArg+2
    sta kArg+5
    jsr kReuFromC64             // source -> bounce
    ldy kArg+6                  // T
    lda gfxRowLutLo,y
    sta kPtr
    lda gfxRowLutHi,y
    sta kPtr+1                  // dest = $2000 + T*320
    jsr kReuToC64               // bounce -> dest

ssfClear:
    // --- blank bottom pixel row B ---
    lda kArg+6                  // T
    clc
    adc kArg+7                  // + R = B
    tay
    lda gfxRowLutLo,y
    sta kPtr
    lda gfxRowLutHi,y
    sta kPtr+1                  // $2000 + B*320
    lda #<$0140; sta kArg
    lda #>$0140; sta kArg+1     // 320
    lda #0
    jsr kReuFillC64             // blank row B pixels

    // --- matrix: rows (T+1)..B -> rows T..(B-1), R*40 bytes ---
    lda kArg+7
    beq ssfClearMatrix          // R == 0 -> skip matrix move
    ldy kArg+7                  // R
    lda gfxCellRowLutLo,y
    sta kArg+3
    lda gfxCellRowLutHi,y
    sta kArg+4                  // R*40
    ldy kArg+6                  // T
    iny
    lda gfxCellRowLutLo,y
    sta kPtr
    lda gfxCellRowLutHi,y
    clc
    adc #>$0400
    sta kPtr+1                  // source = $0400 + (T+1)*40
    lda #<REU_BOUNCE; sta kArg
    lda #>REU_BOUNCE; sta kArg+1
    lda #0; sta kArg+2
    sta kArg+5
    jsr kReuFromC64
    ldy kArg+6                  // T
    lda gfxCellRowLutLo,y
    sta kPtr
    lda gfxCellRowLutHi,y
    clc
    adc #>$0400
    sta kPtr+1                  // dest = $0400 + T*40
    jsr kReuToC64

ssfClearMatrix:
    // --- blank bottom matrix row B ---
    lda kArg+6                  // T
    clc
    adc kArg+7                  // + R = B
    tay
    lda gfxCellRowLutLo,y
    sta kPtr
    lda gfxCellRowLutHi,y
    clc
    adc #>$0400
    sta kPtr+1                  // $0400 + B*SCREEN_COLS
    lda #SCREEN_COLS; sta kArg
    lda #0; sta kArg+1
    ldx curTask
    lda taskGfxBg0,x
    jsr kReuFillC64             // blank row B matrix (bg0)

    // --- char shadow (region-aware) ---
    ldx curTask                 // kReuFillC64 clobbers X
    lda taskShadowFlag,x
    beq ssfDone
    jsr pcShadowScroll
ssfDone:
    rts

.label scrollRegionEnd = *

// ==========================================
//  blitCharAt / blitCharAtRev -- fixed-position glyph blit (kBlitChar /
//  kBlitCharRev). Draw a font glyph + matrix colour nibble at an explicit
//  cell with NO cursor advance and NO char-shadow mirror -- the primitive
//  for fixed UI (status bars). Works on the DISPLAYED surface only (like
//  printCharAt). This block closes the file's high fragment: main.asm
//  asserts that its end stays below lib.inc.
// ==========================================
//  Input: A = char (screencode), X = col (0-39), Y = row (0-24)
//  Clobbers: A, X, Y, kPtr, srcPtr, kArg..kArg+7
//  Both entries share one body: the stub sets the orientation in C and the
//  body carries it across blitCharAtBlit on the stack (that call clobbers C,
//  and the stack is per-task, so it survives the balanced REU subroutines).
//  Normal (C=0) -> matrix nibble = (fg<<4) | bg0, reverse (C=1) -> (bg0<<4) | fg.
//  Reverse makes an idempotent bar: the glyph colour comes from taskGfxBg0,
//  never from the bar-coloured cell, so re-drawing it cannot fade the text.
blitCharAt:
    clc
    bcc blitAtEnter             // always taken: skip the reverse stub
blitCharAtRev:
    sec
blitAtEnter:
    php                         // orientation flag (C) -> stack
    sei                         // atomic vs F-key display switches
    sta kArg+6                  // char (blitChar clobbers kArg..kArg+3)
    stx kArg+2                  // col
    sty kArg+3                  // row
    ldx curTask
    lda taskTextColor,x
    sta kArg+5                  // fg colour for the glyph DMA
    jsr blitCharAtBlit          // glyph -> framebuffer, kPtr = matrix cell
    plp                         // orientation flag back (C)
    ldx curTask
    bcc blitAtNormal            // C=0 -> high nibble = fg
    lda taskGfxBg0,x            // reverse: high nibble = bg0
    ldy taskTextColor,x         //          low  nibble = fg
    jmp blitAtNibble
blitAtNormal:
    lda taskTextColor,x         // high nibble = fg
    ldy taskGfxBg0,x            // low  nibble = bg0
blitAtNibble:
    sty kArg+7                  // both colours are re-read after the blit --
                                // the glyph DMA clobbers the kArg scratch
    asl
    asl
    asl
    asl                         // high nibble << 4
    ora kArg+7
    ldy #0
    sta (kPtr),y
    cli
    rts

// --- blitCharAtBlit -- shared: glyph -> framebuffer, kPtr = matrix cell ---
blitCharAtBlit:
    lda cursorVisible           // either fixed blitter can strand the live
    beq blitNoErase             // block if it draws over the saved cell
    lda GFX_MATRIX_BASE + GFX_CURSOR_SCOL
    cmp kArg+2
    bne blitNoErase
    lda GFX_MATRIX_BASE + GFX_CURSOR_SROW
    cmp kArg+3
    bne blitNoErase
    lda cursorOn
    pha
    lda #0
    sta cursorOn
    jsr eraseCursor
    pla
    sta cursorOn
blitNoErase:
    ldx kArg+2                  // col
    ldy kArg+3                  // row
    lda kArg+6                  // char
    jsr blitChar                // glyph (kArg+2/+3 survive)
    ldy kArg+3                  // row
    lda gfxCellRowLutLo,y
    clc
    adc kArg+2                  // + col
    sta kPtr
    lda gfxCellRowLutHi,y
    adc #>$0400
    sta kPtr+1                  // kPtr = $0400 + row*40 + col
    rts
.label blitCharAtEnd = *

// ==========================================
//  paRoute -- the surface router for kPrintCharAt.
//  Mirrors the char shadow, then routes the single-cell write: the task's
//  surface displayed -> C=1 and nothing drawn, so the caller blits to the
//  framebuffer; background -> glyph and matrix nibble go into the task's REU
//  slot via blitCharSlot, C=0, cell already written.
//  printChar and printGlyph keep their own inline copies of this decision --
//  their band is ABI-pinned -- so this block lives in the pocket under
//  gfx.inc's $ECDC anchor, which is what leaves the $ECDC-$FC3E chain its
//  growth room. It fills that 31B pocket exactly.
//  Input:  kArg+5 = color nibble, kArg+6 = char, cursorCol/cursorRow = cell
//  Output: C=1 displayed (nothing drawn), C=0 cell written to the REU slot
//  Clobbers: A, X, Y, srcPtr, kPtr, kArg..kArg+6 (blitCharSlot)
// ==========================================
paRoute:
    ldx curTask
    lda taskShadowFlag,x
    beq prRoute
    jsr pcShadowChar            // mirror kArg+6 to (cursorCol, cursorRow)
prRoute:
    ldx curTask                 // surfDisplayed keys on the task index
    jsr surfDisplayed
    bcs prRts                   // displayed -> the caller blits to the framebuffer
    lda kArg+6
    ldx cursorCol
    ldy cursorRow
    jsr blitCharSlot            // glyph + matrix nibble into the slot
    clc
prRts:
    rts
.label paRouteEnd = *
