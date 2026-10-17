// ==========================================
//  CHAR SHADOWS -- opt-in per-surface screen-code mirrors (bank 0)
// ==========================================
//  Revived from the dead text-model char shadows: a surface can mirror
//  its 1000 screen codes into a 1 KB REU region (SHADOW_BASE +
//  gfxSlotx$400, bank 0 -- $2000-$7FFF, the freed ex-color-snapshot
//  area) so text can be read back exactly -- no glyph OCR. Lives in the
//  free pocket after boot ($CDA6-$DFFF).
//
//  Opt-in: kSetCharShadow (A=1/0) for the CURRENT task. Disabled tasks
//  pay one flag check per printed char. printChar/clearScreen/
//  scrollScreen sync the shadow (hooks in screen-io.inc).
//
//  Eviction/restore: the surface slot survives pool eviction, so the
//  shadow region (same slot math, bank 0) survives with it. Restoring
//  spawns the task again, which clears the flag -- the entry must call
//  kSetCharShadow(1) BEFORE drawing (idempotent). A FRESH allocation
//  blanks the region in allocScreen, so a reused slot never exposes the
//  previous owner's text. freeScreen/sysKill/spawn/sharing clear the flag.

.pc = $CE00

// --- setCharShadow -- A=1 enable, A=0 disable for curTask ---
//  Clobbers: A, X, Y. C=0 always.
setCharShadow:
    pha
    ldx curTask
    lda screenAlloc,x
    bne scsHaveScr
    lda #0
    sta taskShadowFlag,x        // no surface -- shadow impossible
    pla
    clc
    rts
scsHaveScr:
    pla
    sta taskShadowFlag,x
    beq scsDone                 // disabling -- nothing to compute
    jsr shadowHiOf              // A = SHADOW_BASE + gfxSlotx$400 page hi
    ldx curTask
    sta taskShadowHi,x
scsDone:
    clc
    rts

// --- shadowHiOf -- A = shadow page hi (bank 0) for curTask's slot ---
//  Shadow = SHADOW_BASE + gfxSlotx$400 (lo always 0, bank 0).
//  Clobbers: A, X, Y, kArg..kArg+3, kResult.
shadowHiOf:
    ldx curTask
    lda taskGfxReuBank,x
    sec
    sbc #GFX_SLOT_BASE_BANK
    sta kArg+1                  // hi of slotx$28
    lda taskGfxReuHi,x
    sta kArg                    // lo of slotx$28
    lda #<$28
    sta kArg+2
    lda #>$28
    sta kArg+3
    jsr kDiv16                  // kResult = gfx slot (clobbers X, Y)
    lda kResult
    asl
    asl                         // slotx4
    clc
    adc #>SHADOW_BASE
    rts

// --- seedSlotCursorTail -- slot+$23e8/$23e9 = (0,0), +$23ea = 1 ---
//  Fresh surfaces must own a valid cursor tail: surfClearSlot preserves
//  the tail bytes, so a reused slot still holds the previous owner's
//  state -- a stale ON=0 there would gate the blink off for the new
//  owner (svRestCurOn loads the tail into live cursorOn every switch).
//  Input: X = task index (preserved). Clobbers: A, kPtr, kArg..kArg+5.
seedSlotCursorTail:
    lda #<cursorState
    sta kPtr
    lda #>cursorState
    sta kPtr+1                  // cursorState = live (0,0) -- home
    lda taskGfxReuLo,x
    clc
    adc #<(GFX_SLOT_MATRIX + GFX_CURSOR_COL)
    sta kArg
    lda taskGfxReuHi,x
    adc #>(GFX_SLOT_MATRIX + GFX_CURSOR_COL)
    sta kArg+1
    lda taskGfxReuBank,x
    adc #0
    sta kArg+2
    lda #2
    sta kArg+3
    lda #0
    sta kArg+4
    sta kArg+5
    jsr kReuFromC64             // tail col/row = (0,0)
    lda kArg
    clc
    adc #(GFX_CURSOR_ON - GFX_CURSOR_COL)
    sta kArg
    lda kArg+1
    adc #0
    sta kArg+1                  // +2 -> ON byte
    lda #1
    sta kArg+3                  // len = 1 (kArg+4 still 0 from above)
    lda #1
    jsr kReuFillReu             // tail ON = 1
    rts

