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
    sec
    sbc kArg+6
    sta kArg+7                  // R = B - T
    jsr scrollRows              // scrSrc/scrDst/scrBlank, and consume the direction
    //  The char shadow mirrors the same three rows, and R is needed to move them,
    //  so it is done HERE -- the row loop below counts kArg+7 down to zero.
    lda taskShadowFlag,x
    beq sssNoShadow
    jsr pcShadowScroll          // X = curTask, preserved
sssNoShadow:
    ldy kArg+7
    bne sssMove                 // R != 0 -> do the move
    jmp sssClear                // R == 0 -> blank the vacated row only (far)
sssMove:

    ldy scrSrc                  // first source row: T+1 scrolling up, T down
    sty sssRow
sssLoop:
    // --- pixel row r -> row r+step (320B via scratchBuf) ---
    ldy sssRow
    lda gfxRowLutLo,y           // r*320 lo ($2000 lo byte = 0)
    sta scrollSeedBuf
    lda gfxRowLutHi,y           // r*320 hi + $20
    sec
    sbc #>$2000
    sta scrollSeedBuf+1         // r*320
    jsr sssAddr                 // kArg = slot + r*320 (source)
    lda #<$0140; sta kArg+3
    lda #>$0140; sta kArg+4
    lda #0; sta kArg+5
    lda #<$C400; sta kPtr
    lda #>$C400; sta kPtr+1
    jsr kReuToC64               // slot source -> scratchBuf
    ldy sssRow
    tya
    clc
    adc scrollStep              // the destination row
    tay
    lda gfxRowLutLo,y
    sta scrollSeedBuf
    lda gfxRowLutHi,y
    sec
    sbc #>$2000
    sta scrollSeedBuf+1
    jsr sssAddr                 // kArg = slot + (r+step)*320 (dest)
    lda #<$0140; sta kArg+3
    lda #>$0140; sta kArg+4
    lda #0; sta kArg+5
    lda #<$C400; sta kPtr
    lda #>$C400; sta kPtr+1
    jsr kReuFromC64             // scratchBuf -> slot dest

    // --- matrix row r -> row r+step (40B via scratchBuf) ---
    ldy sssRow
    lda gfxCellRowLutLo,y
    sta scrollSeedBuf
    lda gfxCellRowLutHi,y
    sta scrollSeedBuf+1         // r*40
    jsr sssAddrPlane            // kArg = slot + $2000 + r*40 (source)
    lda #SCREEN_COLS; sta kArg+3
    lda #0; sta kArg+4
    sta kArg+5
    lda #<$C400; sta kPtr
    lda #>$C400; sta kPtr+1
    jsr kReuToC64
    ldy sssRow
    tya
    clc
    adc scrollStep
    tay
    lda gfxCellRowLutLo,y
    sta scrollSeedBuf
    lda gfxCellRowLutHi,y
    sta scrollSeedBuf+1         // (r+step)*40
    jsr sssAddrPlane            // kArg = slot + $2000 + (r+step)*40 (dest)
    lda #SCREEN_COLS; sta kArg+3
    lda #0; sta kArg+4
    sta kArg+5
    lda #<$C400; sta kPtr
    lda #>$C400; sta kPtr+1
    jsr kReuFromC64

    // --- next source row, R rows in all ---
    ldy sssRow
    tya
    clc
    adc scrollStep
    sta sssRow
    dec kArg+7
    beq sssClear                // forward and close: the loop itself is behind us
    jmp sssLoop

sssClear:
    // --- blank the vacated pixel row (B scrolling up, T down) ---
    ldy scrBlank
    lda gfxRowLutLo,y           // B*320 lo ($2000 lo byte = 0)
    sta scrollSeedBuf
    lda gfxRowLutHi,y           // B*320 hi + $20
    sec
    sbc #>$2000
    sta scrollSeedBuf+1         // B*320
    jsr sssAddr                 // kArg = slot + B*320
    lda #<$0140; sta kArg+3
    lda #>$0140; sta kArg+4
    lda #0
    jsr kReuFillReu             // blank that row's pixels (X preserved)

    // --- and its matrix row ---
    ldy scrBlank
    lda gfxCellRowLutLo,y
    sta scrollSeedBuf
    lda gfxCellRowLutHi,y
    sta scrollSeedBuf+1         // B*40
    jsr sssAddrPlane            // kArg = slot + $2000 + B*40
    lda #SCREEN_COLS; sta kArg+3
    lda #0; sta kArg+4
    lda taskGfxBg0,x
    jsr kReuFillReu             // blank that row's matrix (bg0)
