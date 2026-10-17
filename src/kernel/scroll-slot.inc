// ==========================================
//  scrollScreenSlot -- background slot scroll, below the pool
// ==========================================
//  This routine lives at $54AB, right after libstubs.inc, in the pages below
//  POOL_BASE (`.assert scrollSlotEnd <= POOL_BASE` guards the boundary).
//  Called via the scrollScreen dispatcher under SEI with cursorOn = 0.
.pc = $54AB

// --- scrollScreenSlot -- scroll the BACKGROUND surface's slot region ---
//  Called from scrollScreen under SEI with cursorOn = 0. The slot lives
//  in REU, so each row round-trips through C64 scratchBuf (REU can't do
//  REU->REU). Moves rows (T+1)..B up one, blanks row B. R == 0 blanks
//  row B only. X = curTask (reloaded -- the dispatcher's surfDisplayed
//  clobbers X on the sharee path). Clobbers A, X, Y, kPtr, kArg,
//  scrollSeedBuf.
scrollScreenSlot:
    ldx curTask                 // surfDisplayed clobbers X on the sharee path
    lda taskScrollTop,x
    sta kArg+6                  // T
    lda taskScrollBottom,x
    sta kArg+7                  // B
    sec
    sbc kArg+6                  // R = B - T
    bne sssMove                 // R != 0 -> do the move
    jmp sssClear                // R == 0 -> blank row B only (far)
sssMove:

    ldy kArg+6                  // T
    iny                         // r = T+1
sssLoop:
    tya
    sec
    sbc kArg+7                  // r - B
    beq sssRow                  // r == B -> process (last)
    bcc sssRow                  // r < B -> process
    jmp sssClear                // r > B -> done (move finished)
sssRow:
    // --- pixel row r -> r-1 (320B via scratchBuf) ---
    lda gfxRowLutLo,y           // r*320 lo ($2000 lo byte = 0)
    sta scrollSeedBuf
    lda gfxRowLutHi,y           // r*320 hi + $20
    sec
    sbc #>$2000
    sta scrollSeedBuf+1         // r*320
    lda taskGfxReuLo,x
    clc
    adc scrollSeedBuf
    sta kArg
    lda taskGfxReuHi,x
    adc scrollSeedBuf+1
    sta kArg+1
    lda taskGfxReuBank,x
    adc #0
    sta kArg+2                  // source = slot + r*320
    lda #<$0140; sta kArg+3
    lda #>$0140; sta kArg+4
    lda #0; sta kArg+5
    lda #<$C400; sta kPtr
    lda #>$C400; sta kPtr+1
    jsr kReuToC64               // slot source -> scratchBuf
    lda scrollSeedBuf
    sec
    sbc #<$0140
    sta scrollSeedBuf
    lda scrollSeedBuf+1
    sbc #>$0140
    sta scrollSeedBuf+1         // (r-1)*320
    lda taskGfxReuLo,x
    clc
    adc scrollSeedBuf
    sta kArg
    lda taskGfxReuHi,x
    adc scrollSeedBuf+1
    sta kArg+1
    lda taskGfxReuBank,x
    adc #0
    sta kArg+2                  // dest = slot + (r-1)*320
    lda #<$0140; sta kArg+3
    lda #>$0140; sta kArg+4
    lda #0; sta kArg+5
    lda #<$C400; sta kPtr
    lda #>$C400; sta kPtr+1
    jsr kReuFromC64             // scratchBuf -> slot dest

    // --- matrix row r -> r-1 (40B via scratchBuf) ---
    lda gfxCellRowLutLo,y
    sta scrollSeedBuf
    lda gfxCellRowLutHi,y
    sta scrollSeedBuf+1         // r*40
    lda taskGfxReuLo,x
    clc
    adc scrollSeedBuf
    sta kArg
    lda taskGfxReuHi,x
    adc scrollSeedBuf+1
    sta kArg+1
    lda kArg+1
    clc
    adc #>$2000
    sta kArg+1
    lda taskGfxReuBank,x
    adc #0
    sta kArg+2                  // source = slot + $2000 + r*SCREEN_COLS
    lda #SCREEN_COLS; sta kArg+3
    lda #0; sta kArg+4
    sta kArg+5
    lda #<$C400; sta kPtr
    lda #>$C400; sta kPtr+1
    jsr kReuToC64
    lda scrollSeedBuf
    sec
    sbc #SCREEN_COLS
    sta scrollSeedBuf
    lda scrollSeedBuf+1
    sbc #0
    sta scrollSeedBuf+1         // (r-1)*40
    lda taskGfxReuLo,x
    clc
    adc scrollSeedBuf
    sta kArg
    lda taskGfxReuHi,x
    adc scrollSeedBuf+1
    sta kArg+1
    lda kArg+1
    clc
    adc #>$2000
    sta kArg+1
    lda taskGfxReuBank,x
    adc #0
    sta kArg+2                  // dest = slot + $2000 + (r-1)*SCREEN_COLS
    lda #SCREEN_COLS; sta kArg+3
    lda #0; sta kArg+4
    sta kArg+5
    lda #<$C400; sta kPtr
    lda #>$C400; sta kPtr+1
    jsr kReuFromC64
    iny
    jmp sssLoop

sssClear:
    // --- blank bottom pixel row B ---
    ldy kArg+7                  // B
    lda gfxRowLutLo,y           // B*320 lo ($2000 lo byte = 0)
    sta scrollSeedBuf
    lda gfxRowLutHi,y           // B*320 hi + $20
    sec
    sbc #>$2000
    sta scrollSeedBuf+1         // B*320
    lda taskGfxReuLo,x
    clc
    adc scrollSeedBuf
    sta kArg
    lda taskGfxReuHi,x
    adc scrollSeedBuf+1
    sta kArg+1
    lda taskGfxReuBank,x
    adc #0
    sta kArg+2
    lda #<$0140; sta kArg+3
    lda #>$0140; sta kArg+4
    lda #0
    jsr kReuFillReu             // blank row B pixels (X preserved)

    // --- blank bottom matrix row B ---
    ldy kArg+7                  // B
    lda gfxCellRowLutLo,y
    sta scrollSeedBuf
    lda gfxCellRowLutHi,y
    sta scrollSeedBuf+1
    lda taskGfxReuLo,x
    clc
    adc scrollSeedBuf
    sta kArg
    lda taskGfxReuHi,x
    adc scrollSeedBuf+1
    sta kArg+1
    lda kArg+1
    clc
    adc #>$2000
    sta kArg+1
    lda taskGfxReuBank,x
    adc #0
    sta kArg+2
    lda #SCREEN_COLS; sta kArg+3
    lda #0; sta kArg+4
    lda taskGfxBg0,x
    jsr kReuFillReu             // blank row B matrix (bg0)

    // --- char shadow (region-aware) ---
    lda taskShadowFlag,x
    beq sssDone
    jsr pcShadowScroll
sssDone:
    rts

.label scrollSlotEnd = *