// --- pcShadowChar -- mirror the char in kArg+6 to the shadow cell ---
//  Called from printChar (SEI held) and kReadString's DEL erase (SEI
//  held) when taskShadowFlag[curTask] != 0. Mirrors the cell at the
//  LIVE cursor -- both call sites write exactly there. X = curTask,
//  preserved. A clobbered.
pcShadowChar:
    ldy cursorRow
    lda gfxCellRowLutLo,y
    clc
    adc cursorCol
    sta $df04                   // REU lo = cell index (0-999)
    lda gfxCellRowLutHi,y
    adc taskShadowHi,x          // + shadow page hi (lo carry propagates)
    sta $df05
    lda #0
    sta $df06                   // bank 0
    lda #<$4006
    sta $df02                   // C64 source = kArg+6 (the char)
    lda #>$4006
    sta $df03
    lda #1
    sta $df07
    lda #0
    sta $df08
    sta $df0a
    lda #$90
    sta $df01
    jsr reuWait
    rts

// --- pcShadowScroll -- scroll the shadow's region up one row ---
//  Called from scrollScreenFocused when taskShadowFlag[curTask] != 0
//  (SEI held, cursorOn = 0). Moves rows (T+1)..B up one and blanks
//  row B with $20. X = curTask, preserved.
pcShadowScroll:
    lda taskScrollTop,x
    sta kArg+6                  // T
    lda taskScrollBottom,x
    sec
    sbc kArg+6                  // R = B - T
    sta kArg+7                  // R
    beq pcssClear               // R == 0 -> blank row B only

    // move rows (T+1)..B up one: R*40 bytes via scratchBuf
    ldy kArg+7                  // R
    lda gfxCellRowLutLo,y
    sta kArg+3
    lda gfxCellRowLutHi,y
    sta kArg+4                  // R*40
    ldy kArg+6                  // T
    lda gfxCellRowLutLo+1,y     // (T+1)*40 lo (shadow lo = 0)
    sta kArg
    lda gfxCellRowLutHi+1,y
    clc
    adc taskShadowHi,x          // + shadow page hi
    sta kArg+1
    lda #0; sta kArg+2          // bank 0
    sta kArg+5
    lda #<$C400; sta kPtr
    lda #>$C400; sta kPtr+1
    jsr kReuToC64               // shadow source -> scratchBuf
    lda kArg                    // source lo
    sec
    sbc #SCREEN_COLS            // - one row
    sta kArg                    // dest lo
    lda kArg+1                  // source hi (shadow hi included)
    sbc #0
    sta kArg+1                  // dest hi
    jsr kReuFromC64             // scratchBuf -> shadow dest

pcssClear:
    // blank row B (40B) -> $20
    ldy taskScrollBottom,x      // B
    lda gfxCellRowLutLo,y
    sta kArg
    lda gfxCellRowLutHi,y
    clc
    adc taskShadowHi,x
    sta kArg+1
    lda #0; sta kArg+2
    lda #SCREEN_COLS; sta kArg+3
    lda #0; sta kArg+4
    lda #$20
    jsr kReuFillReu             // X preserved
    rts

// --- charAt -- read a screen code back from the char shadow ---
//  Input:  A = col (0-39), Y = row (0-24) -- matches kGetCursor's X/Y
//  Output: A = screen code at (col,row); C=1 + A=$20 when the task has
//          no surface or the shadow is disabled
//  Clobbers: A, X, kArg+6/+7. Y preserved. SEI-guarded: the shadow
//  belongs to the CALLING task, so the read must be atomic vs switching.
charAt:
    sei
    sta kArg+7                  // col (survives the curTask reload)
    ldx curTask
    lda screenAlloc,x
    beq caUnavail
    lda taskShadowFlag,x
    beq caUnavail
    lda gfxCellRowLutLo,y       // row*40 lo
    clc
    adc kArg+7                  // + col
    sta $df04
    lda gfxCellRowLutHi,y
    adc taskShadowHi,x          // + shadow page hi (lo carry propagates)
    sta $df05
    lda #0
    sta $df06                   // bank 0
    lda #<$4006
    sta $df02
    lda #>$4006
    sta $df03                   // C64 dest = kArg+6
    lda #1
    sta $df07
    lda #0
    sta $df08
    sta $df0a
    lda #$91
    sta $df01                   // REU -> C64
    jsr reuWait
    lda kArg+6
    cli
    clc
    rts
caUnavail:
    lda #$20
    cli
    sec
    rts

//  End of the char-shadow block. $CFA4+ holds the sprite tables (sprite.inc);
//  main.asm asserts this stays below them -- before this label existed there
//  was no way for that assert to catch this 420B block growing into its
//  neighbour.
.label charShadowEnd = *