sssDone:
    rts

// --- sssAddr -- kArg/kArg+1/kArg+2 = slot base + a byte offset ---
//  In:  scrollSeedBuf/scrollSeedBuf+1 = offset within the surface's REU block
//  Out: kArg/kArg+1 = REU address, kArg+2 = bank. X (curTask) preserved; Y
//       preserved, so a caller's row index survives.
//  Clobbers: A, kArg..kArg+2
sssAddr:
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
    rts

// --- sssAddrPlane -- sssAddr, then the $2000 cell-matrix plane ---
//  The matrix sits $2000 into the slot, so the plane goes on the address hi
//  byte. A ripple out of that add is the bank and must be carried into it: a
//  slot base is slot x $2800, so its hi byte can be $E0 or more and the plane
//  takes it past $FF.
//  Clobbers: A, kArg..kArg+2
sssAddrPlane:
    jsr sssAddr
    lda kArg+1
    clc
    adc #>$2000
    sta kArg+1
    bcc sapDone
    inc kArg+2
sapDone:
    rts

// ==========================================
//  Scroll direction, and the rows a scroll uses
// ==========================================
//  A scroll moves a region's rows one place up or one place down. Up moves
//  rows T+1..B to T..B-1 and blanks B; down moves T..B-1 to T+1..B and blanks
//  T. Three surfaces follow that move -- the framebuffer or the slot, and the
//  char shadow -- and all three work from the same three row numbers, derived
//  once per scroll by scrollRows:
//
//      scrSrc    the first row to move FROM
//      scrDst    the first row to move TO
//      scrBlank  the row the content vacated (blanked after the move)
//
//  scrollStep is the signed row offset from a source row to its destination:
//  $FF for an up scroll, $01 for a down one. It is a ONE-SHOT LATCH -- the
//  default is $FF and scrollRows puts it back there as it reads it, so the
//  value only exists inside an SEI'd window and no task can inherit it. A
//  direction left standing would turn every later up scroll into a down one.
scrollStep:   .byte $FF
scrSrc:       .byte 0
scrDst:       .byte 0
scrBlank:     .byte 0
sssRow:       .byte 0

// --- scrollRows -- derive scrSrc/scrDst/scrBlank from the region ---
//  In:  kArg+6 = T, kArg+7 = R (= B - T), scrollStep = the direction.
//  Out: scrSrc, scrDst, scrBlank; scrollStep back to $FF. X and Y preserved.
//  Clobbers: A.
scrollRows:
    tya
    pha
    ldy scrollStep              // the direction, read before the latch is cleared
    lda #$ff
    sta scrollStep              // consumed: back to the up-scroll default
    lda kArg+6                  // T
    cpy #$80                    // $FF (up) is >= $80; $01 (down) is not
    bcs srUp
    // --- down: source T, dest T+1, blank T ---
    sta scrSrc
    sta scrBlank
    clc
    adc #1
    sta scrDst
    pla
    tay
    rts
srUp:
    // --- up: source T+1, dest T, blank B ---
    sta scrDst
    clc
    adc #1
    sta scrSrc
    lda kArg+6
    clc
    adc kArg+7                  // T + R = B
    sta scrBlank
    pla
    tay
    rts

// --- scrollRegionDown -- kScrollRegionDown entry ($FFF6) ---
//  Scroll the current task's region DOWN one row, the mirror of kScrollScreen:
//  rows T..B-1 move to T+1..B and row T is blanked. No arguments, like
//  kScrollScreen -- the region is the task's own, and the cursor is left where
//  the caller put it, because there is no wrap to clamp.
//  The whole thing runs under SEI: that closes the window in which this task
//  could be killed with the latch set, and it is what lets the entry decide
//  whether a scroll will happen at all before it sets the latch.
//  Clobbers A, X, Y, kPtr (kScrollScreen's own contract).
scrollRegionDown:
    sei
    ldx curTask
    lda screenAlloc,x
    beq srdDone                 // no surface -> nothing consumes a latch: do not set one
    lda #1
    sta scrollStep
    jsr scrollScreen            // SEI'd inside; reaches a routine that consumes it
srdDone:
    clc
    cli
    rts

.label scrollSlotEnd = *
